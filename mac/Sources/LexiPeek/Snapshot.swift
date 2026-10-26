import AppKit
import LexiPeekCore

// Checks without Screen Recording: LEXIPEEK_SNAPSHOT=<dir> writes the app's own views as PNGs (scripts/snapshots.sh).
enum Snapshot {
    static let dir = ProcessInfo.processInfo.environment["LEXIPEEK_SNAPSHOT"]
    // LEXIPEEK_SNAPSHOT_TAB=dictionary: cards open on Dictionary (to see that tab).
    static let dictionary = dir != nil && ProcessInfo.processInfo.environment["LEXIPEEK_SNAPSHOT_TAB"] == "dictionary"

    // The named shot asked for by the last URL: ?snap=<name>[&tab=dictionary][&full=1][&save=1] (☆ once it is final).
    private struct Request { var name: String, dictionary: Bool, full: Bool, save: Bool }
    private static var request: Request?
    private static var pending: DispatchWorkItem?

    // LEXIPEEK_SNAPSHOT_APPEARANCE=light|dark: the app's appearance, whatever the system's.
    // Requests come as lexipeek:// URLs in the file <dir>/request (not by `open`: an installed copy could take them).
    static func start(_ open: @escaping (URL) -> Void) {
        switch ProcessInfo.processInfo.environment["LEXIPEEK_SNAPSHOT_APPEARANCE"] {
        case "dark": NSApp.appearance = NSAppearance(named: .darkAqua)
        case "light": NSApp.appearance = NSAppearance(named: .aqua)
        default: break
        }
        guard let dir else { return }
        let file = URL(fileURLWithPath: dir).appendingPathComponent("request")
        Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { _ in
            guard let s = try? String(contentsOf: file, encoding: .utf8) else { return }
            try? FileManager.default.removeItem(at: file)
            if let url = URL(string: s.trimmingCharacters(in: .whitespacesAndNewlines)) { open(url) }
        }
    }

    // lexipeek://notes?tab=words|phrases|history&snap=<name>: the Notes window on that tab.
    static func notes(_ url: URL) -> (lexipeek.NotesTab, String)? {
        guard dir != nil, url.host == "notes" else { return nil }
        let q = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems ?? []
        func v(_ n: String) -> String? { q.first { $0.name == n }?.value }
        let tab: lexipeek.NotesTab = v("tab") == "history" ? .history : v("tab") == "phrases" ? .phrases : .words
        return (tab, v("snap") ?? "notes")
    }

    // Reads the snapshot options of a lexipeek:// URL; true for lexipeek://unread (the selection-failed card).
    static func take(_ url: URL) -> Bool {
        guard dir != nil else { return false }
        pending?.cancel()
        let q = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems ?? []
        func v(_ n: String) -> String? { q.first { $0.name == n }?.value }
        request = v("snap").map { Request(name: $0, dictionary: v("tab") == "dictionary", full: v("full") == "1", save: v("save") == "1") }
        return url.host == "unread"
    }

    // Snapshot mode: the window off every screen (it is drawn, never seen).
    static let away = NSPoint(x: -20000, y: -20000)
    static func hide(_ w: NSWindow) {
        if dir != nil { w.setFrameOrigin(away) }
    }

    static func write(_ view: NSView?, _ name: String) {
        guard let dir, let view, let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return }
        view.cacheDisplay(in: view.bounds, to: rep)
        try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: dir).appendingPathComponent(name))
    }

    static func mark(_ name: String) {
        guard let dir else { return }
        FileManager.default.createFile(atPath: URL(fileURLWithPath: dir).appendingPathComponent(name).path, contents: nil)
    }

    // After each change of the card, once it is quiet: card.png; for a named request, once the card is final,
    // <name>.png, <name>-end.png (scrolled to the end, when the body scrolls), then the marker <name>.done.
    static func card(_ view: NSView, _ model: CardModel) {
        guard dir != nil else { return }
        pending?.cancel()
        let work = DispatchWorkItem {
            write(view, "card.png")
            guard let r = request else { return }
            let final = model.unread != nil || (model.showInput && model.card == nil) || (model.card != nil && !model.state.translating)
            guard final else { return }
            if let card = model.card, lexipeek.card_view(card, model.state).tabs {
                if r.dictionary && model.state.tab != .dictionary { model.state.tab = .dictionary; return }
                if r.full && !model.state.full_entry && !card.entries.isEmpty { model.state.full_entry = true; return }
            }
            if r.save && !model.state.saved && model.card != nil { model.toggleSaved(); return }
            request = nil
            write(view, "\(r.name).png")
            writeEnd(view, "\(r.name)-end.png")
            mark("\(r.name).done")
        }
        pending = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.6, execute: work)
    }

    // The view once more with its scrolling part at the end, when it scrolls; scrolled back after.
    static func writeEnd(_ view: NSView?, _ name: String) {
        guard let view, let s = scroller(view), let doc = s.documentView else { return }
        let top = s.contentView.bounds.origin
        s.contentView.scroll(to: NSPoint(x: 0, y: doc.isFlipped ? doc.frame.height - s.contentView.bounds.height : 0))
        s.reflectScrolledClipView(s.contentView)
        write(view, name)
        s.contentView.scroll(to: top)
        s.reflectScrolledClipView(s.contentView)
    }

    private static func scroller(_ v: NSView) -> NSScrollView? {
        if let s = v as? NSScrollView, let doc = s.documentView, doc.frame.height > s.contentView.bounds.height + 1 { return s }
        for sub in v.subviews { if let s = scroller(sub) { return s } }
        return nil
    }
}
