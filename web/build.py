#!/usr/bin/env python3
"""Gzip the web page for the ESP32: web/index.html -> firmware/data/www/index.html.gz.

The firmware serves /www/index.html.gz from LittleFS at "/". Run this before
`pio run -t uploadfs`. Plain Python 3, no packages (PlatformIO already has Python).
"""
import gzip
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent
SRC = ROOT / "index.html"
OUT = ROOT.parent / "firmware" / "data" / "www" / "index.html.gz"
BUDGET = 20 * 1024  # keep the page small: it shares the radio with Bluetooth audio


def squeeze(html: str) -> str:
    # Drop leading indentation and blank lines. Safe for this page: no <pre> blocks
    # and no multi-line string literals in the script.
    return "\n".join(line.strip() for line in html.splitlines() if line.strip())


def main() -> int:
    html = squeeze(SRC.read_text(encoding="utf-8"))
    data = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(data)
    print(f"Web page: {len(data)} bytes gzipped ({len(data) * 100 // BUDGET}% of the {BUDGET // 1024} KB budget) -> {OUT.relative_to(ROOT.parent)}")
    if len(data) > BUDGET:
        print("Web page is over budget", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
