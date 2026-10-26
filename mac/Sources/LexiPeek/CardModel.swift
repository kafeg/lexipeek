import Foundation
import LexiPeekCore

// What the card draws, copied out of the core's CardView.
struct CardLook {
    struct Row {
        var style: lexipeek.CardRow.Style
        var label: String
        var text: String
        var html: String
    }

    struct Item: Identifiable {
        var id: lexipeek.Method
        var title: String
        var enabled: Bool
    }

    var style = lexipeek.HeaderStyle.word
    var title = ""
    var subtitle = ""
    var tooltip = ""
    var badge = ""
    var wide = false
    var tabs = false
    var tab = lexipeek.CardTab.translate
    var rows: [Row] = []
    var entryToggle = ""
    var menu = false
    var menuTitle = ""
    var methods: [Item] = []
    var looks = ""

    init(_ v: lexipeek.CardView) {
        style = v.style
        title = String(v.title)
        subtitle = String(v.subtitle)
        tooltip = String(v.tooltip)
        badge = String(v.badge)
        wide = v.wide
        tabs = v.tabs
        tab = v.tab
        rows = v.rows.map { Row(style: $0.style, label: String($0.label), text: String($0.text), html: String($0.html)) }
        entryToggle = String(v.entry_toggle)
        menu = v.menu
        menuTitle = String(v.menu_title)
        methods = v.methods.map { Item(id: $0.method, title: String($0.title), enabled: $0.enabled) }
        looks = String(v.looks)
    }
}

// The card's state: one request at a time, its translation by the chosen method; the core says what it shows.
final class CardModel: ObservableObject {
    @Published var input = ""
    @Published var showInput = false
    @Published var card: lexipeek.Card?
    @Published var state = lexipeek.CardState()
    @Published var pinned = false
    // The hotkey found no selection in this app (bundle id, name): the card says so and offers Cmd+C there.
    @Published var unread: (id: String, name: String)?
    var retry: (() -> Void)?

    let client: CoreClient
    private var request = 0

    init(client: CoreClient) { self.client = client }

    var look: CardLook? { card.map { CardLook(lexipeek.card_view($0, state)) } }

    func reset(input: Bool) {
        request = 0
        self.input = ""
        showInput = input
        card = nil
        state = lexipeek.CardState()
        unread = nil
    }

    func lookup(_ text: String, source: lexipeek.Source) {
        let t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !t.isEmpty else { return }
        state = lexipeek.CardState()
        request = client.lookup(t, source: source) { [weak self] id, card in
            guard let self, id == self.request else { return }
            var s = lexipeek.first_state(card)
            if Snapshot.dictionary { s.tab = .dictionary }
            if let m = Snapshot.method, s.has_method { s.method = m }
            self.card = card
            self.state = s
            if s.has_method { self.translate(s.method) }
        }
    }

    func translate(_ m: lexipeek.Method) {
        guard request != 0 else { return }
        state.has_method = true
        state.method = m
        state.translating = true
        let id = request
        client.translate(id, method: m) { [weak self] t in
            guard let self, id == self.request, self.state.has_method, m == self.state.method else { return }
            self.state.translation = t
            self.state.translated = true
            self.state.translating = false
        }
    }

    // A method picked in the card: this text again with it, and the next cards too (Settings → Translation).
    func choose(_ m: lexipeek.Method) {
        Prefs.shared.method = String(lexipeek.app.method_id(m))
        translate(m)
    }

    func switchTab() { state.tab = state.tab == .dictionary ? .translate : .dictionary }
    func toggleEntry() { state.full_entry.toggle() }
}
