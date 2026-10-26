import Foundation

// A global hotkey: Carbon key code and modifiers, with the label shown in menus and settings.
public struct Shortcut: Codable, Equatable {
    public static let cmd: UInt32 = 0x0100, shift: UInt32 = 0x0200, option: UInt32 = 0x0800, control: UInt32 = 0x1000

    public var keyCode: UInt32
    public var modifiers: UInt32
    public var key: String

    public init(keyCode: UInt32, modifiers: UInt32, key: String) {
        self.keyCode = keyCode
        self.modifiers = modifiers
        self.key = key.uppercased()
    }

    public static let translate = Shortcut(keyCode: 0x02, modifiers: option, key: "D")
    public static let input = Shortcut(keyCode: 0x00, modifiers: option, key: "A")

    // A global hotkey needs ⌘, ⌥ or ⌃: Shift alone would eat typing.
    public var isValid: Bool { modifiers & (Self.cmd | Self.option | Self.control) != 0 && !key.isEmpty }

    public var label: String {
        var s = ""
        if modifiers & Self.control != 0 { s += "⌃" }
        if modifiers & Self.option != 0 { s += "⌥" }
        if modifiers & Self.shift != 0 { s += "⇧" }
        if modifiers & Self.cmd != 0 { s += "⌘" }
        return s + key
    }
}
