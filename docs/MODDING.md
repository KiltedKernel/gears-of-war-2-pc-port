# Modding the Gears of War 2 PC port

This port runs Gears of War 2 natively on PC. Mods replace or add game packages and configuration, and stack with each other.

Mods contain material derived from the games. Share them only with people who own the games, and never
redistribute the games' own files unmodified.

## Installing a mod

Copy the mod's folder into `mods\` next to the game's executable (`build\mods\` in a source build):

```
build\
  gow2.exe
  mods\
    BrighterGridlock\
      mod.ini
      GearGame\CookedXenon\MP_Gridlock.xxx
```

That's all: start the game. The launcher's **Mods** tab lists the installed mods: tick or untick a mod to turn it on or
off, use **Up** / **Down** to change the load order (later mods win when two change the same file), **Install .zip...**
to add a shared mod, and select a mod to see what it changes and which other mods it overlaps. (Without the launcher:
set `enabled = false` in a mod's `mod.ini` to turn it off.)

The mod tool does the same from the command line:

```
python -P tools/modtool.py install BrighterGridlock-0.1.zip     # a shared mod into mods\ (--replace to update it)
python -P tools/modtool.py disable BrighterGridlock             # off, still installed
python -P tools/modtool.py enable BrighterGridlock              # on again
python -P tools/modtool.py pack BrighterGridlock                # your mod as one .zip to share (checked first)
```

`install` accepts a zip with one mod folder holding a `mod.ini`. It refuses paths that would land outside that
folder, and won't overwrite an installed mod unless you pass `--replace`.

## What a mod is

A folder with a `mod.ini` and game files at the same relative paths as on the disc.

- A file at `GearGame\CookedXenon\X.xxx` in your mod **replaces** the game's `X.xxx`.
- A file that the game doesn't have is **added**. The game finds packages through its table of contents
  (`GearGame\Xbox360TOC.txt`), and the runtime builds that list for you from the disc and every enabled mod, so a mod
  never ships or edits a TOC. If a mod contains one, it is ignored.

### mod.ini

```ini
[mod]
name = Brighter Gridlock
version = 0.1
author = Your name
description = One line about what the mod does.
priority = 0
enabled = true
```

| key | meaning |
|---|---|
| `name` | shown in the log and by `modtool.py mods` (default: the folder's name) |
| `version`, `author`, `description` | free text |
| `priority` | whole number, default 0; when two mods have the same file, the higher priority wins (equal: the later name alphabetically) |
| `enabled` | `false` / `0` / `no` turns the mod off |

Files placed directly in `mods\` (not inside a folder with a `mod.ini`) still work, as one mod named
"(loose files)" at priority 0.

### Load order and conflicts

When the game starts it writes to `build\gow2.log`:

```
mods: Night Gridlock 1 (priority -5)
mods: Brighter Gridlock 0.1 by someone (priority 0)
mods: conflict on geargame\cookedxenon\mp_gridlock.xxx: Night Gridlock, Brighter Gridlock (the last wins)
```

`python -P tools/modtool.py mods` shows the same from the command line, without starting the game.

## The mod tool

`tools/modtool.py` is the command line for creators (Python 3.10+):

```
python -P tools/modtool.py mods                      # installed mods, files, conflicts
python -P tools/modtool.py new-mod MyMod --name "My mod" --author "Me"
python -P tools/modtool.py check MyMod               # every package read back (Gears 2 packages are version 575)
python -P tools/modtool.py pack MyMod                # one .zip to share
python -P tools/modtool.py install MyMod-0.1.zip     # into mods\ (--replace to update)
python -P tools/modtool.py enable|disable MyMod
```

## Game configuration

Gears 2 keeps all its .ini files packed in `GearGame\CookedXenon\Coalesced_INT.bin` (menus, chapters, weapons,
game modes...). `tools/coalesced.py` reads and writes it:

```
python -P tools/coalesced.py dump  <Coalesced_INT.bin> config.txt          # readable text, one ##### header per ini
python -P tools/coalesced.py merge <Coalesced_INT.bin> <out.bin> my.ini   # apply an ini fragment
```

A fragment names its ini with `##### <file>` (for example `##### Xe-GearGame.ini` or `##### GearGameUI.int`),
then sections and keys, UE3 style: `Key=v` replaces the key, `+Key=v` adds a value, `-Key=v` removes one, and
`*Key=(Identifier=X,...)` replaces the entry with the same first field (a menu entry).

**Ship fragments, not the whole file.** Put them in the mod's `config\` folder (any number of `.ini` files):

```
MyMod\
  mod.ini
  config\
    menu.ini
```

When the game starts, the runtime merges every enabled mod's fragments, in load order, into each language's
`Coalesced_<LANG>.bin` (the disc's, or a whole file a mod ships) and serves the merged file. So several mods can
change the configuration at once. The log lists each fragment (`mods: config menu.ini from My mod: 3 lines`);
mistakes are logged with the file and line. `python -P tools/modtool.py check MyMod` checks fragments too.

A working example is in `examples/mods/ExampleMenuText`: it changes the main menu's "Latest News" line. Copy it into
`mods\` (or install its folder as a .zip from the launcher).

## Formats and further reading

- `docs/REVERSE_ENGINEERING.md`: how the port works and what is known about the game (addresses, formats).
- `docs/ENGINEERING_LOG.md`: the full development journal.
- `tools/upk.py`: read packages (big-endian; Gears 2's are version 575).
- `tools/coalesced.py`: the packed configuration. 
- `tools/texture2d.py`: textures.

## Debugging

- `build\gow2.log` lists the mods loaded, conflicts, and every file a mod overrides (`files: mod override ...`).
- `GOW3_LOG_POS=1` logs the player's and enemies' positions every 2 seconds, which helps when placing things.
