// Public StarDict sources (InkPeek's Catalog.kt): their listings, a download into dicts-own.
#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include "lexipeek/dicts.hpp"
#include "packs/http.hpp"
#include "text/text.hpp"

namespace lexipeek {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

struct Lang {
  const char *two, *three, *name;
};

// ISO 639-1 with its 639-2/T code (Java's Locale.getISO3Language) and English name.
const Lang LANGS[] = {
    {"aa", "aar", "Afar"}, {"ab", "abk", "Abkhazian"}, {"af", "afr", "Afrikaans"}, {"ak", "aka", "Akan"}, {"am", "amh", "Amharic"},
    {"an", "arg", "Aragonese"}, {"ar", "ara", "Arabic"}, {"as", "asm", "Assamese"}, {"av", "ava", "Avaric"}, {"ay", "aym", "Aymara"},
    {"az", "aze", "Azerbaijani"}, {"ba", "bak", "Bashkir"}, {"be", "bel", "Belarusian"}, {"bg", "bul", "Bulgarian"}, {"bi", "bis", "Bislama"},
    {"bm", "bam", "Bambara"}, {"bn", "ben", "Bengali"}, {"bo", "bod", "Tibetan"}, {"br", "bre", "Breton"}, {"bs", "bos", "Bosnian"},
    {"ca", "cat", "Catalan"}, {"ce", "che", "Chechen"}, {"ch", "cha", "Chamorro"}, {"co", "cos", "Corsican"}, {"cr", "cre", "Cree"},
    {"cs", "ces", "Czech"}, {"cu", "chu", "Church Slavic"}, {"cv", "chv", "Chuvash"}, {"cy", "cym", "Welsh"}, {"da", "dan", "Danish"},
    {"de", "deu", "German"}, {"dv", "div", "Divehi"}, {"dz", "dzo", "Dzongkha"}, {"ee", "ewe", "Ewe"}, {"el", "ell", "Greek"},
    {"en", "eng", "English"}, {"eo", "epo", "Esperanto"}, {"es", "spa", "Spanish"}, {"et", "est", "Estonian"}, {"eu", "eus", "Basque"},
    {"fa", "fas", "Persian"}, {"ff", "ful", "Fula"}, {"fi", "fin", "Finnish"}, {"fj", "fij", "Fijian"}, {"fo", "fao", "Faroese"},
    {"fr", "fra", "French"}, {"fy", "fry", "Western Frisian"}, {"ga", "gle", "Irish"}, {"gd", "gla", "Scottish Gaelic"}, {"gl", "glg", "Galician"},
    {"gn", "grn", "Guarani"}, {"gu", "guj", "Gujarati"}, {"gv", "glv", "Manx"}, {"ha", "hau", "Hausa"}, {"he", "heb", "Hebrew"},
    {"hi", "hin", "Hindi"}, {"ho", "hmo", "Hiri Motu"}, {"hr", "hrv", "Croatian"}, {"ht", "hat", "Haitian"}, {"hu", "hun", "Hungarian"},
    {"hy", "hye", "Armenian"}, {"hz", "her", "Herero"}, {"ia", "ina", "Interlingua"}, {"id", "ind", "Indonesian"}, {"ie", "ile", "Interlingue"},
    {"ig", "ibo", "Igbo"}, {"ii", "iii", "Sichuan Yi"}, {"ik", "ipk", "Inupiaq"}, {"io", "ido", "Ido"}, {"is", "isl", "Icelandic"},
    {"it", "ita", "Italian"}, {"iu", "iku", "Inuktitut"}, {"ja", "jpn", "Japanese"}, {"jv", "jav", "Javanese"}, {"ka", "kat", "Georgian"},
    {"kg", "kon", "Kongo"}, {"ki", "kik", "Kikuyu"}, {"kj", "kua", "Kuanyama"}, {"kk", "kaz", "Kazakh"}, {"kl", "kal", "Kalaallisut"},
    {"km", "khm", "Khmer"}, {"kn", "kan", "Kannada"}, {"ko", "kor", "Korean"}, {"kr", "kau", "Kanuri"}, {"ks", "kas", "Kashmiri"},
    {"ku", "kur", "Kurdish"}, {"kv", "kom", "Komi"}, {"kw", "cor", "Cornish"}, {"ky", "kir", "Kyrgyz"}, {"la", "lat", "Latin"},
    {"lb", "ltz", "Luxembourgish"}, {"lg", "lug", "Ganda"}, {"li", "lim", "Limburgish"}, {"ln", "lin", "Lingala"}, {"lo", "lao", "Lao"},
    {"lt", "lit", "Lithuanian"}, {"lu", "lub", "Luba-Katanga"}, {"lv", "lav", "Latvian"}, {"mg", "mlg", "Malagasy"}, {"mh", "mah", "Marshallese"},
    {"mi", "mri", "Maori"}, {"mk", "mkd", "Macedonian"}, {"ml", "mal", "Malayalam"}, {"mn", "mon", "Mongolian"}, {"mr", "mar", "Marathi"},
    {"ms", "msa", "Malay"}, {"mt", "mlt", "Maltese"}, {"my", "mya", "Burmese"}, {"na", "nau", "Nauru"}, {"nb", "nob", "Norwegian Bokmål"},
    {"nd", "nde", "North Ndebele"}, {"ne", "nep", "Nepali"}, {"ng", "ndo", "Ndonga"}, {"nl", "nld", "Dutch"}, {"nn", "nno", "Norwegian Nynorsk"},
    {"no", "nor", "Norwegian"}, {"nr", "nbl", "South Ndebele"}, {"nv", "nav", "Navajo"}, {"ny", "nya", "Chichewa"}, {"oc", "oci", "Occitan"},
    {"oj", "oji", "Ojibwa"}, {"om", "orm", "Oromo"}, {"or", "ori", "Odia"}, {"os", "oss", "Ossetian"}, {"pa", "pan", "Punjabi"},
    {"pi", "pli", "Pali"}, {"pl", "pol", "Polish"}, {"ps", "pus", "Pashto"}, {"pt", "por", "Portuguese"}, {"qu", "que", "Quechua"},
    {"rm", "roh", "Romansh"}, {"rn", "run", "Rundi"}, {"ro", "ron", "Romanian"}, {"ru", "rus", "Russian"}, {"rw", "kin", "Kinyarwanda"},
    {"sa", "san", "Sanskrit"}, {"sc", "srd", "Sardinian"}, {"sd", "snd", "Sindhi"}, {"se", "sme", "Northern Sami"}, {"sg", "sag", "Sango"},
    {"si", "sin", "Sinhala"}, {"sk", "slk", "Slovak"}, {"sl", "slv", "Slovenian"}, {"sm", "smo", "Samoan"}, {"sn", "sna", "Shona"},
    {"so", "som", "Somali"}, {"sq", "sqi", "Albanian"}, {"sr", "srp", "Serbian"}, {"ss", "ssw", "Swati"}, {"st", "sot", "Southern Sotho"},
    {"su", "sun", "Sundanese"}, {"sv", "swe", "Swedish"}, {"sw", "swa", "Swahili"}, {"ta", "tam", "Tamil"}, {"te", "tel", "Telugu"},
    {"tg", "tgk", "Tajik"}, {"th", "tha", "Thai"}, {"ti", "tir", "Tigrinya"}, {"tk", "tuk", "Turkmen"}, {"tl", "tgl", "Tagalog"},
    {"tn", "tsn", "Tswana"}, {"to", "ton", "Tongan"}, {"tr", "tur", "Turkish"}, {"ts", "tso", "Tsonga"}, {"tt", "tat", "Tatar"},
    {"tw", "twi", "Twi"}, {"ty", "tah", "Tahitian"}, {"ug", "uig", "Uyghur"}, {"uk", "ukr", "Ukrainian"}, {"ur", "urd", "Urdu"},
    {"uz", "uzb", "Uzbek"}, {"ve", "ven", "Venda"}, {"vi", "vie", "Vietnamese"}, {"vo", "vol", "Volapük"}, {"wa", "wln", "Walloon"},
    {"wo", "wol", "Wolof"}, {"xh", "xho", "Xhosa"}, {"yi", "yid", "Yiddish"}, {"yo", "yor", "Yoruba"}, {"za", "zha", "Zhuang"},
    {"zh", "zho", "Chinese"}, {"zu", "zul", "Zulu"},
};

std::vector<CatalogItem> wikdict(const std::string &html) {
  static const text::Regex line("href=\"(wikdict-([a-z]{2,3})-([a-z]{2,3})\\.zip)\".*?\\s(\\d+(?:\\.\\d+)?)([KMG])\\s*$");
  std::vector<CatalogItem> out;
  size_t at = 0;
  while (at < html.size()) {
    auto e = html.find('\n', at);
    if (e == std::string::npos) e = html.size();
    text::Regex::Match m;
    if (line.find(std::string_view(html).substr(at, e - at), m)) {
      double n = std::stod(m.groups[4]), unit = m.groups[5] == "K" ? 1e3 : m.groups[5] == "M" ? 1e6 : 1e9;
      out.push_back({"wikdict", m.groups[2], m.groups[3], catalog_sources()[0].url + m.groups[1], static_cast<std::int64_t>(n * unit), "CC BY-SA 4.0"});
    }
    at = e + 1;
  }
  return out;
}

std::vector<CatalogItem> wiktionary(const std::string &body) {
  std::vector<CatalogItem> out;
  auto j = json::parse(body);
  for (auto &a : j.at("assets")) {
    auto name = a.at("name").get<std::string>();
    if (!name.ends_with(".tar.zst")) continue;
    auto pair = name.substr(0, name.size() - 8);
    auto dash = pair.find('-');
    out.push_back({"wiktionary", pair.substr(0, dash), dash == std::string::npos ? "?" : pair.substr(dash + 1), a.at("browser_download_url").get<std::string>(),
                   a.value("size", std::int64_t{0}), "CC BY-SA 4.0"});
  }
  return out;
}

std::vector<CatalogItem> freedict(const std::string &body) {
  std::vector<CatalogItem> out;
  for (auto &d : json::parse(body)) {
    if (!d.is_object() || !d.contains("releases") || !d["releases"].is_array()) continue;
    for (auto &r : d["releases"]) {
      if (!r.is_object() || r.value("platform", "") != "stardict") continue;
      auto name = d.value("name", "");
      auto dash = name.find('-');
      std::int64_t size = 0;
      try {
        size = std::stoll(r.value("size", "0"));
      } catch (const std::exception &) {
      }
      out.push_back({"freedict", two_letter(name.substr(0, dash)), two_letter(dash == std::string::npos ? "?" : name.substr(dash + 1)), r.at("URL").get<std::string>(),
                     size, "GPL / CC (see the dictionary)"});
      break;
    }
  }
  return out;
}

}  // namespace

const std::vector<CatalogSource> &catalog_sources() {
  static const std::vector<CatalogSource> s{
      {"wikdict", "WikDict", "https://download.wikdict.com/dictionaries/stardict/",
       "Short translations, part of speech and gender; forms lead to the base (ging → gehen)."},
      {"wiktionary", "Wiktionary (xxyzz)", "https://api.github.com/repos/xxyzz/wiktionary_stardict/releases/latest",
       "Full Wiktionary articles: pronunciation, all senses, examples."},
      {"freedict", "FreeDict", "https://freedict.org/freedict-database.json",
       "Older compact dictionaries without word forms; WikDict is usually better."}};
  return s;
}

std::vector<CatalogItem> parse_catalog(const std::string &source, const std::string &body) {
  if (source == "wikdict") return wikdict(body);
  if (source == "wiktionary") return wiktionary(body);
  if (source == "freedict") return freedict(body);
  return {};
}

std::string two_letter(const std::string &code) {
  for (auto &l : LANGS)
    if (code == l.three) return l.two;
  return code;
}

std::string catalog_language(const std::string &code) {
  for (auto &l : LANGS)
    if (code == l.two) return l.name;
  return code;
}

CatalogList fetch_catalog(const DictSettings &s) {
  CatalogList out;
  for (auto &src : catalog_sources()) {
    if (std::find(s.sources_off.begin(), s.sources_off.end(), src.id) != s.sources_off.end()) continue;
    std::string body;
    auto err = http::get(src.url, body);
    try {
      if (err.empty()) {
        auto items = parse_catalog(src.id, body);
        out.items.insert(out.items.end(), items.begin(), items.end());
      }
    } catch (const std::exception &e) {
      err = e.what();
    }
    if (!err.empty()) out.failed.push_back(src.title + ": " + err);
  }
  return out;
}

Imported download_dict(const fs::path &data, const CatalogItem &item, const Progress &progress, const Cancel &cancel) {
  auto name = item.url.substr(item.url.find_last_of('/') + 1);
  if (auto q = name.find('?'); q != std::string::npos) name.resize(q);
  auto file = data / "tmp" / ("catalog-" + name);
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  fs::remove(file, ec);
  Imported r;
  auto s = http::download(item.url, file, item.size, progress, cancel);
  if (s.cancelled) r.error = "Cancelled";
  else if (!s.error.empty()) r.error = s.error;
  else r = import_dicts({file}, own_dir(data));
  fs::remove(file, ec);
  if (!r.error.empty()) return r;
  // The address with the dictionaries it brought: the catalog marks it while one of them is on disk.
  auto set = load_settings(data);
  std::erase_if(set.dicts.got, [&](const std::string &g) { return g.substr(0, g.find('\t')) == item.url; });
  std::string line = item.url;
  for (auto &n : r.added) line += "\t" + n;
  for (auto &n : r.already) line += "\t" + n;
  set.dicts.got.push_back(line);
  if (auto e = save_settings(data, set); !e.empty()) r.error = e;
  return r;
}

}  // namespace lexipeek
