// The three methods (as InkPeek's mt/Engine.kt, mt/Llm.kt): models found in the data dir by the packs list.
#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
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

  Plan plan(const std::string &from, const std::string &to, Method m) {
    Plan p;
    if (m == Method::fast) {
      p.key = "fast:" + from + "-" + to;
      p.by = "Mozilla";
      for (auto &[a, b] : steps(from, to)) {
        auto d = layout.model(a, b);
        if (!std::filesystem::exists(d / "set")) {
          p.error = "No Mozilla model " + a + "-" + b + " (model-" + a + "-" + b + ")";
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
      p.error = std::string("No ") + kind + " model covers " + from + "-" + to;
      return p;
    }
    p.c = *c;
    if (m == Method::direct) {
      p.key = "direct:" + c->id;
      p.by = "OPUS-MT";
      p.dirs = {layout.opus(c->id)};
      if (auto t = c->tags.find(to); t != c->tags.end()) p.tag = t->second;
      if (!std::filesystem::exists(p.dirs[0])) p.error = "Not installed: " + c->id;
    } else {
      p.key = "llm:" + c->id;
      p.by = c->name;
      p.llm = true;
      p.gguf = layout.llm(c->id);
      if (!std::filesystem::exists(p.gguf)) p.error = "Not installed: " + c->id;
    }
    return p;
  }

  std::unique_ptr<Held> load(const Plan &p) {
    if (p.llm) {
      auto h = std::make_unique<Llm>();
      int threads = o.threads > 0 ? o.threads : std::max(1, std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 8) / 2);
      h->model = std::make_unique<LlmModel>(p.gguf.string(), threads);
      return h;
    }
    auto h = std::make_unique<Bergamots>();
    for (auto &d : p.dirs) {
      auto cfg = bergamot_config(d);
      if (cfg.empty()) throw std::runtime_error("Missing model file in " + d.string());
      h->steps.push_back(std::make_unique<BergamotModel>(cfg));
    }
    return h;
  }

  std::chrono::milliseconds idle(const Plan &p) const { return p.llm ? o.idle_llm : o.idle_bergamot; }
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
  try {
    impl_->host.with(p.key, impl_->idle(p), [&] { return impl_->load(p); }, [](Held &) {});
  } catch (const std::exception &e) {
    return e.what();
  }
  return "";
}

Translation Translator::translate(const std::string &from, const std::string &to, const std::string &text, Method m) {
  Translation r;
  if (from == to) {
    r.text = text;
    return r;
  }
  auto p = impl_->plan(from, to, m);
  r.by = p.by;
  if (!p.error.empty()) {
    r.error = p.error;
    return r;
  }
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
          auto t = trim(model.generate(fill(p.c.prompt, name(from), name(to), piece), static_cast<int>(utf16_length(piece) * 2 + 32)));
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
        for (auto &s : sentences(text)) out += (out.empty() ? "" : " ") + model.translate(p.tag + " " + s);
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

void Translator::release() { impl_->host.release(); }

std::string Translator::held() const { return impl_->host.held(); }

}  // namespace lexipeek
