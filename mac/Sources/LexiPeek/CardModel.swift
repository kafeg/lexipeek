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
        var copy: String
    }

    struct Lang: Identifiable {
        var id: String
        var title: String
        var on: Bool
    }

    struct Item: Identifiable {
        var id: Int
        var method: lexipeek.Method
        var title: String
        var enabled: Bool
        var ai: Bool
    }

    struct Tab {
        var tab: lexipeek.CardTab
        var title: String
    }

    var style = lexipeek.HeaderStyle.word
    var title = ""
    var subtitle = ""
    var tooltip = ""
    var badge = ""
    var books: [Lang] = []
    var answers: [Lang] = []
    var tabs = false
    var tabItems: [Tab] = []
    var tab = lexipeek.CardTab.translate
    var rows: [Row] = []
    var entryToggle = ""
    var menu = false
    var menuTitle = ""
    var menuHelp = ""
    var methods: [Item] = []
    var back = ""
    var pager = ""
    var prev = false
    var next = false
    var star = ""
    var copy = ""
    var fix = ""
    var canSave = false
    var saved = false
    var starHelp = ""
    var width = 400
    var text = 13
    var font = ""
    var height = 45

    init(_ v: lexipeek.CardView) {
        style = v.style
        title = String(v.title)
        subtitle = String(v.subtitle)
        tooltip = String(v.tooltip)
        badge = String(v.badge)
        books = v.books.map { Lang(id: String($0.code), title: String($0.title), on: $0.on) }
        answers = v.answers.map { Lang(id: String($0.code), title: String($0.title), on: $0.on) }
        tabs = v.tabs
        tabItems = v.tab_items.map { Tab(tab: $0.tab, title: String($0.title)) }
        tab = v.tab
        rows = v.rows.map {
            Row(style: $0.style, label: String($0.label), text: String($0.text), html: String($0.html), link: String($0.link),
                open: String($0.open), entry: Int($0.entry), copy: String($0.copy))
        }
        entryToggle = String(v.entry_toggle)
        menu = v.menu
        menuTitle = String(v.menu_title)
        menuHelp = String(v.menu_help)
        methods = v.methods.enumerated().map { Item(id: $0.offset, method: $0.element.method, title: String($0.element.title), enabled: $0.element.enabled, ai: $0.element.ai) }
        back = String(v.back)
        pager = String(v.pager)
        prev = v.prev
        next = v.next
        star = String(v.star)
        copy = String(v.copy)
        fix = String(v.fix)
        canSave = v.can_save
        saved = v.saved
        starHelp = String(v.star_help)
        width = Int(v.width)
        text = Int(v.text)
        font = String(v.font)
        height = Int(v.height)
    }
}

// The card in a shell: the core's CardNav decides (what is shown, "←", sections, methods); this asks the core for what a
// step needs, one request at a time, and hands the answers back.
final class CardModel: ObservableObject {
    @Published var input = ""
    @Published var showInput = false
    @Published private(set) var nav = lexipeek.CardNav() { didSet { relook() } }
    // What the card draws and its card: worked out once per change (the views read them many times a render).
    private(set) var look: CardLook?
    private(set) var card: lexipeek.Card?
    @Published var pinned = false
    @Published var unread: String?  // the app (its name) that did not show its selection

    let client: CoreClient
    private let lane: CardLane
    private var request = 0

    private var observer: Any?

    init(client: CoreClient) {
        self.client = client
        lane = CardLane(client)
        // Notes may have removed (or another card saved) this card's word: its ★ again.
        observer = NotificationCenter.default.addObserver(forName: .notesChanged, object: nil, queue: .main) { [weak self] _ in
            guard let self, let card = self.card else { return }
            let saved = self.client.isSaved(card)
            if saved != self.nav.state.saved { self.nav.state.saved = saved }
        }
    }

    // One dictionary's line clicked: its full entry alone ("←" comes back to all).
    func openEntry(_ entry: Int) { nav.open_entry(Int32(entry)) }

    var state: lexipeek.CardState {
        get { nav.state }
        set { nav.state = newValue }
    }

    private func relook() {
        card = nav.has_card() ? nav.card() : nil
        look = card.map { CardLook(lexipeek.card_view($0, nav.state)) }
    }

    func reset(input: Bool) {
        request = 0
        self.input = ""
        showInput = input
        nav.reset()
        unread = nil
    }

    // A look-up asked and not answered yet.
    var looking: Bool { request != 0 && card == nil }

    // Something is on its way: the card keeps its height meanwhile (no jumps), then fits what it shows.
    var loading: Bool { card == nil ? !showInput : nav.busy() }

    var place: String { String(nav.place()) }

    func lookup(_ text: String, source: lexipeek.Source) {
        let t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !t.isEmpty else { return }
        run(nav.look(std.string(t), source, false, std.string(), std.string()))
    }

    // A word clicked in the card (the gloss, a section): its card here, "←" comes back to this one.
    func open(_ word: String) { run(nav.open(std.string(word))) }

    // The badge's menu: this text again in another language or into another one.
    func relookup(lang: String = "", answer: String = "") { run(nav.relookup(std.string(lang), std.string(answer))) }

    func back() { run(nav.back()) }

    func openSection(_ entry: Int, _ key: String) { run(nav.open_section(Int32(entry), std.string(key), 0)) }

    func turn(_ by: Int) { run(nav.turn(Int32(by))) }

    // What a step asks for: a look-up, the card before made current again, a section's page.
    private func run(_ step: lexipeek.CardStep) {
        switch step.kind {
        case .lookup:
            request = lane.lookup(String(step.text), source: step.source, lang: String(step.lang), answer: String(step.answer)) { [weak self] id, card in
                guard let self, id == self.request else { return }
                self.nav.arrived(card)
                if let m = Snapshot.method, self.nav.state.has_method { self.nav.state.method = m }
                if Snapshot.ai { self.nav.state.by_ai = card.ai.translate }
                NotificationCenter.default.post(name: .notesChanged, object: nil)
                if self.nav.state.has_method { self.translate(self.nav.state.method) }
                self.askAI()
            }
        case .restore:
            guard let card else { return }
            request = lane.restore(card)
            if step.translate { translate(nav.state.method) }
            askAI()
        case .section:
            guard let card else { return }
            let id = request
            client.section(card, entry: Int(step.entry), key: String(step.key), page: Int(step.page)) { [weak self] p in
                guard let self, id == self.request else { return }
                self.nav.section_arrived(p)
            }
        default:
            break
        }
    }

    // The AI answers the card shows now (the core says which: only on the person's action), the open one first.
    func askAI() {
        guard let card, request != 0 else { return }
        let id = request
        let asks = lexipeek.card_asks(card, nav.state)
        guard !asks.isEmpty else { return }
        let key = Keychain.current  // read only when something is asked
        for a in asks {
            var asking = lexipeek.AiState()
            asking.asking = true
            nav.state = lexipeek.with_answer(nav.state, a, asking)
            lane.ask(id, card, a, key: key) { [weak self] st in
                guard let self, id == self.request else { return }
                self.nav.state = lexipeek.with_answer(self.nav.state, a, st)
            }
        }
    }

    // A tab picked (a click, ←/→): an AI one asks when it opens.
    func select(_ tab: lexipeek.CardTab) {
        nav.select(tab)
        askAI()
    }

    func switchTab(_ by: Int = 1) {
        nav.next_tab(Int32(by))
        askAI()
    }

    func toggleEntry() { nav.toggle_entry() }

    // AI translation picked in the card's menu: this card only (Card → Opening makes it the default).
    func chooseAI() {
        nav.pick_ai()
        askAI()
    }

    func translate(_ m: lexipeek.Method) {
        guard request != 0 else { return }
        nav.translating(m)
        let id = request
        lane.translate(id, method: m) { [weak self] t in
            guard let self, id == self.request else { return }
            self.nav.translated(m, t)
        }
    }

    // A method picked in the card: this text again with it, and the next cards too (Settings → Translation).
    func choose(_ m: lexipeek.Method) {
        Prefs.shared.method = String(lexipeek.app.method_id(m))
        nav.pick_method(m)
        translate(m)
    }

    // ☆ / ★: saves the card's text in Notes (a word as its base form) with its translation, or removes it.
    func toggleSaved() {
        guard let card, look?.canSave == true else { return }
        nav.state.saved = client.toggleSaved(card, translation: String(lexipeek.shown_translation(nav.state)))
        NotificationCenter.default.post(name: .notesChanged, object: nil)
    }
}
