import CxxStdlib
import Foundation
import LexiPeekCore

// What the card draws, copied out of the core's CardView.
struct CardLook {
    struct Row {
        var style: lexipeek.CardRow.Style
        var label: String
        var text: String
        var html: String
        var link: String
        var open: String
        var entry: Int
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
    var back = ""
    var pager = ""
    var prev = false
    var next = false
    var footer = false
    var star = ""
    var starHelp = ""

    init(_ v: lexipeek.CardView) {
        style = v.style
        title = String(v.title)
        subtitle = String(v.subtitle)
        tooltip = String(v.tooltip)
        badge = String(v.badge)
        wide = v.wide
        tabs = v.tabs
        tab = v.tab
        rows = v.rows.map {
            Row(style: $0.style, label: String($0.label), text: String($0.text), html: String($0.html), link: String($0.link),
                open: String($0.open), entry: Int($0.entry))
        }
        entryToggle = String(v.entry_toggle)
        menu = v.menu
        menuTitle = String(v.menu_title)
        methods = v.methods.map { Item(id: $0.method, title: String($0.title), enabled: $0.enabled) }
        back = String(v.back)
        pager = String(v.pager)
        prev = v.prev
        next = v.next
        footer = v.footer
        star = String(v.star)
        starHelp = String(v.star_help)
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
    // The cards this one was opened from (a gloss word, a section's word): "←" goes back to them as they were.
    private var trail: [(card: lexipeek.Card, state: lexipeek.CardState)] = []

    init(client: CoreClient) { self.client = client }

    var look: CardLook? { card.map { CardLook(lexipeek.card_view($0, state)) } }

    func reset(input: Bool) {
        request = 0
        self.input = ""
        showInput = input
        card = nil
        state = lexipeek.CardState()
        unread = nil
        trail = []
    }

    func lookup(_ text: String, source: lexipeek.Source, keep: Bool = false) {
        let t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !t.isEmpty else { return }
        if !keep { trail = [] }
        let before = trail.last.map { String($0.card.text) } ?? ""
        state = lexipeek.CardState()
        request = client.lookup(t, source: source) { [weak self] id, card in
            guard let self, id == self.request else { return }
            var s = lexipeek.first_state(card)
            if Snapshot.dictionary { s.tab = .dictionary }
            if let m = Snapshot.method, s.has_method { s.method = m }
            s.before = std.string(before)
            self.card = card
            self.state = s
            NotificationCenter.default.post(name: .notesChanged, object: nil)
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

    // ☆ / ★: saves the card's text in Notes (a word as its base form) with its translation, or removes it.
    func toggleSaved() {
        guard let card, !lexipeek.card_view(card, state).star.empty() else { return }
        state.saved = client.toggleSaved(card, translation: state.translated && state.translation.ok() ? String(state.translation.text) : "")
        NotificationCenter.default.post(name: .notesChanged, object: nil)
    }

    // A word clicked in the card (the gloss, a section): its card here, "←" comes back to this one.
    func open(_ word: String) {
        guard let card else { return }
        trail.append((card, state))
        lookup(word, source: .input, keep: true)
    }

    // "←": from a section to its entry, else to the card this one was opened from, as it was.
    func back() {
        if !String(state.section.key).isEmpty {
            state.section = lexipeek.SectionPage()
            return
        }
        guard let last = trail.popLast() else { return }
        request = client.restore(last.card)
        card = last.card
        state = last.state
        if state.translating { translate(state.method) }
    }

    func openSection(_ entry: Int, _ key: String, page: Int = 0) {
        guard let card else { return }
        let id = request
        client.section(card, entry: entry, key: key, page: page) { [weak self] p in
            guard let self, id == self.request else { return }
            self.state.section = p
        }
    }

    func turn(_ by: Int) {
        let s = state.section
        if !String(s.key).isEmpty { openSection(Int(s.entry), String(s.key), page: Int(s.page) + by) }
    }

    // Where the body is: a new place (another card, tab, section or page) scrolls to the top.
    var place: String {
        guard let card else { return "" }
        return "\(String(card.text))|\(state.tab.rawValue)|\(state.full_entry)|\(String(state.section.key))|\(state.section.page)"
    }

    func switchTab() { state.tab = state.tab == .dictionary ? .translate : .dictionary }
    func toggleEntry() { state.full_entry.toggle() }
}
