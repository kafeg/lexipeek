import Foundation
import LexiPeekKit

// The shell's own settings (hotkeys, per-app rules, recents); the core's answer language and method too, until M1's settings.json.
final class Prefs: ObservableObject {
    static let shared = Prefs()
    private let d = UserDefaults.standard

    @Published var translateKey: Shortcut { didSet { save(translateKey, "translateKey") } }
    @Published var inputKey: Shortcut { didSet { save(inputKey, "inputKey") } }
    @Published var offApps: [String] { didSet { d.set(offApps, forKey: "offApps") } }
    @Published var copyApps: [String] { didSet { d.set(copyApps, forKey: "copyApps") } }
    @Published var method: String { didSet { d.set(method, forKey: "method") } }
    @Published var answer: String { didSet { d.set(answer, forKey: "answer") } }
    @Published var recents: Recents { didSet { save(recents, "recents") } }

    private init() {
        translateKey = Self.load("translateKey") ?? .translate
        inputKey = Self.load("inputKey") ?? .input
        offApps = d.stringArray(forKey: "offApps") ?? []
        copyApps = d.stringArray(forKey: "copyApps") ?? []
        method = d.string(forKey: "method") ?? ""
        answer = d.string(forKey: "answer") ?? "ru"
        recents = Self.load("recents") ?? Recents()
    }

    private static func load<T: Decodable>(_ key: String) -> T? {
        UserDefaults.standard.data(forKey: key).flatMap { try? JSONDecoder().decode(T.self, from: $0) }
    }

    private func save<T: Encodable>(_ v: T, _ key: String) { d.set(try? JSONEncoder().encode(v), forKey: key) }
}
