import CxxStdlib
import LexiPeekCore
import SwiftUI

// Settings → Packs: the core's rows (pairs, their parts, sizes, what this Mac cannot run); download, cancel, remove.
final class PacksModel: ObservableObject {
    struct Part: Identifiable {
        var id: String
        var title: String
        var size: Int64
        var installed: Bool
        var update: Bool
        var cannot: String
    }

    struct Row: Identifiable {
        var id: String { from + "-" + to }
        var from: String
        var to: String
        var title: String
        var level: String
        var installed: Bool
        var missing: Int64
        var extras: [Part]
    }

    @Published var rows: [Row] = []
    @Published var busy: String?
    @Published var progress = 0.0
    @Published var error = ""
    @Published var listError = ""
    private var download: lexipeek.mac.Download?
    private var timer: Timer?
    let client: CoreClient

    init(client: CoreClient) { self.client = client }

    func load() {
        rows = lexipeek.mac.rows(std.string(), CoreClient.device).map { r in
            let extras = [r.direct, r.llm].filter { lexipeek.mac.has_part($0) }.map { o -> Part in
                let p = lexipeek.mac.part(o)
                return Part(id: String(p.id), title: String(p.title), size: p.size, installed: p.installed,
                            update: p.update, cannot: String(p.cannot))
            }
            return Row(from: String(r.from), to: String(r.to), title: String(r.title), level: String(r.level),
                       installed: r.installed, missing: r.missing, extras: extras)
        }
    }

    func refresh() {
        busy = "list"
        DispatchQueue.global(qos: .userInitiated).async {
            let e = String(lexipeek.mac.refresh(std.string()))
            DispatchQueue.main.async {
                self.busy = nil
                self.listError = e
                self.load()
            }
        }
    }

    func install(_ ids: [String], _ what: String) {
        guard busy == nil, !ids.isEmpty else { return }
        let dl = lexipeek.mac.Download()
        download = dl
        busy = what
        progress = 0
        error = ""
        timer = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in
            let total = dl.get_total()
            self?.progress = total > 0 ? Double(dl.get_done()) / Double(total) : 0
        }
        run { lexipeek.mac.install(std.string(), Self.strings(ids), dl) }
    }

    func remove(_ ids: [String], _ what: String) {
        guard busy == nil, !ids.isEmpty else { return }
        busy = what
        error = ""
        run { lexipeek.mac.remove(std.string(), Self.strings(ids)) }
    }

    func cancel() { download?.cancel() }

    func pairIDs(_ r: Row) -> [String] { lexipeek.mac.pair_ids(std.string(), std.string(r.from), std.string(r.to)).map { String($0) } }

    func freedIDs(_ r: Row) -> [String] {
        lexipeek.mac.freed_ids(std.string(), std.string(r.from), std.string(r.to), CoreClient.device).map { String($0) }
    }

    private func run(_ work: @escaping () -> lexipeek.Status) {
        DispatchQueue.global(qos: .userInitiated).async {
            let s = work()
            let e = s.cancelled ? "" : String(s.error)
            DispatchQueue.main.async {
                self.timer?.invalidate()
                self.download = nil
                self.busy = nil
                self.error = e
                self.load()
                self.client.reconfigure()
            }
        }
    }

    private static func strings(_ a: [String]) -> lexipeek.mac.Strings {
        var v = lexipeek.mac.Strings()
        for s in a { v.push_back(std.string(s)) }
        return v
    }
}

struct PacksPane: View {
    @StateObject private var model: PacksModel

    init(client: CoreClient) { _model = StateObject(wrappedValue: PacksModel(client: client)) }

    var body: some View {
        Form {
            Section {
                ForEach(model.rows) { row in
                    pair(row)
                    ForEach(row.extras) { part($0) }
                }
                if model.rows.isEmpty { Text("No list of packs yet: check for packs.").foregroundStyle(.secondary) }
            } header: {
                HStack {
                    Text("Language pairs")
                    Spacer()
                    Button("Check for packs") { model.refresh() }.disabled(model.busy != nil)
                }
            } footer: {
                VStack(alignment: .leading, spacing: 4) {
                    if let b = model.busy, b != "list" {
                        HStack {
                            ProgressView(value: model.progress).controlSize(.small)
                            Text(b).font(.caption)
                            Button("Cancel") { model.cancel() }.controlSize(.small)
                        }
                    }
                    ForEach([model.error, model.listError].filter { !$0.isEmpty }, id: \.self) {
                        Text($0).font(.caption).foregroundStyle(.red)
                    }
                    Button(CoreClient.dataDir) { NSWorkspace.shared.open(URL(fileURLWithPath: CoreClient.dataDir)) }
                        .buttonStyle(.link).font(.caption)
                }
            }
        }
        .formStyle(.grouped)
        .onAppear { model.load() }
    }

    private func pair(_ r: PacksModel.Row) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(r.title)
                Text(r.level.isEmpty ? "Dictionary and Mozilla models" : "\(r.level.capitalized) · dictionary and Mozilla models")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            if r.installed {
                if r.missing > 0 { download(r.missing) { model.install(model.pairIDs(r), r.title) } }
                Button("Remove") { model.remove(model.freedIDs(r), r.title) }.disabled(model.busy != nil)
            } else {
                download(r.missing) { model.install(model.pairIDs(r), r.title) }
            }
        }
    }

    private func part(_ p: PacksModel.Part) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(p.title)
                if !p.cannot.isEmpty { Text(p.cannot).font(.caption).foregroundStyle(.secondary) }
            }
            .padding(.leading, 16)
            .opacity(p.cannot.isEmpty ? 1 : 0.5)
            Spacer()
            if p.installed {
                if p.update { download(p.size) { model.install([p.id], p.title) } }
                Button("Remove") { model.remove([p.id], p.title) }.disabled(model.busy != nil)
            } else if p.cannot.isEmpty {
                download(p.size) { model.install([p.id], p.title) }
            }
        }
    }

    private func download(_ size: Int64, _ action: @escaping () -> Void) -> some View {
        Button("Download \(ByteCountFormatter.string(fromByteCount: size, countStyle: .file))", action: action)
            .disabled(model.busy != nil)
    }
}
