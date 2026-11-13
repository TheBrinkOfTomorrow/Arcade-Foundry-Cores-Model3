# Release notes

Newest first. Each release is also on the repository's Releases page.

## 1.3.0 (2026-11-12)

**Arcade Foundry Cores: Model 3 1.3.0**

The Model 3 cores as shipped with Insert Coin 1.3.0.

- Save states: every game can save the whole board and load it back
  (afcore 5.4), and a loaded game carries on exactly as it would have:
  same pictures, same sound. Supermodel's own save state left out a few
  things (the first frame's vblank in a fresh core, the music decoder's
  state, some CPU timing), now saved too. Core version up a minor version
  for every game.

- Link play: the games with a network board (Daytona USA 2, Scud Race, Sega
  Rally 2 and the other net board games) can link cabinets over the network,
  with the same protocol as Supermodel, so a core can also link with
  Supermodel. A cabinet that stops answering for 3 seconds breaks the link
  instead of freezing the others. Core version 1.1.0 for those games.
- Dirt Devils links 3 or 4 cabinets: with more than two, the other cabinets'
  data was in the opposite order to what the game expects, so the slaves
  stayed on the grid. Core version 1.1.1.
- Le Mans 24 starts in the Export country on a new save: English attract
  and no Japan-only warning. The game's hidden country screen (test menu:
  Start, Start, Service, Service, Start, Test) still changes it. Core
  version 1.1.1.
- **Game categories:** the app's library groups games by category, taken
  from each game's controls; games.toml sets one by hand where the controls
  give the wrong answer.

## 1.2.0 (2026-11-06)

**Arcade Foundry Cores: Model 3 1.2.0**

The Model 3 cores as shipped with Insert Coin 1.2.0, which carries the 1.1.0
and 1.2.0 work together (there was no separate 1.1.0 release). The games are
the same 24 sets as in 1.0.0.

- Apple devices: the screen goes to the app as a Metal texture (afcore's
  `screen_surface`) instead of being read back to the CPU and converted every
  frame.
- ROM check: `check_roms` checks a ROM set without starting the game, so the
  app's Downloads can say whether a set is complete and right.
- Debug switches: the developer environment variables are now
  `AF_DEBUG_` plus their old name (`AF_DEBUG_PPC_SKIP`); the old
  names still work in this release. The list is in Arcade-Foundry-Cores-Common's
  docs/DEBUG-SWITCHES.md.
- Build: the core interface header now comes from the shared Arcade-Foundry-Cores-Common repo (the `common/` submodule). Clone with `--recursive`, or run `git submodule update --init`.

## 1.0.0 (2026-11-02)

**Arcade Foundry Cores: Model 3 1.0.0**

First release of the Sega Model 3 cores for Arcade Foundry: Insert Coin, as shipped with Insert Coin 1.0.0.

- **Games:** 24 sets, from Virtua Fighter 3 and Scud Race to Daytona USA 2, Sega Rally 2, Spikeout, The Lost World, Sega Bass Fishing and Virtua Striker 2 '99. The full list is in the app's [GAMES.md](https://github.com/TheBrinkOfTomorrow/Arcade-Foundry-Insert-Coin/blob/main/GAMES.md).
- **How it works:** a fork of Supermodel, the Sega Model 3 emulator by the Supermodel Team, built as one game per library behind the same C interface as the other cores. All the emulation comes from Supermodel; our changes are in `UPSTREAM.md`.
- **Platforms:** macOS, iOS, iPadOS and tvOS (Metal), and Linux (OpenGL).
- **Building:** `cmake -B build -DM3CORE_ROMSET=vf3 && cmake --build build`.

No ROMs are included. You supply your own. **Licence: GPL-3.0-or-later**, as Supermodel is (see `Docs/LICENSE.txt`).

The release number is a snapshot label for "as shipped with Insert Coin 1.0.0"; each game core has its own version.
