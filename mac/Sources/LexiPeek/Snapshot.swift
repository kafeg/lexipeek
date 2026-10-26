import AppKit

// Checks without Screen Recording: LEXIPEEK_SNAPSHOT=<dir> writes the app's own views as PNGs.
enum Snapshot {
    static let dir = ProcessInfo.processInfo.environment["LEXIPEEK_SNAPSHOT"]
    // LEXIPEEK_SNAPSHOT_TAB=dictionary: cards open on Dictionary (to see that tab).
    static let dictionary = dir != nil && ProcessInfo.processInfo.environment["LEXIPEEK_SNAPSHOT_TAB"] == "dictionary"

    static func write(_ view: NSView?, _ name: String) {
        guard let dir, let view, let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return }
        view.cacheDisplay(in: view.bounds, to: rep)
        try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: dir).appendingPathComponent(name))
    }
}
