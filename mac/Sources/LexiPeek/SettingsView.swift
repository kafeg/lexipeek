import AppKit
import CxxStdlib
import LexiPeekCore
import LexiPeekKit
import ServiceManagement
import SwiftUI

// Settings: InkPeek's sections as toolbar tabs, each with its sub-tabs (a segmented control); rows from the core's settings_view.
enum SettingsSection: Int, CaseIterable {
    case languages, card, notes, general

    var core: lexipeek.SettingsSection { [.languages, .card, .notes, .general][rawValue] }
    var key: String { ["languages", "card", "notes", "general"][rawValue] }
    var title: String { String(lexipeek.settings_sections()[rawValue]) }
    var tabs: [String] { lexipeek.settings_tabs(core).map { String($0) } }
    var symbol: String {
        switch self {
        case .languages: "character.book.closed"
        case .card: "rectangle.on.rectangle"
        case .notes: "star"
        case .general: "gearshape"
        }
    }
}

// What the panes show and what their rows do: every change goes through the core and is written at once.
final class SettingsModel: ObservableObject {
    @Published var trusted = Selection.trusted
    @Published var hotkeyErrors: [String: String] = [:]
    @Published var tabs: [SettingsSection: Int] = [:]
    @Published private(set) var revision = 0
    var openFirstRun: (() -> Void)?
    let client: CoreClient
    let notes: NotesModel

    init(client: CoreClient, notes: NotesModel) {
        self.client = client
        self.notes = notes
        NotificationCenter.default.addObserver(forName: .dictionariesChanged, object: nil, queue: .main) { [weak self] _ in
            self?.revision += 1
        }
    }

    private var version: String {
        let i = Bundle.main.infoDictionary
        return "\(i?["CFBundleShortVersionString"] as? String ?? "") (\(i?["CFBundleVersion"] as? String ?? ""))"
    }

    func state() -> lexipeek.SettingsState {
        var s = lexipeek.SettingsState()
        s.trusted = trusted
        s.has_pair = lexipeek.app.has_pair(std.string())
        s.login = SMAppService.mainApp.status == .enabled
        s.version = std.string(version)
        s.answers = lexipeek.app.answers(std.string())
        for d in client.dictionaries { s.dictionaries.push_back(d) }
        return s
    }

    func page(_ section: SettingsSection) -> lexipeek.SettingsPage {
        _ = revision
        return lexipeek.app.settings_page(std.string(), section.core, Int32(tabs[section] ?? 0), state())
    }

    func select(_ section: SettingsSection, _ tab: Int) {
        tabs[section] = tab
        if section == .notes { notes.tab = NotesModel.tabs[min(tab, NotesModel.tabs.count - 1)] }
    }

    func toggle(_ key: String, _ on: Bool) {
        var on = on
        if key == "open_at_login" {
            try? on ? SMAppService.mainApp.register() : SMAppService.mainApp.unregister()
            on = SMAppService.mainApp.status == .enabled
        }
        changed(lexipeek.app.toggle(std.string(), std.string(key), on))
    }

    func choose(_ key: String, _ index: Int) {
        changed(lexipeek.app.choose(std.string(), std.string(key), Int32(index), state()))
    }

    func move(_ id: String, _ by: Int) {
        changed(lexipeek.app.move(std.string(), std.string(id), Int32(by), state()))
    }

    func press(_ key: String) {
        switch key {
        case "setup": openFirstRun?()
        case "access":
            let k = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
            AXIsProcessTrustedWithOptions([k: true] as CFDictionary)
            NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
        default: break
        }
    }

    private func changed(_ error: std.string) {
        Prefs.shared.reload(String(error))
        revision += 1
    }
}

enum SettingsWindow {
    static func make(model: SettingsModel) -> NSWindow {
        let tabs = NSTabViewController()
        tabs.tabStyle = .toolbar
        for section in SettingsSection.allCases {
            let vc = NSHostingController(rootView: SectionView(section: section, model: model, notes: model.notes))
            vc.sizingOptions = .preferredContentSize
            let item = NSTabViewItem(viewController: vc)
            item.label = section.title
            item.image = NSImage(systemSymbolName: section.symbol, accessibilityDescription: section.title)
            item.identifier = section.key
            tabs.addTabViewItem(item)
        }
        let w = NSWindow(contentViewController: tabs)
        w.styleMask = [.titled, .closable]
        w.toolbarStyle = .preference
        w.isReleasedWhenClosed = false
        w.center()
        return w
    }

    static func select(_ w: NSWindow?, _ section: SettingsSection, tab: Int? = nil, model: SettingsModel) {
        guard let tabs = w?.contentViewController as? NSTabViewController else { return }
        if let tab { model.select(section, tab) }
        tabs.selectedTabViewItemIndex = section.rawValue
        w?.title = section.title
    }
}

// A section: its sub-tabs, then its pane (the core's rows, or Packs, Other dictionaries, Notes drawn by their own views).
struct SectionView: View {
    let section: SettingsSection
    @ObservedObject var model: SettingsModel
    @ObservedObject var notes: NotesModel

    var body: some View {
        let page = model.page(section)
        VStack(spacing: 0) {
            if !page.tabs.isEmpty {
                let labels = section == .notes && notes.look.tabs.count == page.tabs.count ? notes.look.tabs : page.tabs.map { String($0) }
                Picker("", selection: Binding(get: { Int(page.tab) }, set: { model.select(section, $0) })) {
                    ForEach(labels.indices, id: \.self) { Text(labels[$0]).tag($0) }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .fixedSize()
                .padding(.top, 14)
            }
            switch String(page.pane) {
            case "packs": PacksPane(client: model.client)
            case "other": OtherDictionariesPane()
            case "notes": NotesList(model: notes).onAppear { notes.refresh() }
            default: RowsView(rows: page.rows.map { SettingLine($0) }, model: model)
            }
        }
        .frame(width: 560, height: 540)
    }
}

// A row copied out of the core's SettingRow.
struct SettingLine: Identifiable {
    var kind: lexipeek.SettingRow.Kind
    var key, title, about, value: String
    var on, enabled, up, down: Bool
    var choices: [String]
    var selected: Int
    var id: String { "\(kind.rawValue)|\(key)|\(title)" }

    init(_ r: lexipeek.SettingRow) {
        kind = r.kind
        key = String(r.key)
        title = String(r.title)
        about = String(r.about)
        value = String(r.value)
        on = r.on
        enabled = r.enabled
        up = r.up
        down = r.down
        choices = r.choices.map { String($0) }
        selected = Int(r.selected)
    }
}

// InkPeek's Rows: every row a title, a one-line description and its control; a heading starts a group.
struct RowsView: View {
    var rows: [SettingLine]
    @ObservedObject var model: SettingsModel
    @ObservedObject var prefs = Prefs.shared

    private var groups: [(head: SettingLine?, rows: [SettingLine])] {
        var out: [(head: SettingLine?, rows: [SettingLine])] = []
        for r in rows {
            if r.kind == .heading || out.isEmpty { out.append((r.kind == .heading ? r : nil, [])) }
            if r.kind != .heading { out[out.count - 1].rows.append(r) }
        }
        return out
    }

    var body: some View {
        Form {
            ForEach(Array(groups.enumerated()), id: \.offset) { _, g in
                Section {
                    ForEach(g.rows) { row($0) }
                } header: {
                    if let h = g.head {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(h.title)
                            if !h.about.isEmpty { Text(h.about).font(.caption).foregroundStyle(.secondary).textCase(nil) }
                        }
                    }
                }
            }
        }
        .formStyle(.grouped)
    }

    private func label(_ r: SettingLine) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(r.title).lineLimit(1)
            Text(r.about).font(.caption).foregroundStyle(.secondary).lineLimit(2).fixedSize(horizontal: false, vertical: true)
        }
        .help(r.about)
    }

    @ViewBuilder
    private func row(_ r: SettingLine) -> some View {
        switch r.kind {
        case .toggle:
            Toggle(isOn: Binding(get: { r.on }, set: { model.toggle(r.key, $0) })) { label(r) }.disabled(!r.enabled)
        case .choice:
            Picker(selection: Binding(get: { r.selected }, set: { model.choose(r.key, $0) })) {
                ForEach(r.choices.indices, id: \.self) { Text(r.choices[$0]).tag($0) }
            } label: { label(r) }
            .disabled(!r.enabled)
        case .hotkey:
            LabeledContent {
                if r.key == "input" {
                    ShortcutField(shortcut: $prefs.inputKey, other: prefs.translateKey, error: model.hotkeyErrors["input"])
                } else {
                    ShortcutField(shortcut: $prefs.translateKey, other: prefs.inputKey, error: model.hotkeyErrors["translate"])
                }
            } label: { label(r) }
        case .apps:
            VStack(alignment: .leading, spacing: 6) {
                label(r)
                AppList(ids: r.key == "copy_apps" ? $prefs.copyApps : $prefs.offApps)
            }
        case .item:
            LabeledContent {
                HStack(spacing: 2) {
                    Button { model.move(r.key, -1) } label: { Image(systemName: "chevron.up") }.disabled(!r.up).help("Move up")
                    Button { model.move(r.key, 1) } label: { Image(systemName: "chevron.down") }.disabled(!r.down).help("Move down")
                }
                .buttonStyle(.borderless)
            } label: { label(r) }
        case .button:
            LabeledContent {
                Button(r.value) { model.press(r.key) }.disabled(!r.enabled)
            } label: { label(r) }
        case .status:
            LabeledContent {
                if r.on {
                    Label(r.value, systemImage: "checkmark.circle.fill").foregroundStyle(.green)
                } else {
                    Text(r.value).foregroundStyle(.secondary)
                }
            } label: { label(r) }
        case .link:
            LabeledContent {
                if let url = URL(string: r.value) {
                    Link(destination: url) { Image(systemName: "arrow.up.right.square") }.help(r.value)
                }
            } label: { label(r) }
        default:
            label(r)
        }
    }
}

// Click, then press the new keys; Esc cancels.
private struct ShortcutField: View {
    @Binding var shortcut: Shortcut
    var other: Shortcut
    var error: String?
    @State private var recording = false
    @State private var monitor: Any?
    @State private var conflict = false

    var body: some View {
        HStack {
            if let e = error ?? (conflict ? "Used by the other hotkey" : nil) {
                Text(e).font(.caption).foregroundStyle(.red)
            }
            Button(recording ? "Press keys…" : shortcut.label) { recording ? stop() : start() }
                .frame(minWidth: 90)
        }
    }

    private func start() {
        recording = true
        conflict = false
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { e in
            if e.keyCode == 0x35 { stop(); return nil }
            var m: UInt32 = 0
            let f = e.modifierFlags
            if f.contains(.command) { m |= Shortcut.cmd }
            if f.contains(.option) { m |= Shortcut.option }
            if f.contains(.control) { m |= Shortcut.control }
            if f.contains(.shift) { m |= Shortcut.shift }
            guard let s = Shortcut(keyCode: e.keyCode, modifiers: m), s.isValid else { NSSound.beep(); return nil }
            if s == other { conflict = true } else { shortcut = s }
            stop()
            return nil
        }
    }

    private func stop() {
        recording = false
        if let monitor { NSEvent.removeMonitor(monitor) }
        monitor = nil
    }
}

// Apps by bundle id, with their icons; added from the running apps.
private struct AppList: View {
    @Binding var ids: [String]

    var body: some View {
        ForEach(ids, id: \.self) { id in
            HStack {
                icon(id)
                Text(name(id))
                Spacer()
                Button { ids.removeAll { $0 == id } } label: { Image(systemName: "minus.circle.fill").foregroundStyle(.secondary) }
                    .buttonStyle(.borderless)
                    .help("Remove")
            }
        }
        Menu {
            ForEach(running, id: \.self) { id in
                Button { ids.append(id) } label: { Text(name(id)) }
            }
        } label: {
            Label("Add App…", systemImage: "plus")
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
    }

    private var running: [String] {
        NSWorkspace.shared.runningApplications
            .filter { $0.activationPolicy == .regular }
            .compactMap(\.bundleIdentifier)
            .filter { !ids.contains($0) && $0 != Bundle.main.bundleIdentifier }
            .sorted { name($0) < name($1) }
    }

    private func url(_ id: String) -> URL? { NSWorkspace.shared.urlForApplication(withBundleIdentifier: id) }

    private func name(_ id: String) -> String {
        guard let url = url(id) else { return id }
        return FileManager.default.displayName(atPath: url.path).replacingOccurrences(of: ".app", with: "")
    }

    @ViewBuilder private func icon(_ id: String) -> some View {
        if let url = url(id) {
            Image(nsImage: NSWorkspace.shared.icon(forFile: url.path)).resizable().frame(width: 18, height: 18)
        }
    }
}
