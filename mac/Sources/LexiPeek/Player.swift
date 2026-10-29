import AVFoundation

// Read aloud's sound: one WAV at a time for the app (the core made it); a card's own playing stops when its card says so.
final class Player: NSObject, AVAudioPlayerDelegate {
    static let shared = Player()
    private var player: AVAudioPlayer?
    private weak var owner: AnyObject?
    private var ended: (() -> Void)?

    func play(_ file: String, owner: AnyObject, ended: @escaping () -> Void) {
        stopNow()
        guard Snapshot.dir == nil, let p = try? AVAudioPlayer(contentsOf: URL(fileURLWithPath: file)) else { return ended() }
        p.delegate = self
        player = p
        self.owner = owner
        self.ended = ended
        p.play()
    }

    // Only the owner's playing: another card's goes on.
    func stop(owner: AnyObject) {
        if self.owner === owner { stopNow() }
    }

    private func stopNow() {
        player?.stop()
        player = nil
        owner = nil
        ended = nil
    }

    func audioPlayerDidFinishPlaying(_ p: AVAudioPlayer, successfully _: Bool) {
        guard p === player else { return }
        let e = ended
        stopNow()
        e?()
    }
}
