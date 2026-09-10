// A reader of swgemu.exe from disk for bin::verify() in the tests: it maps VAs
// onto file offsets through the PE section table. The binary is never run.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace cp { namespace bin {

namespace {
struct Section { uint32_t va, vsize, raw, rawsize; };
std::vector<unsigned char> g_image; std::vector<Section> g_sections; uint32_t g_base = 0; bool g_loaded = false;

bool load()
{
    if (g_loaded) return !g_image.empty();
    g_loaded = true;
    const char* path = std::getenv("CP_EXE");
    if (!path || !*path) path = "E:\\Games\\SWG\\Dev\\SWG Infinity\\Test Center\\swgemu.exe";
    FILE* f = std::fopen(path, "rb"); if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    g_image.resize(n); size_t got = std::fread(g_image.data(), 1, n, f); std::fclose(f);
    if (got != static_cast<size_t>(n) || n < 0x100) { g_image.clear(); return false; }
    const unsigned char* p = g_image.data();
    uint32_t pe = *reinterpret_cast<const uint32_t*>(p + 0x3C);
    if (std::memcmp(p + pe, "PE\0\0", 4) != 0) { g_image.clear(); return false; }
    uint16_t nsec = *reinterpret_cast<const uint16_t*>(p + pe + 6);
    uint16_t optsz = *reinterpret_cast<const uint16_t*>(p + pe + 20);
    g_base = *reinterpret_cast<const uint32_t*>(p + pe + 24 + 28);
    const unsigned char* s = p + pe + 24 + optsz;
    for (int i = 0; i < nsec; ++i, s += 40) {
        Section sec; sec.vsize = *reinterpret_cast<const uint32_t*>(s + 8); sec.va = *reinterpret_cast<const uint32_t*>(s + 12);
        sec.rawsize = *reinterpret_cast<const uint32_t*>(s + 16); sec.raw = *reinterpret_cast<const uint32_t*>(s + 20);
        g_sections.push_back(sec);
    }
    return true;
}
} // namespace

const unsigned char* mockRead(uint32_t va, size_t n)
{
    if (!load()) return nullptr;
    uint32_t rva = va - g_base;
    for (const Section& s : g_sections) {
        if (rva >= s.va && rva + n <= s.va + s.vsize) {
            uint32_t off = rva - s.va;
            if (off + n > s.rawsize) return nullptr;            // there is no data in the .bss part of a section
            return g_image.data() + s.raw + off;
        }
    }
    return nullptr;
}

bool mockExeAvailable() { return load(); }

}} // namespace cp::bin
