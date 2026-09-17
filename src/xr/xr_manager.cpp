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

#include "xr/xr_manager.hpp"

#ifdef ENABLE_OPENXR

#include "main_loop.hpp"
#include "utils/log.hpp"

#include <SDL_system.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#ifndef GL_FRAMEBUFFER_SRGB_EXT
#define GL_FRAMEBUFFER_SRGB_EXT 0x8DB9
#endif

XRManager* XRManager::m_xr_manager = NULL;

// ----------------------------------------------------------------------------
static const char* xrResultString(XrInstance instance, XrResult r)
{
    static char buf[XR_MAX_RESULT_STRING_SIZE];
    if (instance != XR_NULL_HANDLE &&
        xrResultToString(instance, r, buf) == XR_SUCCESS)
        return buf;
    snprintf(buf, sizeof(buf), "XrResult(%d)", (int)r);
    return buf;
}   // xrResultString

/** Logs and returns false from the enclosing bool function on failure. */
#define XR_CHECK(call, what)                                               \
    do                                                                     \
    {                                                                      \
        XrResult _r = (call);                                              \
        if (XR_FAILED(_r))                                                 \
        {                                                                  \
            Log::error("XR", "%s failed: %s", what,                        \
                       xrResultString(m_instance, _r));                    \
            return false;                                                  \
        }                                                                  \
    } while (0)

// ----------------------------------------------------------------------------
void XRManager::create()
{
    if (!m_xr_manager)
        m_xr_manager = new XRManager();
}   // create

// ----------------------------------------------------------------------------
void XRManager::destroy()
{
    delete m_xr_manager;
    m_xr_manager = NULL;
}   // destroy

// ----------------------------------------------------------------------------
XRManager::XRManager()
{
    memset(m_view_config, 0, sizeof(m_view_config));
    memset(m_views, 0, sizeof(m_views));
    memset(m_proj_views, 0, sizeof(m_proj_views));
    memset(&m_proj_layer, 0, sizeof(m_proj_layer));
    memset(&m_quad_layer, 0, sizeof(m_quad_layer));
}   // XRManager

// ----------------------------------------------------------------------------
XRManager::~XRManager()
{
    shutdown();
}   // ~XRManager

// ----------------------------------------------------------------------------
bool XRManager::initLoader()
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (!env)
    {
        Log::error("XR", "No JNI environment.");
        return false;
    }
    if (env->GetJavaVM(&m_java_vm) != JNI_OK || !m_java_vm)
    {
        Log::error("XR", "Could not get JavaVM.");
        return false;
    }
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!activity)
    {
        Log::error("XR", "Could not get activity.");
        return false;
    }
    m_activity = env->NewGlobalRef(activity);
    env->DeleteLocalRef(activity);

    PFN_xrInitializeLoaderKHR init_loader = NULL;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                          (PFN_xrVoidFunction*)&init_loader);
    if (init_loader)
    {
        XrLoaderInitInfoAndroidKHR info = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        info.applicationVM = m_java_vm;
        info.applicationContext = m_activity;
        XR_CHECK(init_loader((const XrLoaderInitInfoBaseHeaderKHR*)&info),
                 "xrInitializeLoaderKHR");
    }
    else
    {
        Log::warn("XR", "xrInitializeLoaderKHR not available.");
    }
    return true;
}   // initLoader

// ----------------------------------------------------------------------------
bool XRManager::createInstance()
{
    uint32_t ext_count = 0;
    XR_CHECK(xrEnumerateInstanceExtensionProperties(NULL, 0, &ext_count, NULL),
             "xrEnumerateInstanceExtensionProperties");
    std::vector<XrExtensionProperties> exts(ext_count);
    for (unsigned i = 0; i < ext_count; i++)
    {
        exts[i].type = XR_TYPE_EXTENSION_PROPERTIES;
        exts[i].next = NULL;
    }
    XR_CHECK(xrEnumerateInstanceExtensionProperties(NULL, ext_count, &ext_count,
                                                    exts.data()),
             "xrEnumerateInstanceExtensionProperties");

    auto has_ext = [&](const char* name) -> bool
    {
        for (unsigned i = 0; i < ext_count; i++)
        {
            if (strcmp(exts[i].extensionName, name) == 0)
                return true;
        }
        return false;
    };

    std::vector<const char*> enabled;
    if (!has_ext(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME))
    {
        Log::error("XR", "Runtime lacks %s.", XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
        return false;
    }
    enabled.push_back(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
    if (has_ext(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
    {
        enabled.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        m_has_refresh_rate_ext = true;
    }
    for (unsigned i = 0; i < ext_count; i++)
        Log::debug("XR", "Runtime extension: %s", exts[i].extensionName);

    XrInstanceCreateInfo ci = {XR_TYPE_INSTANCE_CREATE_INFO};
    strncpy(ci.applicationInfo.applicationName, "SuperTuxKart",
            XR_MAX_APPLICATION_NAME_SIZE - 1);
    ci.applicationInfo.applicationVersion = 1;
    strncpy(ci.applicationInfo.engineName, "SuperTuxKart",
            XR_MAX_ENGINE_NAME_SIZE - 1);
    ci.applicationInfo.engineVersion = 1;
    // Target the 1.0 API so any Quest runtime accepts us.
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ci.enabledExtensionCount = (uint32_t)enabled.size();
    ci.enabledExtensionNames = enabled.data();

    XrResult r = xrCreateInstance(&ci, &m_instance);
    if (XR_FAILED(r))
    {
        Log::error("XR", "xrCreateInstance failed: %d", (int)r);
        m_instance = XR_NULL_HANDLE;
        return false;
    }

    XrInstanceProperties ip = {XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(m_instance, &ip)))
    {
        Log::info("XR", "Runtime %s version %u.%u.%u", ip.runtimeName,
                  XR_VERSION_MAJOR(ip.runtimeVersion),
                  XR_VERSION_MINOR(ip.runtimeVersion),
                  XR_VERSION_PATCH(ip.runtimeVersion));
    }
    return true;
}   // createInstance

// ----------------------------------------------------------------------------
bool XRManager::createSession()
{
    PFN_xrGetOpenGLESGraphicsRequirementsKHR get_reqs = NULL;
    XR_CHECK(xrGetInstanceProcAddr(m_instance,
                                   "xrGetOpenGLESGraphicsRequirementsKHR",
                                   (PFN_xrVoidFunction*)&get_reqs),
             "xrGetInstanceProcAddr(xrGetOpenGLESGraphicsRequirementsKHR)");
    XrGraphicsRequirementsOpenGLESKHR reqs =
        {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    XR_CHECK(get_reqs(m_instance, m_system_id, &reqs),
             "xrGetOpenGLESGraphicsRequirementsKHR");

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    XrVersion gl_version = XR_MAKE_VERSION(major, minor, 0);
    Log::info("XR", "GLES %d.%d, runtime requires %u.%u - %u.%u", major, minor,
              XR_VERSION_MAJOR(reqs.minApiVersionSupported),
              XR_VERSION_MINOR(reqs.minApiVersionSupported),
              XR_VERSION_MAJOR(reqs.maxApiVersionSupported),
              XR_VERSION_MINOR(reqs.maxApiVersionSupported));
    if (gl_version < reqs.minApiVersionSupported)
        Log::warn("XR", "GLES version below runtime minimum, trying anyway.");

    m_egl_display = eglGetCurrentDisplay();
    m_egl_context = eglGetCurrentContext();
    if (m_egl_display == EGL_NO_DISPLAY || m_egl_context == EGL_NO_CONTEXT)
    {
        Log::error("XR", "No current EGL display/context on this thread.");
        return false;
    }
    EGLint config_id = 0;
    if (!eglQueryContext(m_egl_display, m_egl_context, EGL_CONFIG_ID, &config_id))
    {
        Log::error("XR", "eglQueryContext(EGL_CONFIG_ID) failed: 0x%x",
                   eglGetError());
        return false;
    }
    const EGLint attribs[] = { EGL_CONFIG_ID, config_id, EGL_NONE };
    EGLint num_configs = 0;
    EGLConfig config = NULL;
    if (!eglChooseConfig(m_egl_display, attribs, &config, 1, &num_configs) ||
        num_configs < 1)
    {
        Log::error("XR", "eglChooseConfig for config id %d failed: 0x%x",
                   config_id, eglGetError());
        return false;
    }
    m_egl_config = config;

    XrGraphicsBindingOpenGLESAndroidKHR binding =
        {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    binding.display = m_egl_display;
    binding.config = m_egl_config;
    binding.context = m_egl_context;

    XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &binding;
    sci.systemId = m_system_id;
    XR_CHECK(xrCreateSession(m_instance, &sci, &m_session), "xrCreateSession");
    Log::info("XR", "Session created (EGL config id %d).", config_id);
    return true;
}   // createSession

// ----------------------------------------------------------------------------
bool XRManager::createSpaces()
{
    XrReferenceSpaceCreateInfo ci = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    ci.poseInReferenceSpace.orientation.w = 1.0f;

    ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    XR_CHECK(xrCreateReferenceSpace(m_session, &ci, &m_local_space),
             "xrCreateReferenceSpace(LOCAL)");
    ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    XR_CHECK(xrCreateReferenceSpace(m_session, &ci, &m_view_space),
             "xrCreateReferenceSpace(VIEW)");
    ci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (XR_FAILED(xrCreateReferenceSpace(m_session, &ci, &m_stage_space)))
        m_stage_space = XR_NULL_HANDLE;
    return true;
}   // createSpaces

// ----------------------------------------------------------------------------
int64_t XRManager::chooseSwapchainFormat()
{
    uint32_t count = 0;
    if (XR_FAILED(xrEnumerateSwapchainFormats(m_session, 0, &count, NULL)) ||
        count == 0)
        return GL_RGBA8;
    std::vector<int64_t> formats(count);
    xrEnumerateSwapchainFormats(m_session, count, &count, formats.data());
    for (unsigned i = 0; i < count; i++)
        Log::debug("XR", "Swapchain format 0x%llx", (long long)formats[i]);

    // STK writes display-ready (gamma encoded) colour. With the sRGB write
    // control extension we can store those bytes untouched in an sRGB
    // swapchain, which the compositor then displays without extra gamma.
    if (m_has_srgb_write_control &&
        std::find(formats.begin(), formats.end(), (int64_t)GL_SRGB8_ALPHA8)
            != formats.end())
        return GL_SRGB8_ALPHA8;
    if (std::find(formats.begin(), formats.end(), (int64_t)GL_RGBA8)
            != formats.end())
        return GL_RGBA8;
    return formats[0];
}   // chooseSwapchainFormat

// ----------------------------------------------------------------------------
bool XRManager::createSwapchain(XRSwapchain& sc, uint32_t w, uint32_t h,
                                bool with_depth)
{
    destroySwapchain(sc);
    sc.m_format = chooseSwapchainFormat();

    XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                    XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = sc.m_format;
    ci.sampleCount = 1;
    ci.width = w;
    ci.height = h;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    XR_CHECK(xrCreateSwapchain(m_session, &ci, &sc.m_handle), "xrCreateSwapchain");
    sc.m_width = w;
    sc.m_height = h;

    uint32_t count = 0;
    XR_CHECK(xrEnumerateSwapchainImages(sc.m_handle, 0, &count, NULL),
             "xrEnumerateSwapchainImages");
    sc.m_images.resize(count);
    for (unsigned i = 0; i < count; i++)
    {
        sc.m_images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        sc.m_images[i].next = NULL;
        sc.m_images[i].image = 0;
    }
    XR_CHECK(xrEnumerateSwapchainImages(sc.m_handle, count, &count,
                 (XrSwapchainImageBaseHeader*)sc.m_images.data()),
             "xrEnumerateSwapchainImages");

    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

    if (with_depth)
    {
        glGenRenderbuffers(1, &sc.m_depth_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, sc.m_depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    sc.m_fbos.resize(count, 0);
    glGenFramebuffers(count, sc.m_fbos.data());
    for (unsigned i = 0; i < count; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, sc.m_fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, sc.m_images[i].image, 0);
        if (with_depth)
        {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, sc.m_depth_rb);
        }
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            Log::error("XR", "Swapchain FBO %u incomplete: 0x%x", i, status);
            glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    Log::info("XR", "Swapchain %ux%u format 0x%llx with %u images%s.", w, h,
              (long long)sc.m_format, count, with_depth ? " + depth" : "");
    return true;
}   // createSwapchain

// ----------------------------------------------------------------------------
void XRManager::destroySwapchain(XRSwapchain& sc)
{
    if (!sc.m_fbos.empty())
    {
        glDeleteFramebuffers((GLsizei)sc.m_fbos.size(), sc.m_fbos.data());
        sc.m_fbos.clear();
    }
    if (sc.m_depth_rb)
    {
        glDeleteRenderbuffers(1, &sc.m_depth_rb);
        sc.m_depth_rb = 0;
    }
    if (sc.m_handle != XR_NULL_HANDLE)
    {
        xrDestroySwapchain(sc.m_handle);
        sc.m_handle = XR_NULL_HANDLE;
    }
    sc.m_images.clear();
    sc.m_width = sc.m_height = 0;
    sc.m_acquired_index = -1;
}   // destroySwapchain

// ----------------------------------------------------------------------------
bool XRManager::init()
{
    if (m_initialized)
        return true;
    if (!initLoader())
        return false;
    if (!createInstance())
        return false;

    XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR_CHECK(xrGetSystem(m_instance, &sgi, &m_system_id), "xrGetSystem");

    XrSystemProperties sp = {XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(xrGetSystemProperties(m_instance, m_system_id, &sp)))
    {
        Log::info("XR", "System '%s' vendor 0x%x, max swapchain %ux%u, "
                  "orientation tracking %d, position tracking %d",
                  sp.systemName, sp.vendorId,
                  sp.graphicsProperties.maxSwapchainImageWidth,
                  sp.graphicsProperties.maxSwapchainImageHeight,
                  (int)sp.trackingProperties.orientationTracking,
                  (int)sp.trackingProperties.positionTracking);
    }

    const char* gl_exts = (const char*)glGetString(GL_EXTENSIONS);
    m_has_srgb_write_control =
        gl_exts != NULL && strstr(gl_exts, "GL_EXT_sRGB_write_control") != NULL;
    Log::info("XR", "GL_EXT_sRGB_write_control: %d", (int)m_has_srgb_write_control);

    if (!createSession())
        return false;
    if (!createSpaces())
        return false;

    uint32_t view_count = 0;
    XR_CHECK(xrEnumerateViewConfigurationViews(m_instance, m_system_id,
                 XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, NULL),
             "xrEnumerateViewConfigurationViews");
    if (view_count < 2)
    {
        Log::error("XR", "Stereo view configuration has %u views.", view_count);
        return false;
    }
    view_count = 2;
    for (int i = 0; i < 2; i++)
    {
        m_view_config[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
        m_view_config[i].next = NULL;
    }
    XR_CHECK(xrEnumerateViewConfigurationViews(m_instance, m_system_id,
                 XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &view_count,
                 m_view_config),
             "xrEnumerateViewConfigurationViews");
    for (int i = 0; i < 2; i++)
    {
        Log::info("XR", "View %d recommended %ux%u (max %ux%u), samples %u", i,
                  m_view_config[i].recommendedImageRectWidth,
                  m_view_config[i].recommendedImageRectHeight,
                  m_view_config[i].maxImageRectWidth,
                  m_view_config[i].maxImageRectHeight,
                  m_view_config[i].recommendedSwapchainSampleCount);
    }

    m_initialized = true;
    Log::info("XR", "OpenXR initialised.");
    return true;
}   // init

// ----------------------------------------------------------------------------
void XRManager::shutdown()
{
    if (m_frame_begun && m_session_running)
    {
        m_should_render = false;
        endFrame();
    }
    destroySwapchain(m_screen_swapchain);
    destroySwapchain(m_eye_swapchain[0]);
    destroySwapchain(m_eye_swapchain[1]);
    m_eye_swapchains_created = false;

    if (m_session_running && m_session != XR_NULL_HANDLE)
    {
        xrEndSession(m_session);
        m_session_running = false;
    }
    if (m_stage_space != XR_NULL_HANDLE) xrDestroySpace(m_stage_space);
    if (m_view_space != XR_NULL_HANDLE)  xrDestroySpace(m_view_space);
    if (m_local_space != XR_NULL_HANDLE) xrDestroySpace(m_local_space);
    m_stage_space = m_view_space = m_local_space = XR_NULL_HANDLE;
    if (m_session != XR_NULL_HANDLE)
    {
        xrDestroySession(m_session);
        m_session = XR_NULL_HANDLE;
    }
    if (m_instance != XR_NULL_HANDLE)
    {
        xrDestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
    }
    if (m_activity)
    {
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        if (env)
            env->DeleteGlobalRef(m_activity);
        m_activity = NULL;
    }
    m_initialized = false;
    m_views_valid = false;
}   // shutdown

// ----------------------------------------------------------------------------
void XRManager::handleSessionStateChanged(const XrEventDataSessionStateChanged& ev)
{
    Log::info("XR", "Session state %d -> %d", (int)m_session_state, (int)ev.state);
    m_session_state = ev.state;
    switch (ev.state)
    {
    case XR_SESSION_STATE_READY:
    {
        XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO};
        bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        XrResult r = xrBeginSession(m_session, &bi);
        if (XR_FAILED(r))
        {
            Log::error("XR", "xrBeginSession failed: %s",
                       xrResultString(m_instance, r));
            break;
        }
        m_session_running = true;
        Log::info("XR", "Session running.");
        if (m_has_refresh_rate_ext)
        {
            PFN_xrRequestDisplayRefreshRateFB request_rate = NULL;
            xrGetInstanceProcAddr(m_instance, "xrRequestDisplayRefreshRateFB",
                                  (PFN_xrVoidFunction*)&request_rate);
            if (request_rate)
                request_rate(m_session, 72.0f);
        }
        break;
    }
    case XR_SESSION_STATE_STOPPING:
    {
        if (m_frame_begun)
        {
            m_should_render = false;
            endFrame();
        }
        XrResult r = xrEndSession(m_session);
        if (XR_FAILED(r))
            Log::error("XR", "xrEndSession failed: %s", xrResultString(m_instance, r));
        m_session_running = false;
        m_views_valid = false;
        Log::info("XR", "Session stopped.");
        break;
    }
    case XR_SESSION_STATE_EXITING:
    case XR_SESSION_STATE_LOSS_PENDING:
        m_exit_requested = true;
        Log::info("XR", "Runtime requested exit.");
        if (main_loop)
            main_loop->abort();
        break;
    default:
        break;
    }
}   // handleSessionStateChanged

// ----------------------------------------------------------------------------
void XRManager::pollEvents()
{
    if (m_instance == XR_NULL_HANDLE)
        return;
    XrEventDataBuffer ev;
    for (;;)
    {
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        ev.next = NULL;
        XrResult r = xrPollEvent(m_instance, &ev);
        if (r != XR_SUCCESS)
            break;
        switch (ev.type)
        {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
            handleSessionStateChanged(
                *reinterpret_cast<const XrEventDataSessionStateChanged*>(&ev));
            break;
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            Log::warn("XR", "Instance loss pending.");
            m_exit_requested = true;
            if (main_loop)
                main_loop->abort();
            break;
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            Log::info("XR", "Reference space change pending (recenter).");
            break;
        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            Log::info("XR", "Interaction profile changed.");
            break;
        default:
            break;
        }
    }
}   // pollEvents

// ----------------------------------------------------------------------------
bool XRManager::beginFrame()
{
    if (!m_session_running)
        return false;
    if (m_frame_begun)
        return m_should_render;

    XrFrameWaitInfo wi = {XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState fs = {XR_TYPE_FRAME_STATE};
    XrResult r = xrWaitFrame(m_session, &wi, &fs);
    if (XR_FAILED(r))
    {
        Log::error("XR", "xrWaitFrame failed: %s", xrResultString(m_instance, r));
        return false;
    }
    XrFrameBeginInfo bi = {XR_TYPE_FRAME_BEGIN_INFO};
    r = xrBeginFrame(m_session, &bi);
    if (XR_FAILED(r))
    {
        Log::error("XR", "xrBeginFrame failed: %s", xrResultString(m_instance, r));
        return false;
    }
    m_frame_begun = true;
    m_should_render = fs.shouldRender == XR_TRUE;
    m_predicted_display_time = fs.predictedDisplayTime;
    m_submit_projection = false;
    m_submit_quad = false;

    XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = m_predicted_display_time;
    li.space = m_local_space;
    XrViewState vs = {XR_TYPE_VIEW_STATE};
    uint32_t count = 0;
    for (int i = 0; i < 2; i++)
    {
        m_views[i].type = XR_TYPE_VIEW;
        m_views[i].next = NULL;
    }
    r = xrLocateViews(m_session, &li, &vs, 2, &count, m_views);
    m_views_valid = XR_SUCCEEDED(r) && count == 2 &&
        (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
    return m_should_render;
}   // beginFrame

// ----------------------------------------------------------------------------
/** Rotates a vector by an OpenXR (right-handed) quaternion. */
static void rotateVecByQuat(const XrQuaternionf& q, float vx, float vy,
                            float vz, float& ox, float& oy, float& oz)
{
    const float ux = q.x, uy = q.y, uz = q.z, s = q.w;
    const float dot_uv = ux * vx + uy * vy + uz * vz;
    const float dot_uu = ux * ux + uy * uy + uz * uz;
    float cx = uy * vz - uz * vy;
    float cy = uz * vx - ux * vz;
    float cz = ux * vy - uy * vx;
    ox = 2.0f * dot_uv * ux + (s * s - dot_uu) * vx + 2.0f * s * cx;
    oy = 2.0f * dot_uv * uy + (s * s - dot_uu) * vy + 2.0f * s * cy;
    oz = 2.0f * dot_uv * uz + (s * s - dot_uu) * vz + 2.0f * s * cz;
}   // rotateVecByQuat

// ----------------------------------------------------------------------------
/** OpenXR's tracking space is right-handed with -Z forward (same as
 *  OpenGL); STK/Irrlicht is left-handed with +Z forward. Both use the same
 *  X (right) and Y (up) axes, so converting a single position or direction
 *  vector between the two is exactly a Z negation. */
static core::vector3df xrToStk(float x, float y, float z)
{
    return core::vector3df(x, y, -z);
}   // xrToStk

// ----------------------------------------------------------------------------
core::matrix4 XRManager::getEyeProjectionMatrix(int eye, float zNear,
                                                float zFar) const
{
    core::matrix4 m;
    if (!m_views_valid)
        return m;
    const XrFovf& fov = m_views[eye].fov;
    const float tan_left   = tanf(fov.angleLeft);
    const float tan_right  = tanf(fov.angleRight);
    const float tan_up     = tanf(fov.angleUp);
    const float tan_down   = tanf(fov.angleDown);
    const float tan_width  = tan_right - tan_left;
    const float tan_height = tan_up - tan_down;

    float* M = m.pointer();
    M[0] = 2.0f / tan_width;
    M[1] = 0.0f;
    M[2] = 0.0f;
    M[3] = 0.0f;

    M[4] = 0.0f;
    M[5] = 2.0f / tan_height;
    M[6] = 0.0f;
    M[7] = 0.0f;

    // Asymmetric-frustum skew terms; both reduce to 0 for a symmetric FOV
    // (tan_right == -tan_left, tan_up == -tan_down), matching
    // buildProjectionMatrixPerspectiveFovLH exactly in that case.
    M[8] = -(tan_right + tan_left) / tan_width;
    M[9] = -(tan_up + tan_down) / tan_height;
    M[10] = zFar / (zFar - zNear);
    M[11] = 1.0f;

    M[12] = 0.0f;
    M[13] = 0.0f;
    M[14] = -zNear * zFar / (zFar - zNear);
    M[15] = 0.0f;
    return m;
}   // getEyeProjectionMatrix

// ----------------------------------------------------------------------------
core::matrix4 XRManager::getEyeViewAffector(int eye) const
{
    core::matrix4 m;   // identity if views aren't valid yet
    if (!m_views_valid)
        return m;
    const XrPosef& pose = m_views[eye].pose;

    const core::vector3df pos = xrToStk(pose.position.x, pose.position.y,
                                        pose.position.z);
    float fx, fy, fz, ux, uy, uz;
    // OpenXR cameras look down their local -Z axis.
    rotateVecByQuat(pose.orientation, 0.0f, 0.0f, -1.0f, fx, fy, fz);
    rotateVecByQuat(pose.orientation, 0.0f, 1.0f, 0.0f, ux, uy, uz);
    const core::vector3df forward = xrToStk(fx, fy, fz);
    const core::vector3df up = xrToStk(ux, uy, uz);

    // Built exactly like Irrlicht's own buildCameraLookAtMatrixLH, just in
    // the base camera's view space (identity = looking straight ahead)
    // rather than world space, so it can be layered on top of STK's normal
    // (kart-following) camera via setViewMatrixAffector().
    m.buildCameraLookAtMatrixLH(pos, pos + forward, up);
    return m;
}   // getEyeViewAffector

// ----------------------------------------------------------------------------
GLuint XRManager::acquireScreenImage()
{
    XRSwapchain& sc = m_screen_swapchain;
    if (sc.m_handle == XR_NULL_HANDLE || sc.m_acquired_index >= 0)
        return 0;
    XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage(sc.m_handle, &ai, &index)))
        return 0;
    XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(sc.m_handle, &wi)))
    {
        XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(sc.m_handle, &ri);
        return 0;
    }
    sc.m_acquired_index = (int)index;
    return sc.m_fbos[index];
}   // acquireScreenImage

// ----------------------------------------------------------------------------
void XRManager::releaseScreenImage()
{
    XRSwapchain& sc = m_screen_swapchain;
    if (sc.m_acquired_index < 0)
        return;
    XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(sc.m_handle, &ri);
    sc.m_acquired_index = -1;

    memset(&m_quad_layer, 0, sizeof(m_quad_layer));
    m_quad_layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    m_quad_layer.layerFlags = 0;
    m_quad_layer.space = m_local_space;
    m_quad_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    m_quad_layer.subImage.swapchain = sc.m_handle;
    m_quad_layer.subImage.imageRect.offset.x = 0;
    m_quad_layer.subImage.imageRect.offset.y = 0;
    m_quad_layer.subImage.imageRect.extent.width = (int32_t)sc.m_width;
    m_quad_layer.subImage.imageRect.extent.height = (int32_t)sc.m_height;
    m_quad_layer.subImage.imageArrayIndex = 0;
    m_quad_layer.pose.orientation.w = 1.0f;
    m_quad_layer.pose.position.x = 0.0f;
    m_quad_layer.pose.position.y = 0.0f;
    m_quad_layer.pose.position.z = -2.0f;
    // A ~2.6 m wide virtual screen 2 m away (~66 degrees of view).
    const float width_m = 2.6f;
    m_quad_layer.size.width = width_m;
    m_quad_layer.size.height = width_m * (float)sc.m_height / (float)sc.m_width;
    m_submit_quad = true;
}   // releaseScreenImage

// ----------------------------------------------------------------------------
bool XRManager::createEyeSwapchains()
{
    if (m_eye_swapchains_created)
        return true;
    for (int i = 0; i < 2; i++)
    {
        if (!createSwapchain(m_eye_swapchain[i],
                             m_view_config[i].recommendedImageRectWidth,
                             m_view_config[i].recommendedImageRectHeight, true))
            return false;
    }
    m_eye_swapchains_created = true;
    return true;
}   // createEyeSwapchains

// ----------------------------------------------------------------------------
GLuint XRManager::acquireEyeImage(int eye)
{
    XRSwapchain& sc = m_eye_swapchain[eye];
    if (sc.m_handle == XR_NULL_HANDLE || sc.m_acquired_index >= 0)
        return 0;
    XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage(sc.m_handle, &ai, &index)))
        return 0;
    XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(sc.m_handle, &wi)))
    {
        XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(sc.m_handle, &ri);
        return 0;
    }
    sc.m_acquired_index = (int)index;
    return sc.m_fbos[index];
}   // acquireEyeImage

// ----------------------------------------------------------------------------
void XRManager::releaseEyeImage(int eye)
{
    XRSwapchain& sc = m_eye_swapchain[eye];
    if (sc.m_acquired_index < 0)
        return;
    XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(sc.m_handle, &ri);
    sc.m_acquired_index = -1;
}   // releaseEyeImage

// ----------------------------------------------------------------------------
void XRManager::queueProjectionLayer()
{
    if (!m_views_valid || !m_eye_swapchains_created)
        return;
    for (int i = 0; i < 2; i++)
    {
        memset(&m_proj_views[i], 0, sizeof(m_proj_views[i]));
        m_proj_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        m_proj_views[i].pose = m_views[i].pose;
        m_proj_views[i].fov = m_views[i].fov;
        m_proj_views[i].subImage.swapchain = m_eye_swapchain[i].m_handle;
        m_proj_views[i].subImage.imageRect.offset.x = 0;
        m_proj_views[i].subImage.imageRect.offset.y = 0;
        m_proj_views[i].subImage.imageRect.extent.width =
            (int32_t)m_eye_swapchain[i].m_width;
        m_proj_views[i].subImage.imageRect.extent.height =
            (int32_t)m_eye_swapchain[i].m_height;
        m_proj_views[i].subImage.imageArrayIndex = 0;
    }
    memset(&m_proj_layer, 0, sizeof(m_proj_layer));
    m_proj_layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    m_proj_layer.layerFlags = 0;
    m_proj_layer.space = m_local_space;
    m_proj_layer.viewCount = 2;
    m_proj_layer.views = m_proj_views;
    m_submit_projection = true;
}   // queueProjectionLayer

// ----------------------------------------------------------------------------
void XRManager::endFrame()
{
    if (!m_frame_begun)
        return;
    const XrCompositionLayerBaseHeader* layers[2];
    uint32_t count = 0;
    if (m_should_render && m_submit_projection)
        layers[count++] = (const XrCompositionLayerBaseHeader*)&m_proj_layer;
    if (m_should_render && m_submit_quad)
        layers[count++] = (const XrCompositionLayerBaseHeader*)&m_quad_layer;

    XrFrameEndInfo ei = {XR_TYPE_FRAME_END_INFO};
    ei.displayTime = m_predicted_display_time;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = count;
    ei.layers = count > 0 ? layers : NULL;
    XrResult r = xrEndFrame(m_session, &ei);
    if (XR_FAILED(r))
    {
        static int error_count = 0;
        if (error_count++ < 10)
            Log::error("XR", "xrEndFrame failed: %s", xrResultString(m_instance, r));
    }
    else
    {
        static bool first = true;
        if (first)
        {
            first = false;
            Log::info("XR", "First frame submitted (%u layers).", count);
        }
    }
    m_frame_begun = false;
    m_submit_projection = false;
    m_submit_quad = false;
}   // endFrame

// ----------------------------------------------------------------------------
bool XRManager::presentFlatScreen(unsigned fb_width, unsigned fb_height)
{
    if (!m_initialized)
        return false;

    pollEvents();
    if (!m_session_running)
    {
        // Nothing to present into; avoid spinning the main loop at 100%.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return true;
    }
    if (!m_frame_begun)
        beginFrame();
    if (!m_frame_begun)
        return true;

    // If per-eye stereo rendering already queued a projection layer this
    // frame (ShaderBasedRenderer::renderVR), the eyes already have their
    // final image and there is nothing left to blit here.
    if (!m_submit_projection && m_should_render && fb_width > 0 && fb_height > 0)
    {
        uint32_t w = std::min<uint32_t>(fb_width, m_view_config[0].maxImageRectWidth);
        uint32_t h = std::min<uint32_t>(fb_height, m_view_config[0].maxImageRectHeight);
        if (m_screen_swapchain.m_handle == XR_NULL_HANDLE ||
            m_screen_swapchain.m_width != w || m_screen_swapchain.m_height != h)
        {
            if (!createSwapchain(m_screen_swapchain, w, h, false))
            {
                endFrame();
                return true;
            }
        }
        GLuint fbo = acquireScreenImage();
        if (fbo != 0)
        {
            GLint prev_read = 0, prev_draw = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);
            GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
            if (scissor)
                glDisable(GL_SCISSOR_TEST);
            if (m_has_srgb_write_control)
                glDisable(GL_FRAMEBUFFER_SRGB_EXT);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
            glBlitFramebuffer(0, 0, (GLint)fb_width, (GLint)fb_height,
                              0, 0, (GLint)w, (GLint)h,
                              GL_COLOR_BUFFER_BIT, GL_LINEAR);

            if (m_has_srgb_write_control)
                glEnable(GL_FRAMEBUFFER_SRGB_EXT);
            if (scissor)
                glEnable(GL_SCISSOR_TEST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw);
            releaseScreenImage();
        }
    }
    endFrame();
    return true;
}   // presentFlatScreen

#endif   // ENABLE_OPENXR

// ============================================================================
/** Called by COGLES2Driver::endScene() on Android instead of swapping the
 *  window. Returns true if the frame was handed to OpenXR. */
extern "C" bool stk_xr_present(unsigned width, unsigned height)
{
#ifdef ENABLE_OPENXR
    if (XRManager::isVRActive())
        return XRManager::get()->presentFlatScreen(width, height);
#endif
    return false;
}   // stk_xr_present
