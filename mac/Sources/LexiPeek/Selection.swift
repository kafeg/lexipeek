import AppKit
import ApplicationServices

// The selected text of the frontmost app: Accessibility first, Cmd+C only where the settings allow it.
enum Selection {
    struct Result {
        var text: String
        var bounds: CGRect?  // the selection on screen, Cocoa coordinates
    }

    static var trusted: Bool { AXIsProcessTrusted() }

    static func read(app: NSRunningApplication?, allowCopy: Bool) async -> Result? {
        guard trusted, let app else { return nil }
        if let r = viaAX(pid: app.processIdentifier) { return r }
        guard allowCopy, let t = await viaCopy() else { return nil }
        return Result(text: t, bounds: nil)
    }

    private static func viaAX(pid: pid_t) -> Result? {
        let appEl = AXUIElementCreateApplication(pid)
        var focused: CFTypeRef?
        guard AXUIElementCopyAttributeValue(appEl, kAXFocusedUIElementAttribute as CFString, &focused) == .success,
              let focused, CFGetTypeID(focused) == AXUIElementGetTypeID() else { return nil }
        let el = focused as! AXUIElement
        var value: CFTypeRef?
        guard AXUIElementCopyAttributeValue(el, kAXSelectedTextAttribute as CFString, &value) == .success,
              let text = value as? String, !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
        return Result(text: text, bounds: bounds(el))
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
    private static func viaCopy() async -> String? {
        let pb = NSPasteboard.general
        let saved = pb.pasteboardItems?.map { item in
            item.types.reduce(into: [NSPasteboard.PasteboardType: Data]()) { $0[$1] = item.data(forType: $1) }
        } ?? []
        let before = pb.changeCount
        let src = CGEventSource(stateID: .combinedSessionState)
        for down in [true, false] {
            let e = CGEvent(keyboardEventSource: src, virtualKey: 0x08, keyDown: down)  // C
            e?.flags = .maskCommand
            e?.post(tap: .cghidEventTap)
        }
        for _ in 0..<10 where pb.changeCount == before { try? await Task.sleep(nanoseconds: 30_000_000) }
        guard pb.changeCount != before else { return nil }
        let text = pb.string(forType: .string)
        pb.clearContents()
        pb.writeObjects(saved.map { types in
            let item = NSPasteboardItem()
            for (t, data) in types { item.setData(data, forType: t) }
            return item
        })
        return text
    }
}
