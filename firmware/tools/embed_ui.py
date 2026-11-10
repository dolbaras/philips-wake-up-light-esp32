#!/usr/bin/env python3
"""Сжать веб-панель ui/index.html (gzip) и встроить в прошивку как src/net/ui_gz.h (kUiGz).

Запускается сама перед сборкой (extra_scripts в platformio.ini) или вручную после правки панели.
"""
import gzip, os, sys

root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
src = os.path.join(root, "ui", "index.html")
dst = os.path.join(root, "src", "net", "ui_gz.h")

data = gzip.compress(open(src, "rb").read(), compresslevel=9, mtime=0)
lines = [", ".join(f"0x{b:02x}" for b in data[i:i + 20]) for i in range(0, len(data), 20)]
text = (
    "// СГЕНЕРИРОВАНО tools/embed_ui.py из ui/index.html — не править руками.\n"
    "#pragma once\n#include <cstdint>\n#include <cstddef>\n"
    "static const uint8_t kUiGz[] = {\n  " + ",\n  ".join(lines) + "\n};\n"
    f"static const size_t kUiGzLen = {len(data)};\n"
)
old = open(dst).read() if os.path.exists(dst) else ""
if old != text:
    open(dst, "w").write(text)
print(f"ui_gz.h: {len(data)} байт gzip из {os.path.getsize(src)} байт html", file=sys.stderr)
