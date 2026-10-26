import AppKit
import LexiPeekCore
import SwiftUI

// The card: draws what the core's card_view says (header, tabs, rows, footer); the HTML of a full entry by AppKit.
// The body scrolls inside a height limit; the card never outgrows half the screen.
struct CardView: View {
    @ObservedObject var model: CardModel
    @FocusState private var focused: Bool
    var focusInput = false
    @State private var bodyHeight: CGFloat = 0

    static func width(_ look: CardLook?) -> CGFloat { look?.wide == true ? 460 : 400 }
    private var limit: CGFloat { (NSScreen.main?.visibleFrame.height ?? 800) * 0.45 }

    var body: some View {
        let look = model.look
        VStack(alignment: .leading, spacing: 10) {
            if let u = model.unread { unread(u) }
            if model.showInput {
                TextField("Type a word or a sentence", text: $model.input)
                    .textFieldStyle(.roundedBorder)
                    .focused($focused)
                    .onSubmit { model.lookup(model.input, source: .input) }
            }
            if let look {
                header(look)
                if look.tabs {
                    Picker("", selection: $model.state.tab) {
                        Text("Dictionary").tag(lexipeek.CardTab.dictionary)
                        Text("Translate").tag(lexipeek.CardTab.translate)
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                }
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
                .frame(height: min(max(bodyHeight, 20), limit))
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
        .padding(14)
        .frame(width: Self.width(look), alignment: .leading)
        .frame(maxHeight: .infinity, alignment: .top)
        .onAppear { focused = focusInput }
    }

    private func unread(_ u: (id: String, name: String)) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("\(u.name) does not show its selection to LexiPeek.").font(.callout)
            HStack {
                Button("Read it with ⌘C in \(u.name)") {
                    Prefs.shared.copyApps.append(u.id)
                    model.retry?()
                }
                .controlSize(.small)
                Text("The clipboard is put back after.").font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    // Not selectable: a selectable Text drops its line limit on a click and spills over the card.
    private func header(_ look: CardLook) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 8) {
            VStack(alignment: .leading, spacing: 2) {
                switch look.style {
                case .word:
                    Text(look.title).font(.title3.weight(.semibold)).lineLimit(1)
                    if !look.subtitle.isEmpty {
                        Text(look.subtitle).font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
                    }
                case .phrase:
                    Text(look.title).font(.headline).lineLimit(2).truncationMode(.tail)
                default:
                    Text(look.title).font(.callout).foregroundStyle(.secondary).lineLimit(2).truncationMode(.tail)
                }
            }
            .help(look.tooltip)
            Spacer(minLength: 8)
            if !look.badge.isEmpty { Text(look.badge).font(.caption).foregroundStyle(.secondary).lineLimit(1) }
            if !look.star.isEmpty {
                Button { model.toggleSaved() } label: { Image(systemName: look.star == "★" ? "star.fill" : "star") }
                    .buttonStyle(.borderless)
                    .help("\(look.starHelp) (⌘S)")
            }
            Button { model.pinned.toggle() } label: { Image(systemName: model.pinned ? "pin.fill" : "pin") }
                .buttonStyle(.borderless)
                .help("Keep the card open (⌘P)")
        }
    }

    @ViewBuilder
    private func row(_ r: CardLook.Row) -> some View {
        switch r.style {
        case .entry:
            VStack(alignment: .leading, spacing: 2) {
                Text(r.label).font(.caption).foregroundStyle(.secondary)
                (r.html.isEmpty ? Text(r.text) : Text(Self.html(r.html)))
                    .textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
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
            Text(r.text).font(.callout).foregroundStyle(.secondary).textSelection(.enabled)
                .fixedSize(horizontal: false, vertical: true).padding(.leading, 24)
        case .section:
            Button { model.openSection(r.entry, r.open) } label: {
                VStack(alignment: .leading, spacing: 2) {
                    Text(r.label).lineLimit(2).truncationMode(.tail)
                    Text(r.text).font(.caption).foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .padding(.top, 4)
            .overlay(alignment: .top) { Divider() }
        case .text:
            Text(r.text).textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
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
                .help("Open \(word)")
        }
    }

    private func footer(_ look: CardLook) -> some View {
        HStack {
            if !look.back.isEmpty {
                Button(look.back) { model.back() }.buttonStyle(.link).lineLimit(1).truncationMode(.tail).help("Back (⌘[)")
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
        .font(.caption)
    }

    // The entry's HTML in the system font and colours (AppKit's importer: main thread only).
    static func html(_ html: String) -> AttributedString {
        let doc = "<style>body{font-family:-apple-system;font-size:13px}</style>" + html
        guard let ns = try? NSMutableAttributedString(data: Data(doc.utf8), options: [
            .documentType: NSAttributedString.DocumentType.html, .characterEncoding: String.Encoding.utf8.rawValue,
        ], documentAttributes: nil) else { return AttributedString(html) }
        ns.addAttribute(.foregroundColor, value: NSColor.labelColor, range: NSRange(location: 0, length: ns.length))
        while ns.string.hasSuffix("\n") { ns.deleteCharacters(in: NSRange(location: ns.length - 1, length: 1)) }
        return (try? AttributedString(ns, including: \.appKit)) ?? AttributedString(ns.string)
    }

    // The current method by name; the others in its menu, unavailable ones dimmed with the reason.
    private func methods(_ look: CardLook) -> some View {
        Menu {
            ForEach(look.methods) { m in
                Button { model.choose(m.id) } label: { Text(m.title) }.disabled(!m.enabled)
            }
        } label: {
            Text(look.menuTitle)
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
    }
}

private struct HeightKey: PreferenceKey {
    static let defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = max(value, nextValue()) }
}
