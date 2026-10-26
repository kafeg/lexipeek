import XCTest
@testable import LexiPeekKit

final class ShortcutTests: XCTestCase {
    func testDefaults() {
        XCTAssertEqual(Shortcut.translate.label, "⌥D")
        XCTAssertEqual(Shortcut.input.label, "⌥A")
        XCTAssertNotEqual(Shortcut.translate, Shortcut.input)
    }

    func testLabelOrder() {
        let s = Shortcut(keyCode: 0, modifiers: Shortcut.cmd | Shortcut.shift | Shortcut.option | Shortcut.control, key: "a")
        XCTAssertEqual(s.label, "⌃⌥⇧⌘A")
    }

    func testNeedsModifier() {
        XCTAssertFalse(Shortcut(keyCode: 0, modifiers: 0, key: "A").isValid)
        XCTAssertFalse(Shortcut(keyCode: 0, modifiers: Shortcut.shift, key: "A").isValid)
        XCTAssertTrue(Shortcut(keyCode: 0, modifiers: Shortcut.control, key: "A").isValid)
    }

    func testCodable() throws {
        let data = try JSONEncoder().encode(Shortcut.translate)
        XCTAssertEqual(try JSONDecoder().decode(Shortcut.self, from: data), Shortcut.translate)
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
    func testNewestFirstNoRepeats() {
        var r = Recents()
        for t in ["a", "b", "a", "  ", "c"] { r.add(t) }
        XCTAssertEqual(r.items, ["c", "a", "b"])
    }

    func testLimit() {
        var r = Recents()
        for i in 0..<9 { r.add("w\(i)") }
        XCTAssertEqual(r.items, ["w8", "w7", "w6", "w5", "w4"])
    }

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
