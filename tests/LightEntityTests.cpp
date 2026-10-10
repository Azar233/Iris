#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "scene/SceneDocument.h"
#include "module/RuntimeScene.h"
#include "module/SimulationCache.h"

static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        Scene scene;
        const auto parent = scene.createEntity("Parent");
        scene.find(parent)->transform.translation = glm::vec3(2, 0, 0);
        scene.find(parent)->transform.rotationDegrees.z = 90;
        SceneLightComponent component; component.type = LocalLightType::Spot;
        const auto id = scene.createLightEntity("Spot", component);
        scene.find(id)->transform.translation = glm::vec3(0, 3, 0);
        check(scene.setParent(id, parent), "parenting light failed");
        scene.updateWorldTransforms();
        auto lights = scene.buildLocalLights();
        check(lights.size() == 1 && std::abs(lights[0].position.x + 1) < 1e-5f, "parent translation/rotation not applied");
        check(std::abs(lights[0].direction.x - 1) < 1e-5f, "spot direction not inherited");
        check(std::abs(lights[0].radius - 6) < 1e-5f, "range unexpectedly scaled");
        RuntimeScene runtime; runtime.resetFrom(scene);
        check(runtime.scene().find(id)->light.has_value(), "module clone lost light");
        const auto hash = sceneContentHash(scene);
        scene.find(id)->light->intensity += 1;
        check(sceneContentHash(scene) != hash, "light edit did not invalidate module content hash");
        scene.find(id)->visible = false; check(scene.buildLocalLights().empty(), "hidden light still illuminates");
        scene.find(id)->visible = true;
        const auto copy = scene.duplicateEntity(id);
        check(copy && scene.find(copy)->light && copy != id, "duplicate lost light component");
        scene.destroyEntity(id); check(scene.buildLocalLights().size() == 1, "delete retained light");
        Scene limit;
        for (int i = 0; i < 64; ++i) check(limit.createLightEntity("Point", SceneLightComponent{}) != 0, "limit rejected valid light");
        check(limit.createLightEntity("Overflow", component) == 0, "light budget not enforced");
        component.range = std::numeric_limits<float>::quiet_NaN();
        check(!validSceneLight(component), "NaN accepted");
        component = SceneLightComponent{};
        SceneDocument document;
        SceneDocumentEntity entity; entity.id = 1; entity.name = "Point"; entity.light = component;
        entity.transform.translation = glm::vec3(1, 2, 3); document.entities.push_back(entity);
        document.renderer.localLights.push_back(LocalLight{});
        const auto directory = std::filesystem::temp_directory_path() / "IrisLightEntityTests";
        std::filesystem::create_directories(directory);
        const auto file = directory / "light.myscene";
        std::string error; check(saveSceneDocument(file, document, error), "light save failed");
        SceneDocument loaded; check(loadSceneDocument(file, loaded, error), "light load failed");
        check(loaded.entities[0].light && loaded.entities[0].modelResource.empty(), "non-mesh light not restored");
        const auto resolved = resolveSceneLocalLights(loaded);
        check(resolved.size() == 2 && resolved[1].position == glm::vec3(1, 2, 3), "legacy and entity lights not composed once");
        std::ifstream input(file); std::string json((std::istreambuf_iterator<char>(input)), {});
        const auto begin = json.find("\"range\":") + 8;
        const auto end = json.find(',', begin); json.replace(begin, end - begin, "-1");
        std::ofstream bad(directory / "invalid.myscene"); bad << json; bad.close();
        check(!loadSceneDocument(directory / "invalid.myscene", loaded, error), "invalid light accepted");
        check(loaded.entities[0].light->range == 6, "failed load replaced existing document");
        SimulationCache cache; cache.key.moduleId = "light-test"; cache.key.buildId = "test";
        SimulationCacheFrame frame; frame.frame = 0; frame.contentHash = 1;
        frame.entities.emplace_back(1, SceneTransform{}, glm::vec3(1), std::optional<SceneLightComponent>(SceneLightComponent{}));
        frame.entities.emplace_back(2, SceneTransform{}, glm::vec3(1), std::nullopt);
        frame.entities.emplace_back(3, SceneTransform{}, glm::vec3(1));
        cache.frames.push_back(frame);
        check(saveSimulationCache(directory / "light.simcache", cache, error), "light cache save failed");
        SimulationCache restored;
        check(loadSimulationCache(directory / "light.simcache", restored, error), "light cache load failed");
        check(restored.frames[0].entities[0].light && restored.frames[0].entities[0].lightRecorded, "cache lost light parameters");
        check(!restored.frames[0].entities[1].light && restored.frames[0].entities[1].lightRecorded, "cache lost explicit component removal");
        check(!restored.frames[0].entities[2].lightRecorded, "legacy cache field interpreted as deletion");
        document.entities[0].light.reset();
        check(saveSceneDocument(file, document, error) && loadSceneDocument(file, loaded, error), "legacy entity load failed");
        check(!loaded.entities[0].light && resolveSceneLocalLights(loaded).size() == 1, "legacy default introduced a new light");
        std::cout << "Light entities: hierarchy, clone/hash, visibility, budget, persistence and rejection PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
