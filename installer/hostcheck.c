// Host check of the installer's first step, compiled with the bundled clang at install time: does the CPU run
// -march=x86-64-v3 code, and does a Vulkan 1.3 device exist. Prints one JSON object on stdout.
// The Vulkan loader is opened at run time, so a machine without one gets "loader": false instead of a failed start.
#if defined(__x86_64__) || defined(_M_X64)
#include <cpuid.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#ifdef _WIN32
#include <windows.h>
#define open_lib() LoadLibraryA("vulkan-1.dll")
#define lib_sym(l, n) ((void *)GetProcAddress((l), (n)))
#else
#include <dlfcn.h>
// macOS: Homebrew's loader (vulkan-loader's libvulkan.dylib) or MoltenVK itself; dyld's default search does not
// reach /opt/homebrew/lib, so the Homebrew prefix paths come first. The .so names are the Linux ones.
#define open_lib() dlopen_vulkan()
#define lib_sym(l, n) dlsym((l), (n))
static void *dlopen_vulkan(void) {
    static const char *const names[] = {
#ifdef __APPLE__
        "/opt/homebrew/lib/libvulkan.dylib", "/opt/homebrew/lib/libMoltenVK.dylib",
#endif
        "libvulkan.so.1", "libvulkan.so",
    };
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        void *lib = dlopen(names[i], RTLD_NOW);
        if (lib) return lib;
    }
    return NULL;
}
#endif

static void json_string(const char *s) {
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') printf("\\%c", *s);
        else if ((unsigned char)*s < 0x20) printf("\\u%04x", *s);
        else putchar(*s);
    }
    putchar('"');
}

#if defined(__x86_64__) || defined(_M_X64)
// The x86-64-v3 level from CPUID: AVX, AVX2, BMI1, BMI2, F16C, FMA, LZCNT and MOVBE, with the OS saving the YMM state.
// Not __builtin_cpu_supports("x86-64-v3"): clang reads that from the C runtime's CPU model, and libgcc before GCC 12
// (the Linux release's sysroot has GCC 10) has no level bits, so it answers false on every CPU.
static int x86_64_v3(void) {
    unsigned a, b, c, d;
    if (!__get_cpuid(1, &a, &b, &c, &d)) return 0;
    const unsigned fma = 1u << 12, movbe = 1u << 22, osxsave = 1u << 27, avx = 1u << 28, f16c = 1u << 29;
    if ((c & (fma | movbe | osxsave | avx | f16c)) != (fma | movbe | osxsave | avx | f16c)) return 0;
    unsigned lo, hi;
    __asm__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    if ((lo & 6) != 6) return 0;   // XMM and YMM state enabled by the OS
    if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return 0;
    const unsigned bmi1 = 1u << 3, avx2 = 1u << 5, bmi2 = 1u << 8;
    if ((b & (bmi1 | avx2 | bmi2)) != (bmi1 | avx2 | bmi2)) return 0;
    if (!__get_cpuid(0x80000001, &a, &b, &c, &d)) return 0;
    return (c & (1u << 5)) != 0;   // LZCNT (ABM)
}

// The x86-64-v2 level: SSSE3, SSE4.1, SSE4.2, POPCNT and CMPXCHG16B (what -march=x86-64-v2 code may use).
static int x86_64_v2(void) {
    unsigned a, b, c, d;
    if (!__get_cpuid(1, &a, &b, &c, &d)) return 0;
    const unsigned need = (1u << 0) | (1u << 9) | (1u << 13) | (1u << 19) | (1u << 20) | (1u << 23);   // SSE3 SSSE3 CX16 SSE4.1 SSE4.2 POPCNT
    return (c & need) == need;
}
#else
// The x86-64 level gate exists only for x86 hosts: a non-x86 host (an Apple-silicon Mac) builds the native arm64
// runtime, so both levels pass.
static int x86_64_v3(void) {
    return 1;
}
static int x86_64_v2(void) {
    return 1;
}
#endif

int main(void) {
    printf("{\"x86_64_v3\": %s, \"x86_64_v2\": %s, ", x86_64_v3() ? "true" : "false", x86_64_v2() ? "true" : "false");
#ifdef _WIN32
    // The commit limit (RAM plus page file): what all processes together can commit before allocations fail.
    MEMORYSTATUSEX mem = {.dwLength = sizeof mem};
    if (GlobalMemoryStatusEx(&mem)) printf("\"commit_limit_gib\": %.1f, ", (double)mem.ullTotalPageFile / (1ull << 30));
#endif
    void *lib = open_lib();
    PFN_vkGetInstanceProcAddr gipa = lib ? (PFN_vkGetInstanceProcAddr)lib_sym(lib, "vkGetInstanceProcAddr") : NULL;
    if (!gipa) {
        printf("\"loader\": false, \"devices\": []}\n");
        return 0;
    }
    printf("\"loader\": true, ");
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "totk-install",
                             .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    // MoltenVK (macOS) is a portability driver: the loader enumerates its devices only when the instance asks for
    // the portability subset. Only on macOS: the Khronos loader offers the extension on Windows and Linux too, and
    // asking there would let a portability-only device through, where none was accepted before (runtime/vk_device.c).
#ifdef __APPLE__
    {
        PFN_vkEnumerateInstanceExtensionProperties exts = (PFN_vkEnumerateInstanceExtensionProperties)
            gipa(NULL, "vkEnumerateInstanceExtensionProperties");
        uint32_t n_exts = 0;
        if (exts && exts(NULL, &n_exts, NULL) == VK_SUCCESS && n_exts > 0) {
            VkExtensionProperties *ext_list = malloc(n_exts * sizeof *ext_list);
            if (ext_list && exts(NULL, &n_exts, ext_list) == VK_SUCCESS)
                for (uint32_t i = 0; i < n_exts; i++)
                    if (!strcmp(ext_list[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
                        static const char *const ext_names[] = {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
                        ci.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
                        ci.enabledExtensionCount = 1;
                        ci.ppEnabledExtensionNames = ext_names;
                        break;
                    }
            free(ext_list);
        }
    }
#endif
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r = create ? create(&ci, NULL, &inst) : VK_ERROR_INITIALIZATION_FAILED;
    if (r != VK_SUCCESS) {
        printf("\"instance\": %d, \"devices\": []}\n", (int)r);
        return 0;
    }
    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties props = (PFN_vkGetPhysicalDeviceProperties)gipa(inst, "vkGetPhysicalDeviceProperties");
    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)gipa(inst, "vkDestroyInstance");
    VkPhysicalDevice devs[16];
    uint32_t n = 16;
    if (enumerate(inst, &n, devs) < 0) n = 0;
    printf("\"instance\": 0, \"devices\": [");
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        props(devs[i], &p);
        printf("%s{\"name\": ", i ? ", " : "");
        json_string(p.deviceName);
        printf(", \"api\": [%u, %u, %u], \"type\": %d}", VK_API_VERSION_MAJOR(p.apiVersion),
               VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion), (int)p.deviceType);
    }
    printf("]}\n");
    destroy(inst, NULL);
    return 0;
}
