import CxxStdlib
import Foundation
import LexiPeekCore
import LexiPeekKit

// What the card draws, copied out of the core's Card.
struct CardData {
    struct Method: Identifiable {
        var id: lexipeek.Method
        var name: String
        var available: Bool
        var reason: String
    }

    enum Kind { case word, phrase, passage }

    var text = ""
    var lang = ""
    var answer = ""
    var other = ""
    var langName = ""
    var answerName = ""
    var kind = Kind.word
    var header = ""
    var base = ""
    var phrase = false
    var entries: [(dictionary: String, line: String, article: String)] = []
    var gloss: [(word: String, base: String, meaning: String)] = []
    var separable: [(word: String, base: String, meaning: String)] = []
    var looks = 0
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
        phrase = c.kind != .word
        kind = c.kind == .word ? .word : c.kind == .phrase ? .phrase : .passage
        other = String(c.other_name)
        langName = String(c.lang_name)
        answerName = String(c.answer_name)
        entries = c.entries.map { (String($0.dictionary), String($0.line), String($0.article)) }
        gloss = c.gloss.map { (String($0.word), String($0.base), String($0.meaning)) }
        separable = c.separable.map { (String($0.word), String($0.base), String($0.meaning)) }
        looks = Int(c.looks)
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
    private var engine: lexipeek.mac.Engine  // under lock
    private let lookups = DispatchQueue(label: "lexipeek.lookup", qos: .userInitiated)
    private let worker = DispatchQueue(label: "lexipeek.translate", qos: .userInitiated)
    private let lock = NSLock()
    private var latest = 0
    private var cards: [Int: lexipeek.Card] = [:]

    static var dataDir: String { String(lexipeek.mac.data_dir()) }

    // This Mac for the packs' requirements: memory and the int8 dot product.
    static let device: lexipeek.Device = {
        var v: Int32 = 0
        var n = MemoryLayout<Int32>.size
        let dot = sysctlbyname("hw.optional.arm.FEAT_DotProd", &v, &n, nil, 0) == 0 && v == 1
        return lexipeek.mac.device(Int32(ProcessInfo.processInfo.physicalMemory / 1_048_576), dot)
    }()

    init() {
        engine = lexipeek.mac.Engine.make(std.string(), Self.device)
        let e = engine
        lookups.async { e.prepare() }
    }

    // A new engine for changed settings or packs; the old one's model is freed.
    func reconfigure() {
        lookups.async {
            let e = lexipeek.mac.Engine.make(std.string(), Self.device)
            e.prepare()
            let old = self.lock.withLock { let o = self.engine; self.engine = e; return o }
            DispatchQueue.global(qos: .utility).async { old.release() }
        }
    }

    // The newest looks, one per word: (text, meaning).
    func recents() -> [(String, String)] {
        let engine = lock.withLock { self.engine }
        return engine.recents(Int32(Recents.limit)).map { (String($0.text), String($0.meaning)) }
    }

    private func current(_ id: Int) -> Bool { lock.withLock { latest == id } }

    // Returns the request's id; done runs on the main queue, only if no newer request came.
    func lookup(_ text: String, source: lexipeek.Source, done: @escaping (Int, CardData) -> Void) -> Int {
        let id = lock.withLock { latest += 1; cards.removeAll(); return latest }
        lock.withLock { self.engine }.cancel()
        lookups.async {
            guard self.current(id) else { return }
            let card = self.lock.withLock { self.engine }.lookup(std.string(text), source)
            self.lock.withLock { self.cards[id] = card }
            let data = CardData(card)
            DispatchQueue.main.async { if self.current(id) { done(id, data) } }
        }
        return id
    }

    func translate(_ id: Int, method: lexipeek.Method, done: @escaping (TranslationData) -> Void) {
        let engine = lock.withLock { self.engine }
        engine.cancel()  // the one running (another method, an older card) gives way
        worker.async {
            guard self.current(id), let card = self.lock.withLock({ self.cards[id] }) else { return }
            let t = engine.translate(card, method)
            let data = TranslationData(text: String(t.text), by: String(t.by), error: String(t.error))
            DispatchQueue.main.async { if self.current(id) { done(data) } }
        }
    }

    // Memory pressure: free the held model now.
    func release() {
        let engine = lock.withLock { self.engine }
        DispatchQueue.global(qos: .utility).async { engine.release() }
    }
}
