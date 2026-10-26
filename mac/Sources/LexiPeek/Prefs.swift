import CxxStdlib
import Foundation
import LexiPeekCore
import LexiPeekKit

// The core's settings.json (shared with the CLI): every change is written at once.
final class Prefs: ObservableObject {
    static let shared = Prefs()

    @Published var translateKey: Shortcut { didSet { save() } }
    @Published var inputKey: Shortcut { didSet { save() } }
    @Published var offApps: [String] { didSet { save() } }
    @Published var copyApps: [String] { didSet { save() } }
    @Published var method: String { didSet { save() } }
    @Published var answer: String { didSet { save() } }
    @Published var openAtLogin: Bool { didSet { save() } }
    @Published var selectionIcon: Bool { didSet { save() } }
    @Published var pinMoved: Bool { didSet { save() } }
    @Published var error = ""

    private init() {
        let s = lexipeek.mac.settings(std.string())
        translateKey = Self.shortcut(s.lookup) ?? .translate
        inputKey = Self.shortcut(s.input) ?? .input
        offApps = s.ignore_apps.map { String($0) }
        copyApps = s.copy_apps.map { String($0) }
        method = String(lexipeek.mac.method_of(s))
        answer = String(s.answer)
        openAtLogin = s.open_at_login
        selectionIcon = s.selection_icon
        pinMoved = s.pin_moved
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

    private static func strings(_ a: [String]) -> lexipeek.mac.Strings {
        var v = lexipeek.mac.Strings()
        for s in a { v.push_back(std.string(s)) }
        return v
    }

    // Read again first: fields this shell does not show stay as they are.
    private func save() {
        var s = lexipeek.mac.settings(std.string())
        s.lookup = Self.hotkey(translateKey)
        s.input = Self.hotkey(inputKey)
        s.ignore_apps = Self.strings(offApps)
        s.copy_apps = Self.strings(copyApps)
        lexipeek.mac.set_method(&s, std.string(method))
        s.answer = std.string(answer.isEmpty ? "ru" : answer)
        s.open_at_login = openAtLogin
        s.selection_icon = selectionIcon
        s.pin_moved = pinMoved
        error = String(lexipeek.mac.save(std.string(), s))
    }
}
