import CxxStdlib
import Foundation
import LexiPeekCore
import LexiPeekKit

// The core on two serial queues: look-ups (fast) and translations (one worker); each card asks through its own CardLane.
final class CoreClient {
    fileprivate let engine: lexipeek.app.Engine
    fileprivate let lookups = DispatchQueue(label: "lexipeek.lookup", qos: .userInitiated)
    fileprivate let worker = DispatchQueue(label: "lexipeek.translate", qos: .userInitiated)
    // AI requests go out together (the open tab's first), each waits on the network.
    fileprivate let ai = DispatchQueue(label: "lexipeek.ai", qos: .userInitiated, attributes: .concurrent)

    static var dataDir: String { String(lexipeek.app.data_dir()) }

    // This Mac for the packs' requirements: memory and the int8 dot product.
    static let device: lexipeek.Device = {
        var v: Int32 = 0
        var n = MemoryLayout<Int32>.size
        let dot = sysctlbyname("hw.optional.arm.FEAT_DotProd", &v, &n, nil, 0) == 0 && v == 1
        return lexipeek.app.device(Int32(ProcessInfo.processInfo.physicalMemory / 1_048_576), dot)
    }()

    init() {
        engine = lexipeek.app.Engine.make(std.string(), Self.device)
        lookups.async { [engine] in engine.prepare() }
    }

    // Changed settings: the next cards use them.
    func configure() {
        lookups.async { [engine] in engine.configure(std.string()) }
    }

    // Changed packs: the dictionaries again, the held model freed.
    func reload() {
        lookups.async { [engine] in
            engine.reload()
            engine.prepare()
        }
    }

    // The newest looks, one per word: (text, meaning).
    func recents() -> [(String, String)] {
        engine.recents(Int32(Recents.limit)).map { (String($0.text), String($0.meaning)) }
    }

    // ☆ in the card → whether it is saved now (one line appended to the log: fast enough for the main thread).
    func toggleSaved(_ card: lexipeek.Card, translation: String) -> Bool {
        engine.toggle_saved(card, std.string(translation))
    }

    func notes(_ tab: lexipeek.NotesTab) -> lexipeek.NotesView { engine.notes(tab) }
    func removeNote(_ tab: lexipeek.NotesTab, _ text: String) { engine.remove_note(tab, std.string(text)) }
    func clearHistory() { engine.clear_history() }

    // Notes → Export… / Import… / TSV… off the main queue; done gets what to say.
    func backup(_ file: String, done: @escaping (String) -> Void) { run(done) { $0.backup(std.string(file)) } }
    func restore(_ file: String, done: @escaping (String) -> Void) { run(done) { $0.restore(std.string(file)) } }
    func exportTSV(_ tab: lexipeek.NotesTab, _ file: String, done: @escaping (String) -> Void) {
        run(done) { $0.export_tsv(tab, std.string(file)) }
    }

    private func run(_ done: @escaping (String) -> Void, _ work: @escaping (lexipeek.app.Engine) -> std.string) {
        lookups.async { [engine] in
            let s = String(work(engine))
            DispatchQueue.main.async { done(s) }
        }
    }

    // A section of the card's entry (its words' meanings: off the main queue); done on the main queue.
    func section(_ card: lexipeek.Card, entry: Int, key: String, page: Int, done: @escaping (lexipeek.SectionPage) -> Void) {
        lookups.async { [engine] in
            let p = engine.section(card, Int32(entry), std.string(key), Int32(page))
            DispatchQueue.main.async { done(p) }
        }
    }

    // Memory pressure: free the held model now.
    func memory() -> lexipeek.MemoryView { engine.memory() }

    func release() {
        DispatchQueue.global(qos: .utility).async { [engine] in engine.release() }
    }
}

// One card's requests (the pop-up's, the window's): a newer one makes its older ones no-ops and stops only its own
// translation, so the two cards never cancel each other.
final class CardLane {
    private let client: CoreClient
    private let lock = NSLock()
    private var latest = 0
    private var cards: [Int: lexipeek.Card] = [:]
    private var translating = 0  // the request whose translation is running, 0 = none

    init(_ client: CoreClient) { self.client = client }

    private func current(_ id: Int) -> Bool { lock.withLock { latest == id } }

    // Its own running translation gives way (another card's goes on).
    private func stop() {
        if lock.withLock({ translating != 0 }) { client.engine.cancel() }
    }

    // Returns the request's id; done runs on the main queue, only if no newer request came.
    func lookup(_ text: String, source: lexipeek.Source, lang: String = "", answer: String = "",
                done: @escaping (Int, lexipeek.Card) -> Void) -> Int {
        let id = lock.withLock { latest += 1; cards.removeAll(); return latest }
        stop()
        client.lookups.async { [engine = client.engine] in
            guard self.current(id) else { return }
            let card = engine.lookup_as(std.string(text), source, std.string(lang), std.string(answer))
            self.lock.withLock { self.cards[id] = card }
            DispatchQueue.main.async { if self.current(id) { done(id, card) } }
        }
        return id
    }

    // A card shown again ("←"): it becomes the current request, so its translation can run.
    func restore(_ card: lexipeek.Card) -> Int {
        lock.withLock {
            latest += 1
            cards = [latest: card]
            return latest
        }
    }

    func translate(_ id: Int, method: lexipeek.Method, done: @escaping (lexipeek.Translation) -> Void) {
        stop()  // the one running (another method, an older card) gives way
        client.worker.async { [engine = client.engine] in
            guard self.current(id), let card = self.lock.withLock({ self.cards[id] }) else { return }
            self.lock.withLock { self.translating = id }
            let t = engine.translate(card, method)
            self.lock.withLock { if self.translating == id { self.translating = 0 } }
            DispatchQueue.main.async { if self.current(id) { done(t) } }
        }
    }

    // One AI answer of the card (the saved one offline); done on the main queue, only if no newer request came.
    func ask(_ id: Int, _ card: lexipeek.Card, _ a: lexipeek.ai.Ask, key: String, done: @escaping (lexipeek.AiState) -> Void) {
        client.ai.async { [engine = client.engine] in
            guard self.current(id) else { return }
            let st = engine.ask(card, a, std.string(key))
            DispatchQueue.main.async { if self.current(id) { done(st) } }
        }
    }
}
