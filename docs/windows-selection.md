# Reading the selection on Windows

How LexiPeek gets the selected text out of the app in front (`platform/win/input.cpp`), which apps give it and which
cannot. The clipboard is never touched — Settings → Reading the selection promises that — and nothing is injected into
another process.

## The ways, in the order they are tried

1. **UI Automation, `TextPattern` at the focused element** (`read_selection`). `GetFocusedElement()` →
   `UIA_TextPatternId` → `GetSelection()`. Chromium and the apps built on it, Firefox, Win32 Edit controls through the
   UIA proxies, WinUI, and Qt 6 answer here. The selection's rectangles come from the same range, so the card opens at
   the text.
2. **The classic Edit messages at the focused control** (`read_edit`): `EM_GETSEL` for the offsets, `WM_GETTEXT` for the
   text, both marshalled between processes by the system. Taken for three window classes:
   - `Edit`, `RichEdit*` — Notepad and many Win32 apps. The offsets count UTF-16 units.
   - `Scintilla` — Notepad++ and the editors built on Scintilla. The offsets count **the document's bytes** (UTF-8 for a
     Unicode document), while `WM_GETTEXT` still gives UTF-16, so the text is converted and cut by bytes. An offset that
     would cut a character in half means the document is in an 8-bit code page and the selection is given up instead of
     guessed. These messages are deprecated in Scintilla's own documentation ("will be removed in future versions"), so
     this way can disappear with a future Notepad++. Measured: `Mädchen Haus …` with the second word selected gives
     `EM_GETSEL 9..13`, where the byte slice is "Haus" and the UTF-16 slice would be "aus "; the same text in Notepad
     gives `8..13`, i.e. UTF-16 units.
   The caret's rectangle (`GUITHREADINFO.rcCaret`) is the card's anchor here, not the selection's own.
3. Nothing read → the card with a field to type in, and the line "{app} does not show its selection to LexiPeek"
   (`card_unread`). That is the app's answer, not a failure of ours.

`LexiPeek.exe --probe-selection <file>` writes what both ways see at the focus right now: the focused element with its
patterns, every element of the window with a `TextPattern`, and the window messages with their offsets.

## What each app gives (measured, Windows 10 22H2)

A document full of one word, one word selected, then every interface asked at the focus: UI Automation
(`TextPattern`, `ValuePattern`, `LegacyIAccessible`), the whole window's subtree for any element with a `TextPattern`,
MSAA (`AccessibleObjectFromWindow` + `accFocus`), IAccessible2 (`IAccessibleText`, by `QueryInterface` and by
`IServiceProvider::QueryService`) and the classic Edit messages.

| App (engine) | UIA `TextPattern` at the focus | Elsewhere in the window | MSAA / IAccessible2 | Edit messages | Read from |
|---|---|---|---|---|---|
| Notepad (Win32 `Edit`) | yes, the selection | the same `Edit` | `IAccessible` with the whole text, no `IAccessibleText` | `EM_GETSEL` + `WM_GETTEXT` | UIA |
| Explorer++ (Win32 `Edit`) | yes, the selection | the same `Edit` | as Notepad | `EM_GETSEL` + `WM_GETTEXT` | UIA |
| Chromium, Vivaldi | yes, the selection | the document's elements | — | — | UIA |
| **Notepad++ (Scintilla)** | **no** — the element's *name* is the whole document, not the selection | none | `IAccessible` with the whole text, **no `IAccessibleText`** | **`EM_GETSEL` + `WM_GETTEXT` answer** | **the Edit messages** |
| **SumatraPDF 3.5.2** | no (the focus is the frame window) | only the toolbar's two `Edit` boxes | `IAccessible` "client" with the title, no `IAccessibleText` | nothing (not an Edit control) | **nothing** |
| **Opera 12.18 (Presto)** | no | none in the whole window | `IAccessible` "client" with the title only, no `IAccessibleText` | nothing | **nothing** |
| Falkon 3.1 (Qt 5 + QtWebEngine) | no, for the page's widget | only the address bar and the search box (Qt line edits) | `IAccessible` "grouping", no `IAccessibleText` | nothing | **nothing** (the page) |

Falkon stands in for Otter Browser, whose Windows binaries are served only from a host behind a bot check. Both are
Qt browsers: their own widgets (the address bar) get a `TextPattern` from Qt, their page does not.

**The MSAA / IAccessible2 idea was measured and dropped.** Not one of these apps exposes `IAccessibleText` — neither by
`QueryInterface` on the `IAccessible` nor through `IServiceProvider::QueryService`. A third fallback layer on
IAccessible2 would add code that never fires. MSAA does give an `IAccessible` almost everywhere, but only with the
*whole* text as the element's name or value, with no way to tell which part of it is selected.

For SumatraPDF, Opera 12 and the page of a Qt browser nothing honest is left: both remaining ways — reading the
clipboard after a synthetic Ctrl+C, or injecting into the process — are ruled out. Their card with a field to type in is
the answer.
