import CxxStdlib
import Foundation
import LexiPeekCore
import LexiPeekKit

// The core on two serial queues: look-ups (fast) and translations (one worker); a newer request makes older ones no-ops.
final class CoreClient {
    private let engine: lexipeek.app.Engine
    private let lookups = DispatchQueue(label: "lexipeek.lookup", qos: .userInitiated)
    private let worker = DispatchQueue(label: "lexipeek.translate", qos: .userInitiated)
    private let lock = NSLock()
    private var latest = 0
    private var cards: [Int: lexipeek.Card] = [:]

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

    private func current(_ id: Int) -> Bool { lock.withLock { latest == id } }

    // Returns the request's id; done runs on the main queue, only if no newer request came.
    func lookup(_ text: String, source: lexipeek.Source, done: @escaping (Int, lexipeek.Card) -> Void) -> Int {
        let id = lock.withLock { latest += 1; cards.removeAll(); return latest }
        engine.cancel()
        lookups.async {
            guard self.current(id) else { return }
            let card = self.engine.lookup(std.string(text), source)
            self.lock.withLock { self.cards[id] = card }
            DispatchQueue.main.async { if self.current(id) { done(id, card) } }
        }
        return id
    }

    func translate(_ id: Int, method: lexipeek.Method, done: @escaping (lexipeek.Translation) -> Void) {
        engine.cancel()  // the one running (another method, an older card) gives way
        worker.async { [engine] in
            guard self.current(id), let card = self.lock.withLock({ self.cards[id] }) else { return }
            let t = engine.translate(card, method)
            DispatchQueue.main.async { if self.current(id) { done(t) } }
        }
    }

    // Memory pressure: free the held model now.
    func release() {
        DispatchQueue.global(qos: .utility).async { [engine] in engine.release() }
    }
}
