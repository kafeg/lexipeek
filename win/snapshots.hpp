// `--snapshots <dir>`: what the shell draws, light and dark, rendered by XAML itself (RenderTargetBitmap, in a signed-in
// desktop; Mica's place filled with its base colour): <name>-light.png, <name>-dark.png, then done(the failures' count).
// The tray's menu (as it is and with its ⚠ lines), the LexiPeek window, every Settings section × tab and About's tabs,
// the install question, then the cases of a cases file (scripts/snapshot-cases.sh's `cases` and `ai_cases`; fixtures:
// `ai_fixtures`' dir, with LEXIPEEK_AI_REPLIES; firstrun?step=… lines on an empty data dir, its files copied in from
// firstrun_from as the step needs); <name>-<theme>-end.png too when a page scrolls.
#pragma once
#include <functional>
#include <string>
#include "shell.hpp"

namespace lexipeek::win {

void snapshots(Shell &shell, const std::string &dir, const std::string &cases, const std::string &fixtures, const std::string &firstrun_from,
               std::function<void(int)> done);

}  // namespace lexipeek::win
