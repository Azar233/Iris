#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// Lightweight parameter metadata for C++ scene / simulation modules.
//
// A module declares its parameters once in `registerParameters`; the Inspector
// then generates real controls from this metadata instead of a hard-coded widget
// list, and `.myscene` stores only the values that differ from the registered
// default. There is deliberately no generic reflection: the registry is the
// explicit schema.

enum class ModuleParameterType {
    Bool = 0,
    Int,
    Float,
    Color,
    Enum,
    Asset
};

const char* moduleParameterTypeName(ModuleParameterType type);

struct ModuleParameterDescriptor {
    std::string id;
    std::string displayName;
    std::string tooltip;
    ModuleParameterType type{ModuleParameterType::Float};
    // Inclusive range for Int / Float, and per-channel range for Color.
    double minimum{0.0};
    double maximum{1.0};
    std::vector<std::string> enumLabels;
    // ';'-separated extension list for Asset parameters, for example ".myscene;.gltf".
    std::string assetExtensionFilter;
};

struct ModuleParameterValue {
    ModuleParameterType type{ModuleParameterType::Float};
    bool boolean{false};
    int integer{0};
    float number{0.0f};
    glm::vec3 color{1.0f};
    // Asset path, or the selected Enum label.
    std::string text;
};

// One persisted override. `overrides()` reports only parameters whose current
// value differs from the registered default, so a scene round trip stays minimal
// and deterministic.
struct ModuleParameterOverride {
    std::string id;
    ModuleParameterValue value;
};

class ParameterRegistry {
public:
    void registerBool(
        const std::string& id,
        const std::string& displayName,
        bool defaultValue,
        const std::string& tooltip = {}
    );
    void registerInt(
        const std::string& id,
        const std::string& displayName,
        int defaultValue,
        int minimum,
        int maximum,
        const std::string& tooltip = {}
    );
    void registerFloat(
        const std::string& id,
        const std::string& displayName,
        float defaultValue,
        float minimum,
        float maximum,
        const std::string& tooltip = {}
    );
    void registerColor(
        const std::string& id,
        const std::string& displayName,
        const glm::vec3& defaultValue,
        const std::string& tooltip = {}
    );
    void registerEnum(
        const std::string& id,
        const std::string& displayName,
        std::vector<std::string> labels,
        int defaultIndex,
        const std::string& tooltip = {}
    );
    void registerAsset(
        const std::string& id,
        const std::string& displayName,
        std::string assetExtensionFilter,
        const std::string& tooltip = {}
    );

    std::size_t size() const { return descriptors_.size(); }
    bool contains(const std::string& id) const;
    // Registration order, so UI layout and serialization stay deterministic.
    const std::vector<ModuleParameterDescriptor>& descriptors() const { return descriptors_; }
    const std::vector<ModuleParameterValue>& values() const { return values_; }

    const ModuleParameterDescriptor* descriptor(const std::string& id) const;
    const ModuleParameterValue* value(const std::string& id) const;
    bool isDefault(const std::string& id) const;

    // Structural problems (unknown id, type mismatch, non-finite number) fail and
    // leave the registry untouched. Numeric values outside their declared range are
    // clamped into it, and an unknown enum index is clamped to a valid label, so a
    // hand-edited scene cannot make a parameter permanently unusable.
    bool setValue(const std::string& id, const ModuleParameterValue& value, std::string& error);
    bool setBool(const std::string& id, bool value, std::string& error);
    bool setInt(const std::string& id, int value, std::string& error);
    bool setFloat(const std::string& id, float value, std::string& error);
    bool setColor(const std::string& id, const glm::vec3& value, std::string& error);
    bool setEnumIndex(const std::string& id, int index, std::string& error);
    bool setEnumLabel(const std::string& id, const std::string& label, std::string& error);
    bool setAsset(const std::string& id, const std::string& path, std::string& error);

    bool boolValue(const std::string& id, bool fallback) const;
    int intValue(const std::string& id, int fallback) const;
    float floatValue(const std::string& id, float fallback) const;
    glm::vec3 colorValue(const std::string& id, const glm::vec3& fallback) const;
    std::string enumLabel(const std::string& id, const std::string& fallback = {}) const;
    std::string assetValue(const std::string& id, const std::string& fallback = {}) const;

    void resetToDefaults();
    std::vector<ModuleParameterOverride> overrides() const;

    // Deterministic fingerprint of every registered parameter and its current value, in
    // registration order. It is part of the simulation cache key: the per-frame content
    // hash cannot detect a parameter change, because a cached frame always hashes to the
    // value it recorded, so two runs with different parameter values would otherwise
    // share a cache entry.
    std::uint64_t fingerprint() const;

    // Transactional: every override is validated before any value changes, so a
    // rejected load cannot leave a half-applied parameter set behind.
    bool applyOverrides(
        const std::vector<ModuleParameterOverride>& overrides,
        std::string& error
    );

    void clear();

private:
    std::size_t indexOf(const std::string& id, ModuleParameterType expected,
                        std::string& error) const;

    std::vector<ModuleParameterDescriptor> descriptors_;
    std::vector<ModuleParameterValue> values_;
    std::vector<ModuleParameterValue> defaults_;
};
