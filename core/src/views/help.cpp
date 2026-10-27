// Help: InkPeek's topics where they say the same on a desktop, LexiPeek's own where they do not.
#include "lexipeek/l10n.hpp"
#include "lexipeek/views.hpp"

namespace lexipeek {

std::vector<HelpTopic> help_view(const std::string &lookup, const std::string &input) {
  auto item = [](const char *title, const std::string &text) { return HelpItem{title[0] ? tr(title) : "", text}; };
  auto go = [](HelpTopic t, SettingsSection s, int tab, const std::string &name) {
    t.go = tr("help_open", {name}), t.section = s, t.tab = tab;
    return t;
  };
  auto sections = settings_sections();
  auto tabs = [](SettingsSection s, int i) { return settings_tabs(s).at(static_cast<size_t>(i)); };
  std::vector<HelpTopic> out;
  out.push_back({tr("hs_start_t"),
                 {item("hs_access_t", tr("hs_access")), item("hs_keys_t", tr("hs_keys", {lookup, input})),
                  item("hs_icon_t", tr("hs_icon")), item("hs_other_t", tr("hs_other"))}});
  out.push_back(go({tr("hc_card_t"),
                    {item("hc_kinds_t", tr("hc_kinds")), item("hc_nav_t", tr("hc_nav")), item("hc_bar_t", tr("hc_bar")),
                     item("hc_keys_t", tr("hc_keys"))}},
                   SettingsSection::card, 0, sections[1]));
  out.push_back(go({tr("packs_title"),
                    {item("hp_get_t", tr("hp_get_d")), item("help_dict_t", tr("help_dict")), item("help_gram_t", tr("help_gram")),
                     item("help_build_t", tr("help_build")), item("", tr("help_labels"))}},
                   SettingsSection::languages, 0, tabs(SettingsSection::languages, 0)));
  out.push_back(go({tr("mt_t"),
                    {item("mt_fast_t", tr("hm_fast")), item("mt_direct_t", tr("hm_opus")), item("mt_llm_t", tr("hm_llm")),
                     item("mt_off_t", tr("hm_off")), item("mt_english_t", tr("mt_english_about")), item("help_lid_t", tr("help_lid"))}},
                   SettingsSection::languages, 1, tabs(SettingsSection::languages, 1)));
  out.push_back(go({tr("own_title"),
                    {item("help_other_t", tr("help_other")), item("help_formats_t", tr("help_formats")),
                     item("help_folder_t", tr("ho_folder"))}},
                   SettingsSection::languages, 2, tabs(SettingsSection::languages, 2)));
  out.push_back(go({tr("tab_ai"),
                    {item("aih_what_t", tr("aih_what")), item("prompt_quick_t", tr("aih_translate")), item("a_analysis", tr("aih_analysis")),
                     item("a_context", tr("aih_context")), item("ai_modules", tr("ha_when")), item("aih_keys_t", tr("ha_keys")),
                     item("aih_local_t", tr("aih_local")), item("aih_free_t", tr("aih_free"))}},
                   SettingsSection::ai, 0, sections[2]));
  out.push_back({tr("aih_send_t"), {item("hn_ai_t", tr("hn_ai_d")), item("hn_dl_t", tr("hn_dl"))}});
  out.push_back({tr("ht_notes"),
                 {item("hw_save_t", tr("hw_save")), item("hw_hist_t", tr("hw_hist")), item("hw_del_t", tr("hw_del")),
                  item("hw_anki_t", tr("hw_anki_d")), item("hw_backup_t", tr("hw_backup_d")), item("hw_daily_t", tr("hw_daily_d"))}});
  return out;
}

}  // namespace lexipeek
