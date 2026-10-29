import AppKit
import ApplicationServices
import WebKit

// LEXIPEEK_SELECTION_TEST=<dir> (scripts/selection-test.sh): each <dir>/cases/<name>.html in an off-screen web view, its
// range selected (see `script`), read by the hotkey's own code (Selection.read on the web area of our own process);
// <dir>/<name>.txt the text, <name>.way how it was read (or "none"), <name>.dom the DOM's own selection text; <dir>/done.
// Invisible as the snapshots: an accessory app never activated, the window at -20000,-20000, no menu-bar item, no hotkeys.
final class SelectionTest: NSObject, WKNavigationDelegate {
    static let dir = ProcessInfo.processInfo.environment["LEXIPEEK_SELECTION_TEST"]
    private static var running: SelectionTest?

    private let out: URL
    private var cases: [URL]
    private let window: NSWindow
    private let web = WKWebView(frame: NSRect(x: 0, y: 0, width: 900, height: 700))

    // Accessibility is granted to the app, but a process started from a shell answers for its parent (Terminal): AX into
    // the web content process fails (kAXErrorAPIDisabled). So the app runs itself again as its own responsible process.
    static func respawnDisclaimed() {
        guard dir != nil, ProcessInfo.processInfo.environment["LEXIPEEK_SELECTION_TEST_OWN"] == nil,
              let f = dlsym(UnsafeMutableRawPointer(bitPattern: -2), "responsibility_spawnattrs_setdisclaim") else { return }
        typealias Disclaim = @convention(c) (UnsafeMutablePointer<posix_spawnattr_t?>, Int32) -> Int32
        var attr: posix_spawnattr_t?
        posix_spawnattr_init(&attr)
        _ = unsafeBitCast(f, to: Disclaim.self)(&attr, 1)
        setenv("LEXIPEEK_SELECTION_TEST_OWN", "1", 1)
        let path = Bundle.main.executablePath!
        var argv: [UnsafeMutablePointer<CChar>?] = [strdup(path), nil]
        var pid: pid_t = 0
        guard posix_spawn(&pid, path, nil, &attr, &argv, environ) == 0 else { return }
        var status: Int32 = 0
        waitpid(pid, &status, 0)
        exit(status == 0 ? 0 : 1)
    }

    static func start() {
        guard let dir else { return }
        running = SelectionTest(URL(fileURLWithPath: dir))
        running?.next()
    }

    private init(_ out: URL) {
        self.out = out
        let all = (try? FileManager.default.contentsOfDirectory(at: out.appendingPathComponent("cases"), includingPropertiesForKeys: nil)) ?? []
        cases = all.filter { $0.pathExtension == "html" }.sorted { $0.lastPathComponent < $1.lastPathComponent }
        window = NSWindow(contentRect: NSRect(origin: Snapshot.away, size: web.frame.size), styleMask: [.borderless], backing: .buffered, defer: false)
        super.init()
        window.isReleasedWhenClosed = false
        window.contentView = web
        window.setFrameOrigin(Snapshot.away)
        window.orderFrontRegardless()
        web.navigationDelegate = self
    }

    private func next() {
        guard !cases.isEmpty else {
            FileManager.default.createFile(atPath: out.appendingPathComponent("done").path, contents: nil)
            NSApp.terminate(nil)
            return
        }
        let page = cases.removeFirst()
        web.loadFileURL(page, allowingReadAccessTo: page.deletingLastPathComponent())
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        guard let page = webView.url else { return next() }
        let name = page.deletingPathExtension().lastPathComponent
        webView.evaluateJavaScript(Self.script) { dom, _ in
            self.save(name, "dom", dom as? String ?? "")
            // AX asks this process's main thread for the window's part of the tree: the reading runs on another thread.
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
                DispatchQueue.global().async {
                    let area = Self.webArea()
                    let r = area.flatMap { Selection.read(element: $0) }
                    let none = !Selection.trusted ? "no-access" : area == nil ? "no-web-area" : "none"
                    DispatchQueue.main.async {
                        self.save(name, "txt", r?.text ?? "")
                        self.save(name, "way", r?.way ?? none)
                        self.next()
                    }
                }
            }
        }
    }

    private func save(_ name: String, _ ext: String, _ s: String) {
        try? s.write(to: out.appendingPathComponent("\(name).\(ext)"), atomically: true, encoding: .utf8)
    }

    // The AXWebArea of our own window (the element a browser's focus is on when the hotkey reads a page).
    private static func webArea() -> AXUIElement? {
        let app = AXUIElementCreateApplication(getpid())
        AXUIElementSetMessagingTimeout(app, 2)
        func children(_ e: AXUIElement, _ attr: String) -> [AXUIElement] {
            var v: CFTypeRef?
            guard AXUIElementCopyAttributeValue(e, attr as CFString, &v) == .success else { return [] }
            return (v as? [AXUIElement]) ?? []
        }
        func find(_ e: AXUIElement, _ depth: Int) -> AXUIElement? {
            var role: CFTypeRef?
            AXUIElementCopyAttributeValue(e, kAXRoleAttribute as CFString, &role)
            if role as? String == "AXWebArea" { return e }
            guard depth < 8 else { return nil }
            for c in children(e, kAXChildrenAttribute) { if let w = find(c, depth + 1) { return w } }
            return nil
        }
        for w in children(app, kAXWindowsAttribute) { if let a = find(w, 0) { return a } }
        return nil
    }

    // The range: <body data-from="…" data-to="…"> from the start of the first "from" to the end of the first "to" after
    // it; else from the start of the first [data-sel] element to the end of the last; else the whole body.
    // data-end-next on <body>: the range ends at the start of the block after it (a triple click's range).
    private static let script = """
    (() => {
      const b = document.body, sel = getSelection(), r = document.createRange();
      const texts = [], walk = document.createTreeWalker(b, NodeFilter.SHOW_TEXT);
      for (let n; (n = walk.nextNode());) texts.push(n);
      const at = (pos, end) => {
        let p = 0;
        for (const n of texts) {
          const l = n.data.length;
          if (end ? pos <= p + l && pos > p : pos < p + l) return [n, pos - p];
          p += l;
        }
        throw new Error('out of the text: ' + pos);
      };
      const all = texts.map(n => n.data).join('');
      const marked = [...document.querySelectorAll('[data-sel]')];
      let last = null;
      if (b.dataset.from !== undefined) {
        const i = all.indexOf(b.dataset.from), j = all.indexOf(b.dataset.to, i) + b.dataset.to.length;
        r.setStart(...at(i, false));
        r.setEnd(...at(j, true));
      } else if (marked.length) {
        last = marked[marked.length - 1];
        r.setStart(marked[0], 0);
        r.setEnd(last, last.childNodes.length);
      } else {
        r.selectNodeContents(b);
      }
      if (b.dataset.endNext !== undefined) {
        let n = last || r.endContainer;
        while (n && n !== b && !n.nextElementSibling) n = n.parentElement;
        if (n && n.nextElementSibling) r.setEnd(n.nextElementSibling, 0);
      }
      sel.removeAllRanges();
      sel.addRange(r);
      return sel.toString();
    })()
    """
}
