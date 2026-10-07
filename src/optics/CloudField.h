// Cloud density field, shared verbatim between the CPU reference raymarcher and the GLSL fragment
// shader that renders the layer.
//
// This file is deliberately **plain GLSL 3.30**. The GPU side includes it directly; the C++ side
// goes through `optics/CloudFieldCpp.h`, which supplies the handful of names C++ lacks and then
// includes this file. Keeping the conditionals out of *this* file is not tidiness: the GLSL
// preprocessor is a restricted subset, and every `#ifdef`/`#undef` pair added here is another chance
// for a driver to disagree with the C preprocessor about what the text means. One guard, one body,
// no dialects.
//
// Rules for editing this file, because it has to satisfy two compilers:
//   - No `inline`, no C-style casts (`(float)x` -- use `float(x)`), no `constexpr`, no C++ types.
//   - Constants are `const float` / `const int`.
//   - Array indexing by a non-constant is not allowed in GLSL 3.30, so nothing here loops over an
//     array with a variable index.
//   - A struct passed by value is fine in both languages; a struct is how the layer's parameters
//     travel, because a seventeen-argument function is a parameter list nobody can diff.
#ifndef MYRENDERER_CLOUD_FIELD_INCLUDED
#define MYRENDERER_CLOUD_FIELD_INCLUDED

// Everything the density field needs to know about the layer it belongs to (P1-A slice 6, step C5).
//
// This is plain data with no defaults: GLSL 3.30 has no default member initialisers, so the C++ side
// has to build a fully populated value and the shader has to build one from its uniforms. That is
// deliberate -- it keeps one definition of the field's inputs instead of a parameter list that the
// two callers each transcribe.
struct MyRendererCloudParams {
    float baseHeight;
    float topHeight;
    float featureScale;
    // Number of feature-scale tiles before the base/detail field repeats. Keeping this in the
    // shared struct prevents the persisted `cloudNoisePeriod` from becoming a UI-only value.
    int noisePeriod;
    bool offlineNoise;
    float windX;
    float windZ;
    // Fraction of the sky the layer covers, in 0..1: 0 is clear, 1 is overcast. Unlike the C1
    // threshold this replaced, larger means *more* cloud, which is the direction the weather map's
    // red channel also runs. Two coverage knobs pointing opposite ways is a bug factory.
    float coverage;
    float densityScale;
    // World-unit size of one repeat of the weather pattern. Deliberately much larger than
    // `featureScale`: the weather is a property of the air mass, an individual cloud is not.
    float weatherScale;
    // How far the weather map's channels may swing each of their three quantities away from the
    // layer-wide value. 0 makes the map inert and the layer uniform, which is the control case the
    // structure test compares against.
    float coverageVariation;
    // 0 is a flat layered deck (stratus), 1 a bottom-heavy convective cloud (cumulus).
    float cloudType;
    float typeVariation;
    // How far the profile may slide up and down inside the slab, as a fraction of the slab.
    float heightVariation;
    // How strongly the finest octaves take density out of a cloud's interior, and how much of that
    // survives at the silhouette. `detailEdge` is the one that decides whether the edge is ragged or
    // smooth; see `myrenderer_cloud_density`.
    float detailStrength;
    float detailEdge;
    float shapeBlend;
};

// Deterministic hash. Integer hashing alone means no table, no random state, and no dependence on
// sampling order, so the field is identical on every run, every thread and both sides of the CPU /
// GPU boundary.
MYRENDERER_CLOUD_INLINE unsigned int myrenderer_cloud_hash(unsigned int x) {
    x ^= x >> 16U;
    x *= 0x7feb352dU;
    x ^= x >> 15U;
    x *= 0x846ca68bU;
    return x ^ (x >> 16U);
}

MYRENDERER_CLOUD_INLINE float myrenderer_cloud_hash_unit(int x, int y) {
    // The operands are converted with the single-word `uint`, which works in both languages: GLSL
    // rejects the C-style `(unsigned int)x`, and GCC rejects the functional `unsigned int(x)` because
    // it parses a two-word type name followed by a parenthesised expression as a declaration.
    unsigned int mixed = myrenderer_cloud_hash(
        uint(x) * 0x9e3779b9U
        ^ myrenderer_cloud_hash(uint(y) * 0x85ebca6bU));
    return float(mixed & 0xffffffU) / 16777216.0;
}

// Tileable Worley (cellular) noise on a `period` x `period` lattice, with a decorrelating salt.
//
// Two details carry the whole tiling contract, and both were found by measurement rather than by
// reading:
//   - The feature point is built from the *wrapped* cell coordinate plus where the sample sits
//     inside its cell, never from the absolute position. Otherwise `worley(x + period, y)` and
//     `worley(x, y)` differ by the rounding that adding a large offset to a large coordinate
//     introduces (about 1e-7), and the same cloud viewed from two places is two slightly different
//     values.
//   - The bias before the modulo is not decoration. C-style `%` truncates towards zero, so a bare
//     modulo maps -1 and 3 to -1 and 3 for a period of 4 instead of to the same cell, and the two
//     halves of a period would not match.
//
// The salt is applied *after* the wrap and only inside the hash, so a salted field tiles exactly
// like an unsalted one and different salts give independent fields on the same lattice. That is what
// lets the weather map's three channels be three different maps rather than three views of one.
// GLSL 3.30 leaves a negative operand's integer remainder undefined. Normalize the sign before
// taking `%`, then restore the Euclidean remainder. Works for arbitrary periods, not just powers
// of two where a driver's bit-mask optimization happened to hide the problem.
MYRENDERER_CLOUD_INLINE int myrenderer_cloud_wrap(int value, int period) {
    int magnitude = value < 0 ? -value : value;
    int remainder = magnitude % period;
    return value < 0 && remainder != 0 ? period - remainder : remainder;
}

MYRENDERER_CLOUD_INLINE float myrenderer_cloud_worley_salted(
    float x, float y, int period, int salt)
{
    int safePeriod = period < 1 ? 1 : period;
    float cellX = floor(x);
    float cellY = floor(y);
    int cellIndexX = int(cellX);
    int cellIndexY = int(cellY);
    const int modulusBias = 1048576;
    int wrappedCellX = myrenderer_cloud_wrap(cellIndexX + modulusBias, safePeriod);
    int wrappedCellY = myrenderer_cloud_wrap(cellIndexY + modulusBias, safePeriod);
    float fractionX = x - cellX;
    float fractionY = y - cellY;
    int saltX = salt * 131;
    int saltY = salt * 197;

    float best = 2.0;
    // The nearest feature may live in any of the eight neighbouring cells. Searching only the
    // current and positive neighbours stamps the lattice's left and lower boundaries into the
    // result, which appears as a repeating checker pattern after ray marching.
    for (int offsetY = -1; offsetY <= 1; ++offsetY) {
        for (int offsetX = -1; offsetX <= 1; ++offsetX) {
            int wrappedX = myrenderer_cloud_wrap(wrappedCellX + offsetX, safePeriod);
            int wrappedY = myrenderer_cloud_wrap(wrappedCellY + offsetY, safePeriod);
            float featureX = float(offsetX) + 0.5
                + (myrenderer_cloud_hash_unit(wrappedX + saltX, wrappedY + saltY) - 0.5) * 0.8;
            float featureY = float(offsetY) + 0.5
                + (myrenderer_cloud_hash_unit(wrappedY + saltY + 101, wrappedX + saltX + 57)
                    - 0.5) * 0.8;
            float deltaX = fractionX - featureX;
            float deltaY = fractionY - featureY;
            float distance = sqrt(deltaX * deltaX + deltaY * deltaY);
            best = distance < best ? distance : best;
        }
    }
    // Round cells: 1 at the feature point, falling to 0 at the cell corner. A squared distance
    // would give a diamond instead of the blobby silhouette a cloud needs.
    float value = 1.0 - best / 0.7071068;
    return clamp(value, 0.0, 1.0);
}

MYRENDERER_CLOUD_INLINE float myrenderer_cloud_worley(float x, float y, int period) {
    return myrenderer_cloud_worley_salted(x, y, period, 0);
}

// The base shape: the two octaves that decide *where* clouds are.
//
// Every octave's lattice is a whole-number multiple of the base one, so the sum repeats exactly when
// the base field does. The weights are the C1 ones renormalised over the two octaves that remain
// after the finest octave moved out to `myrenderer_cloud_detail_shape`; the field's median stays near
// 0.4 and its upper decile near 0.7, so a coverage in 0.2..0.8 sweeps the sky from mostly clear to
// mostly covered.
//
// Unrolled by hand because GLSL 3.30 will not index an array with a loop variable.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_base_shape_salted(
    float tileX, float tileY, int noisePeriod, int salt)
{
    int period = noisePeriod < 1 ? 1 : noisePeriod;
    float first = myrenderer_cloud_worley_salted(
        tileX * 4.0, tileY * 4.0, period * 4, salt);
    float second = myrenderer_cloud_worley_salted(
        tileX * 8.0, tileY * 8.0, period * 8, salt + 31);
    return first * 0.706 + second * 0.294;
}

MYRENDERER_CLOUD_INLINE float myrenderer_cloud_base_shape(
    float tileX, float tileY, int noisePeriod)
{
    return myrenderer_cloud_base_shape_salted(tileX, tileY, noisePeriod, 0);
}

// The detail octaves: the fine structure *inside* a cloud.
//
// These are the octaves that made the C2 layer read as texture instead of as clouds. Summed into the
// shape used for the coverage threshold they put their own small blobs wherever the sum sat near the
// contour, so the layer's silhouette was a filigree: measured on the reference marcher at
// base 3200 / top 8000 / feature 6400, the best parameter combination available in C2 gave a mask
// with nine regions whose boundary density was 0.369 -- over a third of every covered pixel touched
// the edge. Feeding them in through `myrenderer_cloud_density`'s cone weight instead, so they can
// only remove density from the middle of an already-formed cloud, is the C5 fix.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_detail_shape_salted(
    float tileX, float tileY, int noisePeriod, int salt)
{
    int period = noisePeriod < 1 ? 1 : noisePeriod;
    float fine = myrenderer_cloud_worley_salted(
        tileX * 16.0, tileY * 16.0, period * 16, salt);
    float finer = myrenderer_cloud_worley_salted(
        tileX * 32.0, tileY * 32.0, period * 32, salt + 47);
    return fine * 0.6 + finer * 0.4;
}

MYRENDERER_CLOUD_INLINE float myrenderer_cloud_detail_shape(
    float tileX, float tileY, int noisePeriod)
{
    return myrenderer_cloud_detail_shape_salted(tileX, tileY, noisePeriod, 0);
}

// Height-dependent domain warp for the nominally horizontal shape field. Sampling exactly the same
// 2D coordinates at every height extrudes each cell through the full slab; from below those columns
// project as repeated vertical curtains. Moving the horizontal domain continuously with altitude
// gives the volume changing cross-sections while retaining exact horizontal tiling. A cubic bend is
// used instead of trigonometry so the CPU and GPU keep the shared field within the existing strict
// parity tolerance.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_height_warp_x(float taper) {
    float t = clamp(taper, 0.0, 1.0);
    float curve = t * t * (3.0 - 2.0 * t);
    float bend = t * (1.0 - t);
    return curve * 0.73 + bend * 3.4 + (t - 0.5) * bend * 1.8;
}

MYRENDERER_CLOUD_INLINE float myrenderer_cloud_height_warp_y(float taper) {
    float t = clamp(taper, 0.0, 1.0);
    float curve = t * t * (3.0 - 2.0 * t);
    float bend = t * (1.0 - t);
    return -curve * 0.51 - bend * 2.7 + (t - 0.5) * bend * 2.2;
}

// True 3D cellular field. All 27 neighbours participate in the nearest-point search, so no cell
// boundary is visible and vertical changes are as continuous as horizontal changes. The height is
// hashed independently; a 2D field shifted through height still produces aligned cloud curtains.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_worley3(
    float x, float y, float z, int noisePeriod, int salt)
{
    int period = noisePeriod < 1 ? 1 : noisePeriod;
    float cellX = floor(x);
    float cellY = floor(y);
    float cellZ = floor(z);
    int wrappedX = myrenderer_cloud_wrap(int(cellX), period);
    int wrappedY = myrenderer_cloud_wrap(int(cellY), period);
    int wrappedZ = myrenderer_cloud_wrap(int(cellZ), period);
    float fractionX = x - cellX;
    float fractionY = y - cellY;
    float fractionZ = z - cellZ;
    float bestSquared = 4.0f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int ix = (wrappedX + dx + period) % period;
                int iy = (wrappedY + dy + period) % period;
                int iz = (wrappedZ + dz + period) % period;
                int verticalSalt = iz * 977 + salt * 131;
                float px = float(dx) + 0.5f
                    + (myrenderer_cloud_hash_unit(ix + verticalSalt, iy + 17) - 0.5f) * 0.8f;
                float py = float(dy) + 0.5f
                    + (myrenderer_cloud_hash_unit(iy + verticalSalt + 101, ix + 57) - 0.5f) * 0.8f;
                float pz = float(dz) + 0.5f
                    + (myrenderer_cloud_hash_unit(ix + 313, iy + verticalSalt + 419) - 0.5f) * 0.8f;
                float deltaX = fractionX - px;
                float deltaY = fractionY - py;
                float deltaZ = fractionZ - pz;
                float distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
                bestSquared = min(bestSquared, distanceSquared);
            }
        }
    }
    return clamp(1.0f - sqrt(bestSquared) / 0.8660254f, 0.0f, 1.0f);
}

// Feature scale now describes the large cloud body, rather than four small cells per feature.
// Only the lower octave owns the silhouette; the second adds broad variation without stamping fine
// cellular texture into the edge. Horizontal repeats still occur at featureScale * noisePeriod.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_gradient3(
    int x, int y, int z, int period, float dx, float dy, float dz) {
    uint hash = myrenderer_cloud_hash(uint(myrenderer_cloud_wrap(x, period)) * 0x9e3779b9U
        ^ uint(myrenderer_cloud_wrap(y, period)) * 0x85ebca6bU
        ^ uint(myrenderer_cloud_wrap(z, period)) * 0xc2b2ae35U);
    int h = int(hash & 15U);
    float u = h < 8 ? dx : dy;
    float v = h < 4 ? dy : ((h == 12 || h == 14) ? dx : dz);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_fade(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}
// The gradient noise already used by the noise-v1 generator; unrolled corners
// let the reference and GLSL evaluate the same field without dialect arrays.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_perlin3(float x, float y, float z, int period) {
    int ix = int(floor(x)), iy = int(floor(y)), iz = int(floor(z));
    float fx = x - float(ix), fy = y - float(iy), fz = z - float(iz);
    float sx = myrenderer_cloud_fade(fx), sy = myrenderer_cloud_fade(fy), sz = myrenderer_cloud_fade(fz);
    return myrenderer_cloud_lerp(
        myrenderer_cloud_lerp(
            myrenderer_cloud_lerp(myrenderer_cloud_gradient3(ix,iy,iz,period,fx,fy,fz),
                myrenderer_cloud_gradient3(ix+1,iy,iz,period,fx-1.0,fy,fz),sx),
            myrenderer_cloud_lerp(myrenderer_cloud_gradient3(ix,iy+1,iz,period,fx,fy-1.0,fz),
                myrenderer_cloud_gradient3(ix+1,iy+1,iz,period,fx-1.0,fy-1.0,fz),sx),sy),
        myrenderer_cloud_lerp(
            myrenderer_cloud_lerp(myrenderer_cloud_gradient3(ix,iy,iz+1,period,fx,fy,fz-1.0),
                myrenderer_cloud_gradient3(ix+1,iy,iz+1,period,fx-1.0,fy,fz-1.0),sx),
            myrenderer_cloud_lerp(myrenderer_cloud_gradient3(ix,iy+1,iz+1,period,fx,fy-1.0,fz-1.0),
                myrenderer_cloud_gradient3(ix+1,iy+1,iz+1,period,fx-1.0,fy-1.0,fz-1.0),sx),sy),sz);
}
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_perlin_shape(float x, float y, float z, int period) {
    return clamp(0.5 + (myrenderer_cloud_perlin3(x,y,z,period) * 0.5714286
        + myrenderer_cloud_perlin3(x*2.0,y*2.0,z*2.0,period*2) * 0.2857143
        + myrenderer_cloud_perlin3(x*4.0,y*4.0,z*4.0,period*4) * 0.1428571) * 0.5, 0.0, 1.0);
}

MYRENDERER_CLOUD_INLINE float myrenderer_cloud_volume_base_shape(
    float tileX, float tileY, float taper, int noisePeriod)
{
    float vertical = clamp(taper, 0.0, 1.0) * 2.0f;
    float first = myrenderer_cloud_worley3(tileX, tileY, vertical, noisePeriod, 101);
    float second = myrenderer_cloud_worley3(
        tileX * 2.0f, tileY * 2.0f, vertical * 2.0f, noisePeriod * 2, 154);
    return first * 0.85f + second * 0.15f;
}

MYRENDERER_CLOUD_INLINE float myrenderer_cloud_volume_detail_shape(
    float tileX, float tileY, float taper, int noisePeriod)
{
    float vertical = clamp(taper, 0.0, 1.0) * 2.0f;
    return myrenderer_cloud_worley3(
        tileX * 4.0f, tileY * 4.0f, vertical * 4.0f, noisePeriod * 4, 307);
}

// The 2D weather map (P1-A slice 6, step C5): red is coverage, green is cloud type, blue is height.
//
// It is evaluated procedurally rather than sampled from a texture, and that is a correctness choice
// rather than a shortcut. A texture would have to be filtered; GL leaves bilinear filtering's
// precision implementation-defined, so the GPU's filtered value and the CPU reference's would differ
// in the low bits and the CPU/GPU field parity this project measures -- the integer hash is
// currently bit-identical -- would degrade to a tolerance argument. A pure function of position has
// no such seam. The cost is that the map cannot be painted by hand; `MyRendererCloudWeatherMap`
// exports exactly this field to a hashable RGB asset so it can at least be *inspected* and compared
// offline, and authored-map import is recorded as a follow-up in docs/cloud-layer-c1.md.
//
// Two low-frequency octaves per channel on a lattice a whole factor coarser than the base shape, so
// the weather varies between cloud masses rather than within one. Channels are separated by salt.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_weather(float tileX, float tileY, int channel) {
    int salt = 1 + channel * 7;
    float coarse = myrenderer_cloud_worley_salted(tileX * 2.0, tileY * 2.0, 2, salt);
    float fine = myrenderer_cloud_worley_salted(tileX * 4.0, tileY * 4.0, 4, salt + 53);
    return clamp(coarse * 0.7 + fine * 0.3, 0.0, 1.0);
}

// Double-lobed Henyey-Greenstein phase function. Forward scattering gives the silver lining where
// the layer sits between the camera and the sun; the weaker backward lobe keeps the anti-solar side
// from going flat. Normalised by the isotropic value so it averages about 1, which is what lets the
// cloud's brightness be reasoned about separately from its shape.
//
// This is an approximation, not a Mie solution: the analytic sky uses its own Mie phase, so the two
// models are not strictly consistent. Recorded in docs/cloud-layer-c1.md.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_phase(float cosViewSun) {
    const float forwardG = 0.8;
    const float backwardG = -0.3;
    const float backwardBlend = 0.5;
    const float pi = 3.14159265358979323846;
    float clamped = clamp(cosViewSun, -1.0, 1.0);
    float smallest = 1.0e-6;

    float forwardG2 = forwardG * forwardG;
    float forwardDenominator = 1.0 + forwardG2 - 2.0 * forwardG * clamped;
    float forward = (1.0 - forwardG2) / (4.0 * pi
        * max(forwardDenominator * sqrt(max(forwardDenominator, smallest)), smallest));

    float backwardG2 = backwardG * backwardG;
    float backwardDenominator = 1.0 + backwardG2 - 2.0 * backwardG * clamped;
    float backward = (1.0 - backwardG2) / (4.0 * pi
        * max(backwardDenominator * sqrt(max(backwardDenominator, smallest)), smallest));

    return (forward * (1.0 - backwardBlend) + backward * backwardBlend) * 4.0 * pi;
}

// The layer's vertical density profile across the slab. `taper` is the position in 0..1 from the
// base to the top and `type` blends the two shapes the presets need:
//   - 1 (cumulus) rises quickly off the base and thins gradually towards the top, the "clouds are
//     wider at the bottom" rule from the reference material.
//   - 0 (stratus) is a broad flat deck: a softer underside and a faster lid.
// The edges are interpolated rather than switched, so the weather map's green channel can move a
// region continuously between the two instead of stamping a hard boundary across the sky.
//
// A taper outside 0..1 -- which the height channel produces when it slides the profile out of the
// slab -- returns zero, because both smoothsteps clamp to their flat ends there.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_profile(float taper, float type) {
    float t = clamp(taper, 0.0, 1.0);
    float convection = clamp(type, 0.0, 1.0);
    float layering = 1.0 - convection;
    float riseEdge = 0.18 + layering * 0.12;
    float fallStart = 0.55 + layering * 0.20;
    float fallEnd = 1.0 - layering * 0.05;
    float rise = smoothstep(0.0, riseEdge, t);
    float fall = 1.0 - smoothstep(fallStart, fallEnd, t);
    return rise * fall;
}

// The multiple-scattering approximation (P1-A slice 6, step C3).
//
// Single scattering alone leaves a cloud's interior and its shaded side far too dark, because it
// counts only the light that reaches a sample directly from the sun. The light that has already
// bounced inside the cloud is what fills that in, and the standard approximation is to evaluate the
// phase function a few more times per sample, each time at a wider effective lobe and with the
// energy reduced by a fixed factor. That is Hillaire's octave approximation; the brief records it as
// the preferred cheap form, with a precomputed LUT as an optional later path.
//
// The weights are a geometric series that sums to one, so the total scattered energy is unchanged by
// adding octaves: this redistributes light from the single-scatter lobe into a diffuse fill rather
// than brightening the layer. Brightness is the calibrated scales' job, and mixing the two would
// mean re-calibrating every time the octave count moved.
//
// `attenuation` is the brief's `a` in 0.5..0.7 and `octaves` its octave count.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_multi_phase(
    float cosViewSun, int octaves, float attenuation, float eccentricity)
{
    float clamped = clamp(cosViewSun, -1.0, 1.0);
    float a = clamp(attenuation, 0.0, 0.99);
    int count = octaves < 1 ? 1 : octaves;
    float smallest = 1.0e-6;
    float pi = 3.14159265358979323846;

    float total = 0.0;
    float weightSum = 0.0;
    float weight = 1.0 - a;
    float g = eccentricity;
    for (int octave = 0; octave < 8; ++octave) {
        if (octave >= count) break;
        // Each successive octave is flatter: a photon that has scattered more times has forgotten
        // the original direction. `eccentricity` is how fast that happens.
        float g2 = g * g;
        float denominator = 1.0 + g2 - 2.0 * g * clamped;
        // The numerator is `1 - g2`; the `(1 + g2)` term belongs to the denominator's *expansion*,
        // not to the numerator. Writing `1 + g2 - 2*g*cos` as the numerator as well makes the two
        // cancel whenever `cos = 1` and `g = 0.6`, which collapses the whole phase function to a
        // constant `1 / (4*pi)`. This test caught exactly that: a term that is constant over the
        // sphere integrates to `1 / (4*pi)`, not to 1, and the "fill" it was supposed to add was a
        // bare rescaling of the cloud's brightness by 0.4.
        float phase = (1.0 - g2) / (4.0 * pi
            * max(denominator * sqrt(max(denominator, smallest)), smallest));
        total += phase * weight;
        weightSum += weight;
        weight *= a;
        g *= eccentricity;
    }
    // `weight = (1 - a) * a^n` is a proper geometric distribution: the weights sum to one for any
    // octave count, so the result stays a phase function with mean 1 over the sphere. The first
    // revision used `a^n` and divided by the weight sum, which happens to be `1 / (1 - a)` -- that
    // normalises the *shape* but leaves the integral at `1 - a`, quietly scaling the cloud's
    // brightness by 0.4 while claiming to add a fill.
    //
    // The raw Henyey-Greenstein form integrates to one over the sphere; this project's convention is
    // that a phase function is *normalised by the isotropic value*, so it averages about 1 and can be
    // reasoned about separately from brightness. That is the `4 * pi` here, matching
    // `myrenderer_cloud_phase`. Leaving it out scaled the fill by `1 / (4*pi)` -- a factor of 79 that
    // the calibration test caught and no amount of image editing would have explained.
    return weightSum > 0.0 ? (total / weightSum) * 4.0 * pi : 0.0;
}

// The powder effect: the darkening of a cloud's thin edges where light passes through without being
// multiply scattered. `density` is the local density and `distance` the step it was sampled over.
// Without it a cloud's silhouette glows, which is the opposite of what a real one does.
// Height lighting estimates the ambient path through the slab without adding a
// second density march. It is bounded and deliberately separate from extinction.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_ambient_weight(
    float height, float baseHeight, float topHeight, bool heightLighting) {
    if (!heightLighting) return 1.0;
    float h = clamp((height - baseHeight) / max(topHeight - baseHeight, 1.0e-3), 0.0, 1.0);
    return 0.12 + 0.88 * h * h;
}

// Squared interval endpoints concentrate the unchanged solar sample budget
// near the scattering point. Every interval retains its actual world length.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_light_fraction(
    float fraction, bool heightLighting) {
    return heightLighting ? fraction * fraction : fraction;
}

MYRENDERER_CLOUD_INLINE float myrenderer_cloud_powder(float density, float distance) {
    float opticalDepth = max(density, 0.0) * max(distance, 0.0);
    return 1.0 - exp(-2.0 * opticalDepth);
}

// The layer's normalised density at a world position. Wind is a world-unit translation of the
// horizontal plane, which is what makes advection reproducible from a frame number alone.
//
// The construction, in order, and why each step is where it is:
//   1. The weather map's three channels resolve to a local coverage, a local cloud type and a local
//      vertical offset. They are evaluated on a lattice coarser than the clouds so they describe
//      masses of cloud rather than individual ones.
//   2. The profile turns the vertical position into a density envelope, with the height channel
//      sliding it up or down inside the slab.
//   3. The *base* shape is thresholded by the local coverage. Everything that decides the silhouette
//      happens here, on two octaves only.
//   4. The *detail* octaves subtract from the result, weighted by a cone that is zero at both ends of
//      `baseCloud`. That weight is the whole point: a detail octave that reaches the contour
//      thresholds into its own blobs and the layer becomes texture. Confining it to the interior
//      means it can add shape to a cloud but cannot invent or delete one.
//
// `myrenderer_cloud_layer_profile` is step 2 on its own. The C1 analytic layer needs the profile
// separately so it can divide it back out and apply the slab crossing as an explicit thickness term
// instead; sharing the function keeps the slide and the type blend from being transcribed there and
// drifting.
MYRENDERER_CLOUD_INLINE float myrenderer_cloud_layer_profile(
    float worldX, float worldY, float worldZ, MyRendererCloudParams layer)
{
    float slab = max(layer.topHeight - layer.baseHeight, 1.0e-3);
    float taper = (worldY - layer.baseHeight) / slab;
    if (taper < 0.0 || taper > 1.0) return 0.0;

    float weatherSpan = max(layer.weatherScale, 1.0e-3);
    float weatherX = (worldX + layer.windX) / weatherSpan;
    float weatherY = (worldZ + layer.windZ) / weatherSpan;
    float weatherType = layer.typeVariation != 0.0
        ? myrenderer_cloud_weather(weatherX, weatherY, 1) : 0.5;
    float cloudType = clamp(
        layer.cloudType + (weatherType - 0.5) * layer.typeVariation, 0.0, 1.0);

    // Blue is height: a vertical slide of the profile within the fixed slab. The slab itself stays
    // where the parameters put it -- the march's span, the sun march and the analytic layer all
    // intersect it once from the same numbers, and a per-sample slab would make every one of them
    // approximate. Sliding the profile is what a bumpy cloud base actually looks like anyway.
    float weatherHeight = layer.heightVariation != 0.0
        ? myrenderer_cloud_weather(weatherX, weatherY, 2) : 0.5;
    float slid = taper - (weatherHeight - 0.5) * layer.heightVariation;
    return myrenderer_cloud_profile(slid, cloudType);
}

MYRENDERER_CLOUD_INLINE float myrenderer_cloud_density(
    float worldX, float worldY, float worldZ, MyRendererCloudParams layer)
{
    float profile = myrenderer_cloud_layer_profile(worldX, worldY, worldZ, layer);
    if (profile <= 0.0) return 0.0;

    float scale = max(layer.featureScale, 1.0e-3);
    float tileX = (worldX + layer.windX) / scale;
    float tileY = (worldZ + layer.windZ) / scale;
    float slab = max(layer.topHeight - layer.baseHeight, 1.0e-3);
    float taper = (worldY - layer.baseHeight) / slab;
    tileX += myrenderer_cloud_height_warp_x(taper) * 0.15f;
    tileY += myrenderer_cloud_height_warp_y(taper) * 0.15f;
    float weatherSpan = max(layer.weatherScale, 1.0e-3);
    // The weather map advects with the same world offset as the clouds, so a wind change moves the
    // masses and the clouds inside them together instead of sliding one through the other.
    float weatherX = (worldX + layer.windX) / weatherSpan;
    float weatherY = (worldZ + layer.windZ) / weatherSpan;

    // Red is coverage, centred on 0.5 so the layer parameter stays the *mean* coverage and the map
    // only redistributes it. A map that multiplied coverage instead would make the layer parameter
    // stop being the thing the calibration tables are indexed by.
    float weatherCoverage = myrenderer_cloud_weather(weatherX, weatherY, 0);
    float coverage = clamp(
        layer.coverage + (weatherCoverage - 0.5) * layer.coverageVariation, 0.0, 1.0);
    if (coverage <= 0.0) return 0.0;

    float baseShape;
    if (layer.offlineNoise) {
        float vertical = clamp(taper, 0.0, 1.0) * 2.0;
        baseShape = myrenderer_cloud_offline_sample(tileX,tileY,vertical,0) * 0.85
            + myrenderer_cloud_offline_sample(tileX,tileY,vertical,1) * 0.15;
    } else {
        baseShape = myrenderer_cloud_volume_base_shape(tileX,tileY,taper,layer.noisePeriod);
    }
    // Perlin modulation breaks uniform cellular domes; alpha in the canonical
    // offline volume already stores the same gradient FBM. Old inputs use zero.
    if (layer.shapeBlend > 0.0) {
        float vertical = clamp(taper, 0.0, 1.0) * 2.0;
        float perlin = layer.offlineNoise
            ? myrenderer_cloud_offline_sample(tileX,tileY,vertical,3)
            : myrenderer_cloud_perlin_shape(tileX,tileY,vertical,layer.noisePeriod);
        baseShape = clamp(baseShape + (perlin - 0.5) * 0.8 * clamp(layer.shapeBlend,0.0,1.0),0.0,1.0);
    }
    baseShape *= profile;
    // Coverage is a threshold on the base shape, remapped so the parameter reads as the fraction
    // covered rather than as the threshold itself.
    float threshold = 1.0 - coverage;
    float baseCloud = clamp((baseShape - threshold) / coverage, 0.0, 1.0);
    if (baseCloud <= 0.0) return 0.0;

    float detail;
    if (layer.offlineNoise) {
        detail = myrenderer_cloud_offline_sample(tileX,tileY,clamp(taper,0.0,1.0)*2.0,2);
    } else {
        detail = myrenderer_cloud_volume_detail_shape(tileX,tileY,taper,layer.noisePeriod);
    }
    float cone = baseCloud * (1.0 - baseCloud) * 4.0;
    float edge = clamp(layer.detailEdge, 0.0, 1.0);
    float weight = cone + (1.0 - cone) * edge;
    float density = baseCloud - detail * weight * clamp(layer.detailStrength, 0.0, 1.0);
    return clamp(density, 0.0, 1.0) * max(layer.densityScale, 0.0);
}

#endif // MYRENDERER_CLOUD_FIELD_INCLUDED
