/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2020 Ian McInerney <Ian.S.McInerney at ieee.org>
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

#include <kiplatform/ui.h>

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <pthread.h>

#include <wx/dialog.h>
#include <wx/log.h>
#include <wx/nonownedwnd.h>
#include <wx/toplevel.h>
#include <wx/button.h>
#include <wx/settings.h>


bool KIPLATFORM::UI::IsDarkTheme()
{
    // Disabled for now because it appears that the wxWidgets event goes out before the
    // NSAppearance name has been updated
#ifdef NOTYET
    // It appears the wxWidgets event goes out before the NSAppearance name has been updated
    NSString *appearanceName = [[NSAppearance currentAppearance] name];
    return !![appearanceName containsString:@"Dark"];
#else
    wxColour bg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );

    // Weighted W3C formula
    double brightness = ( bg.Red() / 255.0 ) * 0.299 +
                        ( bg.Green() / 255.0 ) * 0.587 +
                        ( bg.Blue() / 255.0 ) * 0.117;

    return brightness < 0.5;
#endif
}


wxColour KIPLATFORM::UI::GetDialogBGColour()
{
     wxColor bg = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE );

    if( KIPLATFORM::UI::IsDarkTheme() )
        bg = bg.ChangeLightness( 80 );
    else
        bg = bg.ChangeLightness( 160 );

    return bg;
}


void KIPLATFORM::UI::GetInfoBarColours( wxColour& aFGColour, wxColour& aBGColour )
{
    aFGColour = wxSystemSettings::GetColour( wxSYS_COLOUR_INFOTEXT );

    // wxWidgets hard-codes wxSYS_COLOUR_INFOBK to { 0xFF, 0xFF, 0xD3 } on Mac.
    if( KIPLATFORM::UI::IsDarkTheme() )
        aBGColour = wxColour( 28, 27, 20 );
    else
        aBGColour = wxColour( 255, 249, 189 );
}


void KIPLATFORM::UI::ForceFocus( wxWindow* aWindow )
{
    // On OSX we need to forcefully give the focus to the window
    [[aWindow->GetHandle() window] makeFirstResponder: aWindow->GetHandle()];
}


bool KIPLATFORM::UI::IsWindowActive( wxWindow* aWindow )
{
    // Just always return true
    return true;
}


void KIPLATFORM::UI::EnsureVisible( wxWindow* aWindow )
{
    NSView* view = (NSView*)aWindow->GetHandle();
    if( view )
    {
        NSWindow* nsWindow = [view window];
        if( nsWindow )
        {
            [nsWindow setCollectionBehavior:
                NSWindowCollectionBehaviorCanJoinAllSpaces];
        }
    }
}


void KIPLATFORM::UI::ReparentWindow( wxNonOwnedWindow* aWindow, wxTopLevelWindow* aParent )
{
    NSWindow* parentWindow = aParent->GetWXWindow();
    NSWindow* theWindow    = aWindow->GetWXWindow();

    if( parentWindow && theWindow )
        [parentWindow addChildWindow:theWindow ordered:NSWindowAbove];
}


void KIPLATFORM::UI::ReparentModal( wxNonOwnedWindow* aWindow )
{
    // Quietly return if no parent is found
    if( wxTopLevelWindow* parent = static_cast<wxTopLevelWindow*>( wxGetTopLevelParent( aWindow->GetParent() ) ) )
        ReparentWindow( aWindow, parent );
}


void KIPLATFORM::UI::FixupCancelButtonCmdKeyCollision( wxWindow *aWindow )
{
    wxButton* button = dynamic_cast<wxButton*>( wxWindow::FindWindowById( wxID_CANCEL, aWindow ) );

    if( button )
    {
        static const wxString placeholder = wxT( "{amp}" );

        wxString buttonLabel = button->GetLabel();
        buttonLabel.Replace( wxT( "&&" ), placeholder );
        buttonLabel.Replace( wxT( "&" ), wxEmptyString );
        buttonLabel.Replace( placeholder, wxT( "&" ) );
        button->SetLabel( buttonLabel );
    }
}


bool KIPLATFORM::UI::IsStockCursorOk( wxStockCursor aCursor )
{
    switch( aCursor )
    {
    case wxCURSOR_SIZING:
    case wxCURSOR_BULLSEYE:
    case wxCURSOR_HAND:
    case wxCURSOR_ARROW:
        return true;
    default:
        return false;
    }
}


void KIPLATFORM::UI::LargeChoiceBoxHack( wxChoice* aChoice )
{
    // Not implemented
}


void KIPLATFORM::UI::EllipsizeChoiceBox( wxChoice* aChoice )
{
    // Not implemented
}


double KIPLATFORM::UI::GetPixelScaleFactor( const wxWindow* aWindow )
{
    return aWindow->GetContentScaleFactor();
}


double KIPLATFORM::UI::GetContentScaleFactor( const wxWindow* aWindow )
{
    // Native GUI resolution on Retina displays
    return GetPixelScaleFactor( aWindow ) / 2.0;
}


wxSize KIPLATFORM::UI::GetUnobscuredSize( const wxWindow* aWindow )
{
    return wxSize( aWindow->GetSize().GetX() - wxSystemSettings::GetMetric( wxSYS_VSCROLL_X ),
                   aWindow->GetSize().GetY() - wxSystemSettings::GetMetric( wxSYS_HSCROLL_Y ) );
}


void KIPLATFORM::UI::SetOverlayScrolling( const wxWindow* aWindow, bool overlay )
{
    // Not implemented
}


bool KIPLATFORM::UI::AllowIconsInMenus()
{
    return true;
}


wxPoint KIPLATFORM::UI::GetMousePosition()
{
    return wxGetMousePosition();
}


bool KIPLATFORM::UI::WarpPointer( wxWindow* aWindow, int aX, int aY )
{
    aWindow->WarpPointer( aX, aY );
    return true;
}


void KIPLATFORM::UI::ImmControl( wxWindow* aWindow, bool aEnable )
{
}


void KIPLATFORM::UI::ImeNotifyCancelComposition( wxWindow* aWindow )
{
}


bool KIPLATFORM::UI::InfiniteDragPrepareWindow( wxWindow* aWindow )
{
    return true;
}


void KIPLATFORM::UI::InfiniteDragReleaseWindow()
{
    // Not needed on this platform
}


void KIPLATFORM::UI::SetFloatLevel( wxWindow* aWindow )
{
    // On OSX we need to forcefully give the focus to the window
    [[aWindow->GetHandle() window] setLevel:NSFloatingWindowLevel];
}

void KIPLATFORM::UI::ReleaseChildWindow( wxNonOwnedWindow* aWindow )
{
    if( wxTopLevelWindow* parent = static_cast<wxTopLevelWindow*>(
            wxGetTopLevelParent( aWindow->GetParent() ) ) )
    {
        NSWindow* parentWindow = parent->GetWXWindow();
        NSWindow* theWindow = aWindow->GetWXWindow();

        if( parentWindow && theWindow )
        {
            [parentWindow removeChildWindow:theWindow];
            [theWindow setLevel:NSFloatingWindowLevel];
        }
    }
}


void KIPLATFORM::UI::AllowNetworkFileSystems( wxDialog* aDialog )
{
    // Not needed on macOS - file dialogs show network filesystems by default
}


wxColour KIPLATFORM::UI::GetPanelBGColour()
{
    if( IsDarkTheme() )
        return wxColour( 30, 30, 30 );
    return wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
}


void KIPLATFORM::UI::ApplyDarkWindowTheme( wxWindow* aWindow )
{
    // macOS handles dark mode automatically via the system appearance
}


void KIPLATFORM::UI::ApplyDarkFrameTheme( wxWindow* aWindow )
{
    // macOS handles dark mode automatically via the system appearance
}


void KIPLATFORM::UI::EnableWin32DarkMode()
{
    // Windows-only; no-op on macOS
}

void KIPLATFORM::UI::FixupWebViewKeyEquivalents( wxWindow* aWebView )
{
    // macOS WebView key handling is managed by the OS
}


/// Recursively search an NSView hierarchy for a WKWebView instance.
static WKWebView* FindWKWebView( NSView* root )
{
    if( [root isKindOfClass:[WKWebView class]] )
        return ( WKWebView* ) root;

    for( NSView* child in [root subviews] )
    {
        WKWebView* found = FindWKWebView( child );

        if( found )
            return found;
    }

    return nil;
}


/// Return true only when executing on the main thread's REAL pthread stack.
///
/// KiCad tool handlers run in COROUTINE fibers whose stacks are heap allocations on
/// the main thread -- so NSThread.isMainThread is true there, but the stack pointer
/// lies outside the pthread stack bounds.  JSContext construction must not occur on
/// such a stack.
static bool IsOnRealMainThreadStack()
{
    if( ![NSThread isMainThread] )
        return false;

    pthread_t self     = pthread_self();
    uintptr_t stackTop = (uintptr_t) pthread_get_stackaddr_np( self );
    size_t    size     = pthread_get_stacksize_np( self );
    uintptr_t sp       = (uintptr_t) __builtin_frame_address( 0 );

    return sp <= stackTop && sp >= stackTop - size;
}


bool KIPLATFORM::UI::RunWebViewScriptFireAndForget( wxWindow* aWebView, const wxString& aScript )
{
    if( !aWebView )
        return false;

    NSView* nativeView = aWebView->GetHandle();

    if( !nativeView )
        return false;

    WKWebView* wkWebView = FindWKWebView( nativeView );

    if( !wkWebView )
        return false;

    std::string scriptUtf8( aScript.utf8_str() );
    NSString*   script = [NSString stringWithUTF8String:scriptUtf8.c_str()];

    if( !script )
        return false;

    // nil completion handler = fire-and-forget: WebKit does NOT deserialize a result
    // through the shared JSContext, so this is safe to call from any stack (including
    // tool-coroutine fiber stacks).
    [wkWebView evaluateJavaScript:script completionHandler:nil];
    return true;
}


// Process-wide JSContext warm state.
static bool g_jscVmWarmed      = false;
static BOOL g_warmEvalInFlight = NO;
static BOOL g_warmReplyArrived = NO;
static BOOL g_warmReplyHadValue = NO;


bool KIPLATFORM::UI::WarmWebViewJSContext( wxWindow* aWebView )
{
    if( g_jscVmWarmed )
        return true;

    if( !aWebView )
        return false;

    if( !IsOnRealMainThreadStack() )
        return false;

    NSView* nativeView = aWebView->GetHandle();

    if( !nativeView )
        return false;

    WKWebView* wkWebView = FindWKWebView( nativeView );

    if( !wkWebView )
        return false;

    if( !g_warmEvalInFlight )
    {
        g_warmReplyArrived  = NO;
        g_warmReplyHadValue = NO;
        g_warmEvalInFlight  = YES;

        [wkWebView evaluateJavaScript:@"0"
                    completionHandler:^( id result, NSError* error ) {
                        (void) error;
                        g_warmReplyHadValue = ( result != nil );
                        g_warmReplyArrived  = YES;
                        g_warmEvalInFlight  = NO;
                    }];
    }

    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:3.0];

    while( !g_warmReplyArrived && [deadline timeIntervalSinceNow] > 0 )
    {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }

    if( g_warmReplyArrived && g_warmReplyHadValue )
        g_jscVmWarmed = true;
    else
        wxLogTrace( "webview", "WarmWebViewJSContext: warm-up %s; will retry",
                    g_warmReplyArrived ? "returned no value" : "timed out (reply still pending)" );

    return g_jscVmWarmed;
}


void KIPLATFORM::UI::PokeWebViewJSContext( wxWindow* aWebView )
{
    if( !g_jscVmWarmed )
        return;

    if( !aWebView )
        return;

    if( !IsOnRealMainThreadStack() )
        return;

    NSView* nativeView = aWebView->GetHandle();

    if( !nativeView )
        return;

    WKWebView* wkWebView = FindWKWebView( nativeView );

    if( !wkWebView )
        return;

    [wkWebView evaluateJavaScript:@"0"
                completionHandler:^( id result, NSError* error ) {
                    (void) result;
                    (void) error;
                }];
}
