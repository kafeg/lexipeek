import AppKit
import CxxStdlib
import LexiPeekCore
import SwiftUI

// What the first-run window draws, copied out of the core's FirstRunView.
struct FirstRunLook {
    struct Choice: Identifiable {
        var id: String
        var title: String
        var detail: String
    }

    var step = lexipeek.FirstStep.access
    var stepText = ""
    var title = "", text = "", note = ""
    var button = "", secondary = ""
    var buttonEnabled = true
    var answerLabel = "", bookLabel = ""
    var answers: [Choice] = [], books: [Choice] = []
    var answer = "", book = ""
    var progress = false
    var fraction = -1.0
    var progressText = ""
    var error = ""

    init() {}

    init(_ v: lexipeek.FirstRunView) {
        func choices(_ c: [lexipeek.Choice]) -> [Choice] {
            c.map { Choice(id: String($0.code), title: String($0.title), detail: String($0.detail)) }
        }
        step = v.step
        stepText = String(v.step_text)
        title = String(v.title)
        text = String(v.text)
        note = String(v.note)
        button = String(v.button)
        secondary = String(v.secondary)
        buttonEnabled = v.button_enabled
        answerLabel = String(v.answer_label)
        bookLabel = String(v.book_label)
        answers = choices(Array(v.answers))
        books = choices(Array(v.books))
        answer = String(v.answer)
        book = String(v.book)
        progress = v.progress
        fraction = v.fraction
        progressText = String(v.progress_text)
        error = String(v.error)
    }
}

// The first run's state; the core says which step it is and what it shows. Accessibility is watched: granted, the
// window goes on by itself.
final class FirstRunModel: ObservableObject {
    @Published var state = lexipeek.FirstRunState() { didSet { refresh() } }
    @Published private(set) var look = FirstRunLook()
    var close: (() -> Void)?
    private let client: CoreClient
    private let downloader = Downloader()
    private var timers: [Timer] = []

    init(client: CoreClient) {
        self.client = client
        state.trusted = Selection.trusted
        state.system = lexipeek.ui_language()
        state.hotkey = std.string(Prefs.shared.translateKey.label)
        state.input = std.string(Prefs.shared.inputKey.label)
        refresh()
    }

    func refresh() { look = FirstRunLook(lexipeek.app.first_run(std.string(), state)) }

    // The app's language changed: the pair's default answer and the texts again.
    func relabel() { state.system = lexipeek.ui_language() }

    // While the window is open: Accessibility watched, the packs list fetched when there is none.
    func start() {
        guard Snapshot.dir == nil else { return }
        timers.append(Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            guard let self, self.state.trusted != Selection.trusted else { return }
            self.state.trusted = Selection.trusted
            // Past the Accessibility step: the list, fetched now.
            if self.look.step == .list && !self.state.fetching { self.fetch() }
        })
        if look.step == .list || look.step == .pair { fetch() }
    }

    // The window closed: a download stops too (the next one resumes it).
    func stop() {
        timers.forEach { $0.invalidate() }
        timers = []
        if state.downloading { downloader.cancel() }
    }

    func primary() {
        switch look.step {
        case .access:
            Access.ask()
        case .list: fetch()
        case .pair: install()
        case .download: if state.downloading { downloader.cancel() } else { install() }
        default: finish()
        }
    }

    func other() {
        switch look.step {
        case .access:
            state.access_later = true
            if look.step == .list { fetch() }
        case .download: state.error = std.string()
        default: break
        }
    }

    func pickAnswer(_ code: String) {
        state.answer = std.string(code)
        state.book = std.string()
    }

    func pickBook(_ code: String) { state.book = std.string(code) }

    // The window closed or Done: it does not open by itself again unless no pair is installed.
    func finish() {
        Prefs.shared.firstRun = true
        close?()
    }

    // The release's list: fresh, or kept from the last time.
    private func fetch() {
        guard !state.fetching else { return }
        state.fetching = true
        DispatchQueue.global(qos: .userInitiated).async {
            let e = lexipeek.app.refresh(std.string())
            DispatchQueue.main.async {
                self.state.list_error = e
                self.state.fetching = false
            }
        }
    }

    // The picked pair: its answer language for the next cards, then what it still needs, with progress.
    private func install() {
        guard !downloader.busy else { return }
        let answer = look.answer, book = look.book
        guard !book.isEmpty else { return }
        Prefs.shared.answer = answer
        state.answer = std.string(answer)
        state.book = std.string(book)
        let ids = lexipeek.app.missing_ids(std.string(), std.string(book), std.string(answer))
        state.error = std.string()
        state.done = 0
        state.total = 0
        state.downloading = true
        downloader.start({ lexipeek.app.install(std.string(), ids, $0) },
                         progress: { [weak self] d, t in
                             self?.state.done = d
                             self?.state.total = t
                         },
                         done: { [weak self] s in
                             self?.state.error = s.cancelled ? std.string() : s.error
                             self?.state.downloading = false
                             self?.client.reload()
                         })
    }
}

struct FirstRunView: View {
    @ObservedObject var model: FirstRunModel

    var body: some View {
        let look = model.look
        VStack(alignment: .leading, spacing: 14) {
            HStack(spacing: 12) {
                Image(nsImage: NSApp.applicationIconImage).resizable().frame(width: 48, height: 48)
                VStack(alignment: .leading, spacing: 2) {
                    Text(look.stepText).font(.caption).foregroundStyle(.secondary)
                    Text(look.title).font(.title2.weight(.semibold))
                }
            }
            if !look.text.isEmpty { Text(look.text).fixedSize(horizontal: false, vertical: true) }
            content(look)
            if !look.error.isEmpty {
                Label { Text(look.error) } icon: { Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.orange) }.font(.callout)
            }
            if !look.note.isEmpty { Note(look.note) }
            Spacer(minLength: 0)
            HStack {
                Spacer()
                if !look.secondary.isEmpty { Button(look.secondary) { model.other() } }
                if !look.button.isEmpty {
                    Button(look.button) { model.primary() }
                        .keyboardShortcut(.defaultAction)
                        .disabled(!look.buttonEnabled)
                }
            }
        }
        .padding(20)
        .frame(width: 480, height: 440, alignment: .topLeading)
    }

    @ViewBuilder
    private func content(_ look: FirstRunLook) -> some View {
        switch look.step {
        case .pair: pair(look)
        case .download, .list:
            if look.progress {
                VStack(alignment: .leading, spacing: 4) {
                    if look.fraction >= 0 { ProgressView(value: look.fraction) } else { ProgressView().progressViewStyle(.linear) }
                    if !look.progressText.isEmpty { Text(look.progressText).font(.caption).foregroundStyle(.secondary) }
                }
            }
        default: EmptyView()
        }
    }

    // The answer language in a menu; the book languages as a list, by the packs' levels.
    private func pair(_ look: FirstRunLook) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Picker(look.answerLabel, selection: Binding(get: { look.answer }, set: { model.pickAnswer($0) })) {
                ForEach(look.answers) { Text($0.title).tag($0.id) }
            }
            .fixedSize()
            Text(look.bookLabel)
            List(look.books, selection: Binding(get: { look.book }, set: { if let c = $0 { model.pickBook(c) } })) { c in
                HStack {
                    Text(c.title)
                    Spacer()
                    Text(c.detail).foregroundStyle(.secondary)
                }
                .tag(c.id)
            }
            .listStyle(.bordered(alternatesRowBackgrounds: true))
            .frame(height: 150)
        }
    }
}

enum FirstRunWindow {
    static func make(model: FirstRunModel) -> NSWindow {
        let w = NSWindow(contentViewController: NSHostingController(rootView: FirstRunView(model: model)))
        w.styleMask = [.titled, .closable]
        w.title = L.t("fr_window")
        w.isReleasedWhenClosed = false
        w.center()
        return w
    }
}

// A footer note: small, secondary, left-aligned.
struct Note: View {
    var text: String
    init(_ text: String) { self.text = text }
    var body: some View {
        Text(text)
            .font(.caption)
            .foregroundStyle(.secondary)
            .multilineTextAlignment(.leading)
            .frame(maxWidth: .infinity, alignment: .leading)
            .fixedSize(horizontal: false, vertical: true)
    }
}
