import AppKit

SelectionTest.respawnDisclaimed()

L.apply()
let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
