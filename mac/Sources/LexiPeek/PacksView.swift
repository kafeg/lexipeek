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
    private var download: lexipeek.app.Download?
    private var timer: Timer?
    let client: CoreClient

    init(client: CoreClient) { self.client = client }

    func load() {
        rows = lexipeek.app.rows(std.string(), CoreClient.device).map { r in
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
            let e = String(lexipeek.app.refresh(std.string()))
            DispatchQueue.main.async {
                self.busy = nil
                self.listError = e
                self.load()
            }
        }
    }

    func install(_ ids: [String], _ what: String) {
        guard busy == nil, !ids.isEmpty else { return }
        let dl = lexipeek.app.Download()
        download = dl
        busy = what
        progress = 0
        error = ""
        timer = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in
            let total = dl.get_total()
            self?.progress = total > 0 ? Double(dl.get_done()) / Double(total) : 0
        }
        run { lexipeek.app.install(std.string(), Self.strings(ids), dl) }
    }

    func remove(_ ids: [String], _ what: String) {
        guard busy == nil, !ids.isEmpty else { return }
        busy = what
        error = ""
        run { lexipeek.app.remove(std.string(), Self.strings(ids)) }
    }

    func cancel() { download?.cancel() }

    func pairIDs(_ r: Row) -> [String] { lexipeek.app.missing_ids(std.string(), std.string(r.from), std.string(r.to)).map { String($0) } }

    func freedIDs(_ r: Row) -> [String] {
        lexipeek.app.freed_ids(std.string(), std.string(r.from), std.string(r.to)).map { String($0) }
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
                self.client.reload()
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
                if let b = model.busy {
                    HStack {
                        Text(b == "list" ? "Checking for packs…" : b).lineLimit(1)
                        ProgressView(value: b == "list" ? nil : model.progress).progressViewStyle(.linear)
                        if b != "list" { Button("Cancel") { model.cancel() }.controlSize(.small) }
                    }
                }
                ForEach([model.error, model.listError].filter { !$0.isEmpty }, id: \.self) {
                    Label($0, systemImage: "exclamationmark.triangle.fill").foregroundStyle(.orange).font(.callout)
                }
                LabeledContent {
                    Button("Check for Updates") { model.refresh() }.disabled(model.busy != nil)
                } label: {
                    Text(summary)
                    Button("Show in Finder") { NSWorkspace.shared.open(URL(fileURLWithPath: CoreClient.dataDir)) }
                        .buttonStyle(.link).font(.caption)
                }
            }
            let installed = model.rows.filter(\.installed), available = model.rows.filter { !$0.installed }
            if !installed.isEmpty {
                Section("Installed") { ForEach(installed) { installedRow($0) } }
            }
            if !available.isEmpty {
                Section("Available") { ForEach(available) { availableRow($0) } }
            }
            if model.rows.isEmpty {
                Section { Text("No list of packs yet. Check for updates.").foregroundStyle(.secondary) }
            }
        }
        .formStyle(.grouped)
        .frame(width: 520, height: 520)
        .onAppear { model.load() }
    }

    private var summary: String {
        let n = model.rows.filter(\.installed).count
        return n == 0 ? "No language pairs installed" : n == 1 ? "1 language pair installed" : "\(n) language pairs installed"
    }

    private func size(_ n: Int64) -> String { ByteCountFormatter.string(fromByteCount: n, countStyle: .file) }

    private func level(_ r: PacksModel.Row) -> String { r.level.isEmpty ? "" : r.level.capitalized }

    private func installedRow(_ r: PacksModel.Row) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(r.title)
                Text(([level(r), "Mozilla"] + r.extras.filter(\.installed).map(\.title)).filter { !$0.isEmpty }.joined(separator: " · "))
                    .font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            if r.missing > 0 || r.extras.contains(where: \.update) {
                Button("Update") {
                    model.install(model.pairIDs(r) + r.extras.filter(\.update).map(\.id), r.title)
                }
                .disabled(model.busy != nil)
            }
            Menu {
                ForEach(r.extras) { p in
                    if p.installed {
                        Button("Remove \(p.title)") { model.remove([p.id], p.title) }
                    } else if p.cannot.isEmpty {
                        Button("Download \(p.title) (\(size(p.size)))") { model.install([p.id], p.title) }
                    } else {
                        Button("\(p.title): \(p.cannot)") {}.disabled(true)
                    }
                }
                if !r.extras.isEmpty { Divider() }
                Button("Remove \(r.title)", role: .destructive) { model.remove(model.freedIDs(r), r.title) }
            } label: {
                Image(systemName: "ellipsis.circle")
            }
            .menuStyle(.borderlessButton)
            .menuIndicator(.hidden)
            .fixedSize()
            .disabled(model.busy != nil)
        }
    }

    private func availableRow(_ r: PacksModel.Row) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(r.title)
                Text([level(r), size(r.missing)].filter { !$0.isEmpty }.joined(separator: " · "))
                    .font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            Button("Download") { model.install(model.pairIDs(r), r.title) }.disabled(model.busy != nil)
        }
    }
}
