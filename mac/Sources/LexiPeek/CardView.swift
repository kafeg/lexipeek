import AppKit
import LexiPeekCore
import LexiPeekKit
import SwiftUI

// The card: draws what the core's card_view says (header, tabs, rows, footer); the HTML of a full entry by AppKit.
// The body scrolls inside the height limit; width, text size and font are the settings' (the core's CardView).
struct CardView: View {
    @ObservedObject var model: CardModel
    @FocusState private var focused: Bool
    var focusInput = false
    // In the LexiPeek window: as wide as the page, no height limit (the page scrolls).
    var embedded = false
    @State private var bodyHeight: CGFloat = 0
    @State private var recents: [(text: String, meaning: String)] = []
    @State private var picked: Int?
    @State private var titleCut = false
    @State private var titleOpen = false
    @State private var titleHeight: CGFloat = 0

    private var limit: CGFloat { (NSScreen.main?.visibleFrame.height ?? 800) * CGFloat(model.look?.height ?? 45) / 100 }

    // The settings' size and font; delta: the header a little larger, secondary lines a little smaller.
    private func font(_ delta: CGFloat = 0, _ weight: Font.Weight = .regular) -> Font {
        let look = model.look
        let design: Font.Design = switch look?.font ?? "" {
        case "serif": .serif
        case "rounded": .rounded
        case "mono": .monospaced
        default: .default
        }
        return .system(size: CGFloat(look?.text ?? 13) + delta, weight: weight, design: design)
    }

    var body: some View {
        let look = model.look
        VStack(alignment: .leading, spacing: 10) {
            if let u = model.unread { unread(u) }
            if model.showInput {
                TextField(L.t("card_input_hint"), text: $model.input)
                    .textFieldStyle(.roundedBorder)
                    .focused($focused)
                    .onSubmit {
                        if let i = picked, model.input.isEmpty, i < recents.count { model.lookup(recents[i].text, source: .input) }
                        else { model.lookup(model.input, source: .input) }
                    }
                    .onKeyPress(.downArrow) { pick(1) }
                    .onKeyPress(.upArrow) { pick(-1) }
                if look == nil && model.input.isEmpty && !recents.isEmpty { recentList() }
            }
            if let look {
                header(look)
                if look.tabs { tabs(look) }
                ScrollViewReader { proxy in
                    ScrollView {
                        VStack(alignment: .leading, spacing: 8) {
                            ForEach(look.rows.indices, id: \.self) { row(look.rows[$0]) }
                        }
                        .id("top")
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(GeometryReader { g in Color.clear.preference(key: HeightKey.self, value: g.size.height) })
                    }
                    .onChange(of: model.place) { proxy.scrollTo("top", anchor: .top) }
                }
                .scrollIndicators(.automatic)
                .frame(height: embedded ? nil : min(max(bodyHeight, 20), limit))
                .frame(minHeight: embedded ? 0 : nil, maxHeight: embedded ? .infinity : nil)
                // The body's height is known only after layout: the panel fits itself to it again.
                .onPreferenceChange(HeightKey.self) {
                    bodyHeight = $0
                    model.objectWillChange.send()
                }
                if look.footer { footer(look) }
            } else if !model.showInput {
                ProgressView().controlSize(.small).frame(maxWidth: .infinity)
            }
        }
        .font(font())
        .padding(14)
        .frame(width: embedded ? nil : CGFloat(look?.width ?? 400), alignment: .leading)
        .frame(maxWidth: embedded ? .infinity : nil, alignment: .leading)
        .frame(maxHeight: .infinity, alignment: .top)
        .onAppear {
            focused = focusInput
            if model.showInput { recents = model.client.recents().map { (text: $0.0, meaning: $0.1) } }
        }
    }

    // As InkPeek's title: cut with "…" (not selectable: a selection inside spills over); a click on a cut one shows it whole
    // in its place, selectable, at most half the card high, ▲ folds it again.
    @ViewBuilder
    private func title(_ s: String, _ f: Font, lines: Int, secondary: Bool) -> some View {
        if titleOpen {
            VStack(alignment: .leading, spacing: 4) {
                ScrollView {
                    Text(s).font(f).foregroundStyle(secondary ? .secondary : .primary)
                        .textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(GeometryReader { g in Color.clear.preference(key: TitleKey.self, value: g.size.height) })
                }
                .frame(height: min(max(titleHeight, 16), limit / 2))
                .onPreferenceChange(TitleKey.self) {
                    titleHeight = $0
                    model.objectWillChange.send()
                }
                Button { titleOpen = false } label: { Image(systemName: "chevron.up").frame(maxWidth: .infinity) }
                    .buttonStyle(.borderless)
                    .help(L.t("card_collapse"))
            }
        } else {
            Text(s).font(f).foregroundStyle(secondary ? .secondary : .primary).lineLimit(lines).truncationMode(.tail)
                .background(GeometryReader { cut in
                    // The whole text at the same width, unseen: taller means the title is cut.
                    Text(s).font(f).fixedSize(horizontal: false, vertical: true)
                        .frame(width: cut.size.width, alignment: .leading)
                        .hidden()
                        .background(GeometryReader { whole in
                            Color.clear.preference(key: CutKey.self, value: whole.size.height > cut.size.height + 1)
                        })
                })
                .onPreferenceChange(CutKey.self) {
                    titleCut = $0
                    if $0 && Snapshot.whole { titleOpen = true }
                }
                .contentShape(Rectangle())
                .onTapGesture { if titleCut { titleOpen = true } }
                .help(titleCut ? L.t("card_whole") : "")
        }
    }

    // ↑↓ in the empty input field: a recent lookup, Return opens it.
    private func pick(_ by: Int) -> KeyPress.Result {
        guard model.input.isEmpty, !recents.isEmpty else { return .ignored }
        picked = max(0, min(recents.count - 1, (picked ?? (by > 0 ? -1 : recents.count)) + by))
        return .handled
    }

    // The last lookups (the menu's Recents): a click opens one.
    private func recentList() -> some View {
        VStack(alignment: .leading, spacing: 2) {
            ForEach(recents.indices, id: \.self) { i in
                Button { model.lookup(recents[i].text, source: .input) } label: {
                    (Text(Recents.title(recents[i].text)) + Text(recents[i].meaning.isEmpty ? "" : " — \(recents[i].meaning)").foregroundColor(.secondary))
                        .lineLimit(1).truncationMode(.tail)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.vertical, 3).padding(.horizontal, 6)
                        .background(RoundedRectangle(cornerRadius: 5).fill(picked == i ? Color.accentColor.opacity(0.18) : .clear))
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
            Text(L.t("card_recent_keys")).font(font(-3)).foregroundStyle(.tertiary).padding(.top, 2)
        }
    }

    // Text tabs with a line under the open one: lighter than a segmented control.
    private func tabs(_ look: CardLook) -> some View {
        HStack(spacing: 14) {
            ForEach(look.tabItems.indices, id: \.self) { i in
                let t = look.tabItems[i]
                let on = t.tab == look.tab
                Button { model.select(t.tab) } label: {
                    Text(t.title).font(font(-1, on ? .medium : .regular))
                        .foregroundStyle(on ? .primary : .secondary)
                        .padding(.bottom, 4)
                        .overlay(alignment: .bottom) { if on { Rectangle().fill(Color.accentColor).frame(height: 2) } }
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
            Spacer(minLength: 0)
        }
        .overlay(alignment: .bottom) { Divider() }
    }

    // The badge, a menu when the packs offer another language of the text or another answer language.
    @ViewBuilder
    private func badge(_ look: CardLook) -> some View {
        if look.books.isEmpty && look.answers.isEmpty {
            Text(look.badge).font(font(-2)).foregroundStyle(.secondary).lineLimit(1)
        } else {
            Menu {
                Section(L.t("card_text_in")) {
                    ForEach(look.books) { l in Button { model.relookup(lang: l.id) } label: { check(l) } }
                }
                Section(L.t("card_into")) {
                    ForEach(look.answers) { l in Button { model.relookup(answer: l.id) } label: { check(l) } }
                }
            } label: {
                Text(look.badge.isEmpty ? "…" : look.badge).font(font(-2))
            }
            .menuStyle(.borderlessButton)
            .fixedSize()
        }
    }

    private func check(_ l: CardLook.Lang) -> some View {
        l.on ? AnyView(Label(l.title, systemImage: "checkmark")) : AnyView(Text(l.title))
    }

    private func copyButton(_ s: String) -> some View {
        Button {
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(s, forType: .string)
        } label: { Image(systemName: "doc.on.doc").font(font(-2)) }
            .buttonStyle(.borderless)
            .foregroundStyle(.secondary)
            .help(L.t("card_copy"))
    }

    // A row with its copy button on the right, when it has something to copy.
    @ViewBuilder
    private func copyable(_ r: CardLook.Row, @ViewBuilder _ content: () -> some View) -> some View {
        if r.copy.isEmpty {
            content()
        } else {
            HStack(alignment: .firstTextBaseline, spacing: 6) {
                content().frame(maxWidth: .infinity, alignment: .leading)
                copyButton(r.copy)
            }
        }
    }

    private func unread(_ u: (id: String, name: String)) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(L.t("card_unread", u.name)).font(.callout)
            HStack {
                Button(L.t("card_read_copy", u.name)) {
                    Prefs.shared.copyApps.append(u.id)
                    model.retry?()
                }
                .controlSize(.small)
                Text(L.t("card_clipboard")).font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    // Not selectable: a selectable Text drops its line limit on a click and spills over the card.
    private func header(_ look: CardLook) -> some View {
        HStack(alignment: titleOpen ? .top : .firstTextBaseline, spacing: 8) {
            VStack(alignment: .leading, spacing: 2) {
                switch look.style {
                case .word:
                    title(look.title, font(4, .semibold), lines: 2, secondary: false)
                    if !look.subtitle.isEmpty {
                        Text(look.subtitle).font(font(-2)).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                    }
                case .phrase:
                    title(look.title, font(0, .semibold), lines: 2, secondary: false)
                default:
                    title(look.title, font(-1), lines: 1, secondary: true)
                }
            }
            .onChange(of: look.title) { titleOpen = false }
            Spacer(minLength: 8)
            if !look.badge.isEmpty || !look.books.isEmpty || !look.answers.isEmpty { badge(look) }
            if !look.star.isEmpty {
                Button { model.toggleSaved() } label: { Image(systemName: look.star == "★" ? "star.fill" : "star") }
                    .buttonStyle(.borderless)
                    .help(look.starHelp + " (⌘S)")
            }
            if !embedded {
                Button { model.pinned.toggle() } label: { Image(systemName: model.pinned ? "pin.fill" : "pin") }
                    .buttonStyle(.borderless)
                    .help(L.t("card_pin"))
            }
        }
    }

    @ViewBuilder
    private func row(_ r: CardLook.Row) -> some View {
        switch r.style {
        case .entry:
            VStack(alignment: .leading, spacing: 2) {
                if !r.label.isEmpty { Text(r.label).font(font(-2)).foregroundStyle(.secondary) }
                copyable(r) {
                    (r.html.isEmpty ? Text(r.text) : Text(Self.html(r.html, size: model.look?.text ?? 13, font: model.look?.font ?? "")))
                        .textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
                }
            }
        case .gloss:
            // Whole, as InkPeek's rows: a long meaning wraps; a click opens the word here.
            link(r.link) {
                (Text(r.label).bold() + Text(r.text.isEmpty ? "" : " \(r.text)").foregroundColor(.secondary))
                    .fixedSize(horizontal: false, vertical: true)
            }
        case .heading:
            Text(r.text).bold().textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
        case .sense:
            HStack(alignment: .firstTextBaseline, spacing: 6) {
                Text(r.label).foregroundStyle(.secondary).monospacedDigit().frame(minWidth: 18, alignment: .trailing)
                Text(r.text).textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
            }
        case .example:
            Text(r.text).font(font(-1)).foregroundStyle(.secondary).textSelection(.enabled)
                .fixedSize(horizontal: false, vertical: true).padding(.leading, 24)
        case .section:
            Button { model.openSection(r.entry, r.open) } label: {
                VStack(alignment: .leading, spacing: 2) {
                    Text(r.label).lineLimit(2).truncationMode(.tail)
                    Text(r.text).font(font(-2)).foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .padding(.top, 4)
            .overlay(alignment: .top) { Divider() }
        case .text:
            copyable(r) { Text(r.text).font(font(1)).textSelection(.enabled).fixedSize(horizontal: false, vertical: true) }
        case .markdown:
            Text(Self.markdown(r.text)).textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
        case .english:
            Text(r.text).font(font(-1)).foregroundStyle(.secondary).textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
        case .error:
            Text(r.text).foregroundStyle(.red)
        case .progress:
            HStack(spacing: 6) {
                ProgressView().controlSize(.small)
                Text(r.text).foregroundStyle(.secondary)
            }
        default:
            Text(r.text).foregroundStyle(.secondary)
        }
    }

    // A row a click opens as a word of its own in this card; plain when it has no word.
    @ViewBuilder
    private func link(_ word: String, @ViewBuilder _ content: () -> some View) -> some View {
        if word.isEmpty {
            content()
        } else {
            Button { model.open(word) } label: { content().frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle()) }
                .buttonStyle(.plain)
                .help(L.t("card_open_word", word))
        }
    }

    private func footer(_ look: CardLook) -> some View {
        HStack {
            if !look.back.isEmpty {
                Button(look.back) { model.back() }.buttonStyle(.link).lineLimit(1).truncationMode(.tail).help(L.t("card_back"))
            }
            if !look.entryToggle.isEmpty { Button(look.entryToggle) { model.toggleEntry() }.buttonStyle(.link) }
            if look.menu { methods(look) }
            Spacer()
            if !look.pager.isEmpty {
                Button("◀") { model.turn(-1) }.buttonStyle(.borderless).disabled(!look.prev)
                Text(look.pager).foregroundStyle(.secondary).monospacedDigit()
                Button("▶") { model.turn(1) }.buttonStyle(.borderless).disabled(!look.next)
            }
        }
        .font(font(-2))
    }

    // The entry's HTML in the card's font and the system's colours (AppKit's importer: main thread only).
    static func html(_ html: String, size: Int, font: String) -> AttributedString {
        let family = switch font {
        case "serif": "ui-serif, 'New York', serif"
        case "rounded": "ui-rounded, -apple-system"
        case "mono": "ui-monospace, Menlo, monospace"
        default: "-apple-system"
        }
        let doc = "<style>body{font-family:\(family);font-size:\(size)px}</style>" + html
        guard let ns = try? NSMutableAttributedString(data: Data(doc.utf8), options: [
            .documentType: NSAttributedString.DocumentType.html, .characterEncoding: String.Encoding.utf8.rawValue,
        ], documentAttributes: nil) else { return AttributedString(html) }
        ns.addAttribute(.foregroundColor, value: NSColor.labelColor, range: NSRange(location: 0, length: ns.length))
        while ns.string.hasSuffix("\n") { ns.deleteCharacters(in: NSRange(location: ns.length - 1, length: 1)) }
        return (try? AttributedString(ns, including: \.appKit)) ?? AttributedString(ns.string)
    }

    // An AI answer's light Markdown (**bold**, *italic*), its lines kept.
    static func markdown(_ s: String) -> AttributedString {
        (try? AttributedString(markdown: s, options: .init(interpretedSyntax: .inlineOnlyPreservingWhitespace))) ?? AttributedString(s)
    }

    // The current method by name; the others in its menu, unavailable ones dimmed with the reason.
    private func methods(_ look: CardLook) -> some View {
        Menu {
            ForEach(look.methods) { m in
                Button { m.ai ? model.chooseAI() : model.choose(m.method) } label: { Text(m.title) }.disabled(!m.enabled)
            }
        } label: {
            Text(look.menuTitle)
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
    }
}

private struct TitleKey: PreferenceKey {
    static let defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = max(value, nextValue()) }
}

private struct CutKey: PreferenceKey {
    static let defaultValue = false
    static func reduce(value: inout Bool, nextValue: () -> Bool) { value = value || nextValue() }
}

private struct HeightKey: PreferenceKey {
    static let defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = max(value, nextValue()) }
}
