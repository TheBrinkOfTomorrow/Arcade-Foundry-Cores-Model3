# Release notes

Newest first. Each release is also on the repository's Releases page.

## 1.0.0 (2026-11-02)

**Arcade Foundry Cores: Model 3 1.0.0**

First release of the Sega Model 3 cores for Arcade Foundry: Insert Coin, as shipped with Insert Coin 1.0.0.

- **Games:** 24 sets, from Virtua Fighter 3 and Scud Race to Daytona USA 2, Sega Rally 2, Spikeout, The Lost World, Sega Bass Fishing and Virtua Striker 2 '99. The full list is in the app's [GAMES.md](https://github.com/TheBrinkOfTomorrow/Arcade-Foundry-Insert-Coin/blob/main/GAMES.md).
- **How it works:** a fork of Supermodel, the Sega Model 3 emulator by the Supermodel Team, built as one game per library behind the same C interface as the other cores. All the emulation comes from Supermodel; our changes are in `UPSTREAM.md`.
- **Platforms:** macOS, iOS, iPadOS and tvOS (Metal), and Linux (OpenGL).
- **Building:** `cmake -B build -DM3CORE_ROMSET=vf3 && cmake --build build`.

No ROMs are included. You supply your own. **Licence: GPL-3.0-or-later**, as Supermodel is (see `Docs/LICENSE.txt`).

The release number is a snapshot label for "as shipped with Insert Coin 1.0.0"; each game core has its own version.
