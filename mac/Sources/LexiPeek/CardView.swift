import AppKit
import LexiPeekCore
import SwiftUI

// The card. A word: its header and the dictionary first. A phrase: the translation first, word by word in Dictionary.
// A passage: only its translation. The body scrolls inside a height limit; the card never outgrows half the screen.
struct CardView: View {
    @ObservedObject var model: CardModel
    @FocusState private var focused: Bool
    var focusInput = false
    @State private var bodyHeight: CGFloat = 0

    static func width(_ card: CardData?) -> CGFloat { card?.kind == .passage ? 460 : 400 }
    private var limit: CGFloat { (NSScreen.main?.visibleFrame.height ?? 800) * 0.45 }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if let u = model.unread { unread(u) }
            if model.showInput {
                TextField("Type a word or a sentence", text: $model.input)
                    .textFieldStyle(.roundedBorder)
                    .focused($focused)
                    .onSubmit { model.lookup(model.input, source: .input) }
            }
            if let card = model.card {
                header(card)
                if tabs(card) {
                    Picker("", selection: $model.tab) {
                        Text("Dictionary").tag(CardModel.Tab.dictionary)
                        Text("Translate").tag(CardModel.Tab.translate)
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                }
                ScrollView {
                    VStack(alignment: .leading, spacing: 8) {
                        if tabs(card) && model.tab == .dictionary { dictionary(card) } else { translate(card) }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(GeometryReader { g in Color.clear.preference(key: HeightKey.self, value: g.size.height) })
                }
                .scrollIndicators(.automatic)
                .frame(height: min(max(bodyHeight, 20), limit))
                .onPreferenceChange(HeightKey.self) { bodyHeight = $0 }
                footer(card)
            } else if !model.showInput {
                ProgressView().controlSize(.small).frame(maxWidth: .infinity)
            }
        }
        .padding(14)
        .frame(width: Self.width(model.card), alignment: .leading)
        .frame(maxHeight: .infinity, alignment: .top)
        .onAppear { focused = focusInput }
    }

    private func tabs(_ card: CardData) -> Bool {
        switch card.kind {
        case .word: true
        case .phrase, .passage: !card.gloss.isEmpty || !card.separable.isEmpty
        }
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
    private func header(_ card: CardData) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 8) {
            VStack(alignment: .leading, spacing: 2) {
                switch card.kind {
                case .word:
                    Text(card.header.isEmpty ? card.text : card.header).font(.title3.weight(.semibold)).lineLimit(1)
                    if !card.base.isEmpty { Text("→ \(card.base)").foregroundStyle(.secondary) }
                case .phrase:
                    Text(card.text).font(.headline).lineLimit(2).truncationMode(.tail)
                case .passage:
                    Text(card.text).font(.callout).foregroundStyle(.secondary).lineLimit(2).truncationMode(.tail)
                }
            }
            .help(card.text)
            Spacer(minLength: 8)
            if !badge(card).isEmpty { Text(badge(card)).font(.caption).foregroundStyle(.secondary).lineLimit(1) }
            Button { model.pinned.toggle() } label: { Image(systemName: model.pinned ? "pin.fill" : "pin") }
                .buttonStyle(.borderless)
                .help("Keep the card open (⌘P)")
        }
    }

    private func badge(_ card: CardData) -> String {
        if card.lang.isEmpty { return "" }
        return card.lang == card.answer ? card.langName : "\(card.lang) → \(card.answer)"
    }

    @ViewBuilder
    private func dictionary(_ card: CardData) -> some View {
        if card.kind == .word {
            if card.entries.isEmpty { Text("No entry in the dictionaries").foregroundStyle(.secondary) }
            ForEach(card.entries.indices, id: \.self) { i in
                let e = card.entries[i]
                VStack(alignment: .leading, spacing: 2) {
                    Text(e.dictionary).font(.caption).foregroundStyle(.secondary)
                    (model.fullEntry ? Text(Self.html(e.article)) : Text(e.line))
                        .textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
                }
            }
        } else {
            ForEach(Array((card.separable + card.gloss).enumerated()), id: \.offset) { _, g in
                (Text(g.base.isEmpty || g.base == g.word ? g.word : "\(g.word) (\(g.base))").bold()
                    + Text(g.meaning.isEmpty ? "" : "  \(g.meaning)").foregroundColor(.secondary))
                    .lineLimit(1).truncationMode(.tail)
            }
        }
    }

    @ViewBuilder
    private func translate(_ card: CardData) -> some View {
        if card.lang == card.answer && !card.lang.isEmpty {
            Text("Already in \(card.answerName).").foregroundStyle(.secondary)
        } else if card.lang.isEmpty {
            Text(card.other.isEmpty ? "Could not tell the language of this text."
                 : "No language pack from \(card.other) into \(card.answerName) yet.")
                .foregroundStyle(.secondary)
        } else if card.methods.isEmpty {
            Text(card.error.isEmpty ? "Nothing can translate this text here." : card.error).foregroundStyle(.secondary)
        } else if let t = model.translation {
            if t.error.isEmpty {
                Text(t.text).textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
            } else {
                Text(t.error).foregroundStyle(.red)
            }
        } else if model.translating {
            HStack(spacing: 6) {
                ProgressView().controlSize(.small)
                Text("Translating…").foregroundStyle(.secondary)
            }
        } else if model.method == nil {
            Text(card.methods.first?.reason ?? "").foregroundStyle(.secondary)
        }
    }

    @ViewBuilder
    private func footer(_ card: CardData) -> some View {
        let dict = tabs(card) && model.tab == .dictionary
        HStack {
            if dict && card.kind == .word && !card.entries.isEmpty {
                Button(model.fullEntry ? "Short ⏎" : "Full entry ⏎") { model.fullEntry.toggle() }.buttonStyle(.link)
            }
            if !dict && !card.methods.isEmpty { methods(card) }
            Spacer()
            if card.kind == .word && card.looks > 1 { Text("Looked up \(card.looks)×").foregroundStyle(.secondary) }
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
    private func methods(_ card: CardData) -> some View {
        let current = card.methods.first { $0.id == model.method }
        return Menu {
            ForEach(card.methods) { m in
                Button { model.choose(m.id) } label: {
                    Text(m.available ? m.name : "\(m.name) — \(m.reason)")
                }
                .disabled(!m.available)
            }
        } label: {
            Text(current?.name ?? "No method available")
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
    }
}

private struct HeightKey: PreferenceKey {
    static let defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = max(value, nextValue()) }
}
