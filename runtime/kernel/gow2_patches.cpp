// GoW2 game patches, as mid-asm hooks placed by the recompiler (config/GOW2.toml [[midasm_hook]]).
//
// Frame rate unlock (from Xenia's game-patches "Unlock FPS" by illusion & boma, same addresses in this XEX):
//  - 824A2E94: the engine's max tick rate getter (824A2E78) loads its 30 fps cap from this+1284; skipped, it returns
//    the 0.0 already in f1, meaning "no cap"
//  - 8298CFB0: the D3D maps the title's presentation interval TWO to hardware value 2 (present every 2nd vblank);
//    1 presents every vblank. The vblank rate itself follows gow2.ini frame_rate (xe_glue).
// UE3 times the game with the performance counter, so game speed does not change with the frame rate.
#include <stdafx.h>
#include <cpu/ppc_context.h>
#include <user/settings.h>

namespace
{
    bool Unlocked() { return settings::Get().frameRate > 30; }
}

bool GoW2MaxTickRateHook(PPCRegister& r3)
{
    return Unlocked();   // true: skip the load of the cap
}

void GoW2PresentIntervalHook(PPCRegister& r11)
{
    if (Unlocked()) r11.u64 = 1;
}

// 16x anisotropic filtering: MaxAnisotropy (loaded at 8280E7FC) forced to 16
void GoW2AnisotropyHook(PPCRegister& r5)
{
    if (settings::Get().anisotropic16x) r5.u64 = 16;
}

// texture streaming fix: a 196 MB pool (8280D534) and RHIGetTextureMemoryStats (8280DDC8) returning false, which
// locks the streaming fudge factor at 1.0: textures stream in sooner, less pop-in (helps most with resolution scaling)
void GoW2TexturePoolHook(PPCRegister& r4)
{
    if (settings::Get().textureStreamingFix) r4.u64 = 196;
}

extern "C" PPC_FUNC(__imp__sub_8280DDC8);
PPC_FUNC(sub_8280DDC8)
{
    if (settings::Get().textureStreamingFix) { ctx.r3.u64 = 0; return; }
    __imp__sub_8280DDC8(ctx, base);
}

// Black Shading Fix: skip the store of GUseTilingMode (stw r11,11940(r6) at 82806A44) so it stays FALSE, with r11 = 0
// like the original patch's li r11,0. Without tiling (and its MSAA resolves) the dark/blocky character shading goes.
bool GoW2TilingModeHook(PPCRegister& r11)
{
    if (!settings::Get().blackShadingFix) return false;
    r11.u64 = 0;
    return true;
}

// shadow map resolution cap: 0 keeps the game's (800). The shadow depth pass renders into the emulated 10 MB EDRAM,
// so the cap stays well under 1600 (1600^2 x 4 bytes = 10 MB)
void GoW2ShadowResolutionHook(PPCRegister& r11)
{
    if (const int res = settings::Get().shadowResolution) r11.u64 = uint32_t(res);
}
