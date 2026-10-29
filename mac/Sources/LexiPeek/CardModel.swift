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
    var menu = false
    var menuTitle = ""
    var menuHelp = ""
    var methods: [Item] = []
    var back = ""
    var pager = ""
    var prev = false
    var next = false
    var copy = ""
    var fix = ""
    var fixPane = (lexipeek.SettingsSection.ai, 0)
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
        menu = v.menu
        menuTitle = String(v.menu_title)
        menuHelp = String(v.menu_help)
        methods = v.methods.enumerated().map { Item(id: $0.offset, method: $0.element.method, title: String($0.element.title), enabled: $0.element.enabled, ai: $0.element.ai) }
        back = String(v.back)
        pager = String(v.pager)
        prev = v.prev
        next = v.next
        copy = String(v.copy)
        fix = String(v.fix)
        fixPane = (v.fix_section, Int(v.fix_tab))
        canSave = v.can_save
        saved = v.saved
        starHelp = String(v.star_help)
        width = Int(v.width)
        text = Int(v.text)
        font = String(v.font)
        height = Int(v.height)
    }
}

// The card in a shell: the core's CardSession decides and asks (what is shown, "←", sections, methods, AI answers); this
// polls it while something is on its way and draws what it says.
final class CardModel: ObservableObject {
    @Published var input = ""
    @Published var showInput = false
    @Published private(set) var nav = lexipeek.CardNav() { didSet { relook() } }
    // What the card draws and its card: worked out once per change (the views read them many times a render).
    private(set) var look: CardLook?
    private(set) var card: lexipeek.Card?
    @Published var pinned = false
    @Published var unread: String?  // the app (its name) that did not show its selection
    @Published var noAccess = false  // the hotkey without Accessibility: nothing could be read

    let client: CoreClient
    private var session: lexipeek.app.CardSession
    private var timer: Timer?
    private var observer: Any?

    init(client: CoreClient) {
        self.client = client
        session = lexipeek.app.CardSession.make(client.engine)
        // Notes may have removed (or another card saved) this card's word: its ★ again.
        observer = NotificationCenter.default.addObserver(forName: .notesChanged, object: nil, queue: .main) { [weak self] _ in
            self?.session.recheck_saved()
            self?.pull()
        }
    }

    var state: lexipeek.CardState { nav.state }

    private func relook() {
        card = nav.has_card() ? nav.card() : nil
        look = card.map { CardLook(lexipeek.card_view($0, nav.state)) }
    }

    // What arrived is drawn; polled again while something is on its way.
    private func pull() {
        if session.poll() { nav = session.nav() }
        if session.notes_changed() { NotificationCenter.default.post(name: .notesChanged, object: nil) }
        guard session.busy(), timer == nil else { return }
        let t = Timer(timeInterval: 0.04, repeats: true) { [weak self] _ in
            guard let self else { return }
            if !self.session.busy() { self.timer?.invalidate(); self.timer = nil }
            self.pull()
        }
        RunLoop.main.add(t, forMode: .common)
        timer = t
    }

    // An action: the AI key for what it may ask (read only when a provider is set), then what changed.
    private func act(_ f: (inout lexipeek.app.CardSession) -> Void) {
        f(&session)
        pull()
    }

    func reset(input: Bool) {
        self.input = ""
        showInput = input
        unread = nil
        noAccess = false
        act { $0.reset() }
    }

    // A look-up asked and not answered yet.
    var looking: Bool { session.looking() }

    // Something is on its way: the card keeps its height meanwhile (no jumps), then fits what it shows.
    var loading: Bool { card == nil ? !showInput : nav.busy() }

    var place: String { String(nav.place()) }

    func lookup(_ text: String, source: lexipeek.Source) {
        let t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !t.isEmpty else { return }
        session.set_key(std.string(Keychain.current))
        act { $0.look(std.string(t), source, std.string(), std.string()) }
    }

    // A word clicked in the card (the gloss, a section): its card here, "←" comes back to this one.
    func open(_ word: String) { act { $0.open(std.string(word)) } }

    // The badge's menu: this text again in another language or into another one.
    func relookup(lang: String = "", answer: String = "") { act { $0.relookup(std.string(lang), std.string(answer)) } }

    func back() { act { $0.back() } }

    func openSection(_ entry: Int, _ key: String) { act { $0.open_section(Int32(entry), std.string(key)) } }

    func turn(_ by: Int) { act { $0.turn(Int32(by)) } }

    // One dictionary's line clicked: its full entry alone ("←" comes back to all).
    func openEntry(_ entry: Int) { act { $0.open_entry(Int32(entry)) } }

    func fullEntries() { act { $0.full_entries() } }

    // A tab picked (a click, ←/→): an AI one asks when it opens.
    func select(_ tab: lexipeek.CardTab) { act { $0.select(tab) } }

    func switchTab(_ by: Int = 1) { act { $0.next_tab(Int32(by)) } }

    // AI translation picked in the card's menu: this card only (Card → Opening makes it the default).
    func chooseAI() { act { $0.choose_ai() } }

    // This card only (snapshots' &method=).
    func use(_ m: lexipeek.Method) { act { $0.use(m) } }

    // A method picked in the card: this text again with it, and the next cards too (Settings → Translation).
    func choose(_ m: lexipeek.Method) {
        Prefs.shared.method = String(lexipeek.app.method_id(m))
        use(m)
    }

    // ☆ / ★: saves the card's text in Notes (a word as its base form) with its translation, or removes it.
    func toggleSaved() { act { $0.toggle_saved() } }
}
