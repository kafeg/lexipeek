import AppKit
import CxxStdlib
import LexiPeekCore
import SwiftUI

// The LexiPeek window: Look up (a word, a phrase or a pasted text: its card here) · Notes (Words · Phrases · History);
// the app is in the Dock while it is open.
enum MainPage: Hashable {
    case lookup, notes(lexipeek.NotesTab)
}

final class MainModel: ObservableObject {
    @Published var page = MainPage.lookup
    @Published var query = ""
    @Published var memory = ""  // the sidebar's line: what is in memory
    var openSettings: () -> Void = {}
    var openAbout: () -> Void = {}
    private let client: CoreClient
    let card: CardModel
    let notes: NotesModel

    init(client: CoreClient) {
        self.client = client
        card = CardModel(client: client)
        notes = NotesModel(client: client)
        notes.open = { [weak self] t in self?.look(t) }
    }

    func look(_ t: String) {
        query = t
        page = .lookup
        card.reset(input: false)
        card.lookup(t, source: .input)
    }

    func refreshMemory() {
        let m = client.memory()
        memory = m.held ? "\(String(m.line)) · \(String(m.detail))" : String(m.line)
    }

    func show(_ p: MainPage) {
        page = p
        if case .notes(let tab) = p { notes.tab = tab }
    }
}

struct MainView: View {
    @ObservedObject var model: MainModel
    @ObservedObject var notes: NotesModel
    @ObservedObject var card: CardModel
    @FocusState private var focused: Bool

    var body: some View {
        NavigationSplitView {
            List(selection: Binding(get: { model.page }, set: { if let p = $0 { model.show(p) } })) {
                Label(L.t("win_lookup"), systemImage: "magnifyingglass").tag(MainPage.lookup)
                Section(L.t("tab_words")) {
                    ForEach(Array(NotesModel.tabs.enumerated()), id: \.offset) { i, tab in
                        Label(i < notes.counts.count ? notes.counts[i] : "", systemImage: ["star", "text.quote", "clock"][i]).tag(MainPage.notes(tab))
                    }
                }
            }
            .navigationSplitViewColumnWidth(min: 190, ideal: 210)
            // Under the list: what is in memory, Settings and About.
            .safeAreaInset(edge: .bottom) {
                VStack(alignment: .leading, spacing: 8) {
                    Text(model.memory).font(.caption).foregroundStyle(.secondary).lineLimit(3)
                    HStack {
                        Button { model.openSettings() } label: { Label(L.t("settings_more"), systemImage: "gearshape") }
                        Spacer()
                        Button { model.openAbout() } label: { Image(systemName: "info.circle") }.help(L.t("menu_about"))
                    }
                    .buttonStyle(.borderless)
                }
                .padding(12)
            }
        } detail: {
            switch model.page {
            case .lookup: lookup
            case .notes: NotesList(model: notes).onAppear { notes.refresh() }
            }
        }
        .frame(minWidth: 720, minHeight: 460)
        .onAppear {
            notes.refresh()
            model.refreshMemory()
        }
        .onReceive(Timer.publish(every: 15, on: .main, in: .common).autoconnect()) { _ in model.refreshMemory() }
    }

    private var lookup: some View {
        VStack(alignment: .leading, spacing: 0) {
            // Grows to a few lines for a pasted text; Return looks it up, ⌥Return a new line.
            TextField(L.t("card_input_hint"), text: $model.query, axis: .vertical)
                .lineLimit(1...6)
                .textFieldStyle(.roundedBorder)
                .controlSize(.large)
                .focused($focused)
                .onSubmit { model.look(model.query) }
                .padding(14)
            Divider()
            if card.card != nil || card.looking {
                CardView(model: model.card, embedded: true).frame(minHeight: 0, maxHeight: .infinity, alignment: .top)
            } else {
                // Nothing looked up yet: what to do here and from any app.
                Text(L.t("win_hint", Prefs.shared.translateKey.label, Prefs.shared.inputKey.label))
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 420)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .onAppear { focused = true }
    }
}

enum MainWindow {
    static func make(model: MainModel) -> NSWindow {
        let host = NSHostingController(rootView: MainView(model: model, notes: model.notes, card: model.card))
        host.sizingOptions = []  // the window keeps its size; a long card scrolls inside
        let w = NSWindow(contentViewController: host)
        w.title = ProcessInfo.processInfo.processName
        w.styleMask = [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView]
        w.setContentSize(NSSize(width: 860, height: 560))
        w.isReleasedWhenClosed = false
        w.center()
        return w
    }
}
