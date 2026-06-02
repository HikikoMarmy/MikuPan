#include "mikupan_renderer_internal.h"
#include "mikupan_pipeline.h"
#include "mikupan_profiler.h"
#include "mikupan_shader.h"
#include "mikupan/mikupan_logging_c.h"
#include <string.h>

#define SHADOW_FBO_SIZE 256

static GLuint g_shadow_fbo  = 0;
static GLuint g_shadow_tex  = 0;
static GLuint g_shadow_depth = 0;
static int    g_shadow_init = 0;
static GLenum g_shadow_fbo_status = 0;
static int    g_shadow_enabled = 0;
static GLint  g_shadow_saved_fbo      = 0;
static int    g_shadow_saved_viewport[4] = {0};
static mat4   g_shadow_saved_world_view;
static mat4   g_shadow_saved_projection;
static mat4   g_shadow_world_clip_view;
static int    g_shadow_matrix_valid = 0;
static int    g_shadow_pass_active = 0;
static int    g_shadow_receiver_pass_active = 0;
static int    g_shadow_receiver_debug_view = 0;
static MikuPan_ShadowDebugInfo g_shadow_debug = {0};

static void MikuPan_UpdateShadowDebugStaticFields(void)
{
    g_shadow_debug.enabled = g_shadow_enabled;
    g_shadow_debug.fbo_initialized = g_shadow_init;
    g_shadow_debug.fbo_complete = (g_shadow_fbo_status == GL_FRAMEBUFFER_COMPLETE);
    g_shadow_debug.fbo_status = g_shadow_fbo_status;
    g_shadow_debug.matrix_valid = g_shadow_matrix_valid;
    g_shadow_debug.texture_id = g_shadow_tex;
    g_shadow_debug.texture_size = SHADOW_FBO_SIZE;
}

void MikuPan_ShadowDebugBeginFrame(void)
{
    const int probe_valid = g_shadow_debug.probe_valid;
    const int probe_nonzero_pixels = g_shadow_debug.probe_nonzero_pixels;
    const int probe_max_value = g_shadow_debug.probe_max_value;
    const float probe_coverage = g_shadow_debug.probe_coverage;
    const float probe_average = g_shadow_debug.probe_average;

    memset(&g_shadow_debug, 0, sizeof(g_shadow_debug));
    g_shadow_debug.probe_valid = probe_valid;
    g_shadow_debug.probe_nonzero_pixels = probe_nonzero_pixels;
    g_shadow_debug.probe_max_value = probe_max_value;
    g_shadow_debug.probe_coverage = probe_coverage;
    g_shadow_debug.probe_average = probe_average;
    MikuPan_UpdateShadowDebugStaticFields();
}

static void MikuPan_ShadowDebugCountMeshType(int mesh_type,
                                             int *type_0,
                                             int *type_2,
                                             int *type_10,
                                             int *type_12,
                                             int *type_32,
                                             int *type_80,
                                             int *type_82,
                                             int *type_other)
{
    switch (mesh_type & 0xff)
    {
        case 0x00: if (type_0 != NULL)  (*type_0)++;  break;
        case 0x02: if (type_2 != NULL)  (*type_2)++;  break;
        case 0x10: if (type_10 != NULL) (*type_10)++; break;
        case 0x12: if (type_12 != NULL) (*type_12)++; break;
        case 0x32: if (type_32 != NULL) (*type_32)++; break;
        case 0x80: if (type_80 != NULL) (*type_80)++; break;
        case 0x82: if (type_82 != NULL) (*type_82)++; break;
        default:   if (type_other != NULL) (*type_other)++; break;
    }
}

void MikuPan_ShadowDebugRecordCasterMeshType(int mesh_type)
{
    MikuPan_ShadowDebugCountMeshType(mesh_type,
        &g_shadow_debug.caster_type_0,
        &g_shadow_debug.caster_type_2,
        NULL,
        NULL,
        NULL,
        &g_shadow_debug.caster_type_80,
        &g_shadow_debug.caster_type_82,
        &g_shadow_debug.caster_type_other);
}

void MikuPan_ShadowDebugRecordReceiverMeshType(int mesh_type)
{
    MikuPan_ShadowDebugCountMeshType(mesh_type,
        &g_shadow_debug.receiver_type_0,
        NULL,
        &g_shadow_debug.receiver_type_10,
        &g_shadow_debug.receiver_type_12,
        &g_shadow_debug.receiver_type_32,
        NULL,
        NULL,
        &g_shadow_debug.receiver_type_other);
}

void MikuPan_ShadowDebugRecordCasterDraw(int mesh_type, int index_count)
{
    g_shadow_debug.caster_draws++;
    g_shadow_debug.caster_indices += index_count;
    MikuPan_ShadowDebugCountMeshType(mesh_type,
        &g_shadow_debug.caster_draw_type_0,
        &g_shadow_debug.caster_draw_type_2,
        NULL,
        NULL,
        NULL,
        &g_shadow_debug.caster_draw_type_80,
        &g_shadow_debug.caster_draw_type_82,
        &g_shadow_debug.caster_draw_type_other);
}

void MikuPan_ShadowDebugRecordReceiverDraw(int mesh_type, int index_count)
{
    (void)mesh_type;
    g_shadow_debug.receiver_draws++;
    g_shadow_debug.receiver_indices += index_count;
}

const MikuPan_ShadowDebugInfo *MikuPan_GetShadowDebugInfo(void)
{
    MikuPan_UpdateShadowDebugStaticFields();
    return &g_shadow_debug;
}

int MikuPan_IsShadowPassActive(void)
{
    return g_shadow_pass_active;
}

int MikuPan_IsShadowReceiverPassActive(void)
{
    return g_shadow_receiver_pass_active;
}

static void MikuPan_EnsureShadowFbo(void)
{
    if (g_shadow_init)
    {
        return;
    }

    glad_glGenTextures(1, &g_shadow_tex);
    MikuPan_BindTexture2DCached(g_shadow_tex);
    glad_glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                      SHADOW_FBO_SIZE, SHADOW_FBO_SIZE, 0,
                      GL_RED, GL_UNSIGNED_BYTE, NULL);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glad_glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    // Broadcast the single R8 channel to G and B so that any plain sampler
    // (e.g. the ImGui debug-preview image) reads the occlusion value as a
    // visible grayscale instead of dark-red-on-black. The receiver and
    // silhouette shaders only ever read/write .r, which the swizzle leaves
    // untouched, so this is purely a presentation aid.
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
    glad_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);

    glad_glGenFramebuffers(1, &g_shadow_fbo);
    glad_glBindFramebuffer(GL_FRAMEBUFFER, g_shadow_fbo);
    glad_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, g_shadow_tex, 0);

    glad_glGenRenderbuffers(1, &g_shadow_depth);
    glad_glBindRenderbuffer(GL_RENDERBUFFER, g_shadow_depth);
    glad_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                               SHADOW_FBO_SIZE, SHADOW_FBO_SIZE);
    glad_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, g_shadow_depth);

    g_shadow_fbo_status = glad_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (g_shadow_fbo_status != GL_FRAMEBUFFER_COMPLETE)
    {
        info_log("Shadow FBO incomplete! status=0x%x", g_shadow_fbo_status);
    }

    glad_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_shadow_init = 1;
}

/*
 * Build the shadow projector's world->clip matrix in OpenGL conventions.
 *
 * The PS2 scamera.* matrices map into GS screen space (origin 2048, sub-pixel
 * fixed point, etc.) and are NOT valid as GL clip matrices. Instead we build a
 * proper orthographic light-view-projection here with cglm — the same column-
 * major, NDC [-1,1] convention MikuPan uses for the main camera — from purely
 * world-space inputs:
 *   center       : caster bounding-box centre (world space)
 *   light_dir    : source-side direction, matching shadow.c's ndirection
 *   world_bbox8  : 8 caster bounding-box corners (world space, stride 4 floats)
 *
 * The result is used both as the caster silhouette's view matrix (projection is
 * identity in BeginShadowPass) and, via g_shadow_world_clip_view, as the
 * receiver's uShadowMatrix — so caster and receiver stay perfectly consistent.
 */
float *MikuPan_ComputeShadowClipView(const float *center,
                                     const float *light_dir,
                                     const float *world_bbox8)
{
    static mat4 clip;

    vec3 c   = { center[0], center[1], center[2] };
    vec3 dir = { light_dir[0], light_dir[1], light_dir[2] };
    glm_vec3_normalize(dir);

    /* One-time diagnostic: verify the source-side direction matches the
     * flashlight. A reversed shadow means this vector is being treated as
     * light travel direction instead of caster->light/source direction. */
    {
        static int logged = 0;
        if (logged < 8)
        {
            logged++;
            info_log("Shadow dir: center=(%.1f,%.1f,%.1f) source_dir=(%.3f,%.3f,%.3f)",
                     c[0], c[1], c[2], dir[0], dir[1], dir[2]);
        }
    }

    /* Match SetShadowCamera(): scamera.p = center + ndirection * 25000.
     * ndirection points from the caster toward the light/source side; the
     * resulting projection casts onto receivers away from that source. */
    const float dist = 5000.0f;
    vec3 eye;
    glm_vec3_scale(dir, dist, eye);
    glm_vec3_add(c, eye, eye);

    /* Avoid a degenerate up vector for a near-vertical projection. */
    vec3 up = { 0.0f, 1.0f, 0.0f };
    float dup = glm_vec3_dot(dir, up);
    if (dup > 0.99f || dup < -0.99f)
    {
        up[0] = 0.0f; up[1] = 0.0f; up[2] = 1.0f;
    }

    mat4 view;
    glm_lookat(eye, c, up, view);

    /* Fit an ortho box to the caster bbox in view space. */
    float minx =  1e30f, miny =  1e30f, minz =  1e30f;
    float maxx = -1e30f, maxy = -1e30f, maxz = -1e30f;
    for (int i = 0; i < 8; i++)
    {
        vec4 wp = { world_bbox8[i * 4 + 0], world_bbox8[i * 4 + 1],
                    world_bbox8[i * 4 + 2], 1.0f };
        vec4 vp;
        glm_mat4_mulv(view, wp, vp);
        if (vp[0] < minx) minx = vp[0];
        if (vp[0] > maxx) maxx = vp[0];
        if (vp[1] < miny) miny = vp[1];
        if (vp[1] > maxy) maxy = vp[1];
        if (vp[2] < minz) minz = vp[2];
        if (vp[2] > maxz) maxz = vp[2];
    }

    const float padx = (maxx - minx) * 0.05f + 1.0f;
    const float pady = (maxy - miny) * 0.05f + 1.0f;

    /* View space looks down -Z: near = -maxz, far = -minz. Extend far well past
     * the caster so the floor it drops onto stays inside the depth slab. */
    float near_z = -maxz - 1.0f;
    float far_z  = -minz + 20000.0f;
    if (near_z < 1.0f) near_z = 1.0f;

    mat4 proj;
    glm_ortho(minx - padx, maxx + padx, miny - pady, maxy + pady,
              near_z, far_z, proj);

    glm_mat4_mul(proj, view, clip);
    return (float *) clip;
}

void MikuPan_BeginShadowPass(float *world_clip_view)
{
    MikuPan_EnsureShadowFbo();
    g_shadow_debug.caster_passes++;

    // Snapshot the previous render target + viewport so EndShadowPass can
    // hand the renderer back to the main scene pass without it noticing.
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &g_shadow_saved_fbo);
    glad_glGetIntegerv(GL_VIEWPORT, g_shadow_saved_viewport);
    memcpy(g_shadow_saved_world_view, WorldView, sizeof(g_shadow_saved_world_view));
    memcpy(g_shadow_saved_projection, projection, sizeof(g_shadow_saved_projection));

    glad_glBindFramebuffer(GL_FRAMEBUFFER, g_shadow_fbo);
    MikuPan_SetViewportCached(0, 0, SHADOW_FBO_SIZE, SHADOW_FBO_SIZE);
    MikuPan_SetRenderStateShadow();

    glad_glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glad_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (world_clip_view != NULL)
    {
        mat4 identity_projection;
        glm_mat4_identity(identity_projection);

        memcpy(g_shadow_world_clip_view, world_clip_view, sizeof(g_shadow_world_clip_view));
        MikuPan_SetViewProjectionMatrices(world_clip_view, (float *)identity_projection);
        g_shadow_matrix_valid = 1;
    }

    // Override every shader bind for the duration of the pass — the mesh
    // renderers call MikuPan_SetCurrentShaderProgram(MESH_*_SHADER) and the
    // override silently routes them to SHADOW_SILHOUETTE_SHADER. Cleared in
    // MikuPan_EndShadowPass so subsequent main-pass draws are unaffected.
    MikuPan_InvalidateModelTransformCache();
    MikuPan_SetShaderOverride(SHADOW_SILHOUETTE_SHADER);
    g_shadow_pass_active = 1;
}

void MikuPan_DrawShadowSilhouetteEllipse(void)
{
    // Reuse the fullscreen sprite VAO — the same one the postprocess pass
    // uses. The shadow caster shader ignores the vertex attributes (all
    // computation is done from gl_FragCoord) so any layout works as long as
    // we issue 4 vertices for a fullscreen TRIANGLE_STRIP.
    MikuPan_PipelineInfo *pi = MikuPan_GetPipelineInfo(UV4_COLOUR4_POSITION4);
    if (pi == NULL) return;

    MikuPan_BindVAO(pi->vao);
    MikuPan_SetCurrentShaderProgram(SHADOW_BLOB_SHADER);

    GLuint prog = MikuPan_GetCurrentShaderProgram();
    GLint sz_loc = glad_glGetUniformLocation(prog, "uShadowSize");
    GLint dk_loc = glad_glGetUniformLocation(prog, "uShadowDarkness");
    if (sz_loc >= 0) glad_glUniform2f(sz_loc, (float)SHADOW_FBO_SIZE, (float)SHADOW_FBO_SIZE);
    if (dk_loc >= 0) glad_glUniform1f(dk_loc, 0.6f);

    // Need additive/normal blending so the ellipse alpha isn't clobbered.
    glad_glDisable(GL_DEPTH_TEST);
    glad_glEnable(GL_BLEND);
    glad_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    MikuPan_TimedDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glad_glEnable(GL_DEPTH_TEST);
    // The render-state cache is now out of sync with the GL we just toggled;
    // force the scene pass to reissue depth/blend on its next state change.
    MikuPan_ResetRenderStateCache();
}

void MikuPan_EndShadowPass(void)
{
    // Drop the override + active flag *before* we start pushing scene-side
    // uniforms below — those uniform pushes go through SetCurrentShaderProgram
    // internally and would otherwise route into the silhouette shader.
    MikuPan_SetShaderOverride(-1);
    g_shadow_pass_active = 0;
    MikuPan_SetViewProjectionMatrices((float *)g_shadow_saved_world_view,
                                      (float *)g_shadow_saved_projection);

    glad_glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)g_shadow_saved_fbo);
    MikuPan_SetViewportCached(g_shadow_saved_viewport[0],
                              g_shadow_saved_viewport[1],
                              g_shadow_saved_viewport[2],
                              g_shadow_saved_viewport[3]);

    // The clear colour above clobbered the renderer's; restore the value the
    // scene clear path expects. The actual main-pass clear runs next frame so
    // a single push here is enough.
    glad_glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    // Push shadow uniforms for the receiver decal pass. The forward mesh
    // shader's built-in sampler is kept disabled so the shadow is applied
    // only through AssignShadow's receiver traversal, not order-dependent
    // later mesh draws. Texture unit 1 is reserved for the shadow map; unit 0
    // stays the diffuse path so uTexture continues to work without changes.
    MikuPan_SetUniform1iToAllShaders(0,                                                "uShadowEnabled");
    MikuPan_SetUniform1iToAllShaders(1,                                                "uShadowTex");
    MikuPan_SetUniform1fToAllShaders(0.6f,                                             "uShadowStrength");
    MikuPan_SetUniformMatrix4fvToAllShaders((float *)g_shadow_world_clip_view,         "uShadowMatrix");

    MikuPan_ActiveTextureCached(GL_TEXTURE1);
    glad_glBindTexture(GL_TEXTURE_2D, g_shadow_tex);
    MikuPan_ActiveTextureCached(GL_TEXTURE0);
}

void MikuPan_BeginShadowReceiverPass(void)
{
    if (!g_shadow_enabled || !g_shadow_matrix_valid)
    {
        g_shadow_receiver_pass_active = 0;
        return;
    }

    g_shadow_debug.receiver_passes++;
    MikuPan_SetRenderStateShadowReceiver();
    MikuPan_SetShaderOverride(SHADOW_RECEIVER_SHADER);
    MikuPan_SetCurrentShaderProgram(SHADOW_RECEIVER_SHADER);
    MikuPan_SetUniform1iToCurrentShader(g_shadow_receiver_debug_view,
                                        "uShadowDebugView");
    g_shadow_receiver_pass_active = 1;

    MikuPan_ActiveTextureCached(GL_TEXTURE1);
    glad_glBindTexture(GL_TEXTURE_2D, g_shadow_tex);
    MikuPan_ActiveTextureCached(GL_TEXTURE0);
    MikuPan_InvalidateModelTransformCache();
}

void MikuPan_EndShadowReceiverPass(void)
{
    if (!g_shadow_receiver_pass_active)
    {
        return;
    }

    MikuPan_SetShaderOverride(-1);
    g_shadow_receiver_pass_active = 0;
    glad_glDisable(GL_POLYGON_OFFSET_FILL);
    MikuPan_ResetRenderStateCache();
    MikuPan_InvalidateModelTransformCache();
}

unsigned int MikuPan_GetShadowTexture(void)
{
    return g_shadow_tex;
}

float* MikuPan_GetShadowMatrix(void)
{
    return g_shadow_matrix_valid ? (float *)g_shadow_world_clip_view : NULL;
}

int MikuPan_IsShadowEnabled(void)
{
    return g_shadow_enabled;
}

void MikuPan_SetShadowEnabled(int enabled)
{
    g_shadow_enabled = enabled ? 1 : 0;
}

int MikuPan_IsShadowReceiverDebugViewEnabled(void)
{
    return g_shadow_receiver_debug_view;
}

void MikuPan_SetShadowReceiverDebugViewEnabled(int enabled)
{
    g_shadow_receiver_debug_view = enabled ? 1 : 0;
}

void MikuPan_ShadowDebugProbeTexture(void)
{
    unsigned char pixels[SHADOW_FBO_SIZE * SHADOW_FBO_SIZE];
    GLint saved_read_fbo = 0;
    int nonzero = 0;
    int max_value = 0;
    unsigned int sum = 0;

    MikuPan_EnsureShadowFbo();

    glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &saved_read_fbo);
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, g_shadow_fbo);
    glad_glReadPixels(0, 0, SHADOW_FBO_SIZE, SHADOW_FBO_SIZE,
                      GL_RED, GL_UNSIGNED_BYTE, pixels);
    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)saved_read_fbo);

    for (int i = 0; i < SHADOW_FBO_SIZE * SHADOW_FBO_SIZE; i++)
    {
        int value = pixels[i];
        if (value != 0)
        {
            nonzero++;
        }
        if (value > max_value)
        {
            max_value = value;
        }
        sum += (unsigned int)value;
    }

    g_shadow_debug.probe_valid = 1;
    g_shadow_debug.probe_nonzero_pixels = nonzero;
    g_shadow_debug.probe_max_value = max_value;
    g_shadow_debug.probe_coverage =
        (float)nonzero / (float)(SHADOW_FBO_SIZE * SHADOW_FBO_SIZE);
    g_shadow_debug.probe_average =
        (float)sum / (float)(SHADOW_FBO_SIZE * SHADOW_FBO_SIZE * 255);
}
