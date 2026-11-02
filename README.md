# Arcade Foundry Cores: Sega Model 3

Sega Model 3 arcade games as libraries for Arcade Foundry: Insert Coin, one
game per build, behind the same C interface (`Core/include/afcore.h`) as the
Model 1 and Model 2 cores. The cores run on macOS, iOS, iPadOS and tvOS
(Metal) and on Linux (OpenGL). No ROMs are included; you supply your own.

Building a core: `cmake -B build -DM3CORE_ROMSET=vf3 && cmake --build build`.

## Credits

This repository is a fork of **Supermodel**, the Sega Model 3 emulator by
The Supermodel Team (https://github.com/trzy/Supermodel), Copyright 2003-2026
The Supermodel Team. All of the emulation comes from Supermodel. Our changes
and how we track upstream are listed in `UPSTREAM.md`.

## License

GPL-3.0-or-later, as Supermodel is. See `Docs/LICENSE.txt`.

## Supermodel's original build instructions

These build the stock Supermodel emulator, which we use as the reference the
cores are checked against (`Core/tools/refrun.sh`). The Windows build files
have been removed from this fork.

### Linux

Ensure SDL2 is installed. Most package managers ought to have this available. For example, on Ubuntu, it should be sufficient to run:

```
sudo apt install libsdl2-dev
sudo apt install libsdl2-net-dev
sudo apt install libglu1-mesa-dev
```

And then build Supermodel:

```
make -f Makefiles/Makefile.UNIX
```

### macOS

Ensure Apple's Xcode Command Line Tools are installed:

From a terminal:
```
xcode-select --install
```

Ensure SDL2 is installed.  Download the latest *.dmg files from both of the links below, and install per the READMEs in the .dmgs (i.e. in "/Library/Frameworks")

* SDL2: https://github.com/libsdl-org/SDL/releases

* SDL_net: https://github.com/libsdl-org/SDL_net/releases


And then build Supermodel:

```
make -f Makefiles/Makefile.OSX
```

### Note: running on macOS
If you try and run a macOS binary that was downloaded from the internet and/or built on a different machine, you need to grant macOS permission to execute the binary (just 1-time):

* Open the folder containing the binary in Finder, and right (or ctrl) click on it:

* Click "Open" when the following dialogue box appears : "macOS cannot verify the developer of “supermodel-git-xxxx”. Are you sure you want to open it?"

* Close the terminal window that opens (after clicking open)

Details: https://support.apple.com/guide/mac-help/open-a-mac-app-from-an-unidentified-developer-mh40616/mac
