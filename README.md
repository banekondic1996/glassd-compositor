# labwc

- [1. Project Description](#1-project-description)
  - [1.1 What Is This?](#11-what-is-this)
  - [1.2 Why](#12-why)
- [2. Build and Installation](#2-build-and-installation)
- [3. Configuration](#3-configuration)
- [4. Theming](#4-theming)
- [5. Usage](#5-usage)
  - [5.1 Gaming](#51-gaming)
- [6. Integration](#6-integration)
- [7. Preview](#7-preview)

## 1. Project Description

This is modified Labwc compositor that exposes cursor position and can control windows via IPC

### 1.1 What Is This?

Labwc stands for Lab Wayland Compositor, where lab can mean any of the
following:

- lightweight and *box-inspired
- sense of experimentation and treading new ground
- inspired by BunsenLabs and ArchLabs
- your favorite pet

Labwc is a [wlroots]-based window-stacking compositor for [Wayland], inspired
by [Openbox].

It is light-weight and independent with a focus on simply stacking windows well
and rendering some window decorations. It takes a no-bling/frills approach and
says no to features such as animations.  It relies on clients for panels,
screenshots, wallpapers and so on to create a full desktop environment.

Labwc tries to stay in keeping with [wlroots] and [sway] in terms of general
approach and coding style.

Labwc has no reliance on any particular Desktop Environment, Desktop Shell or
session. Nor does it depend on any UI toolkits such as Qt or GTK.

### 1.2 Why?
**Based on my opinion:**
Because I needed cursor position on screen to be available,
and I need to integrate my own shell. This is by far fastest and
and most simple Wayland compositor, that fits my needs

**Based on creators of Labwc:**
Firstly, we believe that there is a need for a simple Wayland window-stacking
compositor which strikes a balance between minimalism and bloat approximately
at the level where Window Managers like Openbox reside in the X11 domain.  Most
of the core developers are accustomed to low resource Desktop Environments such
as Mate/XFCE or standalone Window Managers such as Openbox under X11.  Labwc
aims to make a similar setup possible under Wayland, with small and independent
components rather than a large, integrated software eco-system.

Secondly, the Wayland community has achieved an amazing amount so far, and we
want to help solve the unsolved problems to make Wayland viable for more
people. We think that standardisation and de-fragmentation is a route to
greater Wayland adoption, and wanting to play our part in this, Labwc only
understands [wayland-protocols] &amp; [wlr-protocols], and it cannot be
controlled with dbus, sway/i3/custom-IPC or other technology.

Thirdly, it is important to us that scope is tightly controlled so that the
compositor matures to production quality. On the whole, we value robustness,
reliability, stability and simplicity over new features. Coming up with new
ideas and features is easy - maintaining and stabilising them is not.

Fourthly, we are of the view that a compositor should be boring in order to do
its job well. In this regard we follow in the footsteps of [metacity] which
describes itself as a "Boring window manager for the adult in you. Many window
managers are like Marshmallow Froot Loops; Metacity is like Cheerios."

Finally, we think that an elegant solution to all of this does not need to feel
square and pixelated like something out of the 1990s, but should look
contemporary and enable cutting-edge performance.

## 2. Build and Installation

To build, simply run:

    meson setup build/
    meson compile -C build/

Run-time dependencies include:

- wlroots wayland libinput xkbcommon libxml2 cairo pango glib-2.0 libpng
- librsvg >=2.46 (optional)
- libsfdo (optional)
- xwayland, xcb (optional)

Build dependencies include:

- meson, ninja, gcc/clang
- wayland-protocols

**Disable xwayland with `meson -Dxwayland=disabled build/`** 

For OS/distribution specific details see [wiki].

If the right version of `wlroots` is not found on the system, the build setup
will automatically download the wlroots repo. If this fallback is not desired
please use:

    meson setup --wrap-mode=nodownload build/

To enforce the supplied wlroots.wrap file, run:

    meson setup --force-fallback-for=wlroots build/

If installing after using the wlroots.wrap file, use the following to
prevent installing the wlroots headers:

    meson install --skip-subprojects -C build/

## 3. Configuration

User config files are located at `${XDG_CONFIG_HOME:-$HOME/.config/labwc/}`
with the following six files being used: [rc.xml], [menu.xml], [autostart], [shutdown],
[environment] and [themerc-override].

Run `labwc --reconfigure` to reload configuration and theme.

For a step-by-step initial configuration guide, see [getting-started].

## 4. Theming

Themes are located at `~/.local/share/themes/\<theme-name\>/labwc/` or
equivalent `XDG_DATA_{DIRS,HOME}` location in accordance with freedesktop XDG
directory specification.

For full theme options, see [labwc-theme(5)] or the [themerc] example file.

For themes, search the internet for "openbox themes" and place them in
`~/.local/share/themes/`. Some good starting points include:

- https://github.com/addy-dclxvi/openbox-theme-collections
- https://github.com/the-zero885/Lubuntu-Arc-Round-Openbox-Theme
- https://github.com/BunsenLabs/bunsen-themes

## 5. Usage

    ./build/labwc [-s <command>]

> **_NOTE:_** If you are running on **NVIDIA**, you will need the
> `nvidia-drm.modeset=1` kernel parameter.

If you have not created an rc.xml config file, default bindings will be:

| combination              | action
| ------------------------ | ------
| `alt`-`tab`              | activate next window
| `alt`-`shift`-`tab`      | activate previous window
| `super`-`return`         | lab-sensible-terminal
| `alt`-`F4`               | close window
| `super`-`a`              | toggle maximize
| `super`-`mouse-left`     | move window
| `super`-`mouse-right`    | resize window
| `super`-`arrow`          | resize window to fill half the output
| `alt`-`space`            | show the window menu
| `XF86_AudioLowerVolume`  | amixer sset Master 5%-
| `XF86_AudioRaiseVolume`  | amixer sset Master 5%+
| `XF86_AudioMute`         | amixer sset Master toggle
| `XF86_MonBrightnessUp`   | brightnessctl set +10%
| `XF86_MonBrightnessDown` | brightnessctl set 10%-

A root-menu can be opened by clicking on the desktop.

### 5.1 Gaming

Cursor confinement is supported from version `0.6.2`. If using older versions,
use a nested [gamescope] instance for gaming.  It can be added to steam via
game launch option: `gamescope -f -- %command%`.

## 6. Integration

Suggested apps to use with Labwc:

- Screen shooter: [grim]
- Screen recorder: [wf-recorder]
- Background image: [swaybg]
- Panel: [waybar], [lavalauncher], [sfwbar], [xfce4-panel]
- Launchers: [bemenu], [fuzzel], [wofi]
- Output managers: [wlopm], [kanshi], [wlr-randr]
- Screen locker: [swaylock]
- Gamma adjustment: [gammastep]
- Idle screen inhibitor: [sway-audio-idle-inhibit]

See [integration] for further details.

## 7. Preview

<img width="1917" height="994" alt="image" src="https://github.com/user-attachments/assets/4945c683-b8fa-4bd9-b6f7-47dc858acef6" />

