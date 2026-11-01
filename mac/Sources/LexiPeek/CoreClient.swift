import CxxStdlib
import Foundation
import LexiPeekCore
import LexiPeekKit
import SystemConfiguration

// The engine and a serial queue for settings, packs and Notes' files; the cards ask through their own CardSession.
final class CoreClient {
    let engine: lexipeek.app.Engine
    private let lookups = DispatchQueue(label: "lexipeek.lookup", qos: .userInitiated)

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

    // The sync folder's calls (it may be a slow share) and its timer.
    private let syncQueue = DispatchQueue(label: "lexipeek.sync", qos: .utility)
    private var syncTimer: DispatchSourceTimer?

    init() {
        engine = lexipeek.app.Engine.make(std.string(), Self.device)
        let name = SCDynamicStoreCopyComputerName(nil, nil) as String? ?? ""
        indexing.async { [engine] in
            engine.name_device(std.string(name))
            _ = engine.sync(true)
            DispatchQueue.main.async { NotificationCenter.default.post(name: .notesChanged, object: nil) }
            engine.prepare()
        }
        // Every 30 s the folder is looked at; read only when its files changed.
        let t = DispatchSource.makeTimerSource(queue: syncQueue)
        t.schedule(deadline: .now() + 30, repeating: 30)
        t.setEventHandler { [engine] in
            if engine.sync(false) { DispatchQueue.main.async { NotificationCenter.default.post(name: .notesChanged, object: nil) } }
        }
        t.resume()
        syncTimer = t
        // The Anki binding sends on its own; never from a snapshot run (it would reach a real Anki).
        if Snapshot.dir == nil { engine.anki_start(std.string(Keychain.key(Keychain.anki))) }
    }

    // A sync call off the main queue; done on it.
    func syncing<T>(_ work: @escaping (lexipeek.app.Engine) -> T, done: @escaping (T) -> Void) {
        syncQueue.async { [engine] in
            let r = work(engine)
            DispatchQueue.main.async { done(r) }
        }
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

    // Notes → Export… / Import… off the main queue; done gets what to say.
    func backup(_ file: String, done: @escaping (String) -> Void) { run(done) { $0.backup(std.string(file)) } }
    func restore(_ file: String, done: @escaping (String) -> Void) { run(done) { $0.restore(std.string(file)) } }

    private func run(_ done: @escaping (String) -> Void, _ work: @escaping (lexipeek.app.Engine) -> std.string) {
        lookups.async { [engine] in
            let s = String(work(engine))
            DispatchQueue.main.async { done(s) }
        }
    }

    // Memory pressure: free the held model now.
    func memory() -> lexipeek.MemoryView { engine.memory() }

    func release() {
        DispatchQueue.global(qos: .utility).async { [engine] in engine.release() }
    }

    func releaseSpeech() {
        DispatchQueue.global(qos: .utility).async { [engine] in engine.release_speech() }
    }
}
