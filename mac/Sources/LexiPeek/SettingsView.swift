import AppKit
import LexiPeekKit
import ServiceManagement
import SwiftUI

// Settings: General · Packs · Translation · Permissions.
struct SettingsView: View {
    enum Pane: Hashable { case general, packs, translation, permissions }

    @ObservedObject var prefs = Prefs.shared
    @ObservedObject var state: SettingsState

    var body: some View {
        TabView(selection: $state.pane) {
            GeneralPane(prefs: prefs, state: state).tabItem { Label("General", systemImage: "gearshape") }.tag(Pane.general)
            PacksPane().tabItem { Label("Packs", systemImage: "shippingbox") }.tag(Pane.packs)
            TranslationPane(prefs: prefs).tabItem { Label("Translation", systemImage: "character.bubble") }.tag(Pane.translation)
            PermissionsPane(state: state).tabItem { Label("Permissions", systemImage: "hand.raised") }.tag(Pane.permissions)
        }
        .frame(width: 520, height: 420)
    }
}

final class SettingsState: ObservableObject {
    @Published var pane = SettingsView.Pane.general
    @Published var trusted = Selection.trusted
    @Published var hotkeyErrors: [String: String] = [:]
}

private struct GeneralPane: View {
    @ObservedObject var prefs: Prefs
    @ObservedObject var state: SettingsState
    @State private var login = SMAppService.mainApp.status == .enabled

    var body: some View {
        Form {
            Section("Hotkeys") {
                LabeledContent("Translate selection") {
                    ShortcutField(shortcut: $prefs.translateKey, other: prefs.inputKey, error: state.hotkeyErrors["translate"])
                }
                LabeledContent("Type text") {
                    ShortcutField(shortcut: $prefs.inputKey, other: prefs.translateKey, error: state.hotkeyErrors["input"])
                }
            }
            Section {
                Toggle("Open at login", isOn: $login).onChange(of: login) { _, on in
                    try? on ? SMAppService.mainApp.register() : SMAppService.mainApp.unregister()
                    login = SMAppService.mainApp.status == .enabled
                }
            }
            Section("Apps where the hotkey does nothing") { AppList(ids: $prefs.offApps) }
            Section {
                AppList(ids: $prefs.copyApps)
            } header: {
                Text("Apps where Cmd+C may read the selection")
            } footer: {
                Text("For apps that do not show their selection to Accessibility. The clipboard is restored after.")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
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
            let s = Shortcut(keyCode: UInt32(e.keyCode), modifiers: m, key: e.charactersIgnoringModifiers ?? "")
            guard s.isValid else { NSSound.beep(); return nil }
            if s.keyCode == other.keyCode && s.modifiers == other.modifiers { conflict = true } else { shortcut = s }
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

// Bundle ids, added from the running apps.
private struct AppList: View {
    @Binding var ids: [String]

    var body: some View {
        ForEach(ids, id: \.self) { id in
            HStack {
                Text(name(id))
                Spacer()
                Button { ids.removeAll { $0 == id } } label: { Image(systemName: "minus.circle") }.buttonStyle(.borderless)
            }
        }
        Menu("Add an app") {
            ForEach(running, id: \.self) { id in Button(name(id)) { ids.append(id) } }
        }
        .fixedSize()
    }

    private var running: [String] {
        NSWorkspace.shared.runningApplications
            .filter { $0.activationPolicy == .regular }
            .compactMap(\.bundleIdentifier)
            .filter { !ids.contains($0) && $0 != Bundle.main.bundleIdentifier }
    }

    private func name(_ id: String) -> String {
        guard let url = NSWorkspace.shared.urlForApplication(withBundleIdentifier: id) else { return id }
        return FileManager.default.displayName(atPath: url.path)
    }
}

private struct PacksPane: View {
    var body: some View {
        Form {
            LabeledContent("Data folder") {
                Button(CoreClient.dataDir) { NSWorkspace.shared.open(URL(fileURLWithPath: CoreClient.dataDir)) }
                    .buttonStyle(.link)
            }
            Text("Packs: the list with sizes, downloads and removal come with the core's pack manager.")
                .foregroundStyle(.secondary)
        }
        .formStyle(.grouped)
    }
}

private struct TranslationPane: View {
    @ObservedObject var prefs: Prefs

    var body: some View {
        Form {
            Picker("Method", selection: $prefs.method) {
                Text("The first available").tag("")
                Text("Fast · Mozilla").tag("fast")
                Text("Direct · OPUS-MT").tag("direct")
                Text("AI model · Hy-MT2").tag("llm")
            }
            TextField("Translate into", text: $prefs.answer, prompt: Text("ru"))
            Text("A method this Mac cannot run is shown dimmed in the card, with the reason.")
                .font(.caption).foregroundStyle(.secondary)
        }
        .formStyle(.grouped)
    }
}

private struct PermissionsPane: View {
    @ObservedObject var state: SettingsState

    var body: some View {
        Form {
            Section {
                LabeledContent("Accessibility") {
                    Label(state.trusted ? "Granted" : "Not granted",
                          systemImage: state.trusted ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                        .foregroundStyle(state.trusted ? .green : .orange)
                }
                if !state.trusted {
                    Button("Open System Settings") {
                        let key = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
                        AXIsProcessTrustedWithOptions([key: true] as CFDictionary)
                        NSWorkspace.shared.open(URL(string:
                            "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")!)
                    }
                }
            } footer: {
                VStack(alignment: .leading, spacing: 6) {
                    Text("LexiPeek reads the selected text of the app in front when you press the hotkey. "
                        + "That needs Accessibility; nothing else is read, nothing leaves this Mac.")
                    Text("Without it: ⌥A, the Service “Look up in LexiPeek” and lexipeek:// links still work.")
                    Text("After a macOS update or a new build, remove LexiPeek from the list and add it again. "
                        + "Run it from /Applications.")
                }
                .font(.caption).foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
    }
}
