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

#ifndef HEADER_XR_INPUT_HPP
#define HEADER_XR_INPUT_HPP

#ifdef ENABLE_OPENXR

// Only core-spec action/session types are used here (no graphics- or
// platform-specific extension types), so the plain core header is enough.
#include <openxr/openxr.h>

/**
 * \brief Maps Quest Touch controller input to STK's existing gamepad input
 *        pipeline (see src/input/), via one synthetic "Oculus Touch"
 *        GamePadDevice fed through InputManager::dispatchInput() every
 *        frame. STK's normal gamepad axis/button bindings
 *        (GamepadConfig::setDefaultBinds()) are reused unmodified: axis 0 =
 *        steer, axis 1 = accelerate/brake, buttons 0-7 = fire/nitro/drift/
 *        rescue/look-back/pause/menu-ok/menu-cancel.
 * \ingroup xr
 */
class XRInput
{
private:
    static XRInput* m_xr_input;

    XrActionSet m_action_set = XR_NULL_HANDLE;

    XrAction m_steer_action    = XR_NULL_HANDLE;   // float, left thumbstick X
    XrAction m_accel_action    = XR_NULL_HANDLE;   // float, right trigger
    XrAction m_brake_action    = XR_NULL_HANDLE;   // float, left trigger
    XrAction m_nitro_action    = XR_NULL_HANDLE;   // bool,  right A
    XrAction m_fire_action     = XR_NULL_HANDLE;   // bool,  right B
    XrAction m_drift_action    = XR_NULL_HANDLE;   // bool,  right squeeze
    XrAction m_rescue_action   = XR_NULL_HANDLE;   // bool,  left X
    XrAction m_look_back_action = XR_NULL_HANDLE;  // bool,  left Y
    XrAction m_pause_action    = XR_NULL_HANDLE;   // bool,  left menu

    /** Id this synthetic controller was registered under with STK's
     *  DeviceManager; also used as the "device id" for dispatchInput(). */
    int m_gamepad_id = -1;

    int m_last_steer_val = 0;
    int m_last_throttle_val = 0;
    bool m_last_button[6] = { false, false, false, false, false, false };

    bool createActions();
    bool suggestBindings();
    void registerGamePad();
    void dispatchAxis(int axis_id, int old_value, int new_value);
    void dispatchButton(int button_id, bool old_pressed, bool new_pressed);

public:
    static void create();
    static void destroy();
    static XRInput* get()               { return m_xr_input; }

    XRInput();
    ~XRInput();

    /** Must be called once, after XRManager::get()->init() has created the
     *  session (actions can only be attached to a session once). */
    bool init();

    /** xrSyncActions + read every action + dispatchInput() on change. Call
     *  once per frame, after XRManager::get()->beginFrame(). */
    void update();
};

#endif   // ENABLE_OPENXR

#endif   // HEADER_XR_INPUT_HPP
