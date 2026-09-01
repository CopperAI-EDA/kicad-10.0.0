/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <wx/aui/auibar.h>
#include <wx/aui/dockart.h>


class WX_AUI_TOOLBAR_ART : public wxAuiDefaultToolBarArt
{
public:
    WX_AUI_TOOLBAR_ART() :
            wxAuiDefaultToolBarArt()
    {
        saturateHighlightColor();
    }

    virtual ~WX_AUI_TOOLBAR_ART() = default;

#if wxCHECK_VERSION( 3, 3, 0 )
    wxSize GetToolSize( wxReadOnlyDC& aDc, wxWindow* aWindow, const wxAuiToolBarItem& aItem ) override;
#else
    wxSize GetToolSize( wxDC& aDc, wxWindow* aWindow, const wxAuiToolBarItem& aItem ) override;
#endif

    /**
     * Unfortunately we need to re-implement this to actually be able to control the size
     */
    void DrawBackground( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect ) override;
    void DrawPlainBackground( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect ) override;
    void DrawButton( wxDC& aDc, wxWindow* aWindow, const wxAuiToolBarItem& aItem,
                     const wxRect& aRect ) override;
    void DrawSeparator( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect ) override;
    void DrawGripper( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect ) override;

    void UpdateColoursFromSystem() override;

    int ShowDropDown( wxWindow* wnd, const wxAuiToolBarItemArray& items ) override;

private:
    void saturateHighlightColor();
};


class WX_AUI_DOCK_ART : public wxAuiDefaultDockArt
{
public:
    WX_AUI_DOCK_ART();

    void DrawSash( wxDC& aDc, wxWindow* aWindow, int aOrientation,
                   const wxRect& aRect ) override;
    void DrawBackground( wxDC& aDc, wxWindow* aWindow, int aOrientation,
                         const wxRect& aRect ) override;
    void DrawCaption( wxDC& aDc, wxWindow* aWindow, const wxString& aText,
                      const wxRect& aRect, wxAuiPaneInfo& aPane ) override;
    void DrawBorder( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect,
                     wxAuiPaneInfo& aPane ) override;

    /**
     * Draw the caption buttons (close, pin, maximise).
     *
     * wxAuiDefaultDockArt draws a hardcoded 3D raised/sunken box behind the
     * glyph on hover and press. That box is the strongest remaining Win32 tell
     * in the application, and it sits in the corner of every docked panel.
     */
    void DrawPaneButton( wxDC& aDc, wxWindow* aWindow, int aButton, int aButtonState,
                         const wxRect& aRect, wxAuiPaneInfo& aPane ) override;

    /**
     * wxAuiDockArt::Clone() is pure virtual and wxAuiDefaultDockArt::Clone()
     * returns a *base* copy, so without this a floated pane silently loses every
     * override above and reverts to stock rendering while keeping our colours.
     */
    wxAuiDockArt* Clone() override;
};


/**
 * Notebook tab rendering.
 *
 * Derives from wxAuiFlatTabArt, not wxAuiGenericTabArt. wx 3.3 ships both; the
 * generic one draws the old trapezoid-with-gradient tabs and the flat one is
 * what wxAuiDefaultTabArt is #defined to. Using the generic art was the reason
 * the notebook tabs kept looking like a Win32 application while the rest of the
 * chrome had been darkened.
 */
class WX_AUI_TAB_ART : public wxAuiFlatTabArt
{
public:
    WX_AUI_TAB_ART();

    wxNODISCARD wxAuiTabArt* Clone() override
    {
        // Constructed fresh rather than copied: wxAuiFlatTabArt deletes its copy
        // constructor because these are used polymorphically.
        return new WX_AUI_TAB_ART();
    }

    /**
     * wx 3.3 replaced DrawTab()'s close_button_state parameter with a per-page
     * button vector, so the "hide the close button on non-closable pages"
     * behaviour moved from an argument to editing page.buttons.
     */
    int DrawPageTab( wxDC& dc, wxWindow* wnd, wxAuiNotebookPage& page,
                     const wxRect& rect ) override;
};

