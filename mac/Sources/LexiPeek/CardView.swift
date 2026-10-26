import AppKit
import LexiPeekCore
import SwiftUI

// The card: input field (no selection), header, tabs Dictionary · Translate.
struct CardView: View {
    @ObservedObject var model: CardModel
    @FocusState private var focused: Bool
    var focusInput = false

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if model.showInput {
                TextField("Type a word or a sentence", text: $model.input)
                    .textFieldStyle(.roundedBorder)
                    .focused($focused)
                    .onSubmit { model.lookup(model.input, source: .input) }
            }
            if let card = model.card {
                header(card)
                Picker("", selection: $model.tab) {
                    Text("Dictionary").tag(CardModel.Tab.dictionary)
                    Text("Translate").tag(CardModel.Tab.translate)
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                switch model.tab {
                case .dictionary: dictionary(card)
                case .translate: translate(card)
                }
            } else if !model.showInput {
                ProgressView().controlSize(.small).frame(maxWidth: .infinity)
            }
        }
        .padding(14)
        .frame(width: 400, alignment: .leading)
        .frame(maxHeight: .infinity, alignment: .top)
        .onAppear { focused = focusInput }
    }

    private func header(_ card: CardData) -> some View {
        HStack(alignment: .firstTextBaseline) {
            VStack(alignment: .leading, spacing: 2) {
                Text(card.header.isEmpty ? card.text : card.header)
                    .font(.headline)
                    .lineLimit(card.phrase ? 3 : 1)
                    .textSelection(.enabled)
                if !card.base.isEmpty { Text("→ \(card.base)").font(.subheadline).foregroundStyle(.secondary) }
            }
            Spacer(minLength: 8)
            Text(card.lang.isEmpty ? "? → \(card.answer)" : "\(card.lang) → \(card.answer)")
                .font(.caption).foregroundStyle(.secondary)
            Button { model.pinned.toggle() } label: {
                Image(systemName: model.pinned ? "pin.fill" : "pin")
            }
            .buttonStyle(.borderless)
            .help("Pin the card (⌘P)")
        }
    }

    @ViewBuilder
    private func dictionary(_ card: CardData) -> some View {
        if card.entries.isEmpty && card.gloss.isEmpty && card.separable.isEmpty {
            Text("No dictionary entry").foregroundStyle(.secondary)
        }
        ForEach(card.entries.indices, id: \.self) { i in
            let e = card.entries[i]
            VStack(alignment: .leading, spacing: 2) {
                Text(e.dictionary).font(.caption).foregroundStyle(.secondary)
                (model.fullEntry ? Text(Self.html(e.article)) : Text(e.line)).textSelection(.enabled)
            }
        }
        ForEach(Array((card.separable + card.gloss).enumerated()), id: \.offset) { _, g in
            HStack(alignment: .firstTextBaseline) {
                Text(g.base.isEmpty || g.base == g.word ? g.word : "\(g.word) (\(g.base))").bold()
                Text(g.meaning).foregroundStyle(.secondary)
            }
        }
        HStack {
            if !card.entries.isEmpty && !model.fullEntry {
                Button("Full entry ⏎") { model.fullEntry = true }.buttonStyle(.link)
            }
            Spacer()
            if card.looks > 1 { Text("Looked up \(card.looks)×").foregroundStyle(.secondary) }
        }
        .font(.caption)
    }

    @ViewBuilder
    private func translate(_ card: CardData) -> some View {
        if card.methods.isEmpty {
            Text(card.lang.isEmpty ? "The language of this text is not known" : card.error)
                .foregroundStyle(.secondary)
        } else {
            methods(card)
            Group {
                if let t = model.translation {
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
            .frame(maxWidth: .infinity, minHeight: 36, alignment: .topLeading)
        }
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
                Button { model.translate(m.id) } label: {
                    Text(m.available ? m.name : "\(m.name) — \(m.reason)")
                }
                .disabled(!m.available)
            }
        } label: {
            Text(current?.name ?? "No method available")
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
        .font(.caption)
    }
}
