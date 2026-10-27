import AppKit

// The icon at a new selection (an option): after a drag or a double / triple click in another app, the selection is
// read by Accessibility (never Cmd+C) and a small icon appears by the cursor; a click opens the card.
final class SelectionIcon {
    private let panel: NSPanel
    private var monitors: [Any] = []
    private var downAt = NSPoint.zero
    private var hide: DispatchWorkItem?
    private var found: Selection.Result?
    private var reading: Task<Void, Never>?
    var open: ((Selection.Result) -> Void)?

    init() {
        panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 28, height: 28),
                        styleMask: [.nonactivatingPanel, .borderless], backing: .buffered, defer: true)
        panel.level = .popUpMenu
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = true
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .transient]
        let effect = NSVisualEffectView(frame: panel.contentRect(forFrameRect: panel.frame))
        effect.material = .popover
        effect.state = .active
        effect.maskImage = NSImage(size: effect.frame.size, flipped: false) { r in
            NSColor.black.setFill()
            NSBezierPath(roundedRect: r, xRadius: 7, yRadius: 7).fill()
            return true
        }
        let button = NSButton(image: NSImage(named: "MenuBarIcon")!,
                              target: self, action: #selector(clicked))
        button.isBordered = false
        button.frame = effect.bounds
        button.toolTip = L.t("lookup_service")
        effect.addSubview(button)
        panel.contentView = effect
    }

    func setEnabled(_ on: Bool) {
        monitors.forEach(NSEvent.removeMonitor)
        monitors = []
        close()
        guard on else { return }
        if let m = NSEvent.addGlobalMonitorForEvents(matching: .leftMouseDown, handler: { [weak self] _ in
            self?.downAt = NSEvent.mouseLocation
            self?.close()
        }) { monitors.append(m) }
        // A press on our own windows (the card) does not reach the global monitor: noted here.
        if let m = NSEvent.addLocalMonitorForEvents(matching: .leftMouseDown, handler: { [weak self] e in
            self?.downAt = NSEvent.mouseLocation
            return e
        }) { monitors.append(m) }
        if let m = NSEvent.addGlobalMonitorForEvents(matching: .leftMouseUp, handler: { [weak self] e in self?.mouseUp(e) }) {
            monitors.append(m)
        }
        if let m = NSEvent.addGlobalMonitorForEvents(matching: [.keyDown, .scrollWheel], handler: { [weak self] _ in
            self?.close()
        }) { monitors.append(m) }
    }

    private func mouseUp(_ e: NSEvent) {
        let at = NSEvent.mouseLocation
        // A drag that started on our own window (moving the card) is not a selection in another app.
        if NSApp.windows.contains(where: { $0.isVisible && $0.frame.contains(downAt) }) { return }
        let dragged = hypot(at.x - downAt.x, at.y - downAt.y) > 6
        guard dragged || e.clickCount >= 2 else { return }
        let app = NSWorkspace.shared.frontmostApplication
        guard app?.bundleIdentifier != Bundle.main.bundleIdentifier,
              !Prefs.shared.offApps.contains(app?.bundleIdentifier ?? "") else { return }
        reading?.cancel()
        reading = Task { @MainActor in
            try? await Task.sleep(nanoseconds: 60_000_000)  // the app updates its selection after the mouse-up
            guard !Task.isCancelled, let s = await Selection.read(app: app) else { return }
            guard !Task.isCancelled else { return }
            self.show(s, at: at)
        }
    }

    private func show(_ s: Selection.Result, at: NSPoint) {
        found = s
        let screen = NSScreen.screens.first { $0.frame.contains(at) }?.visibleFrame ?? .zero
        var origin = NSPoint(x: at.x + 8, y: at.y + 10)
        origin.x = min(origin.x, screen.maxX - 28)
        origin.y = min(origin.y, screen.maxY - 28)
        panel.setFrameOrigin(origin)
        panel.orderFrontRegardless()
        hide?.cancel()
        let h = DispatchWorkItem { [weak self] in self?.close() }
        hide = h
        DispatchQueue.main.asyncAfter(deadline: .now() + 4, execute: h)
    }

    func close() {
        reading?.cancel()
        hide?.cancel()
        panel.orderOut(nil)
    }

    @objc private func clicked() {
        guard let s = found else { return }
        close()
        open?(s)
    }
}
