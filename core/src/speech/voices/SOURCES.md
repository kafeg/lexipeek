# The app's voices: sources

Each voice is a clip of one native speaker (5–12 s) and what it says, kept here only as its speaker embedding (`.spk`)
and codec codes (`.rvq`) made by `scripts/gen-voice.sh`; the recording itself is not in the repository. Chosen per language and
gender out of 2–6 candidates by reading 20 single words with each and transcribing them (whisper large-v3-turbo): the word,
and nothing after it.

| voice | speaker | source | clip | licence |
|---|---|---|---|---|
| de-female | ramona_deininger | Caspar Hauser oder Die Trägheit des Herzens — Jakob Wassermann | `caspar_hauser_09_f000023.wav`, 7.23 s | M-AILABS (below); LibriVox recording, public domain |
| de-male | karlsson | Unterm Birnbaum — Theodor Fontane | `untermbirnbaum_05_f000100.wav`, 11.18 s | M-AILABS (below); LibriVox recording, public domain |
| en-female | judy_bieber | Rinkitink in Oz — L. Frank Baum | `rinkitink_baum_18_64kb_f000162.wav`, 7.59 s | M-AILABS (below); LibriVox recording, public domain |
| en-male | elliot_miller | The Pink Fairy Book — Andrew Lang (ed.) | `pink_fairy_book_36_f000004.wav`, 8.22 s | M-AILABS (below); LibriVox recording, public domain |
| es-female | karen_savage | Angelina — Rafael Delgado | `angelina_53_delgado_f000043.wav`, 8.52 s | M-AILABS (below); LibriVox recording, public domain |
| es-male | tux | La batalla de los Arapiles — Benito Pérez Galdós | `arapiles_40_perezgaldos_f000077.wav`, 6.05 s | M-AILABS (below); LibriVox recording, public domain |
| fr-female | nadine_eckert_boulet | Madame Bovary — Gustave Flaubert | `madame_bovary_3_05_f000082.wav`, 8.34 s | M-AILABS (below); LibriVox recording, public domain |
| fr-male | gilles_g_le_blanc | Arsène Lupin contre Herlock Sholmès — Maurice Leblanc | `lupin_contre_holmes_07_f000060.wav`, 9.12 s | M-AILABS (below); LibriVox recording, public domain |
| it-female | lisa_caputo | I Malavoglia — Giovanni Verga | `imalavoglia_06_verga_f000101.wav`, 6.21 s | M-AILABS (below); LibriVox recording, public domain |
| it-male | riccardo_fasol | Le meraviglie del Duemila — Emilio Salgari | `meraviglieduemila_15_salgari_f000085.wav`, 7.72 s | M-AILABS (below); LibriVox recording, public domain |
| pl-female | nina_brown | Rękopis znaleziony w Saragossie (Polish) — Jan Potocki | `saragossie_03_f000011.wav`, 8.68 s | M-AILABS (below); LibriVox recording, public domain |
| pl-male | piotr_nater | Lalka — Bolesław Prus | `lalka_2_16_f000163.wav`, 7.61 s | M-AILABS (below); LibriVox recording, public domain |
| pt-female | a Common Voice contributor | Mozilla Common Voice 16.0 (pt) | `common_voice_pt_30543800.mp3`, 5.97 s | CC0 1.0 |
| pt-male | a Common Voice contributor | Mozilla Common Voice 16.0 (pt) | `common_voice_pt_36461251.mp3`, 6.90 s | CC0 1.0 |
| ru-female | hajdurova | Фальшивый купон — Лев Толстой | `falshiviy_kupon_s000166.wav`, 9.01 s | M-AILABS (below); LibriVox recording, public domain |
| ru-male | minaev | Обломов — Иван Гончаров | `oblomov_s000291.wav`, 9.12 s | M-AILABS (below); recording from bibe.ru |

- M-AILABS Speech Dataset: https://github.com/i-celeste-aurora/m-ailabs-dataset (by_book, the speaker's folder). ru-male's
  recording (Aleksey Minaev, Oblomov) came to M-AILABS from bibe.ru, not LibriVox: covered by the M-AILABS licence.
- Mozilla Common Voice: https://commonvoice.mozilla.org, CC0 1.0 (https://creativecommons.org/publicdomain/zero/1.0/).

## M-AILABS licence

Copyright (c) 2017-2019 by the original creators @ M-AILABS

Copyright (c) 2020 I. Celeste Aurora Solak (github.com/i-celeste-aurora)


Redistribution and use in any form, including any commercial use, with or without modification are permitted provided that the following conditions are met:

- Redistributions of source data must retain the above copyright notice, this list of conditions and the following disclaimer.
- Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this downloaded data, source-code or binary-code without specific prior written permission.

THIS DATA IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE and/or DATA, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

As of October 2018, this data is provided courtesy of I. Celeste Aurora. Any license restrictions you may find in downloaded data is removed herewith, only valid license (which is even more free than before) is shown above.
