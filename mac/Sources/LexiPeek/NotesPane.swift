import AppKit
import LexiPeekCore
import SwiftUI

extension Notification.Name {
    // A card saved, removed or looked up something: Settings → Notes reads its list again.
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

// Settings → Notes: the open tab's rows (Words · Phrases · History are the section's tabs), newest first; a click opens
// the card, ✕ removes, "Clear history" under History.
struct NotesList: View {
    @ObservedObject var model: NotesModel
    @State private var confirmClear = false

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if model.look.rows.isEmpty {
                Text(model.look.empty).foregroundStyle(.secondary).frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List(model.look.rows) { row($0) }
                    .listStyle(.inset)
            }
            if model.look.clear {
                HStack {
                    Spacer()
                    Button("Clear history…") { confirmClear = true }
                }
            }
        }
        .padding(14)
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
