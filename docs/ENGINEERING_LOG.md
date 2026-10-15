# Engineering log

The development journal of the Gears of War 2 PC port. The entries up to 2026-10-11 were written in the first
working tree (gow2-port); from 2026-10-14 the port is rebuilt on the runtime, launcher, setup and multiplayer of the
Gears of War 3 port, which itself started from this one.


Player-owned disc: `Gears of War 2 [RF][MEX].iso` (user confirmed it is a dump of their own disc). Dev copies of
game data live in `D:\gow2-decomp\` (never committed, never shipped). Generated C++ (`ppc/`) is produced on the
player's PC from their own XEX; the repo holds tools, config and runtime only.

## 2026-10-09 - Recon
- ISO: XGD2, XDVDFS partition at 0xFD90000 (`tools`: scratch xdvdfs.py lister/extractor).
  `default.xex` 18,595,840 B; `GearGame/CookedXenon/*.xxx` 1,781 UE3 packages (6.7 GB, Xbox cooked: big-endian,
  tiled textures, XMA audio); `GearGame/Movies/*.bik`; `Xbox360TOC*.txt`; `$SystemUpdate`.
- XEX2: retail AES (key in XenonUtils/xex.h), basic compression, base 0x82000000, entry 0x82AAC8D8,
  security image size 0x1230000 but the blocks add up to 0x1228000 (see fix below).
  Sections: .rdata 82000400, .pdata 821D1800 (36,657 RUNTIME_FUNCTION entries), BINKBSS, .text 82220000
  (0xD2DBCC), BINK 82F4DC00 (Bink statically linked), .data 82F60000, .XBMOVIE, BINKDATA, .idata, .XBLD, .reloc.
  Imports: xboxkrnl.exe (324 records), xam.xex (244) -> 275 named functions in the recompiled code.
- `tools/xex_unpack.py`: decrypt + decompress to the flat image (`D:\gow2-decomp\default.bin`, MZ header OK).

## 2026-10-09 - Toolchain
- LLVM 23.1.3 (clang-cl) + Ninja via scoop; VS 2022 for the Windows SDK/libs (`vcvars64`).
- XenonRecomp (hedge-dev, commit ddd128b) in `tools/XenonRecomp`, built with `tools/build_xenon.cmd`.
  Local patches (to upstream later):
  1. `XenonUtils/xex.cpp`: basic-compression buffer allocated as max(sum of blocks, security imageSize), zeroed.
     The image is treated as imageSize bytes later -> heap over-read (my first dumper crashed on it).
  2. `tools/patch_xenonrecomp_insns.py`: bdnzt, dcbst (no-op), eqv, mulhd, frsqrte, vnor128, vsel128 (= vsel, mask
     in operand 3), vpkuhus128/vpkuwus128 (clamp with min_epu then packus), vpkswss128 (packs_epi32),
     vcfpuxws128 (new `simde_mm_vctuxs` in ppc_context.h). Operand order (vB, vA) follows XenonRecomp's reversed
     vectors like its VPKSHUS. **Not unit-tested yet** (XenonTests).

## 2026-10-09 - Recompilation
- `config/GOW2.toml`; save/restore helpers by README byte patterns, one hit each:
  restgprlr_14 82A930F0, savegprlr_14 82A930A0, restfpr_14 82A9344C, savefpr_14 82A93400, restvmx_14 82A944E8,
  savevmx_14 82A94250, restvmx_64 82A9457C, savevmx_64 82A942E4. setjmp/longjmp not located yet.
- XenonAnalyse: 504 jump tables (`config/GOW2_switch_tables.toml`).
- First recompile: ~1,100 "switch case jumps outside function" errors (84 switches) + 173 unknown instructions.
  - `tools/fix_boundaries.py`: bounds from .pdata (BeginAddress, FuncLen<<8 packed, in instructions) when it covers
    the switch; frameless leaf functions (no .pdata, e.g. 82230368 with an inline table right after bctr) get a
    control-flow walk (branches + switch labels, tail calls to known starts stop it). Overlaps merged.
  - `tools/fix_stubs.py`: stubs like 8299D3F8 (argument shuffle, `b` into the real function) swallowed the next
    function; bounded to end at the real start (18 stubs).
  - Result: **0 switch errors, 0 unknown instructions; 50,161 functions**, 232 MB C++ in 198 files (`tools/recomp.sh`).
- Build: CMake + Ninja + clang-cl (`tools/build.cmd`), whole program compiles and links in 43 s -> `build/gow2.exe`.

## 2026-10-09 - First boot (minimal runtime: runtime/main.cpp, generated weak import stubs)
- 4 GB guest space; function table at base + 0x83230000 (+ (addr - 0x82220000) * 2), 50,109 mapped.
- Fake PCR at 0x7FFF0000 for r13. Entry 82AAC8D8 runs the CRT startup and calls, in order:
  NtAllocateVirtualMemory(lr 82AAE7A0) -> KeGetCurrentProcessType (82AAE8F0) -> RtlInitializeCriticalSection
  (82AAE93C), then crashes (the stub allocated nothing).

## 2026-10-09 - Runtime and boot to the main loop
Runtime = Unleashed Recompiled's kernel/cpu pieces (GPL-3.0, `runtime/COPYING.unleashed`) + GoW2 code in
`runtime/kernel/gow2_*.cpp` (namespace `gow2k`; `tools/dedupe_hooks.py` comments out the Unleashed hook of any import
defined there - run before every build). Result: **the game boots through CRT, D3D, audio, file system, startup
packages and the logo movies into its frame loop: 11,700 swaps in 180 s (~65/s), no crash**, null GPU (no pixels).
- **Address space** (`kernel/memory.cpp`), as on the console / Xenia:
  - 0x00000000-0x3FFFFFFF 4 KB-page and 0x40000000-0x7EFFFFFF 64 KB-page virtual memory. UE3's free routine
    (827F80C8) classifies pointers by these ranges, so they matter. Reserved as placeholders; on commit,
    NtAllocateVirtualMemory maps 64 KB views of the physical section (`kernel/gow2_memory.cpp`), so virtual
    allocations alias physical memory and MmGetPhysicalAddress is exact.
  - 0x7F000000-0x9FFFFFFF ordinary memory: GPU MMIO 0x7FC80000, image, function table, host-side heap at 0x90000000.
  - 512 MB physical section viewed at 0xA0000000, 0xC0000000 and 0xE0000000. **The E0 view is shifted down 4 KB**:
    virtual E0000000+x = physical x+0x1000, as on the console. The D3D computes physical addresses that way (its
    fence block E011A000 -> physical 0x11B000); without the shift the GPU wrote fences to the wrong page.
  - One physical allocator (`kernel/physical.cpp`, address-ordered first fit) for MmAllocatePhysicalMemoryEx and VM
    frames. Window by page size: MEM_LARGE_PAGES (0x20000000) -> 0xA0000000, 16MB_PAGES (0x80000000) -> 0xC0000000,
    else 0xE0000000. UE3 takes a 140 MB pool.
- **GPU (null command processor, `gpu/command_processor.cpp`)**: PM4 ring, indirect buffers, WAIT_REG_MEM, REG_RMW,
  COND_WRITE, EVENT_WRITE_SHD fences, MEM_WRITE, INTERRUPT, XE_SWAP; draws consumed, not executed.
  - VdSwap's packet is `type3(XE_SWAP, count field 62)` = header + 63 dwords = the 64 the guest reserves. Count 63
    desynchronized the parser and swallowed the next fence; the D3D then waited forever in its ring-space wait
    8298C2A0 (`[dev+10896]` -> retired fence, `[dev+10908]` = last issued fence).
  - Scratch registers 0x578-0x57F mirror to SCRATCH_ADDR (0x1DD) when enabled in SCRATCH_UMSK (0x1DC), big-endian.
  - Debug: `GOW2_GPU_DEBUG=1` (ring state and MMIO every 2 s), `GOW2_GPU_TRACE=1` (packets, register/memory writes).
- **Kernel**: Xbox `NtQueryVirtualMemory` has 3 args (no return length). Sync (`gow2_sync.cpp`, `kernel/wait.h`):
  - Unleashed's Event/Semaphore waits handled only 0/INFINITE (a finite timeout returned success at once). They now
    take finite relative and absolute timeouts (WaitOnAddress, compatible with std::atomic notify).
  - Mutants, notification/synchronization timers (one scheduler thread; APC routines not delivered yet),
    NtWaitForMultipleObjectsEx / KeWaitForMultipleObjects any/all with timeouts (shared signal generation).
  - ExTerminateThread throws `GuestThreadExit` to GuestThreadFunc; thread waits honor timeouts.
  - InterlockedPop/FlushSList: 64-bit CAS on the BE header (next u32, depth u16, sequence u16), compatible with
    the title's inline ldarx/stdcx. push.
  - RtlFillMemoryUlong, RtlTime(To|FieldsTo)Time(Fields).
- **XAM** (`gow2_xam.cpp`): one local offline profile in slot 0 (Unleashed reported all four users signed in);
  XUID/name, no launch data (0x490), XamAlloc/XamFree, XNotifyPositionUI, XMsgCancelIORequest, WSA last error,
  inet_addr.
  - Enumerators: empty achievements; XamCreateEnumeratorHandle's private structure is **0x18-byte header +
    extra_size**. The title writes p+0x18..p+0x18+extra (829A5D4C); a short block corrupted the host heap.
  - XamEnumerate rejects bad handles and completes XOVERLAPPED (result, length, event, ERROR_IO_PENDING).
- **Audio**: XMACreateContext/XMAReleaseContext hand out 64-byte contexts from a 320-entry physical pool. With no
  context, a voice object stayed null (827FDDD8). Decoding not implemented.
- **Recompiler**: `vpkd3d128` type 3 (FLOAT16_2, pack VPACK_32) was a `__builtin_debugtrap` (D3D 8298A094). Now
  `(half(x)<<16)|half(y)` into host u32[shift]. `tools/recomp.sh` regenerates into ppc_new and copies only changed
  files, so a recompiler fix rebuilds one file, not 198.
- **Debug tools**: crash reporter (guest regs + symbolized host stack), `GOW2_HANG_DUMP=<s>` (all threads' stacks
  with guest function names), `GOW2_TRACE_IMPORTS`, `GOW2_CHECK_HEAP`, `GOW2_DEBUG_HOOKS` (`runtime/debug_hooks.cpp`:
  a strong `sub_X` wraps the recompiled `__imp__sub_X`). Unimplemented imports log their first 3 calls.
- Known gaps: crash reporter can stall in DbgHelp while other threads run (no stack printed). Still stubbed: XNet/WSA
  (offline), XMsgStartIORequest(Ex), ObDereferenceObject, ExRegisterTitleTerminateNotification, Xam*UI, voice, sessions.

## 2026-10-10 - GPU rendering: Xenia's Direct3D 12 GPU embedded
**The intro movie and the title screen ("PRESS START") render correctly** (RTX 5070 Ti, D3D12; screenshots via
`tools/shot.ps1`). The game's own recompiled D3D drives Xenia's PM4 command processor, DXBC shader translator, texture
cache, EDRAM render-target cache and presenter.
- **Dependency**: Xenia Canary (BSD-3) at `D:\xenia`, commit in `tools/xenia-commit.txt`; `tools/setup_xenia.sh`
  clones it, fetches submodules, applies `tools/xenia-gow2.patch`, writes version.h. `tools/build_xenia.cmd` builds
  its GPU libraries with its own CMake and **MSVC** (static CRT, `-DXE_GOW2_NO_LTCG=ON`: no /GL objects, since gow2.exe
  links with clang-cl/lld). `gow2.exe` links the resulting .libs (CMakeLists.txt, `XENIA_LIBS`).
- **Patch** (all under `XE_GOW2`; upstream code kept in `#else`): no Xenia kernel/CPU headers in the GPU build;
  the command processor worker is an `xe::threading::Thread` (not XHostThread); GraphicsSystem::Setup takes memory
  from the glue, skips Xenia's MMIO registration, runs a 60 Hz vblank thread (D1MODE_V_COUNTER + MarkVblank), and
  delivers interrupts through the glue; fixed 1280x720; title id 4D53082D for trace names.
- **Glue** (`xe_glue/`, built inside Xenia's build; interface `xe_glue/gow2_gpu.h`, plain C++):
  - `xe::Memory` is a zeroed, never-constructed object; Initialize() points virtual_membase at the runtime's guest
    base and physical_membase at its 0xA0000000 view. Xenia's TranslateVirtual adds 0x1000 for E0 addresses, which
    is the runtime's E0 shift. Only the members the GPU uses are defined: invalidation callbacks, access watches,
    and a placeholder physical heap whose queries say "committed read-write".
  - CPU-write watches: a 4 KB-page bitmap over the 512 MB. Watched pages are read-only in all three views; a fault
    runs the invalidation callbacks and unwatches the returned range (capped at 4 MB, Xenia's rule).
  - A UI thread owns Xenia's Win32WindowedAppContext, the window and its message loop; the presenter is attached
    after Setup.
- **Runtime side** (`runtime/gpu/xenos_backend.cpp`): gpu::Initialize after KiSystemStartup (`GOW2_NULL_GPU=1`
  keeps the null command processor). Guest MMIO stays plain memory seeded with the console's read values. Writes
  are trapped: the page is read-only, the fault handler unprotects it and sets the trap flag, then single-step
  forwards the new value (CP_RB_WPTR -> UpdateWritePointer) and re-protects. No x86 decoding, no polling. The same
  first-chance handler forwards physical write faults to the glue.
- **VdSwap** now matches Xenia: 10 args (front buffer ptr, texture format, color space, width, height). It writes
  the front buffer's fetch constant (CPU address made physical) to fetch constant 0, then XE_SWAP
  {'SWAP', physical, w, h}, then NOPs. The old 8-arg version read the format/colour-space pointers as width/height.
  Front buffer E056F000 -> physical 0x570000, 8_8_8_8 1280x720.
- **Gotchas**:
  - Xenia's GetExecutablePath uses `_get_wpgmptr`, which is unset under a narrow `main`, and the CRT fail-fasts
    (0xC0000409) in InitializeLogging. The runtime now uses `wmain`.
  - Run gow2.exe from PowerShell/cmd: launched from this git-bash, it reports exit 127.
  - Xenia logs to `build/gow2-gpu.log`; startup steps go to `build/gow2-gpu-start.txt`.
- Licensing: shipping binaries must carry Xenia's BSD-3 notice (plus GPL-3.0 for the Unleashed-derived runtime).

## 2026-10-10 - Input, menus, and into the campaign
**Title -> main menu -> Solo Campaign -> difficulty -> the campaign loads and plays its opening (cutscene,
in-engine with prompts)**, driven by keyboard input (`tools/shot.ps1 -Keys`).
- **Input** (`runtime/hid/hid.cpp`, `kernel/gow2_input.cpp`):
  - XInput pads, plus keyboard/mouse on player 1. Player 1 is always connected; players 2-4 only with a real pad.
  - XamInputGetCapabilities has 3 args (Unleashed used the Ex form's 4). Not-connected pads return
    ERROR_DEVICE_NOT_CONNECTED (Unleashed reported 4 pads). The packet number counts state changes.
    Connection changes broadcast XN_SYS_INPUTDEVICESCHANGED (0x12).
  - Keys: WASD stick, mouse look, LMB/RMB fire/aim, Space/Shift A, F B, E X, Q Y, R RB, Tab LB, C LS, Z/MMB RS,
    1-4/arrows D-pad, Enter/Esc Start, Backspace Back. Mouse look: a 500 Hz sampler accumulates motion while
    captured (left click; Alt/focus loss releases; cursor hidden/clipped on the UI thread via the glue); each poll
    maps motion since the previous one (>= 8 ms window) to a stick rate past the 7849 deadzone (GOW2_MOUSE_SENS).
  - The game's input thread (827FA8F8) polls pads 0-3 on a 16 ms periodic timer into an 84-byte sample ring
    (827FA7C0). `GOW2_INPUT_DEBUG=1` logs polls and changes.
- **Timebase**: `mftb` was `__rdtsc()` (host TSC, ~80x the 49.875 MHz KeQueryPerformanceFrequency reports).
  XenonRecomp now emits `PPCQueryTimebase()` (runtime/kernel/gow2_time.cpp: QPC scaled to 49.875 MHz).
- **Deadlock that froze the title screen**: the glue's write-watch lock vs Xenia's global critical region
  (the GPU arms watches holding it; the fault path took the glue lock first, then the callback took the global
  lock). Watches now use Xenia's recursive global critical region, taken first, as Xenia's fault path does.
- **Notifications** (xam.cpp, Xenia rules): an id's mask index is bits 25-30 and version bits 16-24 (Unleashed's
  MSG_AREA was wrong). The first system listener gets XN_SYS_UI(0) + XN_SYS_SIGNINCHANGED(1); the first Live
  listener gets LIVE_CONNECTIONCHANGED(LOGON_DISCONNECTED) + LINK_STATE(0). The queue is locked.
- **XMsg** (`kernel/gow2_xmsg.cpp`): StartIORequest(Ex) and InProcessCall dispatch to system apps and complete
  overlapped. XMP 0xFA, XGI 0xFB accept; XLiveBase 0xFC answers like Xenia (58004 logon id = 1, 58006/58007
  NOT_LOGGED_ON, 5800E/58046 ok, else E_FAIL). Unleashed's InProcessCall returned success without filling
  outputs; at 829A4280 that left stack garbage as an allocation size (NtAllocateVirtualMemory 0x900FC000).
  The guest stack chain is now logged on that error.
- **Missed switches** (`tools/find_inline_switches.py`): absolute jump tables with the bounds check in the
  caller. Example: 822FF238 (14 cases, table inline after the bctr), whose bctr became an indirect call to a
  case label -> jump to host 0 when loading the campaign. A switch is keyed by its `lis` (XenonRecomp arms it
  there). Found 6, then fix_boundaries.py (91 bounds) -> 0 switch errors.
- **Networking** (`kernel/gow2_net.cpp`): NetDll_*/XNet on host Winsock (Xenia semantics). UE3's net driver
  needs real sockets even for solo play ("Error initializing network layer" before). Guest sockaddr family is
  BE; 4-byte options/ioctl args are swapped; errors go to the TEB last error (GuestThread::GetLastError). VDP is
  UDP. WSA overlapped I/O is synchronous. XNet is a loopback console with link down, no DNS/QoS.
  INADDR_ANY binds go to loopback unless GOW2_LAN=1: binding all interfaces made Windows Firewall prompt.
- Also: gow2.exe is a GUI-subsystem program (no console window); shot.ps1 is DPI-aware;
  XamUserWriteProfileSettings accepted (not persisted).

## 2026-10-10 - Playtest, saves, audio, settings
**Playtest** (`tools/shot.ps1`; input only while the game window is foreground):
- Choose Path (Skip Training / Train), then gameplay in Act 1: Marcus walks (W) and the camera turns (mouse);
  objective "Take Carmine on his first patrol"; checkpoints. **30.0 fps locked** (the console's rate), at 1x and
  2x internal resolution.
- **Saves**: a checkpoint writes `Gears2Checkpoint0.sav`. After a restart, "Select Save Slot" shows "Tip of the
  Spear - Welcome to Delta" with its thumbnail and time.
- Automated input stops when the user is at the machine (Windows foreground lock); the script skips rather than
  steals focus.

**XContent** (`kernel/gow2_content.cpp`, Xenia semantics) replaces Unleashed's in-memory registry:
- One folder per package: `%LOCALAPPDATA%\GoW2Recomp\content\<xuid for saves, else 0>\4D53082D\<type>\<name>\`,
  plus `<name>.header` (XCONTENT_DATA) for enumeration.
- All five open modes and dispositions; overlapped completion (Length = disposition); Close unmounts the root
  (xam.cpp root map: case-insensitive, remountable, locked).
- Delete, Flush, GetCreator, SetThumbnail (`<name>.thumbnail.png`), enumeration from disk
  (XamCreateItemEnumerator), the "Hard Drive" device, and XamShowDeviceSelectorUI picking it at once.
- GoW2 uses root `savedrive:` and package `Gears2Checkpoint`.

**Audio** - Xenia's apu (XAudio2 output + FFmpeg XMA decoder), embedded like the GPU:
- Patch: no kernel/CPU; worker threads are xe::threading::Thread; the guest callback runs via `CallGuest`.
  The XMA decoder skips Xenia's MMIO registration; its context array comes from the glue's
  `Memory::SystemHeapAlloc` (physical).
- The clients_ `memset` would zero a std::mutex (crash on lock): replaced with field initialization.
- Runtime (`kernel/gow2_audio.cpp`): render driver client/submit (driver = 0x41550000|index),
  XMACreate/ReleaseContext -> the decoder's AllocateContext, XAudioGetVoiceCategoryVolume = 1.0 (Unleashed left
  it unwritten).
- **MMIO trap generalized** (`gpu/xenos_backend.cpp`): devices GPU 0x7FC80000 (writes trapped) and XMA 0x7FEA0000
  (reads and writes trapped; page no-access; a read stores the register's value before single-stepping).
- **XMA registers are little-endian**: the title uses `lwbrx`/`stwbrx` (sub_82951828 reads ContextArrayAddress
  0x7FEA1800). In memory they sit as LE bytes, so Read/WriteRegister need one more swap than the GPU's.
  Without it the title saw the array at 0x00001000, computed hardware index 0x3FC0, and kicked registers
  0x84E/0x88E instead of 0x650/0x690.
- **Timer resolution**: `timeBeginPeriod(1)` at startup. Under the default 15.6 ms, the 5.333 ms audio pump
  delivered 129 frames/s instead of **187.5 (48 kHz / 256)**. Now exact; other timed waits benefit too.
- Verified by data (no listening here): frames/s and peak level every 10 s in gow2.log. Peaks 0.12-0.6 through
  intros, title and menus.

**Settings** (`runtime/user/settings.cpp`): `gow2.ini` next to the exe (defaults written on first run):
- [display] fullscreen, resolution_scale 1-3 (Xenia draw_resolution_scale), vsync.
- [input] mouse_sensitivity, invert_mouse_y. [audio] enabled.
- F11 / Alt+Enter toggles fullscreen (glue -> window on the UI thread); Enter is ignored as Start while Alt is held.

## 2026-10-10 - Frame rate unlock, native mouse look, crash fix, test tooling
- **Frame rate: 60 and 120 fps work** (gow2.ini `frame_rate`; 30 = console). The lock is the D3D presenting every
  2nd vblank plus the engine's max tick rate. Patches from Xenia game-patches "Unlock FPS" (illusion, boma); the
  addresses match this XEX. They are mid-asm hooks (`config/GOW2_hooks.toml`, `kernel/gow2_patches.cpp`), since
  recompiled code ignores code bytes changed at runtime:
  - 824A2E94: skip the load of the cap, so the getter returns 0.0 (no cap).
  - 8298CFB0: `li r11,2` -> 1, present every vblank.
  - The emulated vblank runs at max(60, frame_rate) Hz (patched graphics_system vblank thread ->
    xe::gow2::VblankIntervalMicroseconds).
  - Measured in gameplay: 60.0, and 117.9-120.0 (one 100 dip while streaming); audio stays 187.5 frames/s. The
    config's MaxSmoothedFrameRate=62 / UseVsync=False are not what locks it.
  - TOML gotcha: `[[midasm_hook]]` tables must come after the generated `functions = [...]` (else they capture
    it: 1209 switch errors). recomp.sh appends `config/GOW2_hooks.toml` last.
- **Native mouse look** (`hid/mouse_look.cpp`): the Xbox build keeps UE3's PC mouse path.
  - Coalesced Xe-GearInput.ini: `MouseX -> "Count bXAxis | Axis aMouseX"`, MouseSensitivity=60, mouse smoothing on.
  - `UPlayerInput::InputAxis` = 826404E0 (this r3, ctrl r4, FName r5 = index<<32|number, Delta f1, DeltaTime f2,
    bGamepad r6). The gamepad pump (82526B1C) calls it for all 4 stick axes every frame, zeros included.
  - After the RightX/RightY calls we call it with KEY_MouseX/Y (FName globals from static init 82457E68:
    MouseX 8313AA24, MouseY 8313AA2C, RightX 8313AD94, RightY 8313AD9C) and the raw counts since the last frame.
  - Raw Input (message-only window, RIDEV_INPUTSINK): unaccelerated device counts.
  - MouseY: positive = look up (UE3 convention), so the raw +y (toward the user) is negated. An earlier "this
    game is inverted" conclusion came from a test contaminated by the user's real mouse; confirmed with an isolated
    scripted run (tests/look_vertical.txt): up tilts the camera up. Base 0.2 x sensitivity: 600 counts ~ 40 degrees.
  - Cursor: while captured, the input thread pins it to a 1-pixel clip rectangle at the window center and re-applies
    it every 2 ms (Windows drops clips on focus/move/resize/fullscreen; the old one-time client-area clip let the
    cursor escape). The glue only hides it over the window.
  - The stick emulation stays as fallback where the engine takes no mouse axes. gow2.ini `mouse_mode = native|stick`.
- **Crash fix**: MmAllocatePhysicalMemoryEx now zeroes (as Xenia effectively does). Reused physical memory left a
  stale list link (next = 1), and campaign loads crashed at 82583AE4 (`lwz r11,96(r17)`), intermittently at any
  frame rate.
- **Quality options** (mid-asm hooks, gow2.ini [graphics]):
  - `anisotropic_16x` (default on): 8280E7FC, MaxAnisotropy = 16.
  - `texture_streaming_fix` (opt-in): 8280D534 pool 196 MB; RHIGetTextureMemoryStats (8280DDC8) returns false.
- **Tools**:
  - `tools/coalesced.py`: Coalesced_*.bin reader/dumper (UTF-16 strings, BE counts; writer still differs in string
    encoding).
  - `tools/find_refs.py` (lis+lo address references), `tools/dis.py` (disassembly at an address from the generated
    sources).
  - `tools/playtest.ps1` + `GOW2_INPUT_SCRIPT` (scripted keys/mouse played inside the runtime; the real keyboard
    and mouse are ignored, so tests never touch the user's desktop) + `GOW2_SCREENSHOTS` (guest output via Xenia's
    presenter, stb PNG); the game starts minimized. Tests in `tests/`.
- Lesson: an earlier scripted run read the real keyboard (Down() OR'd GetAsyncKeyState); the user's typing paused
  the game. Scripted mode is now fully isolated.

## 2026-10-10 — Character shading and shadows
- **Black Shading Fix on by default** (`black_shading_fix`): mid-asm hook at 82806A44 skips the store of
  GUseTilingMode (stays FALSE) with r11 = 0, like Xenia game-patches. The dark, blocky shading on characters
  (Dom's face, Carmine's arms and armor in the Choose Path shot) is gone; Xenia's compatibility tracker reports the
  same artifact. `xenos_sample_positions` = true (Xenia cvar, MSAA at real Xenos sample positions).
- **Shadow resolution** (`shadow_resolution`, default 1280; 0 = the game's 800): hooks after the
  MaxShadowResolution loads at 82741300 / 82741884. Capped at 1536: the depth pass renders into the emulated
  10 MB EDRAM. A/B at 800 vs 1280: no artifacts, 60 fps held.
- Carmine's arm: checked my VMX instructions (vsel128 operand form, vpkd3d128 FLOAT16_2 lane order vs Xenia,
  vmaddcfp128 = vA*vD+vB, vcfpuxws128 saturation, vupkd3d128 types used). All agree with Xenia. The per-frame
  function 825982F8 (3 calls/frame) is not his skinning: skipping it changed nothing.
  The physical-alias write-watch idea is ruled out (no MmGetPhysicalAddress on virtual memory). Still open: the
  close-up "tube" upper arm in the opening cutscene, unless it is how the model looks.
- Test scripts: tests/carmine_cutscene.txt, tests/carmine_drop_sir.txt (new campaign -> Choose Path -> Skip),
  tests/carmine_tutorial.txt (-> Train).
- **Carmine arm, user screenshot** (tutorial alley, doors "B3816"): his right arm is held rigidly straight out at
  shoulder height, Lancer sideways in one hand, left arm hanging. Mesh intact, so it's the pose, not skinning: it looks
  like an arm IK / aim offset driven to an out-of-reach target. Not frame-rate related (also at 30 fps, user tested).
  Reviewed against the PPC definitions: vrlimi128, vmsum3/4fp128, vpermwi128, vspltw, vperm, lvlx/lvrx,
  stvlx/stvrx, vnmsubfp128/vmaddfp128 (4-operand form with vD repeated), and every op added for GoW2 (bdnzt, frsqrte,
  mulhd, eqv, vnor128, vsel128, vcfpuxws128, vpk*). All OK.
  Next: run the same spot in Xenia Canary (D:/xenia-bin, keyboard_mode = 1 in its config) to see whether it's a recomp
  bug. Its winkey driver polls GetAsyncKeyState, so it can only be driven by real focused input: the user has to run it.
- **User: the arm bug does not happen in Xenia Canary** -> recomp/runtime bug, not shared GPU code.
  Ruled out: all 510 jump tables re-derived from the image (tools/check_switches.py: labels, counts, defaults
  match), XenonRecomp's Sonic-specific shortcuts (bdnzf eq-only: GoW2 only tests eq; mfocrf cr6-only: GoW2 always
  uses mask 2 = cr6), recompiler options (no *_as_local), save/restore helper addresses, rld*/vupk*sb/vcmp*./
  vector shifts, FPU conversions/FPSCR.
  tools/class_vtable.py finds a native class's vtable from its name: USkelControlLimb 8212D668 (base
  SkelControlBase 8212CD20); overrides at +110 +114 +124 +128 (8257D0D8) +12C +130 (8257D4F0).
- **Fused multiply-add** (likely cause): PPC fmadd/fmsub/fnmsub/fnmadd(s) and vmaddfp/vmaddcfp128/vnmsubfp(128) round
  once; XenonRecomp emitted a*b+c with two roundings (and /fp:strict stops clang fusing). Xenia's JIT uses real FMA.
  Near |x| = 1, 1 - x*x can then go slightly negative (acos/asin, two-bone IK when the arm is almost straight), so
  sqrt gives NaN. UE3 resets a NaN bone to the reference pose: arm straight out at shoulder height, as in the
  screenshot. Now std::fma (single forms rounded to float afterwards) and simde_mm_fmadd_ps/fnmadd_ps (FMA is part of
  -march=x86-64-v3); simde x86/fma.h included in ppc_context.h.
- **FIXED: Carmine's arm = truncated memset.** The fused multiply-add change was not it (user retested).
  Repro: tests/continue.txt (title -> Campaign -> Continue); at 85 s his right arm is straight out.
  runtime/anim_debug.cpp: GOW2_ANIM_COUNT=1 counts calls of the anim-class virtuals listed by tools/gen_anim_hooks.py;
  GOW2_SKIP=<hex,...> makes them return early; GOW2_LIMB_DUMP=1 logs SkelControlLimb inputs/outputs, mesh bone and
  name-map tables, and the component's per-bone control-list table.
  - Live per-frame skel-control slots in this UE3 build are +110 / +114 (+128 does not run). SkelControlLimb
    CalculateNewBoneTransforms = 8257C1F0 (r3 control, r4 bone, r5 SkelComp, r6 TArray<FMatrix> out);
    SkelComp +480 SkeletalMesh, +532 SpaceBases, +644 SkelControlIndex (BYTE per bone); mesh +124/+128 RefSkeleton
    (72-byte FMeshBone, parent at +56), +140 NameIndexMap elements (20 bytes), +192/+196 hash/size.
  - Skipping 8257C1F0 fixed his pose. Carmine's mesh has 95 bones (others 93); his SkelControlIndex ended
    "91:3 93:0 94:0" instead of "91:3 93:11": InitSkelControls (825895B8) memsets the table to 255 with memset
    82A93150 and only claims entries that are still 255.
  - memset's byte tail is "stb 0(r6); bdzlr; stb 1(r6); bdzlr; stb 2(r6); blr". XenonAnalyse ended the function at
    the first bdzlr, so lengths with 2 or 3 bytes after the words lost all but the first. Fixed with explicit bounds
    0x82A93150 size 0xA0 (config/GOW2_functions.toml, MANUAL in tools/fix_boundaries.py).
  - Checked for the same problem everywhere: of 49,566 functions, 213 end on a fall-through instruction. 18 are data
    decoded as code; the rest end in calls that never return (C++ throw 82CD54A0 + handler funclets, longjmp
    82A93950, error-then-longjmp 82BAFB70, exit 82A96328, the 0xC0000144 raise in 82AA1A78). memset was the only
    real one.
- Closing the window now ends the process (xe_glue CloseListener -> TerminateProcess). Before, the game kept running
  headless with audio after the window closed.

## 2026-10-10 — Profile settings, shader cache
- **Profile settings persist** (kernel/gow2_profile.cpp, replaces the zero-filling read and the write that threw
  everything away): Xenia's layout (8-byte header, 40-byte X_USER_PROFILE_SETTING, string/binary payload after the
  entries, each at its max size from the id = type<<28 | size<<16 | id). Writes kept per (title, id) in
  %LOCALAPPDATA%\GoW2Recomp\profile_settings.bin (written via .tmp + rename). Unwritten ids read as Xenia's
  defaults (source 1: vibration 3, gamer tier 2, else 0/empty). Insufficient buffer -> 0x7A with the size.
- **Shader cache** (`[graphics] shader_cache`, default on): xe_glue calls GraphicsSystem::InitializeShaderStorage
  (root %LOCALAPPDATA%\GoW2Recomp\shader_cache, title 4D53082D, non-blocking) after graphics setup, as Xenia's
  emulator does before launching the title. Earlier shaders/pipelines compile in the background (with
  async_shader_compilation draws wait for them), and new ones are appended.
- Verified: run 1 stored 311 shaders + 307 pipelines; run 2 "Loaded 311 shaders", "307 created, 0 ms". The two fps
  dips after Continue (49-51 fps for ~20 s) happen in both runs: they are the checkpoint load and streaming, not
  shaders.
- Verified profile round trip: GoW2 keeps its options in XPROFILE_TITLE_SPECIFIC1 (63E83FFF, 610 bytes; 63E83FFE and
  63E83FFD are written empty). tests/options_toggle.txt sets Game Settings > Fading HUD to Off; after a restart
  (tests/options_probe.txt) it is still Off. The user's file was backed up and restored around the test.
- Verified game speed at 120 fps: in tests/continue.txt every subtitle comes at the same second as at 60 fps
  (85/90/100 s), 120.0 fps in gameplay.

## 2026-10-10 — Gameplay test, instruction tests, NtDuplicateObject
- **Scripted gameplay test** (tests/combat.txt, after Continue): walk with the squad, look, aim (LT), fire (RT), reload
  (RB), cover behind the dumpster (A), blind fire, Y, roadie run. All work (the ammo counter drops), 60 fps, audio
  187.5 frames/s, no crash. Only warning: RtlImageXexHeaderField 00020401 (optional header field, harmless).
- **Instruction tests** (tests/instr_tests.cpp, tools/instr_tests.cmd; tools/recomp.sh runs them after every
  regeneration): tools/gen_instr_tests.py pulls the recompiler's real output for the first use of each op the port
  added or changed (bdnzt eqv mulhd frsqrte vnor128 vsel128 vpkuhus128 vpkuwus128 vpkswss128 vcfpuxws128
  vpkd3d128-float16_2 fmadd fmadds fnmsubs fmsubs vmaddfp128 vnmsubfp128 vmaddcfp128) plus the whole recompiled
  memset. Results are checked against the PPC definitions, in PPC element order. The FMA cases use inputs where
  fused and unfused differ. memset is checked at every length 0..64 and every alignment, with guard bytes.
  27 checks pass. A copy of memset truncated the old way fails at length 2, so the test catches the Carmine bug.
  Notes: tests must start MXCSR at 0x1F80 (a zero csr unmasks FP exceptions, 0xC000008F), and operands may share a
  register (fmsubs f31,f13,f27,f27).
- **NtDuplicateObject** was an Unleashed no-op stub (the game's output handle was never written; GoW2 calls it
  once at startup). Now it returns the same handle with a reference count (one handle per object in this kernel):
  NtClose drops a duplicate's reference before it destroys anything. -2 becomes the current thread's real handle
  (GuestThread::CurrentHandle, a thread_local set in GuestThreadFunc; the main thread has none and keeps -2).
  DUPLICATE_CLOSE_SOURCE is handled.
- Remaining stubs the game calls: ObDereferenceObject, KeEnableFpuExceptions, ExRegisterTitleTerminateNotification
  (all fine as no-ops). Controllers: XInput pads with rumble were already supported (hid.cpp, XamInputSetState).

## 2026-10-10 — Performance, display options, one-step setup
- **Performance** (RTX 5070 Ti, i7-13700K, 2560x1440 at 154 Hz; tests/continue.txt with frame_rate 120): scale 1 and 2
  hold 120 fps (scale 2 dips to 116 while loading); scale 3 gives 54-67 fps with the GPU at 99%. Xenia's EDRAM
  emulation (host render targets) costs grow with scale^2. Capped at 154, scale 2 gives 114-143: about 140 is the
  limit here, so no frame_rate = auto.
- **Display options** ([display]):
  - `resolution_scale = auto` (new default): lround(monitor height / 720), clamped 1..3, so 1440p and 1080p give 2
    and 4K gives 3. The user's ini went from 1 to auto (it was a stretched 720p picture); old copy in
    build/gow2.ini.before_auto.
  - `upscaling = fsr|cas|bilinear` (default fsr) and `sharpness`: Xenia presenter GuestOutputPaintConfig, set from
    the UI thread after SetPresenter.
  - `antialiasing = none|fxaa|fxaa_extreme`: CommandProcessor::SetDesiredSwapPostEffect.
- **One-step setup** (tools/setup.ps1 -Iso <iso>; README.md for players):
  - Checks prerequisites (python + pycryptodome, Git bash, and tools/env.cmd, which finds MSVC with vswhere and
    adds clang-cl/ninja from scoop).
  - Extracts the ISO (xdvdfs.py extract-all, skips existing files) and checks default.xex's SHA-256
    (tools/default_xex.sha256: the config is made for this exact XEX); decrypts default.bin.
  - setup_xenonrecomp.sh: pinned commit xenonrecomp-commit.txt + xenonrecomp-gow2.patch. tools/XenonRecomp is
    git-ignored, so the port's recompiler changes are kept in that patch. setup_xenia.sh is the same for Xenia.
  - build_xenon / build_xenia, then recomp.sh, then build.cmd, then the desktop shortcut.
  All scripts use the repository's own folder; GOW2_XENIA / GOW2_GAME default to ../xenia and ../gow2-decomp.
  CMake takes GOW2_XENIA. gow2.exe without arguments uses %GOW2_GAME%\disc, else <exe>\..\..\..\gow2-decomp\disc.
  Ran end to end here: zero files re-extracted, patches already applied, 0 changed files, 27 tests passed, build OK.
- Pitfalls found on the way:
  - XenonRecomp joins file_path to the config folder as text, so an absolute path crashed it (exit 139).
    recomp.sh now reaches the game folder through the git-ignored junction game_link/ (works across drives).
  - Worse: the old recomp.sh then synced the empty output and deleted all of ppc/. It now replaces ppc/ only after a
    successful run that wrote ppc_func_mapping.cpp.
  - PowerShell Test-Path reads [RF][MEX] in the ISO name as a wildcard: use -LiteralPath.
  - Git bash turns mklink's /J into a path: use New-Item -ItemType Junction.
  - setup_xenia.sh rewrote version.h on every run, rebuilding part of Xenia: it now writes only on change.
- .gitignore: shots/ (game screenshots), save_backups/, game_link/, run*.txt, recomp*.log.

## 2026-10-10 — Keyboard/mouse button prompts (follow the input device)
- **How GoW2 draws prompts**: UE3 markup `<Fonts:Warfare_HUD.Xbox360_18pt>A<Fonts:/>` (GearGameUI.ControllerFont.*) and
  `<StringAliasBindings:GBA_*>` both end up as characters of the font Warfare_HUD.Xbox360_18pt. A B X Y = face
  buttons, L/C = sticks, Q = LS down, j/h = stick clicks, ] ^ = LT/RT, _ ` = LB/RB, > < = Start/Back, l m q s o = d-pad,
  7 3 1 5 = arrows, = and 9 = up-down / left-right ([ControllerFont] in Coalesced_int).
- The font: 2 texture pages. Page 0 is 256x256 DXT5 (Xenos format 20, tiled, k_8in16), 104 glyphs (ASCII). Page 1
  holds 88 accented letters (0xA8-0xF7) and is left alone. FFontCharacter table = TArray of 256 x 24 bytes
  {StartU, StartV, USize, VSize, TextureIndex (byte at +16), VerticalOffset}, big-endian, indexed by character code.
  This run: TArray header at A11FD824 -> data 407E4800 (the UFont object is in physical memory);
  Textures TArray at A11FD830. Page-0 content hash (FNV-1a 64) = bfbe34a9abae9459. Every glyph is 26 texels tall.
- Tools: GOW2_TEXTURE_DUMP=1 (xe_glue OnTextureLoad dumps each new 2D texture's raw guest data to build/texdump);
  tools/texdecode.py (Xenos untiling from Xenia's texture_address.h + DXT1/3/5; run with python -P because
  tools/dis.py shadows the stdlib dis module); GOW2_MEM_DUMP=<s> (crash.cpp: committed guest memory
  0x40000000-0xBFFFFFFF to memdump/).
- **Gears of War: Reloaded** (Steam, F:/SteamLibrary/...; UE3 package version 835 / licensee 76, compression 0x20 =
  LZ4 chunks): tools/upk.py reads it. The licensee's export entries vary in length, so boundaries come from
  contiguous serial data, with a gap-tolerant fallback; still fails on Startup/GOWMenus. Startup_LOC_INT.upk has
  WarfareFonts.Fonts.WarfareFonts_Keycaps20: pages Texture2D_3/_4 (2048x2048 DXT5, linear) and _5 (2048x256).
  Character keycaps: slot = code - 31, 13 per row, 136x136 cells at 148/160 pitch. Named keys (Space, Tab, Esc,
  Shift, BkSp, arrows...) are on page 2 (positions in make_prompts.py NAMED). Keycaps20new is an ObjectRedirector.
  Reloaded has no mouse glyphs, so those are drawn in the same keycap style. EasyAntiCheat: its files are only
  read offline, the game is never run or touched.
- tools/make_prompts.py -> build/prompts/kbm_glyphs.{png,rgba,txt,json}: 26 glyphs for the bindings in hid.cpp:
  A=Space, B=F, X=E, Y=Q, LB=Tab, RB=R, LT=right mouse, RT=left mouse, LS=WASD cluster, LS down=S, RS=mouse,
  stick clicks C / middle mouse, d-pad and arrows = arrow keys, Start=Esc, Back=BkSp. Without Reloaded every icon is
  drawn (same style). Never committed (Reloaded artwork).
- **runtime/ui/prompts.cpp**: OnTextureLoad (xe_glue -> host.onTextureLoad, GPU thread, before upload) recognizes page
  0 by hash. A worker finds the table by its stock 'A'/'B' records (checked with 'Y'), then repacks page 0 for
  keyboard/mouse: kept glyphs (deduplicated) + icons, first-fit decreasing into rows. 9 rows of 26 fit with 1-texel
  gaps (a greedy shelf did not: 2,303 of 2,304 texels of row width); Space limited to 48, Tab/BkSp to 36. Then DXT5
  encode, tiling and byte swap are done in C++. Switching writes the 256 records and the 64 KB page into guest memory;
  xe_glue::WritePhysical invalidates it in Xenia's SharedMemory (gow2_shared_memory() accessor added in the xenia
  patch), so the texture re-uploads. hid::ActiveDevice(): keys, mouse buttons or mouse movement -> keyboard;
  pad buttons, sticks past 30% or triggers -> controller. [input] prompts = auto|keyboard|controller.
- Verified (screenshots): main menu Space / Tab "What's Up" / R "Options" / E "Downloadable Content"; options footer
  E Defaults / Q Reset Tutorials / F Back; tutorial "To move, push [WASD] in any direction". Switching
  keyboard -> controller -> keyboard works (tests/prompt_switch.txt; new script command "t pad <buttons hex> ms").
- Next: mouse in menus. The UE3 UI config already has PC mouse aliases (UIState_Active + LeftMouseButton = Clicked on
  buttons, lists, sliders; Escape closes scenes), so the engine's mouse handling may only need a cursor position
  and mouse key events fed in.

## 2026-10-10 — UE3 reflection, mouse in menus
- **UE3 reflection** (tools/ue3dump.py over GOW2_MEM_DUMP dumps; runtime/ue3/reflection.cpp live):
  - FNameEntry {u32 Index = n*2, u32 HashNext (pool offset), char Name[]}, packed in a pool that starts with None.
    GNames is an array of entry pointers: 0x03AC0000 or 0x03AA0000 (varies by run; found from the entry
    of name 1968 "GameViewportClient").
  - GObjObjects at 0x01300000 (~133k slots), found from Default__GameViewportClient (name 1969):
    UObject +4 slot, +40 Outer, +44 Name, +52 Class. UStruct +60 super, +76 children, +80 size; UField +64 next;
    UProperty +68 dim, +72 element size, +100 offset; UBoolProperty +132 mask.
  - The live game uses low guest addresses for these (the dump needs 0x00010000+).
  - Guarded reads: thread_local g_probingGuestMemory lets crash.cpp's vectored handler pass faults to the
    __except. Every read also checks VirtualQuery and the GPU register range: touching guard pages and MMIO from
    another thread froze the boot.
- Classes / offsets used (by name, through reflection): GearGameUISceneClient (GameUISceneClient: MousePosition
  +0x48, ActiveControl +0x50, ActiveScenes +0x100); UIScreenObject Children +0x60, StateStack +0x88, flags +0x5C
  (bHidden 0x80000000, bNeverFocus 0x20000000); UIObject RenderBounds +0x244 (1280x720 canvas), Owner +0x1B8;
  UIScene flags +0x334 (bDisplayCursor 0x40000, bPauseGameWhileActive 0x8000); UICalloutButton InputAliasTag +0x484;
  UIDataStore_InputAlias.InputAliases +0x78 (80-byte entries: alias name +0, first key name +8; 70 map to buttons).
  The live input path was not needed: GoW2 menus take controller buttons; UE3's own PC mouse path is never fed.
- **runtime/ui/mouse_menu.cpp**: a menu is up when the topmost ActiveScene has a focused leaf (the HUD and toasts have
  none). Then hid::SetMenuMode releases the mouse and shows the cursor. Hover = d-pad steps (160 ms apart) toward
  the hovered clickable widget until it is related (same/owner/child) to the focused one; only for 800 ms after the
  mouse moved, so keys and pad are not fought. Left click = A, or a callout's alias button, or for option lists
  right / left (on the left arrow). Right click = B; wheel = d-pad up/down. Presses go through
  hid::InjectButtons(120 ms), which does not count as controller use. Leaving a menu recaptures the mouse at once;
  Esc is B in menus and Start in play.
- Verified (scripted cursor: "t cursor x y", "t click L|R", "t wheel n"): main menu hover moves focus (Horde, Co-op);
  the Options callout opens Options; right click goes back; GAME SETTINGS by click; Fading HUD value On->Off, left
  arrow Off->On; Subtitles by hover + click; pause menu: right click and Esc resume, clicking RESUME resumes; in play
  the mouse stays captured and fires. Found bugs: a value click on the exact middle went left (now: anywhere but the
  left arrow = next), and hover fought the row/list/arrow nesting (now: related = reached).
- **Hover jumps** (user request: no cycling through entries): UFunction +172 = native function pointer;
  execSetFocus 8242F198 calls the C++ UIScreenObject::SetFocus through vtable +344 (r3 widget, r4 sender, r5 player
  index). The worker queues the hovered widget (an option-list arrow means its list). hid::GetState (the game
  thread's controller poll, once a frame) runs mouse_menu::OnGameThread, which calls it with GuestToHostFunction
  after checking that the vtable and function pointer are still sane. Clicking an unfocused entry = focus, then A.
  Verified: Solo Campaign -> Credits in one step, Co-op, a click on Horde opens it, option rows still OK.
- Start's prompt is now the Enter keycap (Esc is B in menus, so "Press Start: Esc" was wrong; Enter is Start
  everywhere).

## 2026-10-10 — Skip the intro movies
- `gow2.exe -nointro` (also --nointro, -skipintro, --skip-intro; options may come anywhere, the first non-option
  argument is still the disc folder) or `[display] skip_intro_movies = true`.
- The startup movies are [FullScreenMovie] StartupMovies = EpicLogo, MGSLogo, LoadingMovie (Bink 1 "BIKi" files in
  GearGame/Movies). Reporting EpicLogo.bik as missing crashed the movie player (null deref in sub_827F4CB0). Instead
  the file opens normally and gow2_files patches the Bink header's frame counts (u32 at +8 and +16) to 1 in the
  read (NtReadFile and the scatter read), so each logo ends at once. LoadingMovie and the cinematics are untouched.
  Verified: the title screen is up at 15 s (without the option the MGS logo is still playing then); no crash.

## 2026-10-10 - Launcher and PC options
- `build/gow2_launcher.exe` (launcher/launcher.cpp, Win32 + common controls, per-monitor DPI): five tabs (Display,
  Graphics, Effects, Controls, Audio & Game) over gow2.ini. Edits the ini line by line (comments, order and unknown
  keys kept; missing keys appended to their section). Play saves and starts gow2.exe from its folder with the
  launcher's own arguments forwarded. Test mode: GOW2_LAUNCHER_SHOT=<folder> draws every page off-screen to
  page<N>.bmp without taking focus, then saves (GOW2_LAUNCHER_INI=<file> picks the ini).
- New settings: display.window_width/height (windowed size; checked: 1600x900 client), display.frame_rate = auto
  (EnumDisplaySettings refresh rate), graphics.motion_blur / depth_of_field / bloom / lens_flares / distortion /
  fog_volumes / self_shadowing.
- Effects go through the engine's live GSystemSettings at 0x8313751C (142 code references; ten more copies follow
  at a 0x23C stride, presumably the per-detail-level presets). The field offsets come from FSystemSettings' name
  table in sub_82351488: MotionBlur +0x2C, DepthOfField +0x30, AmbientOcclusion +0x34, Bloom +0x38,
  UseHighQualityBloom +0x3C, Distortion +0x40, LensFlares +0x48, FogVolumes +0x4C, UseVsync +0x1A4, ResX/ResY
  +0x1B0/+0x1B4, bEnableForegroundSelfShadowing +0x1E8. ui/system_settings.cpp writes them once ResX/ResY hold
  1280x720 (the config is loaded) and then every second.
- Verified: bloom off visibly removes the title screen's red glow (red channel mean 66.5 -> 39.2). In the Act 1
  street scene, bloom and motion blur off change brightness by less than 0.3/255: little bloom there and no motion
  in a still frame. The renderer gates the post-process chain in sub_824ECEE0 on DepthOfField || Bloom.

## 2026-10-11 - Recompiler fixes from the Gears 3 port
- XenonRecomp is shared with D:\gow3-port, whose tools/patch_xenonrecomp_gow3.py adds instructions Gears 3 needs
  (update-form loads/stores, addc/addme/subfze, bdzf, halfword VMX, vcmpbfp(.), vupkd3d128 types 3 and 5, blrl). It
  also removes the PowerPC 403/405 from the disassembler's default dialect: their mulchw/macchw share opcode 4 with
  VMX128.
- Effect on Gears 2: before the vcmpbfp/vupkd3d128/blrl round, regenerated output was byte-identical. After it, only
  the former trap sites changed: two vupkd3d128 type 5 (FLOAT16_4), one type 3 (FLOAT16_2), one blrl. Gears 2 now
  compiles with **no __builtin_debugtrap left**. Instruction tests 27/27; continue-campaign smoke test fine (60 fps).

## Plan
Done: runtime fork, data imports, file system (extracted disc folder), memory model, sync/XAM/XMA basics,
**GPU rendering (Xenia D3D12)**, **input**, networking (offline), menus -> campaign, **saves**, **audio**, settings,
**60/120 fps**, **native mouse look**.
1. **Next**: hand play-test (combat, cover, weapons, cutscenes with audio; listen for crackle and sync; game speed
   and physics at 120 fps; mouse feel and zoom sensitivity);
   profile settings persistence (XamUserRead/WriteProfileSettings: options, sensitivity, invert);
   Xam*UI dialogs (virtual keyboard, message boxes) if the game hits them.
2. GPU follow-ups: shader/pipeline cache on disk (Xenia's shader storage, title 4D53082D) to remove first-use
   hitches; window close -> clean shutdown; any game-specific GPU issues in later chapters.
3. Done: tools/setup.ps1 + README.md (see 2026-10-10). Still to try: a setup on a clean machine (only tested here
   with everything already in place).
4. Remaining imports as the game needs them: timer APCs, more XMsg/Xam*UI, offline XNet details.
5. Done: instruction tests (tests/instr_tests.cpp). Next: performance profiling and packaging (one-click, the
   player supplies the ISO; no game files shipped).

## 2026-10-11: Project moved to D:\Gears of War PC ports
- With the Gears 3 port, Xenia and both game folders in one directory. game_link was recreated, build\ configured
  again (CMake caches cannot move), helper scripts made relative to the repository, and playtest.ps1/shot.ps1 quote
  the disc path (it has spaces now). See the Gears 3 MODLOG for details. gow2.exe rebuilt from the new location.

## 2026-10-14: gears2-pc, rebuilt on the Gears 3 port's foundation

- The Gears 3 port (gears3-pc) grew out of this one and went much further: a setup wizard, a launcher with mods and
  multiplayer, System Link over the internet with servers, profiles, in-game notices, mods, test tooling. gears2-pc
  starts from that tree, with Gears 2's game-specific parts put back: config/GOW2*.toml (function bounds, jump
  tables, mid-asm hooks), kernel/gow2_patches.cpp (frame-rate unlock, anisotropy, texture pool, black shading fix,
  shadow resolution), ui/mouse_menu.cpp (the menus are UE3 UI scenes, not Gears 3's Scaleform), ui/system_settings.cpp
  (effects written into GSystemSettings), ui/prompts.cpp (the Warfare_HUD font page), mouse look at 826404E0, and
  the reflection layout (UObject slot +4, UStruct super/children/next 60/76/64, property offset +100, bool mask
  +132, "GameViewportClient" = name 1968). Gears 3-only modules were left out (Scaleform menus, new campaign
  chapters, navmesh generation, play-area kill). Gears 3's settings audit had removed the Gears 2 keys
  (texture_streaming_fix, black_shading_fix, shadow_resolution, bloom, distortion, fog_volumes, self_shadowing):
  they are back; ambient_occlusion (Gears 3 only) is gone.
- Same Xenia commit and patch (title id 4D53082D for the shader storage), same XenonRecomp patch: the recompiled
  output equals the old port's except one file. Instruction tests 27/27.
- Found while doing this: gears3-pc's tools/default_xex.sha256 held Gears 2's hash (copied from here when it was
  founded), so its setup turned every Gears 3 disc away. Fixed there.
- First run (tests/new_campaign.txt): title, main menu, Select Difficulty, the opening narration and the first
  level loading, checkpoint saves, 60 fps, audio. The keyboard prompt page does not fit yet: the drawn keycaps are
  sized for Gears 3's font.

## 2026-10-15: every chapter, Horde, Training Grounds; System Link Versus

- Campaign coverage (tools/chapter_test.py, chapters 0-28 = Xe-GearGame.ini ChapterLevelNames, each booted with
  `GearGame_P?game=geargamecontent.GearGameSP?chapter=N`): all 29 load their maps and run 150 s without a crash,
  60 fps after loading (30 during the Bink intro movies of chapters 2 and 25). Chapter 24 (Escape, the Reaver) waits
  on a tutorial box until A or Space: the test now presses A too. Gears 2 skips a cinematic with Back; Start only
  pauses it.
- Crash fixed on the way (chapters 2-3, SP_Assault's intro cinematic): `eh vector constructor iterator` 82CD5BF8
  called the constructor 82ADBEF0 through a pointer, and the recompiled code had no function there: a 5-instruction
  leaf with no .pdata entry, right after a call to the C++ throw 82CD54A0 (which never returns), so the analyser kept
  it inside 82ADBED8. A scan of every gap between .pdata functions found it the only code gap (21 others are data
  words after throw calls); the same scan finds none in Gears 3.
- Horde (tests/horde.txt): main menu > Horde > Play > Avalanche > Start: Horde on Avalanche, wave 1. Training Grounds
  (tests/training.txt): Lesson 1, a 3-on-3 Warzone on Day One with bots.
- System Link (tests/mp_host.txt, mp_join.txt, tools/mp_test.ps1 lan): Multiplayer > Match Type: System Link opens
  the System Link Browser; Create Party (Y); the joiner's Refresh (X) lists "HostTest", A joins; the host picks
  Play, Start (map: Avalanche), Start: both play Warzone, one COG and one Locust. XSessionCreate host (flags 21) and
  joiner (flags 20) through x360_session.cpp, unchanged from Gears 3.
- Scripting the lobby: the menu mouse's hover moves a list's selection when the cursor is left over it, so the
  scripts park the cursor in a corner before using the pad; presses of 80 ms (200 ms ones sometimes moved twice).
- Prompts in the lobby: the lobby copies the button font's texture page while it holds the keyboard picture, and that
  copy was never switched back (garbled controller prompts). Copies with the keyboard picture are now tracked like
  stock ones, and a switch rewrites every copy that still holds one of the two pictures.
- Gears 2 and Gears 3 games never mix: each port's protocol has its own magic, and a hub answers the other game's
  Hello or launcher query with a Reject in that game's magic ("this is for Gears of War 3, not Gears of War 2").

## 2026-10-15: campaign co-op over System Link (the tick-rate cap)

- Versus, Horde and the party all worked over System Link, but in campaign co-op the joiner sat in PlayerWaiting with
  a fixed camera, and its world held no pawns at all (pos_log), while the host had Marcus, AI Dom and Carmine.
- Traffic (GOW2_NET_STATS=1: datagrams and bytes per virtual port every 10 s): for ~110 s after loading, ~1000
  datagrams/s each way (46 B from the host, 16 B back), then a normal idle ~10/s. A Versus match plays at ~30/s.
- Decoding the packets (GOW2_NET_DUMP=N logs every Nth payload; UE3 packets after the 2-byte VDP length: 14-bit
  packet id, acks, bunches with control/open/close/reliable bits, a 10-bit channel, 10-bit sequence, 3-bit type and a
  SerializeInt(4096) length, MaxPacket being 512) and naming the actor-channel fields (GOW2_LOG_NETFIELDS=GearPC_SP:
  UClass::NetFields at +108, after ClassReps at +FC; 286 fields through Actor, Controller, PlayerController, GearPC):
  the host's ClientSetViewTarget (field 85) and the joiner's ServerVerifyViewTarget (86) answered each other, the
  usual UE3 exchange while a client cannot resolve its view target: 110,000 times. AskForPawn / GivePawn ran, but the
  pawn never replicated: the ping-pong held the connection at its 10 KB/s LAN rate, so the server never had room to
  replicate actors.
- The rate came from the frame-rate unlock: its hook made the engine's max tick rate getter return 0.0 ("no cap", as
  Xenia's game patch does). Rendering is then paced by vblanks, but whenever no frame is presented (loading) the
  engine ticks, and its net driver with it, as fast as the CPU allows. With GOW2_FRAME_RATE=30 (no unlock) the
  ping-pong ran at ~60/s for 20 s and the joiner got its pawns. The hook now returns the player's frame rate as the
  cap (registers r3 and f1): co-op at 60 fps works: the joiner plays Dom (GearPawn_COGDom_1) beside the host's Marcus
  from the opening through Skip Training into the hospital (tests/mp_host_coop.txt, mp_join_coop.txt).
- Also: game sockets get a 4 MB receive buffer (a loading title does not read for seconds), and a datagram larger
  than the protocol's payload is logged instead of dropped silently. prop_log finds Gears 2's StateFrame (+24) and
  states anywhere in memory.
