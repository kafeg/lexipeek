// The three methods (as InkPeek's mt/Engine.kt, mt/Llm.kt): models found in the data dir by the packs list.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include "lexipeek/l10n.hpp"
#include "lexipeek/mt.hpp"
#include "lexipeek/packs.hpp"
#include "mt/engines.hpp"
#include "mt/model_host.hpp"
#include "mt/text.hpp"

namespace lexipeek {

std::string_view method_name(Method m) {
  switch (m) {
    case Method::fast: return "fast";
    case Method::direct: return "direct";
    case Method::llm: return "llm";
  }
  return "";
}

std::optional<Method> method_from(std::string_view s) {
  for (auto m : {Method::fast, Method::direct, Method::llm})
    if (method_name(m) == s) return m;
  return std::nullopt;
}

namespace {

using namespace mt;

struct Bergamots : Held {
  std::vector<std::unique_ptr<BergamotModel>> steps;
};

struct Llm : Held {
  std::unique_ptr<LlmModel> model;
};

// What a method needs for from → to: the host key, how to load it, how to translate with it.
struct Plan {
  std::string key, error, by;
  bool llm = false;
  std::vector<std::filesystem::path> dirs;  // Bergamot model folders, in order
  std::string tag;                          // OPUS-MT's target token
  std::filesystem::path gguf;
  Component c;
};

std::string read(const std::filesystem::path &p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

}  // namespace

struct Translator::Impl {
  TranslatorOptions o;
  Layout layout;
  ModelHost host;
  std::mutex lid_mu;
  std::unique_ptr<LangIdModel> lid;
  std::atomic<bool> stop{false};
  std::atomic<int> tag{0};  // the running translation's
  std::atomic<long long> idle_ms{-1};  // set_idle's; -1: the options'
  std::mutex about_mu;
  std::map<std::string, std::pair<std::string, std::int64_t>> about;  // key → title, bytes of what was loaded

  Plan plan(const std::string &from, const std::string &to, Method m) {
    Plan p;
    if (m == Method::fast) {
      p.key = "fast:" + from + "-" + to;
      p.by = "Mozilla";
      for (auto &[a, b] : steps(from, to)) {
        auto d = layout.model(a, b);
        if (!std::filesystem::exists(d / "set")) {
          p.error = tr("mt_no_mozilla", {a + "-" + b, "model-" + a + "-" + b});
          return p;
        }
        p.dirs.push_back(d);
      }
      return p;
    }
    auto manifest = parse_manifest(read(layout.manifest()));
    if (!manifest.error.empty()) {
      p.error = manifest.error;
      return p;
    }
    auto kind = m == Method::direct ? "opus" : "llm";
    auto c = covering(manifest, kind, from, to);
    if (!c) {
      p.error = tr("mt_no_cover", {m == Method::direct ? "OPUS-MT" : "Hy-MT2", from + " → " + to});
      return p;
    }
    p.c = *c;
    if (m == Method::direct) {
      p.key = "direct:" + c->id;
      p.by = "OPUS-MT";
      p.dirs = {layout.opus(c->id)};
      if (auto t = c->tags.find(to); t != c->tags.end()) p.tag = t->second;
      if (!std::filesystem::exists(p.dirs[0])) p.error = tr("not_installed", {p.by});
    } else {
      p.key = "llm:" + c->id;
      p.by = c->name;
      p.llm = true;
      p.gguf = layout.llm(c->id);
      if (!std::filesystem::exists(p.gguf)) p.error = tr("not_installed", {p.by.empty() ? c->id : p.by});
      // This build's llama.cpp runs only with them on x86-64 (AVX2, FMA): without, it would stop on its first one.
      if (cpu_arch() == "x86_64") {
        auto have = cpu_features();
        for (auto f : {"avx2", "fma"})
          if (std::find(have.begin(), have.end(), f) == have.end()) p.error = tr("mt_cannot_features", {"avx2, fma"});
      }
    }
    return p;
  }

  // The menu's name of the model: the pair it serves now (one model may serve several).
  void name(const Plan &p, const std::string &from, const std::string &to) {
    std::lock_guard g(about_mu);
    about[p.key].first = p.by + " " + from + " → " + to;
  }

  std::unique_ptr<Held> load(const Plan &p) {
    std::int64_t bytes = 0;
    std::error_code ec;
    auto add = [&](const std::filesystem::path &f) {
      if (std::filesystem::is_regular_file(f, ec)) bytes += static_cast<std::int64_t>(std::filesystem::file_size(f, ec));
    };
    for (auto &d : p.dirs)
      for (auto &e : std::filesystem::recursive_directory_iterator(d, ec)) add(e.path());
    if (p.llm) add(p.gguf);
    {
      std::lock_guard g(about_mu);
      about[p.key].second = bytes;
    }
    if (p.llm) {
      auto h = std::make_unique<Llm>();
      int threads = o.threads > 0 ? o.threads : std::max(1, std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 8) / 2);
      h->model = std::make_unique<LlmModel>(p.gguf.string(), threads);
      return h;
    }
    auto h = std::make_unique<Bergamots>();
    for (auto &d : p.dirs) {
      auto cfg = bergamot_config(d);
      if (cfg.empty()) throw std::runtime_error(tr("mt_cannot_load"));
      h->steps.push_back(std::make_unique<BergamotModel>(cfg));
    }
    return h;
  }

  std::chrono::milliseconds idle(const Plan &p) const {
    auto ms = idle_ms.load();
    return ms >= 0 ? std::chrono::milliseconds(ms) : p.llm ? o.idle_llm : o.idle_bergamot;
  }
};

Translator::Translator(TranslatorOptions o) : impl_(std::make_unique<Impl>()) {
  impl_->layout = {o.data};
  impl_->o = std::move(o);
}

Translator::~Translator() = default;

std::string Translator::why_not(const std::string &from, const std::string &to, Method m) {
  return from == to ? "" : impl_->plan(from, to, m).error;
}

std::string Translator::load(const std::string &from, const std::string &to, Method m) {
  auto p = impl_->plan(from, to, m);
  if (!p.error.empty()) return p.error;
  impl_->name(p, from, to);
  try {
    impl_->host.with(p.key, impl_->idle(p), [&] { return impl_->load(p); }, [](Held &) {});
  } catch (const std::exception &e) {
    return e.what();
  }
  return "";
}

Translation Translator::translate(const std::string &from, const std::string &to, const std::string &text, Method m, int tag) {
  Translation r;
  if (from == to) {
    r.text = text;
    return r;
  }
  impl_->tag = tag;
  impl_->stop = false;
  auto p = impl_->plan(from, to, m);
  r.by = p.by;
  if (!p.error.empty()) {
    r.error = p.error;
    return r;
  }
  impl_->name(p, from, to);
  try {
    impl_->host.with(p.key, impl_->idle(p), [&] { return impl_->load(p); }, [&](Held &h) {
      if (p.llm) {
        auto &model = *static_cast<Llm &>(h).model;
        auto name = [&](const std::string &code) {
          auto n = p.c.names.find(code);
          return n != p.c.names.end() ? n->second : code;
        };
        std::string out;
        for (auto &piece : chunks(text)) {
          auto t = trim(model.generate(fill(p.c.prompt, name(from), name(to), piece), static_cast<int>(utf16_length(piece) * 2 + 32), &impl_->stop));
          out += (out.empty() ? "" : " ") + t;
        }
        r.text = out;
        return;
      }
      auto &steps = static_cast<Bergamots &>(h).steps;
      if (m == Method::direct) {
        // A multi-target model needs its tag before every sentence.
        auto &model = *steps[0];
        if (p.tag.empty()) {
          r.text = model.translate(text);
          return;
        }
        std::string out;
        bool first = true;
        for (auto &s : sentences(text)) {
          out += (first ? "" : " ") + (trim(s).empty() ? s : model.translate(p.tag + " " + s));
          first = false;
        }
        r.text = trim(out);
        return;
      }
      std::string t = text;
      for (size_t i = 0; i < steps.size(); i++) {
        t = steps[i]->translate(t);
        if (steps.size() > 1 && i == 0) r.english = t;
      }
      r.text = t;
    });
  } catch (const std::exception &e) {
    r.error = e.what();
  }
  return r;
}

std::vector<Language> Translator::langid(const std::string &text, int k) {
  std::lock_guard g(impl_->lid_mu);
  try {
    if (!impl_->lid) impl_->lid = std::make_unique<LangIdModel>(impl_->layout.langid().string());
    return impl_->lid->detect(text, k, 0.01f);
  } catch (const std::exception &) {
    impl_->lid.reset();
    return {};
  }
}

void Translator::cancel(int tag) {
  if (tag == 0 || impl_->tag == tag) impl_->stop = true;
}

void Translator::release() { impl_->host.release(); }

std::string Translator::held() const { return impl_->host.held(); }

void Translator::set_idle(std::chrono::milliseconds idle) {
  impl_->idle_ms = idle.count();
  impl_->host.set_idle(idle);
}

Translator::Memory Translator::memory() const {
  Memory m;
  m.key = impl_->host.held();
  if (m.key.empty()) return m;
  {
    std::lock_guard g(impl_->about_mu);
    if (auto a = impl_->about.find(m.key); a != impl_->about.end()) m.title = a->second.first, m.bytes = a->second.second;
  }
  auto until = impl_->host.until();
  if (until == ModelHost::clock::time_point::max()) m.seconds = -1;
  else m.seconds = std::max<long long>(0, std::chrono::duration_cast<std::chrono::seconds>(until - ModelHost::clock::now()).count());
  return m;
}

}  // namespace lexipeek
