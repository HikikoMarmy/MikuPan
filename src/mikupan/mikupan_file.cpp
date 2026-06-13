#include "typedefs.h"
#include "mikupan_file.h"
#include "mikupan_config.h"
#include "gs/mikupan_texture_manager.h"
#include "mikupan_logging.h"
#include "spdlog/spdlog.h"
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <cctype>
#include <limits>
#include <cstring>
#include <condition_variable>
#include <mutex>

extern "C" {
#include "mikupan_memory.h"
}

static inline std::vector<int> file_loaded_address;

static std::mutex missing_data_dialog_mutex;
static std::condition_variable missing_data_dialog_cv;
static bool missing_data_dialog_requested = false;
static bool missing_data_dialog_open = false;
static bool missing_data_dialog_selected = false;
static std::string missing_data_path;

static std::filesystem::path MikuPan_GetApplicationBasePath()
{
    const char *base = SDL_GetBasePath();
    if (base != nullptr && base[0] != '\0')
    {
        return std::filesystem::path(base);
    }

    return std::filesystem::path(".");
}

static std::filesystem::path MikuPan_GetDataRoot()
{
    const char *folder = mikupan_configuration.data_folder;
    if (folder != nullptr && folder[0] != '\0')
    {
        return std::filesystem::path(folder);
    }

    return MikuPan_GetApplicationBasePath();
}

static std::string MikuPan_ToLowerPathString(const std::filesystem::path& path)
{
    std::string value = path.lexically_normal().generic_string();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

static bool MikuPan_IsExcludedRuntimeAsset(const std::filesystem::path& path)
{
    std::string value = MikuPan_ToLowerPathString(path);
    while (value.rfind("./", 0) == 0)
    {
        value.erase(0, 2);
    }

    return value.rfind("resources/", 0) == 0
        || value.find("/resources/") != std::string::npos
        || value == "mikupan.ini"
        || (value.size() >= 12
            && value.compare(value.size() - 12, 12, "/mikupan.ini") == 0);
}

static bool MikuPan_IsPathInsideDataRoot(const std::filesystem::path& path)
{
    std::string root =
        MikuPan_ToLowerPathString(std::filesystem::absolute(MikuPan_GetDataRoot()));
    std::string target =
        MikuPan_ToLowerPathString(std::filesystem::absolute(path));

    while (root.size() > 1 && root.back() == '/')
    {
        root.pop_back();
    }

    if (target == root)
    {
        return true;
    }

    root += '/';
    return target.rfind(root, 0) == 0;
}

static bool MikuPan_ShouldPromptForMissingDataFile(
    const std::filesystem::path& path)
{
    const std::string filename =
        MikuPan_ToLowerPathString(path.filename());

    if (filename == "img_hd.bin" || filename == "img_bd.bin")
    {
        return true;
    }

    if (MikuPan_IsExcludedRuntimeAsset(path))
    {
        return false;
    }

    return MikuPan_IsPathInsideDataRoot(path);
}

static void MikuPan_ApplySelectedDataFolder(const std::string& folder)
{
    if (folder.empty())
    {
        return;
    }

    std::strncpy(mikupan_configuration.data_folder,
                 folder.c_str(),
                 sizeof(mikupan_configuration.data_folder) - 1);
    mikupan_configuration
        .data_folder[sizeof(mikupan_configuration.data_folder) - 1] = '\0';

    if (!MikuPan_SaveConfiguration(nullptr))
    {
        spdlog::error("Failed to save selected data folder: {}", folder);
    }
    else
    {
        spdlog::info("Selected data folder: {}", folder);
    }
}

static bool MikuPan_MakePathRelativeToRoot(
    const std::filesystem::path& path,
    const std::filesystem::path& root,
    std::filesystem::path& relative)
{
    std::error_code ec;
    std::filesystem::path abs_path = std::filesystem::absolute(path, ec);
    if (ec)
    {
        return false;
    }

    std::filesystem::path abs_root = std::filesystem::absolute(root, ec);
    if (ec)
    {
        return false;
    }

    relative = abs_path.lexically_normal().lexically_relative(
        abs_root.lexically_normal());
    if (relative.empty())
    {
        return false;
    }

    for (const auto& part : relative)
    {
        if (part == "..")
        {
            return false;
        }
    }

    return true;
}

static std::filesystem::path MikuPan_RebasePathAfterDataFolderSelection(
    const std::filesystem::path& path,
    const std::filesystem::path& old_root)
{
    const std::string filename =
        MikuPan_ToLowerPathString(path.filename());
    if (filename == "img_hd.bin" || filename == "img_bd.bin")
    {
        return MikuPan_GetDataRoot() / path.filename();
    }

    std::filesystem::path relative;
    if (MikuPan_MakePathRelativeToRoot(path, old_root, relative))
    {
        return MikuPan_GetDataRoot() / relative;
    }

    return path;
}

static void SDLCALL MikuPan_ServiceMissingDataFolderDialogOnMainThread(
    void *userdata)
{
    (void)userdata;
    MikuPan_ServiceMissingDataFolderDialog();
}

static bool MikuPan_WaitForMissingDataFolderDialogOnMainThread()
{
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(missing_data_dialog_mutex);
            if (!missing_data_dialog_open && !missing_data_dialog_requested)
            {
                return missing_data_dialog_selected;
            }
        }

        MikuPan_ServiceMissingDataFolderDialog();
        SDL_PumpEvents();
        SDL_Delay(10);
    }
}

static bool MikuPan_RequestMissingDataFolderDialog(
    const std::filesystem::path& path)
{
    if (!MikuPan_ShouldPromptForMissingDataFile(path))
    {
        return false;
    }

    if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
    {
        return false;
    }

    const bool is_main_thread = SDL_IsMainThread();

    {
        std::unique_lock<std::mutex> lock(missing_data_dialog_mutex);
        if (missing_data_dialog_open || missing_data_dialog_requested)
        {
            if (!is_main_thread)
            {
                missing_data_dialog_cv.wait(lock, [] {
                    return !missing_data_dialog_open
                        && !missing_data_dialog_requested;
                });
                return missing_data_dialog_selected;
            }

            lock.unlock();
            return MikuPan_WaitForMissingDataFolderDialogOnMainThread();
        }

        missing_data_path = path.lexically_normal().generic_string();
        missing_data_dialog_requested = true;
        missing_data_dialog_selected = false;
    }

    if (is_main_thread)
    {
        MikuPan_ServiceMissingDataFolderDialog();
    }
    else if (!SDL_RunOnMainThread(
                 MikuPan_ServiceMissingDataFolderDialogOnMainThread,
                 nullptr,
                 false))
    {
        {
            std::lock_guard<std::mutex> lock(missing_data_dialog_mutex);
            missing_data_dialog_requested = false;
            missing_data_dialog_selected = false;
        }
        missing_data_dialog_cv.notify_all();
        return false;
    }

    if (is_main_thread)
    {
        return MikuPan_WaitForMissingDataFolderDialogOnMainThread();
    }
    else
    {
        std::unique_lock<std::mutex> lock(missing_data_dialog_mutex);
        missing_data_dialog_cv.wait(lock, [] {
            return !missing_data_dialog_open && !missing_data_dialog_requested;
        });
        return missing_data_dialog_selected;
    }
}

static void SDLCALL MikuPan_MissingDataFolderSelected(
    void *userdata, const char *const *filelist, int filter)
{
    (void)userdata;
    (void)filter;

    std::string selected_folder;
    if (filelist != nullptr && filelist[0] != nullptr
        && filelist[0][0] != '\0')
    {
        selected_folder = filelist[0];
        MikuPan_ApplySelectedDataFolder(selected_folder);
    }

    {
        std::lock_guard<std::mutex> lock(missing_data_dialog_mutex);
        missing_data_dialog_selected = !selected_folder.empty();
        missing_data_dialog_open = false;
    }
    missing_data_dialog_cv.notify_all();
}

extern "C" void MikuPan_ServiceMissingDataFolderDialog()
{
    std::string missing_path_to_show;

    {
        std::lock_guard<std::mutex> lock(missing_data_dialog_mutex);

        if (missing_data_dialog_requested && !missing_data_dialog_open)
        {
            missing_path_to_show = missing_data_path;
            missing_data_dialog_requested = false;
            missing_data_dialog_open = true;
        }
    }

    if (missing_path_to_show.empty())
    {
        return;
    }

    const std::string message =
        "The game data file was not found:\n\n" + missing_path_to_show
        + "\n\nSelect the folder that contains the Fatal Frame data files. "
          "The selected folder will be saved to the configuration file.";

    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             "File not found",
                             message.c_str(),
                             nullptr);

    const char *start = mikupan_configuration.data_folder[0] != '\0'
                            ? mikupan_configuration.data_folder
                            : nullptr;
    SDL_ShowOpenFolderDialog(MikuPan_MissingDataFolderSelected,
                             nullptr,
                             nullptr,
                             start,
                             false);
}

void MikuPan_LoadImgHdFile()
{
    auto data_path = MikuPan_GetDataRoot() / "IMG_HD.BIN";
    if (!std::filesystem::exists(data_path))
    {
        const auto old_root = MikuPan_GetDataRoot();
        if (MikuPan_RequestMissingDataFolderDialog(data_path))
        {
            data_path =
                MikuPan_RebasePathAfterDataFolderSelection(data_path, old_root);
        }

        if (!std::filesystem::exists(data_path))
        {
            return;
        }
    }

    const auto path = data_path.generic_string();
    MikuPan_ReadFullFile(path.c_str(),
        static_cast<char *>(MikuPan_GetHostPointer(ImgHdAddress)));
}

void MikuPan_ReadFullFile(const char *filename, char *buffer)
{
    std::filesystem::path path_filename(filename);

    if (!std::filesystem::exists(path_filename))
    {
        const auto old_root = MikuPan_GetDataRoot();
        if (MikuPan_RequestMissingDataFolderDialog(path_filename))
        {
            path_filename =
                MikuPan_RebasePathAfterDataFolderSelection(path_filename,
                                                           old_root);
        }

        if (!std::filesystem::exists(path_filename))
        {
            return;
        }
    }

    if (buffer == nullptr)
    {
        return;
    }

    auto file_size = std::filesystem::file_size(path_filename);
    std::ifstream infile(path_filename, std::ios::binary);
    infile.read(buffer, file_size);
    infile.close();
}

void MikuPan_NotifyPs2MemoryLoad(int ps2_address)
{
    if (ps2_address < 0)
    {
        return;
    }

    if (std::find(file_loaded_address.begin(), file_loaded_address.end(),
                  ps2_address)
        != file_loaded_address.end())
    {
        MikuPan_RequestFlushTextureCache();
        return;
    }

    file_loaded_address.push_back(ps2_address);
}

void MikuPan_ReadFileInArchive(int sector, int size, u_int *address)
{
    auto archive = MikuPan_GetDataRoot() / "IMG_BD.BIN";
    if (!std::filesystem::exists(archive))
    {
        spdlog::critical("IMG_BD.BIN not found!");
        const auto old_root = MikuPan_GetDataRoot();
        if (MikuPan_RequestMissingDataFolderDialog(archive))
        {
            archive =
                MikuPan_RebasePathAfterDataFolderSelection(archive, old_root);
        }

        if (!std::filesystem::exists(archive))
        {
            return;
        }
    }

    if (address == nullptr)
    {
        spdlog::critical("File loading address is NULL, abort load request!");
        return;
    }

    if (!MikuPan_IsPs2MemoryPointer((int64_t) address))
    {
        spdlog::critical("File loading address is not in PS2 memory range!");
        return;
    }

    auto ps2_address = MikuPan_GetPs2OffsetFromHostPointer(address);

    if (std::find(file_loaded_address.begin(), file_loaded_address.end(),
                  ps2_address)
        != file_loaded_address.end())
    {
        MikuPan_RequestFlushTextureCache();
    }

    file_loaded_address.push_back(ps2_address);
    info_log("PS2 Address 0x%x", ps2_address);

    std::ifstream infile(archive, std::ios::binary);
    infile.seekg(sector * 0x800, std::ios::beg);
    infile.read(reinterpret_cast<char *>(address), size);

    infile.close();
}

void MikuPan_BufferFile(int sector, int size, int64_t address)
{
    auto archive = MikuPan_GetDataRoot() / "IMG_BD.BIN";
    if (!std::filesystem::exists(archive))
    {
        const auto old_root = MikuPan_GetDataRoot();
        if (MikuPan_RequestMissingDataFolderDialog(archive))
        {
            archive =
                MikuPan_RebasePathAfterDataFolderSelection(archive, old_root);
        }

        if (!std::filesystem::exists(archive))
        {
            return;
        }
    }

    if (address == 0)
    {
        return;
    }

    if ((int64_t *) *(int64_t *) address != nullptr)
    {
        free((int64_t *) *(int64_t *) address);
    }

    void *file_ptr = malloc(size);

    *((int64_t *) address) = (int64_t) file_ptr;

    std::ifstream infile(archive, std::ios::binary);
    infile.seekg(sector * 0x800, std::ios::beg);
    infile.read(static_cast<char *>(file_ptr), size);

    infile.close();
}

u_char MikuPan_ReadFile(const char *filename, void *buffer, int size)
{
    auto relative_path = std::filesystem::path(MikuPan_GetRelativePath(filename));

    if (!std::filesystem::exists(relative_path))
    {
        return 0;
    }

    std::ifstream inFile;
    inFile.open(relative_path);
    inFile.read(static_cast<char *>(buffer), size);
    inFile.close();
    return inFile.bad() == 0;
}

u_char MikuPan_WriteFile(const char *filename, const void *buffer, int size)
{
    auto relative_path = std::filesystem::path(MikuPan_GetRelativePath(filename));
    const auto parent = relative_path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent))
    {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error)
        {
            spdlog::error("Failed to create user data directory {}: {}",
                          parent.generic_string(), error.message());
            return 0;
        }
    }

    std::ofstream outFile;
    outFile.open(relative_path, std::ios::binary);
    outFile.write(static_cast<const char *>(buffer), size);
    outFile.close();

    return outFile.bad() == 0;
}

u_int MikuPan_GetFileSize(const char *filename)
{
    std::filesystem::path path(filename);
    if (!std::filesystem::exists(path))
    {
        const auto old_root = MikuPan_GetDataRoot();
        if (MikuPan_RequestMissingDataFolderDialog(path))
        {
            path = MikuPan_RebasePathAfterDataFolderSelection(path, old_root);
        }

        if (!std::filesystem::exists(path))
        {
            return 0;
        }
    }

    return std::filesystem::file_size(path);
}

bool MikuPan_ResolveCdPath(const char* path, char* buffer, size_t buffer_size)
{
    if (!path) {
        spdlog::error("MikuPan_ResolveCdPath: path is null");
        return false;
    }
    
    if (!buffer) {
        spdlog::error("MikuPan_ResolveCdPath: buffer is null");
        return false;
    }
    
    if (buffer_size == 0) {
        spdlog::error("MikuPan_ResolveCdPath: buffer_size is 0");
        return false;
    }
    
    std::string s(path);
    
    auto ltrim = [](std::string& x) {
        x.erase(x.begin(), std::find_if(x.begin(), x.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
    };
    
    auto rtrim = [](std::string& x) {
        x.erase(std::find_if(x.rbegin(), x.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), x.end());
    };
    
    ltrim(s);
    rtrim(s);
    
    if (auto semi = s.find(';'); semi != std::string::npos) {
        s.erase(semi);
    }
    
    std::replace(s.begin(), s.end(), '\\', '/');
    
    if (s.rfind("./", 0) == 0) {
        s.erase(0, 2);
    } else if (auto colon = s.find(':'); colon != std::string::npos) {
        s.erase(0, colon + 1);
        if (!s.empty() && s.front() == '/') {
            s.erase(0, 1);
        }
    }

    while (!s.empty() && s.front() == '/') {
        s.erase(0, 1);
    }

    if (!s.empty()) {
        const std::filesystem::path relative_data_path(s);
        std::filesystem::path resolved_path =
            MikuPan_GetDataRoot() / relative_data_path;
        if (!std::filesystem::exists(resolved_path)) {
            if (MikuPan_RequestMissingDataFolderDialog(resolved_path)) {
                resolved_path = MikuPan_GetDataRoot() / relative_data_path;
            }
        }
        s = resolved_path.generic_string();
    }

    if (s.length() + 1 > buffer_size) {
        spdlog::error("MikuPan_ResolveCdPath: Buffer too small. Need {} bytes, got {}", 
                      s.length() + 1, buffer_size);
        return false;
    }
    
    std::strcpy(buffer, s.c_str());
    
    return true;
}

bool MikuPan_ResolveBasePath(const char* path, char* buffer, size_t buffer_size)
{
    if (!path)
    {
        spdlog::error("MikuPan_ResolveBasePath: path is null");
        return false;
    }

    if (!buffer)
    {
        spdlog::error("MikuPan_ResolveBasePath: buffer is null");
        return false;
    }

    if (buffer_size == 0)
    {
        spdlog::error("MikuPan_ResolveBasePath: buffer_size is 0");
        return false;
    }

    std::filesystem::path resolved_path(path);
    if (resolved_path.is_relative())
    {
        resolved_path = MikuPan_GetApplicationBasePath()
            / resolved_path.relative_path();
    }

    const std::string resolved =
        resolved_path.lexically_normal().generic_string();
    if (resolved.length() + 1 > buffer_size)
    {
        spdlog::error(
            "MikuPan_ResolveBasePath: Buffer too small. Need {} bytes, got {}",
            resolved.length() + 1, buffer_size);
        return false;
    }

    std::strcpy(buffer, resolved.c_str());
    return true;
}

u_char MikuPan_FolderExists(const char *folder)
{
    auto relative_path = std::filesystem::path(MikuPan_GetRelativePath(folder));

    if (!std::filesystem::is_directory(relative_path))
    {
        relative_path = relative_path.remove_filename();
    }

    return std::filesystem::exists(relative_path);
}

u_char MikuPan_CreateFolder(const char *folder)
{
    auto relative_path = std::filesystem::path(MikuPan_GetRelativePath(folder));

    if (!std::filesystem::exists(relative_path))
    {
        return std::filesystem::create_directories(relative_path);
    }

    return 1;
}

int MikuPan_GetListFiles(const char *folder, MikuPan_McTblGetDir *table)
{
    auto relative_path = std::filesystem::path(MikuPan_GetRelativePath(folder));

    if (!std::filesystem::is_directory(relative_path))
    {
        relative_path = relative_path.remove_filename();
    }

    int i = 0;
    for (const auto& entry : std::filesystem::directory_iterator(relative_path))
    {
        table[i].FileSizeByte = entry.file_size();
        strncpy(reinterpret_cast<char *>(table[i].EntryName), entry.path().filename().generic_u8string().c_str(), sizeof(table[i].EntryName));

        if (i++, i > 18)
        {
            break;
        }
    }

    return i;
}

std::string MikuPan_GetRelativePath(const char *path)
{
    char resolved_path[1024];
    if (MikuPan_ResolveUserPath(path, resolved_path, sizeof(resolved_path)))
    {
        return resolved_path;
    }

    return std::filesystem::path(path).relative_path().generic_u8string();
}
