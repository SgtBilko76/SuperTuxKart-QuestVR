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

#include "xr/xr_input.hpp"

#ifdef ENABLE_OPENXR

#include "xr/xr_manager.hpp"
#include "input/device_manager.hpp"
#include "input/gamepad_config.hpp"
#include "input/gamepad_device.hpp"
#include "input/input_manager.hpp"
#include "utils/log.hpp"

#include <cstring>
#include <vector>

XRInput* XRInput::m_xr_input = NULL;

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

#define XR_CHECK(call, what)                                              \
    do                                                                    \
    {                                                                     \
        XrResult _r = (call);                                             \
        if (XR_FAILED(_r))                                                \
        {                                                                 \
            Log::error("XRInput", "%s failed: %s", what,                  \
                       xrResultString(XRManager::get()->getInstance(), _r)); \
            return false;                                                 \
        }                                                                 \
    } while (0)

// ----------------------------------------------------------------------------
void XRInput::create()
{
    if (!m_xr_input)
        m_xr_input = new XRInput();
}   // create

// ----------------------------------------------------------------------------
void XRInput::destroy()
{
    delete m_xr_input;
    m_xr_input = NULL;
}   // destroy

// ----------------------------------------------------------------------------
XRInput::XRInput()
{
}   // XRInput

// ----------------------------------------------------------------------------
XRInput::~XRInput()
{
    if (m_action_set != XR_NULL_HANDLE)
        xrDestroyActionSet(m_action_set);
}   // ~XRInput

// ----------------------------------------------------------------------------
/** Creates one float action per analog input and one boolean action per
 *  button. Each action is bound to a single, specific controller input in
 *  suggestBindings() below, so none of them need declared subaction paths. */
bool XRInput::createActions()
{
    XrInstance instance = XRManager::get()->getInstance();

    XrActionSetCreateInfo set_info = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strncpy(set_info.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    strncpy(set_info.localizedActionSetName, "Gameplay",
            XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    set_info.priority = 0;
    XR_CHECK(xrCreateActionSet(instance, &set_info, &m_action_set),
             "xrCreateActionSet");

    struct ActionDef
    {
        XrAction* action;
        XrActionType type;
        const char* name;
        const char* localized;
    };
    const ActionDef defs[] = {
        { &m_steer_action,     XR_ACTION_TYPE_FLOAT_INPUT,   "steer",     "Steer" },
        { &m_accel_action,     XR_ACTION_TYPE_FLOAT_INPUT,   "accelerate","Accelerate" },
        { &m_brake_action,     XR_ACTION_TYPE_FLOAT_INPUT,   "brake",     "Brake" },
        { &m_nitro_action,     XR_ACTION_TYPE_BOOLEAN_INPUT, "nitro",     "Nitro" },
        { &m_fire_action,      XR_ACTION_TYPE_BOOLEAN_INPUT, "fire",      "Fire" },
        { &m_drift_action,     XR_ACTION_TYPE_BOOLEAN_INPUT, "drift",     "Drift" },
        { &m_rescue_action,    XR_ACTION_TYPE_BOOLEAN_INPUT, "rescue",    "Rescue" },
        { &m_look_back_action, XR_ACTION_TYPE_BOOLEAN_INPUT, "look_back", "Look Back" },
        { &m_pause_action,     XR_ACTION_TYPE_BOOLEAN_INPUT, "pause",     "Pause" },
    };
    for (const ActionDef& d : defs)
    {
        XrActionCreateInfo info = {XR_TYPE_ACTION_CREATE_INFO};
        strncpy(info.actionName, d.name, XR_MAX_ACTION_NAME_SIZE - 1);
        strncpy(info.localizedActionName, d.localized,
                XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        info.actionType = d.type;
        info.countSubactionPaths = 0;
        info.subactionPaths = NULL;
        XR_CHECK(xrCreateAction(m_action_set, &info, d.action),
                 "xrCreateAction");
    }
    return true;
}   // createActions

// ----------------------------------------------------------------------------
bool XRInput::suggestBindings()
{
    XrInstance instance = XRManager::get()->getInstance();

    struct BindingDef
    {
        XrAction action;
        const char* path;
    };
    const BindingDef defs[] = {
        { m_steer_action,     "/user/hand/left/input/thumbstick/x" },
        { m_accel_action,     "/user/hand/right/input/trigger/value" },
        { m_brake_action,     "/user/hand/left/input/trigger/value" },
        { m_nitro_action,     "/user/hand/right/input/a/click" },
        { m_fire_action,      "/user/hand/right/input/b/click" },
        { m_drift_action,     "/user/hand/left/input/thumbstick/click" },
        { m_rescue_action,    "/user/hand/left/input/x/click" },
        { m_look_back_action, "/user/hand/left/input/y/click" },
        { m_pause_action,     "/user/hand/left/input/menu/click" },
    };

    std::vector<XrActionSuggestedBinding> bindings;
    for (const BindingDef& d : defs)
    {
        XrPath path;
        XR_CHECK(xrStringToPath(instance, d.path, &path), "xrStringToPath");
        bindings.push_back({ d.action, path });
    }

    XrPath profile;
    XR_CHECK(xrStringToPath(instance,
        "/interaction_profiles/oculus/touch_controller", &profile),
        "xrStringToPath(touch_controller)");

    XrInteractionProfileSuggestedBinding suggested =
        {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = profile;
    suggested.suggestedBindings = bindings.data();
    suggested.countSuggestedBindings = (uint32_t)bindings.size();
    XR_CHECK(xrSuggestInteractionProfileBindings(instance, &suggested),
             "xrSuggestInteractionProfileBindings");

    // PICO's controllers expose the exact same button/axis names (a/b/x/y/
    // menu/trigger/thumbstick) under their own interaction profile paths, so
    // the same binding table applies unchanged. This is non-fatal: on a
    // runtime that doesn't recognise these profiles (e.g. Quest) the call
    // is simply rejected and Touch-controller input above is unaffected.
    const char* pico_profiles[] = {
        "/interaction_profiles/bytedance/pico4_controller",
        "/interaction_profiles/bytedance/pico4s_controller",
        "/interaction_profiles/bytedance/pico_neo3_controller",
    };
    for (const char* pico_profile : pico_profiles)
    {
        XrPath ppath;
        if (XR_FAILED(xrStringToPath(instance, pico_profile, &ppath)))
            continue;
        XrInteractionProfileSuggestedBinding psuggested =
            {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        psuggested.interactionProfile = ppath;
        psuggested.suggestedBindings = bindings.data();
        psuggested.countSuggestedBindings = (uint32_t)bindings.size();
        XrResult r = xrSuggestInteractionProfileBindings(instance, &psuggested);
        if (XR_FAILED(r))
        {
            Log::info("XRInput", "%s not supported by this runtime (%s), "
                "skipping.", pico_profile, xrResultString(instance, r));
        }
    }

    XrSessionActionSetsAttachInfo attach_info =
        {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach_info.countActionSets = 1;
    attach_info.actionSets = &m_action_set;
    XR_CHECK(xrAttachSessionActionSets(XRManager::get()->getSession(),
             &attach_info), "xrAttachSessionActionSets");
    return true;
}   // suggestBindings

// ----------------------------------------------------------------------------
/** Registers one synthetic "Oculus Touch" GamePadDevice with STK's device
 *  manager, exactly like a real gamepad would register itself, so it picks
 *  up STK's default gamepad bindings and works for both in-race and menu
 *  input with no other engine changes. */
void XRInput::registerGamePad()
{
    // An id that won't collide with any real SDL/Android joystick id.
    m_gamepad_id = 0x51455354;   // 'QEST'

    DeviceManager* dm = input_manager->getDeviceManager();
    GamepadConfig* config = NULL;
    dm->getConfigForGamepad(m_gamepad_id, "Oculus Touch", &config);
    // getConfigForGamepad() creates a fresh GamepadConfig with 0 axes;
    // unlike button count, GamePadDevice's constructor never fixes this up,
    // so every IT_STICKMOTION event would otherwise be rejected outright by
    // GamePadDevice::processAndMapInput()'s "id >= getNumberOfAxes()" check.
    config->setNumberOfAxis(2);
    GamePadDevice* device = new GamePadDevice(m_gamepad_id, "Oculus Touch",
        2 /* axes: steer, accel/brake */, 8 /* buttons */, config);
    dm->addGamepad(device);

    // There is only ever one real input device on Quest. Make it the
    // deterministic "latest used device" so STK's normal solo-play
    // auto-join (KartSelectionScreen::init(), for !m_multiplayer) binds to
    // it immediately instead of falling back to a phantom keyboard - see
    // DeviceManager::getLatestUsedDevice()'s fallback comment. That existing
    // auto-join is what actually calls setSinglePlayer() for us once a race
    // is being set up; nothing else needs to be done here.
    dm->setLatestUsedDevice(device);
}   // registerGamePad

// ----------------------------------------------------------------------------
bool XRInput::init()
{
    if (!createActions())
        return false;
    if (!suggestBindings())
        return false;
    registerGamePad();
    Log::info("XRInput", "Quest Touch controller input ready.");
    return true;
}   // init

// ----------------------------------------------------------------------------
void XRInput::dispatchAxis(int axis_id, int old_value, int new_value)
{
    if (old_value == new_value)
        return;
    input_manager->dispatchInput(Input::IT_STICKMOTION, m_gamepad_id,
        axis_id, Input::AD_NEUTRAL, new_value);
}   // dispatchAxis

// ----------------------------------------------------------------------------
void XRInput::dispatchButton(int button_id, bool old_pressed, bool new_pressed)
{
    if (old_pressed == new_pressed)
        return;
    input_manager->dispatchInput(Input::IT_STICKBUTTON, m_gamepad_id,
        button_id, Input::AD_POSITIVE,
        new_pressed ? Input::MAX_VALUE : 0);
}   // dispatchButton

// ----------------------------------------------------------------------------
void XRInput::update()
{
    XrSession session = XRManager::get()->getSession();

    XrActiveActionSet active = { m_action_set, XR_NULL_PATH };
    XrActionsSyncInfo sync_info = {XR_TYPE_ACTIONS_SYNC_INFO};
    sync_info.countActiveActionSets = 1;
    sync_info.activeActionSets = &active;
    if (XR_FAILED(xrSyncActions(session, &sync_info)))
        return;

    auto getFloat = [session](XrAction action) -> float
    {
        XrActionStateGetInfo info = {XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        XrActionStateFloat state = {XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_FAILED(xrGetActionStateFloat(session, &info, &state)) ||
            !state.isActive)
            return 0.0f;
        return state.currentState;
    };
    auto getBool = [session](XrAction action) -> bool
    {
        XrActionStateGetInfo info = {XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        XrActionStateBoolean state = {XR_TYPE_ACTION_STATE_BOOLEAN};
        if (XR_FAILED(xrGetActionStateBoolean(session, &info, &state)) ||
            !state.isActive)
            return false;
        return state.currentState == XR_TRUE;
    };

    // Steer: thumbstick X in [-1,1] -> STK's signed 16-bit axis range.
    float steer = getFloat(m_steer_action);
    int steer_val = (int)(steer * (float)Input::MAX_VALUE);
    dispatchAxis(0, m_last_steer_val, steer_val);
    m_last_steer_val = steer_val;

    // Accelerate/brake share one axis (PA_ACCEL is the negative direction,
    // PA_BRAKE the positive one, see GamepadConfig::setDefaultBinds()).
    float accel = getFloat(m_accel_action);
    float brake = getFloat(m_brake_action);
    int throttle_val = (int)((brake - accel) * (float)Input::MAX_VALUE);
    dispatchAxis(1, m_last_throttle_val, throttle_val);
    m_last_throttle_val = throttle_val;

    // Buttons 0-5 are STK's default gameplay binds (fire/nitro/drift/
    // rescue/look-back/pause, see GamepadConfig::setDefaultBinds()).
    const XrAction button_actions[6] = { m_fire_action, m_nitro_action,
        m_drift_action, m_rescue_action, m_look_back_action, m_pause_action };
    for (int i = 0; i < 6; i++)
    {
        bool pressed = getBool(button_actions[i]);
        dispatchButton(i, m_last_button[i], pressed);
        m_last_button[i] = pressed;
    }

    // Buttons 6/7 are STK's separate menu confirm/cancel binds (same
    // GamepadConfig, lines 142-143) - reuse the A/B presses for those too,
    // so the same physical buttons work both in races and in menus/dialogs.
    bool nitro_pressed = getBool(m_nitro_action);
    dispatchButton(6, m_last_button[6], nitro_pressed);
    m_last_button[6] = nitro_pressed;

    bool fire_pressed = getBool(m_fire_action);
    dispatchButton(7, m_last_button[7], fire_pressed);
    m_last_button[7] = fire_pressed;
}   // update

#endif   // ENABLE_OPENXR
