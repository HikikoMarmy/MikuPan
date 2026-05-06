#include "mikupan_shader.h"

#include "glad/gl.h"
#include "mikupan/mikupan_file_c.h"
#include <stdlib.h>
#include <string.h>

GLuint current_program = 0;
GLuint backup_current_program = 0;
u_int shader_list[MAX_SHADER_PROGRAMS] = {0};

/// Cached uniform locations per shader. glGetUniformLocation is a string lookup
/// in the driver and is slow; we resolve the hot uniforms once at link time.
typedef struct
{
    GLint model;
    GLint view;
    GLint projection;
    /// Precomputed derived matrices — pushed once per object instead of being
    /// recomputed inside the vertex shader for every vertex.
    GLint mvp;             ///< projection * view * model
    GLint modelView;       ///< view * model
    GLint viewProj;        ///< projection * view (no model factor; used by 0xA)
    GLint normalMatrix;    ///< transpose(inverse(view * model)) — mat3
    GLint viewNormalMatrix;///< transpose(inverse(view))         — mat3 (0xA)

    /* Non-matrix uniforms */
    GLint uColor;          ///< bounding_box.vert
    GLint renderNormals;   ///< textured_mesh_lighted.frag
    GLint uColorScale;     ///< textured_mesh_lighted.frag
    GLint uNormalLength;   ///< normals_debug.geom
    GLint uFog;            ///< textured_mesh_lighted.frag
    GLint uFogColor;       ///< textured_mesh_lighted.frag
    GLint disableLighting;   ///< textured_mesh_lighted.frag — UI debug toggle
    GLint uMeshLightingMode; ///< textured_mesh_lighted.frag — 0=full dynamic, 1=parallel-only (per mesh type)
} CachedUniformLocations;

static CachedUniformLocations uniform_loc[MAX_SHADER_PROGRAMS] = {0};

/// Linear scan — MAX_SHADER_PROGRAMS is 8, so this is faster than maintaining
/// a parallel "current index" tracker that has to stay in sync with external
/// glUseProgram calls (e.g. via MikuPan_ResetShaderCache).
static int FindShaderIndex(GLuint program)
{
    for (int i = 0; i < MAX_SHADER_PROGRAMS; i++)
    {
        if (shader_list[i] == program) return i;
    }
    return -1;
}

/// Resolve a uniform name to its cached location for shader `idx`.
/// Returns -1 when the shader doesn't declare the uniform (glUniform* with -1
/// is a no-op, so callers can skip the bind/draw entirely). Falls back to a
/// driver query for unknown names — that path is correct but slow; if a name
/// shows up here in a profile, add it to CachedUniformLocations.
static GLint GetCachedLocation(int idx, const char *name)
{
    if (idx < 0 || idx >= MAX_SHADER_PROGRAMS)
    {
        return -1;
    }
    const CachedUniformLocations *u = &uniform_loc[idx];

    if (strcmp(name, "model")            == 0) return u->model;
    if (strcmp(name, "view")             == 0) return u->view;
    if (strcmp(name, "projection")       == 0) return u->projection;
    if (strcmp(name, "mvp")              == 0) return u->mvp;
    if (strcmp(name, "modelView")        == 0) return u->modelView;
    if (strcmp(name, "viewProj")         == 0) return u->viewProj;
    if (strcmp(name, "normalMatrix")     == 0) return u->normalMatrix;
    if (strcmp(name, "viewNormalMatrix") == 0) return u->viewNormalMatrix;
    if (strcmp(name, "uColor")           == 0) return u->uColor;
    if (strcmp(name, "renderNormals")    == 0) return u->renderNormals;
    if (strcmp(name, "uColorScale")      == 0) return u->uColorScale;
    if (strcmp(name, "uNormalLength")    == 0) return u->uNormalLength;
    if (strcmp(name, "uFog")             == 0) return u->uFog;
    if (strcmp(name, "uFogColor")        == 0) return u->uFogColor;
    if (strcmp(name, "disableLighting")  == 0) return u->disableLighting;
    if (strcmp(name, "uMeshLightingMode") == 0) return u->uMeshLightingMode;

    return glad_glGetUniformLocation(shader_list[idx], name);
}

// [vert, geom (NULL = none), frag]
const char* shader_file_name[MAX_SHADER_PROGRAMS][3] = {
    {"./resources/shaders/mesh_0x2.vert",                    NULL,                                        "./resources/shaders/textured_mesh_lighted.frag"},
    {"./resources/shaders/mesh_0xA.vert",                    NULL,                                        "./resources/shaders/textured_mesh_lighted.frag"},
    {"./resources/shaders/mesh_0x12.vert",                   NULL,                                        "./resources/shaders/textured_mesh_lighted.frag"},
    {"./resources/shaders/untextured_coloured_sprite.vert",  NULL,                                        "./resources/shaders/untextured_coloured_sprite.frag"},
    {"./resources/shaders/bounding_box.vert",                NULL,                                        "./resources/shaders/untextured_coloured_sprite.frag"},
    {"./resources/shaders/sprite.vert",                      NULL,                                        "./resources/shaders/sprite.frag"},
    {"./resources/shaders/mesh_0x12.vert",                   "./resources/shaders/normals_debug.geom",    "./resources/shaders/normals_debug.frag"},
    {"./resources/shaders/mesh_0x2.vert",                    "./resources/shaders/normals_debug.geom",    "./resources/shaders/normals_debug.frag"},
    // Final scene-to-window blit. Vertex layout matches sprite.vert
    // (UV4_COLOUR4_POSITION4) so the same fullscreen-quad VBO drives it.
    {"./resources/shaders/sprite.vert",                      NULL,                                        "./resources/shaders/postprocess.frag"},
};

int MikuPan_InitShaders()
{
    for (int i = 0 ; i < MAX_SHADER_PROGRAMS ; i++)
    {
        const char* vertex_shader_filename = shader_file_name[i][0];
        u_int shader_file_size = MikuPan_GetFileSize(vertex_shader_filename) + 1;

        char* vertex_shader_buffer = malloc(shader_file_size);
        vertex_shader_buffer[shader_file_size - 1] = 0;
        MikuPan_ReadFullFile(vertex_shader_filename, vertex_shader_buffer);

        GLuint vertexShader = glad_glCreateShader(GL_VERTEX_SHADER);
        glad_glShaderSource(vertexShader, 1, (const GLchar *const *)&vertex_shader_buffer, NULL);
        glad_glCompileShader(vertexShader);

        const char* frag_shader_filename = shader_file_name[i][2];
        shader_file_size = MikuPan_GetFileSize(frag_shader_filename) + 1;

        char* frag_buffer = malloc(shader_file_size);
        frag_buffer[shader_file_size - 1] = 0;
        MikuPan_ReadFullFile(frag_shader_filename, frag_buffer);

        GLuint fragmentShader = glad_glCreateShader(GL_FRAGMENT_SHADER);
        glad_glShaderSource(fragmentShader, 1, (const GLchar *const *)&frag_buffer, NULL);
        glad_glCompileShader(fragmentShader);

        current_program = glad_glCreateProgram();
        shader_list[i] = current_program;

        glad_glAttachShader(current_program, vertexShader);
        glad_glAttachShader(current_program, fragmentShader);

        GLuint geomShader = 0;
        const char* geom_shader_filename = shader_file_name[i][1];
        if (geom_shader_filename != NULL)
        {
            shader_file_size = MikuPan_GetFileSize(geom_shader_filename) + 1;
            char* geom_buffer = malloc(shader_file_size);
            geom_buffer[shader_file_size - 1] = 0;
            MikuPan_ReadFullFile(geom_shader_filename, geom_buffer);

            geomShader = glad_glCreateShader(GL_GEOMETRY_SHADER);
            glad_glShaderSource(geomShader, 1, (const GLchar *const *)&geom_buffer, NULL);
            glad_glCompileShader(geomShader);
            glad_glAttachShader(current_program, geomShader);

            free(geom_buffer);
        }

        glad_glLinkProgram(current_program);

        glad_glDeleteShader(vertexShader);
        glad_glDeleteShader(fragmentShader);
        if (geomShader) glad_glDeleteShader(geomShader);

        free(vertex_shader_buffer);
        free(frag_buffer);

        // Resolve hot uniform locations once, instead of paying for a string
        // lookup per call site. -1 means the shader doesn't declare that
        // uniform; that's fine — glUniform* with location -1 is a no-op.
        uniform_loc[i].model            = glad_glGetUniformLocation(current_program, "model");
        uniform_loc[i].view             = glad_glGetUniformLocation(current_program, "view");
        uniform_loc[i].projection       = glad_glGetUniformLocation(current_program, "projection");
        uniform_loc[i].mvp              = glad_glGetUniformLocation(current_program, "mvp");
        uniform_loc[i].modelView        = glad_glGetUniformLocation(current_program, "modelView");
        uniform_loc[i].viewProj         = glad_glGetUniformLocation(current_program, "viewProj");
        uniform_loc[i].normalMatrix     = glad_glGetUniformLocation(current_program, "normalMatrix");
        uniform_loc[i].viewNormalMatrix = glad_glGetUniformLocation(current_program, "viewNormalMatrix");
        uniform_loc[i].uColor           = glad_glGetUniformLocation(current_program, "uColor");
        uniform_loc[i].renderNormals    = glad_glGetUniformLocation(current_program, "renderNormals");
        uniform_loc[i].uColorScale      = glad_glGetUniformLocation(current_program, "uColorScale");
        uniform_loc[i].uNormalLength    = glad_glGetUniformLocation(current_program, "uNormalLength");
        uniform_loc[i].uFog             = glad_glGetUniformLocation(current_program, "uFog");
        uniform_loc[i].uFogColor        = glad_glGetUniformLocation(current_program, "uFogColor");
        uniform_loc[i].disableLighting  = glad_glGetUniformLocation(current_program, "disableLighting");
        uniform_loc[i].uMeshLightingMode = glad_glGetUniformLocation(current_program, "uMeshLightingMode");
    }

    glad_glUseProgram(current_program);

    return 0;
}

u_int MikuPan_SetCurrentShaderProgram(int shader_program)
{
    if (shader_program >= MAX_SHADER_PROGRAMS)
    {
        return -1;
    }

    GLuint new_program = shader_list[shader_program];
    if (new_program != current_program)
    {
        current_program = new_program;
        glad_glUseProgram(current_program);
    }
    return current_program;
}

u_int MikuPan_GetCurrentShaderProgram()
{
    return current_program;
}

void MikuPan_SetUniformMatrix4fvToAllShaders(float *mat, char *name)
{
    for (int i = 0; i < MAX_SHADER_PROGRAMS; i++)
    {
        GLint loc = GetCachedLocation(i, name);
        if (loc < 0) continue; // shader doesn't declare this uniform — skip it entirely
        MikuPan_SetCurrentShaderProgram(i);
        glad_glUniformMatrix4fv(loc, 1, GL_FALSE, mat);
    }
}

void MikuPan_SetUniformMatrix3fvToAllShaders(float *mat, char *name)
{
    for (int i = 0; i < MAX_SHADER_PROGRAMS; i++)
    {
        GLint loc = GetCachedLocation(i, name);
        if (loc < 0) continue;
        MikuPan_SetCurrentShaderProgram(i);
        glad_glUniformMatrix3fv(loc, 1, GL_FALSE, mat);
    }
}

void MikuPan_SetUniformMatrix4fvToCurrentShader(float *mat, char *name)
{
    GLint loc = GetCachedLocation(FindShaderIndex(current_program), name);
    if (loc < 0) return;
    glad_glUniformMatrix4fv(loc, 1, GL_FALSE, mat);
}

void MikuPan_SetUniformMatrix3fvToCurrentShader(float *mat, char *name)
{
    GLint loc = GetCachedLocation(FindShaderIndex(current_program), name);
    if (loc < 0) return;
    glad_glUniformMatrix3fv(loc, 1, GL_FALSE, mat);
}

void MikuPan_SetUniform4fvToAllShaders(float *vector, char *name)
{
    for (int i = 0; i < MAX_SHADER_PROGRAMS; i++)
    {
        GLint loc = GetCachedLocation(i, name);
        if (loc < 0) continue;
        MikuPan_SetCurrentShaderProgram(i);
        glad_glUniform4fv(loc, 1, vector);
    }
}

void MikuPan_SetUniform4fvToCurrentShader(float *vector, char *name)
{
    GLint loc = GetCachedLocation(FindShaderIndex(current_program), name);
    if (loc < 0) return;
    glad_glUniform4fv(loc, 1, vector);
}

void MikuPan_SetUniform1iToAllShaders(int value, char *name)
{
    for (int i = 0; i < MAX_SHADER_PROGRAMS; i++)
    {
        GLint loc = GetCachedLocation(i, name);
        if (loc < 0) continue;
        MikuPan_SetCurrentShaderProgram(i);
        glad_glUniform1i(loc, value);
    }
}

void MikuPan_SetUniform1iToCurrentShader(int value, char *name)
{
    GLint loc = GetCachedLocation(FindShaderIndex(current_program), name);
    if (loc < 0) return;
    glad_glUniform1i(loc, value);
}

void MikuPan_SetUniform1fToAllShaders(float value, char *name)
{
    for (int i = 0; i < MAX_SHADER_PROGRAMS; i++)
    {
        GLint loc = GetCachedLocation(i, name);
        if (loc < 0) continue;
        MikuPan_SetCurrentShaderProgram(i);
        glad_glUniform1f(loc, value);
    }
}

void MikuPan_SetUniform1fToCurrentShader(float value, char *name)
{
    GLint loc = GetCachedLocation(FindShaderIndex(current_program), name);
    if (loc < 0) return;
    glad_glUniform1f(loc, value);
}

void MikuPan_ResetShaderCache(void)
{
    // After ImGui (or any external) glUseProgram, our cached value is stale.
    // Force the next MikuPan_SetCurrentShaderProgram call to actually run.
    current_program = 0;
}