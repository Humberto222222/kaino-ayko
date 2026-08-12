#!/usr/bin/env python3
"""Regenerates src/embedded_assets.h from the public/ directory."""
import os
import json

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "public")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src", "embedded_assets.h")

def walk(root):
    for dirpath, dirnames, filenames in os.walk(root):
        for fname in sorted(filenames):
            full = os.path.join(dirpath, fname)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            yield rel, full

assets = {}
for rel, full in walk(ROOT):
    with open(full, "r", encoding="utf-8") as f:
        assets[rel] = f.read()

lines = []
lines.append("// GENERATED FILE - do not edit manually")
lines.append("// Keeps raw UTF-8 (emojis included) and only escapes quotes/backslashes/line-breaks,")
lines.append("// which is valid as C++ string literal content.")
lines.append("#include <map>")
lines.append("#include <string>")
lines.append("")
lines.append("static const std::map<std::string, std::string> embedded_assets = {")
for rel, content in sorted(assets.items()):
    # ensure_ascii=False keeps emojis as raw UTF-8 (C++ accepts them);
    # quotes, backslashes, newlines are still escaped -> valid C++ literal.
    escaped = json.dumps(content, ensure_ascii=False)
    lines.append('    {' + json.dumps(rel) + ', ' + escaped + '},')
lines.append("};")
lines.append("")

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print("Generated", OUT, "with", len(assets), "assets")