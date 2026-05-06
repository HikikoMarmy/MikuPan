#ifndef MIKUPAN_PROFILER_H
#define MIKUPAN_PROFILER_H

#include "SDL3/SDL_stdinc.h"  /* Uint64 */
#include "SDL3/SDL_timer.h"   /* SDL_GetPerformanceCounter — used by MIKUPAN_PERF_SCOPE */
#include <glad/gl.h>          /* GLenum / GLsizei / GLint — used by timed-draw wrappers */

#ifdef __cplusplus
extern "C" {
#endif

/// CPU section IDs for the per-section perf graph. Stable integers — the UI
/// panel feeds matching values into MikuPan_PerfGetSectionMs(). Append new
/// IDs at the end so existing IDs keep their values.
typedef enum
{
    PERF_SECT_MESH_RENDER = 0,    ///< total time in MikuPan_RenderMeshType* calls (outer)
    PERF_SECT_SPRITE_RENDER,      ///< total time in 2D sprite/UI draws
    PERF_SECT_BATCH_FLUSH,        ///< MikuPan_FlushMeshBatch / FlushTexturedSpriteBatch
    PERF_SECT_DRAWUI,             ///< MikuPan_DrawUi (game UI text + ImGui submission)
    PERF_SECT_RENDERUI,           ///< MikuPan_RenderUi (ImGui actual render)
    PERF_SECT_DRAW_SUBMIT,        ///< wall-clock spent inside glDrawElements/glDrawArrays
    PERF_SECT_BUFFER_UPLOAD,      ///< wall-clock in stream-upload helper (map+memcpy+unmap)
    PERF_SECT_STATE_CHANGE,       ///< wall-clock in shader/texture/render-state setters (outer)
    PERF_SECT_SC_SHADER,          ///< sub: glUseProgram / MikuPan_SetCurrentShaderProgram
    PERF_SECT_SC_TEXTURE,         ///< sub: MikuPan_SetTexture (incl. hash lookup + bind)
    PERF_SECT_SC_RS3D,            ///< sub: MikuPan_SetRenderState3D (depth/cull/blend)
    PERF_SECT_SC_VAO,             ///< sub: MikuPan_BindVAO (glBindVertexArray)
    /// Per-mesh-type sub-sections of MESH_RENDER. Their sum is approximately
    /// MESH_RENDER minus the dispatch/early-return overhead.
    PERF_SECT_MESH_0x2,           ///< sub: MikuPan_RenderMeshType0x2 with mesh_type 0x2
    PERF_SECT_MESH_0xA,           ///< sub: MikuPan_RenderMeshType0x2 with mesh_type 0xA
    PERF_SECT_MESH_0x10,          ///< sub: MikuPan_RenderMeshType0x32 with mesh_type 0x10
    PERF_SECT_MESH_0x12,          ///< sub: MikuPan_RenderMeshType0x32 with mesh_type 0x12
    PERF_SECT_MESH_0x32,          ///< sub: MikuPan_RenderMeshType0x32 with mesh_type 0x32
    PERF_SECT_MESH_0x82,          ///< sub: MikuPan_RenderMeshType0x82
    /// Sub-sections of SC_TEXTURE — answer "where exactly does the texture
    /// state change spend its time?". Their sum is approximately SC_TEXTURE
    /// minus the dispatch overhead between the inner timers.
    PERF_SECT_TEX_L1_LOOKUP,      ///< sub: MikuPan_LookupTextureByTex0 (raw tex0 → info hash table)
    PERF_SECT_TEX_HASH,           ///< sub: MikuPan_GetTextureHash (XXH3 over GS memory — hot on L1 miss)
    PERF_SECT_TEX_L2_LOOKUP,      ///< sub: MikuPan_GetTextureInfo (by GS-memory hash)
    PERF_SECT_TEX_CREATE,         ///< sub: MikuPan_CreateGLTexture (glTexImage2D + glGenerateMipmap)
    PERF_SECT_TEX_BIND,           ///< sub: MikuPan_BindTexture2DCached (glBindTexture under the cache)
    PERF_SECT_COUNT
} MikuPan_PerfSection;

/* ── Section timing ─────────────────────────────────────────────────── */

Uint64 MikuPan_PerfBegin(void);
void   MikuPan_PerfEnd(MikuPan_PerfSection sect, Uint64 t0);

/// RAII-style scope helper. Declare at function entry; any return path
/// (including early returns) automatically accumulates the elapsed time
/// into the right bucket via __attribute__((cleanup)).
typedef struct { Uint64 t0; int section; } MikuPan_PerfScope;
void MikuPan_PerfScopeEnd(MikuPan_PerfScope *s);

/// `__LINE__`-suffixed variable name lets the same function declare multiple
/// MIKUPAN_PERF_SCOPE blocks (e.g. an outer aggregate plus a per-dispatch
/// inner section) without redeclaration errors.
#define MIKUPAN_PERF_SCOPE_PASTE_(a, b) a##b
#define MIKUPAN_PERF_SCOPE_PASTE(a, b)  MIKUPAN_PERF_SCOPE_PASTE_(a, b)

#define MIKUPAN_PERF_SCOPE(sect) \
    MikuPan_PerfScope MIKUPAN_PERF_SCOPE_PASTE(_perf_scope_, __LINE__) \
        __attribute__((cleanup(MikuPan_PerfScopeEnd))) = \
        { SDL_GetPerformanceCounter(), (sect) }

/* ── Timed glDraw wrappers ──────────────────────────────────────────── */

/// Each wrapper contributes its CPU-side wall-clock to PERF_SECT_DRAW_SUBMIT
/// so the perf graph can show whether the bottleneck is real driver overhead
/// per-draw vs surrounding code.
void MikuPan_TimedDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void MikuPan_TimedDrawArrays(GLenum mode, GLint first, GLsizei count);

/* ── Frame timing (CPU/GPU split via fence-sync) ────────────────────── */

/// Split CPU and GPU wall-clock for the perf-debug graph. CPU time = from
/// MikuPan_PerfBeginFrame through the moment we've submitted all GL commands
/// (after our final glFlush). GPU time = wall-clock the GPU takes to drain
/// that work, measured via fence-sync. CPU + GPU ≈ frame time minus vsync.
void  MikuPan_PerfBeginFrame(void);
void  MikuPan_PerfEndFrame(void);
void  MikuPan_PerfResetFrame(void);
float MikuPan_GetLastFrameCpuMs(void);
float MikuPan_GetLastFrameGpuMs(void);

/* ── Counter increments ─────────────────────────────────────────────── */

void MikuPan_PerfStateChange(void);
void MikuPan_PerfDrawCall(void);
void MikuPan_PerfMeshCacheHit(void);
void MikuPan_PerfMeshCacheMissNew(void);
void MikuPan_PerfMeshCacheMissFull(void);
void MikuPan_PerfTexL1Hit(void);
void MikuPan_PerfTexL1Miss(void);

/* ── Last-frame queries (consumed by the perf-debug UI panel) ───────── */

float MikuPan_PerfGetSectionMs(int section);
int   MikuPan_PerfGetMeshCacheHits(void);
int   MikuPan_PerfGetMeshCacheMissesNew(void);
int   MikuPan_PerfGetMeshCacheMissesFull(void);
int   MikuPan_PerfGetTexL1Hits(void);
int   MikuPan_PerfGetTexL1Misses(void);

/* ── Current-frame snapshots (read mid-frame, e.g. for debug logging) ─ */

int MikuPan_PerfGetStateChangesCurrent(void);
int MikuPan_PerfGetDrawCallsCurrent(void);
int MikuPan_PerfGetMeshCacheHitsCurrent(void);
int MikuPan_PerfGetMeshCacheMissesNewCurrent(void);
int MikuPan_PerfGetMeshCacheMissesFullCurrent(void);

#ifdef __cplusplus
}
#endif

#endif /* MIKUPAN_PROFILER_H */
