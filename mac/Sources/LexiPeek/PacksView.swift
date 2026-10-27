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
        var detail: String  // "Full · Mozilla · OPUS-MT", "Basic · 84 MB"
        var installed: Bool
        var missing: Int64
        var extras: [Part]
    }

    @Published var rows: [Row] = []
    @Published var summary = ""
    @Published var busy: String?
    @Published var progress = 0.0
    @Published var error = ""
    @Published var listError = ""
    private var download: lexipeek.app.Download?
    private var timer: Timer?
    let client: CoreClient

    init(client: CoreClient) { self.client = client }

    func load() {
        let all = lexipeek.app.rows(std.string(), CoreClient.device)
        summary = String(lexipeek.packs_summary(all))
        rows = all.map { r in
            let extras = [r.direct, r.llm].filter { lexipeek.mac.has_part($0) }.map { o -> Part in
                let p = lexipeek.mac.part(o)
                return Part(id: String(p.id), title: String(p.title), size: p.size, installed: p.installed,
                            update: p.update, cannot: String(p.cannot))
            }
            return Row(from: String(r.from), to: String(r.to), title: String(r.title), detail: String(lexipeek.pair_detail(r)),
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
                        Text(b == "list" ? L.t("packs_checking") : b).lineLimit(1)
                        ProgressView(value: b == "list" ? nil : model.progress).progressViewStyle(.linear)
                        if b != "list" { Button(L.t("cancel")) { model.cancel() }.controlSize(.small) }
                    }
                }
                ForEach([model.error, model.listError].filter { !$0.isEmpty }, id: \.self) {
                    Label($0, systemImage: "exclamationmark.triangle.fill").foregroundStyle(.orange).font(.callout)
                }
                LabeledContent {
                    Button(L.t("check_t")) { model.refresh() }.disabled(model.busy != nil)
                } label: {
                    Text(model.summary)
                    Button(L.t("show_finder")) { NSWorkspace.shared.open(URL(fileURLWithPath: CoreClient.dataDir)) }
                        .buttonStyle(.link).font(.caption)
                }
            }
            let installed = model.rows.filter(\.installed), available = model.rows.filter { !$0.installed }
            if !installed.isEmpty {
                Section(L.t("installed_t")) { ForEach(installed) { installedRow($0) } }
            }
            if !available.isEmpty {
                Section(L.t("available_t")) { ForEach(available) { availableRow($0) } }
            }
            if model.rows.isEmpty {
                Section { Text(L.t("packs_no_list")).foregroundStyle(.secondary) }
            }
        }
        .formStyle(.grouped)
        .onAppear { model.load() }
    }

    private func size(_ n: Int64) -> String { String(lexipeek.size_text(n)) }

    private func installedRow(_ r: PacksModel.Row) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(r.title)
                Text(r.detail).font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            if r.missing > 0 || r.extras.contains(where: \.update) {
                Button(L.t("update_btn")) {
                    model.install(model.pairIDs(r) + r.extras.filter(\.update).map(\.id), r.title)
                }
                .disabled(model.busy != nil)
            }
            Menu {
                ForEach(r.extras) { p in
                    if p.installed {
                        Button(L.t("remove_x", p.title)) { model.remove([p.id], p.title) }
                    } else if p.cannot.isEmpty {
                        Button(L.t("download_x", p.title, size(p.size))) { model.install([p.id], p.title) }
                    } else {
                        Button(p.title + ": " + p.cannot) {}.disabled(true)
                    }
                }
                if !r.extras.isEmpty { Divider() }
                Button(L.t("remove_x", r.title), role: .destructive) { model.remove(model.freedIDs(r), r.title) }
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
                Text(r.detail).font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            Button(L.t("download")) { model.install(model.pairIDs(r), r.title) }.disabled(model.busy != nil)
        }
    }
}
