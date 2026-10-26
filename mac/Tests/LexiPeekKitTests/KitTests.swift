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

final class RecentsTests: XCTestCase {
    func testTitle() {
        XCTAssertEqual(Recents.title("one\ntwo"), "one two")
        XCTAssertEqual(Recents.title(String(repeating: "x", count: 50)).count, 40)
    }
}

final class LookupURLTests: XCTestCase {
    func testLookup() {
        XCTAssertEqual(LookupURL(URL(string: "lexipeek://lookup?text=Das%20Haus%20ist%20alt")!), .lookup("Das Haus ist alt"))
        XCTAssertEqual(LookupURL(URL(string: "lexipeek://lookup?text=Tr%C3%A4umen")!), .lookup("Träumen"))
        XCTAssertEqual(LookupURL(URL(string: "lexipeek://input")!), .input)
    }

    func testRejects() {
        XCTAssertNil(LookupURL(URL(string: "lexipeek://lookup?text=%20")!))
        XCTAssertNil(LookupURL(URL(string: "lexipeek://other?text=a")!))
        XCTAssertNil(LookupURL(URL(string: "https://lookup?text=a")!))
    }
}
