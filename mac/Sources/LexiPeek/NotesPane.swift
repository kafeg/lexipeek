import AppKit
import LexiPeekCore
import SwiftUI

extension Notification.Name {
    // A card saved, removed or looked up something: the window's Notes read their list again.
    static let notesChanged = Notification.Name("lexipeek.notesChanged")
    // A settings pane to open (object: (lexipeek.SettingsSection, tab)): a card's button, Help.
    static let openPane = Notification.Name("lexipeek.openPane")
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
    var page = 0, pages = 1
    var pager = ""

    init() {}

    init(_ v: lexipeek.NotesView) {
        tabs = v.tabs.map { String($0) }
        rows = v.rows.map { Row(text: String($0.text), line: String($0.line), looks: String($0.looks)) }
        empty = String(v.empty)
        clear = v.clear
        page = Int(v.page)
        pages = Int(v.pages)
        pager = String(v.pager)
    }
}

final class NotesModel: ObservableObject {
    static let tabs: [lexipeek.NotesTab] = [.words, .phrases, .history]

    @Published var tab = lexipeek.NotesTab.words {
        didSet {
            page = 0
            refresh()
        }
    }
    private var page = 0
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

    func refresh() { look = NotesLook(client.notes(tab, page: page)) }

    func turn(_ by: Int) {
        page = max(0, min(look.pages - 1, look.page + by))
        refresh()
    }

    // "Words (3)", "Phrases (1)", "History (12)": the window's sidebar.
    var counts: [String] { look.tabs }

    func remove(_ text: String) {
        client.removeNote(tab, text)
        refresh()
    }

    func clearHistory() {
        client.clearHistory()
        refresh()
    }

    // What the last Export / Import / TSV said.
    @Published var said = ""

    // A save panel in Downloads with the file's name; nil when cancelled.
    private func saveTo(_ name: String) -> String? {
        let p = NSSavePanel()
        p.nameFieldStringValue = name
        p.directoryURL = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
        return p.runModal() == .OK ? p.url?.path : nil
    }

    func exportTSV() {
        guard let f = saveTo(String(lexipeek.app.notes_file(tab))) else { return }
        client.exportTSV(tab, f) { [weak self] in self?.said = $0 }
    }

    func backup() {
        guard let f = saveTo(String(lexipeek.app.backup_file())) else { return }
        client.backup(f) { [weak self] in self?.said = $0 }
    }

    func restore() {
        let p = NSOpenPanel()
        p.canChooseDirectories = false
        p.allowsMultipleSelection = false
        p.directoryURL = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
        guard p.runModal() == .OK, let f = p.url?.path else { return }
        client.restore(f) { [weak self] in
            self?.said = $0
            self?.refresh()
            NotificationCenter.default.post(name: .notesChanged, object: nil)
        }
    }
}

// The window's Notes: the open page's rows (Words · Phrases · History), newest first; a click opens
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
            HStack {
                if !model.look.rows.isEmpty { Button(L.t("tsv_btn")) { model.exportTSV() }.help(L.t("tsv_d")) }
                Spacer()
                if !model.look.pager.isEmpty {
                    Button("◀") { model.turn(-1) }.buttonStyle(.borderless).disabled(model.look.page == 0)
                    Text(model.look.pager).foregroundStyle(.secondary).monospacedDigit()
                    Button("▶") { model.turn(1) }.buttonStyle(.borderless).disabled(model.look.page + 1 >= model.look.pages)
                    Spacer()
                }
                if model.look.clear { Button(L.t("clear_history_more")) { confirmClear = true } }
            }
            Divider()
            // The whole store to one file and back (InkPeek's backups too).
            HStack(alignment: .top) {
                Text(L.t("backup_d")).font(.caption).foregroundStyle(.secondary).lineLimit(3)
                    .frame(maxWidth: .infinity, alignment: .leading)
                Button(L.t("export_btn")) { model.backup() }
                Button(L.t("import_btn")) { model.restore() }
            }
            if !model.said.isEmpty { Text(model.said).font(.caption).foregroundStyle(.secondary).textSelection(.enabled) }
        }
        .padding(14)
        .confirmationDialog(L.t("clear_history_q"), isPresented: $confirmClear) {
            Button(L.t("clear_history"), role: .destructive) { model.clearHistory() }
        } message: {
            Text(L.t("notes_clear_d"))
        }
    }

    // A list's or a passage's lines run on in a one-line row (cut at its first line break, it said only "Einkaufsliste:…").
    private static func oneLine(_ s: String) -> String { s.split(whereSeparator: \.isNewline).joined(separator: " ") }

    private func row(_ r: NotesLook.Row) -> some View {
        HStack(spacing: 8) {
            Button { model.open(r.text) } label: {
                VStack(alignment: .leading, spacing: 1) {
                    Text(Self.oneLine(r.text)).lineLimit(1).truncationMode(.tail)
                    Text(Self.oneLine(r.line)).font(.callout).foregroundStyle(.secondary).lineLimit(1).truncationMode(.tail)
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
                .help(model.tab == .history ? L.t("forget") : L.t("unsave_word"))
        }
        .padding(.vertical, 2)
    }
}
