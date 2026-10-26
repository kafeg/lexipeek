import AppKit

// Checks without Screen Recording: LEXIPEEK_SNAPSHOT=<dir> writes the app's own views as PNGs.
enum Snapshot {
    static let dir = ProcessInfo.processInfo.environment["LEXIPEEK_SNAPSHOT"]

    static func write(_ view: NSView?, _ name: String) {
        guard let dir, let view, let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return }
        view.cacheDisplay(in: view.bounds, to: rep)
        try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: dir).appendingPathComponent(name))
    }
}
