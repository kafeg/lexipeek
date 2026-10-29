import AppKit
import Combine
import LexiPeekCore
import LexiPeekKit
import SwiftUI

// A non-activating panel: the frontmost app keeps focus; the panel becomes key only for typing.
final class CardPanel: NSPanel {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { false }
}

// The card's content: a drag on its empty background moves the card (buttons and text keep their clicks).
final class CardHostingView<Content: View>: NSHostingView<Content> {
    override var mouseDownCanMoveWindow: Bool { true }
}

// Shows the card at the selection or the cursor, keeps its top edge and its height while it is open.
final class CardController {
    private static let escapeKey: UInt32 = 100, pinKey: UInt32 = 101, readKey: UInt32 = 102

    let model: CardModel
    private let panel: CardPanel
    private let effect = NSVisualEffectView()
    private var hosting: CardHostingView<CardView>?
    private var screen = CGRect.zero
    private var monitors: [Any] = []
    private var pinWatch: AnyCancellable?
    private var keysOn = false
    private var changes: AnyCancellable?
    private var moved: Any?
    private var downAt: NSPoint?

    var isVisible: Bool { panel.isVisible }
    private(set) var shown = ""  // the text the open card was asked for

    init(model: CardModel) {
        self.model = model
        panel = CardPanel(contentRect: NSRect(x: 0, y: 0, width: 400, height: 120),
                          styleMask: [.nonactivatingPanel, .borderless], backing: .buffered, defer: true)
        panel.level = .popUpMenu
        panel.isFloatingPanel = true
        panel.hidesOnDeactivate = false
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = true
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .transient]
        panel.isMovableByWindowBackground = true
        // Dragged somewhere (further than a click's jitter): with the option on, the card stays, as pinned.
        moved = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .leftMouseUp]) { [weak self] e in
            guard let self, e.window === self.panel else { return e }
            if e.type == .leftMouseDown {
                self.downAt = self.panel.frame.origin
            } else if let d = self.downAt {
                let o = self.panel.frame.origin
                if hypot(o.x - d.x, o.y - d.y) > 4 && Prefs.shared.pinMoved { self.model.pinned = true }
                self.downAt = nil
            }
            return e
        }
        effect.material = .popover
        effect.blendingMode = .behindWindow
        effect.state = .active
        // A behind-window material ignores the layer's corner radius: its own mask rounds it.
        effect.maskImage = Self.rounded(12)
        panel.contentView = effect
        changes = model.objectWillChange.sink { [weak self] in DispatchQueue.main.async { self?.fit() } }
    }

    private static func rounded(_ r: CGFloat) -> NSImage {
        let d = r * 2 + 1
        let image = NSImage(size: NSSize(width: d, height: d), flipped: false) { rect in
            NSColor.black.setFill()
            NSBezierPath(roundedRect: rect, xRadius: r, yRadius: r).fill()
            return true
        }
        image.capInsets = NSEdgeInsets(top: r, left: r, bottom: r, right: r)
        image.resizingMode = .stretch
        return image
    }

    // text nil: the input field (focused at once when typing was asked for).
    func show(text: String?, source: lexipeek.Source, bounds: CGRect?, focus: Bool) {
        close()
        model.pinned = false
        shown = text ?? ""
        model.reset(input: text == nil)
        let view = CardView(model: model, focusInput: focus)
        hosting?.removeFromSuperview()
        let h = CardHostingView(rootView: view)
        h.translatesAutoresizingMaskIntoConstraints = true
        effect.addSubview(h)
        hosting = h
        let mouse = NSEvent.mouseLocation
        let anchor = bounds ?? CGRect(origin: mouse, size: .zero)
        let scr = NSScreen.screens.first { $0.frame.contains(anchor.origin) } ?? NSScreen.main
        screen = scr?.visibleFrame ?? .zero
        let size = h.fittingSize
        panel.setFrame(Placement.frame(size: size, anchor: anchor, screen: screen), display: false)
        layout()
        if let text { model.lookup(text, source: source) }
        Snapshot.hide(panel)
        if focus && Snapshot.dir == nil { panel.makeKeyAndOrderFront(nil) } else { panel.orderFrontRegardless() }
        // Snapshot mode: no Esc, ⌘P or outside clicks (the person keeps using the Mac meanwhile).
        if Snapshot.dir == nil { watch() }
    }

    func close() {
        guard panel.isVisible else { return }
        panel.orderOut(nil)
        model.pinned = false
        model.cancel()
        pinWatch = nil
        hotkeys(false)
        monitors.forEach(NSEvent.removeMonitor)
        monitors = []
    }

    // Esc and ⌘P as hotkeys while the card is open and not pinned: they work while another app has the focus; a pinned
    // card leaves them to the apps (it stays while the person works elsewhere).
    private func watch() {
        hotkeys(true)
        pinWatch = model.$pinned.sink { [weak self] pinned in self?.hotkeys(!pinned) }
        if let m = NSEvent.addGlobalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown], handler: { [weak self] _ in
            guard let self, !self.model.pinned else { return }
            self.close()
        }) { monitors.append(m) }
        if let m = NSEvent.addLocalMonitorForEvents(matching: .keyDown, handler: { [weak self] e in self?.key(e) ?? e }) {
            monitors.append(m)
        }
    }

    private func hotkeys(_ on: Bool) {
        guard on != keysOn else { return }
        keysOn = on
        if on {
            Hotkeys.shared.register(Self.escapeKey, code: 0x35, modifiers: 0) { [weak self] in self?.close() }
            Hotkeys.shared.register(Self.pinKey, code: 0x23, modifiers: Shortcut.cmd) { [weak self] in self?.model.pinned.toggle() }
            Hotkeys.shared.register(Self.readKey, code: 0x2F, modifiers: Shortcut.cmd) { [weak self] in self?.model.read() }
        } else {
            Hotkeys.shared.unregister(Self.escapeKey)
            Hotkeys.shared.unregister(Self.pinKey)
            Hotkeys.shared.unregister(Self.readKey)
        }
    }

    // In the card (when it has the focus): Esc, ⌘P, ←/→ tabs, ⌘S ☆, Delete or ⌘[ back; the input field keeps its own keys.
    private func key(_ e: NSEvent) -> NSEvent? {
        guard e.window === panel else { return e }
        let mods = e.modifierFlags.intersection(.deviceIndependentFlagsMask)
        if e.keyCode == 0x35 && mods.isEmpty && !keysOn {
            close()
            return nil
        }
        if e.keyCode == 0x23 && mods == .command && !keysOn {
            model.pinned.toggle()
            return nil
        }
        guard model.card != nil else { return e }
        if e.keyCode == 0x01 && e.modifierFlags.intersection(.deviceIndependentFlagsMask) == .command {
            model.toggleSaved()
            return nil
        }
        if e.keyCode == 0x2F && mods == .command && !keysOn {
            model.read()
            return nil
        }
        guard !(panel.firstResponder is NSTextView) else { return e }
        switch e.keyCode {
        case 0x7B: model.switchTab(-1)
        case 0x7C: model.switchTab(1)
        case 0x33: model.back()
        case 0x21 where e.modifierFlags.contains(.command): model.back()
        default: return e
        }
        return nil
    }

    private func fit() {
        guard panel.isVisible, let h = hosting else { return }
        let fitting = h.fittingSize
        let height = min(model.loading ? max(fitting.height, panel.frame.height) : fitting.height, screen.height)
        if height != panel.frame.height || fitting.width != panel.frame.width {
            var f = Placement.resized(panel.frame, height: height, screen: screen)
            f.size.width = fitting.width
            if f.maxX > screen.maxX { f.origin.x = max(screen.minX, screen.maxX - f.width) }
            if Snapshot.dir != nil { f.origin = Snapshot.away }
            panel.setFrame(f, display: true)
        }
        layout()
        Snapshot.card(effect, model)
    }

    private func layout() {
        effect.frame = NSRect(origin: .zero, size: panel.frame.size)
        hosting?.frame = effect.bounds
        panel.invalidateShadow()
    }
}
