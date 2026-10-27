import AppKit
import CxxStdlib
import LexiPeekCore
import SwiftUI

// The LexiPeek window: Look up · Translate text · Notes (Words · Phrases · History); the app is in the Dock while it is open.
enum MainPage: Hashable {
    case lookup, translate, notes(lexipeek.NotesTab)
}

final class MainModel: ObservableObject {
    @Published var page = MainPage.lookup
    @Published var query = ""
    let card: CardModel
    let notes: NotesModel
    let text: TextModel

    init(client: CoreClient) {
        card = CardModel(client: client)
        notes = NotesModel(client: client)
        text = TextModel(client: client)
        notes.open = { [weak self] t in self?.look(t) }
    }

    func look(_ t: String) {
        query = t
        page = .lookup
        card.reset(input: false)
        card.lookup(t, source: .input)
    }

    func show(_ p: MainPage) {
        page = p
        if case .notes(let tab) = p { notes.tab = tab }
    }
}

struct MainView: View {
    @ObservedObject var model: MainModel
    @ObservedObject var notes: NotesModel
    @FocusState private var focused: Bool

    var body: some View {
        NavigationSplitView {
            List(selection: Binding(get: { model.page }, set: { if let p = $0 { model.show(p) } })) {
                Label(L.t("win_lookup"), systemImage: "magnifyingglass").tag(MainPage.lookup)
                Label(L.t("win_translate"), systemImage: "text.alignleft").tag(MainPage.translate)
                Section(L.t("tab_words")) {
                    ForEach(Array(NotesModel.tabs.enumerated()), id: \.offset) { i, tab in
                        Label(i < notes.counts.count ? notes.counts[i] : "", systemImage: ["star", "text.quote", "clock"][i]).tag(MainPage.notes(tab))
                    }
                }
            }
            .navigationSplitViewColumnWidth(min: 170, ideal: 190)
        } detail: {
            switch model.page {
            case .lookup: lookup
            case .translate: TextPane(model: model.text)
            case .notes: NotesList(model: notes).onAppear { notes.refresh() }
            }
        }
        .frame(minWidth: 720, minHeight: 460)
        .onAppear { notes.refresh() }
    }

    private var lookup: some View {
        VStack(alignment: .leading, spacing: 0) {
            TextField(L.t("card_input_hint"), text: $model.query)
                .textFieldStyle(.roundedBorder)
                .controlSize(.large)
                .focused($focused)
                .onSubmit { model.look(model.query) }
                .padding(14)
            Divider()
            if model.card.card != nil || model.card.loading {
                CardView(model: model.card, embedded: true).frame(minHeight: 0, maxHeight: .infinity, alignment: .top)
            } else {
                Spacer()
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .onAppear { focused = true }
    }
}

// Translate text: a text left, its translation right; its language, the answer language and the method as menus.
final class TextModel: ObservableObject {
    @Published var text = ""
    @Published var result = ""
    @Published var error = ""
    @Published var working = false
    @Published var card: lexipeek.Card?
    @Published private var state = lexipeek.CardState()
    private let client: CoreClient
    private var request = 0

    init(client: CoreClient) { self.client = client }

    var look: CardLook? { card.map { CardLook(lexipeek.card_view($0, state)) } }

    // ⌘↩ or a menu: the text looked up (its language, the methods), then translated by the picked or the set method.
    func go(lang: String = "", answer: String = "", method: lexipeek.Method? = nil) {
        let t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !t.isEmpty else { return }
        working = true
        error = ""
        let keepLang = lang.isEmpty ? "" : lang, keepAnswer = answer.isEmpty ? String(card?.answer ?? "") : answer
        request = client.lookup(t, source: .text, lang: keepLang, answer: keepAnswer) { [weak self] id, c in
            guard let self, id == self.request else { return }
            var s = lexipeek.first_state(c)
            s.tab = .translate
            if let method, s.has_method { s.method = method }
            self.card = c
            self.state = s
            let v = lexipeek.card_view(c, s)
            guard s.has_method else {
                // Same language, no pack, nothing installed: the card's own words say why.
                self.result = ""
                self.error = v.rows.first.map { String($0.text) } ?? ""
                self.working = false
                return
            }
            self.client.translate(id, method: s.method) { t in
                guard id == self.request else { return }
                self.working = false
                if t.ok() { self.result = String(t.text) } else { self.error = String(t.error) }
            }
        }
    }
}

struct TextPane: View {
    @ObservedObject var model: TextModel

    var body: some View {
        HSplitView {
            VStack(alignment: .leading, spacing: 6) {
                bar { languages }
                TextEditor(text: $model.text)
                    .font(.body)
                    .scrollContentBackground(.hidden)
                    .overlay(alignment: .topLeading) {
                        if model.text.isEmpty { Text(L.t("win_text_hint")).foregroundStyle(.tertiary).padding(.leading, 5).allowsHitTesting(false) }
                    }
            }
            .padding(12)
            .frame(minWidth: 260)
            VStack(alignment: .leading, spacing: 6) {
                bar {
                    answers
                    methods
                    Spacer()
                    if model.working { ProgressView().controlSize(.small) }
                    Button(L.t("win_go")) { model.go() }.keyboardShortcut(.return, modifiers: .command).fixedSize()
                }
                ScrollView {
                    Text(model.error.isEmpty ? model.result : model.error)
                        .foregroundStyle(model.error.isEmpty ? .primary : .secondary)
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                if !model.result.isEmpty && model.error.isEmpty {
                    HStack {
                        Spacer()
                        Button {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(model.result, forType: .string)
                        } label: { Image(systemName: "doc.on.doc") }
                            .buttonStyle(.borderless)
                            .help(L.t("card_copy"))
                    }
                }
            }
            .padding(12)
            .frame(minWidth: 260)
        }
    }

    private func bar(@ViewBuilder _ content: () -> some View) -> some View {
        HStack(spacing: 10) { content() }.frame(height: 24)
    }

    @ViewBuilder
    private var languages: some View {
        if let look = model.look, !look.books.isEmpty {
            Menu(look.books.first { $0.on }?.title ?? L.t("card_text_in")) {
                ForEach(look.books) { l in Button(l.title) { model.go(lang: l.id) } }
            }
            .fixedSize()
        } else {
            Text(model.look?.badge ?? "").foregroundStyle(.secondary)
        }
    }

    @ViewBuilder
    private var answers: some View {
        if let look = model.look, !look.answers.isEmpty {
            Menu(look.answers.first { $0.on }?.title ?? L.t("card_into")) {
                ForEach(look.answers) { l in Button(l.title) { model.go(answer: l.id) } }
            }
            .fixedSize()
        }
    }

    @ViewBuilder
    private var methods: some View {
        if let look = model.look, !look.methods.isEmpty {
            Menu(look.menuTitle) {
                ForEach(look.methods.filter { !$0.ai }) { m in
                    Button(m.title) {
                        Prefs.shared.method = String(lexipeek.app.method_id(m.method))
                        model.go(method: m.method)
                    }
                    .disabled(!m.enabled)
                }
            }
            .fixedSize()
        }
    }
}

enum MainWindow {
    static func make(model: MainModel) -> NSWindow {
        let host = NSHostingController(rootView: MainView(model: model, notes: model.notes))
        host.sizingOptions = []  // the window keeps its size; a long card scrolls inside
        let w = NSWindow(contentViewController: host)
        w.title = "LexiPeek"
        w.styleMask = [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView]
        w.setContentSize(NSSize(width: 860, height: 560))
        w.setFrameAutosaveName("LexiPeekWindow")
        w.isReleasedWhenClosed = false
        w.center()
        return w
    }
}
