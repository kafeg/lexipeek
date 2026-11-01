import XCTest
@testable import LexiPeekKit

final class ShortcutTests: XCTestCase {
    func testDefaults() {
        XCTAssertEqual(Shortcut.translate.label, "⌥D")
        XCTAssertEqual(Shortcut.translate.keyCode, 2)
        XCTAssertEqual(Shortcut.input.label, "⌥A")
        XCTAssertEqual(Shortcut.input.keyCode, 0)
    }

    func testLabelOrder() {
        let s = Shortcut(key: "a", modifiers: Shortcut.cmd | Shortcut.shift | Shortcut.option | Shortcut.control)
        XCTAssertEqual(s.label, "⌃⌥⇧⌘A")
    }

    func testValid() {
        XCTAssertFalse(Shortcut(key: "A", modifiers: 0).isValid)
        XCTAssertFalse(Shortcut(key: "A", modifiers: Shortcut.shift).isValid)
        XCTAssertFalse(Shortcut(key: "-", modifiers: Shortcut.option).isValid)
        XCTAssertTrue(Shortcut(key: "7", modifiers: Shortcut.control).isValid)
    }

    func testFromKeyCode() {
        XCTAssertEqual(Shortcut(keyCode: 2, modifiers: Shortcut.option), .translate)
        XCTAssertEqual(Shortcut(keyCode: 29, modifiers: Shortcut.cmd)?.key, "0")
        XCTAssertNil(Shortcut(keyCode: 0x35, modifiers: 0))
    }

    func testCodesUnique() {
        XCTAssertEqual(Set(Shortcut.codes.values).count, Shortcut.codes.count)
        XCTAssertEqual(Shortcut.codes.count, 36)
    }
}

final class PlacementTests: XCTestCase {
    let screen = CGRect(x: 0, y: 0, width: 1000, height: 800)
    let size = CGSize(width: 300, height: 200)

    func testUnderAnchor() {
        let f = Placement.frame(size: size, anchor: CGRect(x: 100, y: 500, width: 50, height: 20), screen: screen)
        XCTAssertEqual(f, CGRect(x: 100, y: 500 - Placement.gap - 200, width: 300, height: 200))
    }

    func testAboveWhenNoRoomBelow() {
        let f = Placement.frame(size: size, anchor: CGRect(x: 100, y: 50, width: 50, height: 20), screen: screen)
        XCTAssertEqual(f.minY, 70 + Placement.gap)
    }

    func testInsideRightEdge() {
        let f = Placement.frame(size: size, anchor: CGRect(x: 950, y: 500, width: 0, height: 0), screen: screen)
        XCTAssertEqual(f.maxX, 1000)
    }

    func testSecondScreen() {
        let right = CGRect(x: 1000, y: -200, width: 1200, height: 900)
        let f = Placement.frame(size: size, anchor: CGRect(x: 1500, y: 0, width: 0, height: 0), screen: right)
        XCTAssertTrue(right.contains(f))
    }

    func testResizedKeepsTop() {
        let f = CGRect(x: 10, y: 400, width: 300, height: 200)
        let r = Placement.resized(f, height: 260, screen: screen)
        XCTAssertEqual(r.maxY, f.maxY)
        XCTAssertEqual(r.height, 260)
        XCTAssertEqual(Placement.resized(CGRect(x: 0, y: 10, width: 300, height: 100), height: 300, screen: screen).minY, 0)
    }
}

final class CardKeysTests: XCTestCase {
    private func key(_ code: UInt16, command: Bool = false, hotkeysOn: Bool = true, card: Bool = true, typing: Bool = false) -> CardKey? {
        CardKeys.local(code: code, command: command, plain: !command, hotkeysOn: hotkeysOn, card: card, typing: typing)
    }

    // Only Esc and ⌘P are taken from the other apps while a card is open: never ⌘., ⌘S or ⌘C.
    func testHotkeys() {
        XCTAssertEqual(CardKeys.hotkeys.map(\.key), [.close, .pin])
        XCTAssertEqual(CardKeys.hotkeys.map(\.code), [0x35, 0x23])
        XCTAssertEqual(CardKeys.hotkeys.map(\.modifiers), [0, Shortcut.cmd])
    }

    func testReadIsLocal() {
        XCTAssertEqual(key(CardKeys.period, command: true), .read)
        XCTAssertEqual(key(CardKeys.period, command: true, typing: true), .read)
        XCTAssertNil(key(CardKeys.period, command: true, card: false))
        XCTAssertNil(key(CardKeys.period))
    }

    func testEscAndPinLocalOnlyWithoutHotkeys() {
        XCTAssertNil(key(CardKeys.escape))
        XCTAssertNil(key(CardKeys.p, command: true))
        XCTAssertEqual(key(CardKeys.escape, hotkeysOn: false, card: false), .close)
        XCTAssertEqual(key(CardKeys.p, command: true, hotkeysOn: false, card: false), .pin)
    }

    func testCardKeys() {
        XCTAssertEqual(key(CardKeys.s, command: true), .save)
        XCTAssertEqual(key(CardKeys.left), .previousTab)
        XCTAssertEqual(key(CardKeys.right), .nextTab)
        XCTAssertEqual(key(CardKeys.delete), .back)
        XCTAssertEqual(key(CardKeys.bracket, command: true), .back)
        XCTAssertNil(key(CardKeys.bracket))
        XCTAssertNil(key(CardKeys.left, typing: true))
        XCTAssertNil(key(8, command: true))  // ⌘C goes on to the field or the app
    }
}

final class DockTests: XCTestCase {
    func testRegularWhileAWindowIsShown() {
        let a = ObjectIdentifier(NSObject.self), b = ObjectIdentifier(NSString.self)
        XCTAssertFalse(Dock.regular(shown: [], closing: nil))
        XCTAssertTrue(Dock.regular(shown: [a], closing: nil))
        XCTAssertFalse(Dock.regular(shown: [a], closing: a))
        XCTAssertTrue(Dock.regular(shown: [a, b], closing: a))
    }
}
