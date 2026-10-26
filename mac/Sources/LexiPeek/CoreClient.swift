import CxxStdlib
import Foundation
import LexiPeekCore

// What the card draws, copied out of the core's Card.
struct CardData {
    struct Method: Identifiable {
        var id: lexipeek.Method
        var name: String
        var available: Bool
        var reason: String
    }

    var text = ""
    var lang = ""
    var answer = ""
    var header = ""
    var base = ""
    var phrase = false
    var entries: [(dictionary: String, line: String, article: String)] = []
    var gloss: [(word: String, base: String, meaning: String)] = []
    var methods: [Method] = []
    var method: lexipeek.Method?
    var error = ""

    init() {}

    init(_ c: lexipeek.Card) {
        text = String(c.text)
        lang = String(c.lang)
        answer = String(c.answer)
        header = String(c.header)
        base = String(c.base)
        phrase = c.kind == .phrase
        entries = c.entries.map { (String($0.dictionary), String($0.line), String($0.article)) }
        gloss = c.gloss.map { (String($0.word), String($0.base), String($0.meaning)) }
        methods = c.methods.map { Method(id: $0.method, name: String($0.name), available: $0.available, reason: String($0.reason)) }
        method = lexipeek.mac.has_method(c) ? lexipeek.mac.method_of(c) : nil
        error = String(c.error)
    }
}

struct TranslationData {
    var text = ""
    var by = ""
    var error = ""
}

// The core on two serial queues: look-ups (fast) and translations (one worker); a newer request makes older ones no-ops.
final class CoreClient {
    private var engine: lexipeek.mac.Engine
    private let lookups = DispatchQueue(label: "lexipeek.lookup", qos: .userInitiated)
    private let worker = DispatchQueue(label: "lexipeek.translate", qos: .userInitiated)
    private let lock = NSLock()
    private var latest = 0
    private var cards: [Int: lexipeek.Card] = [:]

    static var dataDir: String { String(lexipeek.mac.data_dir()) }

    init(answer: String, method: String) {
        engine = lexipeek.mac.Engine.make(std.string(), std.string(answer), std.string(method))
    }

    // A new engine for changed settings; the old one is freed when its queued work is done.
    func reconfigure(answer: String, method: String) {
        let e = lexipeek.mac.Engine.make(std.string(), std.string(answer), std.string(method))
        lookups.async {
            let old = self.engine
            self.engine = e
            DispatchQueue.global(qos: .utility).async { old.release() }
        }
    }

    private func current(_ id: Int) -> Bool { lock.withLock { latest == id } }

    // Returns the request's id; done runs on the main queue, only if no newer request came.
    func lookup(_ text: String, source: lexipeek.Source, done: @escaping (Int, CardData) -> Void) -> Int {
        let id = lock.withLock { latest += 1; cards.removeAll(); return latest }
        lookups.async {
            guard self.current(id) else { return }
            let card = self.engine.lookup(std.string(text), source)
            self.lock.withLock { self.cards[id] = card }
            let data = CardData(card)
            DispatchQueue.main.async { if self.current(id) { done(id, data) } }
        }
        return id
    }

    func translate(_ id: Int, method: lexipeek.Method, done: @escaping (TranslationData) -> Void) {
        let engine = lookups.sync { self.engine }
        worker.async {
            guard self.current(id), let card = self.lock.withLock({ self.cards[id] }) else { return }
            let t = engine.translate(card, method)
            let data = TranslationData(text: String(t.text), by: String(t.by), error: String(t.error))
            DispatchQueue.main.async { if self.current(id) { done(data) } }
        }
    }

    // Memory pressure: free the held model now.
    func release() {
        let engine = lookups.sync { self.engine }
        DispatchQueue.global(qos: .utility).async { engine.release() }
    }
}
