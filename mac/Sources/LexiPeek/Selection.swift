import AppKit
import ApplicationServices
import os

// The selected text of the frontmost app, by Accessibility only (the clipboard is never touched).
enum Selection {
    struct Result {
        var text: String
        var bounds: CGRect?  // the selection on screen, Cocoa coordinates
    }

    static let log = Logger(subsystem: "io.github.kafeg.lexipeek", category: "selection")
    static var trusted: Bool { AXIsProcessTrusted() }
    // Apps asked to build their accessibility tree (Chromium, Electron): once per process.
    private static var woken = Set<pid_t>()

    static func read(app: NSRunningApplication?) async -> Result? {
        guard trusted, let app else {
            log.info("not trusted or no app")
            return nil
        }
        let pid = app.processIdentifier
        let name = app.bundleIdentifier ?? "?"
        if let r = viaAX(pid: pid) {
            log.info("\(name, privacy: .public): AX")
            return r
        }
        // Chromium and Electron build their tree only when an assistive app asks for it.
        if !woken.contains(pid) {
            woken.insert(pid)
            let el = AXUIElementCreateApplication(pid)
            for attr in ["AXManualAccessibility", "AXEnhancedUserInterface"] {
                let e = AXUIElementSetAttributeValue(el, attr as CFString, kCFBooleanTrue)
                log.info("\(name, privacy: .public): set \(attr, privacy: .public) → \(e.rawValue)")
            }
            for _ in 0..<8 {
                try? await Task.sleep(nanoseconds: 50_000_000)
                if let r = viaAX(pid: pid) {
                    log.info("\(name, privacy: .public): AX after waking")
                    return r
                }
            }
        }
        if let r = viaSystemWide() {
            log.info("\(name, privacy: .public): AX system-wide")
            return r
        }
        log.info("\(name, privacy: .public): no selection by AX")
        return nil
    }

    private static func viaAX(pid: pid_t) -> Result? {
        let appEl = AXUIElementCreateApplication(pid)
        guard let el = element(appEl, kAXFocusedUIElementAttribute) else { return nil }
        return selected(el)
    }

    private static func viaSystemWide() -> Result? {
        guard let el = element(AXUIElementCreateSystemWide(), kAXFocusedUIElementAttribute) else { return nil }
        return selected(el)
    }

    // The element's selected text, else (web views) the text of its selected text markers.
    private static func selected(_ el: AXUIElement) -> Result? {
        var value: CFTypeRef?
        if AXUIElementCopyAttributeValue(el, kAXSelectedTextAttribute as CFString, &value) == .success,
           let text = value as? String, !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return Result(text: text, bounds: bounds(el))
        }
        var range: CFTypeRef?
        guard AXUIElementCopyAttributeValue(el, "AXSelectedTextMarkerRange" as CFString, &range) == .success, let range
        else { return nil }
        var str: CFTypeRef?
        guard AXUIElementCopyParameterizedAttributeValue(el, "AXStringForTextMarkerRange" as CFString, range, &str) == .success,
              let text = str as? String, !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
        return Result(text: text, bounds: nil)
    }

    private static func element(_ from: AXUIElement, _ attr: String) -> AXUIElement? {
        var v: CFTypeRef?
        guard AXUIElementCopyAttributeValue(from, attr as CFString, &v) == .success, let v,
              CFGetTypeID(v) == AXUIElementGetTypeID() else { return nil }
        return (v as! AXUIElement)
    }

    private static func bounds(_ el: AXUIElement) -> CGRect? {
        var range: CFTypeRef?
        guard AXUIElementCopyAttributeValue(el, kAXSelectedTextRangeAttribute as CFString, &range) == .success, let range
        else { return nil }
        var rect: CFTypeRef?
        guard AXUIElementCopyParameterizedAttributeValue(el, kAXBoundsForRangeParameterizedAttribute as CFString, range,
                                                         &rect) == .success, let rect,
              CFGetTypeID(rect) == AXValueGetTypeID() else { return nil }
        var r = CGRect.zero
        guard AXValueGetValue(rect as! AXValue, .cgRect, &r), r.width > 0 || r.height > 0 else { return nil }
        // AX has the origin at the top-left of the main screen.
        let top = NSScreen.screens.first?.frame.maxY ?? 0
        return CGRect(x: r.minX, y: top - r.maxY, width: r.width, height: r.height)
    }

    // Cmd+C, then the clipboard as it was.
}
