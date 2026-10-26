import Foundation
import LexiPeekCore

// The card's state: one request at a time, its translation by the chosen method.
final class CardModel: ObservableObject {
    enum Tab { case dictionary, translate }

    @Published var input = ""
    @Published var showInput = false
    @Published var card: CardData?
    @Published var tab = Tab.translate
    @Published var method: lexipeek.Method?
    @Published var translation: TranslationData?
    @Published var translating = false
    @Published var fullEntry = false
    @Published var pinned = false
    // The hotkey found no selection in this app (bundle id, name): the card says so and offers Cmd+C there.
    @Published var unread: (id: String, name: String)?
    var retry: (() -> Void)?

    let client: CoreClient
    private var request = 0

    init(client: CoreClient) { self.client = client }

    func reset(input: Bool) {
        request = 0
        self.input = ""
        showInput = input
        card = nil
        translation = nil
        translating = false
        fullEntry = false
        unread = nil
    }

    func lookup(_ text: String, source: lexipeek.Source) {
        let t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !t.isEmpty else { return }
        translation = nil
        translating = false
        fullEntry = false
        request = client.lookup(t, source: source) { [weak self] id, card in
            guard let self, id == self.request else { return }
            self.card = card
            self.tab = card.phrase || card.entries.isEmpty ? .translate : .dictionary
            self.method = card.method
            if let m = card.method { self.translate(m) }
        }
    }

    func translate(_ m: lexipeek.Method) {
        guard request != 0 else { return }
        method = m
        translating = true
        let id = request
        client.translate(id, method: m) { [weak self] t in
            guard let self, id == self.request, m == self.method else { return }
            self.translation = t
            self.translating = false
        }
    }

    func switchTab() { tab = tab == .dictionary ? .translate : .dictionary }
}
