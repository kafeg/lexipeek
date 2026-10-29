import CxxStdlib
import Foundation
import LexiPeekCore
import LexiPeekKit

// The core on serial queues for look-ups, translations and indexing and a concurrent one for AI; each card asks through
// its CardLane.
final class CoreClient {
    let engine: lexipeek.app.Engine
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
        // Apple Silicon's int8 dot product, by the name the packs' list uses (Linux's /proc/cpuinfo flag).
        return lexipeek.app.device(Int32(ProcessInfo.processInfo.physicalMemory / 1_048_576), (dot ? ["asimddp"] : []).cxx, std.string("arm64"))
    }()

    // The dictionaries' indexes, built in the background (one at a time: look-ups go on meanwhile).
    private let indexing = DispatchQueue(label: "lexipeek.prepare", qos: .utility)

    init() {
        engine = lexipeek.app.Engine.make(std.string(), Self.device)
        prepare()
    }

    private func prepare() { indexing.async { [engine] in engine.prepare() } }

    // Changed settings: the next cards use them (own dictionaries may have come: indexed in the background).
    func configure() {
        lookups.async { [engine] in
            engine.configure(std.string())
            self.prepare()  // after the change, on the library it made
        }
    }

    // Changed packs: the dictionaries again, the held model freed.
    func reload() {
        lookups.async { [engine] in
            engine.reload()
            self.prepare()
        }
    }

    // The newest looks, one per word: (text, meaning).
    func recents() -> [(String, String)] {
        engine.recents(lexipeek.app.recents_shown()).map { (String($0.text), String($0.meaning)) }
    }

    // ☆ in the card → whether it is saved now (one line appended to the log: fast enough for the main thread).
    func toggleSaved(_ card: lexipeek.Card, translation: String) -> Bool {
        engine.toggle_saved(card, std.string(translation))
    }

    func notes(_ tab: lexipeek.NotesTab, page: Int = 0) -> lexipeek.NotesView { engine.notes(tab, Int32(page)) }
    func isSaved(_ card: lexipeek.Card) -> Bool { engine.saved(card) }
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

    // Memory pressure: free the held model now.
    func memory() -> lexipeek.MemoryView { engine.memory() }

    func release() {
        DispatchQueue.global(qos: .utility).async { [engine] in engine.release() }
    }
}
