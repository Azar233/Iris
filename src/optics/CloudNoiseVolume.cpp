#include "asset/InputManifest.h"
#include "optics/CloudNoiseVolume.h"
#include "optics/CloudFieldCpp.h"
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
constexpr std::uint64_t offset = 14695981039346656037ULL;
constexpr std::uint64_t prime = 1099511628211ULL;
std::size_t sizeFor(int resolution, int period) {
    if ((resolution != 32 && resolution != 64) || period < 1 || period > 16)
        throw std::runtime_error("Cloud noise requires resolution 32/64 and period 1..16");
    return static_cast<std::size_t>(resolution) * resolution * resolution * 4;
}
void validate(const NoiseVolume& v) {
    if (v.texels.size() != sizeFor(v.resolution, v.period))
        throw std::runtime_error("Cloud noise voxel count mismatch");
}
void hashByte(std::uint64_t& hash, std::uint8_t byte) { hash ^= byte; hash *= prime; }
void hashWord(std::uint64_t& hash, std::uint32_t word) {
    for (int byte = 0; byte < 4; ++byte) hashByte(hash, static_cast<std::uint8_t>(word >> (byte * 8)));
}
std::uint64_t fingerprint(const NoiseVolume& v) {
    std::uint64_t hash = offset;
    hashWord(hash, 1); // generator + format version
    hashWord(hash, static_cast<std::uint32_t>(v.resolution));
    hashWord(hash, static_cast<std::uint32_t>(v.period));
    for (auto texel : v.texels) {
        hashByte(hash, static_cast<std::uint8_t>(texel));
        hashByte(hash, static_cast<std::uint8_t>(texel >> 8));
    }
    return hash;
}
float mix(float a, float b, float t) { return a + (b - a) * t; }
float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
float gradient(int x, int y, int z, int period, float dx, float dy, float dz) {
    const auto ix = static_cast<uint>(myrenderer_cloud_wrap(x, period));
    const auto iy = static_cast<uint>(myrenderer_cloud_wrap(y, period));
    const auto iz = static_cast<uint>(myrenderer_cloud_wrap(z, period));
    const uint hash = myrenderer_cloud_hash(ix * 0x9e3779b9U ^ iy * 0x85ebca6bU ^ iz * 0xc2b2ae35U);
    const int h = static_cast<int>(hash & 15U);
    const float u = h < 8 ? dx : dy;
    const float v = h < 4 ? dy : ((h == 12 || h == 14) ? dx : dz);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}
float perlin(float x, float y, float z, int period) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(ix), fy = y - static_cast<float>(iy), fz = z - static_cast<float>(iz);
    float corners[2][2][2];
    for (int dz = 0; dz < 2; ++dz) for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
        corners[dz][dy][dx] = gradient(ix + dx, iy + dy, iz + dz, period,
            fx - static_cast<float>(dx), fy - static_cast<float>(dy), fz - static_cast<float>(dz));
    const float sx = fade(fx), sy = fade(fy), sz = fade(fz);
    return mix(mix(mix(corners[0][0][0], corners[0][0][1], sx),
                   mix(corners[0][1][0], corners[0][1][1], sx), sy),
               mix(mix(corners[1][0][0], corners[1][0][1], sx),
                   mix(corners[1][1][0], corners[1][1][1], sx), sy), sz);
}
void writeWord(std::ostream& stream, std::uint64_t word, int bytes) {
    for (int byte = 0; byte < bytes; ++byte) stream.put(static_cast<char>(word >> (byte * 8)));
}
std::uint64_t readWord(std::istream& stream, int bytes) {
    std::uint64_t value = 0;
    for (int byte = 0; byte < bytes; ++byte) {
        const int next = stream.get();
        if (next == std::char_traits<char>::eof()) throw std::runtime_error("Truncated cloud noise asset");
        value |= static_cast<std::uint64_t>(next) << (byte * 8);
    }
    return value;
}
} // namespace
const std::filesystem::path& canonicalNoisePath() {
    static const std::filesystem::path resolved = [] {
        std::vector<std::filesystem::path> roots;
#ifdef _WIN32
        std::wstring executable(32768,L'\0');
        const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        if(length>0 && length<executable.size()) {
            executable.resize(length);roots.push_back(std::filesystem::path(executable).parent_path());
        }
#endif
        roots.push_back(std::filesystem::current_path());
        roots.emplace_back(MYRENDERER_SOURCE_DIR);
        for(const auto& root:roots) {
            const auto path=root/canonicalNoiseResource;
            if(!std::filesystem::is_regular_file(path)) continue;
            return std::filesystem::absolute(path).lexically_normal();
        }
        throw std::runtime_error("Runtime cloud noise asset not found");
    }();
    return resolved;
}
const NoiseVolume& canonicalNoiseVolume() {
    static const NoiseVolume volume=[] {
        auto input=readNoiseVolume(canonicalNoisePath());
        if(input.resolution!=64 || input.period!=4 || input.fingerprint()!=canonicalNoiseFingerprint)
            throw std::runtime_error("Runtime cloud noise does not match the versioned input");
        return input;
    }();return volume;
}
std::string NoiseVolume::fingerprint() const {
    validate(*this);
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << cloud::fingerprint(*this);
    return out.str();
}
NoiseVolume generateNoiseVolume(int resolution, int period) {
    NoiseVolume v;
    v.resolution = resolution; v.period = period;
    v.texels.resize(sizeFor(resolution, period));
    const float scale = static_cast<float>(period) / static_cast<float>(resolution);
    for (int z = 0; z < resolution; ++z) for (int y = 0; y < resolution; ++y) for (int x = 0; x < resolution; ++x) {
        const float px = (static_cast<float>(x) + 0.5f) * scale;
        const float py = (static_cast<float>(y) + 0.5f) * scale;
        const float pz = (static_cast<float>(z) + 0.5f) * scale;
        const std::array<float, 4> values = {
            myrenderer_cloud_worley3(px, py, pz, period, 101),
            myrenderer_cloud_worley3(px * 2.0f, py * 2.0f, pz * 2.0f, period * 2, 154),
            myrenderer_cloud_worley3(px * 4.0f, py * 4.0f, pz * 4.0f, period * 4, 307),
            std::clamp(0.5f + (perlin(px, py, pz, period) * 0.5714286f
                + perlin(px * 2.0f, py * 2.0f, pz * 2.0f, period * 2) * 0.2857143f
                + perlin(px * 4.0f, py * 4.0f, pz * 4.0f, period * 4) * 0.1428571f) * 0.5f, 0.0f, 1.0f)};
        const auto index = ((static_cast<std::size_t>(z) * resolution + y) * resolution + x) * 4;
        for (std::size_t c = 0; c < 4; ++c)
            v.texels[index + c] = static_cast<std::uint16_t>(std::lround(values[c] * 65535.0f));
    }
    return v;
}
std::array<float, 4> NoiseVolume::sample(float x, float y, float z) const {
    validate(*this);
    // Reduce before converting to int: large world coordinates cannot overflow texel indices.
    const auto coordinate = [this](float value) {
        if (!std::isfinite(value)) throw std::runtime_error("Non-finite cloud noise coordinate");
        const float tile = value / static_cast<float>(period);
        return (tile - std::floor(tile)) * static_cast<float>(resolution) - 0.5f;
    };
    const float px = coordinate(x), py = coordinate(y), pz = coordinate(z);
    const int ix = static_cast<int>(std::floor(px)), iy = static_cast<int>(std::floor(py)), iz = static_cast<int>(std::floor(pz));
    const float fx = px - static_cast<float>(ix), fy = py - static_cast<float>(iy), fz = pz - static_cast<float>(iz);
    const auto voxel = [this](int a, int b, int c, std::size_t channel) {
        const auto index = ((static_cast<std::size_t>(myrenderer_cloud_wrap(c, resolution)) * resolution
            + myrenderer_cloud_wrap(b, resolution)) * resolution + myrenderer_cloud_wrap(a, resolution)) * 4;
        return static_cast<float>(texels[index + channel]) / 65535.0f;
    };
    std::array<float, 4> result{};
    for (std::size_t c = 0; c < 4; ++c)
        result[c] = mix(mix(mix(voxel(ix,iy,iz,c), voxel(ix+1,iy,iz,c),fx),
                           mix(voxel(ix,iy+1,iz,c),voxel(ix+1,iy+1,iz,c),fx),fy),
                       mix(mix(voxel(ix,iy,iz+1,c),voxel(ix+1,iy,iz+1,c),fx),
                           mix(voxel(ix,iy+1,iz+1,c),voxel(ix+1,iy+1,iz+1,c),fx),fy),fz);
    return result;
}
void writeNoiseVolume(const std::filesystem::path& path, const NoiseVolume& volume) {
    validate(volume);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write("MRCNOISE", 8);
    writeWord(file, 1, 4); writeWord(file, static_cast<std::uint32_t>(volume.resolution), 4);
    writeWord(file, static_cast<std::uint32_t>(volume.period), 4); writeWord(file, cloud::fingerprint(volume), 8);
    for (auto texel : volume.texels) writeWord(file, texel, 2);
    file.flush();
    if (!file) throw std::runtime_error("Cannot write cloud noise asset: " + path.string());
}
NoiseVolume readNoiseVolume(const std::filesystem::path& path) {
    capture::recordInput(path);
    std::ifstream file(path, std::ios::binary);
    char magic[8]{}; file.read(magic, 8);
    if (!file || std::string(magic, 8) != "MRCNOISE" || readWord(file, 4) != 1)
        throw std::runtime_error("Invalid cloud noise format/generator version: " + path.string());
    NoiseVolume volume;
    const auto resolution = readWord(file, 4), period = readWord(file, 4);
    if (resolution > 64 || period > 16) throw std::runtime_error("Invalid cloud noise dimensions");
    volume.resolution = static_cast<int>(resolution); volume.period = static_cast<int>(period);
    const auto expected = readWord(file, 8);
    volume.texels.resize(sizeFor(volume.resolution, volume.period));
    for (auto& texel : volume.texels) texel = static_cast<std::uint16_t>(readWord(file, 2));
    if (file.get() != std::char_traits<char>::eof() || !file.eof()) throw std::runtime_error("Trailing cloud noise data");
    if (cloud::fingerprint(volume) != expected) throw std::runtime_error("Cloud noise content fingerprint mismatch");
    return volume;
}
} // namespace cloud
