#include "typedefs.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "SDL3/SDL_dialog.h"
#include "SDL3/SDL_hints.h"
#include "SDL3/SDL_platform.h"
#include "ingame/camera/camera.h"
#include "main/glob.h"
#include "mikupan/mikupan_config.h"
#include "mikupan/mikupan_controller.h"
#include "mikupan/mikupan_file_c.h"
#include "mikupan/mikupan_screenshot.h"
#include "mikupan/mikupan_utils.h"
#include "mikupan/rendering/mikupan_gpu.h"
#include "mikupan/rendering/mikupan_renderer.h"
#include "mikupan/ui/mikupan_ui.h"
#include "mikupan_ui_cheats.h"
#include "mikupan_ui_debug.h"
#include "mikupan_version.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -- Backend wrappers (defined in mikupan_ui.cpp) ----------------------------
void MikuPan_ImGui_ImplInit(SDL_Window* window);
void MikuPan_ImGui_ImplShutdown(void);
void MikuPan_ImGui_ImplNewFrame(void);
void MikuPan_ImGui_ImplProcessEvent(SDL_Event* event);
void MikuPan_ImGui_ImplRenderDrawData(void);

// -- State -------------------------------------------------------------------
const int msaa_list[] = {0, 2, 4, 8};
int show_menu_bar = 0;
static int show_controller_remap = 0;
static int msaa_samples = 0;
static int render_resolution_width = 1920;
static int render_resolution_height = 1080;

#define MIKUPAN_MAX_RESOLUTIONS 96
#define MIKUPAN_MAX_PS2_RESOLUTION_SCALE 6

static MikuPan_Resolution resolution_list[MIKUPAN_MAX_RESOLUTIONS];
static char resolution_labels[MIKUPAN_MAX_RESOLUTIONS][48];
static const char* resolution_label_ptrs[MIKUPAN_MAX_RESOLUTIONS];
static int resolution_count = 0;
static int resolution_selected = 0;

#define MIKUPAN_MAX_GPU_DRIVERS 8
static char gpu_driver_names[MIKUPAN_MAX_GPU_DRIVERS + 1][32];
static char gpu_driver_labels[MIKUPAN_MAX_GPU_DRIVERS + 1][64];
static int gpu_driver_supported[MIKUPAN_MAX_GPU_DRIVERS + 1];
static int gpu_driver_count = 0;
static int gpu_driver_selected = 0;
static char config_save_status[128] = {0};
static SDL_Window* ui_window = NULL;
static int is_fullscreen = 0;
static int window_mode = 0; /* MikuPan_WindowMode: 0=windowed, 1=fullscreen, 2=borderless */

const int no_navigation_window =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration
    | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize
    | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

static float brightness = 1.0f;
static float gamma_value = 1.0f;

#define MIKUPAN_UI_THEME_COUNT 6
#define MIKUPAN_UI_FONT_COUNT 2

static const char* theme_labels[MIKUPAN_UI_THEME_COUNT] = {
    "Moonlit Blue", "Ghost Cyan", "Crimson",
    "FF1 Ritual",   "Mist Teal",  "Sepia Photo",
};

static const char* font_labels[MIKUPAN_UI_FONT_COUNT] = {
    "ImGui Default Monospace",
    "Century Old Style",
};

static ImFont* ui_fonts[MIKUPAN_UI_FONT_COUNT] = {0};
static float ui_display_scale = 1.0f;
static float ui_font_default_size = 13.0f;
static float ui_font_regular_size = 14.0f;

#define MIKUPAN_CRT_DEFAULTS                                                   \
    {0,     1.0f,  0.08f, 0.02f, 0.18f, 1.0f,  1.6f,  0.22f, 1.0f,             \
     0.25f, 0.78f, 0.75f, 0.15f, 0.75f, 0.02f, 0.02f, 0.08f}

static const MikuPan_ConfigCrt crt_defaults = MIKUPAN_CRT_DEFAULTS;
static MikuPan_ConfigCrt crt_settings = MIKUPAN_CRT_DEFAULTS;

static void MikuPan_UiStoreRuntimeConfiguration(void);

static int MikuPan_ClampThemeIndex(int theme)
{
    if (theme < 0 || theme >= MIKUPAN_UI_THEME_COUNT)
    {
        return 0;
    }

    return theme;
}

static int MikuPan_ClampFontIndex(int font)
{
    if (font < 0 || font >= MIKUPAN_UI_FONT_COUNT)
    {
        return 1;
    }

    return font;
}

static float MikuPan_CalculateUiDisplayScale(SDL_Window* window)
{
    SDL_DisplayID display = 0;
    const SDL_DisplayMode* mode = NULL;
    float content_scale = 1.0f;
    float resolution_scale = 1.0f;
    float scale;

    if (window != NULL)
    {
        display = SDL_GetDisplayForWindow(window);
        content_scale = SDL_GetWindowDisplayScale(window);
    }

    if (display == 0)
    {
        display = SDL_GetPrimaryDisplay();
    }

    if (display != 0)
    {
        mode = SDL_GetCurrentDisplayMode(display);
        if (content_scale <= 0.0f)
        {
            content_scale = SDL_GetDisplayContentScale(display);
        }
    }

    if (mode != NULL && mode->w > 0 && mode->h > 0)
    {
        const float sx = (float) mode->w / 1920.0f;
        const float sy = (float) mode->h / 1080.0f;
        resolution_scale = sx < sy ? sx : sy;
    }

    if (content_scale <= 0.0f)
    {
        content_scale = 1.0f;
    }

    scale = content_scale > resolution_scale ? content_scale : resolution_scale;
    return MikuPan_ClampFloat(scale, 0.85f, 2.25f);
}

static void MikuPan_ClampCrtSettings(MikuPan_ConfigCrt* crt)
{
    crt->enabled = crt->enabled ? 1 : 0;
    crt->strength = MikuPan_ClampFloat(crt->strength, 0.0f, 1.0f);
    crt->curvature = MikuPan_ClampFloat(crt->curvature, 0.0f, 0.30f);
    crt->overscan = MikuPan_ClampFloat(crt->overscan, 0.0f, 0.12f);
    crt->scanline_strength =
        MikuPan_ClampFloat(crt->scanline_strength, 0.0f, 1.0f);
    crt->scanline_scale = MikuPan_ClampFloat(crt->scanline_scale, 0.25f, 3.0f);
    crt->scanline_thickness =
        MikuPan_ClampFloat(crt->scanline_thickness, 0.5f, 4.0f);
    crt->mask_strength = MikuPan_ClampFloat(crt->mask_strength, 0.0f, 1.0f);
    crt->mask_scale = MikuPan_ClampFloat(crt->mask_scale, 0.5f, 4.0f);
    crt->vignette_strength =
        MikuPan_ClampFloat(crt->vignette_strength, 0.0f, 1.0f);
    crt->vignette_size = MikuPan_ClampFloat(crt->vignette_size, 0.25f, 1.25f);
    crt->chroma_offset = MikuPan_ClampFloat(crt->chroma_offset, 0.0f, 3.0f);
    crt->blend_strength = MikuPan_ClampFloat(crt->blend_strength, 0.0f, 1.0f);
    crt->blend_radius = MikuPan_ClampFloat(crt->blend_radius, 0.0f, 3.0f);
    crt->noise_strength = MikuPan_ClampFloat(crt->noise_strength, 0.0f, 0.15f);
    crt->flicker_strength =
        MikuPan_ClampFloat(crt->flicker_strength, 0.0f, 0.10f);
    crt->glow_strength = MikuPan_ClampFloat(crt->glow_strength, 0.0f, 0.50f);
}

static void
MikuPan_ClampThirdPersonCameraSettings(MikuPan_ConfigThirdPersonCamera* tps)
{
    tps->enabled = tps->enabled ? 1 : 0;
    tps->distance = MikuPan_ClampFloat(tps->distance, 100.0f, 2500.0f);
    tps->height = MikuPan_ClampFloat(tps->height, 0.0f, 1400.0f);
    tps->side = MikuPan_ClampFloat(tps->side, -600.0f, 600.0f);
    tps->look_ahead = MikuPan_ClampFloat(tps->look_ahead, 100.0f, 2500.0f);
    tps->interest_height =
        MikuPan_ClampFloat(tps->interest_height, -400.0f, 1200.0f);
    tps->fov_deg = MikuPan_ClampFloat(tps->fov_deg, 20.0f, 90.0f);
}

static void MikuPan_ApplyThirdPersonCameraConfiguration(void)
{
    MikuPan_ConfigThirdPersonCamera* tps =
        &mikupan_configuration.third_person_camera;

    MikuPan_ClampThirdPersonCameraSettings(tps);
    camera_third_person_enabled = tps->enabled;
    camera_third_person_distance = tps->distance;
    camera_third_person_height = tps->height;
    camera_third_person_side = tps->side;
    camera_third_person_look_ahead = tps->look_ahead;
    camera_third_person_interest_height = tps->interest_height;
    camera_third_person_fov_deg = tps->fov_deg;
}

static void MikuPan_UiSaveConfiguration(void)
{
    MikuPan_UiStoreRuntimeConfiguration();
    if (MikuPan_SaveConfiguration(NULL))
    {
        snprintf(config_save_status, sizeof(config_save_status),
                 "Saved configuration");
    }
    else
    {
        snprintf(config_save_status, sizeof(config_save_status),
                 "Failed to save configuration");
    }
}

static void MikuPan_UiStoreRuntimeConfiguration(void)
{
    mikupan_configuration.renderer.render.width = render_resolution_width;
    mikupan_configuration.renderer.render.height = render_resolution_height;
    mikupan_configuration.renderer.window_mode = window_mode;
    is_fullscreen = (window_mode != MIKUPAN_WINDOW_WINDOWED);
    mikupan_configuration.renderer.is_fullscreen = is_fullscreen;
    mikupan_configuration.renderer.lighting_mode = MikuPan_GetMeshLightingMode();
    mikupan_configuration.renderer.msaa_index = msaa_samples;
    mikupan_configuration.renderer.shadow_resolution =
        MikuPan_GetShadowResolution();
    mikupan_configuration.renderer.brightness = brightness;
    mikupan_configuration.renderer.gamma = gamma_value;
    mikupan_configuration.selected_theme =
        MikuPan_ClampThemeIndex(mikupan_configuration.selected_theme);
    mikupan_configuration.selected_font =
        MikuPan_ClampFontIndex(mikupan_configuration.selected_font);
    mikupan_configuration.crt = crt_settings;
    mikupan_configuration.third_person_camera.enabled =
        camera_third_person_enabled ? 1 : 0;
    mikupan_configuration.third_person_camera.distance =
        camera_third_person_distance;
    mikupan_configuration.third_person_camera.height =
        camera_third_person_height;
    mikupan_configuration.third_person_camera.side = camera_third_person_side;
    mikupan_configuration.third_person_camera.look_ahead =
        camera_third_person_look_ahead;
    mikupan_configuration.third_person_camera.interest_height =
        camera_third_person_interest_height;
    mikupan_configuration.third_person_camera.fov_deg =
        camera_third_person_fov_deg;
    MikuPan_ClampThirdPersonCameraSettings(
        &mikupan_configuration.third_person_camera);
    mikupan_configuration.input.selected_gamepad_index =
        MikuPan_ControllerGetPreferredGamepadIndex();
    MikuPan_ControllerStoreBindingsToConfig();
}

// -- Public API --------------------------------------------------------------

static int CompareResolutionDesc(const void* a, const void* b)
{
    const MikuPan_Resolution* ra = (const MikuPan_Resolution*) a;
    const MikuPan_Resolution* rb = (const MikuPan_Resolution*) b;
    int area_a = ra->width * ra->height;
    int area_b = rb->width * rb->height;
    return area_b - area_a;
}

static void MikuPan_AspectRatioStr(int w, int h, char* buf, int buf_size)
{
    int a = w, b = h;
    while (b)
    {
        int t = b;
        b = a % b;
        a = t;
    }
    /* a is now the GCD */
    snprintf(buf, buf_size, "%d:%d", w / a, h / a);
}

static int MikuPan_GetPs2ResolutionScale(int w, int h)
{
    if (w <= 0 || h <= 0)
    {
        return 0;
    }

    if (w % PS2_RESOLUTION_X_INT != 0 || h % PS2_RESOLUTION_Y_INT != 0)
    {
        return 0;
    }

    int scale_x = w / PS2_RESOLUTION_X_INT;
    int scale_y = h / PS2_RESOLUTION_Y_INT;

    return scale_x == scale_y ? scale_x : 0;
}

static void MikuPan_AddResolution(int w, int h)
{
    if (w <= 0 || h <= 0 || resolution_count >= MIKUPAN_MAX_RESOLUTIONS)
    {
        return;
    }

    for (int i = 0; i < resolution_count; i++)
    {
        if (resolution_list[i].width == w && resolution_list[i].height == h)
        {
            return;
        }
    }

    resolution_list[resolution_count].width = w;
    resolution_list[resolution_count].height = h;
    resolution_count++;
}

static void MikuPan_AddPs2ResolutionMultiples(void)
{
    for (int scale = 1; scale <= MIKUPAN_MAX_PS2_RESOLUTION_SCALE; scale++)
    {
        MikuPan_AddResolution(PS2_RESOLUTION_X_INT * scale,
                              PS2_RESOLUTION_Y_INT * scale);
    }
}

static void MikuPan_PopulateResolutionList(SDL_DisplayID display,
                                           const SDL_DisplayMode* current_mode)
{
    resolution_count = 0;
    resolution_selected = 0;

    MikuPan_AddPs2ResolutionMultiples();
    MikuPan_AddResolution(render_resolution_width, render_resolution_height);

    int n = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &n);

    if (modes != NULL)
    {
        for (int i = 0; i < n; i++)
        {
            MikuPan_AddResolution(modes[i]->w, modes[i]->h);
        }

        SDL_free(modes);
    }

    // Always include the current desktop mode; older SDL backends sometimes
    // omit it from the fullscreen list.
    if (current_mode != NULL && current_mode->w > 0 && current_mode->h > 0)
    {
        MikuPan_AddResolution(current_mode->w, current_mode->h);
    }

    qsort(resolution_list, resolution_count, sizeof(MikuPan_Resolution),
          CompareResolutionDesc);

    for (int i = 0; i < resolution_count; i++)
    {
        char aspect[12];
        int ps2_scale = MikuPan_GetPs2ResolutionScale(
            resolution_list[i].width, resolution_list[i].height);

        MikuPan_AspectRatioStr(resolution_list[i].width,
                               resolution_list[i].height, aspect,
                               sizeof(aspect));

        if (ps2_scale > 0)
        {
            snprintf(resolution_labels[i], sizeof(resolution_labels[i]),
                     "%d x %d (%s) [PS2 %dx]", resolution_list[i].width,
                     resolution_list[i].height, aspect, ps2_scale);
        }
        else
        {
            snprintf(resolution_labels[i], sizeof(resolution_labels[i]),
                     "%d x %d (%s)", resolution_list[i].width,
                     resolution_list[i].height, aspect);
        }

        resolution_label_ptrs[i] = resolution_labels[i];

        if (resolution_list[i].width == render_resolution_width
            && resolution_list[i].height == render_resolution_height)
        {
            resolution_selected = i;
        }
    }
}

static const char* MikuPan_GpuDriverDisplayName(const char* name)
{
    if (SDL_strcasecmp(name, "vulkan") == 0)
    {
        return "Vulkan";
    }
    if (SDL_strcasecmp(name, "direct3d12") == 0)
    {
        return "Direct3D 12";
    }
    if (SDL_strcasecmp(name, "metal") == 0)
    {
        return "Metal";
    }
    return name;
}

static void MikuPan_PopulateGpuDriverList(void)
{
    gpu_driver_names[0][0] = '\0';
    snprintf(gpu_driver_labels[0], sizeof(gpu_driver_labels[0]), "Auto");
    gpu_driver_supported[0] = 1;
    gpu_driver_count = 1;
    gpu_driver_selected = 0;

    const int n = SDL_GetNumGPUDrivers();
    for (int i = 0; i < n && gpu_driver_count <= MIKUPAN_MAX_GPU_DRIVERS; i++)
    {
        const char* name = SDL_GetGPUDriver(i);
        if (name == NULL)
        {
            continue;
        }

        const int idx = gpu_driver_count;
        snprintf(gpu_driver_names[idx], sizeof(gpu_driver_names[idx]), "%s",
                 name);
        gpu_driver_supported[idx] =
            SDL_GPUSupportsShaderFormats(MIKUPAN_GPU_SHADER_FORMATS, name) ? 1
                                                                           : 0;
        snprintf(gpu_driver_labels[idx], sizeof(gpu_driver_labels[idx]),
                 "%s%s", MikuPan_GpuDriverDisplayName(name),
                 gpu_driver_supported[idx] ? "" : " (unsupported)");

        if (SDL_strcasecmp(name, mikupan_configuration.renderer.gpu_driver)
            == 0)
        {
            gpu_driver_selected = idx;
        }

        gpu_driver_count++;
    }
}

static void MikuPan_UiGpuBackendCombo(void)
{
    if (igBeginCombo("GPU Backend", gpu_driver_labels[gpu_driver_selected], 0))
    {
        for (int i = 0; i < gpu_driver_count; i++)
        {
            bool is_selected = (gpu_driver_selected == i);

            if (!gpu_driver_supported[i])
            {
                igBeginDisabled(true);
            }

            if (igSelectable_Bool(gpu_driver_labels[i], is_selected, 0,
                                  (ImVec2) {0, 0}))
            {
                gpu_driver_selected = i;
                snprintf(mikupan_configuration.renderer.gpu_driver,
                         sizeof(mikupan_configuration.renderer.gpu_driver),
                         "%s", gpu_driver_names[i]);
            }

            if (!gpu_driver_supported[i])
            {
                igEndDisabled();
            }

            if (is_selected)
            {
                igSetItemDefaultFocus();
            }
        }

        igEndCombo();
    }

    SDL_GPUDevice* device = MikuPan_GPUGetDevice();
    const char* active =
        (device != NULL) ? SDL_GetGPUDeviceDriver(device) : NULL;
    igTextDisabled("Active: %s", (active != NULL)
                                     ? MikuPan_GpuDriverDisplayName(active)
                                     : "none");

    if (gpu_driver_names[gpu_driver_selected][0] != '\0'
        && (active == NULL
            || SDL_strcasecmp(gpu_driver_names[gpu_driver_selected], active)
                   != 0))
    {
        igTextDisabled("Save Configuration and restart to apply");
    }
}

static void MikuPan_ApplyUiFont(int font)
{
    ImGuiIO* io = igGetIO_Nil();
    font = MikuPan_ClampFontIndex(font);

    if (ui_fonts[font] != NULL)
    {
        io->FontDefault = ui_fonts[font];
    }
}

static float MikuPan_ClampFontScale(float scale)
{
    if (scale < 0.5f)
    {
        return 0.5f;
    }
    if (scale > 3.0f)
    {
        return 3.0f;
    }
    return scale;
}

static void MikuPan_ApplyUiFontScale(void)
{
    ImGuiStyle* style = igGetStyle();
    mikupan_configuration.font_scale =
        MikuPan_ClampFontScale(mikupan_configuration.font_scale);
    style->FontScaleMain = mikupan_configuration.font_scale;
}

static void MikuPan_LoadUiFonts(void)
{
    ImGuiIO* io = igGetIO_Nil();
    ImFontConfig* default_font_config = ImFontConfig_ImFontConfig();

    ui_font_default_size = 13.0f * ui_display_scale;
    ui_font_regular_size = 14.0f * ui_display_scale;

    if (default_font_config != NULL)
    {
        default_font_config->SizePixels = ui_font_default_size;
    }

    ui_fonts[0] = ImFontAtlas_AddFontDefault(io->Fonts, default_font_config);

    if (default_font_config != NULL)
    {
        ImFontConfig_destroy(default_font_config);
    }

    char font_path[1024];
    if (MikuPan_ResolveBasePath("resources/fonts/CenturyOldStyle.ttf",
                                font_path,
                                sizeof(font_path)))
    {
        if (strncmp(font_path, "assets://", 9) == 0
            || strncmp(font_path, "assets:/", 8) == 0)
        {
            u_int font_size = MikuPan_GetFileSize(font_path);
            if (font_size > 0 && font_size <= INT_MAX)
            {
                void *font_data = malloc(font_size);
                if (font_data != NULL)
                {
                    MikuPan_ReadFullFile(font_path, font_data);
                    ui_fonts[1] = ImFontAtlas_AddFontFromMemoryTTF(
                        io->Fonts, font_data, (int) font_size,
                        ui_font_regular_size, NULL,
                        ImFontAtlas_GetGlyphRangesDefault(io->Fonts));
                }
            }
        }
        else
        {
            ui_fonts[1] = ImFontAtlas_AddFontFromFileTTF(
                io->Fonts, font_path,
                ui_font_regular_size, NULL,
                ImFontAtlas_GetGlyphRangesDefault(io->Fonts));
        }
    }

    if (ui_fonts[1] == NULL)
    {
        ui_fonts[1] = ui_fonts[0];
    }

    mikupan_configuration.selected_font =
        MikuPan_ClampFontIndex(mikupan_configuration.selected_font);
    MikuPan_ApplyUiFont(mikupan_configuration.selected_font);
}

static void MikuPan_ApplyThemeBaseline(ImVec4* c)
{
    c[ImGuiCol_Separator] = (ImVec4) {0.22f, 0.24f, 0.28f, 0.55f};
    c[ImGuiCol_SeparatorHovered] = (ImVec4) {0.40f, 0.45f, 0.52f, 0.85f};
    c[ImGuiCol_SeparatorActive] = (ImVec4) {0.58f, 0.66f, 0.76f, 1.00f};
    c[ImGuiCol_ResizeGrip] = (ImVec4) {0.24f, 0.28f, 0.34f, 0.25f};
    c[ImGuiCol_ResizeGripHovered] = (ImVec4) {0.42f, 0.50f, 0.60f, 0.70f};
    c[ImGuiCol_ResizeGripActive] = (ImVec4) {0.58f, 0.68f, 0.80f, 0.95f};
    c[ImGuiCol_TabDimmed] = (ImVec4) {0.06f, 0.08f, 0.11f, 0.95f};
    c[ImGuiCol_TabDimmedSelected] = (ImVec4) {0.14f, 0.18f, 0.24f, 1.00f};
    c[ImGuiCol_TabDimmedSelectedOverline] =
        (ImVec4) {0.36f, 0.48f, 0.62f, 1.00f};
    c[ImGuiCol_PlotLines] = (ImVec4) {0.58f, 0.70f, 0.84f, 1.00f};
    c[ImGuiCol_PlotLinesHovered] = (ImVec4) {0.76f, 0.86f, 0.98f, 1.00f};
    c[ImGuiCol_PlotHistogram] = (ImVec4) {0.44f, 0.54f, 0.68f, 1.00f};
    c[ImGuiCol_PlotHistogramHovered] = (ImVec4) {0.64f, 0.76f, 0.92f, 1.00f};
    c[ImGuiCol_TableRowBg] = (ImVec4) {0.00f, 0.00f, 0.00f, 0.00f};
    c[ImGuiCol_TableRowBgAlt] = (ImVec4) {0.12f, 0.16f, 0.22f, 0.25f};
    c[ImGuiCol_DragDropTarget] = (ImVec4) {0.76f, 0.86f, 0.98f, 0.90f};
    c[ImGuiCol_NavCursor] = (ImVec4) {0.58f, 0.70f, 0.84f, 1.00f};
    c[ImGuiCol_NavWindowingHighlight] = (ImVec4) {0.76f, 0.86f, 0.98f, 0.70f};
    c[ImGuiCol_NavWindowingDimBg] = (ImVec4) {0.03f, 0.04f, 0.06f, 0.55f};
    c[ImGuiCol_ModalWindowDimBg] = (ImVec4) {0.02f, 0.03f, 0.05f, 0.72f};
}

static void MikuPan_ApplyFatalFrameStyle(int theme)
{
    theme = MikuPan_ClampThemeIndex(theme);

    ImGuiStyle* s = igGetStyle();
    ImVec4* c = s->Colors;

    s->Alpha = 0.96f;
    s->DisabledAlpha = 0.45f;
    s->WindowRounding = 0.0f;
    s->ChildRounding = 0.0f;
    s->PopupRounding = 0.0f;
    s->FrameRounding = 0.0f;
    s->ScrollbarRounding = 0.0f;
    s->GrabRounding = 0.0f;
    s->TabRounding = 0.0f;
    s->WindowBorderSize = 1.0f;
    s->ChildBorderSize = 1.0f;
    s->PopupBorderSize = 1.0f;
    s->FrameBorderSize = 1.0f;
    s->TabBorderSize = 1.0f;
    s->WindowPadding = (ImVec2) {10.0f, 10.0f};
    s->FramePadding = (ImVec2) {8.0f, 4.0f};
    s->ItemSpacing = (ImVec2) {8.0f, 5.0f};
    s->ItemInnerSpacing = (ImVec2) {6.0f, 4.0f};
    s->IndentSpacing = 18.0f;
    s->ScrollbarSize = 14.0f;
    s->GrabMinSize = 10.0f;

    if (theme != 2)
    {
        MikuPan_ApplyThemeBaseline(c);
    }

    switch (theme)
    {
        default:
        case 0:
            // Text: pale paper with a hint of purple-blue
            c[ImGuiCol_Text] = (ImVec4) {0.78f, 0.82f, 0.88f, 1.00f};
            c[ImGuiCol_TextDisabled] = (ImVec4) {0.32f, 0.36f, 0.42f, 1.00f};

            // Background: deep blue-black like moonlit mansion
            c[ImGuiCol_WindowBg] = (ImVec4) {0.04f, 0.06f, 0.10f, 0.96f};
            c[ImGuiCol_ChildBg] = (ImVec4) {0.06f, 0.08f, 0.12f, 0.85f};
            c[ImGuiCol_PopupBg] = (ImVec4) {0.03f, 0.05f, 0.08f, 0.98f};

            // Borders: slate with a cold purple lean
            c[ImGuiCol_Border] = (ImVec4) {0.24f, 0.28f, 0.35f, 0.65f};
            c[ImGuiCol_BorderShadow] = (ImVec4) {0, 0, 0, 0};

            // Frames: cool gray-blue
            c[ImGuiCol_FrameBg] = (ImVec4) {0.09f, 0.12f, 0.16f, 0.95f};
            c[ImGuiCol_FrameBgHovered] = (ImVec4) {0.16f, 0.21f, 0.28f, 0.95f};
            c[ImGuiCol_FrameBgActive] = (ImVec4) {0.23f, 0.30f, 0.40f, 1.00f};

            // Title bars: dark with cold highlights
            c[ImGuiCol_TitleBg] = (ImVec4) {0.05f, 0.07f, 0.10f, 1.00f};
            c[ImGuiCol_TitleBgActive] = (ImVec4) {0.10f, 0.14f, 0.18f, 1.00f};
            c[ImGuiCol_TitleBgCollapsed] =
                (ImVec4) {0.03f, 0.05f, 0.08f, 0.85f};

            c[ImGuiCol_MenuBarBg] = (ImVec4) {0.07f, 0.09f, 0.12f, 1.00f};

            // Scrollbars: soft azure
            c[ImGuiCol_ScrollbarBg] = (ImVec4) {0.03f, 0.04f, 0.06f, 0.85f};
            c[ImGuiCol_ScrollbarGrab] = (ImVec4) {0.18f, 0.24f, 0.30f, 1.00f};
            c[ImGuiCol_ScrollbarGrabHovered] =
                (ImVec4) {0.26f, 0.34f, 0.42f, 1.00f};
            c[ImGuiCol_ScrollbarGrabActive] =
                (ImVec4) {0.34f, 0.44f, 0.54f, 1.00f};

            // Accent: ghost-light cyan / pale purple
            c[ImGuiCol_CheckMark] = (ImVec4) {0.60f, 0.78f, 0.96f, 1.00f};
            c[ImGuiCol_SliderGrab] = (ImVec4) {0.42f, 0.64f, 0.88f, 1.00f};
            c[ImGuiCol_SliderGrabActive] =
                (ImVec4) {0.60f, 0.84f, 0.98f, 1.00f};

            // Buttons: twilight blue-gray
            c[ImGuiCol_Button] = (ImVec4) {0.08f, 0.10f, 0.14f, 0.95f};
            c[ImGuiCol_ButtonHovered] = (ImVec4) {0.18f, 0.24f, 0.32f, 1.00f};
            c[ImGuiCol_ButtonActive] = (ImVec4) {0.28f, 0.36f, 0.48f, 1.00f};

            // Headers
            c[ImGuiCol_Header] = (ImVec4) {0.12f, 0.16f, 0.22f, 0.85f};
            c[ImGuiCol_HeaderHovered] = (ImVec4) {0.20f, 0.28f, 0.38f, 0.95f};
            c[ImGuiCol_HeaderActive] = (ImVec4) {0.28f, 0.38f, 0.52f, 1.00f};

            // Tabs
            c[ImGuiCol_Tab] = (ImVec4) {0.08f, 0.10f, 0.14f, 0.95f};
            c[ImGuiCol_TabHovered] = (ImVec4) {0.20f, 0.28f, 0.38f, 0.95f};
            c[ImGuiCol_TabSelected] = (ImVec4) {0.26f, 0.34f, 0.46f, 1.00f};
            c[ImGuiCol_TabSelectedOverline] =
                (ImVec4) {0.60f, 0.78f, 0.96f, 1.00f};

            // Misc accents (plots, nav, tables, etc.)
            c[ImGuiCol_TextLink] = (ImVec4) {0.60f, 0.78f, 0.96f, 1.00f};
            c[ImGuiCol_TextSelectedBg] = (ImVec4) {0.25f, 0.32f, 0.42f, 0.65f};
            c[ImGuiCol_TableHeaderBg] = (ImVec4) {0.08f, 0.12f, 0.16f, 1.00f};
            c[ImGuiCol_TableBorderStrong] =
                (ImVec4) {0.22f, 0.28f, 0.34f, 1.00f};
            c[ImGuiCol_TableBorderLight] =
                (ImVec4) {0.14f, 0.18f, 0.24f, 1.00f};
            break;
        case 1:
            // Text: pale paper, slightly cold
            c[ImGuiCol_Text] = (ImVec4) {0.82f, 0.86f, 0.88f, 1.00f};
            c[ImGuiCol_TextDisabled] = (ImVec4) {0.38f, 0.42f, 0.45f, 1.00f};

            // Backgrounds: blue-black ink
            c[ImGuiCol_WindowBg] = (ImVec4) {0.05f, 0.07f, 0.09f, 0.96f};
            c[ImGuiCol_ChildBg] = (ImVec4) {0.07f, 0.09f, 0.11f, 0.85f};
            c[ImGuiCol_PopupBg] = (ImVec4) {0.04f, 0.06f, 0.08f, 0.98f};

            // Borders: cold slate
            c[ImGuiCol_Border] = (ImVec4) {0.22f, 0.28f, 0.32f, 0.65f};
            c[ImGuiCol_BorderShadow] = (ImVec4) {0, 0, 0, 0};

            // Frames: dusty wood in moonlight
            c[ImGuiCol_FrameBg] = (ImVec4) {0.10f, 0.13f, 0.16f, 0.95f};
            c[ImGuiCol_FrameBgHovered] = (ImVec4) {0.18f, 0.24f, 0.28f, 0.95f};
            c[ImGuiCol_FrameBgActive] = (ImVec4) {0.26f, 0.34f, 0.40f, 1.00f};

            // Titles: very dark blue-black
            c[ImGuiCol_TitleBg] = (ImVec4) {0.06f, 0.08f, 0.10f, 1.00f};
            c[ImGuiCol_TitleBgActive] = (ImVec4) {0.12f, 0.18f, 0.22f, 1.00f};
            c[ImGuiCol_TitleBgCollapsed] =
                (ImVec4) {0.05f, 0.07f, 0.09f, 0.85f};

            c[ImGuiCol_MenuBarBg] = (ImVec4) {0.08f, 0.10f, 0.12f, 1.00f};

            // Scrollbar: faint blue steel
            c[ImGuiCol_ScrollbarBg] = (ImVec4) {0.04f, 0.05f, 0.07f, 0.85f};
            c[ImGuiCol_ScrollbarGrab] = (ImVec4) {0.22f, 0.30f, 0.36f, 1.00f};
            c[ImGuiCol_ScrollbarGrabHovered] =
                (ImVec4) {0.30f, 0.40f, 0.46f, 1.00f};
            c[ImGuiCol_ScrollbarGrabActive] =
                (ImVec4) {0.38f, 0.50f, 0.58f, 1.00f};

            // Accent color: ghostly cyan (FF1 signature)
            c[ImGuiCol_CheckMark] = (ImVec4) {0.60f, 0.85f, 0.90f, 1.00f};
            c[ImGuiCol_SliderGrab] = (ImVec4) {0.50f, 0.78f, 0.84f, 1.00f};
            c[ImGuiCol_SliderGrabActive] =
                (ImVec4) {0.70f, 0.92f, 0.98f, 1.00f};

            // Buttons: foggy slate
            c[ImGuiCol_Button] = (ImVec4) {0.12f, 0.16f, 0.20f, 0.95f};
            c[ImGuiCol_ButtonHovered] = (ImVec4) {0.22f, 0.30f, 0.36f, 1.00f};
            c[ImGuiCol_ButtonActive] = (ImVec4) {0.32f, 0.42f, 0.50f, 1.00f};

            // Headers
            c[ImGuiCol_Header] = (ImVec4) {0.14f, 0.20f, 0.24f, 0.85f};
            c[ImGuiCol_HeaderHovered] = (ImVec4) {0.24f, 0.34f, 0.40f, 0.95f};
            c[ImGuiCol_HeaderActive] = (ImVec4) {0.34f, 0.46f, 0.54f, 1.00f};

            // Separators
            c[ImGuiCol_Separator] = (ImVec4) {0.22f, 0.28f, 0.32f, 0.55f};
            c[ImGuiCol_SeparatorHovered] =
                (ImVec4) {0.40f, 0.52f, 0.60f, 0.85f};
            c[ImGuiCol_SeparatorActive] = (ImVec4) {0.55f, 0.70f, 0.80f, 1.00f};

            // Tabs
            c[ImGuiCol_Tab] = (ImVec4) {0.10f, 0.14f, 0.18f, 0.95f};
            c[ImGuiCol_TabHovered] = (ImVec4) {0.24f, 0.34f, 0.40f, 0.95f};
            c[ImGuiCol_TabSelected] = (ImVec4) {0.30f, 0.42f, 0.50f, 1.00f};
            c[ImGuiCol_TabSelectedOverline] =
                (ImVec4) {0.65f, 0.90f, 0.95f, 1.00f};

            // Tables / misc accents
            c[ImGuiCol_TableHeaderBg] = (ImVec4) {0.12f, 0.18f, 0.22f, 1.00f};
            c[ImGuiCol_TableBorderStrong] =
                (ImVec4) {0.22f, 0.30f, 0.36f, 1.00f};
            c[ImGuiCol_TableBorderLight] =
                (ImVec4) {0.16f, 0.22f, 0.26f, 1.00f};
            c[ImGuiCol_TextLink] = (ImVec4) {0.65f, 0.90f, 0.95f, 1.00f};
            c[ImGuiCol_TextSelectedBg] = (ImVec4) {0.30f, 0.42f, 0.50f, 0.65f};

            // Nav / modal
            c[ImGuiCol_NavCursor] = (ImVec4) {0.65f, 0.90f, 0.95f, 1.00f};
            c[ImGuiCol_NavWindowingHighlight] =
                (ImVec4) {0.65f, 0.90f, 0.95f, 0.70f};
            c[ImGuiCol_NavWindowingDimBg] =
                (ImVec4) {0.04f, 0.05f, 0.07f, 0.50f};
            c[ImGuiCol_ModalWindowDimBg] =
                (ImVec4) {0.04f, 0.05f, 0.07f, 0.65f};
            break;
        case 2:
            c[ImGuiCol_Text] = (ImVec4) {0.86f, 0.78f, 0.62f, 1.00f};
            c[ImGuiCol_TextDisabled] = (ImVec4) {0.42f, 0.36f, 0.28f, 1.00f};
            c[ImGuiCol_WindowBg] = (ImVec4) {0.06f, 0.04f, 0.03f, 0.96f};
            c[ImGuiCol_ChildBg] = (ImVec4) {0.08f, 0.05f, 0.04f, 0.85f};
            c[ImGuiCol_PopupBg] = (ImVec4) {0.05f, 0.03f, 0.02f, 0.98f};
            c[ImGuiCol_Border] = (ImVec4) {0.40f, 0.10f, 0.08f, 0.65f};
            c[ImGuiCol_BorderShadow] = (ImVec4) {0.00f, 0.00f, 0.00f, 0.00f};
            c[ImGuiCol_FrameBg] = (ImVec4) {0.12f, 0.07f, 0.05f, 0.95f};
            c[ImGuiCol_FrameBgHovered] = (ImVec4) {0.32f, 0.10f, 0.08f, 0.85f};
            c[ImGuiCol_FrameBgActive] = (ImVec4) {0.52f, 0.12f, 0.10f, 0.95f};
            c[ImGuiCol_TitleBg] = (ImVec4) {0.08f, 0.04f, 0.03f, 1.00f};
            c[ImGuiCol_TitleBgActive] = (ImVec4) {0.35f, 0.05f, 0.05f, 1.00f};
            c[ImGuiCol_TitleBgCollapsed] =
                (ImVec4) {0.05f, 0.02f, 0.02f, 0.85f};
            c[ImGuiCol_MenuBarBg] = (ImVec4) {0.10f, 0.05f, 0.04f, 1.00f};
            c[ImGuiCol_ScrollbarBg] = (ImVec4) {0.04f, 0.02f, 0.02f, 0.85f};
            c[ImGuiCol_ScrollbarGrab] = (ImVec4) {0.28f, 0.16f, 0.10f, 1.00f};
            c[ImGuiCol_ScrollbarGrabHovered] =
                (ImVec4) {0.50f, 0.20f, 0.12f, 1.00f};
            c[ImGuiCol_ScrollbarGrabActive] =
                (ImVec4) {0.65f, 0.22f, 0.14f, 1.00f};
            c[ImGuiCol_CheckMark] = (ImVec4) {0.85f, 0.40f, 0.18f, 1.00f};
            c[ImGuiCol_SliderGrab] = (ImVec4) {0.60f, 0.20f, 0.14f, 1.00f};
            c[ImGuiCol_SliderGrabActive] =
                (ImVec4) {0.80f, 0.30f, 0.16f, 1.00f};
            c[ImGuiCol_Button] = (ImVec4) {0.18f, 0.08f, 0.06f, 0.95f};
            c[ImGuiCol_ButtonHovered] = (ImVec4) {0.45f, 0.12f, 0.10f, 1.00f};
            c[ImGuiCol_ButtonActive] = (ImVec4) {0.65f, 0.16f, 0.12f, 1.00f};
            c[ImGuiCol_Header] = (ImVec4) {0.22f, 0.08f, 0.06f, 0.85f};
            c[ImGuiCol_HeaderHovered] = (ImVec4) {0.45f, 0.12f, 0.10f, 0.95f};
            c[ImGuiCol_HeaderActive] = (ImVec4) {0.60f, 0.14f, 0.10f, 1.00f};
            c[ImGuiCol_Separator] = (ImVec4) {0.40f, 0.12f, 0.10f, 0.55f};
            c[ImGuiCol_SeparatorHovered] =
                (ImVec4) {0.65f, 0.18f, 0.12f, 0.85f};
            c[ImGuiCol_SeparatorActive] = (ImVec4) {0.85f, 0.24f, 0.14f, 1.00f};
            c[ImGuiCol_ResizeGrip] = (ImVec4) {0.28f, 0.12f, 0.08f, 0.50f};
            c[ImGuiCol_ResizeGripHovered] =
                (ImVec4) {0.55f, 0.16f, 0.10f, 0.85f};
            c[ImGuiCol_ResizeGripActive] =
                (ImVec4) {0.80f, 0.22f, 0.14f, 1.00f};
            c[ImGuiCol_Tab] = (ImVec4) {0.14f, 0.06f, 0.04f, 0.95f};
            c[ImGuiCol_TabHovered] = (ImVec4) {0.45f, 0.12f, 0.10f, 0.95f};
            c[ImGuiCol_TabSelected] = (ImVec4) {0.55f, 0.14f, 0.10f, 1.00f};
            c[ImGuiCol_TabSelectedOverline] =
                (ImVec4) {0.85f, 0.25f, 0.14f, 1.00f};
            c[ImGuiCol_TabDimmed] = (ImVec4) {0.10f, 0.05f, 0.03f, 0.95f};
            c[ImGuiCol_TabDimmedSelected] =
                (ImVec4) {0.30f, 0.08f, 0.06f, 1.00f};
            c[ImGuiCol_TabDimmedSelectedOverline] =
                (ImVec4) {0.55f, 0.14f, 0.10f, 1.00f};
            c[ImGuiCol_PlotLines] = (ImVec4) {0.85f, 0.50f, 0.18f, 1.00f};
            c[ImGuiCol_PlotLinesHovered] =
                (ImVec4) {1.00f, 0.55f, 0.20f, 1.00f};
            c[ImGuiCol_PlotHistogram] = (ImVec4) {0.65f, 0.20f, 0.14f, 1.00f};
            c[ImGuiCol_PlotHistogramHovered] =
                (ImVec4) {0.90f, 0.30f, 0.18f, 1.00f};
            c[ImGuiCol_TableHeaderBg] = (ImVec4) {0.18f, 0.08f, 0.06f, 1.00f};
            c[ImGuiCol_TableBorderStrong] =
                (ImVec4) {0.40f, 0.12f, 0.10f, 1.00f};
            c[ImGuiCol_TableBorderLight] =
                (ImVec4) {0.25f, 0.10f, 0.08f, 1.00f};
            c[ImGuiCol_TableRowBg] = (ImVec4) {0.00f, 0.00f, 0.00f, 0.00f};
            c[ImGuiCol_TableRowBgAlt] = (ImVec4) {0.95f, 0.50f, 0.18f, 0.04f};
            c[ImGuiCol_TextLink] = (ImVec4) {0.95f, 0.50f, 0.18f, 1.00f};
            c[ImGuiCol_TextSelectedBg] = (ImVec4) {0.55f, 0.14f, 0.10f, 0.65f};
            c[ImGuiCol_DragDropTarget] = (ImVec4) {0.95f, 0.50f, 0.18f, 0.90f};
            c[ImGuiCol_NavCursor] = (ImVec4) {0.85f, 0.25f, 0.14f, 1.00f};
            c[ImGuiCol_NavWindowingHighlight] =
                (ImVec4) {0.95f, 0.50f, 0.18f, 0.70f};
            c[ImGuiCol_NavWindowingDimBg] =
                (ImVec4) {0.04f, 0.02f, 0.02f, 0.50f};
            c[ImGuiCol_ModalWindowDimBg] =
                (ImVec4) {0.04f, 0.02f, 0.02f, 0.65f};
            break;

        case 3:
            /*
                Fatal Frame 1 PS2 inspired theme.

                Palette direction:
                - blackened wood / tatami shadows
                - dried blood red-browns
                - faded paper text
                - restrained bruised violet accents
                - sharp, square PS2-era UI shapes
            */
            // Text: faded paper / old photograph highlight
            c[ImGuiCol_Text] = (ImVec4) {0.86f, 0.80f, 0.70f, 1.00f};
            c[ImGuiCol_TextDisabled] = (ImVec4) {0.42f, 0.35f, 0.34f, 1.00f};

            // Backgrounds: blackened tatami / dark old wood
            c[ImGuiCol_WindowBg] = (ImVec4) {0.055f, 0.035f, 0.040f, 0.96f};
            c[ImGuiCol_ChildBg] = (ImVec4) {0.085f, 0.055f, 0.055f, 0.86f};
            c[ImGuiCol_PopupBg] = (ImVec4) {0.045f, 0.030f, 0.035f, 0.98f};

            // Borders: dusty red-brown lacquer
            c[ImGuiCol_Border] = (ImVec4) {0.31f, 0.19f, 0.18f, 0.68f};
            c[ImGuiCol_BorderShadow] = (ImVec4) {0.00f, 0.00f, 0.00f, 0.00f};

            // Frames: aged wood / stained cloth
            c[ImGuiCol_FrameBg] = (ImVec4) {0.13f, 0.075f, 0.080f, 0.95f};
            c[ImGuiCol_FrameBgHovered] = (ImVec4) {0.24f, 0.12f, 0.15f, 0.96f};
            c[ImGuiCol_FrameBgActive] = (ImVec4) {0.36f, 0.16f, 0.22f, 1.00f};

            // Titles: deep cover-shadow red-black
            c[ImGuiCol_TitleBg] = (ImVec4) {0.07f, 0.035f, 0.040f, 1.00f};
            c[ImGuiCol_TitleBgActive] = (ImVec4) {0.20f, 0.075f, 0.110f, 1.00f};
            c[ImGuiCol_TitleBgCollapsed] =
                (ImVec4) {0.055f, 0.030f, 0.035f, 0.86f};

            c[ImGuiCol_MenuBarBg] = (ImVec4) {0.10f, 0.060f, 0.060f, 1.00f};

            // Scrollbar: old cabinet / dark rust
            c[ImGuiCol_ScrollbarBg] = (ImVec4) {0.045f, 0.030f, 0.035f, 0.86f};
            c[ImGuiCol_ScrollbarGrab] = (ImVec4) {0.28f, 0.14f, 0.16f, 1.00f};
            c[ImGuiCol_ScrollbarGrabHovered] =
                (ImVec4) {0.40f, 0.18f, 0.23f, 1.00f};
            c[ImGuiCol_ScrollbarGrabActive] =
                (ImVec4) {0.54f, 0.22f, 0.31f, 1.00f};

            // Accent color: muted ritual violet / bruised crimson-purple
            c[ImGuiCol_CheckMark] = (ImVec4) {0.58f, 0.34f, 0.62f, 1.00f};
            c[ImGuiCol_SliderGrab] = (ImVec4) {0.42f, 0.23f, 0.48f, 1.00f};
            c[ImGuiCol_SliderGrabActive] =
                (ImVec4) {0.68f, 0.42f, 0.72f, 1.00f};

            // Buttons: muted blood-stained wood
            c[ImGuiCol_Button] = (ImVec4) {0.16f, 0.075f, 0.085f, 0.95f};
            c[ImGuiCol_ButtonHovered] = (ImVec4) {0.32f, 0.13f, 0.18f, 1.00f};
            c[ImGuiCol_ButtonActive] = (ImVec4) {0.40f, 0.15f, 0.22f, 1.00f};

            // Headers: carpet maroon / dried blood
            c[ImGuiCol_Header] = (ImVec4) {0.20f, 0.085f, 0.105f, 0.86f};
            c[ImGuiCol_HeaderHovered] = (ImVec4) {0.38f, 0.14f, 0.20f, 0.96f};
            c[ImGuiCol_HeaderActive] = (ImVec4) {0.42f, 0.15f, 0.24f, 1.00f};

            // Separators: worn red-brown edges with violet interaction highlight
            c[ImGuiCol_Separator] = (ImVec4) {0.31f, 0.19f, 0.18f, 0.58f};
            c[ImGuiCol_SeparatorHovered] =
                (ImVec4) {0.48f, 0.26f, 0.46f, 0.85f};
            c[ImGuiCol_SeparatorActive] = (ImVec4) {0.62f, 0.36f, 0.66f, 1.00f};

            // Resize grips: dim until interacted with
            c[ImGuiCol_ResizeGrip] = (ImVec4) {0.31f, 0.19f, 0.18f, 0.25f};
            c[ImGuiCol_ResizeGripHovered] =
                (ImVec4) {0.48f, 0.26f, 0.46f, 0.70f};
            c[ImGuiCol_ResizeGripActive] =
                (ImVec4) {0.62f, 0.36f, 0.66f, 0.95f};

            // Tabs: dark red-brown, selected with restrained shrine-violet
            c[ImGuiCol_Tab] = (ImVec4) {0.13f, 0.065f, 0.075f, 0.95f};
            c[ImGuiCol_TabHovered] = (ImVec4) {0.38f, 0.14f, 0.20f, 0.96f};
            c[ImGuiCol_TabSelected] = (ImVec4) {0.34f, 0.13f, 0.22f, 1.00f};
            c[ImGuiCol_TabSelectedOverline] =
                (ImVec4) {0.62f, 0.36f, 0.66f, 1.00f};
            c[ImGuiCol_TabDimmed] = (ImVec4) {0.09f, 0.045f, 0.050f, 0.95f};
            c[ImGuiCol_TabDimmedSelected] =
                (ImVec4) {0.18f, 0.075f, 0.105f, 1.00f};
            c[ImGuiCol_TabDimmedSelectedOverline] =
                (ImVec4) {0.38f, 0.22f, 0.42f, 1.00f};

            // Plot colors: subdued, readable, not too modern-looking
            c[ImGuiCol_PlotLines] = (ImVec4) {0.66f, 0.44f, 0.68f, 1.00f};
            c[ImGuiCol_PlotLinesHovered] =
                (ImVec4) {0.80f, 0.58f, 0.82f, 1.00f};
            c[ImGuiCol_PlotHistogram] = (ImVec4) {0.50f, 0.22f, 0.28f, 1.00f};
            c[ImGuiCol_PlotHistogramHovered] =
                (ImVec4) {0.68f, 0.30f, 0.40f, 1.00f};

            // Tables / misc accents
            c[ImGuiCol_TableHeaderBg] = (ImVec4) {0.16f, 0.075f, 0.090f, 1.00f};
            c[ImGuiCol_TableBorderStrong] =
                (ImVec4) {0.34f, 0.18f, 0.19f, 1.00f};
            c[ImGuiCol_TableBorderLight] =
                (ImVec4) {0.23f, 0.12f, 0.13f, 1.00f};
            c[ImGuiCol_TableRowBg] = (ImVec4) {0.00f, 0.00f, 0.00f, 0.00f};
            c[ImGuiCol_TableRowBgAlt] = (ImVec4) {0.12f, 0.065f, 0.070f, 0.32f};

            c[ImGuiCol_TextLink] = (ImVec4) {0.66f, 0.44f, 0.68f, 1.00f};
            c[ImGuiCol_TextSelectedBg] = (ImVec4) {0.34f, 0.18f, 0.36f, 0.65f};

            // Drag/drop and highlights
            c[ImGuiCol_DragDropTarget] = (ImVec4) {0.80f, 0.58f, 0.82f, 0.90f};

            // Nav / modal
            c[ImGuiCol_NavCursor] = (ImVec4) {0.66f, 0.44f, 0.68f, 1.00f};
            c[ImGuiCol_NavWindowingHighlight] =
                (ImVec4) {0.66f, 0.44f, 0.68f, 0.70f};
            c[ImGuiCol_NavWindowingDimBg] =
                (ImVec4) {0.045f, 0.030f, 0.035f, 0.55f};
            c[ImGuiCol_ModalWindowDimBg] =
                (ImVec4) {0.035f, 0.020f, 0.025f, 0.72f};
            break;
        case 4:
            // Text: pale moonlit cloth / ghost grey
            c[ImGuiCol_Text] = (ImVec4) {0.78f, 0.84f, 0.82f, 1.00f};
            c[ImGuiCol_TextDisabled] = (ImVec4) {0.36f, 0.43f, 0.42f, 1.00f};

            // Backgrounds: cold blue-green night fog
            c[ImGuiCol_WindowBg] = (ImVec4) {0.035f, 0.070f, 0.075f, 0.96f};
            c[ImGuiCol_ChildBg] = (ImVec4) {0.055f, 0.095f, 0.100f, 0.86f};
            c[ImGuiCol_PopupBg] = (ImVec4) {0.025f, 0.055f, 0.060f, 0.98f};

            // Borders: misty oxidized teal
            c[ImGuiCol_Border] = (ImVec4) {0.20f, 0.34f, 0.35f, 0.66f};
            c[ImGuiCol_BorderShadow] = (ImVec4) {0.00f, 0.00f, 0.00f, 0.00f};

            // Frames: dark wet wood under moonlight
            c[ImGuiCol_FrameBg] = (ImVec4) {0.070f, 0.120f, 0.125f, 0.95f};
            c[ImGuiCol_FrameBgHovered] =
                (ImVec4) {0.120f, 0.210f, 0.215f, 0.96f};
            c[ImGuiCol_FrameBgActive] =
                (ImVec4) {0.170f, 0.300f, 0.310f, 1.00f};

            // Titles: deep blue-green shadow
            c[ImGuiCol_TitleBg] = (ImVec4) {0.030f, 0.060f, 0.065f, 1.00f};
            c[ImGuiCol_TitleBgActive] =
                (ImVec4) {0.075f, 0.145f, 0.155f, 1.00f};
            c[ImGuiCol_TitleBgCollapsed] =
                (ImVec4) {0.030f, 0.055f, 0.060f, 0.86f};

            c[ImGuiCol_MenuBarBg] = (ImVec4) {0.050f, 0.095f, 0.100f, 1.00f};

            // Scrollbar: dull teal slate
            c[ImGuiCol_ScrollbarBg] = (ImVec4) {0.025f, 0.050f, 0.055f, 0.86f};
            c[ImGuiCol_ScrollbarGrab] =
                (ImVec4) {0.150f, 0.270f, 0.280f, 1.00f};
            c[ImGuiCol_ScrollbarGrabHovered] =
                (ImVec4) {0.210f, 0.370f, 0.380f, 1.00f};
            c[ImGuiCol_ScrollbarGrabActive] =
                (ImVec4) {0.290f, 0.500f, 0.510f, 1.00f};

            // Accent color: restrained spirit violet, less pink
            c[ImGuiCol_CheckMark] = (ImVec4) {0.56f, 0.45f, 0.72f, 1.00f};
            c[ImGuiCol_SliderGrab] = (ImVec4) {0.40f, 0.32f, 0.58f, 1.00f};
            c[ImGuiCol_SliderGrabActive] =
                (ImVec4) {0.66f, 0.55f, 0.82f, 1.00f};

            // Buttons: foggy teal-black
            c[ImGuiCol_Button] = (ImVec4) {0.075f, 0.130f, 0.135f, 0.95f};
            c[ImGuiCol_ButtonHovered] =
                (ImVec4) {0.145f, 0.255f, 0.265f, 1.00f};
            c[ImGuiCol_ButtonActive] = (ImVec4) {0.205f, 0.355f, 0.365f, 1.00f};

            // Headers: darker forest-fog teal
            c[ImGuiCol_Header] = (ImVec4) {0.095f, 0.175f, 0.180f, 0.86f};
            c[ImGuiCol_HeaderHovered] =
                (ImVec4) {0.165f, 0.300f, 0.310f, 0.96f};
            c[ImGuiCol_HeaderActive] = (ImVec4) {0.230f, 0.405f, 0.420f, 1.00f};

            // Separators: fogged teal edges with violet interaction highlight
            c[ImGuiCol_Separator] = (ImVec4) {0.20f, 0.34f, 0.35f, 0.55f};
            c[ImGuiCol_SeparatorHovered] =
                (ImVec4) {0.38f, 0.34f, 0.56f, 0.85f};
            c[ImGuiCol_SeparatorActive] = (ImVec4) {0.56f, 0.45f, 0.72f, 1.00f};

            // Resize grips: dim until interacted with
            c[ImGuiCol_ResizeGrip] = (ImVec4) {0.20f, 0.34f, 0.35f, 0.25f};
            c[ImGuiCol_ResizeGripHovered] =
                (ImVec4) {0.38f, 0.34f, 0.56f, 0.70f};
            c[ImGuiCol_ResizeGripActive] =
                (ImVec4) {0.56f, 0.45f, 0.72f, 0.95f};

            // Tabs: cold teal, selected with subtle spirit violet
            c[ImGuiCol_Tab] = (ImVec4) {0.065f, 0.120f, 0.125f, 0.95f};
            c[ImGuiCol_TabHovered] = (ImVec4) {0.165f, 0.300f, 0.310f, 0.96f};
            c[ImGuiCol_TabSelected] = (ImVec4) {0.145f, 0.240f, 0.270f, 1.00f};
            c[ImGuiCol_TabSelectedOverline] =
                (ImVec4) {0.56f, 0.45f, 0.72f, 1.00f};
            c[ImGuiCol_TabDimmed] = (ImVec4) {0.040f, 0.080f, 0.085f, 0.95f};
            c[ImGuiCol_TabDimmedSelected] =
                (ImVec4) {0.085f, 0.145f, 0.155f, 1.00f};
            c[ImGuiCol_TabDimmedSelectedOverline] =
                (ImVec4) {0.34f, 0.30f, 0.48f, 1.00f};

            // Plot colors: subdued ghost-violet and muted blood-red
            c[ImGuiCol_PlotLines] = (ImVec4) {0.56f, 0.45f, 0.72f, 1.00f};
            c[ImGuiCol_PlotLinesHovered] =
                (ImVec4) {0.70f, 0.62f, 0.86f, 1.00f};
            c[ImGuiCol_PlotHistogram] = (ImVec4) {0.42f, 0.18f, 0.22f, 1.00f};
            c[ImGuiCol_PlotHistogramHovered] =
                (ImVec4) {0.58f, 0.26f, 0.32f, 1.00f};

            // Tables / misc accents
            c[ImGuiCol_TableHeaderBg] =
                (ImVec4) {0.075f, 0.145f, 0.150f, 1.00f};
            c[ImGuiCol_TableBorderStrong] =
                (ImVec4) {0.20f, 0.34f, 0.35f, 1.00f};
            c[ImGuiCol_TableBorderLight] =
                (ImVec4) {0.120f, 0.230f, 0.235f, 1.00f};
            c[ImGuiCol_TableRowBg] = (ImVec4) {0.00f, 0.00f, 0.00f, 0.00f};
            c[ImGuiCol_TableRowBgAlt] =
                (ImVec4) {0.070f, 0.125f, 0.130f, 0.30f};

            c[ImGuiCol_TextLink] = (ImVec4) {0.62f, 0.56f, 0.78f, 1.00f};
            c[ImGuiCol_TextSelectedBg] = (ImVec4) {0.28f, 0.30f, 0.44f, 0.65f};

            // Drag/drop and highlights
            c[ImGuiCol_DragDropTarget] = (ImVec4) {0.70f, 0.62f, 0.86f, 0.90f};

            // Nav / modal
            c[ImGuiCol_NavCursor] = (ImVec4) {0.62f, 0.56f, 0.78f, 1.00f};
            c[ImGuiCol_NavWindowingHighlight] =
                (ImVec4) {0.62f, 0.56f, 0.78f, 0.70f};
            c[ImGuiCol_NavWindowingDimBg] =
                (ImVec4) {0.025f, 0.050f, 0.055f, 0.55f};
            c[ImGuiCol_ModalWindowDimBg] =
                (ImVec4) {0.020f, 0.040f, 0.045f, 0.72f};
            break;
        case 5:
            // Text: faded photographic paper
            c[ImGuiCol_Text] = (ImVec4) {0.86f, 0.80f, 0.68f, 1.00f};
            c[ImGuiCol_TextDisabled] = (ImVec4) {0.42f, 0.36f, 0.27f, 1.00f};

            // Backgrounds: dark sepia print / aged ink
            c[ImGuiCol_WindowBg] = (ImVec4) {0.055f, 0.045f, 0.030f, 0.96f};
            c[ImGuiCol_ChildBg] = (ImVec4) {0.080f, 0.065f, 0.045f, 0.86f};
            c[ImGuiCol_PopupBg] = (ImVec4) {0.045f, 0.035f, 0.024f, 0.98f};

            // Borders: old varnish / worn frame edges
            c[ImGuiCol_Border] = (ImVec4) {0.36f, 0.27f, 0.16f, 0.68f};
            c[ImGuiCol_BorderShadow] = (ImVec4) {0.00f, 0.00f, 0.00f, 0.00f};

            // Frames: cabinet wood and photo album paper
            c[ImGuiCol_FrameBg] = (ImVec4) {0.120f, 0.090f, 0.055f, 0.95f};
            c[ImGuiCol_FrameBgHovered] =
                (ImVec4) {0.230f, 0.170f, 0.095f, 0.96f};
            c[ImGuiCol_FrameBgActive] =
                (ImVec4) {0.330f, 0.240f, 0.130f, 1.00f};

            // Titles: dark exposed film
            c[ImGuiCol_TitleBg] = (ImVec4) {0.060f, 0.045f, 0.030f, 1.00f};
            c[ImGuiCol_TitleBgActive] =
                (ImVec4) {0.160f, 0.110f, 0.060f, 1.00f};
            c[ImGuiCol_TitleBgCollapsed] =
                (ImVec4) {0.045f, 0.035f, 0.024f, 0.86f};

            c[ImGuiCol_MenuBarBg] = (ImVec4) {0.090f, 0.065f, 0.040f, 1.00f};

            // Scrollbar: dark bronze
            c[ImGuiCol_ScrollbarBg] = (ImVec4) {0.040f, 0.032f, 0.022f, 0.86f};
            c[ImGuiCol_ScrollbarGrab] =
                (ImVec4) {0.230f, 0.165f, 0.090f, 1.00f};
            c[ImGuiCol_ScrollbarGrabHovered] =
                (ImVec4) {0.340f, 0.245f, 0.130f, 1.00f};
            c[ImGuiCol_ScrollbarGrabActive] =
                (ImVec4) {0.470f, 0.330f, 0.170f, 1.00f};

            // Accent color: amber candlelight on old paper
            c[ImGuiCol_CheckMark] = (ImVec4) {0.78f, 0.58f, 0.28f, 1.00f};
            c[ImGuiCol_SliderGrab] = (ImVec4) {0.58f, 0.40f, 0.18f, 1.00f};
            c[ImGuiCol_SliderGrabActive] =
                (ImVec4) {0.86f, 0.64f, 0.30f, 1.00f};

            // Buttons: aged wood
            c[ImGuiCol_Button] = (ImVec4) {0.135f, 0.095f, 0.055f, 0.95f};
            c[ImGuiCol_ButtonHovered] =
                (ImVec4) {0.280f, 0.190f, 0.095f, 1.00f};
            c[ImGuiCol_ButtonActive] = (ImVec4) {0.400f, 0.270f, 0.130f, 1.00f};

            // Headers: photo album separators
            c[ImGuiCol_Header] = (ImVec4) {0.170f, 0.120f, 0.065f, 0.86f};
            c[ImGuiCol_HeaderHovered] =
                (ImVec4) {0.320f, 0.220f, 0.110f, 0.96f};
            c[ImGuiCol_HeaderActive] = (ImVec4) {0.430f, 0.290f, 0.140f, 1.00f};

            // Separators and grips: warm paper edge highlights
            c[ImGuiCol_Separator] = (ImVec4) {0.36f, 0.27f, 0.16f, 0.58f};
            c[ImGuiCol_SeparatorHovered] =
                (ImVec4) {0.55f, 0.40f, 0.20f, 0.85f};
            c[ImGuiCol_SeparatorActive] = (ImVec4) {0.78f, 0.58f, 0.28f, 1.00f};
            c[ImGuiCol_ResizeGrip] = (ImVec4) {0.36f, 0.27f, 0.16f, 0.25f};
            c[ImGuiCol_ResizeGripHovered] =
                (ImVec4) {0.55f, 0.40f, 0.20f, 0.70f};
            c[ImGuiCol_ResizeGripActive] =
                (ImVec4) {0.78f, 0.58f, 0.28f, 0.95f};

            // Tabs: dark sepia with amber overline
            c[ImGuiCol_Tab] = (ImVec4) {0.115f, 0.080f, 0.045f, 0.95f};
            c[ImGuiCol_TabHovered] = (ImVec4) {0.320f, 0.220f, 0.110f, 0.96f};
            c[ImGuiCol_TabSelected] = (ImVec4) {0.300f, 0.205f, 0.105f, 1.00f};
            c[ImGuiCol_TabSelectedOverline] =
                (ImVec4) {0.78f, 0.58f, 0.28f, 1.00f};
            c[ImGuiCol_TabDimmed] = (ImVec4) {0.075f, 0.055f, 0.032f, 0.95f};
            c[ImGuiCol_TabDimmedSelected] =
                (ImVec4) {0.155f, 0.105f, 0.055f, 1.00f};
            c[ImGuiCol_TabDimmedSelectedOverline] =
                (ImVec4) {0.45f, 0.32f, 0.16f, 1.00f};

            // Plot colors: readable, warm, restrained
            c[ImGuiCol_PlotLines] = (ImVec4) {0.78f, 0.58f, 0.28f, 1.00f};
            c[ImGuiCol_PlotLinesHovered] =
                (ImVec4) {0.95f, 0.72f, 0.36f, 1.00f};
            c[ImGuiCol_PlotHistogram] = (ImVec4) {0.54f, 0.34f, 0.16f, 1.00f};
            c[ImGuiCol_PlotHistogramHovered] =
                (ImVec4) {0.80f, 0.50f, 0.22f, 1.00f};

            // Tables / misc accents
            c[ImGuiCol_TableHeaderBg] =
                (ImVec4) {0.150f, 0.105f, 0.060f, 1.00f};
            c[ImGuiCol_TableBorderStrong] =
                (ImVec4) {0.36f, 0.27f, 0.16f, 1.00f};
            c[ImGuiCol_TableBorderLight] =
                (ImVec4) {0.230f, 0.165f, 0.090f, 1.00f};
            c[ImGuiCol_TableRowBg] = (ImVec4) {0.00f, 0.00f, 0.00f, 0.00f};
            c[ImGuiCol_TableRowBgAlt] =
                (ImVec4) {0.120f, 0.085f, 0.048f, 0.30f};
            c[ImGuiCol_TextLink] = (ImVec4) {0.82f, 0.62f, 0.32f, 1.00f};
            c[ImGuiCol_TextSelectedBg] = (ImVec4) {0.42f, 0.29f, 0.14f, 0.65f};
            c[ImGuiCol_DragDropTarget] = (ImVec4) {0.95f, 0.72f, 0.36f, 0.90f};

            // Nav / modal
            c[ImGuiCol_NavCursor] = (ImVec4) {0.82f, 0.62f, 0.32f, 1.00f};
            c[ImGuiCol_NavWindowingHighlight] =
                (ImVec4) {0.95f, 0.72f, 0.36f, 0.70f};
            c[ImGuiCol_NavWindowingDimBg] =
                (ImVec4) {0.035f, 0.026f, 0.018f, 0.55f};
            c[ImGuiCol_ModalWindowDimBg] =
                (ImVec4) {0.030f, 0.022f, 0.014f, 0.72f};
            break;
    }

    if (ui_display_scale != 1.0f)
    {
        ImGuiStyle_ScaleAllSizes(s, ui_display_scale);
    }
}

SDL_Window* MikuPan_GetUiWindow(void)
{
    return ui_window;
}

void MikuPan_InitUi(SDL_Window* window)
{
    ui_window = window;
    igCreateContext(NULL);
    ImGuiIO* io = igGetIO_Nil();
    io->ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

    mikupan_configuration.selected_theme =
        MikuPan_ClampThemeIndex(mikupan_configuration.selected_theme);
    mikupan_configuration.selected_font =
        MikuPan_ClampFontIndex(mikupan_configuration.selected_font);
    ui_display_scale = MikuPan_CalculateUiDisplayScale(window);
    MikuPan_LoadUiFonts();
    MikuPan_ApplyUiFontScale();
    MikuPan_ApplyFatalFrameStyle(mikupan_configuration.selected_theme);

    SDL_DisplayID primary = SDL_GetPrimaryDisplay();

    if (primary != 0)
    {
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(primary);
        render_resolution_width = mikupan_configuration.renderer.render.width;
        render_resolution_height = mikupan_configuration.renderer.render.height;

        MikuPan_PopulateResolutionList(primary, mode);
    }

    msaa_samples = mikupan_configuration.renderer.msaa_index;
    brightness = mikupan_configuration.renderer.brightness;
    gamma_value = mikupan_configuration.renderer.gamma;
    window_mode = mikupan_configuration.renderer.window_mode;

    if (window_mode == MIKUPAN_WINDOW_WINDOWED
        && mikupan_configuration.renderer.is_fullscreen)
    {
        window_mode = MIKUPAN_WINDOW_FULLSCREEN;
    }

    if (window_mode < MIKUPAN_WINDOW_WINDOWED
        || window_mode > MIKUPAN_WINDOW_BORDERLESS)
    {
        window_mode = MIKUPAN_WINDOW_WINDOWED;
    }

    mikupan_configuration.renderer.window_mode = window_mode;
    is_fullscreen = (window_mode != MIKUPAN_WINDOW_WINDOWED);
    mikupan_configuration.renderer.is_fullscreen = is_fullscreen;
    MikuPan_PopulateGpuDriverList();

    if (mikupan_configuration.renderer.shadow_resolution <= 0)
    {
        mikupan_configuration.renderer.shadow_resolution = 256;
    }

    MikuPan_SetShadowResolution(mikupan_configuration.renderer.shadow_resolution);
    crt_settings = mikupan_configuration.crt;
    MikuPan_ClampCrtSettings(&crt_settings);
    mikupan_configuration.crt = crt_settings;
    MikuPan_ApplyThirdPersonCameraConfiguration();
    MikuPan_ControllerSetPreferredGamepadIndex(mikupan_configuration.input.selected_gamepad_index);
    mikupan_configuration.input.selected_gamepad_index = MikuPan_ControllerGetPreferredGamepadIndex();
    MikuPan_ControllerLoadBindingsFromConfig();

    if (mikupan_configuration.renderer.lighting_mode > 1 || mikupan_configuration.renderer.lighting_mode < 0)
    {
        mikupan_configuration.renderer.lighting_mode = 0;
    }

    if (gamma_value < 0.0f || gamma_value > 3.0f)
    {
        gamma_value = 1.0f;
    }

    if (brightness < 0.0f || brightness > 2.0f)
    {
        brightness = 1.0f;
    }

    MikuPan_ImGui_ImplInit(window);
}

void MikuPan_RenderUi(void)
{
    igRender();
    ImGuiIO* io = igGetIO_Nil();
    MikuPan_GPUSetViewport(0, 0, (int) io->DisplaySize.x,
                           (int) io->DisplaySize.y);
    MikuPan_ImGui_ImplRenderDrawData();
}

void MikuPan_StartFrameUi(void)
{
    MikuPan_ImGui_ImplNewFrame();
    igNewFrame();
}

void MikuPan_DrawUi(void)
{
    MikuPan_UiHandleShortcuts();
    MikuPan_UiMenuBar();

    MikuPan_UiDebugWindowsRender();
}

void MikuPan_DrawMissingDataUi(const char *missing_file)
{
    ImGuiIO* io = igGetIO_Nil();
    const float scale = ui_display_scale > 0.0f ? ui_display_scale : 1.0f;
    const float width = 520.0f * scale;
    const float height = 0.0f;
    const char *file = (missing_file != NULL && missing_file[0] != '\0')
                           ? missing_file
                           : "IMG_HD.BIN";

    igSetNextWindowPos(
        (ImVec2){io->DisplaySize.x * 0.5f, io->DisplaySize.y * 0.5f},
        ImGuiCond_Always,
        (ImVec2){0.5f, 0.5f});
    igSetNextWindowSize((ImVec2){width, height}, ImGuiCond_Always);
    if (!igBegin("Game Data Required", NULL,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
                 | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        igEnd();
        return;
    }

    igTextWrapped("MikuPan needs the extracted Fatal Frame disc data before it can start.");
    igSpacing();
    igText("Missing: %s", file);
    igSpacing();
    igTextWrapped("Select the folder that contains IMG_HD.BIN and IMG_BD.BIN.");
    igSpacing();

    if (igButton("Select Folder", (ImVec2){160.0f * scale, 0.0f}))
    {
        MikuPan_RequestDataFolderSelection(file);
    }

    igEnd();
}

void MikuPan_ShutDownUi(void)
{
    MikuPan_ImGui_ImplShutdown();
    igDestroyContext(NULL);
}

void MikuPan_ProcessEventUi(SDL_Event* event)
{
    MikuPan_ImGui_ImplProcessEvent(event);
}

void MikuPan_RequestQuit(void)
{
    /* Routed through the SDL event queue so it follows the same shutdown path
     * as closing the window (SDL_AppEvent returns SDL_APP_SUCCESS, running the
     * normal cleanup in SDL_AppQuit). */
    SDL_Event quit_event;
    SDL_zero(quit_event);
    quit_event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit_event);
}

static int MikuPan_UiGamepadMenuShortcutPressed(void)
{
    static int was_down = 0;
    int down = 0;

    SDL_Gamepad *gamepad = MikuPan_GetController();
    if (gamepad != NULL)
    {
        down = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK)
            && SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START);
    }
    else
    {
        int gamepad_count = 0;
        SDL_JoystickID *gamepad_ids = SDL_GetGamepads(&gamepad_count);
        if (gamepad_ids != NULL)
        {
            for (int i = 0; i < gamepad_count && !down; i++)
            {
                SDL_Gamepad *opened_gamepad =
                    SDL_GetGamepadFromID(gamepad_ids[i]);
                if (opened_gamepad != NULL)
                {
                    down = SDL_GetGamepadButton(
                               opened_gamepad,
                               SDL_GAMEPAD_BUTTON_LEFT_STICK)
                        && SDL_GetGamepadButton(
                               opened_gamepad,
                               SDL_GAMEPAD_BUTTON_START);
                }
            }
            SDL_free(gamepad_ids);
        }
    }

    const int pressed = down && !was_down;
    was_down = down;
    return pressed;
}

void MikuPan_UiHandleShortcuts(void)
{
    if (igIsKeyPressed_Bool(ImGuiKey_F1, 0)
        || MikuPan_UiGamepadMenuShortcutPressed())
    {
        show_menu_bar = !show_menu_bar;
    }

    if (igIsKeyPressed_Bool(ImGuiKey_F2, 0))
    {
        dbg_wrk.mode_on = !dbg_wrk.mode_on;
    }

    if (igIsKeyPressed_Bool(ImGuiKey_F12, 0))
    {
        MikuPan_ScreenshotRequest();
    }
}

static void MikuPan_DataFolderSelected(void* userdata,
                                       const char* const * filelist, int filter)
{
    (void) userdata;
    (void) filter;

    if (filelist == NULL || filelist[0] == NULL)
    {
        return;
    }

    strncpy(mikupan_configuration.data_folder, filelist[0],
            sizeof(mikupan_configuration.data_folder) - 1);
    mikupan_configuration
        .data_folder[sizeof(mikupan_configuration.data_folder) - 1] = '\0';
}

void MikuPan_UiMenuBar(void)
{
    MikuPan_CheatSyncPhotoMode();

    if (!show_menu_bar || !igBeginMainMenuBar())
    {
        return;
    }

    if (igBeginMenu("Settings", 1))
    {
        if (igBeginMenu("Display", 1))
        {
            const char* window_modes[] = {"Windowed", "Fullscreen",
                                          "Borderless Fullscreen"};
            if (igCombo_Str_arr("Display Mode", &window_mode, window_modes, 3,
                                -1))
            {
                if (window_mode < MIKUPAN_WINDOW_WINDOWED
                    || window_mode > MIKUPAN_WINDOW_BORDERLESS)
                {
                    window_mode = MIKUPAN_WINDOW_WINDOWED;
                }
                is_fullscreen = (window_mode != MIKUPAN_WINDOW_WINDOWED);
            }

            if (resolution_count > 0)
            {
                if (igCombo_Str_arr("Resolution", &resolution_selected,
                                    resolution_label_ptrs, resolution_count,
                                    -1))
                {
                    render_resolution_width =
                        resolution_list[resolution_selected].width;
                    render_resolution_height =
                        resolution_list[resolution_selected].height;
                }
            }
            else
            {
                igTextDisabled("Resolution: no display modes available");
            }

            igSliderFloat("Brightness", &brightness, 0.0f, 2.0f, "%.2f", 0);
            igSliderFloat("Gamma", &gamma_value, 0.1f, 3.0f, "%.2f", 0);
            igCheckbox("VSync", (bool*) &mikupan_configuration.renderer.vsync);

            igSeparatorText("Graphics");

            MikuPan_UiGpuBackendCombo();

            if (strcmp(SDL_GetPlatform(), "Android") == 0)
            {
                igTextDisabled("MSAA: disabled on Android");
            }
            else
            {
                char msaa_dropdown_list[32];
                snprintf(msaa_dropdown_list, sizeof(msaa_dropdown_list), "%d",
                         msaa_list[msaa_samples]);

                if (igBeginCombo("MSAA", msaa_dropdown_list, 0))
                {
                    for (int i = 0; i < sizeof(msaa_list)/sizeof(msaa_list[0]); i++)
                    {
                        bool is_selected = (msaa_samples == i);
                        snprintf(msaa_dropdown_list, sizeof(msaa_dropdown_list),
                                 "%d", msaa_list[i]);

                        if (igSelectable_Bool(msaa_dropdown_list, is_selected, 0,
                                              (ImVec2) {0, 0}))
                        {
                            msaa_samples = i;
                        }

                        if (is_selected)
                        {
                            igSetItemDefaultFocus();
                        }
                    }

                    igEndCombo();
                }

                igTextDisabled("Scene samples: %dx", MikuPan_GPUGetSceneMSAA());
            }

            MikuPan_UiShadowResolutionCombo("Shadow Resolution");

            const char* display_lighting_modes[] = {"Pixel (Modern)",
                                                    "Vertex (PS2)"};
            igCombo_Str_arr("Lighting Mode", &mikupan_configuration.renderer.lighting_mode,
                            display_lighting_modes, 2, -1);

            igEndMenu();
        }

        if (igBeginMenu("Theme", 1))
        {
            int selected_theme =
                MikuPan_ClampThemeIndex(mikupan_configuration.selected_theme);
            if (selected_theme != mikupan_configuration.selected_theme)
            {
                mikupan_configuration.selected_theme = selected_theme;
            }

            if (igCombo_Str_arr("Theme", &selected_theme, theme_labels,
                                MIKUPAN_UI_THEME_COUNT, -1))
            {
                mikupan_configuration.selected_theme = selected_theme;
                MikuPan_ApplyFatalFrameStyle(selected_theme);
            }

            int selected_font =
                MikuPan_ClampFontIndex(mikupan_configuration.selected_font);
            if (selected_font != mikupan_configuration.selected_font)
            {
                mikupan_configuration.selected_font = selected_font;
            }

            if (igCombo_Str_arr("Font", &selected_font, font_labels,
                                MIKUPAN_UI_FONT_COUNT, -1))
            {
                mikupan_configuration.selected_font = selected_font;
                MikuPan_ApplyUiFont(selected_font);
            }

            float font_scale = mikupan_configuration.font_scale;
            if (igSliderFloat("Font Size", &font_scale, 0.5f, 3.0f, "%.2fx", 0))
            {
                mikupan_configuration.font_scale = font_scale;
                MikuPan_ApplyUiFontScale();
            }

            igEndMenu();
        }

        if (igBeginMenu("CRT", 1))
        {
            igCheckbox("Enabled", (bool*) &crt_settings.enabled);
            if (crt_settings.enabled)
            {
                igSliderFloat("Strength", &crt_settings.strength, 0.0f, 1.0f,
                          "%.2f", 0);
                igSliderFloat("Curvature", &crt_settings.curvature, 0.0f, 0.30f,
                              "%.3f", 0);
                igSliderFloat("Overscan", &crt_settings.overscan, 0.0f, 0.12f,
                              "%.3f", 0);

                igSeparator();
                igSliderFloat("Scanline Strength", &crt_settings.scanline_strength,
                              0.0f, 1.0f, "%.2f", 0);
                igSliderFloat("Scanline Scale", &crt_settings.scanline_scale, 0.25f,
                              3.0f, "%.2f", 0);
                igSliderFloat("Scanline Thickness",
                              &crt_settings.scanline_thickness, 0.5f, 4.0f, "%.2f",
                              0);

                igSeparator();
                igSliderFloat("Mask Strength", &crt_settings.mask_strength, 0.0f,
                              1.0f, "%.2f", 0);
                igSliderFloat("Mask Scale", &crt_settings.mask_scale, 0.5f, 4.0f,
                              "%.2f", 0);

                igSeparator();
                igSliderFloat("Vignette Strength", &crt_settings.vignette_strength,
                              0.0f, 1.0f, "%.2f", 0);
                igSliderFloat("Vignette Size", &crt_settings.vignette_size, 0.25f,
                              1.25f, "%.2f", 0);
                igSliderFloat("Chroma Offset", &crt_settings.chroma_offset, 0.0f,
                              3.0f, "%.2f", 0);
                igSliderFloat("Blend Strength", &crt_settings.blend_strength, 0.0f,
                              1.0f, "%.2f", 0);
                igSliderFloat("Blend Radius", &crt_settings.blend_radius, 0.0f,
                              3.0f, "%.2f", 0);
                igSliderFloat("Noise", &crt_settings.noise_strength, 0.0f, 0.15f,
                              "%.3f", 0);
                igSliderFloat("Flicker", &crt_settings.flicker_strength, 0.0f,
                              0.10f, "%.3f", 0);
                igSliderFloat("Glow", &crt_settings.glow_strength, 0.0f, 0.50f,
                              "%.2f", 0);

                MikuPan_ClampCrtSettings(&crt_settings);

                if (igButton("Reset CRT", (ImVec2) {0.0f, 0.0f}))
                {
                    crt_settings = crt_defaults;
                }
            }


            igEndMenu();
        }

        if (igBeginMenu("Input", 1))
        {
            MikuPan_ControllerDrawDeviceSelectorUi();
            igSeparator();

            igCheckbox("Controller / Joystick Mapping",
                       (bool*) &show_controller_remap);

            if (igMenuItem_Bool("Reset Bindings to Defaults", NULL, false,
                                true))
            {
                MikuPan_ControllerResetBindings();
            }

            igEndMenu();
        }

        igSeparatorText("Game Data Folder");
        igInputText("Folder", mikupan_configuration.data_folder, sizeof(mikupan_configuration.data_folder), 0, NULL, NULL);

        if (igButton("Browse...", (ImVec2) {0.0f, 0.0f}))
        {
            const char *start = mikupan_configuration.data_folder[0] != '\0'
                                    ? mikupan_configuration.data_folder
                                    : NULL;
#ifdef __ANDROID__
            SDL_SetHint("SDL_ANDROID_ALLOW_PERSISTENT_FOLDER_ACCESS", "1");
#endif
            SDL_ShowOpenFolderDialog(MikuPan_DataFolderSelected, NULL,
                                     ui_window, start, false);
        }

        igSeparator();

        if (igMenuItem_Bool("Save Configuration", NULL, false, true))
        {
            MikuPan_UiSaveConfiguration();
        }

        if (config_save_status[0] != '\0')
        {
            igTextDisabled("%s", config_save_status);
        }

        igEndMenu();
    }

    MikuPan_UiDebugMenuRender();

    MikuPan_UiCheatsRender();

    if (igBeginMenu("Info", 1))
    {
        igText("MikuPan");
        igSeparator();
        igText("Tag:      %s", MIKUPAN_GIT_TAG);
        igText("Commit:   %s", MIKUPAN_GIT_COMMIT);
        igText("Version:  %s", MIKUPAN_GIT_DESCRIBE);
        igText("Branch:   %s", MIKUPAN_GIT_BRANCH);
        igText("Built:    %s", MIKUPAN_BUILD_DATE);

        igSeparator();
        char version_line[256];
        snprintf(version_line, sizeof(version_line), "%s (%s)",
                 MIKUPAN_GIT_DESCRIBE, MIKUPAN_GIT_COMMIT);
        if (igButton("Copy version to clipboard", (ImVec2) {0.0f, 0.0f}))
        {
            igSetClipboardText(version_line);
        }

        igEndMenu();
    }

    igEndMainMenuBar();
}

int MikuPan_GetRenderResolutionWidth(void)
{
    return render_resolution_width;
}

int MikuPan_GetRenderResolutionHeight(void)
{
    return render_resolution_height;
}

int MikuPan_GetMSAA(void)
{
    return msaa_list[msaa_samples];
}

int MikuPan_ShowControllerRemapWindow(void)
{
    return show_controller_remap;
}

float MikuPan_GetBrightness(void)
{
    return brightness;
}

float MikuPan_GetGamma(void)
{
    return gamma_value;
}

const MikuPan_ConfigCrt* MikuPan_GetCrtSettings(void)
{
    return &crt_settings;
}

int MikuPan_IsFullScreen(void)
{
    return is_fullscreen;
}

int MikuPan_GetWindowMode(void)
{
    return window_mode;
}

int MikuPan_IsVsync(void)
{
    return mikupan_configuration.renderer.vsync;
}
