//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2026 SuperTuxKart-VR contributors
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#ifndef HEADER_XR_MANAGER_HPP
#define HEADER_XR_MANAGER_HPP

#ifdef ENABLE_OPENXR

#include "graphics/gl_headers.hpp"
#include <matrix4.h>
#include <EGL/egl.h>
#include <jni.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace irr;

/** One OpenXR swapchain plus the GL framebuffer objects wrapping each of its
 *  images, so STK can simply glBindFramebuffer() one of them and draw. */
struct XRSwapchain
{
    XrSwapchain m_handle = XR_NULL_HANDLE;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    int64_t m_format = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> m_images;
    std::vector<GLuint> m_fbos;
    GLuint m_depth_rb = 0;
    int m_acquired_index = -1;

    GLuint currentFBO() const
    {
        return m_acquired_index < 0 ? 0 : m_fbos[m_acquired_index];
    }
};

/**
 * \brief Owns the OpenXR instance/session for the Meta Quest build and
 *        drives the per-frame OpenXR protocol (wait/begin/end frame).
 *
 * Phase 1/2 of the port: the flat STK framebuffer is presented as a quad
 * composition layer floating in front of the user. Later phases add per-eye
 * projection swapchains (see beginFrame()/getViews()).
 * \ingroup xr
 */
class XRManager
{
private:
    static XRManager* m_xr_manager;

    JavaVM*     m_java_vm = NULL;
    jobject     m_activity = NULL;   // global ref

    XrInstance  m_instance = XR_NULL_HANDLE;
    XrSystemId  m_system_id = XR_NULL_SYSTEM_ID;
    XrSession   m_session = XR_NULL_HANDLE;
    XrSpace     m_local_space = XR_NULL_HANDLE;
    XrSpace     m_view_space = XR_NULL_HANDLE;
    XrSpace     m_stage_space = XR_NULL_HANDLE;

    XrSessionState m_session_state = XR_SESSION_STATE_UNKNOWN;
    bool m_session_running = false;
    bool m_frame_begun = false;
    bool m_should_render = false;
    bool m_exit_requested = false;
    XrTime m_predicted_display_time = 0;

    XrViewConfigurationView m_view_config[2];
    XrView m_views[2];
    bool m_views_valid = false;

    EGLDisplay m_egl_display = EGL_NO_DISPLAY;
    EGLContext m_egl_context = EGL_NO_CONTEXT;
    EGLConfig  m_egl_config = NULL;

    /** Swapchain used to show the flat 2D STK screen (menus, and the whole
     *  game until stereo rendering lands). */
    XRSwapchain m_screen_swapchain;
    /** Per-eye projection swapchains, created lazily by createEyeSwapchains. */
    XRSwapchain m_eye_swapchain[2];
    bool m_eye_swapchains_created = false;

    bool m_has_srgb_write_control = false;
    bool m_has_refresh_rate_ext = false;
    bool m_initialized = false;

    /** Layers submitted this frame. */
    XrCompositionLayerProjectionView m_proj_views[2];
    XrCompositionLayerProjection m_proj_layer;
    XrCompositionLayerQuad m_quad_layer;
    bool m_submit_projection = false;
    bool m_submit_quad = false;

    bool initLoader();
    bool createInstance();
    bool createSession();
    bool createSpaces();
    bool createSwapchain(XRSwapchain& sc, uint32_t w, uint32_t h,
                         bool with_depth);
    void destroySwapchain(XRSwapchain& sc);
    int64_t chooseSwapchainFormat();
    void handleSessionStateChanged(const XrEventDataSessionStateChanged& ev);

public:
    static void create();
    static void destroy();
    static XRManager* get()             { return m_xr_manager; }
    static bool isVRActive()
    {
        return m_xr_manager != NULL && m_xr_manager->m_initialized;
    }

    XRManager();
    ~XRManager();

    /** Must be called on the render thread with the GL context current. */
    bool init();
    void shutdown();

    bool isInitialized() const          { return m_initialized; }
    bool isSessionRunning() const       { return m_session_running; }
    bool isExitRequested() const        { return m_exit_requested; }

    /** For src/xr/xr_input.cpp: the action-set/binding/sync calls need the
     *  raw instance and session handles. */
    XrInstance getInstance() const      { return m_instance; }
    XrSession getSession() const        { return m_session; }

    void pollEvents();

    /** xrWaitFrame + xrBeginFrame + xrLocateViews. Safe to call once per
     *  frame; returns true if the runtime wants us to render. */
    bool beginFrame();
    bool isFrameBegun() const           { return m_frame_begun; }
    bool shouldRender() const           { return m_should_render; }
    XrTime getPredictedDisplayTime() const { return m_predicted_display_time; }

    const XrView* getViews() const      { return m_views_valid ? m_views : NULL; }
    const XrViewConfigurationView& getViewConfig(int eye) const
                                        { return m_view_config[eye]; }

    /** Asymmetric-frustum projection matrix for one eye, in the engine's
     *  left-handed, 0..1-depth convention (matches
     *  matrix4::buildProjectionMatrixPerspectiveFovLH). */
    core::matrix4 getEyeProjectionMatrix(int eye, float zNear, float zFar) const;
    /** View-space affector representing the head/eye pose relative to a
     *  neutral, forward-facing seated position, for use with
     *  ICameraSceneNode::setViewMatrixAffector() on top of STK's normal
     *  (kart-following) camera. Identity if views aren't valid yet. */
    core::matrix4 getEyeViewAffector(int eye) const;

    // ------------------------------------------------------------------------
    // 2D screen (quad layer)
    /** Acquire the screen swapchain image and return its FBO id, or 0. */
    GLuint acquireScreenImage();
    /** Release the screen image and queue the quad layer for this frame. */
    void releaseScreenImage();
    uint32_t getScreenWidth() const     { return m_screen_swapchain.m_width; }
    uint32_t getScreenHeight() const    { return m_screen_swapchain.m_height; }

    // ------------------------------------------------------------------------
    // Per-eye rendering (projection layer)
    bool createEyeSwapchains();
    bool hasEyeSwapchains() const       { return m_eye_swapchains_created; }
    GLuint acquireEyeImage(int eye);
    void releaseEyeImage(int eye);
    const XRSwapchain& getEyeSwapchain(int eye) const { return m_eye_swapchain[eye]; }
    /** Mark the projection layer for submission with the current views. */
    void queueProjectionLayer();

    /** xrEndFrame with whatever layers were queued. Resets frame state. */
    void endFrame();

    /** Copies the default framebuffer (the flat STK screen) into the screen
     *  swapchain and submits the frame. Called from the video driver in
     *  place of the window swap. Returns false if the caller should fall
     *  back to a normal swap. */
    bool presentFlatScreen(unsigned fb_width, unsigned fb_height);

    bool hasSRGBWriteControl() const    { return m_has_srgb_write_control; }
};

#endif   // ENABLE_OPENXR

#endif   // HEADER_XR_MANAGER_HPP
