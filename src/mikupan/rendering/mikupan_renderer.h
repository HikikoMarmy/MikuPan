#ifndef MIKUPAN_SDL_RENDERER_H
#define MIKUPAN_SDL_RENDERER_H
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_video.h"
#include "ee/eestruct.h"
#include "graphics/graph2d/sprt.h"
#include "graphics/graph3d/light_types.h"
#include "mikupan/mikupan_basictypes.h"
#include "mikupan/mikupan_types.h"

typedef struct {
    sceVu0FVECTOR p;
    sceVu0FVECTOR i;
    float roll;
    float fov;
    float nearz;
    float farz;
    float ax;
    float ay;
    float cx;
    float cy;
    float zmin;
    float zmax;
    float pad[2];
    sceVu0FMATRIX vs;
    sceVu0FMATRIX vc;
    sceVu0FMATRIX vcv;
    sceVu0FMATRIX wv;
    sceVu0FMATRIX ws;
    sceVu0FMATRIX wc;
    sceVu0FMATRIX wcv;
    sceVu0FVECTOR zd;
    sceVu0FVECTOR yd;
} MikuPan_Camera;

typedef struct
{
    SDL_Window *window;
    int width;
    int height;
} MikuPan_RenderWindow;

SDL_AppResult MikuPan_Init();
void MikuPan_SetupOpenGLContext();
void MikuPan_Clear();
void MikuPan_CreateInternalBuffer(int w, int h, int msaa);
void MikuPan_DestroyInternalBuffer();
void MikuPan_UpdateWindowSize(int width, int height);
int MikuPan_GetWindowWidth();
int MikuPan_GetWindowHeight();
int MikuPan_GetRenderMode();
void MikuPan_RenderSetDebugValues();
void MikuPan_Render2DMessage(DISP_SPRT* sprite);
void MikuPan_RenderLine(float x1, float y1, float x2, float y2, u_char r, u_char g, u_char b, u_char a);
void MikuPan_RenderBoundingBox(sceVu0FVECTOR* vertices);
void MikuPan_RenderSprite(MikuPan_Rect src, MikuPan_Rect dst, u_char r, u_char g, u_char b, u_char a, MikuPan_TextureInfo* texture_info);
void MikuPan_RenderSprite2D(sceGsTex0 *tex, float* buffer);
void MikuPan_RenderUntexturedSprite(float* buffer);
void MikuPan_RenderSprite3D(sceGsTex0 *tex, float* buffer);
void MikuPan_SetupFntTexture();
void MikuPan_SetWorldClipView();
float* MikuPan_GetWorldClipView();
float* MikuPan_GetWorldClip();
void MikuPan_SetupAmbientLighting(const LIGHT_PACK* lp, float *eyevec);
void MikuPan_SetupAmbientLighting2();
/// Called from sglight.c:SetMaterialData when the active SgMaterialC changes.
/// Pushes the four colour vectors (Ambient/Diffuse/Specular/Emission) into the
/// MaterialBlock UBO so the fragment shader can apply them per the original
/// PS2 lighting maths (sglight.c:505-531).
void MikuPan_SetMaterial(const sceVu0FVECTOR* ambient,
                         const sceVu0FVECTOR* diffuse,
                         const sceVu0FVECTOR* specular,
                         const sceVu0FVECTOR* emission);
void MikuPan_SetFontTexture(int fnt);
void MikuPan_DeleteTexture(MikuPan_TextureInfo* texture_info);
MikuPan_TextureInfo* MikuPan_CreateGLTexture(sceGsTex0 *tex0);
void MikuPan_SetTexture(sceGsTex0 *tex0);
void MikuPan_SetupCamera(MikuPan_Camera *camera);
void MikuPan_SetupMirrorMtx(float* wv);
void MikuPan_Setup3D();
void MikuPan_Shutdown();
void MikuPan_EndFrame();
void MikuPan_SetModelTransformMatrix(sceVu0FVECTOR* m);
void MikuPan_RenderMeshType0x32(SGDPROCUNITHEADER *pVUVN, SGDPROCUNITHEADER *pPUHead);
void MikuPan_RenderMeshType0x82(unsigned int* pVUVN, unsigned int *pPUHead);
void MikuPan_RenderMeshType0x2(SGDPROCUNITHEADER* pVUVN, SGDPROCUNITHEADER *pPUHead, float* vertices);
void MikuPan_FlushMeshBatch(void);
void MikuPan_FlushStaticMeshCache(void);
void MikuPan_FlushTexturedSpriteBatch(void);

u_long GSAlphaToOpenGL(int A, int B, int C, int D, int fix);

/// Resolve+downsample the currently-bound DRAW framebuffer (the MSAA scene
/// buffer when called mid-frame) into an RGBA8 buffer at PS2 native size,
/// with a top-left origin so the GS-memory side can stuff the bytes
/// straight into PSMCT32. Returns 1 on success, 0 if the source FBO has no
/// usable colour attachment. Used by mikupan_gs.cpp's
/// MikuPan_GsCaptureFramebuffer (the freeze-frame bridge); kept here
/// because glad and the FBO struct live in this TU.
int MikuPan_ReadFramebufferRGBA8TopLeft(int width, int height, unsigned char *out_rgba);

#endif //MIKUPAN_SDL_RENDERER_H