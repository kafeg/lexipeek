import Foundation

// The last texts looked up, newest first, without repeats.
public struct Recents: Codable, Equatable {
    public static let limit = 5
    public private(set) var items: [String] = []

    public init(_ items: [String] = []) { self.items = Array(items.prefix(Self.limit)) }

    public mutating func add(_ text: String) {
        let t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !t.isEmpty else { return }
        items.removeAll { $0 == t }
        items.insert(t, at: 0)
        if items.count > Self.limit { items.removeLast(items.count - Self.limit) }
    }

    // A menu title: one line, at most 40 characters.
    public static func title(_ text: String) -> String {
        let line = text.split(whereSeparator: \.isNewline).joined(separator: " ")
        return line.count > 40 ? String(line.prefix(39)) + "…" : line
    }
}
