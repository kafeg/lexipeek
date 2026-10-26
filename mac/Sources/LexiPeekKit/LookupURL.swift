import Foundation

// lexipeek://lookup?text=… and lexipeek://input (the card with its input field).
public enum LookupURL: Equatable {
    case lookup(String)
    case input

    public static let scheme = "lexipeek"

    public init?(_ url: URL) {
        guard url.scheme?.lowercased() == Self.scheme else { return nil }
        switch url.host?.lowercased() {
        case "input": self = .input
        case "lookup":
            guard let t = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems?
                .first(where: { $0.name == "text" })?.value,
                !t.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
            self = .lookup(t)
        default: return nil
        }
    }
}
