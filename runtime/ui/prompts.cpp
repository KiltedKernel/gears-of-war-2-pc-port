// Button prompts that follow the input device: while the player uses keyboard/mouse, the controller-button glyphs of
// GoW2's Warfare_HUD.Xbox360_18pt font (used by every <Fonts:...Xbox360_18pt>A<Fonts:/> prompt, alias bindings
// included) are swapped for key and mouse icons; a controller brings the originals back.
//
// The font draws glyphs from texture page 0 (256x256 DXT5, tiled, identified by content) through its 256-entry
// FFontCharacter table {StartU, StartV, USize, VSize, TextureIndex (byte), VerticalOffset}, big-endian, which the game
// reads on every draw. Keyboard icons are wider than the buttons and page 0 is full, so the keyboard version is a
// repack: every page-0 glyph that stays keeps its pixels but may move, the button glyphs become icons
// (build\prompts\kbm_glyphs.*, made by tools/make_prompts.py from the player's Gears of War: Reloaded install), and
// the table is rewritten to match. Switching writes both into guest memory; Xenia re-uploads the texture.
#include <stdafx.h>
#include <kernel/memory.h>
#include <os/logger.h>
#include <hid/hid.h>
#include <user/settings.h>
#include "../../xe_glue/port_gpu.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace prompts
{
namespace
{
    constexpr uint32_t kSize = 256, kBytes = 65536;            // page 0: 256x256 DXT5 = 64x64 blocks of 16 bytes
    constexpr uint64_t kOriginalHash = 0xbfbe34a9abae9459ull;  // FNV-1a 64 of the stock page as the game loads it
    constexpr int kGlyphs = 256;

    struct Glyph { int32_t u, v, w, h, texture, voffset; };
    struct Rgba { uint8_t r, g, b, a; };
    using Image = std::vector<Rgba>;                           // kSize x kSize

    std::mutex g_mutex;
    std::atomic<uint32_t> g_page{ 0 };                         // physical address of page 0, once seen (the latest)
    std::vector<uint32_t> g_pages;                             // every address it was loaded at: menus that load
                                                               // the font again (the multiplayer lobby) keep copies
    std::vector<uint8_t> g_originalTex, g_kbmTex;              // guest (tiled, byte-swapped) data
    std::array<Glyph, kGlyphs> g_originalGlyphs{}, g_kbmGlyphs{};
    uint32_t g_table = 0;                                      // guest address of the FFontCharacter array
    bool g_built = false, g_failed = false;
    std::atomic<int> g_applied{ 0 };                           // 0 = original, 1 = keyboard/mouse
    std::atomic<bool> g_moved{ false };                        // the font was loaded again elsewhere (new table)
    uint64_t g_kbmHash = 0;

    uint64_t Fnv(const uint8_t* p, size_t n)
    {
        uint64_t h = 0xcbf29ce484222325ull;
        for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
        return h;
    }

    // ---- Xenos tiling (Xenia texture_address::Tiled2D) and DXT5 ----
    uint32_t Tiled(uint32_t x, uint32_t y, uint32_t pitchBlocks, uint32_t bppLog2)
    {
        const uint32_t outer = ((y >> 5) * (pitchBlocks >> 5) + (x >> 5)) << 6;
        const uint32_t inner = (((y >> 1) & 7) << 3) | (x & 7);
        const uint32_t oib = (outer | inner) << bppLog2;
        const uint32_t bank = (y >> 4) & 1, pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1);
        return ((y & 1) << 4) | (pipe << 6) | (bank << 11) | (oib & 15) | (((oib >> 4) & 1) << 5) |
               (((oib >> 5) & 7) << 8) | ((oib >> 8) << 12);
    }

    void Rgb565(uint16_t c, int out[3])
    {
        out[0] = ((c >> 11) & 31) * 255 / 31; out[1] = ((c >> 5) & 63) * 255 / 63; out[2] = (c & 31) * 255 / 31;
    }

    // guest bytes -> RGBA (blocks are tiled; each 16-bit word is byte-swapped, k_8in16)
    Image Decode(const std::vector<uint8_t>& guest)
    {
        Image img(kSize * kSize);
        for (uint32_t by = 0; by < kSize / 4; by++)
            for (uint32_t bx = 0; bx < kSize / 4; bx++)
            {
                uint8_t b[16];
                const uint8_t* src = guest.data() + Tiled(bx, by, kSize / 4, 4);
                for (int i = 0; i < 16; i += 2) { b[i] = src[i + 1]; b[i + 1] = src[i]; }
                int a[8] = { b[0], b[1] };
                if (a[0] > a[1]) for (int i = 1; i < 7; i++) a[i + 1] = ((7 - i) * a[0] + i * a[1]) / 7;
                else { for (int i = 1; i < 5; i++) a[i + 1] = ((5 - i) * a[0] + i * a[1]) / 5; a[6] = 0; a[7] = 255; }
                uint64_t abits = 0;
                for (int k = 0; k < 6; k++) abits |= uint64_t(b[2 + k]) << (8 * k);
                const uint16_t c0 = b[8] | (b[9] << 8), c1 = b[10] | (b[11] << 8);
                const uint32_t cbits = b[12] | (b[13] << 8) | (b[14] << 16) | (uint32_t(b[15]) << 24);
                int p[4][3];
                Rgb565(c0, p[0]); Rgb565(c1, p[1]);
                for (int k = 0; k < 3; k++) { p[2][k] = (2 * p[0][k] + p[1][k]) / 3; p[3][k] = (p[0][k] + 2 * p[1][k]) / 3; }
                for (int i = 0; i < 16; i++)
                {
                    const int* c = p[(cbits >> (2 * i)) & 3];
                    img[(by * 4 + i / 4) * kSize + bx * 4 + i % 4] =
                        { uint8_t(c[0]), uint8_t(c[1]), uint8_t(c[2]), uint8_t(a[(abits >> (3 * i)) & 7]) };
                }
            }
        return img;
    }

    uint16_t To565(const int c[3])
    {
        return uint16_t(((c[0] * 31 + 127) / 255) << 11 | ((c[1] * 63 + 127) / 255) << 5 | ((c[2] * 31 + 127) / 255));
    }

    // RGBA -> guest bytes (range-fit DXT5, then tiled and byte-swapped)
    std::vector<uint8_t> Encode(const Image& img)
    {
        std::vector<uint8_t> guest(kBytes);
        for (uint32_t by = 0; by < kSize / 4; by++)
            for (uint32_t bx = 0; bx < kSize / 4; bx++)
            {
                Rgba px[16];
                for (int i = 0; i < 16; i++) px[i] = img[(by * 4 + i / 4) * kSize + bx * 4 + i % 4];
                uint8_t b[16] = {};
                int amax = 0, amin = 255;
                for (auto& q : px) { amax = std::max<int>(amax, q.a); amin = std::min<int>(amin, q.a); }
                b[0] = uint8_t(amax); b[1] = uint8_t(amin);
                static const int remap[8] = { 0, 2, 3, 4, 5, 6, 7, 1 };   // ramp step -> DXT5 index
                uint64_t abits = 0;
                if (amax != amin)
                    for (int i = 0; i < 16; i++)
                    {
                        const int t = std::clamp(((amax - px[i].a) * 7 + (amax - amin) / 2) / (amax - amin), 0, 7);
                        abits |= uint64_t(remap[t]) << (3 * i);
                    }
                for (int k = 0; k < 6; k++) b[2 + k] = uint8_t(abits >> (8 * k));
                // colour endpoints: brightest and darkest visible texels
                int hi[3] = { 0, 0, 0 }, lo[3] = { 0, 0, 0 }, hiL = -1, loL = 1 << 30;
                for (auto& q : px)
                {
                    if (q.a <= 8) continue;
                    const int l = q.r * 3 + q.g * 6 + q.b;
                    if (l > hiL) { hiL = l; hi[0] = q.r; hi[1] = q.g; hi[2] = q.b; }
                    if (l < loL) { loL = l; lo[0] = q.r; lo[1] = q.g; lo[2] = q.b; }
                }
                uint16_t c0 = To565(hi), c1 = To565(lo);
                if (c0 < c1) std::swap(c0, c1);
                if (c0 == c1 && c0) c1 = uint16_t(c0 - 1); else if (c0 == c1) c0 = 1;
                int p[4][3];
                Rgb565(c0, p[0]); Rgb565(c1, p[1]);
                for (int k = 0; k < 3; k++) { p[2][k] = (2 * p[0][k] + p[1][k]) / 3; p[3][k] = (p[0][k] + 2 * p[1][k]) / 3; }
                uint32_t cbits = 0;
                for (int i = 0; i < 16; i++)
                {
                    int best = 0, bestD = 1 << 30;
                    for (int k = 0; k < 4; k++)
                    {
                        const int d = (px[i].r - p[k][0]) * (px[i].r - p[k][0]) + (px[i].g - p[k][1]) * (px[i].g - p[k][1]) +
                                      (px[i].b - p[k][2]) * (px[i].b - p[k][2]);
                        if (d < bestD) { bestD = d; best = k; }
                    }
                    cbits |= uint32_t(best) << (2 * i);
                }
                b[8] = uint8_t(c0); b[9] = uint8_t(c0 >> 8); b[10] = uint8_t(c1); b[11] = uint8_t(c1 >> 8);
                for (int k = 0; k < 4; k++) b[12 + k] = uint8_t(cbits >> (8 * k));
                uint8_t* dst = guest.data() + Tiled(bx, by, kSize / 4, 4);
                for (int i = 0; i < 16; i += 2) { dst[i] = b[i + 1]; dst[i + 1] = b[i]; }
            }
        return guest;
    }

    // ---- keyboard/mouse glyphs (tools/make_prompts.py) ----
    struct Strip { uint32_t w = 0, h = 0; std::vector<Rgba> px; std::vector<std::array<int, 5>> glyphs; };

    bool LoadStrip(Strip& s)
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const auto dir = std::filesystem::path(exe).parent_path() / "prompts";
        std::ifstream idx(dir / "kbm_glyphs.txt");
        std::ifstream raw(dir / "kbm_glyphs.rgba", std::ios::binary);
        if (!idx || !raw || !(idx >> s.w >> s.h)) return false;
        std::array<int, 5> g;
        while (idx >> g[0] >> g[1] >> g[2] >> g[3] >> g[4]) s.glyphs.push_back(g);
        s.px.resize(size_t(s.w) * s.h);
        raw.read((char*)s.px.data(), std::streamsize(s.px.size() * 4));
        return bool(raw) && !s.glyphs.empty();
    }

    // repack page 0: kept glyphs (deduplicated by rect) + icons, shelves of decreasing height, 1 texel apart
    // compact[c]: use the icon's compact version (strip code c + 256, the button's own size) instead of the full one
    bool Build(const Strip& strip, const std::vector<bool>& compact, bool quiet)
    {
        const Image original = Decode(g_originalTex);
        struct Item { int w, h; const Rgba* src; uint32_t pitch; std::vector<int> chars; };
        std::vector<Item> items;
        std::vector<bool> replaced(kGlyphs, false);
        std::vector<const std::array<int, 5>*> chosen(kGlyphs, nullptr);
        for (const auto& g : strip.glyphs)
        {
            const int c = g[0] & 255;
            if (g[0] < 0 || g[0] >= 2 * kGlyphs || g_originalGlyphs[c].texture != 0) continue;
            if ((g[0] >= kGlyphs) == compact[c] || !chosen[c]) chosen[c] = &g;
        }
        for (int c = 0; c < kGlyphs; c++)
            if (const auto* g = chosen[c])
            {
                replaced[c] = true;
                items.push_back({ (*g)[3], (*g)[4], strip.px.data() + size_t((*g)[2]) * strip.w + (*g)[1], strip.w, { c } });
            }
        for (int c = 0; c < kGlyphs; c++)
        {
            const Glyph& o = g_originalGlyphs[c];
            if (replaced[c] || o.texture != 0 || o.w <= 0 || o.h <= 0) continue;
            auto same = std::find_if(items.begin(), items.end(), [&](const Item& it) {
                return it.pitch == kSize && it.src == original.data() + size_t(o.v) * kSize + o.u && it.w == o.w && it.h == o.h; });
            if (same != items.end()) { same->chars.push_back(c); continue; }
            items.push_back({ o.w, o.h, original.data() + size_t(o.v) * kSize + o.u, kSize, { c } });
        }
        // first-fit decreasing by width into rows (the stock glyphs are all one height: 26 texels, 9 rows fit), 1
        // texel between glyphs; the page is nearly full, so a greedy shelf would not do
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.w != b.w ? a.w > b.w : a.h > b.h; });
        struct Row { int y, h, used; };
        std::vector<Row> rows;
        std::vector<std::pair<int, int>> pos(items.size());
        int top = 0;
        for (size_t i = 0; i < items.size(); i++)
        {
            const Item& it = items[i];
            auto row = std::find_if(rows.begin(), rows.end(), [&](const Row& r) {
                return it.h <= r.h && r.used + (r.used ? 1 : 0) + it.w <= int(kSize); });
            if (row == rows.end())
            {
                if (top + it.h > int(kSize)) { if (!quiet) LOGFN_WARNING("prompts: keyboard glyphs do not fit the font page"); return false; }
                rows.push_back({ top, it.h, 0 });
                top += it.h + 1;
                row = rows.end() - 1;
            }
            const int x = row->used + (row->used ? 1 : 0);
            pos[i] = { x, row->y };
            row->used = x + it.w;
        }
        Image page(kSize * kSize, Rgba{ 0, 0, 0, 0 });
        g_kbmGlyphs = g_originalGlyphs;
        for (size_t i = 0; i < items.size(); i++)
        {
            const Item& it = items[i];
            const auto [x, y] = pos[i];
            for (int r = 0; r < it.h; r++)
                std::copy_n(it.src + size_t(r) * it.pitch, it.w, page.data() + size_t(y + r) * kSize + x);
            for (int c : it.chars)
            {
                Glyph& g = g_kbmGlyphs[c];
                g.u = x; g.v = y; g.w = it.w; g.h = it.h;
                if (replaced[c]) g.voffset = g_originalGlyphs[c].voffset;
            }
        }
        const int y = top, shelf = 0;
        g_kbmTex = Encode(page);
        g_kbmHash = Fnv(g_kbmTex.data(), g_kbmTex.size());
        LOGFN("prompts: keyboard/mouse page built ({} glyph rects, {} icons, {} rows used)", items.size(),
            strip.glyphs.size(), y + shelf);
        return true;
    }

    // ---- the font's character table: found by its 'A' and 'B' records (stock layout) ----
    uint32_t BE(uint32_t a) { return ByteSwap(*(uint32_t*)g_memory.Translate(a)); }

    Glyph ReadGlyph(uint32_t a)
    {
        Glyph g;
        g.u = int32_t(BE(a)); g.v = int32_t(BE(a + 4)); g.w = int32_t(BE(a + 8)); g.h = int32_t(BE(a + 12));
        g.texture = int32_t(BE(a + 16) >> 24); g.voffset = int32_t(BE(a + 20));
        return g;
    }

    void WriteGlyph(uint32_t a, const Glyph& g)
    {
        auto put = [&](uint32_t o, uint32_t v) { *(uint32_t*)g_memory.Translate(a + o) = ByteSwap(v); };
        put(0, g.u); put(4, g.v); put(8, g.w); put(12, g.h);
        put(16, (BE(a + 16) & 0x00FFFFFFu) | (uint32_t(g.texture) << 24)); put(20, uint32_t(g.voffset));
    }

    uint32_t FindTable()
    {
        static const uint32_t a[12] = { 201, 55, 24, 26, 0, 0, 227, 55, 26, 26, 0, 0 };   // 'A', 'B'
        uint32_t pattern[12];
        for (int i = 0; i < 12; i++) pattern[i] = ByteSwap(a[i]);
        for (uint64_t base = 0x40000000; base < 0x80000000;)
        {
            MEMORY_BASIC_INFORMATION mbi;
            uint8_t* host = (uint8_t*)g_memory.Translate(uint32_t(base));
            if (!VirtualQuery(host, &mbi, sizeof(mbi))) break;
            const uint64_t end = std::min<uint64_t>(0x80000000, base + uint64_t((uint8_t*)mbi.BaseAddress + mbi.RegionSize - host));
            if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            {
                const uint32_t* w = (const uint32_t*)host;
                const size_t n = size_t(end - base) / 4;
                for (size_t i = 0; i + 12 <= n; i++)
                    if (w[i] == pattern[0] && memcmp(w + i, pattern, sizeof(pattern)) == 0)
                    {
                        const uint32_t record = uint32_t(base + i * 4), table = record - 'A' * 24;
                        if (ReadGlyph(table + 'Y' * 24).u == 222) return table;   // stock 'Y' at (222, 109)
                    }
            }
            base = end;
        }
        return 0;
    }

    // every copy of the character table, in either layout: the stock one, or the keyboard one we wrote (menus such as
    // the multiplayer lobby copy the font's table, also while keyboard prompts are on)
    std::vector<uint32_t> FindTables()
    {
        auto pattern = [](const std::array<Glyph, kGlyphs>& g, uint32_t out[12]) {
            for (int k = 0; k < 2; k++)
            {
                const Glyph& x = g['A' + k];
                const uint32_t v[6] = { uint32_t(x.u), uint32_t(x.v), uint32_t(x.w), uint32_t(x.h), 0, uint32_t(x.voffset) };
                for (int i = 0; i < 6; i++) out[6 * k + i] = ByteSwap(v[i]);
            }
        };
        uint32_t stock[12], kbm[12];
        pattern(g_originalGlyphs, stock);
        pattern(g_kbmGlyphs, kbm);
        std::vector<uint32_t> found;
        for (uint64_t base = 0x40000000; base < 0x80000000;)
        {
            MEMORY_BASIC_INFORMATION mbi;
            uint8_t* host = (uint8_t*)g_memory.Translate(uint32_t(base));
            if (!VirtualQuery(host, &mbi, sizeof(mbi))) break;
            const uint64_t end = std::min<uint64_t>(0x80000000, base + uint64_t((uint8_t*)mbi.BaseAddress + mbi.RegionSize - host));
            if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            {
                const uint32_t* w = (const uint32_t*)host;
                const size_t n = size_t(end - base) / 4;
                for (size_t i = 0; i + 12 <= n; i++)
                {
                    // the texture byte of the records' +16 word is compared masked: the low bytes are other fields
                    auto same = [&](const uint32_t* p) {
                        for (int k = 0; k < 12; k++)
                            if (k % 6 == 4 ? (ByteSwap(w[i + k]) >> 24) != 0 : w[i + k] != p[k]) return false;
                        return true;
                    };
                    if ((w[i] == stock[0] && same(stock)) || (w[i] == kbm[0] && same(kbm)))
                    {
                        const uint32_t table = uint32_t(base + i * 4) - 'A' * 24;
                        const uint32_t yu = ReadGlyph(table + 'Y' * 24).u;
                        if (yu == uint32_t(g_originalGlyphs['Y'].u) || yu == uint32_t(g_kbmGlyphs['Y'].u)) found.push_back(table);
                    }
                }
            }
            base = end;
        }
        return found;
    }

    void Apply(bool kbm)
    {
        const auto& glyphs = kbm ? g_kbmGlyphs : g_originalGlyphs;
        std::vector<uint32_t> tables = FindTables();
        if (std::find(tables.begin(), tables.end(), g_table) == tables.end()) tables.push_back(g_table);
        for (uint32_t t : tables)
            for (int c = 0; c < kGlyphs; c++)
                if (g_originalGlyphs[c].texture == 0) WriteGlyph(t + c * 24, glyphs[c]);
        static const bool debugTables = getenv("GOW2_PROMPTS_DEBUG") != nullptr;
        if (debugTables) LOGFN("prompts: {} character table(s)", tables.size());
        const auto& tex = kbm ? g_kbmTex : g_originalTex;
        // only copies that still hold the page (stock or keyboard): a freed copy's memory may be another texture now
        std::vector<uint8_t> cur(kBytes);
        std::erase_if(g_pages, [&](uint32_t page) {
            portgpu::ReadPhysical(page, cur.data(), kBytes);
            const uint64_t h = Fnv(cur.data(), kBytes);
            const bool keep = h == kOriginalHash || h == g_kbmHash;
            static const bool debug = getenv("GOW2_PROMPTS_DEBUG") != nullptr;
            if (debug)
            {
                size_t vsStock = 0, vsKbm = 0, firstDiff = SIZE_MAX;
                for (size_t i = 0; i < kBytes; i++)
                {
                    vsStock += cur[i] != g_originalTex[i];
                    if (cur[i] != g_kbmTex[i]) { vsKbm++; if (firstDiff == SIZE_MAX) firstDiff = i; }
                }
                LOGFN("prompts: page {:08X}: {} ({} bytes differ from stock, {} from keyboard, first at {:X})", page,
                    h == kOriginalHash ? "stock" : h == g_kbmHash ? "keyboard" : "something else (dropped)", vsStock, vsKbm, firstDiff);
            }
            if (!keep) return true;
            portgpu::WritePhysical(page, tex.data(), tex.size(), true);
            return false;
        });
        g_applied = kbm ? 1 : 0;
        LOGFN("prompts: {} prompts", kbm ? "keyboard/mouse" : "controller");
    }

    void Worker()
    {
        Strip strip;
        const bool haveStrip = LoadStrip(strip);
        if (!haveStrip) LOGFN("prompts: no build\\prompts\\kbm_glyphs.* (run tools/make_prompts.py): controller prompts only");
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!haveStrip || g_failed || !g_page) continue;
            std::lock_guard lock(g_mutex);
            if (!g_built)
            {
                g_table = FindTable();
                if (!g_table) { static int tries = 0; if (++tries == 50) LOGFN_WARNING("prompts: font table not found"); continue; }
                for (int c = 0; c < kGlyphs; c++) g_originalGlyphs[c] = ReadGlyph(g_table + c * 24);
                // the page is nearly full: the widest icons fall back to their compact versions until all fit
                std::vector<bool> compact(kGlyphs, false);
                bool ok = false;
                for (int round = 0; round <= kGlyphs && !(ok = Build(strip, compact, true)); round++)
                {
                    int widest = -1, w = 0;
                    for (const auto& g : strip.glyphs)
                        if (g[0] >= 0 && g[0] < kGlyphs && !compact[g[0]] && g[3] > w) { w = g[3]; widest = g[0]; }
                    if (widest < 0) break;
                    compact[widest] = true;
                }
                if (!ok) { Build(strip, compact, false); g_failed = true; continue; }
                const auto n = std::count(compact.begin(), compact.end(), true);
                if (n) LOGFN("prompts: {} icons in their compact size (the font page is full)", n);
                g_built = true;
                LOGFN("prompts: font table at {:08X}, page 0 at physical {:08X}", g_table, g_page.load());
            }
            const int mode = settings::Get().prompts;   // 0 auto, 1 keyboard, 2 controller
            const bool want = mode == 1 || (mode == 0 && hid::ActiveDevice() == hid::Device::KeyboardMouse);
            // menus that load the font again (the multiplayer lobby) bring a new character table: find it and put the
            // current prompts in it, or its stock coordinates would meet the keyboard page
            if (g_moved.exchange(false))
                if (const uint32_t t = FindTable(); t && t != g_table)
                {
                    LOGFN("prompts: the font was loaded again: table at {:08X}, page at {:08X}", t, g_page.load());
                    g_table = t;
                    Apply(want);
                    continue;
                }
            if (want != (g_applied == 1)) Apply(want);
        }
    }
}

// from the GPU thread (texture cache), just before the texture's guest data is uploaded
void OnTextureLoad(uint32_t address, uint32_t size, uint32_t format, uint32_t width, uint32_t height, bool tiled)
{
    if (format != 20 || width != kSize || height != kSize || !tiled || size != kBytes) return;
    std::vector<uint8_t> data(kBytes);
    portgpu::ReadPhysical(address, data.data(), kBytes);
    const uint64_t h = Fnv(data.data(), kBytes);
    std::lock_guard lock(g_mutex);
    if (h == kOriginalHash)
    {
        if (g_originalTex.empty()) g_originalTex = data;
        if (g_built) g_moved = true;   // loaded again (here or elsewhere): the table may be new too
        g_page = address;
        if (std::find(g_pages.begin(), g_pages.end(), address) == g_pages.end()) g_pages.push_back(address);
        // the game (re)loaded the stock page while keyboard prompts are on: keep them (no invalidation needed, the
        // upload that follows reads this)
        if (g_applied == 1 && !g_kbmTex.empty()) portgpu::WritePhysical(address, g_kbmTex.data(), kBytes, false);
    }
    else if (g_kbmHash && h == g_kbmHash)
    {
        // a copy the game made of the page while it held the keyboard picture (menus that load the font again, such as
        // the multiplayer lobby, copy it): one more copy to switch
        if (std::find(g_pages.begin(), g_pages.end(), address) == g_pages.end()) g_pages.push_back(address);
        g_page = address;
        if (g_applied == 0 && !g_originalTex.empty()) portgpu::WritePhysical(address, g_originalTex.data(), kBytes, false);
    }
}

void Start()
{
    static std::once_flag once;
    std::call_once(once, [] { std::thread(Worker).detach(); });
}
} // namespace prompts
