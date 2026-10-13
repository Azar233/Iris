#include "module/ParameterRegistry.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr std::size_t invalidParameterIndex = static_cast<std::size_t>(-1);

bool isFiniteColor(const glm::vec3& color) {
    return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z);
}

float clampChannel(float value, double minimum, double maximum) {
    return static_cast<float>(std::clamp(
        static_cast<double>(value), minimum, maximum
    ));
}

// Empty filters and empty paths mean "unset"; otherwise the path must end with one
// of the ';'-separated extensions.
bool matchesAssetFilter(const std::string& path, const std::string& filter) {
    if (filter.empty() || path.empty()) return true;
    std::size_t begin = 0U;
    while (begin <= filter.size()) {
        const std::size_t end = filter.find(';', begin);
        const std::string extension = filter.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin
        );
        if (!extension.empty() && path.size() >= extension.size()
            && path.compare(path.size() - extension.size(), extension.size(), extension) == 0) {
            return true;
        }
        if (end == std::string::npos) break;
        begin = end + 1U;
    }
    return false;
}

} // namespace

const char* moduleParameterTypeName(ModuleParameterType type) {
    switch (type) {
        case ModuleParameterType::Bool: return "Bool";
        case ModuleParameterType::Int: return "Int";
        case ModuleParameterType::Float: return "Float";
        case ModuleParameterType::Color: return "Color";
        case ModuleParameterType::Enum: return "Enum";
        case ModuleParameterType::Asset: return "Asset";
    }
    return "Unknown";
}

void ParameterRegistry::registerBool(
    const std::string& id,
    const std::string& displayName,
    bool defaultValue,
    const std::string& tooltip
) {
    ModuleParameterDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = displayName;
    descriptor.tooltip = tooltip;
    descriptor.type = ModuleParameterType::Bool;
    descriptor.minimum = 0.0;
    descriptor.maximum = 1.0;
    ModuleParameterValue value;
    value.type = ModuleParameterType::Bool;
    value.boolean = defaultValue;
    descriptors_.push_back(std::move(descriptor));
    values_.push_back(value);
    defaults_.push_back(value);
}

void ParameterRegistry::registerInt(
    const std::string& id,
    const std::string& displayName,
    int defaultValue,
    int minimum,
    int maximum,
    const std::string& tooltip
) {
    ModuleParameterDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = displayName;
    descriptor.tooltip = tooltip;
    descriptor.type = ModuleParameterType::Int;
    descriptor.minimum = static_cast<double>(std::min(minimum, maximum));
    descriptor.maximum = static_cast<double>(std::max(minimum, maximum));
    ModuleParameterValue value;
    value.type = ModuleParameterType::Int;
    value.integer = static_cast<int>(std::clamp(
        static_cast<double>(defaultValue), descriptor.minimum, descriptor.maximum
    ));
    descriptors_.push_back(std::move(descriptor));
    values_.push_back(value);
    defaults_.push_back(value);
}

void ParameterRegistry::registerFloat(
    const std::string& id,
    const std::string& displayName,
    float defaultValue,
    float minimum,
    float maximum,
    const std::string& tooltip
) {
    ModuleParameterDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = displayName;
    descriptor.tooltip = tooltip;
    descriptor.type = ModuleParameterType::Float;
    descriptor.minimum = std::min(static_cast<double>(minimum), static_cast<double>(maximum));
    descriptor.maximum = std::max(static_cast<double>(minimum), static_cast<double>(maximum));
    ModuleParameterValue value;
    value.type = ModuleParameterType::Float;
    value.number = std::isfinite(defaultValue)
        ? clampChannel(defaultValue, descriptor.minimum, descriptor.maximum)
        : 0.0f;
    descriptors_.push_back(std::move(descriptor));
    values_.push_back(value);
    defaults_.push_back(value);
}

void ParameterRegistry::registerColor(
    const std::string& id,
    const std::string& displayName,
    const glm::vec3& defaultValue,
    const std::string& tooltip,
    double minimum, double maximum
) {
    ModuleParameterDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = displayName;
    descriptor.tooltip = tooltip;
    descriptor.type = ModuleParameterType::Color;
    descriptor.minimum = std::min(minimum, maximum);
    descriptor.maximum = std::max(minimum, maximum);
    ModuleParameterValue value;
    value.type = ModuleParameterType::Color;
    value.color = isFiniteColor(defaultValue)
        ? glm::vec3(
            clampChannel(defaultValue.x, descriptor.minimum, descriptor.maximum),
            clampChannel(defaultValue.y, descriptor.minimum, descriptor.maximum),
            clampChannel(defaultValue.z, descriptor.minimum, descriptor.maximum)
        )
        : glm::vec3(0.0f);
    descriptors_.push_back(std::move(descriptor));
    values_.push_back(value);
    defaults_.push_back(value);
}

void ParameterRegistry::registerEnum(
    const std::string& id,
    const std::string& displayName,
    std::vector<std::string> labels,
    int defaultIndex,
    const std::string& tooltip
) {
    ModuleParameterDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = displayName;
    descriptor.tooltip = tooltip;
    descriptor.type = ModuleParameterType::Enum;
    descriptor.enumLabels = std::move(labels);
    descriptor.minimum = 0.0;
    descriptor.maximum = descriptor.enumLabels.empty()
        ? 0.0
        : static_cast<double>(descriptor.enumLabels.size() - 1U);
    ModuleParameterValue value;
    value.type = ModuleParameterType::Enum;
    value.integer = descriptor.enumLabels.empty()
        ? 0
        : static_cast<int>(std::clamp(
            static_cast<double>(defaultIndex), 0.0, descriptor.maximum
        ));
    value.text = descriptor.enumLabels.empty()
        ? std::string()
        : descriptor.enumLabels[static_cast<std::size_t>(value.integer)];
    descriptors_.push_back(std::move(descriptor));
    values_.push_back(value);
    defaults_.push_back(value);
}

void ParameterRegistry::registerAsset(
    const std::string& id,
    const std::string& displayName,
    std::string assetExtensionFilter,
    const std::string& tooltip
) {
    ModuleParameterDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = displayName;
    descriptor.tooltip = tooltip;
    descriptor.type = ModuleParameterType::Asset;
    descriptor.assetExtensionFilter = std::move(assetExtensionFilter);
    ModuleParameterValue value;
    value.type = ModuleParameterType::Asset;
    descriptors_.push_back(std::move(descriptor));
    values_.push_back(value);
    defaults_.push_back(value);
}

bool ParameterRegistry::contains(const std::string& id) const {
    return descriptor(id) != nullptr;
}

const ModuleParameterDescriptor* ParameterRegistry::descriptor(const std::string& id) const {
    for (const ModuleParameterDescriptor& candidate : descriptors_) {
        if (candidate.id == id) return &candidate;
    }
    return nullptr;
}

const ModuleParameterValue* ParameterRegistry::value(const std::string& id) const {
    for (std::size_t index = 0U; index < descriptors_.size(); ++index) {
        if (descriptors_[index].id == id) return &values_[index];
    }
    return nullptr;
}

bool ParameterRegistry::isDefault(const std::string& id) const {
    for (std::size_t index = 0U; index < descriptors_.size(); ++index) {
        if (descriptors_[index].id != id) continue;
        const ModuleParameterValue& current = values_[index];
        const ModuleParameterValue& fallback = defaults_[index];
        return current.boolean == fallback.boolean
            && current.integer == fallback.integer
            && current.number == fallback.number
            && current.color == fallback.color
            && current.text == fallback.text;
    }
    return false;
}

std::size_t ParameterRegistry::indexOf(
    const std::string& id,
    ModuleParameterType expected,
    std::string& error
) const {
    for (std::size_t index = 0U; index < descriptors_.size(); ++index) {
        if (descriptors_[index].id != id) continue;
        if (descriptors_[index].type != expected) {
            error = "parameter '" + id + "' is " + moduleParameterTypeName(descriptors_[index].type)
                + ", not " + moduleParameterTypeName(expected);
            return invalidParameterIndex;
        }
        return index;
    }
    error = "unknown module parameter '" + id + "'";
    return invalidParameterIndex;
}

bool ParameterRegistry::setValue(
    const std::string& id,
    const ModuleParameterValue& value,
    std::string& error
) {
    const std::size_t index = indexOf(id, value.type, error);
    if (index == invalidParameterIndex) return false;
    const ModuleParameterDescriptor& descriptor = descriptors_[index];
    switch (descriptor.type) {
        case ModuleParameterType::Bool:
            values_[index].boolean = value.boolean;
            return true;
        case ModuleParameterType::Int:
            // The integer field is authoritative for an Int parameter.
            values_[index].integer = static_cast<int>(std::clamp(
                static_cast<double>(value.integer), descriptor.minimum, descriptor.maximum
            ));
            return true;
        case ModuleParameterType::Float:
            if (!std::isfinite(value.number)) {
                error = "parameter '" + id + "' must be a finite number";
                return false;
            }
            values_[index].number = clampChannel(
                value.number, descriptor.minimum, descriptor.maximum
            );
            return true;
        case ModuleParameterType::Color:
            if (!isFiniteColor(value.color)) {
                error = "parameter '" + id + "' must be a finite colour";
                return false;
            }
            values_[index].color = glm::vec3(
                clampChannel(value.color.x, descriptor.minimum, descriptor.maximum),
                clampChannel(value.color.y, descriptor.minimum, descriptor.maximum),
                clampChannel(value.color.z, descriptor.minimum, descriptor.maximum)
            );
            return true;
        case ModuleParameterType::Enum:
            if (descriptor.enumLabels.empty()) {
                error = "parameter '" + id + "' has no enum labels";
                return false;
            }
            values_[index].integer = static_cast<int>(std::clamp(
                static_cast<double>(value.integer), 0.0,
                static_cast<double>(descriptor.enumLabels.size() - 1U)
            ));
            values_[index].text = descriptor.enumLabels[
                static_cast<std::size_t>(values_[index].integer)
            ];
            return true;
        case ModuleParameterType::Asset: {
            const std::string path = value.text;
            if (!matchesAssetFilter(path, descriptor.assetExtensionFilter)) {
                error = "parameter '" + id + "' expects " + descriptor.assetExtensionFilter
                    + ", got '" + path + "'";
                return false;
            }
            values_[index].text = path;
            return true;
        }
    }
    error = "parameter '" + id + "' has an unsupported type";
    return false;
}

bool ParameterRegistry::setBool(const std::string& id, bool value, std::string& error) {
    ModuleParameterValue payload;
    payload.type = ModuleParameterType::Bool;
    payload.boolean = value;
    return setValue(id, payload, error);
}

bool ParameterRegistry::setInt(const std::string& id, int value, std::string& error) {
    ModuleParameterValue payload;
    payload.type = ModuleParameterType::Int;
    payload.integer = value;
    payload.number = static_cast<float>(value);
    return setValue(id, payload, error);
}

bool ParameterRegistry::setFloat(const std::string& id, float value, std::string& error) {
    ModuleParameterValue payload;
    payload.type = ModuleParameterType::Float;
    payload.number = value;
    return setValue(id, payload, error);
}

bool ParameterRegistry::setColor(const std::string& id, const glm::vec3& value, std::string& error) {
    ModuleParameterValue payload;
    payload.type = ModuleParameterType::Color;
    payload.color = value;
    return setValue(id, payload, error);
}

bool ParameterRegistry::setEnumIndex(const std::string& id, int index, std::string& error) {
    ModuleParameterValue payload;
    payload.type = ModuleParameterType::Enum;
    payload.integer = index;
    return setValue(id, payload, error);
}

bool ParameterRegistry::setEnumLabel(
    const std::string& id,
    const std::string& label,
    std::string& error
) {
    const std::size_t index = indexOf(id, ModuleParameterType::Enum, error);
    if (index == invalidParameterIndex) return false;
    const std::vector<std::string>& labels = descriptors_[index].enumLabels;
    const auto found = std::find(labels.begin(), labels.end(), label);
    if (found == labels.end()) {
        error = "parameter '" + id + "' has no enum label '" + label + "'";
        return false;
    }
    return setEnumIndex(id, static_cast<int>(std::distance(labels.begin(), found)), error);
}

bool ParameterRegistry::setAsset(
    const std::string& id,
    const std::string& path,
    std::string& error
) {
    ModuleParameterValue payload;
    payload.type = ModuleParameterType::Asset;
    payload.text = path;
    return setValue(id, payload, error);
}

bool ParameterRegistry::boolValue(const std::string& id, bool fallback) const {
    const ModuleParameterValue* current = value(id);
    return current != nullptr && current->type == ModuleParameterType::Bool
        ? current->boolean
        : fallback;
}

int ParameterRegistry::intValue(const std::string& id, int fallback) const {
    const ModuleParameterValue* current = value(id);
    return current != nullptr && current->type == ModuleParameterType::Int
        ? current->integer
        : fallback;
}

float ParameterRegistry::floatValue(const std::string& id, float fallback) const {
    const ModuleParameterValue* current = value(id);
    return current != nullptr && current->type == ModuleParameterType::Float
        ? current->number
        : fallback;
}

glm::vec3 ParameterRegistry::colorValue(const std::string& id, const glm::vec3& fallback) const {
    const ModuleParameterValue* current = value(id);
    return current != nullptr && current->type == ModuleParameterType::Color
        ? current->color
        : fallback;
}

std::string ParameterRegistry::enumLabel(const std::string& id, const std::string& fallback) const {
    const ModuleParameterValue* current = value(id);
    return current != nullptr && current->type == ModuleParameterType::Enum
        ? current->text
        : fallback;
}

std::string ParameterRegistry::assetValue(const std::string& id, const std::string& fallback) const {
    const ModuleParameterValue* current = value(id);
    return current != nullptr && current->type == ModuleParameterType::Asset
        ? current->text
        : fallback;
}

void ParameterRegistry::resetToDefaults() {
    values_ = defaults_;
}

std::uint64_t ParameterRegistry::fingerprint() const {
    constexpr std::uint64_t offsetBasis = 1469598103934665603ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    const auto mixBytes = [](std::uint64_t hash, const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0U; index < size; ++index) {
            hash ^= static_cast<std::uint64_t>(bytes[index]);
            hash *= prime;
        }
        return hash;
    };
    const auto mixString = [&mixBytes](std::uint64_t hash, const std::string& text) {
        hash = mixBytes(hash, text.data(), text.size());
        // A separator keeps "ab"+"c" distinct from "a"+"bc" across adjacent fields.
        const unsigned char separator = 0xFFU;
        return mixBytes(hash, &separator, 1U);
    };

    std::uint64_t hash = offsetBasis;
    for (std::size_t index = 0U; index < descriptors_.size(); ++index) {
        const ModuleParameterDescriptor& descriptor = descriptors_[index];
        const ModuleParameterValue& value = values_[index];
        hash = mixString(hash, descriptor.id);
        const int type = static_cast<int>(descriptor.type);
        hash = mixBytes(hash, &type, sizeof(type));
        const bool boolean = value.boolean;
        hash = mixBytes(hash, &boolean, sizeof(boolean));
        const int integer = value.integer;
        hash = mixBytes(hash, &integer, sizeof(integer));
        const float number = value.number;
        hash = mixBytes(hash, &number, sizeof(number));
        const float color[3]{value.color.x, value.color.y, value.color.z};
        hash = mixBytes(hash, color, sizeof(color));
        hash = mixString(hash, value.text);
    }
    return hash;
}

std::vector<ModuleParameterOverride> ParameterRegistry::overrides() const {
    std::vector<ModuleParameterOverride> result;
    for (std::size_t index = 0U; index < descriptors_.size(); ++index) {
        const ModuleParameterValue& current = values_[index];
        const ModuleParameterValue& fallback = defaults_[index];
        const bool same = current.boolean == fallback.boolean
            && current.integer == fallback.integer
            && current.number == fallback.number
            && current.color == fallback.color
            && current.text == fallback.text;
        if (same) continue;
        result.push_back(ModuleParameterOverride{descriptors_[index].id, current});
    }
    return result;
}

bool ParameterRegistry::applyOverrides(
    const std::vector<ModuleParameterOverride>& overrides,
    std::string& error
) {
    // Validate against a copy first so a rejected load cannot leave a partially
    // applied parameter set behind.
    std::vector<ModuleParameterValue> prepared = values_;
    for (const ModuleParameterOverride& entry : overrides) {
        std::size_t index = invalidParameterIndex;
        for (std::size_t candidate = 0U; candidate < descriptors_.size(); ++candidate) {
            if (descriptors_[candidate].id == entry.id) {
                index = candidate;
                break;
            }
        }
        if (index == invalidParameterIndex) {
            error = "unknown module parameter '" + entry.id + "'";
            return false;
        }
        if (descriptors_[index].type != entry.value.type) {
            error = "parameter '" + entry.id + "' expects "
                + moduleParameterTypeName(descriptors_[index].type) + ", got "
                + moduleParameterTypeName(entry.value.type);
            return false;
        }
        const ModuleParameterDescriptor& descriptor = descriptors_[index];
        ModuleParameterValue applied = entry.value;
        switch (descriptor.type) {
            case ModuleParameterType::Bool:
                break;
            case ModuleParameterType::Int:
                applied.integer = static_cast<int>(std::clamp(
                    static_cast<double>(applied.integer), descriptor.minimum, descriptor.maximum
                ));
                break;
            case ModuleParameterType::Float:
                if (!std::isfinite(applied.number)) {
                    error = "parameter '" + entry.id + "' must be a finite number";
                    return false;
                }
                applied.number = clampChannel(
                    applied.number, descriptor.minimum, descriptor.maximum
                );
                break;
            case ModuleParameterType::Color:
                if (!isFiniteColor(applied.color)) {
                    error = "parameter '" + entry.id + "' must be a finite colour";
                    return false;
                }
                applied.color = glm::vec3(
                    clampChannel(applied.color.x, descriptor.minimum, descriptor.maximum),
                    clampChannel(applied.color.y, descriptor.minimum, descriptor.maximum),
                    clampChannel(applied.color.z, descriptor.minimum, descriptor.maximum)
                );
                break;
            case ModuleParameterType::Enum:
                if (descriptor.enumLabels.empty()) {
                    error = "parameter '" + entry.id + "' has no enum labels";
                    return false;
                }
                applied.integer = static_cast<int>(std::clamp(
                    static_cast<double>(applied.integer), 0.0,
                    static_cast<double>(descriptor.enumLabels.size() - 1U)
                ));
                applied.text = descriptor.enumLabels[
                    static_cast<std::size_t>(applied.integer)
                ];
                break;
            case ModuleParameterType::Asset:
                if (!matchesAssetFilter(applied.text, descriptor.assetExtensionFilter)) {
                    error = "parameter '" + entry.id + "' expects "
                        + descriptor.assetExtensionFilter + ", got '" + applied.text + "'";
                    return false;
                }
                break;
        }
        prepared[index] = applied;
    }
    values_ = std::move(prepared);
    return true;
}

void ParameterRegistry::clear() {
    descriptors_.clear();
    values_.clear();
    defaults_.clear();
}
