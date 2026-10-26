import AppKit
import Combine
import LexiPeekCore
import LexiPeekKit
import SwiftUI

// The menu-bar app: menu, hotkeys, the card, Service, URL scheme, settings, memory pressure.
final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private static let translateKey: UInt32 = 1, inputKey: UInt32 = 2

    private let prefs = Prefs.shared
    private let client = CoreClient()
    private lazy var model = CardModel(client: client)
    private lazy var card = CardController(model: model)
    private let settingsState = SettingsState()
    private var settingsWindow: NSWindow?
    private var item: NSStatusItem?
    private let menu = NSMenu()
    private var bag: [AnyCancellable] = []
    private var pressure: DispatchSourceMemoryPressure?
    private var trustTimer: Timer?
    private let icon = SelectionIcon()

    func applicationDidFinishLaunching(_ notification: Notification) {
        Selection.log.info("launched; Accessibility trusted: \(Selection.trusted)")
        item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        item?.button?.image = NSImage(systemSymbolName: "character.book.closed", accessibilityDescription: "LexiPeek")
        menu.delegate = self
        item?.menu = menu
        NSApp.servicesProvider = self
        NSUpdateDynamicServices()

        Publishers.CombineLatest(prefs.$translateKey, prefs.$inputKey)
            .sink { [weak self] t, i in self?.registerHotkeys(t, i) }.store(in: &bag)
        // Either one changed (CombineLatest would wait for both).
        Publishers.Merge(prefs.$answer.dropFirst().map { _ in () }, prefs.$method.dropFirst().map { _ in () })
            .debounce(for: .milliseconds(300), scheduler: RunLoop.main)
            .sink { [weak self] in self?.client.reconfigure() }.store(in: &bag)

        icon.open = { [weak self] s in self?.card.show(text: s.text, source: .hotkey, bounds: s.bounds, focus: false) }
        prefs.$selectionIcon.sink { [weak self] on in self?.icon.setEnabled(on) }.store(in: &bag)

        pressure = DispatchSource.makeMemoryPressureSource(eventMask: [.warning, .critical], queue: .main)
        pressure?.setEventHandler { [weak self] in self?.client.release() }
        pressure?.resume()

        trustTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            guard let self, self.settingsState.trusted != Selection.trusted else { return }
            self.settingsState.trusted = Selection.trusted
        }
        if Snapshot.dir != nil { snapshotSettings() }
        if !Selection.trusted && !UserDefaults.standard.bool(forKey: "onboarded") {
            UserDefaults.standard.set(true, forKey: "onboarded")
            openSettings(.permissions)
        }
    }

    func application(_ application: NSApplication, open urls: [URL]) {
        for url in urls {
            switch LookupURL(url) {
            case .lookup(let t): card.show(text: t, source: .url, bounds: nil, focus: false)
            case .input: card.show(text: nil, source: .input, bounds: nil, focus: true)
            case nil: break
            }
        }
    }

    // The Service "Look up in LexiPeek": the system hands over the text.
    @objc func lookUp(_ pboard: NSPasteboard, userData: String?, error: AutoreleasingUnsafeMutablePointer<NSString?>) {
        guard let t = pboard.string(forType: .string) else { return }
        card.show(text: t, source: .service, bounds: nil, focus: false)
    }

    private func registerHotkeys(_ t: Shortcut, _ i: Shortcut) {
        let ok1 = Hotkeys.shared.register(Self.translateKey, t) { [weak self] in self?.translateSelection() }
        let ok2 = Hotkeys.shared.register(Self.inputKey, i) { [weak self] in self?.typeText() }
        settingsState.hotkeyErrors = [:]
        if !ok1 { settingsState.hotkeyErrors["translate"] = "Taken by another app" }
        if !ok2 { settingsState.hotkeyErrors["input"] = "Taken by another app" }
    }

    // With a card open: another selection opens its card at once; the same one or none closes it.
    @objc func translateSelection() {
        let app = NSWorkspace.shared.frontmostApplication
        let id = app?.bundleIdentifier ?? ""
        if prefs.offApps.contains(id) { if card.isVisible { card.close() }; return }
        let allowCopy = prefs.copyApps.contains(id)
        let open = card.isVisible
        Task { @MainActor in
            let s = await Selection.read(app: app, allowCopy: allowCopy)
            if open && (s == nil || s?.text == card.shown) { card.close(); return }
            if let s {
                card.show(text: s.text, source: .hotkey, bounds: s.bounds, focus: false)
            } else {
                // Nothing read: say where, offer Cmd+C there (not when it was allowed already), type meanwhile.
                card.show(text: nil, source: .input, bounds: nil, focus: true)
                if Selection.trusted && !allowCopy && !id.isEmpty {
                    model.unread = (id, app?.localizedName ?? id)
                    model.retry = { [weak self] in
                        self?.card.close()
                        self?.translateSelection()
                    }
                }
            }
        }
    }

    @objc func typeText() {
        if card.isVisible && model.showInput { card.close(); return }
        card.show(text: nil, source: .input, bounds: nil, focus: true)
    }

    @objc private func openRecent(_ sender: NSMenuItem) {
        guard let t = sender.representedObject as? String else { return }
        card.show(text: t, source: .input, bounds: nil, focus: false)
    }

    @objc private func showSettings() { openSettings(.general) }

    private func openSettings(_ pane: SettingsPane) {
        if settingsWindow == nil { settingsWindow = SettingsWindow.make(state: settingsState, client: client) }
        SettingsWindow.select(settingsWindow, pane)
        NSApp.activate(ignoringOtherApps: true)
        settingsWindow?.makeKeyAndOrderFront(nil)
    }

    // Each settings pane drawn off screen, without showing a window.
    private func snapshotSettings() {
        for pane in SettingsPane.allCases {
            let w = NSWindow(contentViewController: NSHostingController(rootView: SettingsWindow.view(pane, state: SettingsState(), client: client)))
            w.setFrameOrigin(NSPoint(x: -10000, y: -10000))
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { Snapshot.write(w.contentView, "settings-\(pane.rawValue).png") }
        }
    }

    // Rebuilt on every open: hotkey labels and Recents stay current.
    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()
        menu.addItem(item("Translate selection", #selector(translateSelection), prefs.translateKey))
        menu.addItem(item("Type text…", #selector(typeText), prefs.inputKey))
        let recents = NSMenuItem(title: "Recents", action: nil, keyEquivalent: "")
        let sub = NSMenu()
        for (t, meaning) in client.recents() {
            let title = meaning.isEmpty ? t : "\(t) — \(meaning)"
            let i = NSMenuItem(title: Recents.title(title), action: #selector(openRecent), keyEquivalent: "")
            i.target = self
            i.representedObject = t
            sub.addItem(i)
        }
        if sub.items.isEmpty { sub.addItem(NSMenuItem(title: "Nothing yet", action: nil, keyEquivalent: "")) }
        recents.submenu = sub
        menu.addItem(recents)
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "Translate into: \(prefs.answer)", action: nil, keyEquivalent: ""))
        menu.addItem(.separator())
        let s = NSMenuItem(title: "Settings…", action: #selector(showSettings), keyEquivalent: ",")
        s.target = self
        menu.addItem(s)
        menu.addItem(NSMenuItem(title: "Quit LexiPeek", action: #selector(NSApplication.terminate), keyEquivalent: "q"))
    }

    private func item(_ title: String, _ action: Selector, _ s: Shortcut) -> NSMenuItem {
        let i = NSMenuItem(title: title, action: action, keyEquivalent: s.key.count == 1 ? s.key.lowercased() : "")
        var m: NSEvent.ModifierFlags = []
        if s.modifiers & Shortcut.cmd != 0 { m.insert(.command) }
        if s.modifiers & Shortcut.option != 0 { m.insert(.option) }
        if s.modifiers & Shortcut.control != 0 { m.insert(.control) }
        if s.modifiers & Shortcut.shift != 0 { m.insert(.shift) }
        i.keyEquivalentModifierMask = m
        i.target = self
        return i
    }
}
