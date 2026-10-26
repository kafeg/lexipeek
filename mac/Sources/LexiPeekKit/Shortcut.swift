import Foundation

// A global hotkey as settings.json keeps it (a letter or a digit and modifiers), with its Carbon key code and label.
public struct Shortcut: Equatable {
    public static let cmd: UInt32 = 0x0100, shift: UInt32 = 0x0200, option: UInt32 = 0x0800, control: UInt32 = 0x1000

    // Key codes of the ANSI layout's letters and digits (Carbon's kVK_ANSI_*).
    static let codes: [String: UInt32] = [
        "A": 0, "S": 1, "D": 2, "F": 3, "H": 4, "G": 5, "Z": 6, "X": 7, "C": 8, "V": 9, "B": 11, "Q": 12, "W": 13,
        "E": 14, "R": 15, "Y": 16, "T": 17, "1": 18, "2": 19, "3": 20, "4": 21, "6": 22, "5": 23, "9": 25, "7": 26,
        "8": 28, "0": 29, "O": 31, "U": 32, "I": 34, "P": 35, "L": 37, "J": 38, "K": 40, "N": 45, "M": 46,
    ]

    public var key: String
    public var modifiers: UInt32

    public init(key: String, modifiers: UInt32) {
        self.key = key.uppercased()
        self.modifiers = modifiers
    }

    // The key of a pressed key code; nil for anything but a letter or a digit.
    public init?(keyCode: UInt16, modifiers: UInt32) {
        guard let k = Self.codes.first(where: { $0.value == UInt32(keyCode) })?.key else { return nil }
        self.init(key: k, modifiers: modifiers)
    }

    public static let translate = Shortcut(key: "D", modifiers: option)
    public static let input = Shortcut(key: "A", modifiers: option)

    public var keyCode: UInt32? { Self.codes[key] }

    // A global hotkey needs ⌘, ⌥ or ⌃: Shift alone would eat typing.
    public var isValid: Bool { keyCode != nil && modifiers & (Self.cmd | Self.option | Self.control) != 0 }

    public var label: String {
        var s = ""
        if modifiers & Self.control != 0 { s += "⌃" }
        if modifiers & Self.option != 0 { s += "⌥" }
        if modifiers & Self.shift != 0 { s += "⇧" }
        if modifiers & Self.cmd != 0 { s += "⌘" }
        return s + key
    }
}
