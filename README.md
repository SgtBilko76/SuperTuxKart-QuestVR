# SuperTuxKart Quest VR

[![Sponsor](https://img.shields.io/badge/Sponsor-SgtBilko76-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/SgtBilko76)

A standalone VR port of [SuperTuxKart](https://supertuxkart.net/), the free open-source kart racer, for Meta Quest. No PC needed.

- Native OpenXR app on Quest (arm64)
- Races rendered in real per-eye stereo with head tracking, layered on the normal kart-following camera
- Menus shown on a floating screen in VR
- Touch controllers mapped to SuperTuxKart's standard gamepad controls

## Install

1. Enable developer mode on your Quest.
2. Download the APK from the [Releases](../../releases) page.
3. Sideload it with [SideQuest](https://sidequestvr.com/) or `adb install -r <file>.apk`.
4. Launch it from *Library → Unknown Sources*.

## Controls

| Input | Action |
|---|---|
| Left thumbstick | Steer |
| Right trigger | Accelerate |
| Left trigger | Brake |
| Face buttons | Nitro, fire, drift, rescue, look back, pause |

## Status

Beta. Please report problems in [Issues](../../issues).

---

## Original SuperTuxKart README

> Below is the upstream project's README, kept for credits, licensing and desktop build instructions.

## SuperTuxKart

[![Linux build status](https://github.com/supertuxkart/stk-code/actions/workflows/linux.yml/badge.svg)](https://github.com/supertuxkart/stk-code/actions/workflows/linux.yml)
[![Apple build status](https://github.com/supertuxkart/stk-code/actions/workflows/apple.yml/badge.svg)](https://github.com/supertuxkart/stk-code/actions/workflows/apple.yml)
[![Windows build status](https://github.com/supertuxkart/stk-code/actions/workflows/windows.yml/badge.svg)](https://github.com/supertuxkart/stk-code/actions/workflows/windows.yml)
[![Switch build status](https://github.com/supertuxkart/stk-code/actions/workflows/switch.yml/badge.svg)](https://github.com/supertuxkart/stk-code/actions/workflows/switch.yml)
[![#supertuxkart on the libera IRC network](https://img.shields.io/badge/libera-%23supertuxkart-brightgreen.svg)](https://web.libera.chat/?channels=#supertuxkart)

SuperTuxKart is a free kart racing game. It focuses on fun and not on realistic kart physics. Instructions can be found on the in-game help page.

The SuperTuxKart homepage can be found at <https://supertuxkart.net/>. There is also our [FAQ](https://supertuxkart.net/FAQ) and information on how get in touch with the [community](https://supertuxkart.net/Community).

The latest release binaries can be found [here](https://github.com/supertuxkart/stk-code/releases/latest), and those of preview releases [here](https://github.com/supertuxkart/stk-code/releases/preview).

### Hardware Requirements
Any hardware that supports OpenGL >= 3.3 or OpenGL ES >= 3.0 should be able to run SuperTuxKart. For Android, Android 5.0 or greater is required.

This includes dedicated GPUs from Nvidia or AMD released after 2010, integrated GPUs released after 2012 and Android phones released after 2014.

The game will run on some very weak devices, but a better GPU allows to run smoothly using higher quality graphics and a higher resolution, and a better CPU helps to keep a smooth playing experience in online multiplayer.

Please check the "What are the hardware requirements?" question in our [FAQ](https://supertuxkart.net/FAQ) for more information.

### License
The software is released under the GNU General Public License (GPL) which can be found in the file [`COPYING`](/COPYING) in the same directory as this file.

---

### Building from source

Building instructions can be found in [`INSTALL.md`](/INSTALL.md)

### Contributing code

**To contribute code to the official STK repository, please review the ['How to contribute code'](https://supertuxkart.net/How_to_contribute_code) guide.** It contains important guidelines to make the process smoother and maximize the chances that your contribution is accepted.

### 3D coordinates
A reminder for those who are looking at the code and 3D models:

SuperTuxKart: X right, Y up, Z forwards

Blender: X right, Y forwards, Z up

The export utilities perform the needed transformation, so in Blender you just work with the XY plane as ground, and things will appear fine in STK (using XZ as ground in the code, obviously).
