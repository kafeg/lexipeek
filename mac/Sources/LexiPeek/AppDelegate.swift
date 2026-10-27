import AppKit
import Combine
import CxxStdlib
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
    private lazy var settings = SettingsModel(client: client)
    private lazy var main = MainModel(client: client)
    private var mainWindow: NSWindow?
    private var settingsWindow: NSWindow?
    private lazy var firstRun = FirstRunModel(client: client)
    private var firstRunWindow: NSWindow?
    private var item: NSStatusItem?
    private let menu = NSMenu()
    private var bag: [AnyCancellable] = []
    private var pressure: DispatchSourceMemoryPressure?
    private var trustTimer: Timer?
    private let icon = SelectionIcon()

    func applicationDidFinishLaunching(_ notification: Notification) {
        Selection.log.info("launched; Accessibility trusted: \(Selection.trusted)")
        // Snapshot mode is invisible: never activated (an accessory app), no menu-bar item, no hotkeys, windows off screen.
        if Snapshot.dir != nil {
            Snapshot.start { [weak self] in self?.application(NSApp, open: [$0]) }
            snapshotSettings()
            return
        }
        item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        item?.button?.image = NSImage(named: "MenuBarIcon")
        item?.button?.image?.isTemplate = true
        item?.button?.image?.accessibilityDescription = "LexiPeek"
        menu.delegate = self
        item?.menu = menu
        NSApp.servicesProvider = self
        NSUpdateDynamicServices()

        Publishers.CombineLatest(prefs.$translateKey, prefs.$inputKey)
            .sink { [weak self] t, i in self?.registerHotkeys(t, i) }.store(in: &bag)
        prefs.$changed.dropFirst()
            .debounce(for: .milliseconds(300), scheduler: RunLoop.main)
            .sink { [weak self] _ in self?.client.configure() }.store(in: &bag)

        icon.open = { [weak self] s in self?.card.show(text: s.text, source: .hotkey, bounds: s.bounds, focus: false) }
        prefs.$selectionIcon.sink { [weak self] on in self?.icon.setEnabled(on) }.store(in: &bag)

        pressure = DispatchSource.makeMemoryPressureSource(eventMask: [.warning, .critical], queue: .main)
        pressure?.setEventHandler { [weak self] in self?.client.release() }
        pressure?.resume()

        trustTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            guard let self, self.settings.trusted != Selection.trusted else { return }
            self.settings.trusted = Selection.trusted
        }
        settings.openFirstRun = { [weak self] in self?.openFirstRun() }
        settings.relabel = { [weak self] in
            SettingsWindow.relabel(self?.settingsWindow)
            self?.firstRun.relabel()
            self?.firstRunWindow?.title = L.t("fr_window")
        }
        settings.showSection = { [weak self] s, tab in
            guard let self else { return }
            SettingsWindow.select(self.settingsWindow, s, tab: tab, model: self.settings)
        }
        if lexipeek.app.first_run_at_launch(std.string(), Selection.trusted) { openFirstRun() }
    }

    func application(_ application: NSApplication, open urls: [URL]) {
        for url in urls {
            if let step = Snapshot.firstRun(url) {
                if firstRunWindow == nil { openFirstRun() }
                Snapshot.firstRun(firstRunWindow, firstRun, step)
                continue
            }
            if let w = Snapshot.window(url) {
                snapshotMain(w.page, w.name, look: w.look)
                continue
            }
            if Snapshot.take(url) {
                card.show(text: nil, source: .input, bounds: nil, focus: false)
                model.unread = ("com.example.app", "Example")
                continue
            }
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
        settings.hotkeyErrors = [:]
        if !ok1 { settings.hotkeyErrors["translate"] = L.t("hk_taken") }
        if !ok2 { settings.hotkeyErrors["input"] = L.t("hk_taken") }
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

    @objc private func showSettings() { openSettings(nil) }

    @objc private func unload() { client.release() }

    @objc private func showMain() { openMain(nil) }

    // The LexiPeek window (nil: the page shown last); the app is in the Dock while it is open.
    private func openMain(_ page: MainPage?) {
        if mainWindow == nil {
            let w = MainWindow.make(model: main)
            NotificationCenter.default.addObserver(forName: NSWindow.willCloseNotification, object: w, queue: .main) { _ in
                if Snapshot.dir == nil { NSApp.setActivationPolicy(.accessory) }
            }
            mainWindow = w
        }
        if let page { main.show(page) }
        main.notes.refresh()
        if Snapshot.dir != nil, let w = mainWindow {
            Snapshot.hide(w)
            w.orderBack(nil)
            return
        }
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        mainWindow?.makeKeyAndOrderFront(nil)
    }

    // The window on a page, off screen: <name>.png, then <name>.done (look: a text looked up there).
    private func snapshotMain(_ page: MainPage, _ name: String, look: String?) {
        openMain(page)
        if let look { main.look(look) }
        DispatchQueue.main.asyncAfter(deadline: .now() + 4) {
            Snapshot.write(self.mainWindow?.contentView?.superview, "\(name).png")
            Snapshot.mark("\(name).done")
        }
    }

    // nil: the section shown last.
    private func openSettings(_ section: SettingsSection?) {
        if settingsWindow == nil { settingsWindow = SettingsWindow.make(model: settings) }
        if let section { SettingsWindow.select(settingsWindow, section, model: settings) }
        NSApp.activate(ignoringOtherApps: true)
        settingsWindow?.makeKeyAndOrderFront(nil)
    }

    // Accessibility, the first pair and its download, the hotkey; closed, it opens at launch only while no pair is installed.
    private func openFirstRun() {
        if firstRunWindow == nil {
            let w = FirstRunWindow.make(model: firstRun)
            firstRun.close = { [weak w] in w?.close() }
            NotificationCenter.default.addObserver(forName: NSWindow.willCloseNotification, object: w, queue: .main) { [weak self] _ in
                self?.firstRun.stop()
                self?.prefs.firstRun = true
            }
            firstRunWindow = w
        }
        firstRun.refresh()
        if let w = firstRunWindow, Snapshot.dir != nil {
            Snapshot.hide(w)
            w.orderBack(nil)
            return
        }
        firstRun.start()
        NSApp.activate(ignoringOtherApps: true)
        firstRunWindow?.makeKeyAndOrderFront(nil)
    }

    // Each section × tab in the real window (with its toolbar), off screen: settings-<section>[-<tab>].png; settings.done
    // when all are written.
    private func snapshotSettings() {
        let w = SettingsWindow.make(model: settings)
        Snapshot.hide(w)
        w.orderBack(nil)
        let shots = SettingsSection.allCases.flatMap { s in
            s.tabKeys.isEmpty ? [(s, 0, "settings-\(s.key)")] : s.tabKeys.enumerated().map { (s, $0.offset, "settings-\(s.key)-\($0.element)") }
        }
        func shot(_ i: Int) {
            guard i < shots.count else { w.orderOut(nil); Snapshot.mark("settings.done"); return }
            let (section, tab, name) = shots[i]
            SettingsWindow.select(w, section, tab: tab, model: settings)
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                Snapshot.write(w.contentView?.superview, "\(name).png")
                Snapshot.writeEnd(w.contentView?.superview, "\(name)-end.png")
                shot(i + 1)
            }
        }
        shot(0)
    }

    // Rebuilt on every open: hotkey labels and Recents stay current.
    func menuNeedsUpdate(_ menu: NSMenu) {
        menu.removeAllItems()
        menu.addItem(item(L.t("hk_lookup_t"), #selector(translateSelection), prefs.translateKey))
        menu.addItem(item(L.t("menu_type"), #selector(typeText), prefs.inputKey))
        let o = NSMenuItem(title: L.t("menu_open"), action: #selector(showMain), keyEquivalent: "o")
        o.target = self
        menu.addItem(o)
        let recents = NSMenuItem(title: L.t("recents"), action: nil, keyEquivalent: "")
        let sub = NSMenu()
        for (t, meaning) in client.recents() {
            let title = meaning.isEmpty ? t : "\(t) — \(meaning)"
            let i = NSMenuItem(title: Recents.title(title), action: #selector(openRecent), keyEquivalent: "")
            i.target = self
            i.representedObject = t
            sub.addItem(i)
        }
        if sub.items.isEmpty { sub.addItem(NSMenuItem(title: L.t("nothing_yet"), action: nil, keyEquivalent: "")) }
        recents.submenu = sub
        menu.addItem(recents)
        menu.addItem(.separator())
        // What is in memory now, and a way to free it.
        let mem = client.memory()
        menu.addItem(NSMenuItem(title: String(mem.line), action: nil, keyEquivalent: ""))
        if mem.held {
            menu.addItem(NSMenuItem(title: String(mem.detail), action: nil, keyEquivalent: ""))
            let u = NSMenuItem(title: String(mem.unload), action: #selector(unload), keyEquivalent: "")
            u.target = self
            menu.addItem(u)
        }
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: L.t("menu_answer", String(lexipeek.app.language(std.string(prefs.answer)))), action: nil, keyEquivalent: ""))
        menu.addItem(.separator())
        let s = NSMenuItem(title: L.t("settings_more"), action: #selector(showSettings), keyEquivalent: ",")
        s.target = self
        menu.addItem(s)
        menu.addItem(NSMenuItem(title: L.t("quit"), action: #selector(NSApplication.terminate), keyEquivalent: "q"))
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
