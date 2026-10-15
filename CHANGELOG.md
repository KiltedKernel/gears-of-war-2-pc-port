# Changelog

## Unreleased

- Fix: NtClose took the game's session handles (numbers from 0xF5E50000) for kernel objects' addresses and ran a
  destructor on whatever was there (an intermittent crash of a joining game in the Gears of War 3 port, same code).
  Session handles are closed as sessions; anything that is not an open object gets "invalid handle".

## 0.1.0 (2026-10-15)

The first release: Gears of War 2 at the Gears of War 3 port's level.


- gears2-pc: the Gears of War 2 port rebuilt on the Gears of War 3 port's runtime, setup, launcher and multiplayer,
  with Gears 2's own function list, jump tables, patches, menus, prompts and effect settings. Boots into the
  campaign at 60 fps.
- The whole campaign: all 29 chapter points load and play (tools/chapter_test.py); Horde and Training Grounds offline.
- Installation wizard (Setup.cmd) verified from a fresh clone: disc image to a playing game.
- Multiplayer over System Link (with friends or on a server, as in the Gears of War 3 port): Multiplayer > Match
  Type: System Link; verified with two games on one PC playing Warzone. Gears 2 and Gears 3 games never mix: each
  turns the other away with a reason.
- Keyboard and mouse prompts fit Gears 2's font page and survive the lobby's copies of it; Esc goes back in menus.
- Launcher: Gears 2's graphics and effect options. Mods use Gears 2's TOC format.
- Fix: Act 1 chapter 3's opening cinematic crashed (a constructor the code analysis missed).
- Campaign co-op over System Link: the second player plays Dom (the frame-rate unlock now caps the engine's tick at
  the chosen frame rate; uncapped, a joiner never received the world).
- Mods: configuration fragments and replaced files pass the game's own SHA-1 file check.
- Setup: Git for Windows' bash is used even when WSL's bash.exe is on PATH.
