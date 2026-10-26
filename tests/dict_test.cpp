// dict and text helpers without packs (the packs: inkpeek_cases_test.cpp).
#include <doctest/doctest.h>
#include "dict/library.hpp"
#include "text/text.hpp"

using namespace lexipeek;

TEST_CASE("lookup key and query cleaning as InkPeek's") {
  CHECK(text::norm_key(" Hau­ser ") == "hauser");
  CHECK(text::norm_key("STRASSE") == "strasse");
  CHECK(text::norm_key("Äpfel") == "äpfel");
  CHECK(text::clean_query("«Haus»,") == "Haus");
  CHECK(text::clean_query("  das   Haus ") == "das Haus");
  CHECK(text::clean_phrase("— Wo ist er?»") == "Wo ist er?");
  CHECK(text::clean_phrase("«¿Dónde está?»") == "¿Dónde está?");
}

TEST_CASE("regex: groups, replace, Unicode letters") {
  text::Regex r("(\\p{L}+)-(\\d)");
  CHECK(r.replace("Haus-1 und Baum-2", "$2:$1") == "1:Haus und 2:Baum");
  CHECK(text::Regex("^\\p{L}+$").matches("Straße"));
  CHECK(!text::Regex("^\\p{L}+$").matches("Haus1"));
  CHECK(text::strip_tags("<b>das</b> Haus &amp; Hof") == "das Haus & Hof");
}

TEST_CASE("head line of a pack article") {
  auto h = dict::head_line("<p class=\"head\"><b>das Haus</b> (Hauses) — дом</p><ol><li>…</li></ol>");
  REQUIRE(h);
  dict::Head head{"Haus", *h, "de"};
  CHECK(head.header() == "das Haus (Hauses)");
  CHECK(head.meaning() == "дом");
  CHECK(!dict::head_line("<p>no head</p>"));
}

TEST_CASE("separable verbs written apart") {
  auto bases = [](const std::string &f) -> std::vector<std::string> {
    if (f == "mache") return {"machen"};
    if (f == "fängt") return {"fangen"};
    return {};
  };
  auto particle = [](const std::string &v) -> std::string {
    if (v == "zumachen") return "zu";
    if (v == "anfangen") return "an";
    return "";
  };
  auto f = dict::find_separable("Ich mache die Tür zu, und er fängt morgen an.", bases, particle);
  REQUIRE(f.size() == 2);
  CHECK(f[0].verb == "zumachen");
  CHECK(f[0].form == "mache");
  CHECK(f[1].verb == "anfangen");
  CHECK(dict::find_separable("Ich gehe zu Hause.", bases, particle).empty());
}
