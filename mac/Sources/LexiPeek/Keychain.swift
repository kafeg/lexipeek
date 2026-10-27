import CxxStdlib
import Foundation
import LexiPeekCore
import Security

// The AI providers' API keys in the login Keychain, one item per provider; given to the core per request, never stored elsewhere.
enum Keychain {
    private static let service = "dev.kafeg.lexipeek.ai"

    private static func query(_ provider: String) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service, kSecAttrAccount as String: provider]
    }

    // Snapshot runs never touch the Keychain (a prompt could show on the owner's screen).
    static func key(_ provider: String) -> String {
        guard !provider.isEmpty, Snapshot.dir == nil else { return "" }
        var q = query(provider)
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: AnyObject?
        guard SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess, let data = out as? Data else { return "" }
        return String(data: data, encoding: .utf8) ?? ""
    }

    // Whether one is kept: its attributes only (the key itself is not read).
    static func has(_ provider: String) -> Bool {
        guard !provider.isEmpty, Snapshot.dir == nil else { return false }
        var q = query(provider)
        q[kSecReturnAttributes as String] = true
        return SecItemCopyMatching(q as CFDictionary, nil) == errSecSuccess
    }

    // An empty key removes the item.
    static func set(_ provider: String, _ key: String) {
        guard !provider.isEmpty, Snapshot.dir == nil else { return }
        SecItemDelete(query(provider) as CFDictionary)
        let k = key.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !k.isEmpty else { return }
        var q = query(provider)
        q[kSecValueData as String] = Data(k.utf8)
        q[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlock
        SecItemAdd(q as CFDictionary, nil)
    }

    // The current provider's key for a request; snapshots run without one (their answers come from files).
    static var current: String {
        let p = String(lexipeek.app.ai_provider(std.string()))
        let k = key(p)
        return k.isEmpty && Snapshot.dir != nil ? "snapshot" : k
    }
}
