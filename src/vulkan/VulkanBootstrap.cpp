#include "vulkan/VulkanBootstrap.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <chrono>
#include <fstream>
#include <glm/geometric.hpp>
#include <glm/packing.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include "vulkan/VulkanRaster.h"
#include "vulkan/VulkanPathTracer.h"
#include "pathtracer/SceneSnapshot.h"
#include "pathtracer/TextureSampling.h"
#include "pathtracer/EnvironmentSampling.h"
#include "asset/InputManifest.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "third_party/renderdoc_app.h"
#endif

namespace iris::vulkan {
namespace detail {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " failed: VkResult=" + std::to_string(result));
}
template<class T> T info(VkStructureType type) { T value{}; value.sType = type; return value; }

#define INSTANCE_FUNCTIONS(X) \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceProperties2) X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkEnumerateDeviceExtensionProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkDestroySurfaceKHR)
#define DEVICE_FUNCTIONS(X) \
    X(vkDestroyDevice) X(vkDeviceWaitIdle) X(vkGetDeviceQueue) X(vkCreateSwapchainKHR) \
    X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkCreateImageView) X(vkDestroyImageView) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) \
    X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCreateFence) X(vkDestroyFence) \
    X(vkWaitForFences) X(vkGetFenceStatus) X(vkResetFences) X(vkAcquireNextImageKHR) X(vkResetCommandBuffer) \
    X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdPipelineBarrier) \
    X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkQueueSubmit) X(vkQueuePresentKHR) \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) \
    X(vkFreeMemory) X(vkBindBufferMemory) X(vkCmdCopyImageToBuffer) X(vkMapMemory) X(vkUnmapMemory) \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) \
    X(vkCreateSampler) X(vkDestroySampler) X(vkCmdCopyBufferToImage) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateGraphicsPipelines) X(vkCreateComputePipelines) X(vkCmdDispatch) X(vkDestroyPipeline) X(vkCmdBindPipeline) X(vkCmdBindVertexBuffers) X(vkCmdDraw) \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) \
    X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCmdBindDescriptorSets) X(vkCmdPushConstants) \
    X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCreateQueryPool) X(vkDestroyQueryPool) \
    X(vkCmdResetQueryPool) X(vkCmdWriteTimestamp) X(vkGetQueryPoolResults)
struct Functions {
#define DECLARE(name) PFN_##name name{nullptr};
    INSTANCE_FUNCTIONS(DECLARE)
    DEVICE_FUNCTIONS(DECLARE)
    DECLARE(vkCreateDebugUtilsMessengerEXT) DECLARE(vkDestroyDebugUtilsMessengerEXT)
    DECLARE(vkSetDebugUtilsObjectNameEXT) DECLARE(vkCmdBeginDebugUtilsLabelEXT) DECLARE(vkCmdEndDebugUtilsLabelEXT)
    DECLARE(vkGetBufferDeviceAddress) DECLARE(vkCreateAccelerationStructureKHR)
    DECLARE(vkDestroyAccelerationStructureKHR) DECLARE(vkGetAccelerationStructureBuildSizesKHR)
    DECLARE(vkGetAccelerationStructureDeviceAddressKHR) DECLARE(vkCmdBuildAccelerationStructuresKHR)
    DECLARE(vkCreateRayTracingPipelinesKHR) DECLARE(vkGetRayTracingShaderGroupHandlesKHR) DECLARE(vkCmdTraceRaysKHR)
#undef DECLARE
};

struct Bootstrap {
    Functions f;
    GLFWwindow* window{nullptr}; bool glfwInitialized{false};
    VkInstance instance{VK_NULL_HANDLE}; VkDebugUtilsMessengerEXT messenger{VK_NULL_HANDLE};
    VkSurfaceKHR surface{VK_NULL_HANDLE}; VkPhysicalDevice physical{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE}; VkQueue queue{VK_NULL_HANDLE}; std::uint32_t family{0};
    VkSwapchainKHR swapchain{VK_NULL_HANDLE}; VkFormat format{VK_FORMAT_UNDEFINED}; VkExtent2D extent{};
    std::vector<VkImage> images; std::vector<VkImageView> views;
    // Present waits belong to acquired images, not frame slots.
    std::vector<VkSemaphore> presentReady;
    VkCommandPool pool{VK_NULL_HANDLE};
    struct Frame { VkCommandBuffer command{VK_NULL_HANDLE}; VkSemaphore acquired{VK_NULL_HANDLE}; VkFence fence{VK_NULL_HANDLE}; };
    std::array<Frame, 2> frames{};
    VkBuffer readback{VK_NULL_HANDLE}; VkDeviceMemory memory{VK_NULL_HANDLE};
    std::atomic<unsigned> warnings{0}, errors{0};
    BootstrapReport report;
    bool debugUtils{false};
    bool rayQueryAvailable{false};
    bool rayTracingPipelineAvailable{false};

    ~Bootstrap() { shutdown(); }
    void shutdown() {
        if (device && f.vkDeviceWaitIdle && f.vkDeviceWaitIdle(device) != VK_SUCCESS) ++errors;
        if (readback) f.vkDestroyBuffer(device, readback, nullptr);
        if (memory) f.vkFreeMemory(device, memory, nullptr);
        destroySwapchain();
        for (const auto& frame : frames) {
            if (frame.fence) f.vkDestroyFence(device, frame.fence, nullptr);
            if (frame.acquired) f.vkDestroySemaphore(device, frame.acquired, nullptr);
        }
        if (pool) f.vkDestroyCommandPool(device, pool, nullptr);
        if (device && f.vkDestroyDevice) f.vkDestroyDevice(device, nullptr);
        if (surface && f.vkDestroySurfaceKHR) f.vkDestroySurfaceKHR(instance, surface, nullptr);
        if (messenger && f.vkDestroyDebugUtilsMessengerEXT) f.vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
        if (instance && f.vkDestroyInstance) f.vkDestroyInstance(instance, nullptr);
        if (window) glfwDestroyWindow(window);
        if (glfwInitialized) glfwTerminate();
        device = VK_NULL_HANDLE; surface = VK_NULL_HANDLE; messenger = VK_NULL_HANDLE; instance = VK_NULL_HANDLE;
        readback = VK_NULL_HANDLE; memory = VK_NULL_HANDLE; pool = VK_NULL_HANDLE;
        frames = {}; window = nullptr; glfwInitialized = false;
    }
    template<class T> T load(const char* name) {
        const auto address = glfwGetInstanceProcAddress(instance, name);
        if (!address) throw std::runtime_error(std::string("Missing Vulkan entry: ") + name);
        return reinterpret_cast<T>(address);
    }
    template<class Handle> void label(VkObjectType type, Handle handle, const std::string& name) {
        if (!f.vkSetDebugUtilsObjectNameEXT) return;
        auto object = info<VkDebugUtilsObjectNameInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT);
        object.objectType = type;
        if constexpr (std::is_pointer_v<Handle>) object.objectHandle = reinterpret_cast<std::uintptr_t>(handle);
        else object.objectHandle = static_cast<std::uint64_t>(handle);
        object.pObjectName = name.c_str(); check(f.vkSetDebugUtilsObjectNameEXT(device, &object), "vkSetDebugUtilsObjectNameEXT");
    }
    static VKAPI_ATTR VkBool32 VKAPI_CALL message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
        auto& self = *static_cast<Bootstrap*>(user);
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++self.errors;
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ++self.warnings;
        std::cerr << "Vulkan: " << data->pMessage << '\n'; return VK_FALSE;
    }
    void initialize(bool requireValidation, bool requireSynchronization, bool manageGlfw = true) {
        const bool disableValidation = std::getenv("IRIS_VULKAN_DISABLE_VALIDATION") != nullptr;
        if (disableValidation && (requireValidation || requireSynchronization))
            throw std::runtime_error("Vulkan validation cannot be both required and disabled");
        if (manageGlfw && !glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwInitialized = manageGlfw;
        if (!glfwVulkanSupported()) throw std::runtime_error("Vulkan loader/driver unavailable; install a Vulkan-capable GPU driver");
        const auto enumerateLayers = load<PFN_vkEnumerateInstanceLayerProperties>("vkEnumerateInstanceLayerProperties");
        std::uint32_t count = 0; check(enumerateLayers(&count, nullptr), "Enumerate layers");
        std::vector<VkLayerProperties> layers(count); check(enumerateLayers(&count, layers.data()), "Enumerate layers");
        for (const auto& layer : layers) if (!disableValidation && std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            report.validationEnabled = true;
            report.validationLayerVersion = layer.specVersion;
        }
        if (requireValidation && !report.validationEnabled)
            throw std::runtime_error("VK_LAYER_KHRONOS_validation unavailable; install or configure the Vulkan SDK validation layers");
        const auto enumerateExtensions = load<PFN_vkEnumerateInstanceExtensionProperties>("vkEnumerateInstanceExtensionProperties");
        check(enumerateExtensions(nullptr, &count, nullptr), "Enumerate instance extensions");
        std::vector<VkExtensionProperties> extensions(count);
        check(enumerateExtensions(nullptr, &count, extensions.data()), "Enumerate instance extensions");
        const auto has = [](const auto& items, const char* name) {
            return std::any_of(items.begin(), items.end(), [name](const auto& item) { return std::strcmp(item.extensionName, name) == 0; });
        };
        debugUtils = has(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (requireValidation && !debugUtils)
            throw std::runtime_error("VK_EXT_debug_utils unavailable; cannot collect required validation diagnostics");
        std::uint32_t requiredCount = 0;
        const auto required = glfwGetRequiredInstanceExtensions(&requiredCount);
        if (!required || requiredCount == 0) throw std::runtime_error("GLFW Vulkan surface extensions unavailable");
        std::vector<const char*> enabled(required, required + requiredCount);
        if (debugUtils) enabled.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (report.validationEnabled) {
            check(enumerateExtensions("VK_LAYER_KHRONOS_validation", &count, nullptr), "Enumerate validation extensions");
            std::vector<VkExtensionProperties> validationExtensions(count);
            check(enumerateExtensions("VK_LAYER_KHRONOS_validation", &count, validationExtensions.data()), "Enumerate validation extensions");
            report.synchronizationValidation = has(validationExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            if (report.synchronizationValidation) enabled.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        }
        if (requireSynchronization && !report.synchronizationValidation)
            throw std::runtime_error("VK_EXT_validation_features unavailable; cannot enable required synchronization validation");
        auto application = info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
        application.pApplicationName = "Iris M3-A1"; application.apiVersion = VK_API_VERSION_1_3;
        const char* validationName = "VK_LAYER_KHRONOS_validation";
        auto debug = info<VkDebugUtilsMessengerCreateInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback = message; debug.pUserData = this;
        const VkValidationFeatureEnableEXT synchronization = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        auto validation = info<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
        validation.enabledValidationFeatureCount = 1; validation.pEnabledValidationFeatures = &synchronization;
        if (report.synchronizationValidation) debug.pNext = &validation;
        auto create = info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
        create.pApplicationInfo = &application; create.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size()); create.ppEnabledExtensionNames = enabled.data();
        create.enabledLayerCount = report.validationEnabled ? 1U : 0U; create.ppEnabledLayerNames = &validationName;
        create.pNext = debugUtils ? static_cast<void*>(&debug) : report.synchronizationValidation ? static_cast<void*>(&validation) : nullptr;
        check(load<PFN_vkCreateInstance>("vkCreateInstance")(&create, nullptr, &instance), "vkCreateInstance");
#define LOAD_INSTANCE(name) f.name = load<PFN_##name>(#name);
        INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
        if (debugUtils) {
            f.vkCreateDebugUtilsMessengerEXT = load<PFN_vkCreateDebugUtilsMessengerEXT>("vkCreateDebugUtilsMessengerEXT");
            f.vkDestroyDebugUtilsMessengerEXT = load<PFN_vkDestroyDebugUtilsMessengerEXT>("vkDestroyDebugUtilsMessengerEXT");
            debug.pNext = nullptr;
            check(f.vkCreateDebugUtilsMessengerEXT(instance, &debug, nullptr, &messenger), "Create debug messenger");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window = glfwCreateWindow(320, 180, "Iris Vulkan M3-A1", nullptr, nullptr);
        glfwDefaultWindowHints();
        if (!window) throw std::runtime_error("Vulkan window creation failed");
        check(glfwCreateWindowSurface(instance, window, nullptr, &surface), "glfwCreateWindowSurface");
        check(f.vkEnumeratePhysicalDevices(instance, &count, nullptr), "Enumerate devices");
        std::vector<VkPhysicalDevice> devices(count); check(f.vkEnumeratePhysicalDevices(instance, &count, devices.data()), "Enumerate devices");
        int best = -1;
        for (const auto candidate : devices) {
            VkPhysicalDeviceProperties properties{}; f.vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3) continue;
            auto dynamic = info<VkPhysicalDeviceDynamicRenderingFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES);
            auto features = info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2); features.pNext = &dynamic;
            f.vkGetPhysicalDeviceFeatures2(candidate, &features); if (!dynamic.dynamicRendering) continue;
            std::uint32_t extensionCount = 0;
            check(f.vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr), "Enumerate device extensions");
            std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
            check(f.vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, deviceExtensions.data()), "Enumerate device extensions");
            if (!has(deviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
            std::uint32_t queueCount = 0; f.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queueCount); f.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
            for (std::uint32_t index = 0; index < queueCount; ++index) {
                VkBool32 present = VK_FALSE; check(f.vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, surface, &present), "Surface support");
                if (!(queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
                const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
                if (score > best) { best = score; physical = candidate; family = index; report.deviceName = properties.deviceName; report.apiVersion = properties.apiVersion; }
                break;
            }
        }
        if (!physical) throw std::runtime_error("No Vulkan 1.3 dynamic-rendering device with one graphics/present queue and swapchain support");
        const float priority = 1;
        auto queueCreate = info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
        queueCreate.queueFamilyIndex = family; queueCreate.queueCount = 1; queueCreate.pQueuePriorities = &priority;
        auto dynamic = info<VkPhysicalDeviceDynamicRenderingFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES); dynamic.dynamicRendering = VK_TRUE;
        std::uint32_t extensionCount=0;
        check(f.vkEnumerateDeviceExtensionProperties(physical,nullptr,&extensionCount,nullptr),"Query ray-query extensions");
        std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
        check(f.vkEnumerateDeviceExtensionProperties(physical,nullptr,&extensionCount,deviceExtensions.data()),"Query ray-query extensions");
        auto address=info<VkPhysicalDeviceBufferDeviceAddressFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
        auto acceleration=info<VkPhysicalDeviceAccelerationStructureFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
        auto rayQuery=info<VkPhysicalDeviceRayQueryFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR);
        auto pipelineFeature=info<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);
        auto features=info<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
        features.pNext=&address;address.pNext=&acceleration;acceleration.pNext=&rayQuery;rayQuery.pNext=&pipelineFeature;
        f.vkGetPhysicalDeviceFeatures2(physical,&features);
        rayQueryAvailable=has(deviceExtensions,VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
            && has(deviceExtensions,VK_KHR_RAY_QUERY_EXTENSION_NAME) && has(deviceExtensions,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
            && address.bufferDeviceAddress && acceleration.accelerationStructure && rayQuery.rayQuery
            && !std::getenv("IRIS_VULKAN_DISABLE_RAY_QUERY");
        std::vector<const char*> deviceExtensionNames{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        rayTracingPipelineAvailable=rayQueryAvailable && has(deviceExtensions,VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
            && pipelineFeature.rayTracingPipeline && !std::getenv("IRIS_VULKAN_DISABLE_PATH_TRACING");
        if(rayQueryAvailable) {
            deviceExtensionNames.insert(deviceExtensionNames.end(),{VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,VK_KHR_RAY_QUERY_EXTENSION_NAME,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME});
            // Enable only the three queried capabilities used by this slice.
            address=info<VkPhysicalDeviceBufferDeviceAddressFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);address.bufferDeviceAddress=VK_TRUE;
            acceleration=info<VkPhysicalDeviceAccelerationStructureFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);acceleration.accelerationStructure=VK_TRUE;
            rayQuery=info<VkPhysicalDeviceRayQueryFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR);rayQuery.rayQuery=VK_TRUE;
            dynamic.pNext=&address;address.pNext=&acceleration;acceleration.pNext=&rayQuery;
            if(rayTracingPipelineAvailable){
                deviceExtensionNames.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
                pipelineFeature=info<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);pipelineFeature.rayTracingPipeline=VK_TRUE;
                rayQuery.pNext=&pipelineFeature;
            }
        }
        auto deviceCreate = info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
        deviceCreate.queueCreateInfoCount = 1; deviceCreate.pQueueCreateInfos = &queueCreate;
        deviceCreate.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensionNames.size()); deviceCreate.ppEnabledExtensionNames = deviceExtensionNames.data(); deviceCreate.pNext = &dynamic;
        check(f.vkCreateDevice(physical, &deviceCreate, nullptr, &device), "vkCreateDevice");
        // Load destruction first, so later missing-entry failures can unwind safely.
        f.vkDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(f.vkGetDeviceProcAddr(device, "vkDestroyDevice"));
#define LOAD_DEVICE(name) f.name = reinterpret_cast<PFN_##name>(f.vkGetDeviceProcAddr(device, #name)); if (!f.name) throw std::runtime_error("Missing device entry: " #name);
        DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
        if(rayQueryAvailable) {
#define LOAD_RAY(name) f.name=reinterpret_cast<PFN_##name>(f.vkGetDeviceProcAddr(device,#name));if(!f.name)throw std::runtime_error("Missing ray-query entry: " #name);
            LOAD_RAY(vkGetBufferDeviceAddress) LOAD_RAY(vkCreateAccelerationStructureKHR)
            LOAD_RAY(vkDestroyAccelerationStructureKHR) LOAD_RAY(vkGetAccelerationStructureBuildSizesKHR)
            LOAD_RAY(vkGetAccelerationStructureDeviceAddressKHR) LOAD_RAY(vkCmdBuildAccelerationStructuresKHR)
#undef LOAD_RAY
        }
        if(rayTracingPipelineAvailable){
#define LOAD_PT(name) f.name=reinterpret_cast<PFN_##name>(f.vkGetDeviceProcAddr(device,#name));if(!f.name)throw std::runtime_error("Missing path tracing entry: " #name);
            LOAD_PT(vkCreateRayTracingPipelinesKHR) LOAD_PT(vkGetRayTracingShaderGroupHandlesKHR) LOAD_PT(vkCmdTraceRaysKHR)
#undef LOAD_PT
        }
        if (debugUtils) {
            f.vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(f.vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
            f.vkCmdBeginDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(f.vkGetDeviceProcAddr(device, "vkCmdBeginDebugUtilsLabelEXT"));
            f.vkCmdEndDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(f.vkGetDeviceProcAddr(device, "vkCmdEndDebugUtilsLabelEXT"));
        }
        f.vkGetDeviceQueue(device, family, 0, &queue);
        auto poolCreate = info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        poolCreate.queueFamilyIndex = family; poolCreate.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(f.vkCreateCommandPool(device, &poolCreate, nullptr, &pool), "Create command pool"); label(VK_OBJECT_TYPE_COMMAND_POOL, pool, "Iris.FrameCommands");
        for (auto& frame : frames) {
            auto allocation = info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
            allocation.commandPool = pool; allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount = 1;
            check(f.vkAllocateCommandBuffers(device, &allocation, &frame.command), "Allocate frame command buffer");
            auto semaphore = info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
            check(f.vkCreateSemaphore(device, &semaphore, nullptr, &frame.acquired), "Create acquire semaphore");
            auto fence = info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO); fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            check(f.vkCreateFence(device, &fence, nullptr, &frame.fence), "Create frame fence");
        }
        createSwapchain();
    }
    void destroySwapchain() {
        for (const auto view : views) if (view) f.vkDestroyImageView(device, view, nullptr);
        for (const auto semaphore : presentReady) if (semaphore) f.vkDestroySemaphore(device, semaphore, nullptr);
        views.clear(); presentReady.clear(); images.clear();
        if (swapchain) f.vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    void createSwapchain() {
        check(f.vkDeviceWaitIdle(device), "Wait before swapchain recreation"); destroySwapchain();
        VkSurfaceCapabilitiesKHR caps{}; check(f.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps), "Surface capabilities");
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if ((caps.supportedUsageFlags & usage) != usage) throw std::runtime_error("Surface does not support color attachment/screenshot transfer readback");
        std::uint32_t count = 0; check(f.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr), "Surface formats");
        std::vector<VkSurfaceFormatKHR> formats(count); check(f.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()), "Surface formats");
        auto selected = std::find_if(formats.begin(), formats.end(), [](const auto& value) {
            return (value.format == VK_FORMAT_B8G8R8A8_SRGB || value.format == VK_FORMAT_R8G8B8A8_SRGB) && value.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (selected == formats.end()) throw std::runtime_error("Bootstrap requires RGBA8/BGRA8 sRGB surface format");
        format = selected->format; extent = caps.currentExtent;
        if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
            int width = 0, height = 0; glfwGetFramebufferSize(window, &width, &height);
            extent.width = std::clamp(static_cast<std::uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<std::uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (extent.width == 0 || extent.height == 0) throw std::runtime_error("Bootstrap surface is minimized; cannot create zero-sized swapchain");
        auto create = info<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
        create.surface = surface; create.minImageCount = std::max(2U, caps.minImageCount);
        if (caps.maxImageCount) create.minImageCount = std::min(create.minImageCount, caps.maxImageCount);
        create.imageFormat = format; create.imageColorSpace = selected->colorSpace; create.imageExtent = extent;
        create.imageArrayLayers = 1; create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; create.preTransform = caps.currentTransform;
        create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(caps.supportedCompositeAlpha & create.compositeAlpha)) {
            for (const auto alpha : {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
                if (caps.supportedCompositeAlpha & alpha) { create.compositeAlpha = alpha; break; }
        }
        create.presentMode = VK_PRESENT_MODE_FIFO_KHR; create.clipped = VK_TRUE;
        if(std::getenv("IRIS_VULKAN_DISABLE_VSYNC")) {
            std::uint32_t modeCount=0;
            check(f.vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&modeCount,nullptr),"Count present modes");
            std::vector<VkPresentModeKHR> modes(modeCount);
            check(f.vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&modeCount,modes.data()),"Read present modes");
            if(std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_IMMEDIATE_KHR)==modes.end())
                throw std::runtime_error("Uncapped Vulkan benchmark requires immediate presentation support");
            create.presentMode=VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        check(f.vkCreateSwapchainKHR(device, &create, nullptr, &swapchain), "Create swapchain"); label(VK_OBJECT_TYPE_SWAPCHAIN_KHR, swapchain, "Iris.Swapchain");
        check(f.vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "Get swapchain images");
        images.resize(count); check(f.vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()), "Get swapchain images");
        views.resize(count); presentReady.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            auto view = info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
            view.image = images[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            check(f.vkCreateImageView(device, &view, nullptr, &views[i]), "Create swapchain view");
            auto semaphore = info<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
            check(f.vkCreateSemaphore(device, &semaphore, nullptr, &presentReady[i]), "Create per-image present semaphore");
            label(VK_OBJECT_TYPE_IMAGE, images[i], "Iris.PresentImage." + std::to_string(i));
            label(VK_OBJECT_TYPE_SEMAPHORE, presentReady[i], "Iris.PresentReady." + std::to_string(i));
        }
        ++report.swapchainCreations;
    }
    void prepareReadback() {
        auto create = info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        create.size = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
        create.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(f.vkCreateBuffer(device, &create, nullptr, &readback), "Create readback buffer");
        VkMemoryRequirements requirements{}; f.vkGetBufferMemoryRequirements(device, readback, &requirements);
        VkPhysicalDeviceMemoryProperties properties{}; f.vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        std::uint32_t type = properties.memoryTypeCount;
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((requirements.memoryTypeBits & (1U << i)) && (properties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { type = i; break; }
        if (type == properties.memoryTypeCount) throw std::runtime_error("No host-visible coherent readback memory");
        auto allocation = info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
        check(f.vkAllocateMemory(device, &allocation, nullptr, &memory), "Allocate readback memory");
        check(f.vkBindBufferMemory(device, readback, memory, 0), "Bind readback memory"); label(VK_OBJECT_TYPE_BUFFER, readback, "Iris.Readback");
    }
    void draw(unsigned number, bool capture) {
        auto& frame = frames[number % frames.size()];
        check(f.vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX), "Wait frame fence");
        std::uint32_t image = 0;
        VkResult acquire = VK_ERROR_OUT_OF_DATE_KHR;
        for (int attempt = 0; attempt < 3; ++attempt) {
            acquire = f.vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frame.acquired, VK_NULL_HANDLE, &image);
            if (acquire != VK_ERROR_OUT_OF_DATE_KHR) break;
            createSwapchain();
        }
        if (acquire != VK_SUBOPTIMAL_KHR) check(acquire, "Acquire image");
        if (capture) prepareReadback();
        check(f.vkResetCommandBuffer(frame.command, 0), "Reset command buffer");
        auto begin = info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        check(f.vkBeginCommandBuffer(frame.command, &begin), "Begin commands");
        if (f.vkCmdBeginDebugUtilsLabelEXT) {
            auto pass = info<VkDebugUtilsLabelEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT); pass.pLabelName = "Iris.Bootstrap.DynamicClear";
            f.vkCmdBeginDebugUtilsLabelEXT(frame.command, &pass);
        }
        auto barrier = info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images[image]; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        // Match the acquire semaphore wait stage so this layout transition
        // executes after acquisition, including when old contents are discarded.
        f.vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        auto attachment = info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        attachment.imageView = views[image]; attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.clearValue.color = {{0.125f, 0.25f, 0.5f, 1.0f}};
        auto rendering = info<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.extent = extent; rendering.layerCount = 1; rendering.colorAttachmentCount = 1; rendering.pColorAttachments = &attachment;
        f.vkCmdBeginRendering(frame.command, &rendering); f.vkCmdEndRendering(frame.command);
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.newLayout = capture ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.dstAccessMask = capture ? VK_ACCESS_TRANSFER_READ_BIT : 0;
        f.vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            capture ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        if (capture) {
            VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {extent.width, extent.height, 1};
            f.vkCmdCopyImageToBuffer(frame.command, images[image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = 0;
            f.vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        if (f.vkCmdEndDebugUtilsLabelEXT) f.vkCmdEndDebugUtilsLabelEXT(frame.command);
        check(f.vkEndCommandBuffer(frame.command), "End commands");
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        auto submit = info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &frame.acquired; submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &frame.command;
        submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &presentReady[image];
        check(f.vkResetFences(device, 1, &frame.fence), "Reset frame fence");
        check(f.vkQueueSubmit(queue, 1, &submit, frame.fence), "Submit frame");
        auto present = info<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        present.waitSemaphoreCount = 1; present.pWaitSemaphores = &presentReady[image];
        present.swapchainCount = 1; present.pSwapchains = &swapchain; present.pImageIndices = &image;
        const auto result = f.vkQueuePresentKHR(queue, &present);
        if (result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR) check(result, "Present frame");
        if (capture) {
            check(f.vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX), "Wait readback");
            report.width = extent.width; report.height = extent.height;
            report.rgba.resize(static_cast<std::size_t>(extent.width) * extent.height * 4);
            void* mapped = nullptr; check(f.vkMapMemory(device, memory, 0, report.rgba.size(), 0, &mapped), "Map readback");
            std::memcpy(report.rgba.data(), mapped, report.rgba.size()); f.vkUnmapMemory(device, memory);
            if (format == VK_FORMAT_B8G8R8A8_SRGB)
                for (std::size_t i = 0; i < report.rgba.size(); i += 4) std::swap(report.rgba[i], report.rgba[i + 2]);
        }
        ++report.renderedFrames;
        if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR || acquire == VK_SUBOPTIMAL_KHR) createSwapchain();
    }
};
}
using namespace detail;
BootstrapReport runBootstrap(bool requireValidation, bool requireSynchronization) {
    Bootstrap state; state.initialize(requireValidation || requireSynchronization, requireSynchronization);
    for (unsigned frame = 0; frame < 9; ++frame) {
        if (frame == 3 || frame == 6) {
            glfwSetWindowSize(state.window, frame == 3 ? 640 : 400, frame == 3 ? 360 : 240);
            glfwPollEvents(); state.createSwapchain();
        }
        glfwPollEvents();
        state.draw(frame, frame == 8);
    }
    check(state.f.vkDeviceWaitIdle(state.device), "Wait before finishing bootstrap");
    state.shutdown();
    state.report.validationWarnings = state.warnings.load(); state.report.validationErrors = state.errors.load();
    if (state.report.validationErrors) throw std::runtime_error("Vulkan validation reported errors");
    return state.report;
}
}
#include "VulkanRaster.inc"
#include "VulkanPathTracer.inc"
