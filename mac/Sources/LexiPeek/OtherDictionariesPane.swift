import AppKit
import CxxStdlib
import LexiPeekCore
import SwiftUI

// Settings → Languages → Other dictionaries: the core's rows (packs and own ones in the card's order), on / off, order,
// folders read in place, import, the catalog of public sources.
final class OtherDictionariesModel: ObservableObject {
    struct Row: Identifiable, Equatable {
        var id: String
        var title: String
        var detail: String
        var pack: Bool
        var on: Bool
        var removable: Bool
    }

    struct Folder: Identifiable {
        var id: String
        var title: String
        var detail: String
    }

    @Published var rows: [Row] = []
    @Published var folders: [Folder] = []
    @Published var note = ""
    @Published var empty = ""
    @Published var importNote = ""
    @Published var message = ""
    @Published var busy = false
    let client: CoreClient

    init(client: CoreClient) { self.client = client }

    func load() {
        let v = lexipeek.app.dictionaries(std.string())
        rows = v.rows.map {
            Row(id: String($0.id), title: String($0.title), detail: String($0.detail), pack: $0.pack, on: $0.on, removable: $0.removable)
        }
        folders = v.folders.map { Folder(id: String($0.path), title: String($0.title), detail: String($0.detail)) }
        note = String(v.note)
        empty = String(v.empty)
        importNote = String(v.import_note)
    }

    func setOn(_ r: Row, _ on: Bool) {
        lexipeek.app.set_dict_on(std.string(), std.string(r.id), on)
        changed()
    }

    func move(_ r: Row, by: Int) {
        lexipeek.app.move_dict(std.string(), std.string(r.id), Int32(by))
        changed()
    }

    func move(from: IndexSet, to: Int) {
        guard let i = from.first, rows.indices.contains(i) else { return }
        lexipeek.app.move_dict_to(std.string(), std.string(rows[i].id), Int32(to > i ? to - 1 : to))
        changed()
    }

    func remove(_ r: Row) {
        message = String(lexipeek.app.remove_dict(std.string(), std.string(r.id)))
        changed(reload: true)
    }

    func addFolder() {
        let p = NSOpenPanel()
        p.canChooseDirectories = true
        p.canChooseFiles = false
        p.prompt = L.t("add_folder")
        p.message = L.t("folder_panel")
        guard p.runModal() == .OK, let url = p.url else { return }
        message = String(lexipeek.app.add_folder(std.string(), std.string(url.path)))
        changed(reload: true)
    }

    func removeFolder(_ f: Folder) {
        lexipeek.app.remove_folder(std.string(), std.string(f.id))
        changed(reload: true)
    }

    func importFiles() {
        let p = NSOpenPanel()
        p.canChooseFiles = true
        p.allowsMultipleSelection = true
        p.prompt = L.t("import_")
        p.message = importNote
        guard p.runModal() == .OK, !p.urls.isEmpty else { return }
        importPaths(p.urls.map(\.path))
    }

    func importPaths(_ paths: [String]) {
        let files = paths.cxx
        busy = true
        message = L.t("importing")
        DispatchQueue.global(qos: .userInitiated).async {
            let r = lexipeek.app.import_dicts(std.string(), files)
            let text = String(lexipeek.app.imported_text(r))
            DispatchQueue.main.async {
                self.busy = false
                self.message = text
                self.changed(reload: true)
            }
        }
    }

    // A new dictionary on disk: the library looks again (and indexes it); else only the settings changed.
    func changed(reload: Bool = false) {
        load()
        client.configure()
        if reload { client.reload() }
    }
}

struct OtherDictionariesPane: View {
    @StateObject private var model: OtherDictionariesModel
    @State private var catalog = false

    init(client: CoreClient) { _model = StateObject(wrappedValue: OtherDictionariesModel(client: client)) }

    var body: some View {
        Form {
            Section {
                HStack {
                    Button(L.t("add_folder_more")) { model.addFolder() }
                    Button(L.t("import_more")) { model.importFiles() }
                    Button(L.t("catalog_more")) { catalog = true }
                    if model.busy { ProgressView().controlSize(.small) }
                }
                .disabled(model.busy)
                if !model.message.isEmpty {
                    Text(model.message).font(.callout).foregroundStyle(.secondary).textSelection(.enabled)
                }
            } footer: {
                Text(model.importNote).font(.caption).foregroundStyle(.secondary)
            }
            Section {
                ForEach(model.rows) { row($0) }
                    .onMove { model.move(from: $0, to: $1) }
                if !model.rows.contains(where: { !$0.pack }) {
                    Text(model.empty).foregroundStyle(.secondary)
                }
            } header: {
                Text(L.t("lang_own_tab"))
            } footer: {
                Text(model.note).font(.caption).foregroundStyle(.secondary)
            }
            if !model.folders.isEmpty {
                Section(L.t("folders_t")) {
                    ForEach(model.folders) { f in
                        HStack {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(f.title).lineLimit(1).truncationMode(.middle)
                                Text(f.detail).font(.caption).foregroundStyle(.secondary)
                            }
                            Spacer()
                            Button(L.t("remove")) { model.removeFolder(f) }.controlSize(.small)
                        }
                    }
                }
            }
        }
        .formStyle(.grouped)
        .onAppear { model.load() }
        .onDrop(of: [.fileURL], isTargeted: nil) { providers in
            drop(providers)
            return true
        }
        .sheet(isPresented: $catalog, onDismiss: { model.changed(reload: true) }) {
            CatalogSheet(model: CatalogModel())
        }
    }

    private func row(_ r: OtherDictionariesModel.Row) -> some View {
        let i = model.rows.firstIndex(of: r) ?? 0
        return HStack {
            if r.pack {
                Image(systemName: "shippingbox").foregroundStyle(.secondary).frame(width: 18)
            } else {
                Toggle("", isOn: Binding(get: { r.on }, set: { model.setOn(r, $0) })).labelsHidden()
            }
            VStack(alignment: .leading, spacing: 2) {
                Text(r.title).lineLimit(1)
                Text(r.detail).font(.caption).foregroundStyle(.secondary).lineLimit(1).truncationMode(.middle)
            }
            .opacity(r.on ? 1 : 0.5)
            Spacer()
            Button { model.move(r, by: -1) } label: { Image(systemName: "chevron.up") }
                .buttonStyle(.borderless).disabled(i == 0).help(L.t("up"))
            Button { model.move(r, by: 1) } label: { Image(systemName: "chevron.down") }
                .buttonStyle(.borderless).disabled(i == model.rows.count - 1).help(L.t("down"))
            if r.removable {
                Button { confirmRemove(r) } label: { Image(systemName: "xmark.circle") }
                    .buttonStyle(.borderless).help(L.t("delete"))
            }
        }
    }

    private func confirmRemove(_ r: OtherDictionariesModel.Row) {
        let a = NSAlert()
        a.messageText = L.t("delete_q", r.title)
        a.informativeText = L.t("delete_d")
        a.addButton(withTitle: L.t("delete")).hasDestructiveAction = true
        a.addButton(withTitle: L.t("cancel"))
        if a.runModal() == .alertFirstButtonReturn { model.remove(r) }
    }

    private func drop(_ providers: [NSItemProvider]) {
        var paths: [String] = []
        let lock = NSLock()
        let group = DispatchGroup()
        for p in providers {
            group.enter()
            _ = p.loadObject(ofClass: URL.self) { url, _ in
                if let url { lock.withLock { paths.append(url.path) } }
                group.leave()
            }
        }
        group.notify(queue: .main) { if !paths.isEmpty { model.importPaths(paths) } }
    }
}

// The catalog: the switched-on sources' lists (fetched on opening), a filter, one download at a time with progress.
final class CatalogModel: ObservableObject {
    struct Row: Identifiable {
        var id: Int
        var title: String
        var detail: String
        var about: String
    }

    struct Source: Identifiable {
        var id: String
        var title: String
        var about: String
        var on: Bool
    }

    @Published var rows: [Row] = []
    @Published var sources: [Source] = []
    @Published var info = ""
    @Published var legend = ""
    @Published var hint = ""
    @Published var empty = ""
    @Published var failed = ""
    @Published var loading = false
    @Published var filter = "" { didSet { show() } }
    @Published var downloading: String?
    @Published var progress = 0.0
    @Published var message = ""
    private var list = lexipeek.CatalogList()
    private let downloader = Downloader()

    func fetch() {
        loading = true
        show()
        DispatchQueue.global(qos: .userInitiated).async {
            let l = lexipeek.app.catalog(std.string())
            DispatchQueue.main.async {
                self.list = l
                self.failed = l.failed.map { String($0) }.joined(separator: "\n")
                self.loading = false
                self.show()
            }
        }
    }

    func show() {
        let v = lexipeek.app.catalog_view(std.string(), list, std.string(filter))
        rows = v.rows.map { Row(id: Int($0.item), title: String($0.title), detail: String($0.detail), about: String($0.about)) }
        sources = v.sources.map { Source(id: String($0.id), title: String($0.title), about: String($0.about), on: $0.on) }
        info = String(v.info)
        legend = String(v.legend)
        hint = String(v.filter_hint)
        empty = String(v.empty)
    }

    func setSource(_ s: Source, _ on: Bool) {
        lexipeek.app.set_source_on(std.string(), std.string(s.id), on)
        fetch()
    }

    func get(_ r: Row) {
        guard downloading == nil else { return }
        downloading = r.title
        progress = 0
        message = ""
        let l = list, id = Int32(r.id)
        downloader.start({ String(lexipeek.app.imported_text(lexipeek.app.download_dict(std.string(), l, id, $0))) },
                         progress: { [weak self] d, t in self?.progress = t > 0 ? Double(d) / Double(t) : 0 },
                         done: { [weak self] text in
                             self?.downloading = nil
                             self?.message = text
                             self?.show()
                         })
    }

    func cancel() { downloader.cancel() }
}

struct CatalogSheet: View {
    @StateObject var model: CatalogModel
    @Environment(\.dismiss) private var dismiss
    @State private var confirm: CatalogModel.Row?

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(L.t("lang_own")).font(.headline)
            HStack {
                ForEach(model.sources) { s in
                    Toggle(s.title, isOn: Binding(get: { s.on }, set: { model.setSource(s, $0) })).help(s.about)
                }
            }
            TextField(model.hint, text: $model.filter).textFieldStyle(.roundedBorder)
            Text(model.legend).font(.caption).foregroundStyle(.secondary)
            if let d = model.downloading {
                HStack {
                    Text(d).lineLimit(1)
                    ProgressView(value: model.progress).progressViewStyle(.linear)
                    Button(L.t("cancel")) { model.cancel() }.controlSize(.small)
                }
            }
            ForEach([model.message, model.failed, model.empty].filter { !$0.isEmpty }, id: \.self) {
                Text($0).font(.callout).foregroundStyle(.secondary).textSelection(.enabled)
            }
            List(model.rows) { r in
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(r.title)
                        Text(r.detail).font(.caption).foregroundStyle(.secondary)
                    }
                    Spacer()
                    Button(L.t("download")) { confirm = r }.disabled(model.downloading != nil)
                }
            }
            .overlay { if model.loading { ProgressView(L.t("loading_lists")) } }
            HStack {
                Text(model.info).font(.caption).foregroundStyle(.secondary)
                Spacer()
                Button(L.t("done")) { dismiss() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding()
        .frame(width: 520, height: 520)
        .onAppear { model.fetch() }
        .alert(confirm?.title ?? "", isPresented: Binding(get: { confirm != nil }, set: { if !$0 { confirm = nil } }), presenting: confirm) { r in
            Button(L.t("download")) { model.get(r) }
            Button(L.t("cancel"), role: .cancel) {}
        } message: { r in
            Text(r.about)
        }
    }
}
