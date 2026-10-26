import Carbon.HIToolbox
import LexiPeekKit

// Global hotkeys by Carbon's RegisterEventHotKey: no Accessibility, no event tap.
final class Hotkeys {
    static let shared = Hotkeys()
    private var refs: [UInt32: EventHotKeyRef] = [:]
    private var actions: [UInt32: () -> Void] = [:]

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
        unregister(id)
        var ref: EventHotKeyRef?
        let hid = EventHotKeyID(signature: OSType(0x4C58504B), id: id)  // 'LXPK'
        guard RegisterEventHotKey(s.keyCode, s.modifiers, hid, GetApplicationEventTarget(), 0, &ref) == noErr, let ref
        else { return false }
        refs[id] = ref
        actions[id] = action
        return true
    }

    func unregister(_ id: UInt32) {
        if let ref = refs.removeValue(forKey: id) { UnregisterEventHotKey(ref) }
        actions[id] = nil
    }
}
