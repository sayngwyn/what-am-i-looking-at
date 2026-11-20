"""Make the two release zips from the built mod folders in dist/ (run build.bat skyrim and build.bat fo4 first).

    python tools/package_release.py

Reads the version from CMakeLists.txt and writes dist/release/WhatAmILookingAt-<version>-Skyrim.zip and
WhatAmILookingAt-<version>-FO4.zip. Each zip holds the mod folder (SKSE or F4SE), LICENSE, THIRD-PARTY-NOTICES.txt and SOURCE.txt;
the font's OFL.txt is already beside the font in the mod folder. The zips are deterministic (sorted names, fixed times) and are
scanned before they are kept: a drive-letter path, a user-profile path or an email address in any file refuses the package.
"""

from __future__ import annotations

import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
OUT = DIST / "release"
STAMP = (2026, 11, 20, 0, 0, 0)
REPO_URL = "https://github.com/sayngwyn/what-am-i-looking-at"
GAMES = (("Skyrim", "WhatAmILookingAt", "SKSE"), ("FO4", "WhatAmILookingAt-FO4", "F4SE"))
REFUSE = re.compile(rb"(?<![A-Za-z/])[A-Za-z]:[\\/][A-Za-z0-9_ .\\/-]{2,}|[Uu]sers[\\/]|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
SOURCE = """What Am I Looking At {version} ({game})

Free software under the GNU General Public License, version 3 or later (see LICENSE).
The source code for this version is at:

{url}/tree/v{version}

Third-party libraries and their licenses are listed in THIRD-PARTY-NOTICES.txt.
"""


def version() -> str:
    match = re.search(r"^project\(WhatAmILookingAt VERSION (\d+\.\d+\.\d+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"), re.M)
    if not match:
        sys.exit("version not found in CMakeLists.txt")
    return match.group(1)


def main() -> int:
    ver = version()
    OUT.mkdir(parents=True, exist_ok=True)
    for game, folder, loader in GAMES:
        base = DIST / folder
        if not (base / loader / "Plugins" / "WhatAmILookingAt.dll").is_file():
            sys.exit(f"{base} has no built DLL: run build.bat first")
        entries: dict[str, bytes] = {}
        for f in sorted(base.rglob("*")):
            if f.is_file():
                entries[f.relative_to(base).as_posix()] = f.read_bytes()
        entries["LICENSE"] = (ROOT / "LICENSE").read_bytes()
        entries["THIRD-PARTY-NOTICES.txt"] = (ROOT / "THIRD-PARTY-NOTICES.txt").read_bytes()
        entries["SOURCE.txt"] = SOURCE.format(version=ver, game=game, url=REPO_URL).replace("\n", "\r\n").encode("utf-8")
        if f"{loader}/Plugins/WhatAmILookingAt/fonts/OFL.txt" not in entries:
            sys.exit("OFL.txt is missing beside the font")
        bad = {name: m.group()[:60] for name, data in entries.items() if not name.endswith(".ttf") for m in [REFUSE.search(data)] if m}
        if bad:
            sys.exit(f"{game}: personal-looking text in {bad}")
        target = OUT / f"WhatAmILookingAt-{ver}-{game}.zip"
        target.unlink(missing_ok=True)
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
            for name in sorted(entries):
                info = zipfile.ZipInfo(name, STAMP)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                archive.writestr(info, entries[name])
        print(f"{target} ({target.stat().st_size} bytes, {len(entries)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
