import AppKit
import ApplicationServices
import CxxStdlib
import LexiPeekCore

// A diagnostic: with a file `ax-probe` in the data dir, the lookup hotkey writes what each Accessibility way of reading
// the selection gives into `ax-probe.txt` (line breaks shown as ⏎) — to see how an app hands over a selection.
enum AXProbe {
    private static var dir: URL { URL(fileURLWithPath: String(lexipeek.app.data_dir())) }
    static var on: Bool { FileManager.default.fileExists(atPath: dir.appendingPathComponent("ax-probe").path) }

    static func write(app: NSRunningApplication?) {
        guard on, let app else { return }
        var out = "app: \(app.bundleIdentifier ?? "?")\n"
        let appEl = AXUIElementCreateApplication(app.processIdentifier)
        AXUIElementSetMessagingTimeout(appEl, 1)
        var v: CFTypeRef?
        guard AXUIElementCopyAttributeValue(appEl, kAXFocusedUIElementAttribute as CFString, &v) == .success, let v,
              CFGetTypeID(v) == AXUIElementGetTypeID() else { return save(out + "no focused element\n") }
        let el = v as! AXUIElement
        func attr(_ name: String) -> CFTypeRef? {
            var x: CFTypeRef?
            return AXUIElementCopyAttributeValue(el, name as CFString, &x) == .success ? x : nil
        }
        func param(_ name: String, _ arg: CFTypeRef) -> CFTypeRef? {
            var x: CFTypeRef?
            return AXUIElementCopyParameterizedAttributeValue(el, name as CFString, arg, &x) == .success ? x : nil
        }
        func show(_ x: CFTypeRef?) -> String {
            guard let x else { return "—" }
            if let s = x as? String { return s.replacingOccurrences(of: "\n", with: "⏎").replacingOccurrences(of: "\r", with: "␍") }
            if let a = x as? NSAttributedString { return "attributed: " + show(a.string as CFString) }
            return String(describing: x)
        }
        out += "role: \(show(attr("AXRole")))\n"
        var names: CFArray?
        if AXUIElementCopyParameterizedAttributeNames(el, &names) == .success, let names {
            out += "parameterized: \((names as? [String] ?? []).joined(separator: ", "))\n"
        }
        out += "AXSelectedText: \(show(attr("AXSelectedText")))\n"
        if let range = attr("AXSelectedTextMarkerRange") {
            out += "AXStringForTextMarkerRange: \(show(param("AXStringForTextMarkerRange", range)))\n"
            out += "AXAttributedStringForTextMarkerRange: \(show(param("AXAttributedStringForTextMarkerRange", range)))\n"
            out += "paragraphs: \(show(Selection.paragraphs(el, range) as CFString?))\n"
            if let start = param("AXStartTextMarkerForTextMarkerRange", range) {
                for p in ["AXParagraphTextMarkerRangeForTextMarker", "AXLineTextMarkerRangeForTextMarker", "AXSentenceTextMarkerRangeForTextMarker"] {
                    if let r = param(p, start) { out += "\(p): \(show(param("AXStringForTextMarkerRange", r)))\n" }
                }
            }
        } else {
            out += "no AXSelectedTextMarkerRange\n"
        }
        save(out)
    }

    private static func save(_ s: String) {
        try? s.write(to: dir.appendingPathComponent("ax-probe.txt"), atomically: true, encoding: .utf8)
    }
}
