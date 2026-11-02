// Linux's input: hotkeys (X11's XGrabKey; Wayland's GlobalShortcuts portal), the PRIMARY selection (never the
// clipboard), the active window, the pointer's buttons.
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QObject>
#include <QProcess>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <climits>
#include <fstream>
#include <map>
#include <memory>
#include "lexipeek/os.hpp"
#include "linux.hpp"

namespace {
struct Shortcut {
  QString id;
  QVariantMap props;
};
}  // namespace
Q_DECLARE_METATYPE(Shortcut)

namespace {

QDBusArgument &operator<<(QDBusArgument &a, const Shortcut &s) {
  a.beginStructure();
  a << s.id << s.props;
  a.endStructure();
  return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, Shortcut &s) {
  a.beginStructure();
  a >> s.id >> s.props;
  a.endStructure();
  return a;
}

// Xlib from several threads (the hotkeys here, a selection read on a worker): before any other Xlib call. Its errors
// never end the process (Xlib's default): a window gone between two calls is ordinary; a grab another app has is noted.
bool grab_failed = false;
int on_error(Display *, XErrorEvent *e) {
  grab_failed |= e->error_code == BadAccess;
  return 0;
}
[[maybe_unused]] const int threads = (XInitThreads(), XSetErrorHandler(on_error), 0);

using Clock = std::chrono::steady_clock;
constexpr unsigned mod_mask = ShiftMask | ControlMask | Mod1Mask | Mod4Mask;

KeySym key_sym(const std::string &k) {
  if (k == "Esc") return XK_Escape;
  if (k == ".") return XK_period;
  if (k.size() == 1 && k[0] >= 'A' && k[0] <= 'Z') return XK_a + (k[0] - 'A');
  if (k.size() == 1 && k[0] >= '0' && k[0] <= '9') return XK_0 + (k[0] - '0');
  return NoSymbol;
}

unsigned mods(const lexipeek::Hotkey &h) {
  return (h.shift ? ShiftMask : 0u) | (h.control ? ControlMask : 0u) | (h.option ? Mod1Mask : 0u) | (h.command ? Mod4Mask : 0u);
}

// A window's property as bytes; empty when it has none.
std::string property(Display *d, Window w, Atom name, Atom type = AnyPropertyType) {
  Atom got = None;
  int format = 0;
  unsigned long n = 0, after = 0;
  unsigned char *data = nullptr;
  std::string out;
  if (XGetWindowProperty(d, w, name, 0, 1 << 20, False, type, &got, &format, &n, &after, &data) == Success && data) {
    out.assign(reinterpret_cast<char *>(data), n * static_cast<unsigned long>(format == 32 ? sizeof(long) : format / 8));
    XFree(data);
  }
  return out;
}

Window active_window(Display *d) {
  auto p = property(d, DefaultRootWindow(d), XInternAtom(d, "_NET_ACTIVE_WINDOW", False), XA_WINDOW);
  return p.size() >= sizeof(long) ? static_cast<Window>(*reinterpret_cast<const long *>(p.data())) : None;
}

long window_pid(Display *d, Window w) {
  if (!w) return 0;
  auto p = property(d, w, XInternAtom(d, "_NET_WM_PID", False), XA_CARDINAL);
  return p.size() >= sizeof(long) ? *reinterpret_cast<const long *>(p.data()) : 0;
}

std::string program_of(Display *d, Window w) {
  if (auto pid = window_pid(d, w); pid > 0) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    if (std::getline(f, name) && !name.empty()) return name;
  }
  // No pid (another machine's window, some toolkits): its class's name.
  auto c = property(d, w, XA_WM_CLASS, XA_STRING);
  return c.substr(0, c.find('\0'));
}

// The hotkeys' X connection, on the main thread, read by Qt's loop.
struct Grab {
  KeyCode code = 0;
  unsigned mods = 0;
  std::function<void()> fire;
  bool down = false;
};
struct X {
  Display *d = nullptr;
  std::map<int, Grab> grabs;
};
void drain(X &x) {
  std::vector<std::function<void()>> fire;
  while (XPending(x.d)) {
    XEvent e;
    XNextEvent(x.d, &e);
    if (e.type != KeyPress && e.type != KeyRelease) continue;
    for (auto &[id, g] : x.grabs) {
      if (g.code != e.xkey.keycode) continue;
      // A release by the key alone: its modifiers may be up already.
      if (e.type == KeyRelease) g.down = false;
      else if ((e.xkey.state & mod_mask) == g.mods && !g.down) g.down = true, fire.push_back(g.fire);
    }
  }
  // After the loop: a handler may unregister its own hotkey (Esc closes the card).
  for (auto &f : fire) f();
}

X &x11() {
  static X x = [] {
    X out;
    out.d = XOpenDisplay(nullptr);
    if (!out.d) return out;
    XkbSetDetectableAutoRepeat(out.d, True, nullptr);  // a held key fires once
    auto *n = new QSocketNotifier(ConnectionNumber(out.d), QSocketNotifier::Read, QCoreApplication::instance());
    QObject::connect(n, &QSocketNotifier::activated, [] { drain(x11()); });
    return out;
  }();
  return x;
}

void ungrab(X &x, const Grab &g) {
  for (unsigned extra : {0u, unsigned(LockMask), unsigned(Mod2Mask), unsigned(LockMask | Mod2Mask)})
    XUngrabKey(x.d, g.code, g.mods | extra, DefaultRootWindow(x.d));
  XFlush(x.d);
}

// PRIMARY's text as UTF-8 (target UTF8_STRING, else Latin-1 STRING); nullopt when none within the time.
std::optional<std::string> read_primary(Clock::time_point until) {
  std::unique_ptr<Display, int (*)(Display *)> d(XOpenDisplay(nullptr), XCloseDisplay);
  if (!d || XGetSelectionOwner(d.get(), XA_PRIMARY) == None) return std::nullopt;
  auto *dp = d.get();
  Atom prop = XInternAtom(dp, "LEXIPEEK_SELECTION", False), incr = XInternAtom(dp, "INCR", False);
  Window w = XCreateSimpleWindow(dp, DefaultRootWindow(dp), 0, 0, 1, 1, 0, 0, 0);
  XSelectInput(dp, w, PropertyChangeMask);
  // The next event for w that pred takes, or false at the deadline.
  auto wait = [&](XEvent &e, auto pred) {
    for (;;) {
      while (XPending(dp)) {
        XNextEvent(dp, &e);
        if (pred(e)) return true;
      }
      auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
      if (left <= 0) return false;
      pollfd p{ConnectionNumber(dp), POLLIN, 0};
      poll(&p, 1, static_cast<int>(left));
    }
  };
  auto take = [&](Atom &type) {
    int format = 0;
    unsigned long n = 0, after = 0;
    unsigned char *data = nullptr;
    std::string out;
    if (XGetWindowProperty(dp, w, prop, 0, LONG_MAX / 4, True, AnyPropertyType, &type, &format, &n, &after, &data) == Success && data) {
      out.assign(reinterpret_cast<char *>(data), n * static_cast<unsigned long>(format / 8));
      XFree(data);
    }
    return out;
  };
  for (Atom target : {XInternAtom(dp, "UTF8_STRING", False), XA_STRING}) {
    XConvertSelection(dp, XA_PRIMARY, target, prop, w, CurrentTime);
    XFlush(dp);
    XEvent e;
    if (!wait(e, [&](const XEvent &x) { return x.type == SelectionNotify && x.xselection.requestor == w; })) break;
    if (e.xselection.property == None) continue;
    Atom type = None;
    auto text = take(type);
    // A large one comes in pieces: each new value read (and deleted) until an empty one.
    if (type == incr) {
      text.clear();
      for (;;) {
        if (!wait(e, [&](const XEvent &x) { return x.type == PropertyNotify && x.xproperty.atom == prop && x.xproperty.state == PropertyNewValue; }))
          return std::nullopt;
        auto piece = take(type);
        if (piece.empty()) break;
        text += piece;
      }
    }
    if (target == XA_STRING) {  // Latin-1 → UTF-8
      std::string u;
      for (unsigned char c : text) {
        if (c >= 0x80) u += static_cast<char>(0xC0 | c >> 6), c = static_cast<unsigned char>(0x80 | (c & 0x3F));
        u += static_cast<char>(c);
      }
      text = u;
    }
    XDestroyWindow(dp, w);
    return text;
  }
  XDestroyWindow(dp, w);
  return std::nullopt;
}

// Wayland apps' primary selection when XWayland gives none: wl-clipboard's wl-paste, when installed.
std::optional<std::string> wl_primary() {
  auto exe = QStandardPaths::findExecutable("wl-paste");
  if (exe.isEmpty()) return std::nullopt;
  QProcess p;
  p.start(exe, {"--primary", "--no-newline", "--type", "text"});
  if (!p.waitForFinished(1000)) {
    p.kill();
    p.waitForFinished(200);
    return std::nullopt;
  }
  if (p.exitCode() != 0) return std::nullopt;
  return p.readAllStandardOutput().toStdString();
}

}  // namespace

namespace lexipeek::os {

namespace linux_ {

// Wayland: the desktop's GlobalShortcuts portal, bound once a launch; it says when one fires.
class Portal : public QObject {
  Q_OBJECT
 public:
  std::map<int, std::function<void()>> fire;
  std::map<int, lexipeek::Hotkey> keys;

  static Portal &get() {
    static auto *p = new Portal;
    return *p;
  }
  bool available() {
    static const bool has = [] {
      auto m = QDBusMessage::createMethodCall(service, path, "org.freedesktop.DBus.Properties", "Get");
      m << QString("org.freedesktop.portal.GlobalShortcuts") << QString("version");
      return QDBusConnection::sessionBus().call(m, QDBus::Block, 2000).type() == QDBusMessage::ReplyMessage;
    }();
    return has;
  }
  // After the hotkeys of this turn of the loop are in: one bind with all of them.
  void bind_later() {
    if (asked_) return;
    asked_ = true;
    QTimer::singleShot(0, this, [this] { create(); });
  }

 private Q_SLOTS:
  void created(uint code, const QVariantMap &results) {
    if (code != 0) return linux_::set_error("GlobalShortcuts: no session (" + std::to_string(code) + ")");
    auto v = results.value("session_handle");
    session_ = v.canConvert<QDBusObjectPath>() ? v.value<QDBusObjectPath>().path() : v.toString();
    QList<Shortcut> list;
    for (auto &[id, h] : keys) list.push_back({name(id), {{"description", description(id)}, {"preferred_trigger", trigger(h)}}});
    auto m = QDBusMessage::createMethodCall(service, path, iface, "BindShortcuts");
    m << QVariant::fromValue(QDBusObjectPath(session_)) << QVariant::fromValue(list) << QString() << request("bound", SLOT(bound(uint, QVariantMap)));
    QDBusConnection::sessionBus().call(m, QDBus::NoBlock);
  }
  void bound(uint code, const QVariantMap &) {
    if (code != 0) linux_::set_error("GlobalShortcuts: not bound (" + std::to_string(code) + ")");
  }
  void activated(const QDBusObjectPath &session, const QString &id, qulonglong, const QVariantMap &) {
    if (session.path() != session_) return;
    for (auto &[i, f] : fire)
      if (name(i) == id && f) return std::function<void()>(f)();
  }

 private:
  static constexpr const char *service = "org.freedesktop.portal.Desktop", *path = "/org/freedesktop/portal/desktop",
                              *iface = "org.freedesktop.portal.GlobalShortcuts";
  bool asked_ = false;
  QString session_;
  int tokens_ = 0;

  Portal() {
    qDBusRegisterMetaType<Shortcut>();
    qDBusRegisterMetaType<QList<Shortcut>>();
    QDBusConnection::sessionBus().connect(service, path, iface, "Activated", this,
                                          SLOT(activated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
  }
  static QString name(int id) { return id == 1 ? "lookup-selection" : id == 2 ? "input" : QString::number(id); }
  static QString description(int id) { return id == 1 ? "Look up the selection" : id == 2 ? "Type a text to look up" : "LexiPeek"; }
  // The shortcuts spec's form: CTRL+ALT+d.
  static QString trigger(const lexipeek::Hotkey &h) {
    QString t;
    if (h.control) t += "CTRL+";
    if (h.option) t += "ALT+";
    if (h.shift) t += "SHIFT+";
    if (h.command) t += "LOGO+";
    return t + QString::fromStdString(h.key).toLower();
  }
  // A request's options (its token) with its Response signal already connected to slot.
  QVariantMap request(const char *what, const char *slot) {
    auto token = QString("lexipeek_%1_%2").arg(what).arg(++tokens_);
    auto sender = QDBusConnection::sessionBus().baseService().mid(1).replace('.', '_');
    QDBusConnection::sessionBus().connect(service, QString("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token),
                                          "org.freedesktop.portal.Request", "Response", this, slot);
    return {{"handle_token", token}};
  }
  void create() {
    // A program outside a sandbox names itself first (xdg-desktop-portal 1.19+; older ones lack it: no matter).
    auto reg = QDBusMessage::createMethodCall(service, path, "org.freedesktop.host.portal.Registry", "Register");
    reg << QString(linux_::app_id) << QVariantMap();
    QDBusConnection::sessionBus().call(reg, QDBus::Block, 2000);
    auto m = QDBusMessage::createMethodCall(service, path, iface, "CreateSession");
    auto options = request("session", SLOT(created(uint, QVariantMap)));
    options["session_handle_token"] = "lexipeek";
    m << options;
    QDBusConnection::sessionBus().call(m, QDBus::NoBlock);
  }
};

}  // namespace linux_

using linux_::Portal;

bool register_hotkey(int id, const Hotkey &h, std::function<void()> fire) {
  unregister_hotkey(id);
  if (id < 100 && linux_::wayland()) {
    auto &p = Portal::get();
    if (!p.available()) return false;
    p.fire[id] = std::move(fire), p.keys[id] = h;
    p.bind_later();
    return true;
  }
  auto &x = x11();
  auto sym = key_sym(h.key);
  KeyCode code = x.d && sym != NoSymbol ? XKeysymToKeycode(x.d, sym) : 0;
  if (!code) return linux_::set_error("no such key: " + h.key), false;
  Grab g{code, mods(h), std::move(fire)};
  grab_failed = false;
  XSetErrorHandler(on_error);  // Qt may have set its own since
  for (unsigned extra : {0u, unsigned(LockMask), unsigned(Mod2Mask), unsigned(LockMask | Mod2Mask)})
    XGrabKey(x.d, code, g.mods | extra, DefaultRootWindow(x.d), False, GrabModeAsync, GrabModeAsync);
  XSync(x.d, False);
  if (grab_failed) {
    ungrab(x, g);
    return linux_::set_error("taken by another app"), false;
  }
  x.grabs[id] = std::move(g);
  QTimer::singleShot(0, [] { drain(x11()); });  // what XSync read already
  return true;
}

void unregister_hotkey(int id) {
  if (linux_::wayland()) Portal::get().fire.erase(id);
  auto &x = x11();
  if (auto it = x.grabs.find(id); it != x.grabs.end()) {
    ungrab(x, it->second);
    x.grabs.erase(it);
  }
}

std::string desktop_shortcuts() { return linux_::wayland() && !Portal::get().available() ? linux_::program() : ""; }

std::optional<Selected> selection() {
  auto until = Clock::now() + std::chrono::milliseconds(1500);
  auto text = read_primary(until);
  if ((!text || text->empty()) && linux_::wayland()) text = wl_primary();
  if (!text || text->empty()) return std::nullopt;
  return Selected{*text, {}};
}

std::string probe_selection() {
  auto env = [](const char *k) { auto *v = std::getenv(k); return std::string(v ? v : "-"); };
  std::string out = "session: " + env("XDG_SESSION_TYPE") + ", DISPLAY " + env("DISPLAY") + ", WAYLAND_DISPLAY " + env("WAYLAND_DISPLAY") +
                    ", desktop " + env("XDG_CURRENT_DESKTOP") + "\n";
  if (auto *d = XOpenDisplay(nullptr)) {
    auto owner = XGetSelectionOwner(d, XA_PRIMARY);
    out += "PRIMARY owner: " + std::to_string(owner) + (owner ? " (" + program_of(d, owner) + ")" : "") + "\n";
    XCloseDisplay(d);
  } else {
    out += "X11: no display\n";
  }
  auto x = read_primary(Clock::now() + std::chrono::milliseconds(1500));
  out += "PRIMARY by X11: " + (x ? std::to_string(x->size()) + " bytes: " + x->substr(0, 200) : std::string("none")) + "\n";
  auto w = QStandardPaths::findExecutable("wl-paste");
  out += "wl-paste: " + (w.isEmpty() ? std::string("not installed") : w.toStdString()) + "\n";
  if (linux_::wayland()) {
    auto y = wl_primary();
    out += "PRIMARY by wl-paste: " + (y ? std::to_string(y->size()) + " bytes: " + y->substr(0, 200) : std::string("none")) + "\n";
  }
  out += std::string("GlobalShortcuts portal: ") + (Portal::get().available() ? "yes" : "no") + "\n";
  return out;
}

std::string front_app() {
  auto *d = x11().d;
  return d ? program_of(d, active_window(d)) : "";
}

bool mouse_down() {
  auto *d = x11().d;
  if (!d) return false;
  Window root = None, child = None;
  int rx = 0, ry = 0, wx = 0, wy = 0;
  unsigned mask = 0;
  XQueryPointer(d, DefaultRootWindow(d), &root, &child, &rx, &ry, &wx, &wy, &mask);
  return mask & (Button1Mask | Button3Mask);
}

void watch_input(std::function<void(Input)>) {}  // no icon at the selection on Linux (spec M10)

bool front_is_mine() {
  auto *d = x11().d;
  return d && window_pid(d, active_window(d)) == getpid();
}

bool compositing() {
  auto *d = x11().d;
  return d && XGetSelectionOwner(d, XInternAtom(d, ("_NET_WM_CM_S" + std::to_string(DefaultScreen(d))).c_str(), False)) != None;
}

// The window manager brings it up with the focus as a pager would (an app's own request may be refused).
void activate(void *window) {
  auto *d = x11().d;
  if (!d || !window) return;
  XEvent e{};
  e.xclient.type = ClientMessage;
  e.xclient.window = static_cast<Window>(reinterpret_cast<uintptr_t>(window));
  e.xclient.message_type = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
  e.xclient.format = 32;
  e.xclient.data.l[0] = 2;
  e.xclient.data.l[1] = CurrentTime;
  XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
  XFlush(d);
}

}  // namespace lexipeek::os

#include "input.moc"
