#include "optics/CloudLightingLut.h"
#include "asset/InputManifest.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace cloud {
namespace {
void word(std::ostream& out, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i)
        out.put(static_cast<char>(v >> (8 * i)));
}
std::uint64_t word(std::istream& in, int n) {
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i) {
        int c = in.get();
        if (c < 0)
            throw std::runtime_error("Truncated lighting LUT");
        v |= static_cast<std::uint64_t>(c) << (8 * i);
    }
    return v;
}
void validate(const LightingLut& lut) {
    if (lut.values.size() != LightingLut::count || lut.values.front() != 16777216u)
        throw std::runtime_error("Invalid lighting LUT dimensions/endpoint");
    for (std::size_t i = 0; i < lut.values.size(); ++i)
        if (lut.values[i] > 16777216u || (i && lut.values[i] > lut.values[i - 1]))
            throw std::runtime_error("Lighting LUT must be bounded and monotonic");
}
std::uint64_t hash(const LightingLut& lut) {
    validate(lut);
    std::uint64_t h = 14695981039346656037ULL;
    for (auto v : {1u, static_cast<unsigned int>(LightingLut::count), 32u})
        for (int i = 0; i < 4; ++i) {
            h ^= static_cast<unsigned char>(v >> (8 * i));
            h *= 1099511628211ULL;
        }
    for (auto v : lut.values)
        for (int i = 0; i < 4; ++i) {
            h ^= static_cast<unsigned char>(v >> (8 * i));
            h *= 1099511628211ULL;
        }
    return h;
}
} // namespace
std::string LightingLut::fingerprint() const {
    std::ostringstream s;
    s << std::hex << std::setfill('0') << std::setw(16) << hash(*this);
    return s.str();
}
LightingLut generateLightingLut() {
    LightingLut l;
    l.values.resize(LightingLut::count);
    for (int i = 0; i < LightingLut::count; ++i)
        l.values[i] = static_cast<std::uint32_t>(
            std::llround(std::exp(-double(i) * 32.0 / (LightingLut::count - 1)) * 16777216.0));
    return l;
}
float LightingLut::sample(float depth) const {
    if (values.size() != count)
        throw std::runtime_error("Invalid lighting LUT sample data");
    if (!std::isfinite(depth) || depth < 0)
        throw std::runtime_error("Invalid cloud optical depth");
    // Outside the tabulated domain, evaluate the analytic tail. Q24 may round
    // sub-LSB transmission to zero inside the domain (bounded absolute error).
    if (depth > 32.0f)
        return std::exp(-depth);
    const float x = depth * 256.0f;
    const int i = std::min(static_cast<int>(x), LightingLut::count - 2);
    const float a = static_cast<float>(values[i]) / 16777216.0f,
                b = static_cast<float>(values[i + 1]) / 16777216.0f;
    return a + (b - a) * (x - static_cast<float>(i));
}
void writeLightingLut(const std::filesystem::path& path, const LightingLut& lut) {
    validate(lut);
    std::ofstream out(path, std::ios::binary);
    out.write("MRCLIGHT", 8);
    word(out, 1, 4);
    word(out, LightingLut::count, 4);
    word(out, 32, 4);
    word(out, hash(lut), 8);
    for (auto v : lut.values)
        word(out, v, 4);
    out.flush();
    if (!out)
        throw std::runtime_error("Cannot write lighting LUT");
}
LightingLut readLightingLut(const std::filesystem::path& path) {
    capture::recordInput(path);
    std::ifstream in(path, std::ios::binary);
    char m[8]{};
    in.read(m, 8);
    if (std::string(m, 8) != "MRCLIGHT" || word(in, 4) != 1 || word(in, 4) != LightingLut::count ||
        word(in, 4) != 32)
        throw std::runtime_error("Invalid lighting LUT header");
    const auto expected = word(in, 8);
    LightingLut l;
    l.values.resize(LightingLut::count);
    for (auto& v : l.values)
        v = static_cast<std::uint32_t>(word(in, 4));
    if (in.get() != std::char_traits<char>::eof() || !in.eof() || hash(l) != expected)
        throw std::runtime_error("Lighting LUT content mismatch");
    return l;
}
const std::filesystem::path& canonicalLightingPath() {
    static const auto path = [] {
        std::vector<std::filesystem::path> roots;
#ifdef _WIN32
        std::wstring exe(32768, L'\0');
        const auto n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (n > 0 && n < exe.size()) {
            exe.resize(n);
            roots.push_back(std::filesystem::path(exe).parent_path());
        }
#endif
        roots.push_back(std::filesystem::current_path());
        roots.emplace_back(MYRENDERER_SOURCE_DIR);
        for (const auto& root : roots)
            if (std::filesystem::is_regular_file(root / canonicalLightingResource))
                return std::filesystem::absolute(root / canonicalLightingResource)
                    .lexically_normal();
        throw std::runtime_error("Runtime lighting LUT not found");
    }();
    return path;
}
const LightingLut& canonicalLightingLut() {
    static const auto lut = [] {
        auto l = readLightingLut(canonicalLightingPath());
        if (l.fingerprint() != canonicalLightingFingerprint)
            throw std::runtime_error("Runtime lighting LUT does not match versioned input");
        return l;
    }();
    return lut;
}
float transmission(float depth, bool offline) {
    return offline ? canonicalLightingLut().sample(depth) : std::exp(-depth);
}
} // namespace cloud
