#!/usr/bin/env python3
"""The Service's menu title in every UI language: mac/Resources/<lang>.lproj/ServicesMenu.strings from core/src/l10n's
lookup_service (run after changing it)."""
import os
import re

root = os.path.join(os.path.dirname(__file__), "..")
for inc in sorted(os.listdir(os.path.join(root, "core/src/l10n"))):
    if not inc.endswith(".inc"):
        continue
    lang = inc[:-4]
    text = open(os.path.join(root, "core/src/l10n", inc), encoding="utf-8").read()
    title = re.search(r'\{"lookup_service", "(.*?)"\}', text).group(1)
    out = os.path.join(root, "mac/Resources", lang + ".lproj")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "ServicesMenu.strings"), "w", encoding="utf-8") as f:
        f.write('"Look up in LexiPeek" = "%s";\n' % title)
