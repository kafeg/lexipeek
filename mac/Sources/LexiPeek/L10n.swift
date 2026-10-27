import CxxStdlib
import Foundation
import LexiPeekCore

// The core's texts by key (core/src/l10n): the shell has no user-visible strings of its own.
enum L {
    static func t(_ key: String) -> String { String(lexipeek.app.text(std.string(key))) }
    static func t(_ key: String, _ a: String) -> String { String(lexipeek.app.text(std.string(key), std.string(a))) }
    static func t(_ key: String, _ a: String, _ b: String) -> String {
        String(lexipeek.app.text(std.string(key), std.string(a), std.string(b)))
    }

    // The system's language ("de"): the first of the preferred ones.
    static var system: String {
        Locale.preferredLanguages.first.flatMap { Locale(identifier: $0).language.languageCode?.identifier } ?? "en"
    }

    // The setting, else the system's language when the app has it, else English: for every text from now on.
    @discardableResult
    static func apply() -> String { String(lexipeek.app.set_language(std.string(), std.string(system))) }
}
