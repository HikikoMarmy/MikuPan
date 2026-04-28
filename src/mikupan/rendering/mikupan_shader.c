#include "mikupan_shader.h"

#include "glad/gl.h"
#include "mikupan/mikupan_file_c.h"
#include <stdlib.h>

GLuint current_program = 0;
GLuint backup_current_program = 0;
u_int shader_list[MAX_SHADER_PROGRAMS] = {0};

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

    current_program = shader_list[shader_program];
    glad_glUseProgram(current_program);
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
        glad_glUniformMatrix4fv(
            glad_glGetUniformLocation(MikuPan_SetCurrentShaderProgram(i), name),
            1, GL_FALSE,
            (float *) mat);
    }
}

void MikuPan_SetUniform4fvToAllShaders(float *vector, char *name)
{
    for (int i = 0; i < MAX_SHADER_PROGRAMS; i++)
    {
        MikuPan_SetCurrentShaderProgram(i);
        MikuPan_SetUniform4fvToCurrentShader(vector, name);
    }
}

void MikuPan_SetUniform4fvToCurrentShader(float *vector, char *name)
{
    glad_glUniform4fv(
            glad_glGetUniformLocation(MikuPan_GetCurrentShaderProgram(), name),
            1,
            (float *) vector);
}

void MikuPan_SetUniform1iToAllShaders(int value, char *name)
{
    for (int i = 0; i < MAX_SHADER_PROGRAMS; i++)
    {
        glad_glUniform1i(
            glad_glGetUniformLocation(MikuPan_SetCurrentShaderProgram(i), name),
            value);
    }
}

void MikuPan_SetUniform1fToAllShaders(float value, char *name)
{
    for (int i = 0; i < MAX_SHADER_PROGRAMS; i++)
    {
        glad_glUniform1f(
            glad_glGetUniformLocation(MikuPan_SetCurrentShaderProgram(i), name),
            value);
    }
}