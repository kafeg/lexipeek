import CxxStdlib
import AppKit
import LexiPeekCore
import LexiPeekKit

// The core's settings.json (shared with the CLI): every change is written at once.
final class Prefs: ObservableObject {
    static let shared = Prefs()

    @Published var translateKey: Shortcut { didSet { save() } }
    @Published var inputKey: Shortcut { didSet { save() } }
    @Published var offApps: [String] { didSet { save() } }
    @Published var method: String { didSet { save() } }
    @Published var answer: String { didSet { save() } }
    @Published var selectionIcon: Bool { didSet { save() } }
    @Published var pinMoved: Bool { didSet { save() } }
    @Published var firstRun: Bool { didSet { save() } }
    @Published var error = ""
    // Bumped whenever settings.json changed here: the core is configured again.
    @Published private(set) var changed = 0
    private var loading = false

    private init() {
        let s = lexipeek.app.settings(std.string())
        translateKey = Self.shortcut(s.lookup) ?? .translate
        inputKey = Self.shortcut(s.input) ?? .input
        offApps = s.ignore_apps.map { String($0) }
        method = String(lexipeek.app.method_id(s))
        answer = String(s.answer)
        selectionIcon = s.selection_icon
        pinMoved = s.pin_moved
        firstRun = s.first_run
        Self.theme(String(s.theme))
    }

    // The core wrote settings.json (a settings row): read it again, without writing it back.
    func reload(_ error: String = "") {
        let s = lexipeek.app.settings(std.string())
        loading = true
        if let k = Self.shortcut(s.lookup), k != translateKey { translateKey = k }
        if let k = Self.shortcut(s.input), k != inputKey { inputKey = k }
        offApps = s.ignore_apps.map { String($0) }
        method = String(lexipeek.app.method_id(s))
        answer = String(s.answer)
        selectionIcon = s.selection_icon
        pinMoved = s.pin_moved
        firstRun = s.first_run
        Self.theme(String(s.theme))
        loading = false
        self.error = error
        changed += 1
    }

    // The theme of the card and every window: "" the system's appearance.
    private static func theme(_ t: String) {
        NSApp?.appearance = t == "light" ? NSAppearance(named: .aqua) : t == "dark" ? NSAppearance(named: .darkAqua) : nil
    }

    private static func shortcut(_ h: lexipeek.Hotkey) -> Shortcut? {
        var m: UInt32 = 0
        if h.option { m |= Shortcut.option }
        if h.command { m |= Shortcut.cmd }
        if h.control { m |= Shortcut.control }
        if h.shift { m |= Shortcut.shift }
        let s = Shortcut(key: String(h.key), modifiers: m)
        return s.isValid ? s : nil
    }

    private static func hotkey(_ s: Shortcut) -> lexipeek.Hotkey {
        var h = lexipeek.Hotkey()
        h.key = std.string(s.key)
        h.option = s.modifiers & Shortcut.option != 0
        h.command = s.modifiers & Shortcut.cmd != 0
        h.control = s.modifiers & Shortcut.control != 0
        h.shift = s.modifiers & Shortcut.shift != 0
        return h
    }

    // Read again first: fields this shell does not show stay as they are.
    private func save() {
        guard !loading else { return }
        var s = lexipeek.app.settings(std.string())
        s.lookup = Self.hotkey(translateKey)
        s.input = Self.hotkey(inputKey)
        s.ignore_apps = offApps.cxx
        lexipeek.app.set_method(&s, std.string(method))
        s.answer = std.string(answer.isEmpty ? "ru" : answer)
        s.selection_icon = selectionIcon
        s.pin_moved = pinMoved
        s.first_run = firstRun
        error = String(lexipeek.app.save(std.string(), s))
        changed += 1
    }
}
