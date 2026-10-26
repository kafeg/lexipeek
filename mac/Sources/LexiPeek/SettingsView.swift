import AppKit
import CxxStdlib
import LexiPeekCore
import LexiPeekKit
import ServiceManagement
import SwiftUI

// Settings: a standard macOS settings window, one toolbar tab per pane (General · Translation · Packs · Permissions).
enum SettingsPane: String, CaseIterable {
    case general, translation, packs, permissions

    var title: String { rawValue.capitalized }
    var symbol: String {
        switch self {
        case .general: "gearshape"
        case .translation: "character.bubble"
        case .packs: "shippingbox"
        case .permissions: "hand.raised"
        }
    }
}

final class SettingsState: ObservableObject {
    @Published var trusted = Selection.trusted
    @Published var hotkeyErrors: [String: String] = [:]
}

enum SettingsWindow {
    static func make(state: SettingsState, client: CoreClient) -> NSWindow {
        let tabs = NSTabViewController()
        tabs.tabStyle = .toolbar
        for pane in SettingsPane.allCases {
            let vc = NSHostingController(rootView: view(pane, state: state, client: client))
            vc.sizingOptions = .preferredContentSize
            let item = NSTabViewItem(viewController: vc)
            item.label = pane.title
            item.image = NSImage(systemSymbolName: pane.symbol, accessibilityDescription: pane.title)
            item.identifier = pane.rawValue
            tabs.addTabViewItem(item)
        }
        let w = NSWindow(contentViewController: tabs)
        w.styleMask = [.titled, .closable]
        w.toolbarStyle = .preference
        w.isReleasedWhenClosed = false
        w.center()
        return w
    }

    static func select(_ w: NSWindow?, _ pane: SettingsPane) {
        guard let tabs = w?.contentViewController as? NSTabViewController else { return }
        tabs.selectedTabViewItemIndex = SettingsPane.allCases.firstIndex(of: pane) ?? 0
        w?.title = pane.title
    }

    static func view(_ pane: SettingsPane, state: SettingsState, client: CoreClient) -> AnyView {
        switch pane {
        case .general: AnyView(GeneralPane(prefs: .shared, state: state))
        case .translation: AnyView(TranslationPane(prefs: .shared))
        case .packs: AnyView(PacksPane(client: client))
        case .permissions: AnyView(PermissionsPane(state: state))
        }
    }
}

// The common frame: grouped form, fixed width, height of its content.
private struct Pane<Content: View>: View {
    @ViewBuilder var content: Content
    var body: some View {
        Form { content }
            .formStyle(.grouped)
            .scrollDisabled(true)
            .frame(width: 520)
            .fixedSize(horizontal: false, vertical: true)
    }
}

struct GeneralPane: View {
    @ObservedObject var prefs: Prefs
    @ObservedObject var state: SettingsState
    @State private var login = SMAppService.mainApp.status == .enabled

    var body: some View {
        Pane {
            Section {
                LabeledContent("Translate selection") {
                    ShortcutField(shortcut: $prefs.translateKey, other: prefs.inputKey, error: state.hotkeyErrors["translate"])
                }
                LabeledContent("Type text") {
                    ShortcutField(shortcut: $prefs.inputKey, other: prefs.translateKey, error: state.hotkeyErrors["input"])
                }
                Toggle("Open at login", isOn: $login).onChange(of: login) { _, on in
                    try? on ? SMAppService.mainApp.register() : SMAppService.mainApp.unregister()
                    login = SMAppService.mainApp.status == .enabled
                    prefs.openAtLogin = login
                }
            }
            Section {
                Toggle("Show an icon next to selected text", isOn: $prefs.selectionIcon)
                Toggle("Keep a card open after moving it", isOn: $prefs.pinMoved)
            } footer: {
                Note("The icon opens the card with a click, without the hotkey. It reads the selection by Accessibility "
                    + "only and never touches the clipboard.")
            }
            Section {
                AppList(ids: $prefs.copyApps)
            } header: {
                Text("Read the selection with ⌘C in")
            } footer: {
                Note("For apps that do not show their selection to Accessibility. The clipboard is put back after.")
            }
            Section {
                AppList(ids: $prefs.offApps)
            } header: {
                Text("The hotkey and the icon do nothing in")
            }
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

struct TranslationPane: View {
    @ObservedObject var prefs: Prefs

    private var answers: [String] {
        let a = lexipeek.app.answers(std.string()).map { String($0) }
        return a.contains(prefs.answer) ? a : [prefs.answer] + a
    }

    var body: some View {
        Pane {
            Section {
                Picker("Translate into", selection: $prefs.answer) {
                    ForEach(answers, id: \.self) { code in
                        Text(String(lexipeek.app.language(std.string(code)))).tag(code)
                    }
                }
                Picker("Method", selection: $prefs.method) {
                    Text("The first available").tag("")
                    Divider()
                    Text("Fast · Mozilla").tag("fast")
                    Text("Direct · OPUS-MT").tag("direct")
                    Text("AI model · Hy-MT2").tag("llm")
                }
            } footer: {
                Note("The card can switch the method for one text. A method this Mac cannot run is dimmed there, with the reason.")
            }
        }
    }
}

struct PermissionsPane: View {
    @ObservedObject var state: SettingsState

    var body: some View {
        Pane {
            Section {
                LabeledContent("Accessibility") {
                    if state.trusted {
                        Label("Allowed", systemImage: "checkmark.circle.fill").foregroundStyle(.green)
                    } else {
                        Button("Allow…") {
                            let key = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
                            AXIsProcessTrustedWithOptions([key: true] as CFDictionary)
                            NSWorkspace.shared.open(URL(string:
                                "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
                        }
                    }
                }
            } footer: {
                Note("LexiPeek reads the selected text of the app in front when you press the hotkey; nothing else is "
                    + "read and nothing leaves this Mac. Without it, ⌥A, the Service “Look up in LexiPeek” and "
                    + "lexipeek:// links still work. If it stops working after an update, remove LexiPeek from the "
                    + "list in System Settings and add it again.")
            }
        }
    }
}

// A footer note: small, secondary, left-aligned.
struct Note: View {
    var text: String
    init(_ text: String) { self.text = text }
    var body: some View {
        Text(text)
            .font(.caption)
            .foregroundStyle(.secondary)
            .multilineTextAlignment(.leading)
            .frame(maxWidth: .infinity, alignment: .leading)
            .fixedSize(horizontal: false, vertical: true)
    }
}
