// Gears of War 2 verifies some files it reads (its packed configuration among them) against SHA-1 hashes built into
// the executable: FAsyncSHAVerify (8223EF68) asks GetFileSHAHash (827F9340: path -> 20 bytes, false when the file has
// no hash), hashes the bytes it read, and calls XamShowDirtyDiscErrorUI on a mismatch. A mod's file (or the
// configuration merged with mods' fragments, mods.h) would fail that, so for a file a mod provides the lookup answers
// with the SHA-1 of the file the game is actually served. Every other file keeps the disc's hash and its check.
#include <stdafx.h>
#include <cpu/ppc_context.h>
#include <kernel/memory.h>
#include <kernel/mods.h>
#include <os/logger.h>
#include <fstream>
#include <map>
#include <mutex>

std::filesystem::path GetGamePath();   // main.cpp

namespace
{
    struct Sha1
    {
        uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
        uint8_t block[64]; size_t used = 0; uint64_t total = 0;
        static uint32_t Rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
        void Compress()
        {
            uint32_t w[80];
            for (int i = 0; i < 16; i++) w[i] = uint32_t(block[4 * i]) << 24 | uint32_t(block[4 * i + 1]) << 16 | uint32_t(block[4 * i + 2]) << 8 | block[4 * i + 3];
            for (int i = 16; i < 80; i++) w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; i++)
            {
                const uint32_t f = i < 20 ? (b & c) | (~b & d) : i < 40 ? b ^ c ^ d : i < 60 ? (b & c) | (b & d) | (c & d) : b ^ c ^ d;
                const uint32_t k = i < 20 ? 0x5A827999 : i < 40 ? 0x6ED9EBA1 : i < 60 ? 0x8F1BBCDC : 0xCA62C1D6;
                const uint32_t t = Rol(a, 5) + f + e + k + w[i];
                e = d; d = c; c = Rol(b, 30); b = a; a = t;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
        }
        void Update(const uint8_t* p, size_t n)
        {
            total += n;
            while (n--) { block[used++] = *p++; if (used == 64) { Compress(); used = 0; } }
        }
        void Final(uint8_t out[20])
        {
            const uint64_t bits = total * 8;
            const uint8_t one = 0x80, zero = 0;
            Update(&one, 1);
            while (used != 56) Update(&zero, 1);
            for (int i = 7; i >= 0; i--) { const uint8_t b = uint8_t(bits >> (8 * i)); Update(&b, 1); }
            for (int i = 0; i < 20; i++) out[i] = uint8_t(h[i / 4] >> (24 - 8 * (i % 4)));
        }
    };

    bool HashFile(const std::filesystem::path& file, uint8_t out[20])
    {
        std::ifstream in(file, std::ios::binary);
        if (!in) return false;
        Sha1 s;
        std::vector<uint8_t> buf(1 << 16);
        while (in) { in.read((char*)buf.data(), buf.size()); s.Update(buf.data(), size_t(in.gcount())); }
        s.Final(out);
        return true;
    }
}

extern "C" PPC_FUNC(__imp__sub_827F9340);
PPC_FUNC(sub_827F9340)
{
    // the path: a big-endian UTF-16 TCHAR string, e.g. "..\GearGame\CookedXenon\Coalesced_INT.bin"
    std::string path;
    if (const uint32_t p = ctx.r3.u32)
        for (uint32_t i = 0; i < 260; i++)
        {
            const uint16_t c = ByteSwap(*(uint16_t*)g_memory.Translate(p + 2 * i));
            if (!c) break;
            path += char(c < 128 ? c : '?');
        }
    std::string rel = path;
    for (const char* pre : { "..\\", "game:\\", "d:\\" })
        if (rel.size() >= strlen(pre) && _strnicmp(rel.c_str(), pre, strlen(pre)) == 0) rel.erase(0, strlen(pre));
    const std::filesystem::path served = mods::Resolve(rel, GetGamePath());
    if (served.empty())
    {
        __imp__sub_827F9340(ctx, base);
        return;
    }
    static std::mutex m;
    static std::map<std::string, std::array<uint8_t, 20>> cache;
    std::lock_guard lock(m);
    auto it = cache.find(served.string());
    if (it == cache.end())
    {
        std::array<uint8_t, 20> h{};
        if (!HashFile(served, h.data())) { __imp__sub_827F9340(ctx, base); return; }
        it = cache.emplace(served.string(), h).first;
        LOGFN("mods: {} is a mod's file: the game's SHA-1 check uses its own hash", path);
    }
    memcpy(g_memory.Translate(ctx.r4.u32), it->second.data(), 20);
    ctx.r3.u64 = 1;
}
