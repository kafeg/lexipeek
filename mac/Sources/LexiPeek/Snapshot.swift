import AppKit
import LexiPeekCore

// Checks without Screen Recording: LEXIPEEK_SNAPSHOT=<dir> writes the app's own views as PNGs (scripts/snapshots.sh).
enum Snapshot {
    static let dir = ProcessInfo.processInfo.environment["LEXIPEEK_SNAPSHOT"]

    // The named shot asked for by the last URL: ?snap=<name>[&tab=dictionary|analysis|context][&full=1][&save=1][&open=<word>]
    // [&section=<key>][&page=1][&entry=<n>][&back=1][&answer=<code>][&method=fast|ai][&whole=1] — save: ☆ once it is
    // final; open: that word clicked in the card; section: the first entry's section opened, page: its next page;
    // entry: that dictionary's line clicked; back: ← once; answer: the badge's other answer language; tab=analysis|context:
    // that AI tab clicked; method=ai: AI translation picked; whole: a cut title opened.
    private struct Request {
        var name: String, tab: lexipeek.CardTab?, full: Bool, save: Bool, open: String?, section: String?, method: lexipeek.Method?, ai: Bool, whole: Bool, entry: Int?,
            back: Bool, page: Bool, answer: String?
    }
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

    // lexipeek://window?page=lookup|words|phrases|history[&look=<text>]&snap=<name>: the LexiPeek window on that page
    // (look: that text looked up there).
    static func window(_ url: URL) -> (page: MainPage, name: String, look: String?)? {
        guard dir != nil, url.host == "window" else { return nil }
        let q = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems ?? []
        func v(_ n: String) -> String? { q.first { $0.name == n }?.value }
        let pages: [String: MainPage] = ["lookup": .lookup, "words": .notes(.words), "phrases": .notes(.phrases), "history": .notes(.history), "help": .help]
        return (pages[v("page") ?? ""] ?? .lookup, v("snap") ?? "window", v("look"))
    }

    // Reads the snapshot options of a lexipeek:// URL; true for lexipeek://unread (the selection-failed card).
    static func take(_ url: URL) -> Bool {
        guard dir != nil else { return false }
        pending?.cancel()
        let q = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems ?? []
        func v(_ n: String) -> String? { q.first { $0.name == n }?.value }
        let tabs: [String: lexipeek.CardTab] = ["dictionary": .dictionary, "translate": .translate, "analysis": .analysis, "context": .context]
        request = v("snap").map {
            Request(name: $0, tab: v("tab").flatMap { tabs[$0] }, full: v("full") == "1", save: v("save") == "1", open: v("open"),
                    section: v("section"),
                    method: v("method").flatMap { m in [lexipeek.Method.fast, .direct, .llm].first { String(lexipeek.app.method_id($0)) == m } },
                    ai: v("method") == "ai", whole: v("whole") == "1", entry: v("entry").flatMap { Int($0) },
                    back: v("back") == "1", page: v("page") == "1", answer: v("answer"))
        }
        return url.host == "unread"
    }

    // Snapshot mode: the window off every screen (it is drawn, never seen).
    static let away = NSPoint(x: -20000, y: -20000)
    static func hide(_ w: NSWindow) {
        if dir != nil { w.setFrameOrigin(away) }
    }

    // &method=fast: the card translates with it, whatever the settings say; &method=ai: by AI.
    static var method: lexipeek.Method? { request?.method }
    static var ai: Bool { request?.ai ?? false }
    // &whole=1: a cut title is shown whole (as clicked).
    static var whole: Bool { request?.whole ?? false }

    // lexipeek://firstrun?snap=<name>&step=<step>: the first-run window in that step (its name); nil for other URLs.
    static func firstRun(_ url: URL) -> String? {
        guard dir != nil, url.host == "firstrun" else { return nil }
        _ = take(url)
        let q = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems ?? []
        return q.first { $0.name == "step" }?.value ?? "access"
    }

    // The step's state as the shell would hold it (Accessibility, a download half-way), drawn from the data dir's packs.
    static func firstRun(_ w: NSWindow?, _ model: FirstRunModel, _ step: String) {
        guard let r = request else { return }
        request = nil
        var s = lexipeek.FirstRunState()
        s.system = "ru"
        s.hotkey = model.state.hotkey
        s.input = model.state.input
        s.trusted = step != "access" && step != "done-later"
        s.access_later = step == "done-later"
        s.fetching = step == "list"
        if step == "list-error" { s.list_error = "Could not resolve host: github.com" }
        if step == "download" { s.downloading = true; s.done = 27_000_000; s.total = 68_000_000 }
        if step == "error" { s.error = "Could not connect to server" }
        model.state = s
        if let w { hide(w) }
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
            write(w?.contentView?.superview, "\(r.name).png")
            mark("\(r.name).done")
        }
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

    // For a named request, once the card is final:
    // <name>.png, <name>-end.png (scrolled to the end, when the body scrolls), then the marker <name>.done.
    static func card(_ view: NSView, _ model: CardModel) {
        guard dir != nil else { return }
        pending?.cancel()
        let work = DispatchWorkItem {
            guard let r = request else { return }
            let asking = [lexipeek.ai.Ask.quick, .analysis, .context].contains { lexipeek.card_answer(model.state, $0).asking }
            let final = model.unread != nil || (model.showInput && model.card == nil) || (model.card != nil && !model.state.translating && !asking)
            guard final else { return }
            if let card = model.card, lexipeek.card_view(card, model.state).tabs {
                if let t = r.tab, model.state.tab != t { model.select(t); return }
                if let w = r.open { request?.open = nil; model.open(w); return }
                if r.full && !model.state.full_entry && !card.entries.isEmpty { model.state.full_entry = true; return }
                if let k = r.section { request?.section = nil; model.openSection(0, k); return }
                if let e = r.entry { request?.entry = nil; model.openEntry(e); return }
                if r.page && model.state.section.page == 0 && !String(model.state.section.key).isEmpty { request?.page = false; model.turn(1); return }
                if r.back { request?.back = false; model.back(); return }
                if let a = r.answer { request?.answer = nil; model.relookup(answer: a); return }
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
