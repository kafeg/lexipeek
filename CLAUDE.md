# LexiPeek

Pop-up dictionary and offline translator for macOS: select text in any app, press a hotkey, a card opens at the
cursor. Desktop sibling of InkPeek, uses its language packs. Spec — `docs/spec.md` (source of truth).
Public repo (MIT): no secrets, keys, personal paths or IPs in commits.

## Stack
C++20 + CMake. `core/` — no Qt (std, sqlite3, nlohmann/json, libcurl, zstd), tested through `lexipeek-cli`.
`app/` — Qt 6 Quick / QML; `app/platform/mac/` — Objective-C++ (hotkey, selected text, non-activating panel).
Engines as submodules at InkPeek's commits: Bergamot, PCRE2, fastText, llama.cpp. macOS 14+, Apple Silicon only.

## Build and run
Not set up yet (milestone M0): `make build`, `make test`, `make install` are the planned commands.

## Rules
- Branch model: `main` only, commit and push to `main`.
- Spec first: a new feature → `docs/spec.md` → implement by milestone, stop after each.
- **InkPeek is the reference.** No shared code. Behaviour and data formats (`packs.json`, sync log, records) are
  defined in InkPeek first; LexiPeek matches them. `tests/inkpeek` (submodule, pinned) gives the cross-test data;
  moving its pin = catching up: bump, run tests, fix LexiPeek. Never change a case to make LexiPeek pass.
- **No AI in core.** Look-up, header and translation work offline from the packs. AI is an optional tab, only on
  the person's action. No guessing heuristics in data.
- **An app, not a machine.** Everything comes from the app's own downloads (InkPeek's `packs` release, sha256).
- `core/` never includes Qt or platform headers. KISS; comments — one line at most.
- The API key never goes to logs, UI or commits.
