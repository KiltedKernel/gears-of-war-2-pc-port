# Reverse engineering notes: Gears of War 2 (Xbox 360)

What the port knows about the game, for this disc's executable only (`tools/default_xex.sha256`). Addresses are guest
virtual addresses in the executable image. The development journal (docs/ENGINEERING_LOG.md) has how each fact was
found.

## The executable

- `default.xex`: XEX2, retail AES encryption, basic compression; base 0x82000000, entry 0x82AAC8D8. `tools/xex_unpack.py`
  writes the flat image (`default.bin`), reading the key from XenonRecomp's sources.
- Sections: .rdata 82000400, .pdata 821D1800 (36,657 RUNTIME_FUNCTION entries), BINKBSS, .text 82220000 (0xD2DBCC),
  BINK 82F4DC00 (Bink linked in), .data 82F60000, .XBMOVIE, BINKDATA, .idata, .XBLD, .reloc. The security info's
  image size (0x1230000) is larger than its blocks add up to (0x1228000).
- Imports: xboxkrnl.exe (324 records) and xam.xex (244): 275 named functions reach the recompiled code.
- Register save/restore helpers: restgprlr_14 82A930F0, savegprlr_14 82A930A0, restfpr_14 82A9344C, savefpr_14
  82A93400, restvmx_14 82A944E8, savevmx_14 82A94250, restvmx_64 82A9457C, savevmx_64 82A942E4.
- Function bounds the analyser gets wrong are in config/GOW2_functions.toml (tools/fix_boundaries.py). Notable:
  memset 82A93150 (size 0xA0): its byte tail has `bdzlr` between stores, and ending the function at the first one
  left lengths of 2-3 bytes past the words unwritten (Carmine's arm, from InitSkelControls 825895B8). Functions that
  never return: C++ throw 82CD54A0, longjmp 82A93950, error-then-longjmp 82BAFB70, exit 82A96328.
- Jump tables: config/GOW2_switch_tables.toml (510, checked against the image by tools/check_switches.py); absolute
  tables with the bounds check in the caller were found by tools/find_inline_switches.py (example 822FF238).

## Patches (config/GOW2_hooks.toml, runtime/kernel/gow2_patches.cpp)

| address | what | port |
|---|---|---|
| 824A2E94 | max tick rate getter (824A2E78) loads its 30 fps cap from this+1284 | skipped: 0.0 = no cap (frame_rate > 30) |
| 8298CFB0 | the D3D maps presentation interval TWO to 2 | 1: present every vblank |
| 8280E7FC | MaxAnisotropy load | 16 (anisotropic_16x) |
| 8280D534, 8280DDC8 | texture pool size; RHIGetTextureMemoryStats | 196 MB; returns false (texture_streaming_fix) |
| 82806A44 | store of GUseTilingMode | skipped, stays FALSE (black_shading_fix) |
| 82741300, 82741884 | GSystemSettings.MaxShadowResolution reads (800) | shadow_resolution |

The frame-rate, anisotropy, texture and tiling patches follow Xenia's game-patches for this title (illusion, boma).
UE3 times the game with the performance counter, so game speed does not depend on the frame rate.

## Engine (UE3, package version 575)

- GSystemSettings: 0x8313751C. Fields (from FSystemSettings' name table, sub_82351488): MotionBlur +0x2C,
  DepthOfField +0x30, Bloom +0x38, Distortion +0x40, LensFlares +0x48, FogVolumes +0x4C,
  bEnableForegroundSelfShadowing +0x1E8; ResX/ResY +0x1B0/+0x1B4 (1280x720 once the config is read).
  runtime/ui/system_settings.cpp writes gow2.ini's effects there.
- Reflection (runtime/ue3/reflection.cpp): FNameEntry {u32 Index = n*2, u32 HashNext, char Name[]}; GNames is an array
  of entry pointers (0x03AC0000 or 0x03AA0000, "GameViewportClient" = name 1968); GObjObjects at 0x01300000 (about
  133k slots). UObject +4 slot, +40 Outer, +44 Name, +52 Class. UStruct +60 super, +76 children, +80 size; UField +64
  next. UProperty +68 ArrayDim, +72 ElementSize, +100 Offset; UBoolProperty +132 BitMask. UFunction +172 native
  function.
- Input: the input thread (827FA8F8) polls pads 0-3 on a 16 ms timer into an 84-byte sample ring (827FA7C0).
  UPlayerInput::InputAxis 826404E0 (this r3, controller r4, FName key r5 = index << 32 | number, delta f1,
  delta time f2, bGamepad r6), called by the pad pump 82526B1C for every stick axis each frame. Key FNames from the
  static initializer 82457E68: KEY_MouseX 8313AA24, KEY_MouseY 8313AA2C, KEY_XboxTypeS_RightX 8313AD94, RightY
  8313AD9C. runtime/hid/mouse_look.cpp calls InputAxis with the mouse keys: the engine's own PC mouse look.
- Skeletal controls: live per-frame slots +110/+114; SkelControlLimb::CalculateNewBoneTransforms 8257C1F0.
  SkelComp +480 SkeletalMesh, +532 SpaceBases, +644 SkelControlIndex.

## Menus (UE3 UI scenes)

- GearGameUISceneClient: MousePosition +0x48, ActiveControl +0x50, ActiveScenes +0x100. UIScreenObject Children
  +0x60, StateStack +0x88, flags +0x5C (bHidden 0x80000000, bNeverFocus 0x20000000). UIObject RenderBounds +0x244
  (1280x720 canvas), Owner +0x1B8. UIScene flags +0x334. UICalloutButton InputAliasTag +0x484.
  UIDataStore_InputAlias.InputAliases +0x78 (80-byte entries: alias name +0, first key name +8).
- execSetFocus 8242F198 calls UIScreenObject::SetFocus through vtable +344 (r3 widget, r4 sender, r5 player).
  runtime/ui/mouse_menu.cpp turns the mouse into focus changes and controller buttons.
- Button prompts: the Warfare_HUD.Xbox360_18pt font, page 0 (256x256 DXT5, tiled) and a 256-entry FFontCharacter
  table {StartU, StartV, USize, VSize, TextureIndex (byte), VerticalOffset}, read on every draw
  (runtime/ui/prompts.cpp).

## Data

- `GearGame\Xbox360TOC*.txt`: one line per file, `<size> <uncompressed size, 0 = stored> <disc offset>
  ..\GearGame\CookedXenon\<file> <md5>`. Packages compressed as a whole list their uncompressed size.
- `GearGame\CookedXenon\Coalesced_<LANG>.bin`: every .ini (63 files). Big-endian int32 counts; strings are int32 n then
  n ANSI bytes, or -n UTF-16LE units, NUL included (tools/coalesced.py).
- Campaign: Xe-GearGame.ini `ChapterLevelNames[0..28]` and `[ChapterN GearCampaignChapterData]` (ChapterType,
  ActType, MapName). A chapter starts with `GearGame_P?game=geargamecontent.GearGameSP?chapter=N`
  (tools/chapter_test.py). The Back button skips a cinematic ("Skip Cinematic"); Start pauses it.
- Movies: `GearGame\Movies\*.bik` (Bink 1); startup logos EpicLogo and MGSLogo.
- Saves: content type 1 (savegame) `Gears2Checkpoint`, files `Gears2Checkpoint<N>.sav`.

## Network

- Xe-GearEngine.ini: LiveSystemLinkPort 14000, LanAnnouncePort 14001, LanQueryTimeout, LanGameUniqueId 1297287213.
  The party, the LAN game browser (right trigger: the "LANBrowser" alias) and co-op search use them; the port's
  virtual LAN (docs/MULTIPLAYER.md) carries every port alike.
