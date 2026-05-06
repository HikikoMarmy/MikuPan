#ifndef MIKUPAN_MIKUPAN_SHADER_H
#define MIKUPAN_MIKUPAN_SHADER_H
#include "typedefs.h"

enum ShaderPrograms
{
    MESH_0x2_SHADER,
    MESH_0xA_SHADER,
    MESH_0x12_SHADER,
    UNTEXTURED_COLOURED_SPRITE_SHADER,
    BOUNDING_BOX_SHADER,
    SPRITE_SHADER,
    NORMALS_0x12_SHADER,
    NORMALS_0x2_SHADER,
    POSTPROCESS_SHADER,
    MAX_SHADER_PROGRAMS
};

enum UniformBuffers
{
    LightBlock    = 0,
    MaterialBlock = 1
};

extern u_int shader_list[MAX_SHADER_PROGRAMS];

int MikuPan_InitShaders();
u_int MikuPan_SetCurrentShaderProgram(int shader_program);
u_int MikuPan_GetCurrentShaderProgram();
void MikuPan_SetUniformMatrix4fvToAllShaders(float* mat, char* name);
void MikuPan_SetUniformMatrix3fvToAllShaders(float* mat, char* name);
void MikuPan_SetUniformMatrix4fvToCurrentShader(float* mat, char* name);
void MikuPan_SetUniformMatrix3fvToCurrentShader(float* mat, char* name);
void MikuPan_SetUniform4fvToAllShaders(float* vector, char* name);
void MikuPan_SetUniform4fvToCurrentShader(float* vector, char* name);
void MikuPan_SetUniform1iToAllShaders(int value, char* name);
void MikuPan_SetUniform1iToCurrentShader(int value, char* name);
void MikuPan_SetUniform1fToAllShaders(float value, char* name);
void MikuPan_SetUniform1fToCurrentShader(float value, char* name);
void MikuPan_ResetShaderCache(void);

#endif//MIKUPAN_MIKUPAN_SHADER_H
