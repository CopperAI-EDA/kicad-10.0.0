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

#include <wx/aui/aui.h>
#include <wx/aui/framemanager.h>
#include <wx/aui/auibook.h>
#include <wx/bitmap.h>
#include <wx/dc.h>
#include <wx/settings.h>

#include <kiplatform/ui.h>
#include <kicad_ui_palette.h>
#include <wx/graphics.h>
#include <wx/dcgraph.h>
#include <pgm_base.h>
#include <settings/common_settings.h>
#include <widgets/panel_notebook_base.h>
#include <widgets/wx_aui_art_providers.h>
#include <gal/color4d.h>


static void drawAuiToolbarBackground( wxDC& aDc, const wxRect& aRect )
{
    aDc.SetPen( *wxTRANSPARENT_PEN );
    aDc.SetBrush( wxBrush( KIPLATFORM::UI::GetPanelBGColour() ) );
    aDc.DrawRectangle( aRect );
}


static wxColour auiPanelBg()
{
    return KIPLATFORM::UI::GetPanelBGColour();
}


static wxColour auiCaptionBg()
{
    return KIUI::PALETTE::SurfaceSunken();
}


static wxColour auiBorderColour()
{
    // Was ( 45, 45, 45 ), which disagreed with the ( 68, 68, 68 ) that
    // ApplyDarkWindowTheme paints everywhere else. Hairline is deliberately
    // closer to Surface: the goal is separation by spacing, not by stroke.
    return KIUI::PALETTE::Hairline();
}



#if wxCHECK_VERSION( 3, 3, 0 )
wxSize WX_AUI_TOOLBAR_ART::GetToolSize( wxReadOnlyDC& aDc, wxWindow* aWindow,
                                        const wxAuiToolBarItem& aItem )
#else
wxSize WX_AUI_TOOLBAR_ART::GetToolSize( wxDC& aDc, wxWindow* aWindow,
                                        const wxAuiToolBarItem& aItem )
#endif
{
    // Based on the upstream wxWidgets implementation, but simplified for our application
    int size = aWindow->FromDIP( Pgm().GetCommonSettings()->m_Appearance.toolbar_icon_size );

    int width = size;
    int height = size;

    if( ( m_flags & wxAUI_TB_TEXT ) && !aItem.GetLabel().empty() )
    {
        aDc.SetFont( m_font );
        int tx, ty;

        if( m_textOrientation == wxAUI_TBTOOL_TEXT_BOTTOM )
        {
            aDc.GetTextExtent( wxT( "ABCDHgj" ), &tx, &ty );
            height += ty;

            if( !aItem.GetLabel().empty() )
            {
                aDc.GetTextExtent( aItem.GetLabel(), &tx, &ty );
                width = wxMax( width, tx + aWindow->FromDIP( 6 ) );
            }
        }
        else if( m_textOrientation == wxAUI_TBTOOL_TEXT_RIGHT )
        {
            width += aWindow->FromDIP( 3 ); // space between left border and bitmap
            width += aWindow->FromDIP( 3 ); // space between bitmap and text

            if( !aItem.GetLabel().empty() )
            {
                aDc.GetTextExtent( aItem.GetLabel(), &tx, &ty );
                width += tx;
                height = wxMax( height, ty );
            }
        }
    }

    if( aItem.HasDropDown() )
    {
        int dropdownWidth = GetElementSize( wxAUI_TBART_DROPDOWN_SIZE );
        width += dropdownWidth + aWindow->FromDIP( 4 );
    }

    return wxSize( width, height );
}


void WX_AUI_TOOLBAR_ART::DrawBackground( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect )
{
    drawAuiToolbarBackground( aDc, aRect );
}


void WX_AUI_TOOLBAR_ART::DrawPlainBackground( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect )
{
    drawAuiToolbarBackground( aDc, aRect );
}


void WX_AUI_TOOLBAR_ART::DrawButton( wxDC& aDc, wxWindow* aWindow, const wxAuiToolBarItem& aItem,
                                     const wxRect& aRect )
{
    drawAuiToolbarBackground( aDc, aRect );

    // Based on upstream implementation
    int bmpX = 0, bmpY = 0;
    int textX = 0, textY = 0;

    const wxBitmap& bmp = aItem.GetCurrentBitmapFor( aWindow );
    const wxSize    bmpSize = bmp.IsOk() ? bmp.GetLogicalSize() : wxSize( 0, 0 );

    if( ( m_flags & wxAUI_TB_TEXT ) && !aItem.GetLabel().empty() )
    {
        aDc.SetFont( m_font );

        int textWidth = 0, textHeight = 0;
        int tx, ty;

        aDc.GetTextExtent( wxT( "ABCDHgj" ), &tx, &textHeight );
        aDc.GetTextExtent( aItem.GetLabel(), &textWidth, &ty );

        if( m_textOrientation == wxAUI_TBTOOL_TEXT_BOTTOM )
        {
            bmpX = aRect.x + ( aRect.width / 2 ) - ( bmpSize.x / 2 );

            bmpY = aRect.y + ( ( aRect.height - textHeight ) / 2 ) - ( bmpSize.y / 2 );

            textX = aRect.x + ( aRect.width / 2 ) - ( textWidth / 2 ) + 1;
            textY = aRect.y + aRect.height - textHeight - 1;
        }
        else if( m_textOrientation == wxAUI_TBTOOL_TEXT_RIGHT )
        {
            bmpX = aRect.x + aWindow->FromDIP( 3 );

            bmpY = aRect.y + ( aRect.height / 2 ) - ( bmpSize.y / 2 );

            textX = bmpX + aWindow->FromDIP( 3 ) + bmpSize.x;
            textY = aRect.y + ( aRect.height / 2 ) - ( textHeight / 2 );
        }
    }
    else
    {
        bmpX = aRect.x + ( aRect.width / 2 ) - ( bmpSize.x / 2 );
        bmpY = aRect.y + ( aRect.height / 2 ) - ( bmpSize.y / 2 );
    }

    bool isThemeDark = KIPLATFORM::UI::IsDarkTheme();

    if( !( aItem.GetState() & wxAUI_BUTTON_STATE_DISABLED ) )
    {
        if( aItem.GetState() & wxAUI_BUTTON_STATE_PRESSED )
        {
            aDc.SetPen( wxPen( m_highlightColour ) );
            aDc.SetBrush( wxBrush( m_highlightColour.ChangeLightness( isThemeDark ? 20 : 150 ) ) );
            aDc.DrawRectangle( aRect );
        }
        else if( ( aItem.GetState() & wxAUI_BUTTON_STATE_HOVER ) || aItem.IsSticky() )
        {
            aDc.SetPen( wxPen( m_highlightColour ) );
            aDc.SetBrush( wxBrush( m_highlightColour.ChangeLightness( isThemeDark ? 40 : 170 ) ) );

            // draw an even lighter background for checked item hovers (since
            // the hover background is the same color as the check background)
            if( aItem.GetState() & wxAUI_BUTTON_STATE_CHECKED )
                aDc.SetBrush(
                        wxBrush( m_highlightColour.ChangeLightness( isThemeDark ? 50 : 180 ) ) );

            aDc.DrawRectangle( aRect );
        }
        else if( aItem.GetState() & wxAUI_BUTTON_STATE_CHECKED )
        {
            // it's important to put this code in an else statement after the
            // hover, otherwise hovers won't draw properly for checked items
            aDc.SetPen( wxPen( m_highlightColour ) );
            aDc.SetBrush( wxBrush( m_highlightColour.ChangeLightness( isThemeDark ? 40 : 170 ) ) );
            aDc.DrawRectangle( aRect );
        }
    }

    if( bmp.IsOk() )
        aDc.DrawBitmap( bmp, bmpX, bmpY, true );

    // set the item's text color based on if it is disabled
    aDc.SetTextForeground( wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT ) );

    if( aItem.GetState() & wxAUI_BUTTON_STATE_DISABLED )
    {
        aDc.SetTextForeground( wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );
    }

    if( ( m_flags & wxAUI_TB_TEXT ) && !aItem.GetLabel().empty() )
    {
        aDc.DrawText( aItem.GetLabel(), textX, textY );
    }
}


void WX_AUI_TOOLBAR_ART::DrawSeparator( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect )
{
    drawAuiToolbarBackground( aDc, aRect );

    wxRect line = aRect;
    wxColour sepColour( 70, 70, 70 );
    aDc.SetPen( wxPen( sepColour ) );

    if( aRect.height > aRect.width )
        aDc.DrawLine( aRect.GetLeft() + aRect.width / 2, aRect.GetTop() + 3,
                      aRect.GetLeft() + aRect.width / 2, aRect.GetBottom() - 3 );
    else
        aDc.DrawLine( aRect.GetLeft() + 3, aRect.GetTop() + aRect.height / 2,
                      aRect.GetRight() - 3, aRect.GetTop() + aRect.height / 2 );
}


void WX_AUI_TOOLBAR_ART::DrawGripper( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect )
{
    drawAuiToolbarBackground( aDc, aRect );
}


void WX_AUI_TOOLBAR_ART::saturateHighlightColor()
{
#ifdef __WXOSX__
    // Use a slightly stronger highlight colour over grey toolbar backgrounds
    KIGFX::COLOR4D highlight( m_highlightColour );
    m_highlightColour = highlight.Saturate( 0.6 ).ToColour();
#endif
}


void WX_AUI_TOOLBAR_ART::UpdateColoursFromSystem()
{
    wxAuiDefaultToolBarArt::UpdateColoursFromSystem();
    saturateHighlightColor();
}


class ToolbarCommandCapture : public wxEvtHandler
{
public:
    ToolbarCommandCapture() { m_lastId = 0; }
    int GetCommandId() const { return m_lastId; }

    bool ProcessEvent( wxEvent& evt ) override
    {
        if( evt.GetEventType() == wxEVT_MENU )
        {
            m_lastId = evt.GetId();
            return true;
        }

        if( GetNextHandler() )
            return GetNextHandler()->ProcessEvent( evt );

        return false;
    }

private:
    int m_lastId;
};


int WX_AUI_TOOLBAR_ART::ShowDropDown( wxWindow* wnd, const wxAuiToolBarItemArray& items )
{
    wxMenu menuPopup;
    bool   skipNextSeparator = true;

    size_t i, count = items.GetCount();
    for( i = 0; i < count; ++i )
    {
        wxAuiToolBarItem& item = items.Item( i );

        if( item.GetKind() == wxITEM_SEPARATOR )
        {
            if( !skipNextSeparator )
            {
                menuPopup.AppendSeparator();
                skipNextSeparator = true;
            }
        }
        else if( item.GetKind() == wxITEM_NORMAL || item.GetKind() == wxITEM_CHECK || item.GetKind() == wxITEM_RADIO )
        {
            wxString text = item.GetShortHelp();

            if( text.empty() )
                text = item.GetLabel();

            if( text.empty() )
                text = wxT( " " );

            wxString firstLine = text.BeforeFirst( '\n' );
            wxString accel;
            wxString label = firstLine.BeforeFirst( '\t', &accel );

            text = label;

            if( !accel.empty() )
            {
                // Remove brackets from accelerator string so it's recognized
                if( accel.starts_with( "(" ) && accel.ends_with( ")" ) )
                    accel = accel.Mid( 1, accel.size() - 2 );

                text << "\t" << accel;
            }

            bool       checked = item.GetState() & wxAUI_BUTTON_STATE_CHECKED;
            wxItemKind menuKind = wxITEM_NORMAL;

            if( ( item.GetKind() == wxITEM_CHECK || item.GetKind() == wxITEM_RADIO ) && checked )
                menuKind = static_cast<wxItemKind>( item.GetKind() );

            wxMenuItem* m = new wxMenuItem( &menuPopup, item.GetId(), text, item.GetShortHelp(), menuKind );

            if( !m->IsCheckable() )
                m->SetBitmap( item.GetBitmapBundle() );

            menuPopup.Append( m );

            if( m->IsCheckable() )
                m->Check( checked );

            skipNextSeparator = false;
        }
    }

    // find out where to put the popup menu of window items
    wxPoint pt = ::wxGetMousePosition();
    pt = wnd->ScreenToClient( pt );

    // find out the screen coordinate at the bottom of the tab ctrl
    wxRect cli_rect = wnd->GetClientRect();
    pt.y = cli_rect.y + cli_rect.height;

    ToolbarCommandCapture* cc = new ToolbarCommandCapture;
    wnd->PushEventHandler( cc );
    wnd->PopupMenu( &menuPopup, pt );
    int command = cc->GetCommandId();
    wnd->PopEventHandler( true );

    return command;
}


WX_AUI_DOCK_ART::WX_AUI_DOCK_ART() :
        wxAuiDefaultDockArt()
{
#if defined( _WIN32 )
    // Use normal control font, wx likes to use "small"
    m_captionFont = *wxNORMAL_FONT;

    // Increase the box the caption rests in size a bit
    m_captionSize = ( wxNORMAL_FONT->GetPointSize() * 7 ) / 4 + 6;
#endif

    SetColour( wxAUI_DOCKART_BACKGROUND_COLOUR, auiPanelBg() );
    SetColour( wxAUI_DOCKART_SASH_COLOUR, KIUI::PALETTE::Field() );
    SetColour( wxAUI_DOCKART_BORDER_COLOUR, auiBorderColour() );
    SetColour( wxAUI_DOCKART_GRIPPER_COLOUR, KIUI::PALETTE::TextMuted() );
    SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, auiCaptionBg() );
    SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR, auiCaptionBg() );
    SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, auiCaptionBg() );
    SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR, auiCaptionBg() );

    // Caption text was ( 245, 245, 245 ), brighter than the TextPrimary used for
    // the content inside the pane. That inverts the hierarchy -- the frame ends
    // up louder than what it frames.
    SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, KIUI::PALETTE::TextMuted() );
    SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, KIUI::PALETTE::TextMuted() );

    SetMetric( wxAUI_DOCKART_PANE_BORDER_SIZE, 0 );

    // Set once here. eeschema used to override this to FromDIP( 6 ) after
    // construction, which wx then DPI-scaled a second time in
    // GetMetricForWindow -- about 13px at 150%, and only in eeschema.
    SetMetric( wxAUI_DOCKART_SASH_SIZE, 6 );

    // Turn off the ridiculous looking gradient
    m_gradientType = wxAUI_GRADIENT_NONE;
}


void WX_AUI_DOCK_ART::DrawSash( wxDC& aDc, wxWindow*, int, const wxRect& aRect )
{
    aDc.SetPen( *wxTRANSPARENT_PEN );
    aDc.SetBrush( wxBrush( wxColour( 18, 18, 18 ) ) );
    aDc.DrawRectangle( aRect );
}


void WX_AUI_DOCK_ART::DrawBackground( wxDC& aDc, wxWindow*, int, const wxRect& aRect )
{
    aDc.SetPen( *wxTRANSPARENT_PEN );
    aDc.SetBrush( wxBrush( auiPanelBg() ) );
    aDc.DrawRectangle( aRect );
}


void WX_AUI_DOCK_ART::DrawCaption( wxDC& aDc, wxWindow*, const wxString& aText,
                                   const wxRect& aRect, wxAuiPaneInfo& aPane )
{
    wxRect rect = aRect;

    aDc.SetPen( *wxTRANSPARENT_PEN );
    aDc.SetBrush( wxBrush( auiCaptionBg() ) );
    aDc.DrawRectangle( rect );

    aDc.SetFont( m_captionFont );
    aDc.SetTextForeground( wxColour( 245, 245, 245 ) );

    wxRect textRect = rect;
    textRect.Deflate( 6, 0 );

    if( aPane.HasCloseButton() )
        textRect.SetRight( textRect.GetRight() - m_buttonSize - 4 );

    aDc.DrawLabel( aText, textRect, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL );
}


void WX_AUI_DOCK_ART::DrawBorder( wxDC& aDc, wxWindow*, const wxRect& aRect,
                                  wxAuiPaneInfo& aPane )
{
    if( !aPane.HasBorder() )
        return;

    // wxAUI_DOCKART_PANE_BORDER_SIZE is 0, so a docked pane has no space
    // allocated for a border: stroking one here painted over the first row of
    // the child's own pixels, which is the faint outline that appeared around
    // panel contents. Docked panes are separated by the sash and by spacing.
    //
    // A floating pane is a different case -- it has no neighbours to separate it
    // from, so it does get an outline.
    if( !aPane.IsFloating() )
        return;

    aDc.SetPen( wxPen( auiBorderColour(), 1 ) );
    aDc.SetBrush( *wxTRANSPARENT_BRUSH );
    aDc.DrawRectangle( aRect );
}


void WX_AUI_TAB_ART::DrawTab( wxDC& dc, wxWindow* wnd, const wxAuiNotebookPage& page, const wxRect& in_rect,
                              int close_button_state, wxRect* out_tab_rect, wxRect* out_button_rect,
                              int* x_extent )
{
    PANEL_NOTEBOOK_BASE* panel = dynamic_cast<PANEL_NOTEBOOK_BASE*>( page.window );

    if( panel && !panel->GetClosable() )
        close_button_state = wxAUI_BUTTON_STATE_HIDDEN;

    return wxAuiGenericTabArt::DrawTab( dc, wnd, page, in_rect, close_button_state, out_tab_rect,
                                        out_button_rect, x_extent );
}


void WX_AUI_DOCK_ART::DrawPaneButton( wxDC& aDc, wxWindow* aWindow, int aButton,
                                      int aButtonState, const wxRect& aRect,
                                      wxAuiPaneInfo& aPane )
{
    // wxAuiDefaultDockArt draws a 3D raised box on hover and a sunken one on
    // press, using the Win32 system colours. Flat surfaces and a hand-drawn
    // glyph instead.
    //
    // The glyph is drawn with DrawLine rather than GetPaneButtonBitmap(): that
    // accessor is wx 3.3 only and this tree's CMake floor is 3.2.
    wxRect rect = aRect;

    if( aButtonState == wxAUI_BUTTON_STATE_HIDDEN )
        return;

    // Nothing at rest -- a button that is always visibly boxed reads as heavy.
    if( aButtonState == wxAUI_BUTTON_STATE_HOVER
            || aButtonState == wxAUI_BUTTON_STATE_PRESSED )
    {
        const wxColour fill = ( aButtonState == wxAUI_BUTTON_STATE_PRESSED )
                                      ? KIUI::PALETTE::SurfacePressed()
                                      : KIUI::PALETTE::SurfaceRaised();

        wxRect hover = rect;
        hover.Deflate( 1 );

        // wxGCDC for an antialiased corner; plain wxDC::DrawRoundedRectangle
        // gives visibly stepped edges at this size.
        wxGCDC gcdc( static_cast<wxWindowDC&>( aDc ) );

        if( wxGraphicsContext* gc = gcdc.GetGraphicsContext() )
        {
            gc->SetAntialiasMode( wxANTIALIAS_DEFAULT );
            gcdc.SetPen( *wxTRANSPARENT_PEN );
            gcdc.SetBrush( wxBrush( fill ) );
            gcdc.DrawRoundedRectangle( hover, aWindow->FromDIP( 3 ) );
        }
        else
        {
            aDc.SetPen( *wxTRANSPARENT_PEN );
            aDc.SetBrush( wxBrush( fill ) );
            aDc.DrawRectangle( hover );
        }
    }

    const wxColour glyph = ( aButtonState == wxAUI_BUTTON_STATE_HOVER
                             || aButtonState == wxAUI_BUTTON_STATE_PRESSED )
                                   ? KIUI::PALETTE::TextPrimary()
                                   : KIUI::PALETTE::TextMuted();

    aDc.SetPen( wxPen( glyph, aWindow->FromDIP( 1 ) ) );

    // Inset well inside the button box; the stock glyph nearly fills it, which
    // is part of why it reads as cramped.
    wxRect g = rect;
    g.Deflate( aWindow->FromDIP( 5 ) );

    switch( aButton )
    {
    case wxAUI_BUTTON_CLOSE:
        aDc.DrawLine( g.GetLeft(), g.GetTop(), g.GetRight() + 1, g.GetBottom() + 1 );
        aDc.DrawLine( g.GetLeft(), g.GetBottom() + 1, g.GetRight() + 1, g.GetTop() );
        break;

    case wxAUI_BUTTON_PIN:
        // A simple horizontal bar reads as "pinned" without the stock pin
        // bitmap, which is a Win32-era raster.
        aDc.DrawLine( g.GetLeft(), g.GetBottom(), g.GetRight() + 1, g.GetBottom() );
        break;

    case wxAUI_BUTTON_MAXIMIZE_RESTORE:
        aDc.SetBrush( *wxTRANSPARENT_BRUSH );
        aDc.DrawRectangle( g );
        break;

    default:
        // Anything else keeps the stock rendering rather than guessing.
        wxAuiDefaultDockArt::DrawPaneButton( aDc, aWindow, aButton, aButtonState, aRect, aPane );
        break;
    }
}


wxAuiDockArt* WX_AUI_DOCK_ART::Clone()
{
    // Copy-construct: wxAuiDefaultDockArt::Clone() returns a base instance, so a
    // floated pane would keep our colours but lose every Draw* override and
    // render as stock Win32.
    return new WX_AUI_DOCK_ART( *this );
}
