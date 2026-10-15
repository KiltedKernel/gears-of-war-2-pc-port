# Status and roadmap

Not released yet. The goal of the first release is the Gears of War 3 port's level (its 0.2.0).

## Verified (scripted playtests in tests/, see docs/ENGINEERING_LOG.md)

- Boots from the player's disc: logo movies, title screen with audio, main menu, Select Difficulty.
- New campaign: the opening narration and the first level loading, checkpoint saves (tests/new_campaign.txt),
  60 fps.
- Menus with keyboard, mouse and controller; keyboard and mouse prompts that follow the device, also in the
  multiplayer lobby (tests/menu_kbm.txt, prompt_switch.txt).
- Multiplayer over System Link between two games on one PC (tools/mp_test.ps1): party created and joined through the
  System Link Browser, Warzone on Avalanche played by both (tests/mp_host.txt, mp_join.txt).
- From the first working tree (gow2-port, 2026-10-10): the campaign's opening plays with combat and cover, saves and
  Continue, native mouse look, mouse in the menus, keyboard and mouse prompts (with Gears of War: Reloaded's art),
  effect and quality options.

## To do for the first release

1. Single player: every campaign act and chapter (coverage test per chapter), Training Grounds (Versus with bots),
   Horde, and the other offline modes from the main menu.
2. Multiplayer over System Link with the Gears 3 port's design (direct play with friends, servers, in-game
   notices): Versus, Horde and campaign co-op between two copies of the game.
3. Installation wizard, launcher (options for Gears 2's settings, multiplayer, mods), keyboard prompts drawn for
   Gears 2's font.
4. Mods: Gears 2's table of contents and configuration, examples.
5. Legal and size: no game data in the repository, notices, a lean tree.
