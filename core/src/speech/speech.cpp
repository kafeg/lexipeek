// Read aloud's plain parts: the model's files, languages, a reading's length, voices, WAV, the kept readings.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>
#include <tuple>
#include <nlohmann/json.hpp>
#include <sha256.h>
#include "lexipeek/speech.hpp"
#include "packs/http.hpp"
#include "text/text.hpp"

namespace lexipeek::speech {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

struct Embedded {
  const char *id, *lang, *text;
  const unsigned char *spk;
  std::size_t spk_n;
  const unsigned char *rvq;
  std::size_t rvq_n;
};
#include "speech/voices.inc"

std::string read_file(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}

std::string sha256_file(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  SHA256 h;
  std::vector<char> buf(1 << 16);
  while (f.read(buf.data(), static_cast<std::streamsize>(buf.size())) || f.gcount() > 0) h.add(buf.data(), static_cast<size_t>(f.gcount()));
  return h.getHash();
}

std::vector<float> floats(const std::string &b) {
  std::vector<float> out(b.size() / sizeof(float));
  if (!out.empty()) std::memcpy(out.data(), b.data(), out.size() * sizeof(float));
  return out;
}

Voice with_codes(Voice v, const std::string &spk, const std::string &rvq) {
  v.spk = floats(spk);
  v.codes = unpack_codes(rvq);
  v.frames = static_cast<int>(v.codes.size() / codebooks);
  return v;
}

std::uint32_t le(const std::string &b, size_t at, int n) {
  std::uint32_t x = 0;
  for (int i = 0; i < n; i++) x |= static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + static_cast<size_t>(i)])) << (8 * i);
  return x;
}

// torchaudio's resample (Hann-windowed sinc, rolloff 0.99, 6 zero crossings), as qwentts.cpp reads a reference clip.
std::vector<float> resample(const std::vector<float> &in, int from, int to) {
  int g = std::gcd(from, to), orig = from / g, nw = to / g;
  double base = std::min(orig, nw) * 0.99, pi = std::acos(-1.0);
  int width = static_cast<int>(std::ceil(6.0 * orig / base)), k = 2 * width + orig;
  std::vector<float> kernel(static_cast<size_t>(nw) * static_cast<size_t>(k));
  for (int j = 0; j < nw; j++)
    for (int i = 0; i < k; i++) {
      double t = std::clamp((-static_cast<double>(j) / nw + static_cast<double>(i - width) / orig) * base, -6.0, 6.0);
      double w = std::cos(t * pi / 12.0), tp = t * pi;
      kernel[static_cast<size_t>(j * k + i)] = static_cast<float>((tp == 0 ? 1.0 : std::sin(tp) / tp) * w * w * base / orig);
    }
  std::vector<float> padded(in.size() + static_cast<size_t>(2 * width + orig), 0.0f);
  std::copy(in.begin(), in.end(), padded.begin() + width);
  auto frames = (padded.size() - static_cast<size_t>(k)) / static_cast<size_t>(orig) + 1;
  auto n = std::min(frames * static_cast<size_t>(nw), static_cast<size_t>(std::ceil(static_cast<double>(to) * static_cast<double>(in.size()) / from)));
  std::vector<float> out(n);
  for (size_t o = 0; o < n; o++) {
    const float *w = kernel.data() + (o % static_cast<size_t>(nw)) * static_cast<size_t>(k);
    const float *x = padded.data() + (o / static_cast<size_t>(nw)) * static_cast<size_t>(orig);
    float sum = 0;
    for (int i = 0; i < k; i++) sum += x[i] * w[i];
    out[o] = sum;
  }
  return out;
}

}  // namespace

const std::vector<ModelFile> &model_files() {
  static const std::string base = "https://huggingface.co/Serveurperso/Qwen3-TTS-GGUF/resolve/b7ee2e8c7459c3bea99da23e3d178125a7d1713c/";
  static const std::vector<ModelFile> files{
      {"qwen-talker-1.7b-base-Q8_0.gguf", base + "qwen-talker-1.7b-base-Q8_0.gguf",
       "4b9a33a236908dd9435a42f7a396e38038329d053b704342a6413c08544c4fda", 2'079'448'256},
      {"qwen-tokenizer-12hz-Q8_0.gguf", base + "qwen-tokenizer-12hz-Q8_0.gguf",
       "1883beeed99348fc35e23dd225e9082f93f6f8c109330a33d935baa8acdbfd94", 291'150'624}};
  return files;
}

std::int64_t model_bytes() {
  std::int64_t n = 0;
  for (auto &f : model_files()) n += f.bytes;
  return n;
}

fs::path model_dir(const fs::path &data) { return data / "tts"; }

bool model_installed(const fs::path &data) {
  std::error_code ec;
  for (auto &f : model_files())
    if (fs::file_size(model_dir(data) / f.name, ec) != static_cast<std::uintmax_t>(f.bytes) || ec) return false;
  return true;
}

Status download_model(const fs::path &data, const Progress &progress, const Cancel &cancel) {
  std::error_code ec;
  fs::create_directories(model_dir(data), ec);
  std::int64_t before = 0, all = model_bytes();
  for (auto &f : model_files()) {
    auto file = model_dir(data) / f.name;
    if (fs::file_size(file, ec) == static_cast<std::uintmax_t>(f.bytes) && !ec) {
      before += f.bytes;
      continue;
    }
    auto part = file;
    part += ".part";
    auto st = http::download(f.url, part, f.bytes, [&](std::int64_t done, std::int64_t) { if (progress) progress(before + done, all); }, cancel);
    if (!st.ok()) return st;
    if (sha256_file(part) != f.sha256) {
      fs::remove(part, ec);
      return {"sha256 mismatch: " + f.name};
    }
    fs::rename(part, file, ec);
    if (ec) return {ec.message()};
    before += f.bytes;
  }
  return {};
}

void remove_model(const fs::path &data) {
  std::error_code ec;
  fs::remove_all(model_dir(data), ec);
}

const std::vector<std::string> &languages() {
  static const std::vector<std::string> l{"de", "en", "ru", "fr", "es", "it", "pl", "pt"};
  return l;
}

bool speaks(const std::string &lang) { return std::find(languages().begin(), languages().end(), lang) != languages().end(); }

std::string model_language(const std::string &lang) {
  static const std::map<std::string, std::string> names{{"de", "german"},  {"en", "english"}, {"ru", "russian"},   {"fr", "french"},
                                                        {"es", "spanish"}, {"it", "italian"}, {"pt", "portuguese"}};
  auto i = names.find(lang);
  return i == names.end() ? "auto" : i->second;
}

int max_tokens(const std::string &t) {
  static const text::Regex digit("\\p{Nd}"), syllabic("[\\p{Han}\\p{Hiragana}\\p{Katakana}\\p{Hangul}]");
  auto all = static_cast<double>(std::count_if(t.begin(), t.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
  auto digits = static_cast<double>(digit.all(t).size()), syl = static_cast<double>(syllabic.all(t).size());
  double said = 0.08 * (all - digits - syl) + 0.45 * digits + 0.3 * syl;
  return std::min(2048, static_cast<int>(std::ceil(12.5 * (2 + 2 * said))));
}

std::vector<std::int32_t> unpack_codes(const std::string &in) {
  size_t n = in.size() * 8 / code_bits;
  n -= n % codebooks;
  std::vector<std::int32_t> out(n);
  std::uint64_t acc = 0;
  int bits = 0;
  size_t at = 0;
  for (auto &c : out) {
    while (bits < code_bits && at < in.size()) acc |= static_cast<std::uint64_t>(static_cast<unsigned char>(in[at++])) << bits, bits += 8;
    c = static_cast<std::int32_t>(acc & ((1u << code_bits) - 1));
    acc >>= code_bits, bits -= code_bits;
  }
  return out;
}

std::string pack_codes(const std::vector<std::int32_t> &codes) {
  std::string out;
  std::uint64_t acc = 0;
  int bits = 0;
  for (auto c : codes) {
    acc |= static_cast<std::uint64_t>(static_cast<std::uint32_t>(c) & ((1u << code_bits) - 1)) << bits;
    for (bits += code_bits; bits >= 8; bits -= 8, acc >>= 8) out += static_cast<char>(acc & 0xFF);
  }
  if (bits > 0) out += static_cast<char>(acc & 0xFF);
  return out;
}

std::vector<Voice> app_voices() {
  std::vector<Voice> out;
  for (auto &e : embedded)
    out.push_back(with_codes({e.id, e.lang, e.text, "app"}, std::string(reinterpret_cast<const char *>(e.spk), e.spk_n),
                             std::string(reinterpret_cast<const char *>(e.rvq), e.rvq_n)));
  return out;
}

std::vector<Voice> own_voices(const fs::path &data, const fs::path &home) {
  std::vector<Voice> out, own;
  std::error_code ec;
  // SprachAnker's word voices on this computer: <home>/SprachAnker/models/word-voices/<lang>.json and <lang>-<id>.wav.
  auto sa = home / "SprachAnker" / "models" / "word-voices";
  for (auto &lang : languages()) {
    if (home.empty()) break;
    try {
      auto j = json::parse(read_file(sa / (lang + ".json")));
      std::string id = j.value("id", ""), said = j.value("text", "");
      auto wav = sa / (lang + "-" + id + ".wav");
      if (id.empty() || said.empty() || !std::all_of(id.begin(), id.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); }) || !fs::exists(wav, ec)) continue;
      Voice v{"sprachanker-" + lang + "-" + id, lang, said, "sprachanker"};
      auto mine = data / "voices" / (v.id + ".spk");
      if (fs::exists(mine, ec)) v = with_codes(v, read_file(mine), read_file(data / "voices" / (v.id + ".rvq")));
      v.wav = wav;
      out.push_back(v);
    } catch (const std::exception &) {
    }
  }
  for (auto &f : fs::directory_iterator(data / "voices", ec)) {
    auto p = f.path();
    if (p.extension() != ".json") continue;
    try {
      auto j = json::parse(read_file(p));
      auto id = text::utf8_of(p.stem());
      if (id.starts_with("sprachanker-")) continue;
      auto stem = p.parent_path() / p.stem();
      own.push_back(with_codes({id, j.value("lang", ""), j.value("text", ""), "own"}, read_file(fs::path(stem) += ".spk"),
                               read_file(fs::path(stem) += ".rvq")));
    } catch (const std::exception &) {
    }
  }
  std::sort(own.begin(), own.end(), [](auto &a, auto &b) { return std::tie(a.lang, a.text) < std::tie(b.lang, b.text); });
  out.insert(out.end(), own.begin(), own.end());
  return out;
}

Voice voice_for(const std::vector<Voice> &own, const SpeechSettings &s, const std::string &lang) {
  for (auto &v : own)
    if (v.lang == lang && std::find(s.off.begin(), s.off.end(), v.id) == s.off.end()) return v;
  auto id = lang + (s.male ? "-male" : "-female");
  for (auto &v : app_voices())
    if (v.id == id) return v;
  return {};
}

Audio read_wav(const std::string &b) {
  Audio a;
  if (b.size() < 12 || b.compare(0, 4, "RIFF") != 0 || b.compare(8, 4, "WAVE") != 0) return a.error = "not a WAV file", a;
  int format = 0, channels = 0, bits = 0;
  std::uint32_t rate = 0;
  for (size_t at = 12; at + 8 <= b.size();) {
    auto id = b.substr(at, 4);
    size_t n = le(b, at + 4, 4), body = at + 8;
    if (body + n > b.size()) n = b.size() - body;
    if (id == "fmt " && n >= 16) {
      format = static_cast<int>(le(b, body, 2)), channels = static_cast<int>(le(b, body + 2, 2));
      rate = le(b, body + 4, 4), bits = static_cast<int>(le(b, body + 14, 2));
      if (format == 0xFFFE && n >= 26) format = static_cast<int>(le(b, body + 24, 2));  // WAVE_FORMAT_EXTENSIBLE: its subformat
    } else if (id == "data") {
      if (channels <= 0 || rate == 0 || !((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32)))
        return a.error = "unsupported WAV format (16/24/32-bit PCM or 32-bit float)", a;
      size_t step = static_cast<size_t>(bits / 8), frames = n / (step * static_cast<size_t>(channels));
      std::vector<float> mono(frames);
      for (size_t i = 0; i < frames; i++) {
        double sum = 0;
        for (int c = 0; c < channels; c++) {
          size_t p = body + (i * static_cast<size_t>(channels) + static_cast<size_t>(c)) * step;
          if (format == 3) {
            float f;
            std::uint32_t u = le(b, p, 4);
            std::memcpy(&f, &u, 4);
            sum += f;
          } else {
            auto u = le(b, p, bits / 8);
            auto sign = 1u << (bits - 1);
            auto v = static_cast<double>(static_cast<std::int64_t>(u ^ sign) - static_cast<std::int64_t>(sign));
            sum += v / static_cast<double>(sign);
          }
        }
        mono[i] = static_cast<float>(sum / channels);
      }
      a.samples = rate == 24000 ? std::move(mono) : resample(mono, static_cast<int>(rate), 24000);
      return a;
    }
    at = body + n + (n & 1);
  }
  return a.error = "no audio in the WAV file", a;
}

std::string wav_bytes(const std::vector<float> &samples, int rate) {
  auto u32 = [](std::string &o, std::uint32_t x) {
    for (int i = 0; i < 4; i++) o += static_cast<char>((x >> (8 * i)) & 0xFF);
  };
  auto u16 = [](std::string &o, std::uint32_t x) { o += static_cast<char>(x & 0xFF), o += static_cast<char>((x >> 8) & 0xFF); };
  auto data = static_cast<std::uint32_t>(samples.size() * 2);
  std::string o = "RIFF";
  u32(o, 36 + data);
  o += "WAVEfmt ";
  u32(o, 16), u16(o, 1), u16(o, 1), u32(o, static_cast<std::uint32_t>(rate)), u32(o, static_cast<std::uint32_t>(rate * 2)), u16(o, 2), u16(o, 16);
  o += "data";
  u32(o, data);
  for (float f : samples) {
    auto v = static_cast<std::int16_t>(std::lround(std::clamp(f, -1.0f, 1.0f) * 32767.0f));
    u16(o, static_cast<std::uint16_t>(v));
  }
  return o;
}

fs::path reading_file(const fs::path &data, const Voice &v, const Read &r) {
  SHA256 h;
  auto key = "qwen3-tts-1.7b-base-q8\n" + v.id + "\n" + r.lang + "\n" + r.text;
  return data / "readings" / (h(key) + ".wav");
}

void prune_readings(const fs::path &data, std::size_t keep) {
  std::error_code ec;
  std::vector<std::pair<fs::file_time_type, fs::path>> all;
  for (auto &f : fs::directory_iterator(data / "readings", ec))
    if (f.path().extension() == ".wav") all.push_back({f.last_write_time(ec), f.path()});
  if (all.size() <= keep) return;
  std::sort(all.begin(), all.end(), [](auto &a, auto &b) { return a.first > b.first; });
  for (size_t i = keep; i < all.size(); i++) fs::remove(all[i].second, ec);
}

}  // namespace lexipeek::speech
