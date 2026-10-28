import CxxStdlib
import LexiPeekCore
import SwiftUI

// Settings → Packs: the core's packs_view (pairs, their button and menu as actions); downloads and removals here.
final class PacksModel: ObservableObject {
    @Published var view = lexipeek.PacksView()
    @Published var busy: String?  // what the progress says ("" while the list is fetched)
    @Published var progress = 0.0
    @Published var error = ""
    @Published var listError = ""
    private let downloader = Downloader()
    let client: CoreClient

    init(client: CoreClient) { self.client = client }

    func load() { view = lexipeek.app.packs(std.string(), CoreClient.device) }

    func refresh() {
        busy = ""
        DispatchQueue.global(qos: .userInitiated).async {
            let e = String(lexipeek.app.refresh(std.string()))
            DispatchQueue.main.async {
                self.busy = nil
                self.listError = e
                self.load()
            }
        }
    }

    func run(_ a: lexipeek.PackAction) {
        guard busy == nil, !a.ids.empty() else { return }
        busy = String(a.what)
        progress = 0
        error = ""
        let ids = a.ids
        if a.remove {
            DispatchQueue.global(qos: .userInitiated).async {
                let s = lexipeek.app.remove(std.string(), ids)
                DispatchQueue.main.async { self.finished(s) }
            }
            return
        }
        downloader.start({ lexipeek.app.install(std.string(), ids, $0) },
                         progress: { [weak self] d, t in self?.progress = t > 0 ? Double(d) / Double(t) : 0 },
                         done: { [weak self] s in self?.finished(s) })
    }

    func cancel() { downloader.cancel() }

    private func finished(_ s: lexipeek.Status) {
        busy = nil
        error = s.cancelled ? "" : String(s.error)
        load()
        client.reload()
    }
}

struct PacksPane: View {
    @ObservedObject var model: PacksModel

    var body: some View {
        let v = model.view
        Form {
            Section {
                if let b = model.busy {
                    HStack {
                        Text(b.isEmpty ? L.t("packs_checking") : b).lineLimit(1)
                        ProgressView(value: b.isEmpty ? nil : model.progress).progressViewStyle(.linear)
                        if !b.isEmpty { Button(L.t("cancel")) { model.cancel() }.controlSize(.small) }
                    }
                }
                ForEach([model.error, model.listError].filter { !$0.isEmpty }, id: \.self) { e in
                    Label { Text(e) } icon: { Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.orange) }.font(.callout)
                }
                LabeledContent {
                    Button(L.t("check_t")) { model.refresh() }.disabled(model.busy != nil)
                } label: {
                    Text(String(v.summary))
                    Button(L.t("show_finder")) { NSWorkspace.shared.open(URL(fileURLWithPath: CoreClient.dataDir)) }
                        .buttonStyle(.link).font(.caption)
                }
            }
            if !v.installed.isEmpty {
                Section(String(v.installed_title)) { ForEach(Array(v.installed.enumerated()), id: \.offset) { line($0.element) } }
            }
            if !v.available.isEmpty {
                Section(String(v.available_title)) { ForEach(Array(v.available.enumerated()), id: \.offset) { line($0.element) } }
            }
            if !v.empty.empty() {
                Section { Text(String(v.empty)).foregroundStyle(.secondary) }
            }
        }
        .formStyle(.grouped)
        .onAppear { model.load() }
    }

    private func line(_ l: lexipeek.PackLine) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(String(l.title))
                Text(String(l.detail)).font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            if !l.button.label.empty() {
                Button(String(l.button.label)) { model.run(l.button) }.disabled(model.busy != nil)
            }
            if !l.menu.isEmpty {
                Menu {
                    ForEach(Array(l.menu.enumerated()), id: \.offset) { _, a in
                        if a.divider { Divider() }
                        Button(String(a.label), role: a.remove && a.divider ? .destructive : nil) { model.run(a) }.disabled(!a.enabled)
                    }
                } label: {
                    Image(systemName: "ellipsis.circle")
                }
                .menuStyle(.borderlessButton)
                .menuIndicator(.hidden)
                .fixedSize()
                .disabled(model.busy != nil)
            }
        }
    }
}
