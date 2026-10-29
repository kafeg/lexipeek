// `--snapshots <dir>`: what the shell draws, light and dark, rendered by XAML itself (RenderTargetBitmap, in a signed-in
// desktop; Mica's place filled with its base colour): <name>-light.png, <name>-dark.png, then done(the failures' count).
// Phase 1: the tray's menu (as it is and with its ⚠ lines), the card's stand-in (a text, the field to type in), the
// LexiPeek window, every Settings section × tab and About's tabs (<name>-<theme>-end.png too when the page scrolls), the
// windows to come, the install question.
#pragma once
#include <functional>
#include <string>
#include "shell.hpp"

namespace lexipeek::win {

void snapshots(Shell &shell, const std::string &dir, std::function<void(int)> done);

}  // namespace lexipeek::win
