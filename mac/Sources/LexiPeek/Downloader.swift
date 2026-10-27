import CxxStdlib
import Foundation
import LexiPeekCore

extension Array where Element == String {
    // As the facade's list of strings.
    var cxx: lexipeek.mac.Strings {
        var v = lexipeek.mac.Strings()
        for s in self { v.push_back(std.string(s)) }
        return v
    }
}

// A download through the core off the main queue: its progress polled five times a second, then done on the main queue.
final class Downloader {
    private var download: lexipeek.app.Download?
    private var timer: Timer?

    var busy: Bool { download != nil }

    func start<R>(_ work: @escaping (lexipeek.app.Download) -> R, progress: @escaping (Int64, Int64) -> Void,
                  done: @escaping (R) -> Void) {
        guard download == nil else { return }
        let dl = lexipeek.app.Download()
        download = dl
        timer = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { _ in progress(dl.get_done(), dl.get_total()) }
        DispatchQueue.global(qos: .userInitiated).async {
            let r = work(dl)
            DispatchQueue.main.async {
                self.timer?.invalidate()
                self.timer = nil
                self.download = nil
                done(r)
            }
        }
    }

    func cancel() { download?.cancel() }
}
