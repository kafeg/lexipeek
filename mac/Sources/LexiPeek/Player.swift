import AVFoundation

// Read aloud's sound: one reading at a time for the app — the natural voice's WAV (the core made it), or the system
// voice saying the text itself (M11). A card's own playing stops when its card says so.
final class Player: NSObject, AVAudioPlayerDelegate, AVSpeechSynthesizerDelegate {
    static let shared = Player()
    private var player: AVAudioPlayer?
    private let speaker = AVSpeechSynthesizer()
    private weak var owner: AnyObject?
    private var ended: (() -> Void)?

    override private init() {
        super.init()
        speaker.delegate = self
    }

    // The system voice: the OS says the text. false — it has no voice for that language (the card then says so).
    func say(_ text: String, lang: String, owner: AnyObject, ended: @escaping () -> Void) -> Bool {
        stopNow()
        guard let voice = Player.voice(lang) else { return false }
        if Snapshot.dir != nil {  // a snapshot run is silent: the card's ▶ comes straight back
            ended()
            return true
        }
        let u = AVSpeechUtterance(string: text)
        u.voice = voice
        self.owner = owner
        self.ended = ended
        speaker.speak(u)
        return true
    }

    // The language's best voice: the system's own choice for it, bettered by an enhanced or premium one it has
    // installed (they sound far better); the novelty voices ("Bells") are never read with.
    private static func voice(_ lang: String) -> AVSpeechSynthesisVoice? {
        let rank = { (v: AVSpeechSynthesisVoice) in v.quality == .premium ? 2 : v.quality == .enhanced ? 1 : 0 }
        let same = AVSpeechSynthesisVoice.speechVoices().filter {
            !$0.voiceTraits.contains(.isNoveltyVoice) && ($0.language == lang || $0.language.hasPrefix(lang + "-"))
        }
        let own = AVSpeechSynthesisVoice(language: lang)
        let best = same.max { rank($0) < rank($1) }
        guard let best else { return own }
        guard let own, rank(own) >= rank(best) else { return best }
        return own
    }

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
        if speaker.isSpeaking { speaker.stopSpeaking(at: .immediate) }
        owner = nil
        ended = nil
    }

    // Said to the end, or stopped by us: the card's ▶ comes back either way (stopNow let its ended go).
    func speechSynthesizer(_: AVSpeechSynthesizer, didFinish _: AVSpeechUtterance) {
        let e = ended
        stopNow()
        DispatchQueue.main.async { e?() }
    }

    func audioPlayerDidFinishPlaying(_ p: AVAudioPlayer, successfully _: Bool) {
        guard p === player else { return }
        let e = ended
        stopNow()
        e?()
    }
}
