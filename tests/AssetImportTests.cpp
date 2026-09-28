#include "asset/InputManifest.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "io/AssimpImporter.h"
#include "io/ModelImporter.h"
#include "io/ObjLoader.h"

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path asset(const char* name) {
    return std::filesystem::path(MYRENDERER_SOURCE_DIR) / "assets" / "models" / name;
}

bool hasScope(const ModelImportResult& result, ModelDiagnosticScope scope) {
    for (const ModelDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.scope == scope) {
            return true;
        }
    }
    return false;
}

const MaterialData* findMaterial(const ModelImportResult& result, const std::string& name) {
    for (const MaterialData& material : result.model.materials) {
        if (material.name == name) {
            return &material;
        }
    }
    return nullptr;
}

bool isClosedTriangleManifold(const MeshData& mesh) {
    if (mesh.indices.size() % 3U != 0U) return false;
    std::unordered_map<std::uint64_t, unsigned int> edgeUses;
    const auto addEdge = [&edgeUses](std::uint32_t left, std::uint32_t right) {
        const std::uint32_t minimum = std::min(left, right);
        const std::uint32_t maximum = std::max(left, right);
        const std::uint64_t key = (static_cast<std::uint64_t>(minimum) << 32U) | maximum;
        ++edgeUses[key];
    };
    for (std::size_t index = 0; index < mesh.indices.size(); index += 3U) {
        addEdge(mesh.indices[index], mesh.indices[index + 1U]);
        addEdge(mesh.indices[index + 1U], mesh.indices[index + 2U]);
        addEdge(mesh.indices[index + 2U], mesh.indices[index]);
    }
    return !edgeUses.empty() && std::all_of(
        edgeUses.begin(),
        edgeUses.end(),
        [](const auto& edge) { return edge.second == 2U; }
    );
}

} // namespace

int main() {
    try {
        ObjLoader obj;
        AssimpImporter assimp;

        const ModelImportResult cube = obj.load(asset("cube.obj"));
        require(cube.model.meshes.size() == 1U, "cube.obj should import one mesh");
        require(!cube.model.meshes.front().indices.empty(), "cube.obj should contain indices");

        const ModelImportResult materials = obj.load(asset("material_regression.obj"));
        require(materials.model.materials.size() == 3U, "material regression should import three materials");
        require(materials.model.textures.size() == 3U, "material regression should import three texture references");

        const ModelImportResult degenerateUv = obj.load(asset("degenerate_uv.obj"));
        require(hasScope(degenerateUv, ModelDiagnosticScope::Mesh), "degenerate UV should produce a mesh diagnostic");

        const ModelImportResult gltf = assimp.load(asset("textured_triangle.gltf"));
        require(!gltf.model.meshes.empty(), "glTF fixture should import a mesh");
        require(!gltf.model.textures.empty(), "glTF fixture should import its embedded texture");

        const ModelImportResult pbr = assimp.load(asset("pbr_material_test.gltf"));
        require(pbr.model.materials.size() >= 5U, "PBR fixture should import its five materials");
        require(pbr.model.meshes.size() == 5U, "PBR fixture should import five transformed meshes");
        bool hasPackedPbrTexture = false;
        bool hasRoughMaterial = false;
        bool hasMetalMaterial = false;
        for (const MaterialData& material : pbr.model.materials) {
            hasPackedPbrTexture |= material.metallicRoughnessTextureIndex >= 0;
            hasRoughMaterial |= material.roughnessFactor > 0.85f;
            hasMetalMaterial |= material.metallicFactor > 0.95f;
        }
        require(hasPackedPbrTexture, "PBR fixture should import the packed metallic-roughness texture");
        require(hasRoughMaterial, "PBR roughness factor should survive glTF import");
        require(hasMetalMaterial, "PBR metallic factor should survive glTF import");

        const ModelImportResult alpha = assimp.load(asset("alpha_material_test.gltf"));
        require(alpha.model.meshes.size() == 4U, "alpha fixture should import four meshes");
        const MaterialData* opaque = findMaterial(alpha, "OpaqueAlphaIgnored");
        const MaterialData* masked = findMaterial(alpha, "MaskedVisible");
        const MaterialData* blended = findMaterial(alpha, "BlendedFar");
        const MaterialData* blendedNear = findMaterial(alpha, "BlendedNear");
        require(opaque != nullptr, "alpha fixture should preserve its opaque material");
        require(masked != nullptr, "alpha fixture should preserve its masked material");
        require(blended != nullptr, "alpha fixture should preserve its blended material");
        require(blendedNear != nullptr, "alpha fixture should preserve its second blended material");
        require(opaque->alphaMode == MaterialAlphaMode::Opaque, "OPAQUE mode should survive glTF import");
        require(masked->alphaMode == MaterialAlphaMode::Mask, "MASK mode should survive glTF import");
        require(masked->alphaCutoff == 0.5f, "MASK cutoff should survive glTF import");
        require(blended->alphaMode == MaterialAlphaMode::Blend, "BLEND mode should survive glTF import");
        require(blended->doubleSided, "doubleSided should survive glTF import");
        require(blended->baseColorFactor.a < 0.5f, "BLEND alpha should survive glTF import");
        require(blendedNear->alphaMode == MaterialAlphaMode::Blend, "second BLEND mode should survive import");

        const ModelImportResult glass = assimp.load(asset("glass_material_test.gltf"));
        const MaterialData* dielectricGlass = findMaterial(glass, "DielectricGlass");
        require(dielectricGlass != nullptr, "glass fixture should preserve its material");
        require(
            dielectricGlass->alphaMode == MaterialAlphaMode::Opaque,
            "optical transmission should remain independent from alpha coverage"
        );
        require(
            dielectricGlass->transmissionFactor > 0.9f,
            "KHR_materials_transmission factor should survive import"
        );
        require(
            dielectricGlass->indexOfRefraction > 1.5f
                && dielectricGlass->indexOfRefraction < 1.53f,
            "KHR_materials_ior should survive import"
        );
        require(
            dielectricGlass->thicknessFactor > 0.7f,
            "KHR_materials_volume thickness should survive import"
        );
        require(
            dielectricGlass->attenuationDistance > 1.3f
                && dielectricGlass->attenuationDistance < 1.5f,
            "KHR_materials_volume attenuation distance should survive import"
        );
        require(
            dielectricGlass->attenuationColor.b > dielectricGlass->attenuationColor.r,
            "KHR_materials_volume attenuation color should survive import"
        );
        const MaterialData* roughGlass = findMaterial(glass, "RoughDielectricGlass");
        require(roughGlass != nullptr, "glass fixture should preserve its rough material");
        require(roughGlass->roughnessFactor > 0.5f, "rough glass should preserve roughness");
        require(roughGlass->transmissionFactor > 0.8f, "rough glass should preserve transmission");
        require(roughGlass->thicknessFactor > 0.5f, "rough glass should preserve volume thickness");

        const ModelImportResult volumeSphere = assimp.load(asset("glass_volume_sphere.gltf"));
        require(volumeSphere.model.meshes.size() == 1U, "Glass-2C fixture should import one sphere");
        require(
            volumeSphere.model.meshes.front().vertices.size() > 1900U,
            "Glass-2C sphere should be smooth enough for curved refraction validation"
        );
        require(
            isClosedTriangleManifold(volumeSphere.model.meshes.front()),
            "Glass-2C sphere should be a closed triangle manifold"
        );
        const MaterialData* oliveVolumeGlass = findMaterial(volumeSphere, "OliveVolumeGlass");
        require(oliveVolumeGlass != nullptr, "Glass-2C fixture should preserve its volume material");
        require(oliveVolumeGlass->transmissionFactor > 0.99f, "volume sphere should transmit light");
        require(oliveVolumeGlass->thicknessFactor > 1.9f, "volume sphere should preserve diameter scale");
        require(
            oliveVolumeGlass->attenuationColor.g > oliveVolumeGlass->attenuationColor.r,
            "olive absorption tint should survive import"
        );

        const ModelImportResult texturedVolume = assimp.load(asset("volume_texture_test.gltf"));
        const MaterialData* texturedVolumeGlass = findMaterial(texturedVolume, "TexturedVolumeGlass");
        require(texturedVolumeGlass != nullptr, "textured volume fixture should preserve its material");
        require(
            texturedVolumeGlass->thicknessTextureIndex >= 0,
            "KHR_materials_volume thickness texture should survive import"
        );
        require(
            static_cast<std::size_t>(texturedVolumeGlass->thicknessTextureIndex)
                < texturedVolume.model.textures.size(),
            "thickness texture index should reference imported texture data"
        );
        require(
            !texturedVolume.model.textures[
                static_cast<std::size_t>(texturedVolumeGlass->thicknessTextureIndex)
            ].srgb,
            "thickness texture must use linear sampling"
        );

        const ModelImportResult prism = assimp.load(asset("prism_spectrum.gltf"));
        require(prism.model.meshes.size() == 1U, "prism fixture should import one closed mesh");
        require(
            prism.model.meshes.front().indices.size() == 24U,
            "triangular prism should contain eight triangles"
        );
        const MaterialData* prismGlass = findMaterial(prism, "PrismClearGlass");
        require(prismGlass != nullptr, "prism fixture should preserve its glass material");
        require(prismGlass->transmissionFactor > 0.95f, "prism glass should remain highly transmissive");
        require(prismGlass->thicknessFactor > 0.49f, "prism glass should preserve volume thickness");
        require(
            prismGlass->dispersion > 0.32f && prismGlass->dispersion < 0.34f,
            "KHR_materials_dispersion should survive import"
        );

        const ModelImportResult skinning = assimp.load(asset("skinning_test.gltf"));
        require(skinning.model.meshes.size() == 1U, "skinning fixture should import one mesh");
        require(
            skinning.model.meshes.front().skinJoints.size() == 3U,
            "glTF skin should preserve its three-joint palette"
        );
        require(
            skinning.model.animations.size() == 1U,
            "glTF animation clip should survive import"
        );
        require(
            skinning.model.animations.front().durationSeconds > 2.99f,
            "animation duration should be converted to seconds"
        );
        require(
            !skinning.model.animations.front().channels.empty()
                && skinning.model.animations.front().channels.front().rotations.size() == 4U,
            "rotation keyframes should survive animation import"
        );
        for (const Vertex& vertex : skinning.model.meshes.front().vertices) {
            const float weightSum = vertex.jointWeights.x + vertex.jointWeights.y
                + vertex.jointWeights.z + vertex.jointWeights.w;
            require(
                weightSum > 0.999f && weightSum < 1.001f,
                "skinning weights should be normalized per vertex"
            );
        }

        const ModelImportResult dae = assimp.load(asset("textured_quad.dae"));
        require(!dae.model.meshes.empty(), "DAE fixture should import a mesh");
        require(!dae.model.textures.empty(), "DAE fixture should import its external texture reference");

        for (const char* path : {
                 "polyhaven/ArmChair_01/ArmChair_01_1k.gltf",
                 "polyhaven/modern_coffee_table_01/modern_coffee_table_01_1k.gltf",
                 "polyhaven/ceramic_vase_01/ceramic_vase_01_1k.gltf",
                 "polyhaven/anthurium_botany_01/anthurium_botany_01_1k.gltf",
                 "polyhaven/bronze_whale_statue/bronze_whale_statue_1k.gltf"}) {
            const ModelImportResult showcase = assimp.load(asset(path));
            require(!showcase.model.meshes.empty(),
                    std::string("Poly Haven showcase model has no mesh: ") + path);
            require(!showcase.model.materials.empty(),
                    std::string("Poly Haven showcase model has no material: ") + path);
            require(!showcase.model.textures.empty(),
                    std::string("Poly Haven showcase model has no textures: ") + path);
            require(showcase.model.boundsMax.x > showcase.model.boundsMin.x
                    && showcase.model.boundsMax.y > showcase.model.boundsMin.y
                    && showcase.model.boundsMax.z > showcase.model.boundsMin.z,
                    std::string("Poly Haven showcase model has invalid bounds: ") + path);
        }

        for (const auto& emissionResult : {obj.load(asset("emissive_test.obj")), assimp.load(asset("emissive_test.gltf"))}) {
            const auto* emission = findMaterial(emissionResult, "EmissionTest");
            require(emission && glm::length(emission->emissiveFactor - glm::vec3(0.75f, 0.5f, 0.25f)) < 1e-6f,
                "linear emission factor must survive OBJ/glTF import");
        }

        {
            capture::InputManifest manifest;
            obj.load(asset("emissive_test.obj"));
            assimp.load(asset("polyhaven/ArmChair_01/ArmChair_01_1k.gltf"));
            bool material=false,buffer=false;
            for(const auto& entry:manifest.files){material=material || entry.second.path.extension()==".mtl";buffer=buffer || entry.second.path.extension()==".bin";}
            require(material && buffer,"Capture must include OBJ material and glTF external buffer dependencies");
            manifest.validate();
        }
        bool missingFileRejected = false;
        try {
            obj.load(asset("does_not_exist.obj"));
        } catch (const std::exception&) {
            missingFileRejected = true;
        }
        require(missingFileRejected, "missing model files must be rejected");

        std::cout << "Asset import tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Asset import test failed: " << error.what() << '\n';
        return 1;
    }
}
