import Carbon.HIToolbox
import LexiPeekKit

// Global hotkeys by Carbon's RegisterEventHotKey: no Accessibility, no event tap.
final class Hotkeys {
    static let shared = Hotkeys()
    private var refs: [UInt32: EventHotKeyRef] = [:]
    private var actions: [UInt32: () -> Void] = [:]
    private var keys: [UInt32: (code: UInt32, modifiers: UInt32)] = [:]
    // While a shortcut is being recorded: every hotkey let go, so its keys reach the field.
    private var suspended = false

    private init() {
        var spec = EventTypeSpec(eventClass: OSType(kEventClassKeyboard), eventKind: UInt32(kEventHotKeyPressed))
        InstallEventHandler(GetApplicationEventTarget(), { _, event, _ in
            var id = EventHotKeyID()
            GetEventParameter(event, EventParamName(kEventParamDirectObject), EventParamType(typeEventHotKeyID), nil,
                              MemoryLayout<EventHotKeyID>.size, nil, &id)
            DispatchQueue.main.async { Hotkeys.shared.actions[id.id]?() }
            return noErr
        }, 1, &spec, nil, nil)
    }

    // false: taken by another app (or the system).
    @discardableResult
    func register(_ id: UInt32, _ s: Shortcut, action: @escaping () -> Void) -> Bool {
        guard let code = s.keyCode else { return false }
        return register(id, code: code, modifiers: s.modifiers, action: action)
    }

    @discardableResult
    func register(_ id: UInt32, code: UInt32, modifiers: UInt32, action: @escaping () -> Void) -> Bool {
        unregister(id)
        keys[id] = (code, modifiers)
        actions[id] = action
        return suspended || take(id)
    }

    private func take(_ id: UInt32) -> Bool {
        guard let k = keys[id] else { return false }
        var ref: EventHotKeyRef?
        let hid = EventHotKeyID(signature: OSType(0x4C58504B), id: id)  // 'LXPK'
        guard RegisterEventHotKey(k.code, k.modifiers, hid, GetApplicationEventTarget(), 0, &ref) == noErr, let ref
        else { return false }
        refs[id] = ref
        return true
    }

    func unregister(_ id: UInt32) {
        if let ref = refs.removeValue(forKey: id) { UnregisterEventHotKey(ref) }
        actions[id] = nil
        keys[id] = nil
    }

    func suspend() {
        guard !suspended else { return }
        suspended = true
        refs.values.forEach { UnregisterEventHotKey($0) }
        refs = [:]
    }

    func resume() {
        guard suspended else { return }
        suspended = false
        keys.keys.forEach { _ = take($0) }
    }
}
