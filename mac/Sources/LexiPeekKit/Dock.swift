// In the Dock (its icon, ⌘Tab) while one of the app's windows is shown; closing: the one going away, still shown.
public enum Dock {
    public static func regular(shown: [ObjectIdentifier], closing: ObjectIdentifier?) -> Bool {
        shown.contains { $0 != closing }
    }
}
