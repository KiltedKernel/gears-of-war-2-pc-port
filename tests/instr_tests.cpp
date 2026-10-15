// Tests of the recompiler's output for the instructions the GoW2 port added or changed, and of the recompiled memset
// (whose truncated byte tail broke Carmine's IK). The code under test is the real generated code, extracted by
// tools/gen_instr_tests.py; expectations come from the PowerPC definitions, in PPC element order (the port stores
// vectors reversed: PPC word e is host u32[3 - e], halfword k is u16[7 - k], byte j is u8[15 - j]).
// Build and run: tools/instr_tests.cmd
#include <ppc_context.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../build/instr_tests/snippets.inc"

namespace
{
    int g_failed = 0, g_passed = 0;

    void Check(bool ok, const char* op, const std::string& what)
    {
        if (ok) { g_passed++; return; }
        g_failed++;
        printf("FAIL %-12s %s\n", op, what.c_str());
    }

    // operand i of "v63,v1,3,1,3" as a register index (the letter is dropped) or a number
    int Operand(const char* operands, int i)
    {
        std::string s(operands);
        size_t start = 0;
        for (int k = 0; k < i; k++) start = s.find(',', start) + 1;
        std::string tok = s.substr(start, s.find(',', start) - start);
        while (!tok.empty() && (tok[0] == 'r' || tok[0] == 'f' || tok[0] == 'v')) tok.erase(0, 1);
        return std::stoi(tok, nullptr, 0);
    }

    uint32_t& W(PPCVRegister& v, int e) { return v.u32[3 - e]; }
    float& F(PPCVRegister& v, int e) { return v.f32[3 - e]; }
    uint16_t& H(PPCVRegister& v, int k) { return v.u16[7 - k]; }
    int16_t SH(PPCVRegister& v, int k) { return int16_t(v.u16[7 - k]); }
    uint8_t& B(PPCVRegister& v, int j) { return v.u8[15 - j]; }

    // a fresh context as the runtime makes one: MXCSR at its default (exceptions masked), not 0
    void ResetCtx(PPCContext& ctx)
    {
        ctx = {};
        ctx.fpscr.csr = 0x1F80;
        ctx.fpscr.setcsr(0x1F80);
    }

    std::string Hex(uint64_t x) { char b[32]; snprintf(b, sizeof b, "%016llX", (unsigned long long)x); return b; }
}

int main()
{
    alignas(64) static uint8_t memory[0x2000];
    uint8_t* base = memory;
    PPCContext ctx{};

    // bdnzt eq,target: --CTR; branch if CTR != 0 and CR0.eq
    {
        ResetCtx(ctx); ctx.ctr.u64 = 2; ctx.cr0.eq = 1;
        Check(T_bdnzt(ctx, base) && ctx.ctr.u64 == 1, "bdnzt", "ctr 2, eq: taken, ctr 1");
        ctx.ctr.u64 = 1; ctx.cr0.eq = 1;
        Check(!T_bdnzt(ctx, base) && ctx.ctr.u64 == 0, "bdnzt", "ctr 1, eq: not taken (ctr reaches 0)");
        ctx.ctr.u64 = 5; ctx.cr0.eq = 0;
        Check(!T_bdnzt(ctx, base) && ctx.ctr.u64 == 4, "bdnzt", "ctr 5, not eq: not taken");
    }
    // eqv rD,rA,rB: ~(A ^ B)
    {
        ResetCtx(ctx); const int d = Operand(kOperands_eqv, 0), a = Operand(kOperands_eqv, 1), b = Operand(kOperands_eqv, 2);
        Reg_r(ctx, a).u64 = 0x00000000F0F0F0F0ull; Reg_r(ctx, b).u64 = 0x00000000FF00FF00ull;
        T_eqv(ctx, base);
        Check(Reg_r(ctx, d).u64 == 0xFFFFFFFFF00FF00Full, "eqv", Hex(Reg_r(ctx, d).u64));
    }
    // mulhd rD,rA,rB: high 64 bits of the signed 128-bit product
    {
        const int d = Operand(kOperands_mulhd, 0), a = Operand(kOperands_mulhd, 1), b = Operand(kOperands_mulhd, 2);
        struct { int64_t a, b, hi; } cases[] = { { -3, 0x4000000000000000ll, -1 }, { 0x7FFFFFFFFFFFFFFFll, 2, 0 },
            { -1, -1, 0 }, { 0x123456789ll, 0x1000000000ll, 0x12 } };
        for (auto& c : cases)
        {
            ResetCtx(ctx); Reg_r(ctx, a).s64 = c.a; Reg_r(ctx, b).s64 = c.b;
            T_mulhd(ctx, base);
            Check(Reg_r(ctx, d).s64 == c.hi, "mulhd", Hex(uint64_t(c.a)) + " * " + Hex(uint64_t(c.b)) + " -> " + Hex(Reg_r(ctx, d).u64));
        }
    }
    // frsqrte fD,fB: 1/sqrt (the hardware gives an estimate; exact is within it)
    {
        ResetCtx(ctx); const int d = Operand(kOperands_frsqrte, 0), s = Operand(kOperands_frsqrte, 1);
        Reg_f(ctx, s).f64 = 4.0; T_frsqrte(ctx, base);
        Check(Reg_f(ctx, d).f64 == 0.5, "frsqrte", "1/sqrt(4)");
        Reg_f(ctx, s).f64 = 0.0; T_frsqrte(ctx, base);
        Check(std::isinf(Reg_f(ctx, d).f64), "frsqrte", "1/sqrt(0) = inf");
    }
    // vnor128 vD,vA,vB: ~(A | B)
    {
        ResetCtx(ctx); const int d = Operand(kOperands_vnor128, 0), a = Operand(kOperands_vnor128, 1), b = Operand(kOperands_vnor128, 2);
        for (int e = 0; e < 4; e++) { W(Reg_v(ctx, a), e) = 0x0F0F0000u + e; W(Reg_v(ctx, b), e) = (a == b) ? 0x0F0F0000u + e : 0x00F00000u; }
        T_vnor128(ctx, base);
        bool ok = true;
        for (int e = 0; e < 4; e++) ok &= W(Reg_v(ctx, d), e) == ~((0x0F0F0000u + e) | (a == b ? 0x0F0F0000u + e : 0x00F00000u));
        Check(ok, "vnor128", "~(A|B) per word");
    }
    // vsel128 vD,vA,vB,vD: D = (A & ~C) | (B & C), C = the old vD
    {
        ResetCtx(ctx); const int d = Operand(kOperands_vsel128, 0), a = Operand(kOperands_vsel128, 1), b = Operand(kOperands_vsel128, 2);
        for (int j = 0; j < 16; j++) { B(Reg_v(ctx, a), j) = 0x11; B(Reg_v(ctx, b), j) = 0x22; B(Reg_v(ctx, d), j) = (j & 1) ? 0xFF : 0x0F; }
        T_vsel128(ctx, base);
        bool ok = true;
        for (int j = 0; j < 16; j++) ok &= B(Reg_v(ctx, d), j) == ((j & 1) ? 0x22 : ((0x11 & 0xF0) | (0x22 & 0x0F)));
        Check(ok, "vsel128", "bitwise select with the destination as mask");
    }
    // vpkuhus128 vD,vA,vB: bytes 0..7 = sat_u8(A.h[0..7]), 8..15 = sat_u8(B.h[0..7]), halfwords unsigned
    {
        ResetCtx(ctx); const int d = Operand(kOperands_vpkuhus128, 0), a = Operand(kOperands_vpkuhus128, 1), b = Operand(kOperands_vpkuhus128, 2);
        const uint16_t av[8] = { 0, 1, 254, 255, 256, 0x7FFF, 0x8000, 0xFFFF }, bv[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
        for (int k = 0; k < 8; k++) { H(Reg_v(ctx, a), k) = av[k]; H(Reg_v(ctx, b), k) = bv[k]; }
        T_vpkuhus128(ctx, base);
        bool ok = true;
        for (int k = 0; k < 8; k++) ok &= B(Reg_v(ctx, d), k) == std::min<uint16_t>(av[k], 255) && B(Reg_v(ctx, d), 8 + k) == bv[k];
        Check(ok, "vpkuhus128", "unsigned saturation, A in the high bytes");
    }
    // vpkuwus128 vD,vA,vB: halfwords 0..3 = sat_u16(A.w[0..3]), 4..7 = sat_u16(B.w[0..3])
    {
        ResetCtx(ctx); const int d = Operand(kOperands_vpkuwus128, 0), a = Operand(kOperands_vpkuwus128, 1), b = Operand(kOperands_vpkuwus128, 2);
        const uint32_t av[4] = { 0, 65535, 65536, 0xFFFFFFFFu }, bv[4] = { 1, 2, 0x80000000u, 1234 };
        for (int e = 0; e < 4; e++) { W(Reg_v(ctx, a), e) = av[e]; W(Reg_v(ctx, b), e) = bv[e]; }
        T_vpkuwus128(ctx, base);
        bool ok = true;
        for (int e = 0; e < 4; e++) ok &= H(Reg_v(ctx, d), e) == std::min<uint32_t>(av[e], 65535) && H(Reg_v(ctx, d), 4 + e) == std::min<uint32_t>(bv[e], 65535);
        Check(ok, "vpkuwus128", "unsigned saturation");
    }
    // vpkswss128 vD,vA,vB: halfwords = sat_s16 of signed words (operands may all be the same register)
    {
        ResetCtx(ctx); const int d = Operand(kOperands_vpkswss128, 0), a = Operand(kOperands_vpkswss128, 1);
        const int32_t av[4] = { -100000, -32768, 32767, 100000 };
        for (int e = 0; e < 4; e++) W(Reg_v(ctx, a), e) = uint32_t(av[e]);
        T_vpkswss128(ctx, base);
        bool ok = true;
        for (int e = 0; e < 4; e++) ok &= SH(Reg_v(ctx, d), e) == std::max(-32768, std::min(32767, av[e]));
        Check(ok, "vpkswss128", "signed saturation");
    }
    // vcfpuxws128 vD,vB,0: float -> uint32, NaN/negative -> 0, >= 2^32 -> 0xFFFFFFFF, truncating
    {
        ResetCtx(ctx); const int d = Operand(kOperands_vcfpuxws128, 0), s = Operand(kOperands_vcfpuxws128, 1);
        const float in[4] = { -1.0f, 1.75f, 4.0e9f, 5.0e9f };
        const uint32_t want[4] = { 0, 1, 4000000000u, 0xFFFFFFFFu };
        for (int e = 0; e < 4; e++) F(Reg_v(ctx, s), e) = in[e];
        T_vcfpuxws128(ctx, base);
        bool ok = true;
        for (int e = 0; e < 4; e++) ok &= W(Reg_v(ctx, d), e) == want[e];
        Check(ok, "vcfpuxws128", "saturating unsigned conversion");
        F(Reg_v(ctx, s), 0) = NAN; T_vcfpuxws128(ctx, base);
        Check(W(Reg_v(ctx, d), 0) == 0, "vcfpuxws128", "NaN -> 0");
    }
    // vpkd3d128 vD,vS,3,1,shift: (half(x) << 16) | half(y) into one word, the others kept
    {
        ResetCtx(ctx); const int d = Operand(kOperands_vpkd3d128, 0), s = Operand(kOperands_vpkd3d128, 1), shift = Operand(kOperands_vpkd3d128, 4);
        F(Reg_v(ctx, s), 0) = 1.0f; F(Reg_v(ctx, s), 1) = -2.0f; F(Reg_v(ctx, s), 2) = 9.0f; F(Reg_v(ctx, s), 3) = 9.0f;
        for (int e = 0; e < 4; e++) W(Reg_v(ctx, d), e) = 0xAAAAAAAAu;
        T_vpkd3d128(ctx, base);
        // with pack 1 (VPACK_32) the packed word is PPC element 3 - shift
        bool ok = W(Reg_v(ctx, d), 3 - shift) == 0x3C00C000u;
        for (int e = 0; e < 4; e++) if (e != 3 - shift) ok &= W(Reg_v(ctx, d), e) == 0xAAAAAAAAu;
        Check(ok, "vpkd3d128", "float16_2 of (1, -2) = 3C00C000 in element " + std::to_string(3 - shift));
    }
    // fused multiply-add: one rounding. a = 1 + 2^-30, b = 1 - 2^-30: a*b - 1 = -2^-60 exactly; unfused gives 0
    {
        ResetCtx(ctx); const int d = Operand(kOperands_fmadd, 0), a = Operand(kOperands_fmadd, 1), c = Operand(kOperands_fmadd, 2), b = Operand(kOperands_fmadd, 3);
        Reg_f(ctx, a).f64 = 1 + std::ldexp(1.0, -30); Reg_f(ctx, c).f64 = 1 - std::ldexp(1.0, -30); Reg_f(ctx, b).f64 = -1.0;
        T_fmadd(ctx, base);
        Check(Reg_f(ctx, d).f64 == -std::ldexp(1.0, -60), "fmadd", "fused: (1+2^-30)(1-2^-30) - 1 = -2^-60");
    }
    {
        ResetCtx(ctx); const int d = Operand(kOperands_fmadds, 0), a = Operand(kOperands_fmadds, 1), c = Operand(kOperands_fmadds, 2), b = Operand(kOperands_fmadds, 3);
        Reg_f(ctx, a).f64 = 1.5; Reg_f(ctx, c).f64 = 2.0; Reg_f(ctx, b).f64 = 0.25;
        T_fmadds(ctx, base);
        Check(Reg_f(ctx, d).f64 == 3.25, "fmadds", "1.5*2 + 0.25");
        Reg_f(ctx, a).f64 = 1.0 / 3.0; Reg_f(ctx, c).f64 = 1.0; Reg_f(ctx, b).f64 = 0.0;
        T_fmadds(ctx, base);
        Check(Reg_f(ctx, d).f64 == double(float(1.0 / 3.0)), "fmadds", "result rounded to single");
    }
    {
        ResetCtx(ctx); const int d = Operand(kOperands_fnmsubs, 0), a = Operand(kOperands_fnmsubs, 1), c = Operand(kOperands_fnmsubs, 2), b = Operand(kOperands_fnmsubs, 3);
        Reg_f(ctx, a).f64 = 3.0; Reg_f(ctx, c).f64 = 3.0; Reg_f(ctx, b).f64 = 10.0;
        const double want = -(Reg_f(ctx, a).f64 * Reg_f(ctx, c).f64 - Reg_f(ctx, b).f64);
        T_fnmsubs(ctx, base);
        Check(Reg_f(ctx, d).f64 == want, "fnmsubs", "-(a*c - b)");
    }
    {
        ResetCtx(ctx); const int d = Operand(kOperands_fmsubs, 0), a = Operand(kOperands_fmsubs, 1), c = Operand(kOperands_fmsubs, 2), b = Operand(kOperands_fmsubs, 3);
        Reg_f(ctx, a).f64 = 3.0; Reg_f(ctx, c).f64 = 4.0; Reg_f(ctx, b).f64 = 2.0;
        const double want = Reg_f(ctx, a).f64 * Reg_f(ctx, c).f64 - Reg_f(ctx, b).f64;   // operands may share a register
        T_fmsubs(ctx, base);
        Check(Reg_f(ctx, d).f64 == want, "fmsubs", "a*c - b");
    }
    // vector fused multiply-add: a = 1 + 2^-12, b = 1 - 2^-12 (floats): a*b - 1 = -2^-24; unfused (in float) gives 0
    {
        ResetCtx(ctx); const int d = Operand(kOperands_vmaddfp128, 0), a = Operand(kOperands_vmaddfp128, 1), b = Operand(kOperands_vmaddfp128, 2);
        for (int e = 0; e < 4; e++) { F(Reg_v(ctx, a), e) = 1 + std::ldexp(1.0f, -12); F(Reg_v(ctx, b), e) = 1 - std::ldexp(1.0f, -12); F(Reg_v(ctx, d), e) = -1.0f; }
        T_vmaddfp128(ctx, base);
        bool ok = true;
        for (int e = 0; e < 4; e++) ok &= F(Reg_v(ctx, d), e) == -std::ldexp(1.0f, -24);
        Check(ok, "vmaddfp128", "fused: vD = vA*vB + vD");
    }
    {
        ResetCtx(ctx); const int d = Operand(kOperands_vnmsubfp128, 0), a = Operand(kOperands_vnmsubfp128, 1), b = Operand(kOperands_vnmsubfp128, 2);
        for (int e = 0; e < 4; e++) { F(Reg_v(ctx, a), e) = 1 + std::ldexp(1.0f, -12); F(Reg_v(ctx, b), e) = 1 - std::ldexp(1.0f, -12); F(Reg_v(ctx, d), e) = 1.0f; }
        T_vnmsubfp128(ctx, base);
        bool ok = true;
        for (int e = 0; e < 4; e++) ok &= F(Reg_v(ctx, d), e) == std::ldexp(1.0f, -24);
        Check(ok, "vnmsubfp128", "fused: vD = -(vA*vB - vD)");
    }
    {
        ResetCtx(ctx); const int d = Operand(kOperands_vmaddcfp128, 0), a = Operand(kOperands_vmaddcfp128, 1), c = Operand(kOperands_vmaddcfp128, 3);
        for (int e = 0; e < 4; e++) { F(Reg_v(ctx, a), e) = 2.0f; F(Reg_v(ctx, d), e) = 3.0f + e; F(Reg_v(ctx, c), e) = 0.5f; }
        T_vmaddcfp128(ctx, base);
        bool ok = true;
        for (int e = 0; e < 4; e++) ok &= F(Reg_v(ctx, d), e) == 2.0f * (3.0f + e) + 0.5f;
        Check(ok, "vmaddcfp128", "vD = vA*vD + vB");
    }
    // memset(r3 = dst, r4 = value, r5 = n): every length 0..64 at every alignment, guard bytes untouched
    {
        bool ok = true;
        std::string first;
        for (uint32_t align = 0; align < 16; align++)
            for (uint32_t n = 0; n <= 64; n++)
            {
                memset(memory, 0x5A, sizeof(memory));
                ResetCtx(ctx);
                const uint32_t dst = 0x100 + align;
                ctx.r3.u64 = dst; ctx.r4.u64 = 0xC7; ctx.r5.u64 = n;
                T_memset(ctx, base);
                bool good = true;
                for (uint32_t i = 0x80; i < 0x200; i++)
                    good &= memory[i] == ((i >= dst && i < dst + n) ? 0xC7 : 0x5A);
                if (!good && first.empty()) first = "align " + std::to_string(align) + " length " + std::to_string(n);
                ok &= good;
            }
        Check(ok, "memset", ok ? "" : "first wrong: " + first);
    }

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
