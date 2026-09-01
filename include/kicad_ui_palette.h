/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

#ifndef KICAD_UI_PALETTE_H
#define KICAD_UI_PALETTE_H

#include <wx/colour.h>

/**
 * The dark chrome palette, in one place.
 *
 * Before this there were two competing palettes and neither knew about the
 * other. common/widgets/wx_aui_art_providers.cpp:43-58 defined the AUI chrome
 * (caption 24,24,24; border 45,45,45; sash 18,18,18), while
 * libs/kiplatform/port/wxmsw/ui.cpp:369-374 defined everything else
 * (panel 30,30,30; field 18,18,18; border 68,68,68; fg 245,245,245). The second
 * one wins on Windows because ApplyDarkWindowTheme() runs on wxEVT_SHOW and
 * recursively overwrites what construction set -- which is also why editing
 * colours in an individual panel's constructor has no visible effect.
 *
 * Deliberately header-only and dependency-free:
 *
 *  - kiplatform is a static library that links core, kimath, nlohmann_json and
 *    wx, but NOT kicommon. Anything kiplatform must consume cannot live in
 *    kicommon, so an out-of-line implementation there is unreachable from
 *    wxmsw/ui.cpp -- the very file that most needs it.
 *  - These values are the input to KIPLATFORM::UI::GetPanelBGColour() and
 *    friends, so the palette must not call back into KIPLATFORM::UI. That would
 *    be circular.
 *  - inline constexpr-style accessors mean a colour tweak recompiles only the
 *    handful of translation units that include this, rather than relinking
 *    kicommon.dll and everything downstream.
 *
 * These are the Windows dark-chrome values. macOS and Linux continue to derive
 * their colours from their own platform code; nothing here changes for them.
 */
namespace KIUI
{
namespace PALETTE
{

/// Default surface: dock panels, toolbars, pane backgrounds.
inline wxColour Surface()        { return wxColour(  30,  30,  30 ); }

/// Recessed surface: caption bars, anything sitting behind Surface.
inline wxColour SurfaceSunken()  { return wxColour(  24,  24,  24 ); }

/// Raised surface: hover fills, the active tab.
inline wxColour SurfaceRaised()  { return wxColour(  37,  37,  38 ); }

/// Pressed state, one step below Surface.
inline wxColour SurfacePressed() { return wxColour(  20,  20,  20 ); }

/// Text entry and list backgrounds. Deliberately darker than Surface -- this is
/// the existing fieldBg, and it is why the Hierarchy tree reads darker than the
/// panel containing it.
inline wxColour Field()          { return wxColour(  18,  18,  18 ); }

/// Structural lines. Kept close to Surface: on a dark UI, spacing separates
/// content more cleanly than strokes.
inline wxColour Hairline()       { return wxColour(  42,  42,  42 ); }

/// Heavier border, for controls that genuinely need an outline.
inline wxColour Border()         { return wxColour(  68,  68,  68 ); }

/// Primary body text.
inline wxColour TextPrimary()    { return wxColour( 212, 212, 212 ); }

/// De-emphasised text: caption titles, placeholders, disabled labels. Caption
/// bars use this; full-strength white on dark inverts the visual hierarchy by
/// making the frame louder than its contents.
inline wxColour TextMuted()      { return wxColour( 150, 150, 150 ); }

/// Bright text, for the few places that should out-rank TextPrimary.
inline wxColour TextBright()     { return wxColour( 245, 245, 245 ); }

/// Accent. Used sparingly and deliberately: the active tab indicator and the
/// property grid selection row. Everything else distinguishes state with the
/// Surface* ramp, which is why an accent is needed at all -- 6-12 unit grey
/// deltas read as unfinished rather than intentional.
inline wxColour Accent()         { return wxColour(   0, 122, 204 ); }

/// Accent hover/pressed.
inline wxColour AccentHover()    { return wxColour(  28, 151, 234 ); }

} // namespace PALETTE
} // namespace KIUI

#endif // KICAD_UI_PALETTE_H
