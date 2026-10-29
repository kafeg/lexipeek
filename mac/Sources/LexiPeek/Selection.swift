import AppKit
import CxxStdlib
import LexiPeekCore
import ApplicationServices
import os

// What keeps the hotkey from working, checked every second by the app (all fine in snapshots): Accessibility off,
// no language pack installed; and the way to turn Accessibility on.
final class Access: ObservableObject {
    static let shared = Access()
    @Published private(set) var trusted = Snapshot.dir != nil || Selection.trusted
    @Published private(set) var hasPair = Snapshot.dir != nil || lexipeek.app.has_pair(std.string())
    var fine: Bool { trusted && hasPair }

    func check() {
        let t = Snapshot.dir != nil || Selection.trusted
        if t != trusted { trusted = t }
        let p = Snapshot.dir != nil || lexipeek.app.has_pair(std.string())
        if p != hasPair { hasPair = p }
    }

    // The system's prompt (it adds LexiPeek to the list) and the Accessibility pane.
    static func ask() {
        let key = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
        AXIsProcessTrustedWithOptions([key: true] as CFDictionary)
        NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
    }
}

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

    // On the main thread (its `woken` and the screens are main-thread state; AX waits a second at most).
    @MainActor static func read(app: NSRunningApplication?) async -> Result? {
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
        // A busy app answers within a second or not at all (AX's own wait is 6 s, on the main thread).
        AXUIElementSetMessagingTimeout(appEl, 1)
        guard let el = element(appEl, kAXFocusedUIElementAttribute) else { return nil }
        return selected(el)
    }

    private static func viaSystemWide() -> Result? {
        let wide = AXUIElementCreateSystemWide()
        AXUIElementSetMessagingTimeout(wide, 1)
        guard let el = element(wide, kAXFocusedUIElementAttribute) else { return nil }
        return selected(el)
    }

    // A web view's selection by its text markers first (as VoiceOver reads it: a line break between blocks, where its
    // selected text runs "money" and "Crypto" of two headings together), else the element's selected text.
    private static func selected(_ el: AXUIElement) -> Result? {
        var range: CFTypeRef?
        var str: CFTypeRef?
        if AXUIElementCopyAttributeValue(el, "AXSelectedTextMarkerRange" as CFString, &range) == .success, let range,
           AXUIElementCopyParameterizedAttributeValue(el, "AXStringForTextMarkerRange" as CFString, range, &str) == .success,
           let text = str as? String, !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return Result(text: paragraphs(el, range) ?? text, bounds: bounds(el))
        }
        var value: CFTypeRef?
        guard AXUIElementCopyAttributeValue(el, kAXSelectedTextAttribute as CFString, &value) == .success,
              let text = value as? String, !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
        return Result(text: text, bounds: bounds(el))
    }

    // A web view's selection paragraph by paragraph, joined by line breaks (Chromium and Electron give its text with none
    // between blocks: "Что сделал:• Очистка…"); nil when it is one paragraph or the app cannot say.
    static func paragraphs(_ el: AXUIElement, _ range: CFTypeRef) -> String? {
        guard CFGetTypeID(range) == AXTextMarkerRangeGetTypeID() else { return nil }
        let r = range as! AXTextMarkerRange
        let start = AXTextMarkerRangeCopyStartMarker(r), end = AXTextMarkerRangeCopyEndMarker(r)
        func param(_ name: String, _ arg: CFTypeRef) -> CFTypeRef? {
            var x: CFTypeRef?
            return AXUIElementCopyParameterizedAttributeValue(el, name as CFString, arg, &x) == .success ? x : nil
        }
        func span(_ a: AXTextMarker, _ b: AXTextMarker) -> AXTextMarkerRange { AXTextMarkerRangeCreate(nil, a, b) }
        func length(_ a: AXTextMarker, _ b: AXTextMarker) -> Int { (param("AXLengthForTextMarkerRange", span(a, b)) as? NSNumber)?.intValue ?? -1 }
        func text(_ a: AXTextMarker, _ b: AXTextMarker) -> String { (param("AXStringForTextMarkerRange", span(a, b)) as? String) ?? "" }
        let total = length(start, end)
        guard total > 0 else { return nil }
        var parts: [String] = []
        var cur = start, at = 0
        for _ in 0..<500 {
            guard let next = param("AXNextParagraphEndTextMarkerForTextMarker", cur), CFGetTypeID(next) == AXTextMarkerGetTypeID() else { return nil }
            let pend = next as! AXTextMarker
            let reach = length(start, pend)
            if reach <= at { return nil }  // no headway: the app's markers do not work this way
            if reach >= total {
                parts.append(text(cur, end))
                break
            }
            parts.append(text(cur, pend))
            cur = pend
            at = reach
        }
        let lines = parts.map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }.filter { !$0.isEmpty }
        return lines.count > 1 ? lines.joined(separator: "\n") : nil
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

}
