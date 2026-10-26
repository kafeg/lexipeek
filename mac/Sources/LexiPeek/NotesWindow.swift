import AppKit
import LexiPeekCore
import SwiftUI

extension Notification.Name {
    // A card saved, removed or looked up something: an open Notes window reads its list again.
    static let notesChanged = Notification.Name("lexipeek.notesChanged")
}

// What Notes draws, copied out of the core's NotesView.
struct NotesLook {
    struct Row: Identifiable {
        var id: String { text }
        var text: String
        var line: String
        var looks: String
    }

    var tabs: [String] = []
    var rows: [Row] = []
    var empty = ""
    var clear = false

    init() {}

    init(_ v: lexipeek.NotesView) {
        tabs = v.tabs.map { String($0) }
        rows = v.rows.map { Row(text: String($0.text), line: String($0.line), looks: String($0.looks)) }
        empty = String(v.empty)
        clear = v.clear
    }
}

final class NotesModel: ObservableObject {
    static let tabs: [lexipeek.NotesTab] = [.words, .phrases, .history]

    @Published var tab = lexipeek.NotesTab.words { didSet { refresh() } }
    @Published private(set) var look = NotesLook()
    let client: CoreClient
    var open: (String) -> Void = { _ in }
    private var observer: Any?

    init(client: CoreClient) {
        self.client = client
        observer = NotificationCenter.default.addObserver(forName: .notesChanged, object: nil, queue: .main) { [weak self] _ in
            self?.refresh()
        }
    }

    func refresh() { look = NotesLook(client.notes(tab)) }

    func remove(_ text: String) {
        client.removeNote(tab, text)
        refresh()
    }

    func clearHistory() {
        client.clearHistory()
        refresh()
    }
}

// Notes: Words · Phrases · History, newest first; a click opens the card, ✕ removes, "Clear history" under History.
struct NotesView: View {
    @ObservedObject var model: NotesModel
    @State private var confirmClear = false

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Picker("", selection: $model.tab) {
                ForEach(Array(NotesModel.tabs.enumerated()), id: \.offset) { i, t in
                    Text(i < model.look.tabs.count ? model.look.tabs[i] : "").tag(t)
                }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            if model.look.rows.isEmpty {
                Text(model.look.empty).foregroundStyle(.secondary).frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List(model.look.rows) { row($0) }
                    .listStyle(.inset(alternatesRowBackgrounds: true))
            }
            if model.look.clear {
                HStack {
                    Spacer()
                    Button("Clear history…") { confirmClear = true }
                }
            }
        }
        .padding(14)
        .frame(minWidth: 380, idealWidth: 460, minHeight: 320, idealHeight: 520)
        .confirmationDialog("Clear the whole history?", isPresented: $confirmClear) {
            Button("Clear history", role: .destructive) { model.clearHistory() }
        } message: {
            Text("Saved words and phrases stay.")
        }
    }

    private func row(_ r: NotesLook.Row) -> some View {
        HStack(spacing: 8) {
            Button { model.open(r.text) } label: {
                VStack(alignment: .leading, spacing: 1) {
                    Text(r.text).lineLimit(1).truncationMode(.tail)
                    Text(r.line).font(.callout).foregroundStyle(.secondary).lineLimit(1).truncationMode(.tail)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .help(r.text)
            if !r.looks.isEmpty { Text(r.looks).font(.caption).foregroundStyle(.secondary).monospacedDigit() }
            Button { model.remove(r.text) } label: { Image(systemName: "xmark") }
                .buttonStyle(.borderless)
                .foregroundStyle(.secondary)
                .help(model.tab == .history ? "Remove from the history" : "Remove from Notes")
        }
        .padding(.vertical, 2)
    }
}

enum NotesWindow {
    static func make(model: NotesModel) -> NSWindow {
        let w = NSWindow(contentViewController: NSHostingController(rootView: NotesView(model: model)))
        w.title = "Notes"
        w.styleMask = [.titled, .closable, .resizable, .miniaturizable]
        w.isReleasedWhenClosed = false
        w.setContentSize(NSSize(width: 460, height: 520))
        w.setFrameAutosaveName("Notes")
        w.center()
        return w
    }
}
