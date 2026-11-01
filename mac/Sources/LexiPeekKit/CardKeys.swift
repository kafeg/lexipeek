// What a key does in the card.
public enum CardKey: Equatable {
    case close, pin, save, read, previousTab, nextTab, back
}

// Esc and ⌘P are hotkeys while the card is open and not pinned; the rest only while it has the focus (not every app's ⌘S, ⌘.).
public enum CardKeys {
    public static let escape: UInt16 = 0x35, p: UInt16 = 0x23, s: UInt16 = 0x01, period: UInt16 = 0x2F,
                      left: UInt16 = 0x7B, right: UInt16 = 0x7C, delete: UInt16 = 0x33, bracket: UInt16 = 0x21

    public static let hotkeys: [(key: CardKey, code: UInt32, modifiers: UInt32)] = [
        (.close, UInt32(escape), 0), (.pin, UInt32(p), Shortcut.cmd),
    ]

    // command: ⌘ alone, plain: no modifier; card: not the bare input field; typing: a text field has the focus.
    public static func local(code: UInt16, command: Bool, plain: Bool, hotkeysOn: Bool, card: Bool, typing: Bool) -> CardKey? {
        if code == escape && plain && !hotkeysOn { return .close }
        if code == p && command && !hotkeysOn { return .pin }
        guard card else { return nil }
        if code == s && command { return .save }
        if code == period && command { return .read }
        guard !typing else { return nil }
        switch code {
        case left: return .previousTab
        case right: return .nextTab
        case delete: return .back
        case bracket where command: return .back
        default: return nil
        }
    }
}
