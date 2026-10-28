# The snapshot cases, shared by the Mac app's (snapshots.sh) and the Qt shell's (qt-snapshots.sh): sourced, not run.
# shellcheck shell=sh
LONG="Am nächsten Morgen stand Anna früh auf, weil sie den ersten Zug in die Stadt nicht verpassen wollte. \
Draußen war es noch dunkel, und auf den Straßen lag eine dünne Schicht Schnee. Sie trank schnell einen Kaffee, \
zog ihren warmen Mantel an und ging zum Bahnhof. Der Zug war fast leer, nur ein alter Mann las seine Zeitung. \
Anna schaute aus dem Fenster und dachte an das Gespräch, das sie am Nachmittag führen musste. Ihr Chef hatte \
ihr gesagt, dass die Firma eine neue Abteilung eröffnen wolle und dass sie dafür die richtige Person sei. \
Sie war stolz, aber auch ein wenig nervös, denn sie hatte noch nie ein Team geleitet. In der Stadt angekommen, \
ging sie zuerst in eine kleine Bäckerei, kaufte ein Brötchen und setzte sich an einen Tisch am Fenster. \
Dort schrieb sie ihre Gedanken in ein Heft: was sie fragen wollte, was sie sich wünschte und was sie fürchtete. \
Als die Uhr zwölf schlug, wusste sie, dass sie bereit war. Das Gespräch dauerte länger als erwartet, aber am \
Ende gab ihr Chef ihr die Hand und sagte, er freue sich auf die Zusammenarbeit. Auf dem Heimweg schien die Sonne, \
der Schnee schmolz, und Anna lachte zum ersten Mal seit Wochen ohne Grund. Zu Hause rief sie ihre Schwester an \
und erzählte ihr alles, vom dunklen Morgen bis zum hellen Nachmittag."

# name | url (snap= names the shot; tab=dictionary opens that tab; full=1 the full entry; save=1 ☆ once it is final;
# open=<word> clicks that word in the card; section=<key> opens the first entry's section;
# window?page=…: the LexiPeek window, notes after the cards that fill them; look= looks up there)
cases() {
  cat <<EOF
word-hausern|lookup?text=Häusern
word-haus-full|lookup?text=Haus&full=1
word-entry-one|lookup?text=Haus&entry=1
section-page-2|lookup?text=machen&full=1&section=rel&page=1
gloss-back|lookup?text=Ich mache die Tür zu&tab=dictionary&open=Tür&back=1
word-answer-en|lookup?text=Häusern&answer=en
word-machen-sections|lookup?text=machen&full=1
section-related|lookup?text=machen&full=1&section=rel
section-forms|lookup?text=machen&full=1&section=forms
gloss-open|lookup?text=Ich mache die Tür zu&tab=dictionary&open=Tür
gloss-open-full|lookup?text=Am nächsten Morgen stand Anna früh auf&tab=dictionary&open=aufstehen&full=1
word-ru|lookup?text=дом
phrase-translate|lookup?text=Ich mache die Tür zu
phrase-dictionary|lookup?text=Ich mache die Tür zu&tab=dictionary
passage-translate|lookup?text=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.
passage-dictionary|lookup?text=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.&tab=dictionary
long-translate|lookup?text=$LONG
long-dictionary|lookup?text=$LONG&tab=dictionary
long-whole|lookup?text=$LONG&whole=1
passage-english|lookup?text=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.&method=fast
passage-whole|lookup?text=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.&whole=1
passage-ru|lookup?text=Вчера мы долго гуляли по парку. Погода была тёплой, и мы не хотели возвращаться домой.
french|lookup?text=Le chat dort sur le canapé depuis ce matin.
unknown-symbols|lookup?text=+++ 42 ### 17 *** 9
unknown-words|lookup?text=Blorvik tanquo meseral fintu dravok.
input|input
selection-failed|unread
word-saved|lookup?text=Häusern&save=1
word-baume-saved|lookup?text=Bäume&tab=dictionary&save=1
article-saved|lookup?text=das Haus
phrase-saved|lookup?text=Ich mache die Tür zu&save=1
passage-saved|lookup?text=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.&save=1
notes-words|window?page=words
notes-phrases|window?page=phrases
notes-history|window?page=history
window-help|window?page=help
window-empty|window?page=lookup
window-lookup|window?page=lookup&look=Häusern
window-passage|window?page=lookup&look=Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.
EOF
}

# The first-run window on an empty data dir: name | step | a file of the data dir copied in before it.
firstrun_cases() {
  cat <<EOF
firstrun-access|access|
firstrun-list|list|
firstrun-list-error|list-error|
firstrun-pair|pair|packs.json
firstrun-download|download|
firstrun-error|error|
firstrun-done|done|installed.json
firstrun-done-later|done-later|
EOF
}

# AI on (Claude; no key, no network: LEXIPEEK_AI_REPLIES answers from files), a third instance with its own data dir:
# name | url | the answers (word, phrase) or "error" (the provider says 401). method=ai: AI translation picked in the card;
# tab=analysis|context: that AI tab clicked.
ai_cases() {
  cat <<EOF
ai-word-translate|lookup?text=Häusern&method=ai&tab=translate|word
ai-word-analysis|lookup?text=Häusern&tab=analysis|word
ai-word-context|lookup?text=Häusern&tab=context|word
ai-phrase-translate|lookup?text=Ich mache die Tür zu&method=ai|phrase
ai-phrase-analysis|lookup?text=Ich mache die Tür zu&tab=analysis|phrase
ai-phrase-context|lookup?text=Ich mache die Tür zu&tab=context|phrase
ai-error|lookup?text=Bäume&tab=analysis|error
EOF
}

# The fake provider's answers (as a model would give them), per kind of text.
ai_fixtures() {
  mkdir -p "$1/word" "$1/phrase"
  cat >"$1/word/quick.json" <<'EOF'
{"base":"Haus","article":"das","plural":"die Häuser","pos":"сущ., ср. р.","forms":"","irregular":false,"form_of":"дательный падеж, мн. ч.","meanings":["дом","здание","семья, род"],"translation":""}
EOF
  cat >"$1/word/analysis.json" <<'EOF'
{"grammar":"**Häusern** — дательный падеж множественного числа от **das Haus**.\nВо множественном числе: **die Häuser**, в дательном добавляется **-n**: *in den Häusern*.","words":["**das Haus** — дом","**das Gebäude** — здание","**das Heim** — домашний очаг"]}
EOF
  cat >"$1/word/context.json" <<'EOF'
{"explanation":"Форма встречается после предлогов с дательным падежом: *in*, *aus*, *von*. Нейтральное слово, любой стиль.","examples":[{"text":"In den alten **Häusern** wohnen viele Familien.","translation":"В старых домах живёт много семей."},{"text":"Aus den **Häusern** kam Musik.","translation":"Из домов доносилась музыка."},{"text":"Zwischen den **Häusern** ist ein kleiner Park.","translation":"Между домами есть маленький парк."}]}
EOF
  cat >"$1/phrase/quick.json" <<'EOF'
{"base":"","article":"","plural":"","pos":"","forms":"","irregular":false,"form_of":"","meanings":[],"translation":"Я закрываю дверь."}
EOF
  cat >"$1/phrase/analysis.json" <<'EOF'
{"grammar":"**mache … zu** — отделяемый глагол **zumachen**: приставка **zu** уходит в конец предложения.\n**die Tür** — винительный падеж (прямое дополнение), артикль *die* не меняется.","words":["**zumachen** — закрывать","**die Tür** — дверь"]}
EOF
  cat >"$1/phrase/context.json" <<'EOF'
{"explanation":"Обычная разговорная фраза; **zumachen** проще и разговорнее, чем *schließen*.","examples":[{"text":"Kannst du bitte das Fenster **zumachen**?","translation":"Можешь, пожалуйста, закрыть окно?"},{"text":"Ich **mache** den Laden um sechs **zu**.","translation":"Я закрываю магазин в шесть."},{"text":"**Mach** die Augen **zu**!","translation":"Закрой глаза!"}]}
EOF
}

# The settings window's shots, written by the app at launch: every section × tab (settings-<section>[-<tab>]); AI's
# and Card → Opening also with AI on (-on, from the AI instance).
settings_shots() {
  echo settings-languages-packs settings-languages-translation settings-languages-translation-empty \
    settings-languages-other-dictionaries settings-card-opening settings-card-opening-on settings-card-article \
    settings-card-appearance settings-ai-connection settings-ai-connection-on settings-ai-modules settings-ai-modules-on \
    settings-general-keys settings-general-system about
}

