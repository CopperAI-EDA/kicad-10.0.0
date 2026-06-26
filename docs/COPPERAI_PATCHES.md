# CopperAI Patches — KiCad 10.0.0

This document captures every CopperAI-specific modification to the KiCad 10.0.0 source tree, why each exists, and the pitfalls discovered during the port from 9.0.7.

---

## Build

```bash
cd vendor/kicad-mac-builder/build-release/kicad/src/kicad-build
make -j8 kicommon eeschema        # incremental rebuild of the two most-changed targets
make -j8                          # full rebuild
```

After rebuilding, copy artifacts to the bundle and re-sign before launching (see `docs/COPPERAI_PATCHES.md` in `kicad-mac-builder`).

---

## Architecture Changes: KiCad 9 → 10

| KiCad 9 | KiCad 10 |
|---|---|
| Separate `.app` bundles per tool (eeschema.app, pcbnew.app, …) | Single `CopperAI.app`; all tools are `.kiface` plugins under `PlugIns/` |
| `Pgm()` differs per sub-app | `Pgm()` returns the shared `PGM_KICAD` singleton across all frames |
| `DATASHEET_FIELD` integer constant | `FIELD_T::DATASHEET` typed enum |
| `IPC_API` server init scattered | Native `KICAD_IPC_API=ON` cmake flag; server init lives in `kicad/kicad.cpp` |

---

## Modified Files

### `libs/kiplatform/include/kiplatform/ui.h`

**What:** Added declarations for two new functions:

```cpp
bool WarmWebViewJSContext( wxWindow* aWebView );
void PokeWebViewJSContext( wxWindow* aWebView );
```

**Why:** KiCad tool handlers run in COROUTINE fiber stacks (heap-allocated, on the main thread). JavaScriptCore (JSC) detects this as an off-main-thread context and crashes when it tries to create a new VM. `WarmWebViewJSContext` forces JSC VM creation on the real main-thread stack at startup; `PokeWebViewJSContext` keeps it alive with periodic no-op evals.

---

### `libs/kiplatform/port/wxosx/ui.mm`

**What:** Three additions:

1. `FindWKWebView(NSView*)` — recursive NSView search to locate the underlying `WKWebView` from a `wxWebView`.

2. `RunWebViewScriptFireAndForget` — **critical fix**. KiCad 10 shipped this as a stub returning `false`:
   ```cpp
   // WRONG — was the stub:
   bool KIPLATFORM::UI::RunWebViewScriptFireAndForget(...) { return false; }
   ```
   The macOS `RunScriptAsync` path **always** calls this first. If it returns `false`, scripts go to `m_deferredScripts` for retry. Since the stub always returned `false`, no script ever executed — including the IPC bridge injection (`window.kicad.ipc`). This was the root cause of "KiCad bridge is not connected".

   Fixed to use `[WKWebView evaluateJavaScript:completionHandler:nil]` — nil completion handler means no JSC deserialization, safe from any stack.

   **Critical ordering**: `RunWebViewScriptFireAndForget` must be defined AFTER `FindWKWebView` in the file, or you get a forward-reference compile error.

3. `WarmWebViewJSContext` / `PokeWebViewJSContext` — full macOS implementations using a synchronous run-loop spin to wait for the JSC warm-up eval to complete.

**Stubs added to:**
- `libs/kiplatform/port/wxgtk/ui.cpp`
- `libs/kiplatform/port/wxmsw/ui.cpp`

---

### `eeschema/sch_edit_frame.h` + `eeschema/sch_edit_frame.cpp`

**What:** The Copper AI agent panel (right-side WebView panel with notebook tabs).

**Key members added:**
```cpp
wxAuiPaneInfo*   m_ollamaAgentPane;
wxAuiNotebook*   m_ollamaAgentNotebook;
wxPanel*         m_ollamaAgentTabPanel;
wxPanel*         m_datasheetTabPanel;
wxWebView*       m_ollamaAgentWebView;
wxWebView*       m_datasheetWebView;
wxString         m_pendingDatasheetUrl;
```

**AUI pane persistence trap:** `wxAuiManager` saves/restores pane visibility from config. Setting `Show(true)` in `AddPane()` is overridden by the saved perspective. Solution: use `CallAfter` to force-show after perspective restore:

```cpp
CallAfter( [this]() {
    wxAuiPaneInfo& agentPane = m_auimgr.GetPane( OllamaAgentPaneName() );
    if( !agentPane.IsShown() ) {
        agentPane.Show( true );
        m_auimgr.Update();
    }
    EnsureOllamaNotebook();
    LoadOllamaAgentWebView();
} );
```

**API field change:** KiCad 9 used `DATASHEET_FIELD` (integer). KiCad 10 uses `FIELD_T::DATASHEET`:
```cpp
// KiCad 9:  symbol->GetField( DATASHEET_FIELD )
// KiCad 10: symbol->GetField( FIELD_T::DATASHEET )
```

---

### `eeschema/tools/sch_actions.cpp`

**What:** `toggleOllamaAgent` TOOL_ACTION with `BITMAPS::copper_ai_agent` icon.

**Bitmap system pitfall (caused a crash):** Adding a new bitmap requires TWO changes:
1. `include/bitmaps/bitmaps_list.h` — add enum entry
2. `common/bitmap_info.cpp` — add cache entries mapping enum → PNG filenames

If you only update `bitmaps_list.h` and forget `bitmap_info.cpp`, `BITMAP_STORE::GetBitmapBundleDef` returns a null/empty image, which crashes in `wxImage::ResampleBox` when the toolbar is built. The crash stack looks like:
```
wxImage::ResampleBox → resampleImage → BITMAP_STORE::GetBitmapBundleDef
→ ACTION_TOOLBAR::Add → SCH_EDIT_FRAME constructor
```

**Also:** PNG files must be added to `resources/bitmaps_png/png/` AND to the `images.tar.gz` archive in the build tree (cmake doesn't auto-detect new files mid-build):
```bash
cd resources/bitmaps_png/png
# add new PNGs then:
cd $BUILD_DIR/resources
gunzip images.tar.gz && tar -rf images.tar -C $SRC/resources/bitmaps_png/png new_icon_24.png ... && gzip -9 images.tar
```

---

### `eeschema/tools/sch_editor_control.h` + `sch_editor_control.cpp`

**What:** `ToggleOllamaAgent` tool implementation and `Go<>()` registration.

---

### `eeschema/toolbars_sch_editor.cpp`

**What:** `.AppendAction( SCH_ACTIONS::toggleOllamaAgent )` in `TOOLBAR_LOC::RIGHT` section of `DefaultToolbarConfig()`.

---

### `eeschema/api/api_handler_sch.cpp`

**What:** Sub-sheet serialization fix in `handleSaveDocumentToString`.

**Why:** Default implementation always serializes the root schematic. When a sub-sheet is open, the agent gets the wrong sheet. Fix uses `m_frame->GetCurrentSheet().Last()` to serialize the displayed sheet instead of `schematic->Root()`.

---

### `common/bitmap_info.cpp`

**What:** Added `copper_ai_agent` entries:

```cpp
aBitmapInfoCache[BITMAPS::copper_ai_agent].emplace_back(
    BITMAPS::copper_ai_agent, wxT("copper_ai_agent_24.png"), 24, wxT("light"));
aBitmapInfoCache[BITMAPS::copper_ai_agent].emplace_back(
    BITMAPS::copper_ai_agent, wxT("copper_ai_agent_dark_24.png"), 24, wxT("dark"));
aBitmapInfoCache[BITMAPS::copper_ai_agent].emplace_back(
    BITMAPS::copper_ai_agent, wxT("copper_ai_agent_48.png"), 48, wxT("light"));
aBitmapInfoCache[BITMAPS::copper_ai_agent].emplace_back(
    BITMAPS::copper_ai_agent, wxT("copper_ai_agent_dark_48.png"), 48, wxT("dark"));
```

---

### `common/widgets/webview_panel.cpp`

**What:** JSContext warm-up timer (`m_jsWarmTimer`) and deferred script flush loop (`FlushDeferredScripts`).

**How the macOS script path works:**
1. `RunScriptAsync(script)` on macOS: calls `RunWebViewScriptFireAndForget` first.
2. If that returns `true` → done.
3. If `false` → appends to `m_deferredScripts`, starts a 100ms retry timer.
4. `FlushDeferredScripts` drains the queue, again via `RunWebViewScriptFireAndForget`.
5. On macOS, `wxWebView::RunScriptAsync()` is **never** called directly (JSC crash risk).

---

### `kicad/kicad.cpp`

**What:** Branding (`CopperAI`) and removed a duplicate `KICAD_IPC_API` server initialization block that conflicted with KiCad 10's native init.

---

## cmake Flags (in build cache)

| Flag | Value | Effect |
|---|---|---|
| `KICAD_IPC_API` | `ON` | Enables protobuf in-process API server |
| `KICAD_PRODUCTION_AGENT_CHAT_DEFAULT` | `ON` | Sets default chat URL to `https://getcopper.dev/chat?copper_client=kicad` |

---

## JSC JIT Crash (macOS 15.7+)

`CopperAI.app` crashes intermittently (~80%) on schematic open due to JSC JIT initializing on a coroutine fiber stack.

**Fix:** `JSC_useJIT=0` in `LSEnvironment` of `Info.plist`. NOT a signing issue.

---

## Runtime Chat URL

Set via `LSEnvironment` in `Info.plist`:
```xml
<key>KICAD_AGENT_CHAT_URL</key>
<string>https://getcopper.dev/chat?copper_client=kicad</string>
```

The `dmgbuild/run.sh` script sets this automatically when building a DMG.
