#ifndef MIKUPAN_MIKUPAN_UTILS_H
#define MIKUPAN_MIKUPAN_UTILS_H

#define PS2_RESOLUTION_X_FLOAT 640.0f
#define PS2_RESOLUTION_X_INT 640
#define PS2_RESOLUTION_Y_FLOAT 448.0f
#define PS2_RESOLUTION_Y_INT 448
#define PS2_CENTER_X 320.0f
#define PS2_CENTER_Y 224.0f
#include "typedefs.h"

void MikuPan_ConvertPs2ScreenCoordToNDCMaintainAspectRatio(float* out, float screen_width, float screen_height, float x, float y);
void MikuPan_ConvertPs2HalfScreenCoordToNDCMaintainAspectRatio(float* out, float screen_width, float screen_height, float x, float y);
float MikuPan_ConvertScaleColor(unsigned char color_fragment);
float MikuPan_ConvertColorFloat(unsigned char color_fragment);
unsigned char MikuPan_GamePadAxisToPS2(int sdl_axis, int deadzone);
void MikuPan_GetPS2Viewport(int width, int height, float *vx, float *vy, float *vw, float *vh, float *scale);
void MikuPan_FixUV(float* uv, int num);
void MikuPan_FixColors(float *color_buf, int num);
int MikuPan_SetTriangleIndex(int* triangle_index, int vertex_count, int vertex_offset, int index_write_offset);
unsigned int *MikuPan_GetNextUnpackAddr(unsigned int *prim);
unsigned char* MikuPan_ConvertImageAlpha(unsigned char* img, int width, int height);
unsigned char MikuPan_AdjustPS2Alpha(unsigned char alpha);
int MikuPan_IsVisibleOnScreen(const sceVu0FVECTOR* vector);
void MikuPan_GSToNDC(int Xgs, int Ygs, int Zgs, float* x, float* y, float* z, float window_width, float window_height);
void MikuPan_ConvertScreenToNDCCoord(int* out, float ref_width, float ref_height, float target_width, float target_height);

/// Convert a 2D PS2 GS framebuffer pixel coordinate to NDC (-1..1), letter-
/// boxed to a 4:3-ish PS2 image area inside the current render target so the
/// on-screen position is correct at any buffer/window resolution.
///
/// `out` receives [ndc_x, ndc_y]. The PS2 GS framebuffer is logically a
/// 4096×4096 plane with the origin at (2048, 2048); the visible viewport is
/// 640×224 (per SCR_WIDTH/SCR_HEIGHT in sdk/ee/eestruct.h) so visible X spans
/// 1728..2368 and visible Y spans 1936..2160. Y is flipped (GS Y grows down,
/// NDC Y grows up). Field-Y (0..224) is internally stretched to frame-Y
/// (0..448) so the shared aspect-correct mapper sees the established 640×448
/// convention; the result is the PS2 image centered inside the buffer with
/// horizontal or vertical letterboxing as needed.
void MikuPan_ConvertPs2GSCoordToNDC(float* out, float window_width, float window_height, float gs_x, float gs_y);

/// Sub-pixel sibling of MikuPan_ConvertPs2GSCoordToNDC.
///
/// PS2 GS XYZF2/XYZ2 wire coordinates are 1/16-pixel fixed-point (the
/// FLT_TO_FIX4 macro in libvu0.c yields int(value * 16)), so a sprite near
/// the framebuffer origin lands around 32768 — not 2048. Use this when the
/// caller already has sub-pixel ints (e.g. anything that flows through
/// sceVu0RotTransPers's mode==0 integer path or the ipos arrays in
/// effect_oth.c). Internally just divides by 16 and forwards to the pixel
/// version, but kept as a separate entry point so call sites read clearly.
void MikuPan_ConvertPs2GSSubPixelToNDC(float* out, float window_width, float window_height, int gs_sub_x, int gs_sub_y);
#endif//MIKUPAN_MIKUPAN_UTILS_H
