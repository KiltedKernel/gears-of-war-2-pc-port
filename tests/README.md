# Scripted tests

Each `.txt` file is an input script for `tools/playtest.ps1`: timed controller, keyboard and mouse events played
inside the runtime (the desktop is never touched), with screenshots taken from the game's own output.

```powershell
powershell -ExecutionPolicy Bypass -File tools\playtest.ps1 -Script tests\versus_bots.txt -Seconds 240 -Fresh -Shots "130,190"
```

Screenshots land in `build\shots`, the log in `build\gow2.log`. Tests run with their own saves, profile and settings
(`build\testdata`) and their own mods (`build\testmods`, from `-Mods "examples\mods\X"`), never the player's.

**Profile state.** Scripts marked *checkpoint* expect a campaign checkpoint in the test profile: run
`new_campaign.txt` once to make one.

| script | state | checks |
|---|---|---|
| new_campaign.txt | any | Solo Campaign > New Campaign > Casual: the opening narration and the first level (makes a checkpoint) |
| continue.txt | checkpoint | Solo Campaign > Continue loads the checkpoint |
| combat.txt | checkpoint | moving, aiming (right mouse), firing (left mouse), reloading (R) |
| esc_pause.txt | checkpoint | Esc pauses in play, Esc resumes |
| menu_kbm.txt | any | the main menu with the keyboard: keyboard and mouse prompts |
| mouse_menu.txt | any | menus with the mouse: hover, a callout click, right click, wheel |
| prompt_switch.txt | any | button prompts follow the last device used |
| mp_host_horde.txt + mp_join.txt | any | System Link Horde: the host's Horde lobby set to System Link, the party joined, Horde on Avalanche: wave 1 and its result on both |
| mp_host_coop.txt + mp_join_coop.txt | any | System Link co-op: Co-op Campaign > Host / Join Co-op Campaign, the campaign lobby, Launch: the joiner plays Dom beside the host's Marcus (Skip Training, the hospital) |
| horde.txt | any | Horde offline: Horde on Avalanche, wave 1 |
| training.txt | any | Training Grounds: Lesson 1, Warzone with bots on Day One |
| mp_host.txt + mp_join.txt | any | System Link (tools/mp_test.ps1): Multiplayer > Match Type: System Link opens the System Link Browser; the host creates a party, the joiner refreshes and joins it, the host starts Warzone on Avalanche and both play it |

Multiplayer runs two games at once, each with its own settings and log in `build\mp\host` and `build\mp\join`,
connected through loopback only (no firewall prompt). `-Connect` picks the setup: `lan` (no code: the second finds
the first), `samepc` (one data folder, as two copies of one install: the second gets `instance2`), `direct`, `code`,
`server` (a gow2_server.exe started for the test), `hairpin` (a friend's code whose address cannot be reached from
inside the network: found on the network instead) and `unreachable` (a code nobody answers at: a message, then the
game on the network). The `net:` lines of each log show every step, `net: [title] text` the messages shown in the
game:

```powershell
powershell -ExecutionPolicy Bypass -File tools\mp_test.ps1 -HostScript tests\mp_host.txt -JoinScript tests\mp_join.txt -Seconds 280 -Shots "175,230" -Connect samepc
```

Scripts can steer the player: `<s> walkto <x> <y> [seconds]` walks to a map position, `<s> face <x> <y>` turns
the camera toward one (hid/hid.cpp). Positions come from `GOW2_LOG_POS=1` (pawns) and
`GOW2_LOG_ACTORS=Class[:Property],...` (any actors).
`python tools/contact_sheet.py out.png build\mp\host\shots build\mp\join\shots` puts a run's screenshots side by
side.

Campaign coverage (every chapter booted directly, no menus) is `python tools\chapter_test.py`; see the README.
