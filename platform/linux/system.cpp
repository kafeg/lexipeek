// Linux's machine and session: memory, autostart, the Secret Service (else a file only the user reads), one instance by
// a socket, the desktop file (the app menu, lexipeek://), the language.
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include "lexipeek/os.hpp"
#include "linux.hpp"

namespace lexipeek::os {

namespace linux_ {

namespace {
std::mutex error_m;
std::string error;  // the last failure's words, from any thread (the keyring's own)

std::filesystem::path env_path(const char *k) {
  auto *v = std::getenv(k);  // NOLINT(concurrency-mt-unsafe)
  return v && *v ? std::filesystem::path(v) : std::filesystem::path();
}
}  // namespace

void set_error(const std::string &e) {
  std::lock_guard g(error_m);
  error = e;
}

std::string program() {
  std::error_code e;
  auto p = std::filesystem::read_symlink("/proc/self/exe", e);
  return e ? std::string() : p.string();
}

std::filesystem::path config_home() {
  auto x = env_path("XDG_CONFIG_HOME");
  return x.empty() ? env_path("HOME") / ".config" : x;
}

std::filesystem::path data_home() {
  auto x = env_path("XDG_DATA_HOME");
  return x.empty() ? env_path("HOME") / ".local" / "share" : x;
}

bool wayland() { return !env_path("WAYLAND_DISPLAY").empty() || env_path("XDG_SESSION_TYPE") == "wayland"; }

bool write_if_changed(const std::filesystem::path &file, const std::string &text) {
  std::ifstream in(file, std::ios::binary);
  std::stringstream was;
  was << in.rdbuf();
  if (in.is_open() && was.str() == text) return true;
  std::error_code e;
  std::filesystem::create_directories(file.parent_path(), e);
  QSaveFile f(QString::fromStdString(file.string()));
  if (!f.open(QIODevice::WriteOnly) || f.write(text.data(), static_cast<qint64>(text.size())) < 0 || !f.commit())
    return set_error(f.errorString().toStdString()), false;
  return true;
}

std::string exec_quote(const std::string &arg) {
  std::string out = "\"";
  for (char c : arg) {
    if (c == '"' || c == '`' || c == '$' || c == '\\') out += "\\\\";  // escaped for Exec, then for the file's value
    out += c;
  }
  return out + "\"";
}

}  // namespace linux_

namespace {

using namespace linux_;

std::filesystem::path autostart_file() { return config_home() / "autostart" / (std::string(app_id) + ".desktop"); }
std::filesystem::path desktop_file() { return data_home() / "applications" / (std::string(app_id) + ".desktop"); }
std::filesystem::path keys_file() { return config_home() / "LexiPeek" / "keys.json"; }

// The desktop entry: the app menu and lexipeek:// (its MimeType), the program where it is now.
std::string desktop_entry(const std::string &args) {
  auto exe = program();
  auto icon = std::filesystem::path(exe).parent_path() / "lexipeek.png";
  std::error_code e;
  return "[Desktop Entry]\nType=Application\nName=LexiPeek\nGenericName=Pop-up dictionary\n"
         "Comment=Select text in any app, press a hotkey: a dictionary and translation card\n"
         "Exec=" + exec_quote(exe) + args + "\nIcon=" + (std::filesystem::exists(icon, e) ? icon.string() : std::string(app_id)) +
         "\nTerminal=false\nCategories=Office;Dictionary;Education;\nStartupNotify=false\nStartupWMClass=lexipeek\n";
}

// mimeapps.list's default for lexipeek:// (the file kept as it is otherwise).
const char *scheme_key = "x-scheme-handler/lexipeek=";
void set_default_handler(bool on) {
  auto file = config_home() / "mimeapps.list";
  std::ifstream in(file);
  std::vector<std::string> lines;
  for (std::string l; std::getline(in, l);)
    if (!l.starts_with(scheme_key)) lines.push_back(l);
  if (on) {
    auto section = std::find(lines.begin(), lines.end(), "[Default Applications]");
    if (section == lines.end()) lines.push_back("[Default Applications]"), section = lines.end() - 1;
    lines.insert(section + 1, scheme_key + std::string(app_id) + ".desktop");
  }
  std::string text;
  for (auto &l : lines) text += l + "\n";
  write_if_changed(file, text);
}

// The Secret Service through libsecret, loaded when it is there (no build or run dependency); nullptr: none.
struct Secret {
  struct Schema {
    const char *name;
    int flags;
    struct {
      const char *name;
      int type;
    } attributes[32];
    int reserved;
    void *reserved1, *reserved2, *reserved3, *reserved4, *reserved5, *reserved6, *reserved7;
  };
  struct Error {
    unsigned domain;
    int code;
    char *message;
  };
  char *(*lookup)(const Schema *, void *, Error **, ...);
  int (*store)(const Schema *, const char *, const char *, const char *, void *, Error **, ...);
  int (*clear)(const Schema *, void *, Error **, ...);
  void (*free_text)(char *);
  void (*free_error)(Error *);
  Schema schema{"dev.kafeg.lexipeek", 0, {{"provider", 0}}, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

  static Secret *get() {
    static Secret *s = [] () -> Secret * {
      auto *h = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
      if (!h) return nullptr;
      auto *x = new Secret;
      x->lookup = reinterpret_cast<decltype(x->lookup)>(dlsym(h, "secret_password_lookup_sync"));
      x->store = reinterpret_cast<decltype(x->store)>(dlsym(h, "secret_password_store_sync"));
      x->clear = reinterpret_cast<decltype(x->clear)>(dlsym(h, "secret_password_clear_sync"));
      x->free_text = reinterpret_cast<decltype(x->free_text)>(dlsym(h, "secret_password_free"));
      x->free_error = reinterpret_cast<decltype(x->free_error)>(dlsym(h, "g_error_free"));
      if (x->lookup && x->store && x->clear && x->free_text && x->free_error) return x;
      delete x;
      return nullptr;
    }();
    return s;
  }
  // false: the service failed (none running, locked and refused); its words in error.
  bool ok(Error *e) {
    if (!e) return true;
    set_error(e->message ? e->message : "Secret Service");
    free_error(e);
    return false;
  }
};
std::mutex secrets_m;

// A keyring call on a thread of its own: one that waits for a prompt nobody can answer (no prompter in the session)
// must not hold the app; after 30 s it counts as failed (the file instead) and its thread is left to end by itself.
template <class F>
auto keyring(F call) -> decltype(call()) {
  auto done = std::make_shared<std::promise<decltype(call())>>();
  auto got = done->get_future();
  std::thread([done, call] { done->set_value(call()); }).detach();
  if (got.wait_for(std::chrono::seconds(30)) == std::future_status::ready) return got.get();
  set_error("the keyring did not answer");
  return {};
}

QJsonObject read_keys() {
  QFile f(QString::fromStdString(keys_file().string()));
  return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

bool write_keys(const QJsonObject &o) {
  auto file = keys_file();
  std::error_code e;
  std::filesystem::create_directories(file.parent_path(), e);
  std::filesystem::permissions(file.parent_path(), std::filesystem::perms::owner_all, e);
  QSaveFile f(QString::fromStdString(file.string()));
  if (!f.open(QIODevice::WriteOnly)) return set_error(f.errorString().toStdString()), false;
  f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  f.write(QJsonDocument(o).toJson());
  return f.commit() || (set_error(f.errorString().toStdString()), false);
}

// One instance per user: a socket in $XDG_RUNTIME_DIR (the user's own, 0700), else a 0700 folder of ours in /tmp.
std::string socket_path() {
  auto run = env_path("XDG_RUNTIME_DIR");
  if (run.empty()) {
    run = "/tmp/lexipeek-" + std::to_string(getuid());
    mkdir(run.c_str(), 0700);
    struct stat st{};
    if (lstat(run.c_str(), &st) != 0 || st.st_uid != getuid() || !S_ISDIR(st.st_mode) || (st.st_mode & 077)) return "";
  }
  return (run / "lexipeek.sock").string();
}

int connect_to(const std::string &path) {
  if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) return -1;
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  std::strncpy(a.sun_path, path.c_str(), sizeof a.sun_path - 1);
  if (fd < 0 || ::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0) {
    if (fd >= 0) close(fd);
    return -1;
  }
  return fd;
}

bool same_user(int fd) {
  ucred c{};
  socklen_t n = sizeof c;
  return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &c, &n) == 0 && c.uid == getuid();
}

// The arguments as they came (none: a plain launch): NUL-separated, at most 1 MB, within a second; nullopt when not.
std::optional<std::vector<std::string>> read_args(int fd) {
  std::string all;
  char buf[4096];
  for (;;) {
    pollfd p{fd, POLLIN, 0};
    if (poll(&p, 1, 1000) <= 0) return std::nullopt;
    auto n = read(fd, buf, sizeof buf);
    if (n < 0) return std::nullopt;
    if (n == 0) break;
    all.append(buf, static_cast<size_t>(n));
    if (all.size() > (1 << 20)) return std::nullopt;
  }
  std::vector<std::string> out;
  for (size_t at = 0; at < all.size();) {
    auto end = all.find('\0', at);
    if (end == std::string::npos) end = all.size();
    out.push_back(all.substr(at, end - at));
    at = end + 1;
  }
  return out;
}

}  // namespace

int ram_mb() { return static_cast<int>(sysconf(_SC_PHYS_PAGES) / 1024 * sysconf(_SC_PAGESIZE) / 1024); }

// Less than 5 % of the memory available (the kernel's own estimate).
bool memory_low() {
  std::ifstream f("/proc/meminfo");
  long total = 0, available = -1;
  for (std::string k; f >> k;) {
    long v = 0;
    f >> v;
    if (k == "MemTotal:") total = v;
    if (k == "MemAvailable:") available = v;
    f.ignore(64, '\n');
  }
  return total > 0 && available >= 0 && available * 20 < total;
}

bool login() {
  std::ifstream f(autostart_file());
  std::string l;
  bool there = f.is_open();
  while (std::getline(f, l))
    if (l == "Hidden=true" || l == "X-GNOME-Autostart-enabled=false") there = false;
  return there;
}

void set_login(bool on) {
  if (!on) {
    std::error_code e;
    std::filesystem::remove(autostart_file(), e);
    return;
  }
  write_if_changed(autostart_file(), desktop_entry(" --login"));
}

std::string secret(const std::string &provider) {
  std::lock_guard g(secrets_m);
  if (auto *s = Secret::get()) {
    auto found = keyring([s, provider]() -> std::optional<std::string> {
      Secret::Error *e = nullptr;
      if (char *v = s->lookup(&s->schema, nullptr, &e, "provider", provider.c_str(), nullptr)) {
        std::string out = v;
        s->free_text(v);
        return out;
      }
      s->ok(e);
      return std::nullopt;
    });
    if (found) return *found;
  }
  return read_keys().value(QString::fromStdString(provider)).toString().toStdString();
}

bool set_secret(const std::string &provider, const std::string &value) {
  std::lock_guard g(secrets_m);
  auto keys = read_keys();
  auto name = QString::fromStdString(provider);
  bool kept = false;
  if (auto *s = Secret::get())
    kept = keyring([s, provider, value] {
      Secret::Error *e = nullptr;
      auto label = "LexiPeek: " + provider;
      if (value.empty()) s->clear(&s->schema, nullptr, &e, "provider", provider.c_str(), nullptr);
      int stored = !value.empty() && s->store(&s->schema, "default", label.c_str(), value.c_str(), nullptr, &e, "provider", provider.c_str(), nullptr);
      return s->ok(e) && (value.empty() || stored);
    });
  // In the keyring: none left in the file; else the file keeps it.
  if (kept || value.empty()) keys.remove(name);
  else keys[name] = QString::fromStdString(value);
  if (keys == read_keys()) return true;
  return write_keys(keys);
}

bool hand_over(const std::vector<std::string> &args) {
  int fd = connect_to(socket_path());
  if (fd < 0) return false;
  std::string all;
  for (auto &a : args) all += a + '\0';
  bool ok = same_user(fd);
  for (size_t at = 0; ok && at < all.size();) {
    auto n = write(fd, all.data() + at, all.size() - at);
    if (n <= 0) ok = false;
    else at += static_cast<size_t>(n);
  }
  close(fd);
  return ok;
}

void receive(std::function<void(const std::vector<std::string> &)> got) {
  auto path = socket_path();
  if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) return;
  // A file left by one that ended without removing it: nobody answers there.
  if (int old = connect_to(path); old >= 0) close(old);
  else unlink(path.c_str());
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  std::strncpy(a.sun_path, path.c_str(), sizeof a.sun_path - 1);
  if (fd < 0 || bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 || listen(fd, 8) != 0) {
    set_error(std::strerror(errno));
    if (fd >= 0) close(fd);
    return;
  }
  auto *n = new QSocketNotifier(fd, QSocketNotifier::Read, QCoreApplication::instance());
  QObject::connect(n, &QSocketNotifier::activated, [fd, got] {
    int c = accept4(fd, nullptr, nullptr, SOCK_CLOEXEC);
    if (c < 0) return;
    auto args = same_user(c) ? read_args(c) : std::nullopt;
    close(c);
    if (args && got) got(*args);
  });
}

void quit_running() {
  if (!hand_over({"--quit"})) return;
  for (int i = 0; i < 50; ++i) {
    int fd = connect_to(socket_path());
    if (fd < 0) return;
    close(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

bool too_old() { return false; }  // glibc's own check refuses an older system

// Nothing to install: the folder runs where it was unpacked (spec M10).
bool installed_here() { return true; }
std::string install(const std::string &, bool) { return ""; }
std::string installed_program() { return program(); }

// The menu entry, autostart and lexipeek://; the folder and the data stay.
void uninstall() {
  std::error_code e;
  std::filesystem::remove(desktop_file(), e);
  std::filesystem::remove(autostart_file(), e);
  set_default_handler(false);
}

void remove_old() {}

bool start(const std::string &program) { return QProcess::startDetached(QString::fromStdString(program), {}); }

void register_scheme() {
  if (!write_if_changed(desktop_file(), desktop_entry(" %u") + "MimeType=x-scheme-handler/lexipeek;\n")) return;
  set_default_handler(true);
  // Autostart follows the program when the folder moved.
  if (login()) set_login(true);
}

std::string scheme_command() {
  std::ifstream m(config_home() / "mimeapps.list");
  bool ours = false;
  for (std::string l; std::getline(m, l);) ours |= l == scheme_key + std::string(app_id) + ".desktop";
  std::ifstream f(desktop_file());
  for (std::string l; ours && std::getline(f, l);)
    if (l.starts_with("Exec=")) return l.substr(5);
  return "";
}

std::function<ai::CliRan(const ai::CliCall &)> cli_runner() { return nullptr; }  // the core's POSIX shell

std::string last_error() {
  std::lock_guard g(linux_::error_m);
  return linux_::error;
}

void round(void *) {}
std::vector<unsigned char> glyph_mask(const std::string &, char16_t, int) { return {}; }
std::string icon_font() { return ""; }
bool taskbar_light() { return false; }

// LANGUAGE's first, else LC_ALL, LC_MESSAGES, LANG ("ru" of "ru_RU.UTF-8").
std::string ui_language() {
  for (auto *k : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
    auto v = env_path(k).string();
    if (v.empty() || v == "C" || v == "POSIX") continue;
    return v.substr(0, v.find_first_of("_.:@"));
  }
  return "en";
}

int self_test(std::ostream &out) {
  int failed = 0;
  auto check = [&](bool ok, const std::string &what) {
    out << (ok ? "ok   " : "FAIL ") << what << "\n";
    failed += !ok;
  };
  const std::string provider = "self-test";
  auto said = [](bool ok) { return ok ? std::string() : " " + last_error(); };
  out << "info keys: " << (Secret::get() ? "libsecret" : "no libsecret: the file") << "\n";
  bool w = set_secret(provider, "k-123 ключ");
  check(w, "secret: written" + said(w));
  check(secret(provider) == "k-123 ключ", "secret: read back as written");
  bool r = set_secret(provider, "");
  check(r, "secret: removed" + said(r));
  check(secret(provider).empty(), "secret: gone");
  bool was = login();
  set_login(!was);
  check(login() == !was, "login: switched");
  set_login(was);
  check(login() == was, "login: back as it was");
  check(ram_mb() > 512, "ram: " + std::to_string(ram_mb()) + " MB");
  if (desktop_shortcuts().empty()) {
    Hotkey h{"Q", true, false, true, true};
    bool reg = register_hotkey(90, h, [] {});
    check(reg, "hotkey: Ctrl+Alt+Shift+Q registered" + said(reg));
    unregister_hotkey(90);
    check(register_hotkey(90, h, [] {}), "hotkey: registered again after unregistering");
    unregister_hotkey(90);
    check(!register_hotkey(91, Hotkey{"%", true, false, false, false}, [] {}), "hotkey: a key it cannot take refused");
  } else {
    out << "info hotkeys: the desktop's shortcuts, " << desktop_shortcuts() << " --lookup-selection\n";
  }
  register_scheme();
  auto scheme = scheme_command();
  check(scheme.find(program()) != std::string::npos, "lexipeek:// opens " + scheme);
  return failed;
}

}  // namespace lexipeek::os
