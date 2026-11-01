import AppKit
import CxxStdlib
import LexiPeekCore

SelectionTest.respawnDisclaimed()
// A snapshot run never touches the person's data: its own data dir, only the packs linked (scripts/snapshots.sh).
if Snapshot.dir != nil, case let why = String(lexipeek.app.isolated(std.string(), std.string())), !why.isEmpty {
    FileHandle.standardError.write(Data("refused: \(why)\n".utf8))
    exit(2)
}

L.apply()
let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
