// Read aloud's engine: qwentts.cpp's C ABI (its own library, its ggml inside), a worker for one reading at a time.
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <sha256.h>
#include <stdexcept>
#include <thread>
#include "lexipeek/l10n.hpp"
#include "lexipeek/speech.hpp"
#include "lexipeek/views.hpp"
#include "mt/model_host.hpp"
#include "text/text.hpp"
#ifdef LEXIPEEK_TTS
#include <qwen.h>
#endif

namespace lexipeek::speech {

namespace fs = std::filesystem;

namespace {

bool write_file(const fs::path &p, const std::string &bytes) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  auto tmp = p;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!(f << bytes) || !f.flush()) return false;
  }
  fs::rename(tmp, p, ec);
  return !ec;
}

std::string read_file(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}

#ifdef LEXIPEEK_TTS
struct Model : mt::Held {
  qt_context *q = nullptr;
  ~Model() override { qt_free(q); }
};

void quiet(qt_log_level level, const char *msg, void *) {
  if (level >= QT_LOG_ERROR || std::getenv("LEXIPEEK_TTS_LOG")) std::fprintf(stderr, "[qwentts] %s\n", msg);
}

void set_cpu(bool cpu) {
#ifdef _WIN32
  _putenv_s("GGML_BACKEND", cpu ? "CPU" : "");
#else
  if (cpu) setenv("GGML_BACKEND", "CPU", 1);
  else unsetenv("GGML_BACKEND");
#endif
}

std::unique_ptr<mt::Held> load(const fs::path &data, bool gpu) {
  static std::once_flag logs;
  std::call_once(logs, [] { qt_log_set(quiet, nullptr); });
  auto dir = model_dir(data);
  auto talker = text::utf8_of(dir / model_files()[0].name), codec = text::utf8_of(dir / model_files()[1].name);
  qt_init_params p;
  qt_init_default_params(&p);
  p.talker_path = talker.c_str(), p.codec_path = codec.c_str();
  set_cpu(!gpu);
  auto m = std::make_unique<Model>();
  m->q = qt_init(&p);
  set_cpu(false);
  if (!m->q) throw std::runtime_error(qt_last_error());
  return m;
}

// The recording's speaker embedding and codec codes, as qwen-codec --talker writes them.
std::string encode(qt_context *q, const std::vector<float> &samples, Voice &v) {
  qt_voice_ref ref{};
  if (qt_extract_voice_ref(q, samples.data(), static_cast<int>(samples.size()), &ref) != QT_STATUS_OK) return qt_last_error();
  v.spk.assign(ref.ref_spk_emb, ref.ref_spk_emb + ref.ref_spk_dim);
  v.codes.assign(ref.ref_codes, ref.ref_codes + static_cast<size_t>(ref.num_codebooks) * static_cast<size_t>(ref.ref_T));
  v.frames = ref.ref_T;
  qt_voice_ref_free(&ref);
  return "";
}

bool cancelled(void *flag) { return static_cast<std::function<bool()> *>(flag)->operator()(); }
#endif

std::string save_voice(const fs::path &data, const Voice &v) {
  auto dir = data / "voices";
  std::string spk(reinterpret_cast<const char *>(v.spk.data()), v.spk.size() * sizeof(float));
  bool ok = write_file(dir / (v.id + ".spk"), spk) && write_file(dir / (v.id + ".rvq"), pack_codes(v.codes)) &&
            write_file(dir / (v.id + ".json"), nlohmann::json{{"lang", v.lang}, {"text", v.text}}.dump(2));
  return ok ? "" : tr("err_write", {text::utf8_of(dir)});
}

}  // namespace

bool built() {
#ifdef LEXIPEEK_TTS
  return true;
#else
  return false;
#endif
}

struct Speaker::Impl {
  fs::path data;
  mt::ModelHost host;
  mutable std::mutex mu;
  std::condition_variable cv;
  SpeechSettings s;
  int keep = 5;
  int last = 0;
  std::optional<std::pair<int, Read>> next;
  Reading cur;
  std::atomic<int> wanted{0};  // the ticket being made; another: cancel
  bool stop_ = false;
  std::thread worker;

  std::chrono::milliseconds idle() const { return std::chrono::minutes(keep); }

  void set(int ticket, Phase ph, std::string wav = "", std::string error = "", bool fix = false) {
    std::lock_guard g(mu);
    if (cur.ticket != ticket) return;
    cur.phase = ph, cur.wav = std::move(wav), cur.error = std::move(error), cur.fix = fix;
  }

  // Runs use with the model held (loaded first: the reading says so meanwhile).
  template <class F>
  void with_model(int ticket, bool gpu, F use) {
    host.with(
        gpu ? "qwen3-tts-gpu" : "qwen3-tts-cpu", idle(),
        [&]() -> std::unique_ptr<mt::Held> {
          set(ticket, Phase::loading);
#ifdef LEXIPEEK_TTS
          return load(data, gpu);
#else
          throw std::runtime_error(tr("read_not_built"));
#endif
        },
        [&](mt::Held &m) { use(m); });
  }

  void make(int ticket, const Read &r) {
    SpeechSettings st;
    {
      std::lock_guard g(mu);
      st = s;
    }
    if (!st.on) return set(ticket, Phase::failed, "", tr("read_off"), true);
    if (!built()) return set(ticket, Phase::failed, "", tr("read_not_built"));
    auto own = own_voices(data);
    auto v = voice_for(own, st, r.lang);
    if (v.id.empty()) return set(ticket, Phase::failed, "", tr("read_no_voice", {language_name(r.lang)}), true);
    auto file = reading_file(data, v, r);
    std::error_code ec;
    if (fs::exists(file, ec)) {
      fs::last_write_time(file, fs::file_time_type::clock::now(), ec);  // the newest kept
      return set(ticket, Phase::ready, text::utf8_of(file));
    }
    if (!model_installed(data)) return set(ticket, Phase::failed, "", tr("read_no_model"), true);
    try {
      with_model(ticket, st.gpu, [&](mt::Held &held) {
        set(ticket, Phase::making);
#ifdef LEXIPEEK_TTS
        auto *q = static_cast<Model &>(held).q;
        std::function<bool()> stale = [&] { return wanted != ticket; };
        auto lang = model_language(r.lang);
        qt_tts_params p;
        qt_tts_default_params(&p);
        p.text = r.text.c_str(), p.lang = lang.c_str(), p.ref_text = v.text.c_str();
        p.ref_spk_emb = v.spk.data(), p.ref_spk_dim = static_cast<int>(v.spk.size());
        p.ref_codes = v.codes.data(), p.ref_T = v.frames;
        p.seed = 42;  // the same text and voice: the same reading
        p.max_new_tokens = max_tokens(r.text);
        p.cancel = cancelled, p.cancel_user_data = &stale;
        qt_audio out{};
        auto status = qt_synthesize(q, &p, &out);
        if (status == QT_STATUS_CANCELLED || stale()) {
          qt_audio_free(&out);
          return set(ticket, Phase::idle);
        }
        if (status != QT_STATUS_OK) throw std::runtime_error(qt_last_error());
        std::vector<float> samples(out.samples, out.samples + out.n_samples);
        qt_audio_free(&out);
        if (!write_file(file, wav_bytes(samples, 24000))) throw std::runtime_error(tr("err_write", {text::utf8_of(file.parent_path())}));
        prune_readings(data);
        set(ticket, Phase::ready, text::utf8_of(file));
#else
        (void)held;
#endif
      });
    } catch (const std::exception &e) {
      set(ticket, Phase::failed, "", tr("read_failed", {e.what()}), true);
    }
  }

  void run() {
    for (;;) {
      std::pair<int, Read> job;
      {
        std::unique_lock l(mu);
        cv.wait(l, [this] { return stop_ || next; });
        if (stop_) return;
        job = std::move(*next);
        next.reset();
      }
      if (wanted == job.first) make(job.first, job.second);
    }
  }
};

Speaker::Speaker(fs::path data) : impl_(std::make_unique<Impl>()) {
  impl_->data = std::move(data);
  impl_->worker = std::thread([i = impl_.get()] { i->run(); });
}

Speaker::~Speaker() {
  {
    std::lock_guard g(impl_->mu);
    impl_->stop_ = true;
    impl_->wanted = -1;
  }
  impl_->cv.notify_all();
  impl_->worker.join();
}

void Speaker::configure(const SpeechSettings &s) {
  std::lock_guard g(impl_->mu);
  if (!s.on) impl_->host.release();
  impl_->s = s, impl_->keep = s.keep;
  impl_->host.set_idle(impl_->idle());
}

int Speaker::read(const Read &r) {
  int t;
  {
    std::lock_guard g(impl_->mu);
    t = ++impl_->last;
    impl_->next = {t, r};
    impl_->cur = {t, Phase::making, r};
    impl_->wanted = t;
  }
  impl_->cv.notify_all();
  return t;
}

void Speaker::stop() {
  std::lock_guard g(impl_->mu);
  impl_->wanted = 0;
  impl_->next.reset();
  impl_->cur = {};
}

Reading Speaker::state() const {
  std::lock_guard g(impl_->mu);
  return impl_->cur;
}

std::string Speaker::add_voice(const std::string &wav, const std::string &said, const std::string &lang) {
  auto a = read_wav(read_file(text::path_of(wav)));
  if (!a.error.empty()) return a.error;
  double secs = static_cast<double>(a.samples.size()) / 24000.0;
  if (secs < 2 || secs > 30) return tr("voice_length");
  auto t = text::trim(said);
  if (t.empty()) return tr("voice_no_text");
  if (!model_installed(impl_->data)) return tr("read_no_model");
  SHA256 h;
  Voice v{h(lang + "\n" + t + "\n" + std::to_string(a.samples.size())).substr(0, 12), lang, t, "own"};
  std::string err;
  try {
    bool gpu;
    {
      std::lock_guard g(impl_->mu);
      gpu = impl_->s.gpu;
    }
    impl_->with_model(-1, gpu, [&](mt::Held &held) {
#ifdef LEXIPEEK_TTS
      err = encode(static_cast<Model &>(held).q, a.samples, v);
#else
      (void)held;
#endif
    });
  } catch (const std::exception &e) {
    return tr("read_failed", {e.what()});
  }
  return err.empty() ? save_voice(impl_->data, v) : err;
}

std::string Speaker::remove_voice(const std::string &id) {
  std::error_code ec;
  for (auto ext : {".json", ".spk", ".rvq"}) fs::remove(impl_->data / "voices" / (id + ext), ec);
  return "";
}

void Speaker::release() { impl_->host.release(); }
bool Speaker::held() const { return !impl_->host.held().empty(); }
std::chrono::steady_clock::time_point Speaker::until() const { return impl_->host.until(); }

}  // namespace lexipeek::speech
