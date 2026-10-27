import Foundation

public enum Recents {
    public static let limit = 5

    // A menu title: one line, at most 40 characters.
    public static func title(_ text: String) -> String {
        let line = text.split(whereSeparator: \.isNewline).joined(separator: " ")
        guard line.count > 40 else { return line }
        return String(line.prefix(39)).trimmingCharacters(in: .whitespaces) + "…"
    }
}
