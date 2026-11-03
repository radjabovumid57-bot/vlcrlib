/**
 * vgraphics_device.c — instance, device, swapchain, depth, render pass, sync.
 *
 * This version adds explicit error checking and verbose logging at every
 * Vulkan call, because the previous version would crash silently on some
 * drivers (see note at createDevice()).
 */
#include "vgraphics.h"
#include "vgraphics_internal.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Small logging helpers (stderr, unbuffered so a crash can't eat it) */
/* ------------------------------------------------------------------ */
#define LOG(...) do { fprintf(stderr, "[vk] " __VA_ARGS__); fflush(stderr); } while (0)
#define LOGERR(...) do { fprintf(stderr, "[vk!] " __VA_ARGS__); fflush(stderr); } while (0)

static const char *vkResultName(VkResult r) {
    switch (r) {
        case VK_SUCCESS:                        return "VK_SUCCESS";
        case VK_NOT_READY:                      return "VK_NOT_READY";
        case VK_TIMEOUT:                        return "VK_TIMEOUT";
        case VK_EVENT_SET:                      return "VK_EVENT_SET";
        case VK_EVENT_RESET:                    return "VK_EVENT_RESET";
        case VK_INCOMPLETE:                     return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:              return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:        return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:        return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:      return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:         return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:     return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR:         return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:          return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_SUBOPTIMAL_KHR:                 return "VK_SUBOPTIMAL_KHR";
        default:                                return "(unknown)";
    }
}

/* ------------------------------------------------------------------ */
/*  Instance                                                          */
/* ------------------------------------------------------------------ */

static VKAPI_ATTR VkBool32 VKAPI_CALL vlcrDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void *userData)
{
    (void)type; (void)userData;
    const char *level =
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" :
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "WARN" : "INFO";
    fprintf(stderr, "[validation:%s] %s\n", level,
            data && data->pMessage ? data->pMessage : "(no message)");
    fflush(stderr);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        VK.validationErrors++;
    return VK_FALSE;
}

bool createInstance(void) {
    VkApplicationInfo ai = {0};
    ai.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName   = "vlcrlib";
    ai.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    ai.pEngineName        = "vlcrlib";
    ai.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    /* Request the loader's supported core version (capped to the version
     * this library currently targets). Features2 is invalid if the
     * application only requests Vulkan 1.0. */
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion pEnumVersion =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE,
                                                               "vkEnumerateInstanceVersion");
    if (pEnumVersion) pEnumVersion(&loaderVersion);
    ai.apiVersion = loaderVersion < VK_API_VERSION_1_3 ? loaderVersion : VK_API_VERSION_1_3;

    uint32_t extCount = 0;
    const char * const *exts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    if (!exts || extCount == 0) {
        LOGERR("SDL_Vulkan_GetInstanceExtensions returned nothing\n");
        return false;
    }

    const char *validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
    uint32_t enabledLayerCount = 0;
    const char * const *enabledLayers = NULL;

    bool wantValidation =
#ifndef NDEBUG
        true;
#else
        false;
#endif
    const char *validationEnv = getenv("VLCRLIB_VALIDATION");
    if (validationEnv && strcmp(validationEnv, "0") != 0) wantValidation = true;

    if (wantValidation) {
        uint32_t layerCount = 0;
        VkResult lr = vkEnumerateInstanceLayerProperties(&layerCount, NULL);
        if (lr == VK_SUCCESS && layerCount > 0) {
            VkLayerProperties *layerProps = calloc(layerCount, sizeof(*layerProps));
            if (layerProps) {
                vkEnumerateInstanceLayerProperties(&layerCount, layerProps);
                for (uint32_t i = 0; i < layerCount; ++i) {
                    if (strcmp(layerProps[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                        enabledLayerCount = 1;
                        enabledLayers = validationLayers;
                        LOG("validation layer enabled\n");
                        break;
                    }
                }
                free(layerProps);
            }
        }
        if (!enabledLayerCount) LOG("validation layer not available\n");
    }

    /* Add the debug-utils extension when validation is active. */
    const char *instanceExts[64];
    uint32_t instanceExtCount = 0;
    for (uint32_t i = 0; i < extCount && instanceExtCount < 63; ++i)
        instanceExts[instanceExtCount++] = exts[i];

    bool debugUtilsEnabled = false;
#if defined(__APPLE__) && defined(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)
    /* MoltenVK exposes portability extensions; the loader refuses to enumerate
       MoltenVK devices without this instance flag. */
    if (instanceExtCount < 64)
        instanceExts[instanceExtCount++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
#endif
    if (enabledLayerCount) {
        uint32_t n = 0;
        if (vkEnumerateInstanceExtensionProperties(NULL, &n, NULL) == VK_SUCCESS && n > 0) {
            VkExtensionProperties *props = calloc(n, sizeof(*props));
            if (props) {
                vkEnumerateInstanceExtensionProperties(NULL, &n, props);
                for (uint32_t i = 0; i < n; ++i) {
                    if (strcmp(props[i].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
                        if (instanceExtCount < 64)
                            instanceExts[instanceExtCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
                        debugUtilsEnabled = true;
                        break;
                    }
                }
                free(props);
            }
        }
    }

    VkInstanceCreateInfo ci = {0};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledLayerCount       = enabledLayerCount;
    ci.ppEnabledLayerNames     = enabledLayers;
    ci.enabledExtensionCount   = instanceExtCount;
    ci.ppEnabledExtensionNames = instanceExts;
#if defined(__APPLE__) && defined(VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR)
    ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    VkResult r = vkCreateInstance(&ci, NULL, &VK.instance);
    if (r != VK_SUCCESS) {
        LOGERR("vkCreateInstance failed: %s\n", vkResultName(r));
        return false;
    }

    VK.validationErrors = 0;
    if (debugUtilsEnabled) {
        PFN_vkCreateDebugUtilsMessengerEXT createMessenger =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                VK.instance, "vkCreateDebugUtilsMessengerEXT");
        if (createMessenger) {
            VkDebugUtilsMessengerCreateInfoEXT dci = {0};
            dci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            dci.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
            dci.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dci.pfnUserCallback = vlcrDebugCallback;
            if (createMessenger(VK.instance, &dci, NULL, &VK.debugMessenger) == VK_SUCCESS)
                LOG("debug utils messenger enabled\n");
            else
                VK.debugMessenger = VK_NULL_HANDLE;
        }
    }

    /* Log the instance version we actually got. */
    uint32_t instVer = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion pEnumVer =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK.instance, "vkEnumerateInstanceVersion");
    if (pEnumVer) {
        pEnumVer(&instVer);
        LOG("instance created; loader supports Vulkan %u.%u.%u\n",
            VK_API_VERSION_MAJOR(instVer),
            VK_API_VERSION_MINOR(instVer),
            VK_API_VERSION_PATCH(instVer));
    } else {
        LOG("instance created; loader is Vulkan 1.0 (no vkEnumerateInstanceVersion)\n");
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  Physical device                                                   */
/* ------------------------------------------------------------------ */

bool pickPhysicalDevice(void) {
    uint32_t n = 0;
    VkResult r = vkEnumeratePhysicalDevices(VK.instance, &n, NULL);
    if (r != VK_SUCCESS) {
        LOGERR("vkEnumeratePhysicalDevices(count) failed: %s\n", vkResultName(r));
        return false;
    }
    if (n == 0) {
        LOGERR("no Vulkan physical devices found\n");
        return false;
    }

    VkPhysicalDevice *devs = calloc(n, sizeof(*devs));
    if (!devs) return false;

    uint32_t count = n;    /* vkEnumerate may shrink this */
    r = vkEnumeratePhysicalDevices(VK.instance, &count, devs);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        LOGERR("vkEnumeratePhysicalDevices(list) failed: %s\n", vkResultName(r));
        free(devs);
        return false;
    }
    n = count;

    LOG("Vulkan devices found: %u\n", n);

    int bestScore = -1;
    VkPhysicalDevice best = VK_NULL_HANDLE;
    uint32_t bestQ = 0;
    VkPhysicalDeviceProperties bestProps;
    memset(&bestProps, 0, sizeof(bestProps));

    for (uint32_t i = 0; i < n; ++i) {
        VkPhysicalDeviceProperties p;
        memset(&p, 0, sizeof(p));
        vkGetPhysicalDeviceProperties(devs[i], &p);

        LOG("  [%u] %s | %s | api %u.%u.%u\n",
            i, p.deviceName, deviceTypeName(p.deviceType),
            VK_API_VERSION_MAJOR(p.apiVersion),
            VK_API_VERSION_MINOR(p.apiVersion),
            VK_API_VERSION_PATCH(p.apiVersion));

        /* Accept anything 1.0+. Don't require 1.2+; we can build with
         * dynamic rendering via extensions on older devices. */
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, NULL);
        if (qn == 0) { LOG("       skip: no queues\n"); continue; }

        VkQueueFamilyProperties *q = calloc(qn, sizeof(*q));
        if (!q) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, q);

        int foundQ = -1;
        for (uint32_t j = 0; j < qn; ++j) {
            VkBool32 present = 0;
            vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], j, VK.surface, &present);
            if ((q[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                foundQ = (int)j;
                break;
            }
        }
        free(q);
        if (foundQ < 0) { LOG("       skip: no graphics+present queue\n"); continue; }

        int score = deviceTypeScore(p.deviceType);
        if (score > bestScore) {
            bestScore = score;
            best = devs[i];
            bestQ = (uint32_t)foundQ;
            bestProps = p;
        }
    }

    free(devs);

    if (best == VK_NULL_HANDLE) {
        LOGERR("no suitable Vulkan device (need graphics+present)\n");
        return false;
    }

    VK.phys = best;
    VK.queueFamily = bestQ;
    LOG("selected: %s (%s)\n", bestProps.deviceName, deviceTypeName(bestProps.deviceType));
    return true;
}

/* ------------------------------------------------------------------ */
/*  Device                                                            */
/*                                                                     */
/*  NOTE: this function is the one that was crashing silently. The     */
/*  previous version unconditionally called vkGetPhysicalDeviceFeatures2 */
/*  (a Vulkan 1.1 entry point) and unconditionally chained a            */
/*  VkPhysicalDeviceVulkan13Features into VkDeviceCreateInfo::pNext.    */
/*  Some 1.0 loaders have no vkGetPhysicalDeviceFeatures2, and some    */
/*  drivers crash the loader when handed a 1.3 feature struct. This     */
/*  version defends against both.                                       */
/* ------------------------------------------------------------------ */

bool createDevice(void) {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {0};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = VK.queueFamily;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    const char *devExt[6] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    uint32_t devExtCount = 1;

    /* What version does this physical device advertise? */
    VkPhysicalDeviceProperties physProps;
    memset(&physProps, 0, sizeof(physProps));
    vkGetPhysicalDeviceProperties(VK.phys, &physProps);
    uint32_t devApi = physProps.apiVersion;
    LOG("physical device API version: %u.%u.%u\n",
        VK_API_VERSION_MAJOR(devApi),
        VK_API_VERSION_MINOR(devApi),
        VK_API_VERSION_PATCH(devApi));

#if defined(__APPLE__) && defined(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)
    {
        uint32_t appExtCount = 0;
        if (vkEnumerateDeviceExtensionProperties(VK.phys, NULL, &appExtCount, NULL) ==
                VK_SUCCESS && appExtCount) {
            VkExtensionProperties *appExt = calloc(appExtCount, sizeof(*appExt));
            if (appExt && vkEnumerateDeviceExtensionProperties(
                    VK.phys, NULL, &appExtCount, appExt) == VK_SUCCESS) {
                for (uint32_t i = 0; i < appExtCount; ++i) {
                    if (strcmp(appExt[i].extensionName,
                               VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME) == 0) {
                        devExt[devExtCount++] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
                        break;
                    }
                }
            }
            free(appExt);
        }
    }
#endif

    PFN_vkGetPhysicalDeviceFeatures2 pGetFeats2 =
        (PFN_vkGetPhysicalDeviceFeatures2)vkGetInstanceProcAddr(VK.instance, "vkGetPhysicalDeviceFeatures2");

    /* Ray Query is optional. Require Vulkan 1.2 for core buffer device address
     * and the three KHR extensions it depends on; otherwise keep raster path. */
    bool haveAS = false, haveRQ = false, haveDeferred = false;
    if (devApi >= VK_API_VERSION_1_2) {
        uint32_t extCount = 0;
        if (vkEnumerateDeviceExtensionProperties(VK.phys, NULL, &extCount, NULL) == VK_SUCCESS && extCount) {
            VkExtensionProperties *props = calloc(extCount, sizeof(*props));
            if (props && vkEnumerateDeviceExtensionProperties(VK.phys, NULL, &extCount, props) == VK_SUCCESS) {
                for (uint32_t i = 0; i < extCount; ++i) {
                    if (!strcmp(props[i].extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)) haveAS = true;
                    if (!strcmp(props[i].extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME)) haveRQ = true;
                    if (!strcmp(props[i].extensionName, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)) haveDeferred = true;
                }
            }
            free(props);
        }
    }
    bool wantRayQuery = haveAS && haveRQ && haveDeferred && pGetFeats2;

    /* We only ask for 1.3 features when the device advertises 1.3 AND the
     * entry point exists. vkGetPhysicalDeviceFeatures2 was added in 1.1. */
    int use13 = 0;
    VkPhysicalDeviceVulkan13Features f13 = {0};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    if (devApi >= VK_API_VERSION_1_3 && pGetFeats2) {
        VkPhysicalDeviceVulkan13Features avail13 = {0};
        avail13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 q2 = {0};
        q2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        q2.pNext = &avail13;

        LOG("querying 1.3 features...\n");
        pGetFeats2(VK.phys, &q2);

        /* Only ask for what the device reports supporting. */
        f13.dynamicRendering = avail13.dynamicRendering;
        f13.synchronization2 = avail13.synchronization2;

        LOG("device reports dynRender=%d sync2=%d\n",
            (int)f13.dynamicRendering, (int)f13.synchronization2);

        /* Only chain if at least one feature is actually present, and even
         * then only if the API level says we can. A NULL pNext is legal
         * and works on every driver. */
        if (f13.dynamicRendering || f13.synchronization2) {
            use13 = 1;
        } else {
            LOG("device is 1.3 but reports no 1.3 features; not chaining\n");
        }
    } else {
        LOG("device is < 1.3 or vkGetPhysicalDeviceFeatures2 unavailable; "
            "not chaining 1.3 features\n");
    }

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures = {0};
    VkPhysicalDeviceRayQueryFeaturesKHR rqFeatures = {0};
    VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeatures = {0};
    asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    rqFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    bdaFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    asFeatures.pNext = &rqFeatures;
    rqFeatures.pNext = &bdaFeatures;
    if (wantRayQuery) {
        VkPhysicalDeviceFeatures2 rtQuery = {0};
        rtQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        rtQuery.pNext = &asFeatures;
        pGetFeats2(VK.phys, &rtQuery);
        wantRayQuery = asFeatures.accelerationStructure && rqFeatures.rayQuery &&
                       bdaFeatures.bufferDeviceAddress;
    }
    if (wantRayQuery) {
        devExt[devExtCount++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        devExt[devExtCount++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
        devExt[devExtCount++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
        asFeatures.accelerationStructure = VK_TRUE;
        rqFeatures.rayQuery = VK_TRUE;
        bdaFeatures.bufferDeviceAddress = VK_TRUE;
        bdaFeatures.pNext = use13 ? &f13 : NULL;
    } else {
        VK.rayQuerySupported = false;
        asFeatures.pNext = NULL;
    }

    VkDeviceCreateInfo ci = {0};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext                   = wantRayQuery ? (void *)&asFeatures : (use13 ? (void *)&f13 : NULL);
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = devExtCount;
    ci.ppEnabledExtensionNames = devExt;

    LOG("vkCreateDevice (rayQuery=%d, Vulkan13Features=%d)...\n", wantRayQuery, use13);
    VkResult r = vkCreateDevice(VK.phys, &ci, NULL, &VK.device);
    if (r != VK_SUCCESS) {
        LOGERR("vkCreateDevice failed: %s\n", vkResultName(r));
        return false;
    }

    VK.rayQuerySupported = wantRayQuery;

    VkQueryPoolCreateInfo qpci = {0};
    qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qpci.queryCount = MAX_FRAMES_IN_FLIGHT * 4;
    if (vkCreateQueryPool(VK.device, &qpci, NULL, &VK.gpuTimerPool) != VK_SUCCESS) {
        VK.gpuTimerPool = VK_NULL_HANDLE;
        LOGERR("GPU timestamp query pool unavailable\n");
    }
    VK.gpuTimerPeriod = physProps.limits.timestampPeriod;
    LOG("Vulkan ray query: %s\n", VK.rayQuerySupported ? "enabled" : "unavailable (raster fallback)");

    vkGetDeviceQueue(VK.device, VK.queueFamily, 0, &VK.gfxQueue);
    VK.presentQueue = VK.gfxQueue;
    LOG("VkDevice ok (dynRender=%d sync2=%d)\n",
        (int)f13.dynamicRendering, (int)f13.synchronization2);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Swapchain / depth / renderpass / framebuffers / commands / sync   */
/* ------------------------------------------------------------------ */

bool createSwapchain(uint32_t w, uint32_t h) {
    VkSurfaceCapabilitiesKHR caps;
    VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VK.phys, VK.surface, &caps);
    if (r != VK_SUCCESS) { LOGERR("vkGetPhysicalDeviceSurfaceCapabilitiesKHR: %s\n", vkResultName(r)); return false; }

    VkExtent2D ext = caps.currentExtent;
    if (ext.width == UINT32_MAX) { ext.width = w; ext.height = h; }
    if (ext.width == 0 || ext.height == 0) { LOGERR("swapchain extent is 0x0\n"); return false; }

    /* Choose a supported format. */
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(VK.phys, VK.surface, &fmtCount, NULL);
    VkSurfaceFormatKHR *formats = calloc(fmtCount ? fmtCount : 1, sizeof(*formats));
    if (fmtCount) vkGetPhysicalDeviceSurfaceFormatsKHR(VK.phys, VK.surface, &fmtCount, formats);

    VkFormat fmt = VK_FORMAT_B8G8R8A8_SRGB;
    VkColorSpaceKHR cs = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    int foundFmt = 0;
    for (uint32_t i = 0; i < fmtCount; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            foundFmt = 1; break;
        }
    }
    if (!foundFmt && fmtCount > 0) {
        fmt = formats[0].format;
        cs  = formats[0].colorSpace;
        LOG("preferred B8G8R8A8_SRGB not available, using %d\n", (int)fmt);
    }
    free(formats);

    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && count > caps.maxImageCount) count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci = {0};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = VK.surface;
    ci.minImageCount    = count;
    ci.imageFormat      = fmt;
    ci.imageColorSpace  = cs;
    ci.imageExtent      = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped          = VK_TRUE;

    r = vkCreateSwapchainKHR(VK.device, &ci, NULL, &VK.swapchain);
    if (r != VK_SUCCESS) { LOGERR("vkCreateSwapchainKHR: %s\n", vkResultName(r)); return false; }

    vkGetSwapchainImagesKHR(VK.device, VK.swapchain, &count, NULL);
    VK.images = calloc(count, sizeof(*VK.images));
    vkGetSwapchainImagesKHR(VK.device, VK.swapchain, &count, VK.images);

    VK.views = calloc(count, sizeof(*VK.views));
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo vi = {0};
        vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image    = VK.images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format   = fmt;
        vi.subresourceRange = (VkImageSubresourceRange){
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        r = vkCreateImageView(VK.device, &vi, NULL, &VK.views[i]);
        if (r != VK_SUCCESS) { LOGERR("vkCreateImageView[%u]: %s\n", i, vkResultName(r)); return false; }
    }

    VK.swapFormat = fmt;
    VK.swapExtent = ext;
    VK.imageCount = count;
    LOG("swapchain: %ux%u, %u images, fmt=%d\n", ext.width, ext.height, count, (int)fmt);
    return true;
}

bool createDepthResources(void) {
    VK.depthFormat = VK_FORMAT_D32_SFLOAT;

    if (!vlcr_createImage(VK.swapExtent.width, VK.swapExtent.height,
                     VK.depthFormat, VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     &VK.depthImage, &VK.depthMem)) {
        LOGERR("depth image creation failed\n");
        return false;
    }

    VkImageViewCreateInfo vi = {0};
    vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image    = VK.depthImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format   = VK.depthFormat;
    vi.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    VkResult r = vkCreateImageView(VK.device, &vi, NULL, &VK.depthView);
    if (r != VK_SUCCESS) { LOGERR("depth view: %s\n", vkResultName(r)); return false; }
    return true;
}

bool createRenderPass(void) {
    VkAttachmentDescription atts[2] = {0};
    atts[0].format         = VK.swapFormat;
    atts[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    atts[1].format         = VK.depthFormat;
    atts[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    atts[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount    = 1;
    sub.pColorAttachments       = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep = {0};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci = {0};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 2;
    ci.pAttachments    = atts;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;

    VkResult r = vkCreateRenderPass(VK.device, &ci, NULL, &VK.renderPass);
    if (r != VK_SUCCESS) { LOGERR("vkCreateRenderPass: %s\n", vkResultName(r)); return false; }
    return true;
}

bool createFramebuffers(void) {
    VK.framebuffers = calloc(VK.imageCount, sizeof(VkFramebuffer));
    for (uint32_t i = 0; i < VK.imageCount; ++i) {
        VkImageView atts[2] = { VK.views[i], VK.depthView };
        VkFramebufferCreateInfo ci = {0};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = VK.renderPass;
        ci.attachmentCount = 2;
        ci.pAttachments    = atts;
        ci.width           = VK.swapExtent.width;
        ci.height          = VK.swapExtent.height;
        ci.layers          = 1;
        VkResult r = vkCreateFramebuffer(VK.device, &ci, NULL, &VK.framebuffers[i]);
        if (r != VK_SUCCESS) { LOGERR("vkCreateFramebuffer[%u]: %s\n", i, vkResultName(r)); return false; }
    }
    return true;
}

bool createCommands(void) {
    VkCommandPoolCreateInfo ci = {0};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = VK.queueFamily;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkResult r = vkCreateCommandPool(VK.device, &ci, NULL, &VK.cmdPool);
    if (r != VK_SUCCESS) { LOGERR("vkCreateCommandPool: %s\n", vkResultName(r)); return false; }

    VK.cmdBuffers = calloc(VK.imageCount, sizeof(VkCommandBuffer));
    VkCommandBufferAllocateInfo ai = {0};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = VK.cmdPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = VK.imageCount;
    r = vkAllocateCommandBuffers(VK.device, &ai, VK.cmdBuffers);
    if (r != VK_SUCCESS) { LOGERR("vkAllocateCommandBuffers: %s\n", vkResultName(r)); return false; }
    return true;
}

bool createSync(void) {
    VK.semImgAvailable = calloc(MAX_FRAMES_IN_FLIGHT, sizeof(VkSemaphore));
    /* A present-wait semaphore is associated with a swapchain image. Reuse
     * it only after that image is acquired again, not merely after its frame
     * fence signals. */
    VK.semRenderDone   = calloc(VK.imageCount, sizeof(VkSemaphore));
    VK.inFlight        = calloc(MAX_FRAMES_IN_FLIGHT, sizeof(VkFence));
    VK.imagesInFlight  = calloc(VK.imageCount, sizeof(VkFence));
    if (!VK.semImgAvailable || !VK.semRenderDone || !VK.inFlight || !VK.imagesInFlight)
        return false;

    VkSemaphoreCreateInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi = {0};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(VK.device, &si, NULL, &VK.semImgAvailable[i]) != VK_SUCCESS) return false;
        if (vkCreateFence    (VK.device, &fi, NULL, &VK.inFlight[i])        != VK_SUCCESS) return false;
    }
    for (uint32_t i = 0; i < VK.imageCount; ++i)
        if (vkCreateSemaphore(VK.device, &si, NULL, &VK.semRenderDone[i]) != VK_SUCCESS) return false;
    return true;
}

bool createRenderPassRT(void) {
    VkAttachmentDescription atts[2] = {0};

    atts[0].format         = VK.swapFormat;
    atts[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    atts[1].format         = VK.depthFormat;
    atts[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount    = 1;
    sub.pColorAttachments       = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep = {0};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 2; ci.pAttachments = atts;
    ci.subpassCount = 1;    ci.pSubpasses = &sub;
    ci.dependencyCount = 1; ci.pDependencies = &dep;

    VkResult r = vkCreateRenderPass(VK.device, &ci, NULL, &VK.renderPassRT);
    if (r != VK_SUCCESS) { LOGERR("vkCreateRenderPass(RT): %s\n", vkResultName(r)); return false; }
    return true;
}
