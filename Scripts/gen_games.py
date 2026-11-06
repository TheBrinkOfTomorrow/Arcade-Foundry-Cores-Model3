#!/usr/bin/env python3
"""Write the files that list this repo's games from games.toml.

  gen_games.py          rewrite them
  gen_games.py --check  only compare (CI): exit 1 when one is out of date

Writes the M3_TITLES_<set> lines in CMakeLists.txt (each core's
afcore_api.title) and the version lines in Core/src/core_versions.h. A new
game: a [[game]] in games.toml, then this.
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "common", "tools", "games"))
import afgames  # noqa: E402


def generate(out):
    games = afgames.load_core(os.path.join(ROOT, "games.toml"))
    if any(g["board"] != "model3" for g in games):
        raise afgames.ManifestError("only Model 3 games belong in this repo")
    out.region("CMakeLists.txt", "titles",
               [f"set(M3_TITLES_{g['set']} {afgames.c_string(g['build'].get('title', g['title']))})" for g in games], "#")
    out.region("Core/src/core_versions.h", "versions",
               [f"#define M3CORE_VERSION_{g['set']} AFCORE_MAKE_VERSION({', '.join(map(str, afgames.version_tuple(g)))})"
                for g in games])


if __name__ == "__main__":
    afgames.main(generate, ROOT, __doc__)
