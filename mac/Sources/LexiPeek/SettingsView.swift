import AppKit
import CxxStdlib
import LexiPeekCore
import LexiPeekKit
import ServiceManagement
import SwiftUI

// Settings: InkPeek's sections as toolbar tabs, each with its sub-tabs (a segmented control); rows from the core's settings_view.
enum SettingsSection: Int, CaseIterable {
    case general, languages, card, ai

    // General first, as macOS settings; the core keeps InkPeek's order (Notes are in the LexiPeek window).
    var core: lexipeek.SettingsSection { [.general, .languages, .card, .ai][rawValue] }
    var key: String { ["general", "languages", "card", "ai"][rawValue] }
    var title: String { String(lexipeek.settings_sections()[Int(core.rawValue)]) }
    var tabs: [String] { lexipeek.settings_tabs(core).map { String($0) } }
    var tabKeys: [String] { lexipeek.settings_tab_keys(core).map { String($0) } }
    var symbol: String {
        switch self {
        case .languages: "character.book.closed"
        case .card: "rectangle.on.rectangle"
        case .ai: "sparkles"
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
    @Published private var checking = false
    @Published private var update = lexipeek.UpdateCheck()
    @Published private var aiTesting = false
    @Published private var aiTest = ""
    // AI → Modules' prompt editor and AI → Connection's model list, shown as sheets.
    @Published var editing: PromptEdit?
    @Published var choosingModel = false
    // Languages → Translation: a method's models being downloaded (its id) and how far.
    @Published private var downloading: String?
    @Published private var downloadProgress = 0.0
    @Published private var downloadError = ""
    var openFirstRun: (() -> Void)?
    // The app's language changed: the window's own labels again.
    var relabel: (() -> Void)?
    var showSection: ((SettingsSection, Int) -> Void)?
    let client: CoreClient

    init(client: CoreClient) { self.client = client }

    private var short: String { Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "" }

    private var version: String { "\(short) (\(Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? ""))" }

    func state() -> lexipeek.SettingsState {
        var s = lexipeek.SettingsState()
        s.trusted = trusted
        s.has_pair = lexipeek.app.has_pair(std.string())
        s.login = SMAppService.mainApp.status == .enabled
        s.version = std.string(version)
        s.answers = lexipeek.app.answers(std.string())
        s.input = std.string(Prefs.shared.inputKey.label)
        s.checking = checking
        s.update = update.text
        s.update_version = update.version
        s.update_url = update.url
        s.ai_key = Keychain.has(String(lexipeek.app.ai_provider(std.string())))
        s.ai_testing = aiTesting
        s.ai_test = std.string(aiTest)
        s.ram_mb = CoreClient.device.ram_mb
        s.dotprod = CoreClient.device.features.contains { String($0) == "asimddp" }
        s.downloading = std.string(downloading ?? "")
        s.progress = downloadProgress
        s.download_error = std.string(downloadError)
        return s
    }

    func page(_ section: SettingsSection) -> lexipeek.SettingsPage {
        _ = revision
        return lexipeek.app.settings_page(std.string(), section.core, Int32(tabs[section] ?? 0), state())
    }

    func select(_ section: SettingsSection, _ tab: Int) {
        tabs[section] = tab
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

    // A text row typed in (the server's address, the program, the model).
    func setText(_ key: String, _ value: String) {
        changed(lexipeek.app.set_text(std.string(), std.string(key), std.string(value)))
    }

    // The API key into the Keychain (an empty one removes it); never into the settings.
    func saveKey(_ key: String) {
        Keychain.set(String(lexipeek.app.ai_provider(std.string())), key)
        aiTest = ""
        revision += 1
    }

    func editPrompt(_ id: String) {
        let e = lexipeek.app.prompt_editor(std.string(), std.string(id))
        editing = PromptEdit(e)
    }

    func savePrompts(_ e: PromptEdit, reset: Bool) {
        var ids = lexipeek.mac.Strings(), texts = lexipeek.mac.Strings()
        for f in e.fields {
            ids.push_back(std.string(f.id))
            texts.push_back(std.string(reset ? "" : f.text))
        }
        changed(lexipeek.app.set_prompts(std.string(), ids, texts))
    }

    func press(_ key: String, edit: String = "") {
        if !edit.isEmpty { editPrompt(edit); return }
        switch key {
        case "setup": openFirstRun?()
        case "order": showSection?(.languages, 2)
        case "answer_lang": showSection?(.languages, 1)
        case "ai_test": testAI()
        case "ai_reset": changed(lexipeek.app.reset_prompts(std.string()))
        case "updates": checkUpdates()
        case "access":
            let k = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
            AXIsProcessTrustedWithOptions([k: true] as CFDictionary)
            NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
        default: break
        }
    }

    // A method's card: picked (as in InkPeek, its models download from the card's button).
    func pickMethod(_ id: String) {
        choose("method", ["fast": 1, "direct": 2, "llm": 3][id] ?? 0)
    }

    func downloadMethod(_ id: String, _ ids: [String]) {
        guard downloading == nil, !ids.isEmpty else { return }
        let dl = lexipeek.app.Download()
        downloading = id
        downloadProgress = 0
        downloadError = ""
        let timer = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in
            let total = dl.get_total()
            self?.downloadProgress = total > 0 ? Double(dl.get_done()) / Double(total) : 0
        }
        var v = lexipeek.mac.Strings()
        for i in ids { v.push_back(std.string(i)) }
        DispatchQueue.global(qos: .userInitiated).async {
            let s = lexipeek.app.install(std.string(), v, dl)
            let e = s.cancelled ? "" : String(s.error)
            DispatchQueue.main.async {
                timer.invalidate()
                self.downloading = nil
                self.downloadError = e
                self.client.reload()
                self.revision += 1
            }
        }
    }

    // Only on a click: GitHub's latest release, off the main queue.
    private func checkUpdates() {
        guard !checking else { return }
        checking = true
        let v = short
        DispatchQueue.global(qos: .userInitiated).async {
            let r = lexipeek.app.check_updates(std.string(v), std.string("https://api.github.com/repos/kafeg/lexipeek/releases/latest"))
            DispatchQueue.main.async {
                self.update = r
                self.checking = false
            }
        }
    }

    // Only on a click: one short request with these settings, off the main queue.
    private func testAI() {
        guard !aiTesting else { return }
        aiTesting = true
        let key = Keychain.current
        DispatchQueue.global(qos: .userInitiated).async {
            let r = String(lexipeek.app.ai_test(std.string(), std.string(key)))
            DispatchQueue.main.async {
                self.aiTest = r
                self.aiTesting = false
            }
        }
    }

    private func changed(_ error: std.string) {
        Prefs.shared.reload(String(error))
        L.apply()
        relabel?()
        revision += 1
    }
}

enum SettingsWindow {
    static func make(model: SettingsModel) -> NSWindow {
        let tabs = NSTabViewController()
        tabs.tabStyle = .toolbar
        for section in SettingsSection.allCases {
            let vc = NSHostingController(rootView: SectionView(section: section, model: model))
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

    // The sections' names in the app's language now.
    static func relabel(_ w: NSWindow?) {
        guard let tabs = w?.contentViewController as? NSTabViewController else { return }
        for (i, item) in tabs.tabViewItems.enumerated() {
            let s = SettingsSection.allCases[i]
            item.label = s.title
            item.image?.accessibilityDescription = s.title
        }
        w?.title = SettingsSection.allCases[tabs.selectedTabViewItemIndex].title
    }

    static func select(_ w: NSWindow?, _ section: SettingsSection, tab: Int? = nil, model: SettingsModel) {
        guard let tabs = w?.contentViewController as? NSTabViewController else { return }
        if let tab { model.select(section, tab) }
        tabs.selectedTabViewItemIndex = section.rawValue
        w?.title = section.title
    }
}

// A section: its sub-tabs, then its pane (the core's rows, or Packs, Other dictionaries drawn by their own views).
struct SectionView: View {
    let section: SettingsSection
    @ObservedObject var model: SettingsModel

    var body: some View {
        let page = model.page(section)
        VStack(spacing: 0) {
            if !page.tabs.isEmpty {
                let labels = page.tabs.map { String($0) }
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
            case "other": OtherDictionariesPane(client: model.client)
            default: RowsView(rows: page.rows.map { SettingLine($0) }, model: model)
            }
        }
        .frame(width: 560, height: 540)
        .sheet(item: section == .ai ? $model.editing : .constant(nil)) { e in PromptSheet(edit: e, model: model) }
        .sheet(isPresented: section == .ai ? $model.choosingModel : .constant(false)) { ModelSheet(model: model) }
    }
}

// A row copied out of the core's SettingRow.
struct SettingLine: Identifiable {
    var kind: lexipeek.SettingRow.Kind
    var key, title, about, value: String
    var on, enabled: Bool
    var choices: [String]
    var selected: Int
    var placeholder, edit: String
    var detail, button: String
    var ids: [String]
    var progress: Double
    var id: String { "\(kind.rawValue)|\(key)|\(title)" }

    init(_ r: lexipeek.SettingRow) {
        kind = r.kind
        key = String(r.key)
        title = String(r.title)
        about = String(r.about)
        value = String(r.value)
        on = r.on
        enabled = r.enabled
        choices = r.choices.map { String($0) }
        selected = Int(r.selected)
        placeholder = String(r.placeholder)
        edit = String(r.edit)
        detail = String(r.detail)
        button = String(r.button)
        ids = r.ids.map { String($0) }
        progress = r.progress
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
        case .toggle where !r.edit.isEmpty:
            HStack {
                Toggle(isOn: Binding(get: { r.on }, set: { model.toggle(r.key, $0) })) { label(r) }.disabled(!r.enabled)
                Button { model.editPrompt(r.edit) } label: { Image(systemName: "square.and.pencil") }
                    .buttonStyle(.borderless)
                    .help(L.t("edit_btn"))
            }
        case .toggle:
            Toggle(isOn: Binding(get: { r.on }, set: { model.toggle(r.key, $0) })) { label(r) }.disabled(!r.enabled)
        case .text:
            LabeledContent {
                LineField(value: r.value, placeholder: r.placeholder) { model.setText(r.key, $0) }
            } label: { label(r) }
        case .secret:
            VStack(alignment: .leading, spacing: 6) {
                label(r)
                KeyField(saved: r.on) { model.saveKey($0) }
            }
        case .model:
            LabeledContent {
                HStack {
                    LineField(value: r.value, placeholder: r.placeholder) { model.setText(r.key, $0) }
                    if r.on { Button(L.t("model_choose")) { model.choosingModel = true } }
                }
            } label: { label(r) }
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
        case .button:
            LabeledContent {
                Button(r.value) { model.press(r.key, edit: r.edit) }.disabled(!r.enabled)
            } label: { label(r) }
        case .status:
            LabeledContent {
                if r.on {
                    Label(r.value, systemImage: "checkmark.circle.fill").foregroundStyle(.green)
                } else {
                    Text(r.value).foregroundStyle(.secondary)
                }
            } label: { label(r) }
        case .method:
            method(r)
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

extension RowsView {
    // A translation method as InkPeek's card: picked by a click; what it does, needs, why not here; its download.
    func method(_ r: SettingLine) -> some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: r.on ? "largecircle.fill.circle" : "circle")
                .foregroundStyle(r.on ? Color.accentColor : .secondary)
                .padding(.top, 1)
            VStack(alignment: .leading, spacing: 3) {
                Text(r.title).fontWeight(r.on ? .semibold : .regular)
                Text(r.about).font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                if !r.detail.isEmpty {
                    Text(r.detail).font(.caption).foregroundStyle(r.enabled ? .secondary : Color.orange).fixedSize(horizontal: false, vertical: true)
                }
                if r.progress >= 0 {
                    ProgressView(value: r.progress).controlSize(.small)
                } else if !r.button.isEmpty {
                    Button(r.button) { model.downloadMethod(r.value, r.ids) }.controlSize(.small).padding(.top, 2)
                }
            }
            Spacer(minLength: 0)
        }
        .padding(.vertical, 2)
        .opacity(r.enabled ? 1 : 0.55)
        .contentShape(Rectangle())
        .onTapGesture { if r.enabled { model.pickMethod(r.value) } }
    }
}

// A line typed in, written when Return is pressed or the field is left.
private struct LineField: View {
    var value: String
    var placeholder: String
    var commit: (String) -> Void
    @State private var text = ""
    @FocusState private var focused: Bool

    var body: some View {
        TextField("", text: $text, prompt: Text(placeholder))
            .labelsHidden()
            .textFieldStyle(.roundedBorder)
            .frame(minWidth: 200)
            .focused($focused)
            .onAppear { text = value }
            .onChange(of: value) { text = value }
            .onSubmit { if text != value { commit(text) } }
            .onChange(of: focused) { if !focused && text != value { commit(text) } }
    }
}

// The API key: typed (hidden), then Save puts it into the Keychain; Remove takes it out. It is never shown again.
private struct KeyField: View {
    var saved: Bool
    var save: (String) -> Void
    @State private var key = ""

    var body: some View {
        HStack {
            SecureField("", text: $key, prompt: Text(L.t("key_t")))
                .labelsHidden()
                .textFieldStyle(.roundedBorder)
                .onSubmit { store() }
            Button(L.t("save_key")) { store() }.disabled(key.isEmpty)
            if saved { Button(L.t("remove")) { save("") } }
        }
    }

    private func store() {
        guard !key.isEmpty else { return }
        save(key)
        key = ""
    }
}

// The prompt editor's state: the core's fields, edited here.
struct PromptEdit: Identifiable {
    struct Field {
        var id, label, text, original: String
    }

    let id = UUID()
    var title, note, cancel, reset, save: String
    var fields: [Field]

    init(_ e: lexipeek.PromptEditor) {
        title = String(e.title)
        note = String(e.note)
        cancel = String(e.cancel)
        reset = String(e.reset)
        save = String(e.save)
        fields = e.fields.map { Field(id: String($0.id), label: String($0.label), text: String($0.text), original: String($0.original)) }
    }
}

// AI → Modules' ✎: what each part of the answer asks for; Original puts the shipped text back.
private struct PromptSheet: View {
    @State var edit: PromptEdit
    @ObservedObject var model: SettingsModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(edit.title).font(.headline)
            Text(edit.note).font(.callout).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            ScrollView {
                VStack(alignment: .leading, spacing: 10) {
                    ForEach(edit.fields.indices, id: \.self) { i in
                        if !edit.fields[i].label.isEmpty { Text(edit.fields[i].label).bold() }
                        TextEditor(text: $edit.fields[i].text)
                            .font(.body)
                            .frame(minHeight: edit.fields.count > 1 ? 54 : 220)
                            .overlay(RoundedRectangle(cornerRadius: 4).stroke(.separator))
                    }
                }
            }
            HStack {
                Button(edit.reset) {
                    model.savePrompts(edit, reset: true)
                    dismiss()
                }
                Spacer()
                Button(edit.cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Button(edit.save) {
                    model.savePrompts(edit, reset: false)
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
            }
        }
        .padding(16)
        .frame(width: 520, height: edit.fields.count > 1 ? 560 : 400)
    }
}

// "Choose…": the provider's models (fetched once the sheet opens), a filter, free ones only; a click picks one.
private struct ModelSheet: View {
    @ObservedObject var model: SettingsModel
    @Environment(\.dismiss) private var dismiss
    @State private var all = lexipeek.mac.Models()
    @State private var error = ""
    @State private var loading = true
    @State private var filter = ""
    @State private var freeOnly = false

    private var shown: lexipeek.mac.Models { lexipeek.app.filter_models(all, std.string(filter), freeOnly) }

    var body: some View {
        let list = shown
        let titles = lexipeek.app.model_titles(list).map { String($0) }
        VStack(alignment: .leading, spacing: 10) {
            Text(loading ? L.t("loading_models") : L.t("models_title", String(lexipeek.app.ai_provider_title(std.string())), String(all.size())))
                .font(.headline)
            HStack {
                TextField(L.t("filter"), text: $filter).textFieldStyle(.roundedBorder)
                if all.contains(where: { $0.free }) { Toggle(L.t("free_only"), isOn: $freeOnly) }
            }
            if loading {
                ProgressView().frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if !error.isEmpty {
                Text(error).foregroundStyle(.red).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            } else if titles.isEmpty {
                Text(L.t("models_none")).foregroundStyle(.secondary).frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List(titles.indices, id: \.self) { i in
                    Button { pick(String(list[i].id)) } label: {
                        Text(titles[i]).frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
            }
            HStack {
                Spacer()
                Button(L.t("cancel")) { dismiss() }.keyboardShortcut(.cancelAction)
            }
        }
        .padding(16)
        .frame(width: 520, height: 480)
        .onAppear(perform: load)
    }

    private func load() {
        let key = Keychain.current
        DispatchQueue.global(qos: .userInitiated).async {
            let r = lexipeek.app.ai_models(std.string(), std.string(key))
            DispatchQueue.main.async {
                all = r.list
                error = String(r.error)
                loading = false
            }
        }
    }

    private func pick(_ id: String) {
        model.setText("ai_model", id)
        dismiss()
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
            if let e = error ?? (conflict ? L.t("hk_conflict") : nil) {
                Text(e).font(.caption).foregroundStyle(.red)
            }
            Button(recording ? L.t("hk_press") : shortcut.label) { recording ? stop() : start() }
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
                    .help(L.t("remove"))
            }
        }
        Menu {
            ForEach(running, id: \.self) { id in
                Button { ids.append(id) } label: { Text(name(id)) }
            }
        } label: {
            Label(L.t("add_app"), systemImage: "plus")
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
