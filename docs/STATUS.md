# Status and roadmap

Current release: **0.1.0** (CHANGELOG.md).

## Verified (scripted playtests in tests/, see docs/ENGINEERING_LOG.md)

- Installation: Setup.cmd / tools/setup.ps1 from a fresh clone and the disc image: disc checked, XenonRecomp and Xenia
  built, the game code recompiled (instruction tests pass), the game built; it boots into a new campaign.
- Boots from the player's disc: logo movies, title screen with audio, menus with keyboard, mouse and controller;
  keyboard and mouse prompts that follow the device, also in the multiplayer lobby (tests/menu_kbm.txt,
  prompt_switch.txt).
- Campaign: all 29 chapter points (five acts) load their maps and play 150 s without a crash, 60 fps after loading
  (tools/chapter_test.py). New campaign with checkpoint saves (tests/new_campaign.txt).
- Horde offline (tests/horde.txt) and Training Grounds: Warzone with bots (tests/training.txt).
- Multiplayer over System Link between two games on one PC (tools/mp_test.ps1): Versus (Warzone on Avalanche), Horde,
  and campaign co-op with the second player as Dom; as two copies of one install, on a network, and on a
  gow2_server.
- Mods: a configuration fragment changes the game (tests/mod_menu_text.txt with examples/mods/ExampleMenuText); the
  game's SHA-1 file check accepts mods' files.
- Launcher: Gears 2's display, graphics, effect, control and audio options, mods, multiplayer.

## Not done / known issues

- No Xbox LIVE: Public and Private Xbox LIVE ask for it; System Link replaces them. Not yet tested: multiplayer
  between two PCs, a real router's UPnP, a mod that replaces a package. No voice chat.
- Chapters were checked for their first minutes; none has been played through to its end in tests.
- The Back button skips Gears 2's cinematics, but the intro movies of some chapters still run through in tests.

## Roadmap

1. Campaign play-throughs: checkpoint to checkpoint, set pieces, chapter transitions.
2. Mods: package replacement examples; more creator documentation.
3. Shared work with the Gears of War 3 port (same runtime, setup, launcher and multiplayer): fixes go to both.
4. The trilogy (Gears 1 and 2 campaigns in Gears 3's engine) lives in the Gears 3 port's tooling.
