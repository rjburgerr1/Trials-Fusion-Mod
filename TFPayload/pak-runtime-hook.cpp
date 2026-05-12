#include "pch.h"
#include "pak-runtime-hook.h"
#include "base-address.h"
#include "keybindings.h"
#include "logging.h"
#include <MinHook.h>
#include <algorithm>
#include <cstring>
#include <intrin.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace PakRuntimeHook {

    static constexpr uintptr_t GHIDRA_BASE_UPLAY = 0x00700000;
    static constexpr uintptr_t LOAD_OBJECT_COLLECTION_AND_INITIALIZE_GAMEPLAY_ADDR_UPLAY = 0x00a98800;
    static constexpr uintptr_t LOAD_OBJECT_COLLECTION_XML_ADDR_UPLAY = 0x00a958f0;
    static constexpr uintptr_t LOAD_AND_CACHE_TEXTURE_FROM_FILE_ADDR_UPLAY = 0x00a94290;
    static constexpr uintptr_t LOAD_AND_CACHE_GFX_RESOURCE_ADDR_UPLAY = 0x00e37880;
    static constexpr uintptr_t LOAD_WORLD_MESH_SCENE_ADDR_UPLAY = 0x00d85e60;
    static constexpr uintptr_t RECURSIVE_SCENE_OBJECT_PHYSICS_STATE_UPDATE_ADDR_UPLAY = 0x0096d300;
    static constexpr uintptr_t OBJECT_LOADER_JOB_CONSTRUCTOR_ADDR_UPLAY = 0x00d7cc80;
    static constexpr uintptr_t INSERT_HASH_TABLE_ADDR_UPLAY = 0x00d7dd30;

    static constexpr uintptr_t LOAD_OBJECT_COLLECTION_AND_INITIALIZE_GAMEPLAY_RVA_STEAM = 0;
    static constexpr uintptr_t LOAD_OBJECT_COLLECTION_XML_RVA_STEAM = 0;
    static constexpr uintptr_t LOAD_AND_CACHE_TEXTURE_FROM_FILE_RVA_STEAM = 0;
    static constexpr uintptr_t LOAD_AND_CACHE_GFX_RESOURCE_RVA_STEAM = 0;
    static constexpr uintptr_t LOAD_WORLD_MESH_SCENE_RVA_STEAM = 0;
    static constexpr uintptr_t RECURSIVE_SCENE_OBJECT_PHYSICS_STATE_UPDATE_RVA_STEAM = 0;
    static constexpr uintptr_t OBJECT_LOADER_JOB_CONSTRUCTOR_RVA_STEAM = 0;
    static constexpr uintptr_t INSERT_HASH_TABLE_RVA_STEAM = 0;

    static constexpr uintptr_t PACKAGE_MANAGER_GLOBAL_ADDR_UPLAY = 0x01755298;
    static constexpr uintptr_t PENDING_OBJECT_LOADER_TABLE_ADDR_UPLAY = 0x017563d0;
    static constexpr size_t PACKAGE_MANAGER_VTABLE_OFFSET_20 = 0x20;
    static constexpr size_t PACKAGE_MANAGER_VTABLE_OFFSET_24 = 0x24;
    static constexpr size_t RESOURCE_STREAM_VTABLE_OFFSET_READ_AT = 0x1c;
    static constexpr size_t RESOURCE_STREAM_VTABLE_OFFSET_READ_CURRENT = 0x20;

    typedef void(__fastcall* LoadObjectCollectionAndInitializeGameplayFunc)(int param1);
    typedef void(__cdecl* LoadObjectCollectionXMLFunc)();
    typedef void(__fastcall* LoadAndCacheTextureFromFileFunc)(
        void* thisPtr, void* edxUnused, const char* path, int* output);
    typedef uintptr_t(__fastcall* LoadAndCacheGfxResourceFunc)(
        void* thisPtr, void* edxUnused, const char* path, int param2);
    typedef uint32_t(__fastcall* LoadWorldMeshSceneFunc)(int* sceneInput);
    typedef void(__thiscall* RecursiveSceneObjectPhysicsStateUpdateFunc)(
        void* thisPtr, int* sceneObject, int* enableFlag, uint32_t* stateOut, void* inheritedActive);
    typedef uintptr_t(__fastcall* ObjectLoaderJobConstructorFunc)(
        void* thisPtr, void* edxUnused, uintptr_t owner, void* pathA, void* pathB, uint8_t flag);
    typedef void(__fastcall* InsertHashTableFunc)(
        void* thisPtr, void* edxUnused, int* keyPtr, uintptr_t* valuePtr);
    typedef uintptr_t(__fastcall* PackageApi20Func)(
        void* thisPtr, void* edxUnused, const char* path, uint32_t arg1);
    typedef uintptr_t(__fastcall* PackageApi24Func)(
        void* thisPtr, void* edxUnused, const char* path, uint32_t arg1, uint32_t arg2);
    typedef uint32_t(__fastcall* ResourceStreamReadCurrentFunc)(
        void* thisPtr, void* edxUnused, void* outBuffer, size_t readSize);
    typedef uint32_t(__fastcall* ResourceStreamReadAtFunc)(
        void* thisPtr, void* edxUnused, void* outBuffer, uint32_t offset, size_t readSize);

    static uintptr_t g_baseAddress = 0;
    static bool g_initialized = false;

    static uintptr_t g_loadObjectCollectionAndInitializeGameplayTarget = 0;
    static uintptr_t g_loadObjectCollectionXMLTarget = 0;
    static uintptr_t g_loadAndCacheTextureFromFileTarget = 0;
    static uintptr_t g_loadAndCacheGfxResourceTarget = 0;
    static uintptr_t g_loadWorldMeshSceneTarget = 0;
    static uintptr_t g_recursiveSceneObjectPhysicsStateUpdateTarget = 0;
    static uintptr_t g_objectLoaderJobConstructorTarget = 0;
    static uintptr_t g_insertHashTableTarget = 0;

    static LoadObjectCollectionAndInitializeGameplayFunc g_originalLoadObjectCollectionAndInitializeGameplay = nullptr;
    static LoadObjectCollectionXMLFunc g_originalLoadObjectCollectionXML = nullptr;
    static LoadAndCacheTextureFromFileFunc g_originalLoadAndCacheTextureFromFile = nullptr;
    static LoadAndCacheGfxResourceFunc g_originalLoadAndCacheGfxResource = nullptr;
    static LoadWorldMeshSceneFunc g_originalLoadWorldMeshScene = nullptr;
    static RecursiveSceneObjectPhysicsStateUpdateFunc g_originalRecursiveSceneObjectPhysicsStateUpdate = nullptr;
    static ObjectLoaderJobConstructorFunc g_originalObjectLoaderJobConstructor = nullptr;
    static InsertHashTableFunc g_originalInsertHashTable = nullptr;

    static volatile LONG g_pendingObjectCollectionReload = 0;
    static volatile LONG g_reloadInProgress = 0;
    static volatile LONG g_continueLoaderReplayAfterCacheEvict = 0;

    static int g_lastGameplayLoaderParam1 = 0;
    static volatile LONG g_gameplayLoaderContextCaptured = 0;
    static volatile LONG g_gameplayLoaderNaturalCallCount = 0;

    static uintptr_t g_lastObjectCollectionECX = 0;
    static uintptr_t g_lastObjectCollectionEDX = 0;
    static uintptr_t g_lastObjectCollectionEDI = 0;
    static volatile LONG g_objectCollectionContextCaptured = 0;
    static volatile LONG g_objectCollectionNaturalCallCount = 0;

    static uintptr_t g_packageManagerObjectAddress = 0;
    static void** g_packageManagerVtable = nullptr;
    static uintptr_t g_packageApi20Target = 0;
    static uintptr_t g_packageApi24Target = 0;
    static PackageApi20Func g_originalPackageApi20 = nullptr;
    static PackageApi24Func g_originalPackageApi24 = nullptr;
    static bool g_packageApiHooksInstalled = false;
    static uintptr_t g_resourceReadCurrentTarget = 0;
    static ResourceStreamReadCurrentFunc g_originalResourceReadCurrent = nullptr;
    static bool g_resourceReadCurrentHookInstalled = false;
    static uintptr_t g_resourceReadAtTarget = 0;
    static ResourceStreamReadAtFunc g_originalResourceReadAt = nullptr;
    static bool g_resourceReadAtHookInstalled = false;
    static volatile LONG g_cacheLoadDepth = 0;
    static volatile LONG g_cacheLoadOpenedPackage = 0;
    static volatile LONG g_cacheLoadPackageSucceeded = 0;
    static uintptr_t g_cacheLoadPackageResult = 0;
    static uintptr_t g_cacheLoadPackageResultVtable = 0;
    static std::string g_currentCacheLoadPath;
    static std::string g_lastCachePath;
    static uintptr_t g_lastCacheManager = 0;
    static uintptr_t g_lastCacheEntry = 0;
    static uint32_t g_lastCacheHash = 0;
    static uint32_t g_lastCacheIndex = 0;
    static uint32_t g_lastCacheCapacity = 0;
    static uintptr_t g_lastCacheCountAddress = 0;
    static uint32_t g_lastCachePriority = 0;
    static int g_lastCacheParam2 = 0;
    static std::string g_lastCacheKind;
    static volatile LONG g_packageApiRetryInProgress = 0;
    static uint32_t g_packageApiRetryFrameCountdown = 0;
    static uint32_t g_packageApiRetryAttempts = 0;

    static uintptr_t g_lastInterestingPackageResult = 0;
    static uintptr_t g_lastInterestingPackageResultVtable = 0;
    static std::string g_lastInterestingPackagePath;
    static uintptr_t g_lastCodexWorldMeshSceneInput = 0;
    static uintptr_t g_lastCodexWorldMeshSceneOwner = 0;
    static uintptr_t g_lastCodexWorldMeshSceneBefore = 0;
    static uintptr_t g_lastCodexWorldMeshSceneAfter = 0;
    static std::string g_lastCodexWorldMeshScenePath;
    static uintptr_t g_lastCodexObjectLoaderOwner = 0;
    static uint32_t g_lastCodexObjectLoaderKey = 0;
    static uintptr_t g_lastCodexObjectLoaderJob = 0;
    static std::string g_lastCodexObjectLoaderPathA;
    static std::string g_lastCodexObjectLoaderPathB;

    struct CapturedCodexGfxTarget {
        std::string path;
        uintptr_t cacheManager = 0;
        uintptr_t cacheEntry = 0;
        uint32_t cacheHash = 0;
        uint32_t cacheIndex = 0;
        uint32_t cacheCapacity = 0;
        uintptr_t cacheCountAddress = 0;
        int cacheParam2 = 0;
        uintptr_t sceneInput = 0;
        uintptr_t sceneOwner = 0;
        uintptr_t sceneResource = 0;
        uint32_t ownerKey = 0;
        bool cacheCaptured = false;
        bool sceneCaptured = false;
    };

    static volatile LONG g_forcedGfxReloadDepth = 0;
    static bool g_forcedGfxScaleLarge = false;
    static float g_forcedGfxScaleValue = 1.0f;
    static bool g_pendingExplicitGfxScaleReload = false;
    static bool g_forcedGfxPatchThisReload = false;
    static std::string g_forcedGfxReloadPath;
    static bool g_publishReloadedGfxToCapturedOwner = true;
    static volatile LONG g_physicsProbeLogBudget = 240;
    static volatile LONG g_physicsProbeScanCalls = 0;
    static thread_local int t_physicsProbeDepth = 0;

    static std::mutex g_packagePathMutex;
    static std::unordered_map<std::string, uint32_t> g_packagePathHitCounts;
    static std::unordered_map<uintptr_t, uint32_t> g_resourceVtableDumpCounts;
    static std::unordered_map<uintptr_t, std::string> g_resourceObjectPaths;
    static std::unordered_map<uintptr_t, uint32_t> g_resourceReadLogCounts;
    static std::unordered_map<std::string, CapturedCodexGfxTarget> g_capturedCodexGfxTargets;

    static uintptr_t GetLoadObjectCollectionAndInitializeGameplayRVA() {
        if (BaseAddress::IsSteamVersion()) {
            return LOAD_OBJECT_COLLECTION_AND_INITIALIZE_GAMEPLAY_RVA_STEAM;
        }

        return LOAD_OBJECT_COLLECTION_AND_INITIALIZE_GAMEPLAY_ADDR_UPLAY - GHIDRA_BASE_UPLAY;
    }

    static uintptr_t GetLoadObjectCollectionXMLRVA() {
        if (BaseAddress::IsSteamVersion()) {
            return LOAD_OBJECT_COLLECTION_XML_RVA_STEAM;
        }

        return LOAD_OBJECT_COLLECTION_XML_ADDR_UPLAY - GHIDRA_BASE_UPLAY;
    }

    static uintptr_t GetLoadAndCacheTextureFromFileRVA() {
        if (BaseAddress::IsSteamVersion()) {
            return LOAD_AND_CACHE_TEXTURE_FROM_FILE_RVA_STEAM;
        }

        return LOAD_AND_CACHE_TEXTURE_FROM_FILE_ADDR_UPLAY - GHIDRA_BASE_UPLAY;
    }

    static uintptr_t GetLoadAndCacheGfxResourceRVA() {
        if (BaseAddress::IsSteamVersion()) {
            return LOAD_AND_CACHE_GFX_RESOURCE_RVA_STEAM;
        }

        return LOAD_AND_CACHE_GFX_RESOURCE_ADDR_UPLAY - GHIDRA_BASE_UPLAY;
    }

    static uintptr_t GetLoadWorldMeshSceneRVA() {
        if (BaseAddress::IsSteamVersion()) {
            return LOAD_WORLD_MESH_SCENE_RVA_STEAM;
        }

        return LOAD_WORLD_MESH_SCENE_ADDR_UPLAY - GHIDRA_BASE_UPLAY;
    }

    static uintptr_t GetRecursiveSceneObjectPhysicsStateUpdateRVA() {
        if (BaseAddress::IsSteamVersion()) {
            return RECURSIVE_SCENE_OBJECT_PHYSICS_STATE_UPDATE_RVA_STEAM;
        }

        return RECURSIVE_SCENE_OBJECT_PHYSICS_STATE_UPDATE_ADDR_UPLAY - GHIDRA_BASE_UPLAY;
    }

    static uintptr_t GetObjectLoaderJobConstructorRVA() {
        if (BaseAddress::IsSteamVersion()) {
            return OBJECT_LOADER_JOB_CONSTRUCTOR_RVA_STEAM;
        }

        return OBJECT_LOADER_JOB_CONSTRUCTOR_ADDR_UPLAY - GHIDRA_BASE_UPLAY;
    }

    static uintptr_t GetInsertHashTableRVA() {
        if (BaseAddress::IsSteamVersion()) {
            return INSERT_HASH_TABLE_RVA_STEAM;
        }

        return INSERT_HASH_TABLE_ADDR_UPLAY - GHIDRA_BASE_UPLAY;
    }

    static uintptr_t RuntimeAddressFromGhidra(uintptr_t ghidraAddress) {
        return g_baseAddress + (ghidraAddress - GHIDRA_BASE_UPLAY);
    }

    static void CallOriginalLoadObjectCollectionXML_WithRegs(
        uintptr_t ecxValue, uintptr_t edxValue, uintptr_t ediValue) {
        LoadObjectCollectionXMLFunc fn = g_originalLoadObjectCollectionXML;
        __asm {
            mov ecx, ecxValue
            mov edx, edxValue
            mov edi, ediValue
            call fn
        }
    }

    static bool CallLoadObjectCollectionAndInitializeGameplay_Inner(int param1, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_originalLoadObjectCollectionAndInitializeGameplay(param1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CallLoadObjectCollectionXML_Inner(DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            CallOriginalLoadObjectCollectionXML_WithRegs(
                g_lastObjectCollectionECX,
                g_lastObjectCollectionEDX,
                g_lastObjectCollectionEDI);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool TryCopyCString(const char* source, std::string* outText) {
        outText->clear();
        if (source == nullptr) {
            return false;
        }

        __try {
            for (size_t i = 0; i < 512; ++i) {
                const char ch = source[i];
                if (ch == '\0') {
                    return !outText->empty();
                }

                if (static_cast<unsigned char>(ch) < 0x20 ||
                    static_cast<unsigned char>(ch) > 0x7e) {
                    outText->clear();
                    return false;
                }

                outText->push_back(ch);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            outText->clear();
            return false;
        }

        return !outText->empty();
    }

    static bool TryCopyAsciiBytes(const char* source, size_t length, std::string* outText) {
        outText->clear();
        if (source == nullptr || length == 0 || length >= 512) {
            return false;
        }

        __try {
            for (size_t i = 0; i < length; ++i) {
                const char ch = source[i];
                if (static_cast<unsigned char>(ch) < 0x20 ||
                    static_cast<unsigned char>(ch) > 0x7e) {
                    outText->clear();
                    return false;
                }

                outText->push_back(ch);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            outText->clear();
            return false;
        }

        return !outText->empty();
    }

    static bool IsLikelyPackagePathText(const std::string& text) {
        if (text.empty() || text.size() >= 512) {
            return false;
        }

        return text.find('/') != std::string::npos ||
            text.find('\\') != std::string::npos ||
            text.find('.') != std::string::npos ||
            text.find(':') != std::string::npos ||
            text.find("<evo2_") != std::string::npos;
    }

    static bool TryCopyGameString(void* stringObject, std::string* outText) {
        outText->clear();
        if (stringObject == nullptr) {
            return false;
        }

        __try {
            const uintptr_t base = reinterpret_cast<uintptr_t>(stringObject);
            const uint32_t meta = *reinterpret_cast<uint32_t*>(base + 4);
            const uint16_t length = static_cast<uint16_t>(meta >> 16);
            const char* buffer = *reinterpret_cast<const char**>(base + 8);
            return TryCopyAsciiBytes(buffer, length, outText);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            outText->clear();
            return false;
        }
    }

    static bool TryCopyPackagePathArgument(const char* source, std::string* outText) {
        if (TryCopyCString(source, outText) && IsLikelyPackagePathText(*outText)) {
            return true;
        }

        if (TryCopyGameString(const_cast<char*>(source), outText) && IsLikelyPackagePathText(*outText)) {
            return true;
        }

        outText->clear();
        return false;
    }

    static std::string ToLowerCopy(const std::string& text) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return lower;
    }

    static bool EndsWith(const std::string& text, const char* suffix) {
        const size_t suffixLen = strlen(suffix);
        return text.size() >= suffixLen &&
            text.compare(text.size() - suffixLen, suffixLen, suffix) == 0;
    }

    static bool IsInterestingPackagePath(const std::string& lowerPath) {
        if (lowerPath.find("objectcollection") != std::string::npos) {
            return true;
        }

        if (EndsWith(lowerPath, ".pak")) {
            return true;
        }

        if (lowerPath.find("codex") != std::string::npos) {
            return true;
        }

        if (EndsWith(lowerPath, ".gfx")) {
            return true;
        }

        if (EndsWith(lowerPath, ".mdl")) {
            return true;
        }

        return false;
    }

    static uint32_t GetEvictionPathPriority(const std::string& lowerPath) {
        if (lowerPath.empty()) {
            return 0;
        }

        uint32_t priority = 0;
        if (EndsWith(lowerPath, ".gfx")) {
            priority = 100;
        }
        else if (EndsWith(lowerPath, ".mdl")) {
            priority = 90;
        }
        else if (lowerPath.find("objectcollection") != std::string::npos) {
            priority = 10;
        }

        if (lowerPath.find("codex") != std::string::npos) {
            priority += 1000;
        }
        if (lowerPath.find("/test/") != std::string::npos ||
            lowerPath.find("\\test\\") != std::string::npos ||
            lowerPath.find("custom") != std::string::npos) {
            priority += 900;
        }

        return priority;
    }

    static bool IsPreferredEvictionPath(const std::string& lowerPath) {
        return GetEvictionPathPriority(lowerPath) > 0;
    }

    static bool IsCodexCustomGfxPath(const std::string& lowerPath) {
        return EndsWith(lowerPath, ".gfx") &&
            lowerPath.find("codex") != std::string::npos &&
            (lowerPath.find("custom") != std::string::npos ||
                lowerPath.find("/codex/") != std::string::npos ||
                lowerPath.find("\\codex\\") != std::string::npos);
    }

    static bool IsInterestingObjectLoaderPath(
        const std::string& lowerPathA,
        const std::string& lowerPathB) {
        return lowerPathA.find("codex") != std::string::npos ||
            lowerPathB.find("codex") != std::string::npos ||
            EndsWith(lowerPathA, ".gfx") ||
            EndsWith(lowerPathB, ".gfx");
    }

    static bool ShouldLogPathHit(const std::string& lowerPath, uint32_t* hitCountOut) {
        std::lock_guard<std::mutex> lock(g_packagePathMutex);
        uint32_t& hitCount = g_packagePathHitCounts[lowerPath];
        ++hitCount;
        *hitCountOut = hitCount;

        if (lowerPath.find("codex") != std::string::npos) {
            return true;
        }

        if (lowerPath.find("objectcollection") != std::string::npos) {
            return true;
        }

        if (EndsWith(lowerPath, ".pak")) {
            return hitCount <= 4;
        }

        if (EndsWith(lowerPath, ".gfx") || EndsWith(lowerPath, ".mdl")) {
            return hitCount <= 4;
        }

        return hitCount <= 2;
    }

    static bool ShouldDumpResourceVtable(uintptr_t vtable, uint32_t* dumpCountOut) {
        if (vtable == 0) {
            return false;
        }

        std::lock_guard<std::mutex> lock(g_packagePathMutex);
        uint32_t& dumpCount = g_resourceVtableDumpCounts[vtable];
        ++dumpCount;
        *dumpCountOut = dumpCount;
        return dumpCount == 1;
    }

    static void TrackResourceObject(uintptr_t objectAddress, const std::string& pathText) {
        if (objectAddress == 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(g_packagePathMutex);
        g_resourceObjectPaths[objectAddress] = pathText;
    }

    static bool GetTrackedResourceReadInfo(
        uintptr_t objectAddress,
        std::string* pathText,
        uint32_t* readCountOut) {
        std::lock_guard<std::mutex> lock(g_packagePathMutex);
        auto pathIt = g_resourceObjectPaths.find(objectAddress);
        if (pathIt == g_resourceObjectPaths.end()) {
            return false;
        }

        *pathText = pathIt->second;
        uint32_t& readCount = g_resourceReadLogCounts[objectAddress];
        ++readCount;
        *readCountOut = readCount;
        return true;
    }

    static void InstallResourceReadHook(uintptr_t resourceVtable);
    static uint32_t __fastcall Hook_ResourceStreamReadCurrent(
        void* thisPtr, void* edxUnused, void* outBuffer, size_t readSize);
    static uint32_t __fastcall Hook_ResourceStreamReadAt(
        void* thisPtr, void* edxUnused, void* outBuffer, uint32_t offset, size_t readSize);

    static bool CaptureCacheEvictionTarget(
        const char* kind,
        const std::string& pathText,
        uintptr_t ownerObject,
        uintptr_t cacheEntry,
        uint32_t pathHash,
        uint32_t cacheIndex,
        uint32_t cacheCapacity,
        uintptr_t countAddress,
        uint32_t priority,
        int param2 = 0) {
        std::lock_guard<std::mutex> lock(g_packagePathMutex);
        if (priority < g_lastCachePriority) {
            return false;
        }

        g_lastCacheKind = kind;
        g_lastCachePath = pathText;
        g_lastCacheManager = ownerObject;
        g_lastCacheEntry = cacheEntry;
        g_lastCacheHash = pathHash;
        g_lastCacheIndex = cacheIndex;
        g_lastCacheCapacity = cacheCapacity;
        g_lastCacheCountAddress = countAddress;
        g_lastCachePriority = priority;
        g_lastCacheParam2 = param2;

        if (strcmp(kind, "GfxResource") == 0 &&
            IsCodexCustomGfxPath(ToLowerCopy(pathText))) {
            CapturedCodexGfxTarget& target = g_capturedCodexGfxTargets[pathText];
            target.path = pathText;
            target.cacheManager = ownerObject;
            target.cacheEntry = cacheEntry;
            target.cacheHash = pathHash;
            target.cacheIndex = cacheIndex;
            target.cacheCapacity = cacheCapacity;
            target.cacheCountAddress = countAddress;
            target.cacheParam2 = param2;
            target.cacheCaptured = true;
        }
        return true;
    }

    static void BeginCacheLoadTracking(const std::string& pathText) {
        std::lock_guard<std::mutex> lock(g_packagePathMutex);
        g_currentCacheLoadPath = pathText;
        InterlockedExchange(&g_cacheLoadOpenedPackage, 0);
        InterlockedExchange(&g_cacheLoadPackageSucceeded, 0);
        g_cacheLoadPackageResult = 0;
        g_cacheLoadPackageResultVtable = 0;
        InterlockedIncrement(&g_cacheLoadDepth);
    }

    static void EndCacheLoadTracking(
        bool* openedPackageOut,
        bool* packageSucceededOut,
        uintptr_t* packageResultOut,
        uintptr_t* packageResultVtableOut) {
        *openedPackageOut = InterlockedCompareExchange(&g_cacheLoadOpenedPackage, 0, 0) != 0;
        *packageSucceededOut =
            InterlockedCompareExchange(&g_cacheLoadPackageSucceeded, 0, 0) != 0;
        {
            std::lock_guard<std::mutex> lock(g_packagePathMutex);
            *packageResultOut = g_cacheLoadPackageResult;
            *packageResultVtableOut = g_cacheLoadPackageResultVtable;
            g_currentCacheLoadPath.clear();
        }
        InterlockedDecrement(&g_cacheLoadDepth);
    }

    static void MarkCacheLoadPackageOpenIfMatching(
        const std::string& pathText,
        uintptr_t result,
        uintptr_t resultVtable) {
        if (InterlockedCompareExchange(&g_cacheLoadDepth, 0, 0) <= 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(g_packagePathMutex);
        if (!g_currentCacheLoadPath.empty() && g_currentCacheLoadPath == pathText) {
            InterlockedExchange(&g_cacheLoadOpenedPackage, 1);
            if (result != 0) {
                InterlockedExchange(&g_cacheLoadPackageSucceeded, 1);
            }
            g_cacheLoadPackageResult = result;
            g_cacheLoadPackageResultVtable = resultVtable;
        }
    }

    static uintptr_t TryReadVtable(void* objectPtr) {
        if (objectPtr == nullptr) {
            return 0;
        }

        __try {
            void* vtable = *reinterpret_cast<void**>(objectPtr);
            return reinterpret_cast<uintptr_t>(vtable);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    static uintptr_t TryReadPointer(uintptr_t address) {
        if (address == 0) {
            return 0;
        }

        __try {
            return *reinterpret_cast<uintptr_t*>(address);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    static bool TryReadDword(uintptr_t address, uint32_t* outValue) {
        *outValue = 0;
        if (address == 0) {
            return false;
        }

        __try {
            *outValue = *reinterpret_cast<uint32_t*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool TryReadFloat(uintptr_t address, float* outValue) {
        *outValue = 0.0f;
        if (address == 0) {
            return false;
        }

        __try {
            *outValue = *reinterpret_cast<float*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static uint8_t TryReadByteValue(uintptr_t address) {
        if (address == 0) {
            return 0;
        }

        __try {
            return *reinterpret_cast<uint8_t*>(address);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    static bool TryWriteByte(uintptr_t address, uint8_t value) {
        if (address == 0) {
            return false;
        }

        __try {
            *reinterpret_cast<uint8_t*>(address) = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool TryWriteDword(uintptr_t address, uint32_t value) {
        if (address == 0) {
            return false;
        }

        __try {
            *reinterpret_cast<uint32_t*>(address) = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool IsLikelyRuntimePointer(uintptr_t value) {
        if (value < 0x10000 || value == 0x3f800000) {
            return false;
        }

        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(value), &mbi, sizeof(mbi)) == 0) {
            return false;
        }

        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
            return false;
        }

        return true;
    }

    static void LogSceneTreeNode(
        const char* reason,
        const std::string& pathText,
        uintptr_t owner,
        uintptr_t scene,
        int depth,
        int index) {
        if (scene == 0 || depth > 4) {
            return;
        }

        const uint32_t typeFlags = static_cast<uint32_t>(TryReadPointer(scene + 0x8));
        const uint32_t childCount = static_cast<uint32_t>(TryReadPointer(scene + 0x10));
        const uintptr_t children = TryReadPointer(scene + 0x18);
        const uintptr_t word80 = TryReadPointer(scene + 0x80);
        const uintptr_t word84 = TryReadPointer(scene + 0x84);
        const uintptr_t word88 = TryReadPointer(scene + 0x88);
        const uintptr_t word8c = TryReadPointer(scene + 0x8c);
        const uintptr_t word90 = TryReadPointer(scene + 0x90);
        const uintptr_t word94 = TryReadPointer(scene + 0x94);
        const uintptr_t word98 = TryReadPointer(scene + 0x98);
        const uintptr_t word9c = TryReadPointer(scene + 0x9c);
        const uintptr_t worda0 = TryReadPointer(scene + 0xa0);
        const uintptr_t worda4 = TryReadPointer(scene + 0xa4);
        const uintptr_t worda8 = TryReadPointer(scene + 0xa8);
        const uintptr_t wordac = TryReadPointer(scene + 0xac);
        const uint8_t byteb0 = TryReadByteValue(scene + 0xb0);
        const uint8_t byteb1 = TryReadByteValue(scene + 0xb1);
        const uint8_t byteb2 = TryReadByteValue(scene + 0xb2);
        const uint8_t byteb3 = TryReadByteValue(scene + 0xb3);
        const uintptr_t wordb4 = TryReadPointer(scene + 0xb4);
        const uintptr_t wordb8 = TryReadPointer(scene + 0xb8);

        LOG_INFO("[PakRuntime/SceneTree] reason=" << reason
            << " depth=" << std::dec << depth
            << " index=" << index
            << " path=" << pathText
            << " owner=0x" << std::hex << owner
            << " scene=0x" << scene
            << " type=0x" << typeFlags
            << " child_count=0x" << childCount
            << " children=0x" << children
            << " w80=0x" << word80
            << " w84=0x" << word84
            << " w88=0x" << word88
            << " w8c=0x" << word8c
            << " w90=0x" << word90
            << " w94=0x" << word94
            << " w98=0x" << word98
            << " w9c=0x" << word9c
            << " wa0=0x" << worda0
            << " wa4=0x" << worda4
            << " wa8=0x" << worda8
            << " wac=0x" << wordac
            << " b0=0x" << static_cast<uint32_t>(byteb0)
            << " b1=0x" << static_cast<uint32_t>(byteb1)
            << " b2=0x" << static_cast<uint32_t>(byteb2)
            << " b3=0x" << static_cast<uint32_t>(byteb3)
            << " wb4=0x" << wordb4
            << " wb8=0x" << wordb8
            << " ptr_a0=" << (IsLikelyRuntimePointer(worda0) ? "yes" : "no")
            << " ptr_a8=" << (IsLikelyRuntimePointer(worda8) ? "yes" : "no"));

        if (children == 0 || childCount == 0) {
            return;
        }

        const uint32_t maxChildren = std::min<uint32_t>(childCount, 8);
        for (uint32_t childIndex = 0; childIndex < maxChildren; ++childIndex) {
            const uintptr_t child = TryReadPointer(children + childIndex * sizeof(uintptr_t));
            LOG_INFO("[PakRuntime/SceneTree] child-edge reason=" << reason
                << " depth=" << std::dec << depth
                << " index=" << index
                << " child_index=" << childIndex
                << " child=0x" << std::hex << child);
            LogSceneTreeNode(reason, pathText, owner, child, depth + 1, static_cast<int>(childIndex));
        }
    }

    static void LogSceneTreeSnapshot(
        const char* reason,
        const std::string& pathText,
        uintptr_t owner,
        uintptr_t scene) {
        LOG_INFO("[PakRuntime/SceneTree] snapshot-begin reason=" << reason
            << " path=" << pathText
            << " owner=0x" << std::hex << owner
            << " root=0x" << scene);
        LogSceneTreeNode(reason, pathText, owner, scene, 0, 0);
    }

    static bool ForceReloadGfxResource(
        uintptr_t cacheManager,
        const std::string& pathText,
        int param2,
        uintptr_t* parsedResourceOut,
        DWORD* exceptionCodeOut) {
        *parsedResourceOut = 0;
        *exceptionCodeOut = 0;

        if (g_originalLoadAndCacheGfxResource == nullptr ||
            cacheManager == 0 ||
            pathText.empty()) {
            return false;
        }

        struct GameStringView {
            uint32_t reserved;
            uint32_t meta;
            const char* buffer;
        };

        if (pathText.size() > 0xffff) {
            return false;
        }

        GameStringView pathArg = {};
        pathArg.meta = static_cast<uint32_t>(pathText.size()) << 16;
        pathArg.buffer = pathText.c_str();

        __try {
            *parsedResourceOut = g_originalLoadAndCacheGfxResource(
                reinterpret_cast<void*>(cacheManager),
                nullptr,
                reinterpret_cast<const char*>(&pathArg),
                param2);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCodeOut = GetExceptionCode();
            return false;
        }
    }

    static bool IsCapturedCodexSceneResource(uintptr_t sceneObject, std::string* pathOut) {
        if (sceneObject == 0) {
            return false;
        }

        std::lock_guard<std::mutex> lock(g_packagePathMutex);
        for (const auto& item : g_capturedCodexGfxTargets) {
            const CapturedCodexGfxTarget& target = item.second;
            if (target.sceneCaptured && target.sceneResource == sceneObject) {
                if (pathOut != nullptr) {
                    *pathOut = target.path;
                }
                return true;
            }
        }
        return false;
    }

    static bool IsCurrentCodexOwnerScene(uintptr_t sceneObject, std::string* pathOut) {
        if (sceneObject == 0) {
            return false;
        }

        std::lock_guard<std::mutex> lock(g_packagePathMutex);
        for (const auto& item : g_capturedCodexGfxTargets) {
            const CapturedCodexGfxTarget& target = item.second;
            if (target.sceneOwner == 0) {
                continue;
            }

            const uintptr_t ownerScene = TryReadPointer(target.sceneOwner + 0x68);
            if (ownerScene == sceneObject) {
                if (pathOut != nullptr) {
                    *pathOut = target.path;
                }
                return true;
            }
        }
        return false;
    }

    static void BeginPhysicsProbeScan(
        const char* reason,
        const std::string& pathText,
        uintptr_t owner,
        uintptr_t scene,
        LONG callBudget) {
        InterlockedExchange(&g_physicsProbeLogBudget, callBudget * 2);
        InterlockedExchange(&g_physicsProbeScanCalls, callBudget);
        LOG_INFO("[PakRuntime/PhysicsProbe] scan-begin reason=" << reason
            << " path=" << pathText
            << " owner=0x" << std::hex << owner
            << " owner_scene=0x" << scene
            << " call_budget=" << std::dec << callBudget);
    }

    static float ReadFloatOrZero(uintptr_t address) {
        float value = 0.0f;
        TryReadFloat(address, &value);
        return value;
    }

    static void LogPhysicsProbeState(
        const char* phase,
        const char* tag,
        void* physicsWorld,
        int* sceneObject,
        int* enableFlag,
        uint32_t* stateOut,
        void* inheritedActive,
        const std::string& rootPath) {
        if (InterlockedCompareExchange(&g_physicsProbeLogBudget, 0, 0) <= 0) {
            return;
        }
        InterlockedDecrement(&g_physicsProbeLogBudget);

        const uintptr_t scene = reinterpret_cast<uintptr_t>(sceneObject);
        const uintptr_t bodyTemplate = scene != 0 ? TryReadPointer(scene + 0xa0) : 0;
        const uintptr_t bodyActive = scene != 0 ? TryReadPointer(scene + 0xa8) : 0;
        const uint8_t bodyGate = scene != 0 ? TryReadByteValue(scene + 0xb0) : 0;
        const uint8_t sceneFlagsB2 = scene != 0 ? TryReadByteValue(scene + 0xb2) : 0;
        const uint32_t sceneTypeFlags = scene != 0 ? static_cast<uint32_t>(TryReadPointer(scene + 0x8)) : 0;
        const uint32_t childCount = scene != 0 ? static_cast<uint32_t>(TryReadPointer(scene + 0x10)) : 0;
        const uintptr_t children = scene != 0 ? TryReadPointer(scene + 0x18) : 0;
        const uintptr_t body = bodyActive != 0 ? bodyActive : bodyTemplate;

        LOG_INFO("[PakRuntime/PhysicsProbe] " << phase
            << " tag=" << tag
            << " depth=" << std::dec << t_physicsProbeDepth
            << " path=" << rootPath
            << " world=0x" << std::hex << reinterpret_cast<uintptr_t>(physicsWorld)
            << " scene=0x" << scene
            << " enable=0x" << reinterpret_cast<uintptr_t>(enableFlag)
            << " state_out=0x" << reinterpret_cast<uintptr_t>(stateOut)
            << " inherited=0x" << reinterpret_cast<uintptr_t>(inheritedActive)
            << " scene_type_flags=0x" << sceneTypeFlags
            << " child_count=0x" << childCount
            << " children=0x" << children
            << " flags_b2=0x" << static_cast<uint32_t>(sceneFlagsB2)
            << " body_gate=0x" << static_cast<uint32_t>(bodyGate)
            << " body_template=0x" << bodyTemplate
            << " body_active=0x" << bodyActive);

        if (body != 0) {
            LOG_INFO("[PakRuntime/PhysicsProbe] " << phase
                << " tag=" << tag
                << " body=0x" << std::hex << body
                << " row0=(" << std::dec
                << ReadFloatOrZero(body + 0x10) << ", "
                << ReadFloatOrZero(body + 0x14) << ", "
                << ReadFloatOrZero(body + 0x18) << ")"
                << " row1=("
                << ReadFloatOrZero(body + 0x20) << ", "
                << ReadFloatOrZero(body + 0x24) << ", "
                << ReadFloatOrZero(body + 0x28) << ")"
                << " row2=("
                << ReadFloatOrZero(body + 0x30) << ", "
                << ReadFloatOrZero(body + 0x34) << ", "
                << ReadFloatOrZero(body + 0x38) << ")"
                << " pos=("
                << ReadFloatOrZero(body + 0x40) << ", "
                << ReadFloatOrZero(body + 0x44) << ", "
                << ReadFloatOrZero(body + 0x48) << ")"
                << " v160=("
                << ReadFloatOrZero(body + 0x160) << ", "
                << ReadFloatOrZero(body + 0x164) << ", "
                << ReadFloatOrZero(body + 0x168) << ")");
        }
    }

    static void __fastcall Hook_RecursiveSceneObjectPhysicsStateUpdate(
        void* thisPtr,
        void* edxUnused,
        int* sceneObject,
        int* enableFlag,
        uint32_t* stateOut,
        void* inheritedActive) {
        (void)edxUnused;

        std::string rootPath;
        const bool rootMatch =
            IsCapturedCodexSceneResource(reinterpret_cast<uintptr_t>(sceneObject), &rootPath);
        const bool ownerSceneMatch =
            !rootMatch &&
            IsCurrentCodexOwnerScene(reinterpret_cast<uintptr_t>(sceneObject), &rootPath);
        const bool inheritedMatch = t_physicsProbeDepth > 0;
        const LONG remainingScanCalls =
            InterlockedCompareExchange(&g_physicsProbeScanCalls, 0, 0);
        const bool scanMatch =
            !rootMatch &&
            !ownerSceneMatch &&
            !inheritedMatch &&
            remainingScanCalls > 0 &&
            InterlockedDecrement(&g_physicsProbeScanCalls) >= 0;
        const bool shouldProbe = rootMatch || ownerSceneMatch || inheritedMatch || scanMatch;
        if (rootMatch || ownerSceneMatch) {
            InterlockedExchange(&g_physicsProbeLogBudget, 240);
        }

        if (shouldProbe) {
            if (rootPath.empty()) {
                rootPath = scanMatch ? "scan-window" : "captured-child";
            }
            ++t_physicsProbeDepth;
            LogPhysicsProbeState(
                "before",
                rootMatch ? "root" : (ownerSceneMatch ? "owner-scene" : (scanMatch ? "scan" : "child")),
                thisPtr,
                sceneObject,
                enableFlag,
                stateOut,
                inheritedActive,
                rootPath);
        }

        g_originalRecursiveSceneObjectPhysicsStateUpdate(
            thisPtr,
            sceneObject,
            enableFlag,
            stateOut,
            inheritedActive);

        if (shouldProbe) {
            LogPhysicsProbeState(
                "after",
                rootMatch ? "root" : (ownerSceneMatch ? "owner-scene" : (scanMatch ? "scan" : "child")),
                thisPtr,
                sceneObject,
                enableFlag,
                stateOut,
                inheritedActive,
                rootPath);
            --t_physicsProbeDepth;
        }
    }

    static bool ForceReloadWorldMeshScene(
        uintptr_t sceneInput,
        uintptr_t* sceneBeforeOut,
        uintptr_t* sceneAfterOut,
        DWORD* exceptionCodeOut) {
        *sceneBeforeOut = 0;
        *sceneAfterOut = 0;
        *exceptionCodeOut = 0;

        if (g_originalLoadWorldMeshScene == nullptr || sceneInput == 0) {
            return false;
        }

        uintptr_t owner = 0;
        __try {
            owner = *reinterpret_cast<uintptr_t*>(sceneInput + 0x24);
            if (owner != 0) {
                *sceneBeforeOut = *reinterpret_cast<uintptr_t*>(owner + 0x68);
                *reinterpret_cast<uintptr_t*>(owner + 0x68) = 0;
            }

            g_originalLoadWorldMeshScene(reinterpret_cast<int*>(sceneInput));

            if (owner != 0) {
                *sceneAfterOut = *reinterpret_cast<uintptr_t*>(owner + 0x68);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCodeOut = GetExceptionCode();
            return false;
        }
    }

    static bool InvalidateCapturedWorldMeshScene(
        const std::string& expectedPath,
        uintptr_t* ownerOut,
        uintptr_t* sceneBeforeOut) {
        *ownerOut = 0;
        *sceneBeforeOut = 0;

        uintptr_t owner = 0;
        std::string scenePath;
        {
            std::lock_guard<std::mutex> lock(g_packagePathMutex);
            owner = g_lastCodexWorldMeshSceneOwner;
            scenePath = g_lastCodexWorldMeshScenePath;
        }

        if (owner == 0 || scenePath != expectedPath) {
            return false;
        }

        *ownerOut = owner;
        *sceneBeforeOut = TryReadPointer(owner + 0x68);
        return TryWriteDword(owner + 0x68, 0);
    }

    static uint32_t G4HashPathPreserveCase(const std::string& text) {
        uint32_t seed = static_cast<uint32_t>(text.size());
        uint32_t hash = 0;
        for (unsigned char ch : text) {
            hash = hash + static_cast<uint32_t>(ch) * seed;
            seed = ((seed & 0xffff) * 18000u) + (seed >> 16);
        }
        return hash;
    }

    static bool TryFindHashCacheEntry(
        uintptr_t ownerObject,
        uint32_t pathHash,
        uint32_t capacityOffset,
        uint32_t tableOffset,
        uintptr_t* entryAddressOut,
        uint32_t* valueOut,
        uint32_t* stateOut,
        uint32_t* capacityOut,
        uint32_t* indexOut) {
        *entryAddressOut = 0;
        *valueOut = 0;
        *stateOut = 0;
        *capacityOut = 0;
        *indexOut = 0;

        uint32_t capacity = 0;
        uint32_t table = 0;
        if (!TryReadDword(ownerObject + capacityOffset, &capacity) ||
            !TryReadDword(ownerObject + tableOffset, &table) ||
            capacity == 0 ||
            table == 0) {
            return false;
        }

        const uint32_t mask = capacity - 1;
        uint32_t hashProbe = pathHash + ~(pathHash << 15);
        hashProbe = (hashProbe >> 10 ^ hashProbe) * 9;
        hashProbe = hashProbe ^ (hashProbe >> 6);
        hashProbe = hashProbe + ~(hashProbe << 11);
        uint32_t index = (hashProbe >> 16 ^ hashProbe) & mask;

        for (uint32_t probe = 0; probe < capacity; ++probe) {
            const uintptr_t entryAddress = table + index * 0xc;
            uint32_t entryHash = 0;
            uint32_t entryValue = 0;
            uint32_t entryState = 0;
            if (!TryReadDword(entryAddress, &entryHash) ||
                !TryReadDword(entryAddress + 4, &entryValue) ||
                !TryReadDword(entryAddress + 8, &entryState)) {
                return false;
            }

            const uint8_t stateByte = static_cast<uint8_t>(entryState & 0xff);
            if (stateByte == 0) {
                return false;
            }

            if (stateByte == 2 && entryHash == pathHash) {
                *entryAddressOut = entryAddress;
                *valueOut = entryValue;
                *stateOut = stateByte;
                *capacityOut = capacity;
                *indexOut = index;
                return true;
            }

            if (stateByte != 1 && stateByte != 2) {
                return false;
            }

            index = (index + 1) & mask;
        }

        return false;
    }

    static bool TryFindResourceCacheEntry(
        uintptr_t resourceManager,
        uint32_t pathHash,
        uintptr_t* entryAddressOut,
        uint32_t* valueOut,
        uint32_t* stateOut,
        uint32_t* capacityOut,
        uint32_t* indexOut) {
        return TryFindHashCacheEntry(
            resourceManager,
            pathHash,
            0x264,
            0x268,
            entryAddressOut,
            valueOut,
            stateOut,
            capacityOut,
            indexOut);
    }

    static bool TryFindGfxCacheEntry(
        uintptr_t gfxCache,
        uint32_t pathHash,
        uintptr_t* entryAddressOut,
        uint32_t* valueOut,
        uint32_t* stateOut,
        uint32_t* capacityOut,
        uint32_t* indexOut) {
        return TryFindHashCacheEntry(
            gfxCache,
            pathHash,
            0x20,
            0x24,
            entryAddressOut,
            valueOut,
            stateOut,
            capacityOut,
            indexOut);
    }

    static bool TryRefindCapturedCacheEntry(
        const std::string& cacheKind,
        uintptr_t cacheManager,
        uint32_t cacheHash,
        uintptr_t* entryAddressOut,
        uint32_t* valueOut,
        uint32_t* stateOut,
        uint32_t* capacityOut,
        uint32_t* indexOut,
        uintptr_t* countAddressOut) {
        *entryAddressOut = 0;
        *valueOut = 0;
        *stateOut = 0;
        *capacityOut = 0;
        *indexOut = 0;
        *countAddressOut = 0;

        if (cacheKind == "GfxResource") {
            *countAddressOut = cacheManager + 0x14;
            return TryFindGfxCacheEntry(
                cacheManager,
                cacheHash,
                entryAddressOut,
                valueOut,
                stateOut,
                capacityOut,
                indexOut);
        }

        if (cacheKind == "ObjectCollectionMembership") {
            *countAddressOut = cacheManager + 0x258;
            return TryFindResourceCacheEntry(
                cacheManager,
                cacheHash,
                entryAddressOut,
                valueOut,
                stateOut,
                capacityOut,
                indexOut);
        }

        return false;
    }

    static std::string FormatHexSample(const void* buffer, size_t size) {
        static const char* digits = "0123456789abcdef";
        std::string sample;
        if (buffer == nullptr || size == 0) {
            return sample;
        }

        const size_t sampleSize = std::min<size_t>(size, 16);
        sample.reserve(sampleSize * 3);

        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(buffer);
        for (size_t i = 0; i < sampleSize; ++i) {
            if (i != 0) {
                sample.push_back(' ');
            }

            sample.push_back(digits[bytes[i] >> 4]);
            sample.push_back(digits[bytes[i] & 0xf]);
        }

        return sample;
    }

    static std::string FormatAsciiSample(const void* buffer, size_t size) {
        std::string sample;
        if (buffer == nullptr || size == 0) {
            return sample;
        }

        const size_t sampleSize = std::min<size_t>(size, 16);
        sample.reserve(sampleSize);

        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(buffer);
        for (size_t i = 0; i < sampleSize; ++i) {
            const unsigned char ch = bytes[i];
            sample.push_back((ch >= 0x20 && ch <= 0x7e) ? static_cast<char>(ch) : '.');
        }

        return sample;
    }

    static bool TryWriteFloatToBuffer(
        void* buffer,
        size_t bufferSize,
        size_t offset,
        float value) {
        if (buffer == nullptr || offset + sizeof(float) > bufferSize) {
            return false;
        }

        memcpy(static_cast<unsigned char*>(buffer) + offset, &value, sizeof(value));
        return true;
    }

    static uint32_t PatchCodexGfxTransformBuffer(
        const std::string& pathText,
        uint32_t absoluteOffset,
        void* buffer,
        size_t bytesRead) {
        if (buffer == nullptr || bytesRead == 0 ||
            InterlockedCompareExchange(&g_forcedGfxReloadDepth, 0, 0) <= 0 ||
            !g_forcedGfxPatchThisReload) {
            return 0;
        }

        std::string activePath;
        float scaleValue = 1.0f;
        {
            std::lock_guard<std::mutex> lock(g_packagePathMutex);
            activePath = g_forcedGfxReloadPath;
            scaleValue = g_forcedGfxScaleValue;
        }

        if (activePath.empty() || activePath != pathText) {
            return 0;
        }

        static const char* kMeshNames[] = {
            "codex_mesh_log1",
            "codex_mesh_0001",
            "woodenramp_half",
            "woodenramp_half_u30",
            "woodenramp_half_u45",
            "woodenramp_half_u60",
            "woodenramp_half_u75",
            "woodenramp_half_u90"
        };

        static constexpr int kScaleOffsets[] = { -48, -44, -40 };
        const unsigned char* readOnly = static_cast<const unsigned char*>(buffer);
        uint32_t patchedRefs = 0;

        for (const char* meshName : kMeshNames) {
            const size_t meshNameLength = strlen(meshName);
            if (meshNameLength == 0 || meshNameLength > bytesRead) {
                continue;
            }

            for (size_t i = 0; i + meshNameLength <= bytesRead; ++i) {
                if (memcmp(readOnly + i, meshName, meshNameLength) != 0) {
                    continue;
                }

                bool patchedThisRef = true;
                for (int relOffset : kScaleOffsets) {
                    if (relOffset < 0 && i < static_cast<size_t>(-relOffset)) {
                        patchedThisRef = false;
                        break;
                    }

                    const size_t writeOffset = static_cast<size_t>(
                        static_cast<intptr_t>(i) + relOffset);
                    if (!TryWriteFloatToBuffer(buffer, bytesRead, writeOffset, scaleValue)) {
                        patchedThisRef = false;
                        break;
                    }
                }

                if (patchedThisRef) {
                    ++patchedRefs;
                    LOG_INFO("[PakRuntime/GfxPatch] path=" << pathText
                        << " mesh=" << meshName
                        << " name_abs=0x" << std::hex << (absoluteOffset + i)
                        << " scale=" << std::dec << scaleValue);
                }
            }
        }

        return patchedRefs;
    }

    static bool PatchVtableEntry(
        void** vtable, size_t index, void* hookFunc, void** originalFunc) {
        if (vtable == nullptr) {
            return false;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }

        *originalFunc = vtable[index];
        vtable[index] = hookFunc;
        VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
        return true;
    }

    static bool RestoreVtableEntry(void** vtable, size_t index, void* originalFunc) {
        if (vtable == nullptr || originalFunc == nullptr) {
            return false;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }

        vtable[index] = originalFunc;
        VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
        return true;
    }

    static bool ResolvePackageManagerObject(
        uintptr_t objectAddress,
        uintptr_t* resolvedObjectAddress,
        void*** resolvedVtable) {
        if (objectAddress == 0 ||
            IsBadReadPtr(reinterpret_cast<const void*>(objectAddress), sizeof(void*))) {
            LOG_WARNING("[PakRuntime] Package manager object unreadable object=0x"
                << std::hex << objectAddress);
            return false;
        }

        void** vtable = reinterpret_cast<void**>(
            TryReadVtable(reinterpret_cast<void*>(objectAddress)));

        if (vtable == nullptr ||
            IsBadReadPtr(vtable, PACKAGE_MANAGER_VTABLE_OFFSET_24 + sizeof(void*))) {
            LOG_WARNING("[PakRuntime] Package manager object unreadable vtable object=0x"
                << std::hex << objectAddress
                << " vtable=0x" << reinterpret_cast<uintptr_t>(vtable));
            return false;
        }

        const uintptr_t slot20 = reinterpret_cast<uintptr_t>(
            vtable[PACKAGE_MANAGER_VTABLE_OFFSET_20 / sizeof(void*)]);
        const uintptr_t slot24 = reinterpret_cast<uintptr_t>(
            vtable[PACKAGE_MANAGER_VTABLE_OFFSET_24 / sizeof(void*)]);
        if (IsBadCodePtr(reinterpret_cast<FARPROC>(slot20)) ||
            IsBadCodePtr(reinterpret_cast<FARPROC>(slot24))) {
            LOG_WARNING("[PakRuntime] Package manager object non-executable slots object=0x"
                << std::hex << objectAddress
                << " vtable=0x" << reinterpret_cast<uintptr_t>(vtable)
                << " off20=0x" << slot20
                << " off24=0x" << slot24);
            return false;
        }

        *resolvedObjectAddress = objectAddress;
        *resolvedVtable = vtable;
        LOG_INFO("[PakRuntime] Package manager object accepted");
        return true;
    }

    static void MaybeLogPackageCall(
        const char* apiName,
        uintptr_t callerAddress,
        void* thisPtr,
        const char* path,
        uint32_t arg1,
        uint32_t arg2,
        uintptr_t result) {
        std::string pathText;
        if (!TryCopyPackagePathArgument(path, &pathText)) {
            return;
        }

        const std::string lowerPath = ToLowerCopy(pathText);
        if (!IsInterestingPackagePath(lowerPath)) {
            return;
        }

        uint32_t hitCount = 0;
        const bool shouldLog = ShouldLogPathHit(lowerPath, &hitCount);
        const uintptr_t resultVtable = TryReadVtable(reinterpret_cast<void*>(result));
        const uintptr_t callerGhidraAddress = callerAddress != 0 && g_baseAddress != 0
            ? callerAddress - g_baseAddress + GHIDRA_BASE_UPLAY
            : 0;
        MarkCacheLoadPackageOpenIfMatching(pathText, result, resultVtable);

        g_lastInterestingPackagePath = pathText;
        g_lastInterestingPackageResult = result;
        g_lastInterestingPackageResultVtable = resultVtable;

        if (!shouldLog) {
            return;
        }

        LOG_INFO("[PakRuntime/" << apiName << "] hit#" << hitCount
            << " this=0x" << std::hex << reinterpret_cast<uintptr_t>(thisPtr)
            << " caller=0x" << callerAddress
            << " ghidra=0x" << callerGhidraAddress
            << " path=" << pathText
            << " arg1=0x" << arg1
            << " arg2=0x" << arg2
            << " result=0x" << result
            << " result_vtable=0x" << resultVtable);

        if (result == 0 ||
            (!EndsWith(lowerPath, ".gfx") && !EndsWith(lowerPath, ".mdl") &&
                lowerPath.find("objectcollection") == std::string::npos)) {
            return;
        }

        TrackResourceObject(result, pathText);
        InstallResourceReadHook(resultVtable);

        uint32_t dumpCount = 0;
        if (!ShouldDumpResourceVtable(resultVtable, &dumpCount)) {
            return;
        }

        LOG_INFO("[PakRuntime/" << apiName << "] resource-vtable dump path=" << pathText
            << " result=0x" << std::hex << result
            << " vtable=0x" << resultVtable);

        for (size_t offset = 0; offset <= 0x40; offset += sizeof(uintptr_t)) {
            const uintptr_t entry = TryReadPointer(resultVtable + offset);
            LOG_INFO("[PakRuntime/" << apiName << "]   vt+0x" << std::hex << offset
                << " = 0x" << entry);
        }

        for (size_t offset = 0; offset <= 0x40; offset += sizeof(uint32_t)) {
            uint32_t value = 0;
            if (TryReadDword(result + offset, &value)) {
                LOG_INFO("[PakRuntime/" << apiName << "]   obj+0x" << std::hex << offset
                    << " = 0x" << value);
            }
        }
    }

    static uintptr_t __fastcall Hook_PackageApi20(
        void* thisPtr, void* edxUnused, const char* path, uint32_t arg1) {
        const uintptr_t callerAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
        const uintptr_t result = g_originalPackageApi20(thisPtr, edxUnused, path, arg1);
        MaybeLogPackageCall("Pkg20", callerAddress, thisPtr, path, arg1, 0, result);
        return result;
    }

    static uintptr_t __fastcall Hook_PackageApi24(
        void* thisPtr, void* edxUnused, const char* path, uint32_t arg1, uint32_t arg2) {
        const uintptr_t callerAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
        const uintptr_t result = g_originalPackageApi24(thisPtr, edxUnused, path, arg1, arg2);
        MaybeLogPackageCall("Pkg24", callerAddress, thisPtr, path, arg1, arg2, result);
        return result;
    }

    static void InstallResourceReadHook(uintptr_t resourceVtable) {
        if (resourceVtable == 0) {
            return;
        }

        if (!g_resourceReadAtHookInstalled) {
            const uintptr_t readAtTarget = TryReadPointer(
                resourceVtable + RESOURCE_STREAM_VTABLE_OFFSET_READ_AT);
            if (readAtTarget == 0 || IsBadCodePtr(reinterpret_cast<FARPROC>(readAtTarget))) {
                LOG_WARNING("[PakRuntime] Resource read-at hook target invalid vtable=0x"
                    << std::hex << resourceVtable
                    << " target=0x" << readAtTarget);
            }
            else {
                MH_STATUS hookStatus = MH_CreateHook(
                    reinterpret_cast<LPVOID>(readAtTarget),
                    reinterpret_cast<LPVOID>(&Hook_ResourceStreamReadAt),
                    reinterpret_cast<LPVOID*>(&g_originalResourceReadAt));
                if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
                    LOG_WARNING("[PakRuntime] Failed to create resource read-at hook: "
                        << MH_StatusToString(hookStatus));
                }
                else {
                    hookStatus = MH_EnableHook(reinterpret_cast<LPVOID>(readAtTarget));
                    if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
                        LOG_WARNING("[PakRuntime] Failed to enable resource read-at hook: "
                            << MH_StatusToString(hookStatus));
                        MH_RemoveHook(reinterpret_cast<LPVOID>(readAtTarget));
                        g_originalResourceReadAt = nullptr;
                    }
                    else {
                        g_resourceReadAtTarget = readAtTarget;
                        g_resourceReadAtHookInstalled = true;
                        LOG_INFO("[PakRuntime] Resource read-at hook installed target=0x"
                            << std::hex << g_resourceReadAtTarget
                            << " vtable=0x" << resourceVtable
                            << " offset=0x" << RESOURCE_STREAM_VTABLE_OFFSET_READ_AT);
                    }
                }
            }
        }

        if (!g_resourceReadCurrentHookInstalled) {
            const uintptr_t readCurrentTarget = TryReadPointer(
                resourceVtable + RESOURCE_STREAM_VTABLE_OFFSET_READ_CURRENT);
            if (readCurrentTarget == 0 || IsBadCodePtr(reinterpret_cast<FARPROC>(readCurrentTarget))) {
                LOG_WARNING("[PakRuntime] Resource read-current hook target invalid vtable=0x"
                    << std::hex << resourceVtable
                    << " target=0x" << readCurrentTarget);
                return;
            }

            MH_STATUS hookStatus = MH_CreateHook(
                reinterpret_cast<LPVOID>(readCurrentTarget),
                reinterpret_cast<LPVOID>(&Hook_ResourceStreamReadCurrent),
                reinterpret_cast<LPVOID*>(&g_originalResourceReadCurrent));
            if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
                LOG_WARNING("[PakRuntime] Failed to create resource read-current hook: "
                    << MH_StatusToString(hookStatus));
                return;
            }

            hookStatus = MH_EnableHook(reinterpret_cast<LPVOID>(readCurrentTarget));
            if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
                LOG_WARNING("[PakRuntime] Failed to enable resource read-current hook: "
                    << MH_StatusToString(hookStatus));
                MH_RemoveHook(reinterpret_cast<LPVOID>(readCurrentTarget));
                g_originalResourceReadCurrent = nullptr;
                return;
            }

            g_resourceReadCurrentTarget = readCurrentTarget;
            g_resourceReadCurrentHookInstalled = true;
            LOG_INFO("[PakRuntime] Resource read-current hook installed target=0x"
                << std::hex << g_resourceReadCurrentTarget
                << " vtable=0x" << resourceVtable
                << " offset=0x" << RESOURCE_STREAM_VTABLE_OFFSET_READ_CURRENT);
        }
    }

    static uint32_t __fastcall Hook_ResourceStreamReadCurrent(
        void* thisPtr, void* edxUnused, void* outBuffer, size_t readSize) {
        uint32_t offsetBefore = 0;
        uint32_t offsetAfter = 0;
        uint32_t backingObject = 0;
        TryReadDword(reinterpret_cast<uintptr_t>(thisPtr) + 0x8, &offsetBefore);
        TryReadDword(reinterpret_cast<uintptr_t>(thisPtr) + 0x10, &backingObject);

        const uint32_t bytesRead = g_originalResourceReadCurrent(
            thisPtr, edxUnused, outBuffer, readSize);
        TryReadDword(reinterpret_cast<uintptr_t>(thisPtr) + 0x8, &offsetAfter);

        std::string pathText;
        uint32_t readCount = 0;
        if (!GetTrackedResourceReadInfo(
                reinterpret_cast<uintptr_t>(thisPtr), &pathText, &readCount)) {
            return bytesRead;
        }

        const uint32_t patchedRefs = PatchCodexGfxTransformBuffer(
            pathText,
            offsetBefore,
            outBuffer,
            bytesRead);

        if (readCount > 4) {
            return bytesRead;
        }

        const size_t sampleSize = std::min<size_t>(readSize, bytesRead);
        const std::string hexSample = FormatHexSample(outBuffer, sampleSize);
        const std::string asciiSample = FormatAsciiSample(outBuffer, sampleSize);
        LOG_INFO("[PakRuntime/StreamRead] hit#" << std::dec << readCount
            << " this=0x" << std::hex << reinterpret_cast<uintptr_t>(thisPtr)
            << " backing=0x" << backingObject
            << " path=" << pathText
            << " offset=0x" << offsetBefore
            << " after=0x" << offsetAfter
            << " size=0x" << readSize
            << " bytes=0x" << bytesRead
            << " patched_refs=0x" << patchedRefs
            << " sample_hex=" << hexSample
            << " sample_ascii=" << asciiSample);
        return bytesRead;
    }

    static uint32_t __fastcall Hook_ResourceStreamReadAt(
        void* thisPtr, void* edxUnused, void* outBuffer, uint32_t offset, size_t readSize) {
        uint32_t backingObject = 0;
        TryReadDword(reinterpret_cast<uintptr_t>(thisPtr) + 0x10, &backingObject);

        const uint32_t bytesRead = g_originalResourceReadAt(
            thisPtr, edxUnused, outBuffer, offset, readSize);

        std::string pathText;
        uint32_t readCount = 0;
        if (!GetTrackedResourceReadInfo(
                reinterpret_cast<uintptr_t>(thisPtr), &pathText, &readCount)) {
            return bytesRead;
        }

        const uint32_t patchedRefs = PatchCodexGfxTransformBuffer(
            pathText,
            offset,
            outBuffer,
            bytesRead);

        if (readCount > 4) {
            return bytesRead;
        }

        const size_t sampleSize = std::min<size_t>(readSize, bytesRead);
        const std::string hexSample = FormatHexSample(outBuffer, sampleSize);
        const std::string asciiSample = FormatAsciiSample(outBuffer, sampleSize);
        LOG_INFO("[PakRuntime/StreamReadAt] hit#" << std::dec << readCount
            << " this=0x" << std::hex << reinterpret_cast<uintptr_t>(thisPtr)
            << " backing=0x" << backingObject
            << " path=" << pathText
            << " offset=0x" << offset
            << " size=0x" << readSize
            << " bytes=0x" << bytesRead
            << " patched_refs=0x" << patchedRefs
            << " sample_hex=" << hexSample
            << " sample_ascii=" << asciiSample);
        return bytesRead;
    }

    static void LogObjectLoaderOwnerState(
        const char* tag,
        uintptr_t job,
        uintptr_t owner,
        const std::string& pathA,
        const std::string& pathB,
        uint8_t flag) {
        uint32_t ownerKey = 0;
        uint32_t ownerPackageId = 0;
        std::string ownerName;
        const uintptr_t scene = owner != 0 ? TryReadPointer(owner + 0x68) : 0;
        if (owner != 0) {
            TryReadDword(owner + 0x1c, &ownerKey);
            TryReadDword(owner + 0x18, &ownerPackageId);
            TryCopyGameString(reinterpret_cast<void*>(owner + 0x8), &ownerName);
        }

        const uintptr_t vtable = TryReadPointer(job);
        const uintptr_t vtableStart = vtable != 0 ? TryReadPointer(vtable + 4) : 0;
        const uintptr_t vtableRun = vtable != 0 ? TryReadPointer(vtable + 8) : 0;

        LOG_INFO("[PakRuntime/ObjectLoader] " << tag
            << " job=0x" << std::hex << job
            << " vtable=0x" << vtable
            << " start=0x" << vtableStart
            << " run=0x" << vtableRun
            << " owner=0x" << owner
            << " owner_key=0x" << ownerKey
            << " package_id=0x" << ownerPackageId
            << " scene=0x" << scene
            << " flag=0x" << static_cast<uint32_t>(flag)
            << " owner_name=" << ownerName
            << " path_a=" << pathA
            << " path_b=" << pathB);
    }

    static uintptr_t __fastcall Hook_ObjectLoaderJobConstructor(
        void* thisPtr,
        void* edxUnused,
        uintptr_t owner,
        void* pathA,
        void* pathB,
        uint8_t flag) {
        std::string pathAText;
        std::string pathBText;
        TryCopyGameString(pathA, &pathAText);
        TryCopyGameString(pathB, &pathBText);

        const uintptr_t result = g_originalObjectLoaderJobConstructor(
            thisPtr,
            edxUnused,
            owner,
            pathA,
            pathB,
            flag);

        const std::string lowerA = ToLowerCopy(pathAText);
        const std::string lowerB = ToLowerCopy(pathBText);
        if (IsInterestingObjectLoaderPath(lowerA, lowerB)) {
            LogObjectLoaderOwnerState(
                "constructed",
                result,
                owner,
                pathAText,
                pathBText,
                flag);
        }
        return result;
    }

    static void __fastcall Hook_InsertHashTable(
        void* thisPtr,
        void* edxUnused,
        int* keyPtr,
        uintptr_t* valuePtr) {
        const uintptr_t pendingTable =
            RuntimeAddressFromGhidra(PENDING_OBJECT_LOADER_TABLE_ADDR_UPLAY);
        const bool isPendingObjectLoaderTable =
            reinterpret_cast<uintptr_t>(thisPtr) == pendingTable;

        uintptr_t job = 0;
        uintptr_t owner = 0;
        uint32_t key = 0;
        std::string pathAText;
        std::string pathBText;
        uint8_t flag = 0;
        bool shouldLog = false;

        if (isPendingObjectLoaderTable) {
            TryReadDword(reinterpret_cast<uintptr_t>(keyPtr), &key);
            job = TryReadPointer(reinterpret_cast<uintptr_t>(valuePtr));
            owner = TryReadPointer(job + 0x24);
            TryCopyGameString(reinterpret_cast<void*>(job + 0x28), &pathAText);
            TryCopyGameString(reinterpret_cast<void*>(job + 0x38), &pathBText);
            flag = static_cast<uint8_t>(TryReadPointer(job + 0x48) & 0xff);

            const std::string lowerA = ToLowerCopy(pathAText);
            const std::string lowerB = ToLowerCopy(pathBText);
            shouldLog = IsInterestingObjectLoaderPath(lowerA, lowerB);
            if (shouldLog) {
                LOG_INFO("[PakRuntime/ObjectLoader] queue-before table=0x" << std::hex
                    << reinterpret_cast<uintptr_t>(thisPtr)
                    << " key=0x" << key);
                LogObjectLoaderOwnerState(
                    "queue-before",
                    job,
                    owner,
                    pathAText,
                    pathBText,
                    flag);
            }
        }

        g_originalInsertHashTable(thisPtr, edxUnused, keyPtr, valuePtr);

        if (isPendingObjectLoaderTable && shouldLog) {
            uint32_t tableCount = 0;
            uint32_t tableCapacity = 0;
            uintptr_t tableEntries = 0;
            TryReadDword(pendingTable, &tableCount);
            TryReadDword(pendingTable + 0xc, &tableCapacity);
            tableEntries = TryReadPointer(pendingTable + 0x10);
            LOG_INFO("[PakRuntime/ObjectLoader] queue-after table=0x" << std::hex
                << pendingTable
                << " key=0x" << key
                << " count=0x" << tableCount
                << " capacity=0x" << tableCapacity
                << " entries=0x" << tableEntries);

            if (ToLowerCopy(pathAText).find("codex") != std::string::npos ||
                ToLowerCopy(pathBText).find("codex") != std::string::npos) {
                std::lock_guard<std::mutex> lock(g_packagePathMutex);
                g_lastCodexObjectLoaderOwner = owner;
                g_lastCodexObjectLoaderKey = key;
                g_lastCodexObjectLoaderJob = job;
                g_lastCodexObjectLoaderPathA = pathAText;
                g_lastCodexObjectLoaderPathB = pathBText;
            }
        }
    }

    static uint32_t __fastcall Hook_LoadWorldMeshScene(int* sceneInput) {
        std::string pathText;
        uintptr_t owner = 0;
        uintptr_t sceneBefore = 0;
        uint32_t ownerKey = 0;
        uint32_t pendingCount = 0;
        uint32_t pendingCapacity = 0;
        if (sceneInput != nullptr) {
            TryCopyGameString(sceneInput + 10, &pathText);
            owner = TryReadPointer(reinterpret_cast<uintptr_t>(sceneInput) + 0x24);
            sceneBefore = owner != 0 ? TryReadPointer(owner + 0x68) : 0;
            if (owner != 0) {
                TryReadDword(owner + 0x1c, &ownerKey);
            }
            const uintptr_t pendingTable =
                RuntimeAddressFromGhidra(PENDING_OBJECT_LOADER_TABLE_ADDR_UPLAY);
            TryReadDword(pendingTable, &pendingCount);
            TryReadDword(pendingTable + 0xc, &pendingCapacity);
        }

        const std::string lowerPath = ToLowerCopy(pathText);
        const bool interesting =
            !pathText.empty() && lowerPath.find("codex") != std::string::npos;
        const bool replayCandidate = interesting && EndsWith(lowerPath, ".gfx");
        if (interesting) {
            LOG_INFO("[PakRuntime/WorldMeshScene] before path=" << pathText
                << " input=0x" << std::hex << reinterpret_cast<uintptr_t>(sceneInput)
                << " owner=0x" << owner
                << " owner_key=0x" << ownerKey
                << " scene_before=0x" << sceneBefore
                << " pending_count=0x" << pendingCount
                << " pending_capacity=0x" << pendingCapacity);
        }

        const uint32_t result = g_originalLoadWorldMeshScene(sceneInput);

        if (replayCandidate) {
            const uintptr_t sceneAfter = owner != 0 ? TryReadPointer(owner + 0x68) : 0;
            {
                std::lock_guard<std::mutex> lock(g_packagePathMutex);
                g_lastCodexWorldMeshSceneInput = reinterpret_cast<uintptr_t>(sceneInput);
                g_lastCodexWorldMeshSceneOwner = owner;
                g_lastCodexWorldMeshSceneBefore = sceneBefore;
                g_lastCodexWorldMeshSceneAfter = sceneAfter;
                g_lastCodexWorldMeshScenePath = pathText;

                if (IsCodexCustomGfxPath(lowerPath)) {
                    CapturedCodexGfxTarget& target = g_capturedCodexGfxTargets[pathText];
                    target.path = pathText;
                    target.sceneInput = reinterpret_cast<uintptr_t>(sceneInput);
                    target.sceneOwner = owner;
                    target.sceneResource = sceneAfter;
                    target.ownerKey = ownerKey;
                    target.sceneCaptured = true;
                }
            }
            LOG_INFO("[PakRuntime/WorldMeshScene] after path=" << pathText
                << " input=0x" << std::hex << reinterpret_cast<uintptr_t>(sceneInput)
                << " owner=0x" << owner
                << " owner_key=0x" << ownerKey
                << " scene_before=0x" << sceneBefore
                << " scene_after=0x" << sceneAfter
                << " result=0x" << result);

            if (IsCodexCustomGfxPath(lowerPath)) {
                LogSceneTreeSnapshot(
                    "worldmesh-load",
                    pathText,
                    owner,
                    sceneAfter);
                BeginPhysicsProbeScan(
                    "worldmesh-load",
                    pathText,
                    owner,
                    sceneAfter,
                    160);
            }
        }
        else if (interesting) {
            const uintptr_t sceneAfter = owner != 0 ? TryReadPointer(owner + 0x68) : 0;
            LOG_INFO("[PakRuntime/WorldMeshScene] after path=" << pathText
                << " input=0x" << std::hex << reinterpret_cast<uintptr_t>(sceneInput)
                << " owner=0x" << owner
                << " owner_key=0x" << ownerKey
                << " scene_before=0x" << sceneBefore
                << " scene_after=0x" << sceneAfter
                << " result=0x" << result
                << " replay_candidate=no");
        }

        return result;
    }

    static void __fastcall Hook_LoadObjectCollectionAndInitializeGameplay(int param1) {
        const LONG replaying = InterlockedCompareExchange(&g_reloadInProgress, 0, 0);
        if (!replaying) {
            g_lastGameplayLoaderParam1 = param1;
            InterlockedExchange(&g_gameplayLoaderContextCaptured, 1);
            const LONG count = InterlockedIncrement(&g_gameplayLoaderNaturalCallCount);
            LOG_INFO("[PakRuntime] Natural LoadObjectCollectionAndInitializeGameplay call #" << count
                << " param1=0x" << std::hex << param1);
        }

        g_originalLoadObjectCollectionAndInitializeGameplay(param1);
    }

    static void __fastcall Hook_LoadObjectCollectionXML(void* ecxValue, void* edxValue) {
        uintptr_t ediValue = 0;
        __asm {
            mov ediValue, edi
        }

        const LONG replaying = InterlockedCompareExchange(&g_reloadInProgress, 0, 0);
        if (!replaying) {
            g_lastObjectCollectionECX = reinterpret_cast<uintptr_t>(ecxValue);
            g_lastObjectCollectionEDX = reinterpret_cast<uintptr_t>(edxValue);
            g_lastObjectCollectionEDI = ediValue;
            InterlockedExchange(&g_objectCollectionContextCaptured, 1);
            const LONG count = InterlockedIncrement(&g_objectCollectionNaturalCallCount);
            LOG_INFO("[PakRuntime] Natural LoadObjectCollectionXML call #" << count
                << " ecx=0x" << std::hex << g_lastObjectCollectionECX
                << " edx=0x" << g_lastObjectCollectionEDX
                << " edi=0x" << g_lastObjectCollectionEDI);
        }

        CallOriginalLoadObjectCollectionXML_WithRegs(
            reinterpret_cast<uintptr_t>(ecxValue),
            reinterpret_cast<uintptr_t>(edxValue),
            ediValue);
    }

    static void __fastcall Hook_LoadAndCacheTextureFromFile(
        void* thisPtr, void* edxUnused, const char* path, int* output) {
        std::string pathText;
        const bool havePath = TryCopyPackagePathArgument(path, &pathText);
        const std::string lowerPath = havePath ? ToLowerCopy(pathText) : std::string();
        const bool interesting = havePath && IsInterestingPackagePath(lowerPath);

        if (interesting) {
            BeginCacheLoadTracking(pathText);
        }

        g_originalLoadAndCacheTextureFromFile(thisPtr, edxUnused, path, output);

        if (!interesting) {
            return;
        }

        bool openedPackage = false;
        bool packageSucceeded = false;
        uintptr_t packageResult = 0;
        uintptr_t packageResultVtable = 0;
        EndCacheLoadTracking(
            &openedPackage, &packageSucceeded, &packageResult, &packageResultVtable);
        uint32_t output0 = 0;
        if (output != nullptr) {
            TryReadDword(reinterpret_cast<uintptr_t>(output), &output0);
        }

        const uint32_t pathHash = G4HashPathPreserveCase(pathText);
        uintptr_t cacheEntry = 0;
        uint32_t cacheValue = 0;
        uint32_t cacheState = 0;
        uint32_t cacheCapacity = 0;
        uint32_t cacheIndex = 0;
        const bool foundCacheEntry = TryFindResourceCacheEntry(
            reinterpret_cast<uintptr_t>(thisPtr),
            pathHash,
            &cacheEntry,
            &cacheValue,
            &cacheState,
            &cacheCapacity,
            &cacheIndex);

        const char* status = "hit/no-open";
        if (openedPackage && packageSucceeded) {
            status = "miss/opened-ok";
        }
        else if (openedPackage) {
            status = "miss/open-failed";
        }

        LOG_INFO("[PakRuntime/CacheLoad] " << status
            << " this=0x" << std::hex << reinterpret_cast<uintptr_t>(thisPtr)
            << " path=" << pathText
            << " hash=0x" << pathHash
            << " package_result=0x" << packageResult
            << " package_vtable=0x" << packageResultVtable
            << " output0=0x" << output0
            << " cache_found=" << (foundCacheEntry ? "yes" : "no")
            << " cache_entry=0x" << cacheEntry
            << " cache_index=0x" << cacheIndex
            << " cache_capacity=0x" << cacheCapacity
            << " cache_value=0x" << cacheValue
            << " cache_state=0x" << cacheState);

        if (foundCacheEntry) {
            const std::string lowerEvictPath = ToLowerCopy(pathText);
            const uint32_t evictionPriority = GetEvictionPathPriority(lowerEvictPath);
            if (evictionPriority > 0) {
                const bool selectedForEviction = CaptureCacheEvictionTarget(
                    "ObjectCollectionMembership",
                    pathText,
                    reinterpret_cast<uintptr_t>(thisPtr),
                    cacheEntry,
                    pathHash,
                    cacheIndex,
                    cacheCapacity,
                    reinterpret_cast<uintptr_t>(thisPtr) + 0x258,
                    evictionPriority,
                    0);
                LOG_INFO("[PakRuntime/CacheLoad] eviction target path=" << pathText
                    << " entry=0x" << std::hex << cacheEntry
                    << " priority=0x" << evictionPriority
                    << " selected=" << (selectedForEviction ? "yes" : "no"));
            }
        }
    }

    static uintptr_t __fastcall Hook_LoadAndCacheGfxResource(
        void* thisPtr, void* edxUnused, const char* path, int param2) {
        std::string pathText;
        const bool havePath = TryCopyPackagePathArgument(path, &pathText);
        const std::string lowerPath = havePath ? ToLowerCopy(pathText) : std::string();
        const bool interesting = havePath && IsPreferredEvictionPath(lowerPath);

        if (interesting) {
            BeginCacheLoadTracking(pathText);
        }

        const uintptr_t parsedResource = g_originalLoadAndCacheGfxResource(
            thisPtr, edxUnused, path, param2);

        if (!interesting) {
            return parsedResource;
        }

        bool openedPackage = false;
        bool packageSucceeded = false;
        uintptr_t packageResult = 0;
        uintptr_t packageResultVtable = 0;
        EndCacheLoadTracking(
            &openedPackage, &packageSucceeded, &packageResult, &packageResultVtable);

        const uint32_t pathHash = G4HashPathPreserveCase(pathText);
        uintptr_t cacheEntry = 0;
        uint32_t cacheValue = 0;
        uint32_t cacheState = 0;
        uint32_t cacheCapacity = 0;
        uint32_t cacheIndex = 0;
        const bool foundCacheEntry = TryFindGfxCacheEntry(
            reinterpret_cast<uintptr_t>(thisPtr),
            pathHash,
            &cacheEntry,
            &cacheValue,
            &cacheState,
            &cacheCapacity,
            &cacheIndex);

        const char* status = "hit/no-open";
        if (openedPackage && packageSucceeded) {
            status = "miss/opened-ok";
        }
        else if (openedPackage) {
            status = "miss/open-failed";
        }

        LOG_INFO("[PakRuntime/GfxCacheLoad] " << status
            << " this=0x" << std::hex << reinterpret_cast<uintptr_t>(thisPtr)
            << " path=" << pathText
            << " hash=0x" << pathHash
            << " param2=0x" << param2
            << " package_result=0x" << packageResult
            << " package_vtable=0x" << packageResultVtable
            << " parsed_resource=0x" << parsedResource
            << " cache_found=" << (foundCacheEntry ? "yes" : "no")
            << " cache_entry=0x" << cacheEntry
            << " cache_index=0x" << cacheIndex
            << " cache_capacity=0x" << cacheCapacity
            << " cache_value=0x" << cacheValue
            << " cache_state=0x" << cacheState
            << " cache_entry_kind="
            << (foundCacheEntry && cacheValue == 0 ? "negative-open-failure" :
                (foundCacheEntry ? "parsed-resource" : "none")));

        if (foundCacheEntry) {
            uint32_t evictionPriority = GetEvictionPathPriority(lowerPath);
            if (strcmp(status, "miss/open-failed") == 0 || cacheValue == 0) {
                evictionPriority += 25;
            }

            const bool selectedForEviction = CaptureCacheEvictionTarget(
                "GfxResource",
                pathText,
                reinterpret_cast<uintptr_t>(thisPtr),
                cacheEntry,
                pathHash,
                cacheIndex,
                cacheCapacity,
                reinterpret_cast<uintptr_t>(thisPtr) + 0x14,
                evictionPriority,
                param2);
            LOG_INFO("[PakRuntime/GfxCacheLoad] eviction target path=" << pathText
                << " entry=0x" << std::hex << cacheEntry
                << " parsed_resource=0x" << parsedResource
                << " priority=0x" << evictionPriority
                << " selected=" << (selectedForEviction ? "yes" : "no"));
        }

        return parsedResource;
    }

    static bool InstallPackageApiHooks(bool quietRetry = false) {
        if (g_packageApiHooksInstalled) {
            return true;
        }

        if (BaseAddress::IsSteamVersion()) {
            if (!quietRetry) {
                LOG_WARNING("[PakRuntime] Package API probe disabled for Steam until mapped");
            }
            return false;
        }

        void** vtable = nullptr;
        const uintptr_t packageManagerGlobal =
            g_baseAddress + (PACKAGE_MANAGER_GLOBAL_ADDR_UPLAY - GHIDRA_BASE_UPLAY);
        const uintptr_t packageManagerObject = TryReadPointer(packageManagerGlobal);

        if (!ResolvePackageManagerObject(packageManagerObject, &g_packageManagerObjectAddress, &vtable)) {
            if (!quietRetry) {
                LOG_WARNING("[PakRuntime] Package API probe could not resolve package manager object"
                    << " global=0x" << std::hex << packageManagerGlobal
                    << " value=0x" << packageManagerObject);
            }
            g_packageManagerObjectAddress = 0;
            return false;
        }

        g_packageApi20Target = reinterpret_cast<uintptr_t>(
            vtable[PACKAGE_MANAGER_VTABLE_OFFSET_20 / sizeof(void*)]);
        g_packageApi24Target = reinterpret_cast<uintptr_t>(
            vtable[PACKAGE_MANAGER_VTABLE_OFFSET_24 / sizeof(void*)]);

        MH_STATUS hookStatus = MH_CreateHook(
            reinterpret_cast<LPVOID>(g_packageApi20Target),
            reinterpret_cast<LPVOID>(&Hook_PackageApi20),
            reinterpret_cast<LPVOID*>(&g_originalPackageApi20));
        if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
            LOG_WARNING("[PakRuntime] Failed to create package API slot 20 hook: "
                << MH_StatusToString(hookStatus));
            g_packageManagerObjectAddress = 0;
            g_packageApi20Target = 0;
            g_packageApi24Target = 0;
            return false;
        }

        hookStatus = MH_EnableHook(reinterpret_cast<LPVOID>(g_packageApi20Target));
        if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
            LOG_WARNING("[PakRuntime] Failed to enable package API slot 20 hook: "
                << MH_StatusToString(hookStatus));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_packageApi20Target));
            g_packageManagerObjectAddress = 0;
            g_packageApi20Target = 0;
            g_packageApi24Target = 0;
            g_originalPackageApi20 = nullptr;
            return false;
        }

        hookStatus = MH_CreateHook(
            reinterpret_cast<LPVOID>(g_packageApi24Target),
            reinterpret_cast<LPVOID>(&Hook_PackageApi24),
            reinterpret_cast<LPVOID*>(&g_originalPackageApi24));
        if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
            LOG_WARNING("[PakRuntime] Failed to create package API slot 24 hook: "
                << MH_StatusToString(hookStatus));
            MH_DisableHook(reinterpret_cast<LPVOID>(g_packageApi20Target));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_packageApi20Target));
            g_packageManagerObjectAddress = 0;
            g_packageApi20Target = 0;
            g_packageApi24Target = 0;
            g_originalPackageApi20 = nullptr;
            g_originalPackageApi24 = nullptr;
            return false;
        }

        hookStatus = MH_EnableHook(reinterpret_cast<LPVOID>(g_packageApi24Target));
        if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
            LOG_WARNING("[PakRuntime] Failed to enable package API slot 24 hook: "
                << MH_StatusToString(hookStatus));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_packageApi24Target));
            MH_DisableHook(reinterpret_cast<LPVOID>(g_packageApi20Target));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_packageApi20Target));
            g_packageManagerObjectAddress = 0;
            g_packageApi20Target = 0;
            g_packageApi24Target = 0;
            g_originalPackageApi20 = nullptr;
            g_originalPackageApi24 = nullptr;
            return false;
        }

        g_packageManagerVtable = vtable;
        g_packageApiHooksInstalled = true;

        LOG_INFO("[PakRuntime] Package API MinHook probe installed on object 0x" << std::hex
            << g_packageManagerObjectAddress
            << " vtable=0x" << reinterpret_cast<uintptr_t>(g_packageManagerVtable)
            << " off20=0x" << g_packageApi20Target
            << " off24=0x" << g_packageApi24Target);
        return true;
    }

    static void RetryPackageApiHooksOnFrame() {
        if (g_packageApiHooksInstalled) {
            return;
        }

        if (g_packageApiRetryFrameCountdown > 0) {
            --g_packageApiRetryFrameCountdown;
            return;
        }

        g_packageApiRetryFrameCountdown = 180;

        if (InterlockedCompareExchange(&g_packageApiRetryInProgress, 1, 0) != 0) {
            return;
        }

        ++g_packageApiRetryAttempts;
        if (g_packageApiRetryAttempts == 1 || (g_packageApiRetryAttempts % 10) == 0) {
            LOG_INFO("[PakRuntime] Retrying package API probe on game frame, attempt "
                << std::dec << g_packageApiRetryAttempts);
        }

        const bool installed = InstallPackageApiHooks(true);
        InterlockedExchange(&g_packageApiRetryInProgress, 0);

        if (installed) {
            LOG_INFO("[PakRuntime] Package API probe resolved after delayed retry attempt "
                << std::dec << g_packageApiRetryAttempts);
        }
    }

    static bool EvictAndReloadCapturedCodexGfxTarget(
        CapturedCodexGfxTarget target,
        float scaleValue) {
        if (!target.cacheCaptured ||
            target.path.empty() ||
            target.cacheManager == 0 ||
            target.cacheEntry == 0 ||
            target.cacheCountAddress == 0) {
            return false;
        }

        uint32_t entryHash = 0;
        uint32_t entryState = 0;
        uint32_t tableCount = 0;
        TryReadDword(target.cacheEntry, &entryHash);
        TryReadDword(target.cacheEntry + 8, &entryState);
        TryReadDword(target.cacheCountAddress, &tableCount);

        if (entryHash != target.cacheHash || (entryState & 0xff) != 2) {
            LOG_WARNING("[PakRuntime] Captured codex GFX cache entry stale path="
                << target.path
                << " entry=0x" << std::hex << target.cacheEntry
                << " expected_hash=0x" << target.cacheHash
                << " actual_hash=0x" << entryHash
                << " state=0x" << (entryState & 0xff));

            uintptr_t refoundEntry = 0;
            uint32_t refoundValue = 0;
            uint32_t refoundState = 0;
            uint32_t refoundCapacity = 0;
            uint32_t refoundIndex = 0;
            uintptr_t refoundCountAddress = 0;
            if (!TryRefindCapturedCacheEntry(
                    "GfxResource",
                    target.cacheManager,
                    target.cacheHash,
                    &refoundEntry,
                    &refoundValue,
                    &refoundState,
                    &refoundCapacity,
                    &refoundIndex,
                    &refoundCountAddress)) {
                LOG_WARNING("[PakRuntime] Could not re-find captured codex GFX cache entry path="
                    << target.path
                    << " manager=0x" << std::hex << target.cacheManager
                    << " hash=0x" << target.cacheHash);
                return false;
            }

            target.cacheEntry = refoundEntry;
            target.cacheIndex = refoundIndex;
            target.cacheCapacity = refoundCapacity;
            target.cacheCountAddress = refoundCountAddress;
            entryState = refoundState;
            TryReadDword(target.cacheCountAddress, &tableCount);

            LOG_INFO("[PakRuntime] Re-found captured codex GFX cache entry path="
                << target.path
                << " entry=0x" << std::hex << target.cacheEntry
                << " value=0x" << refoundValue
                << " state=0x" << refoundState
                << " index=0x" << target.cacheIndex
                << " capacity=0x" << target.cacheCapacity);

            if ((entryState & 0xff) != 2) {
                LOG_WARNING("[PakRuntime] Re-found captured codex GFX cache entry is not occupied path="
                    << target.path
                    << " entry=0x" << std::hex << target.cacheEntry
                    << " state=0x" << (entryState & 0xff));
                return false;
            }
        }

        if (!TryWriteByte(target.cacheEntry + 8, 1)) {
            LOG_WARNING("[PakRuntime] Failed to tombstone captured codex GFX cache entry path="
                << target.path
                << " entry=0x" << std::hex << target.cacheEntry);
            return false;
        }

        if (tableCount > 0) {
            TryWriteDword(target.cacheCountAddress, tableCount - 1);
        }

        const uint32_t newCount = tableCount > 0 ? tableCount - 1 : tableCount;
        LOG_INFO("[PakRuntime] Evicted captured codex GFX path=" << target.path
            << " manager=0x" << std::hex << target.cacheManager
            << " entry=0x" << target.cacheEntry
            << " hash=0x" << target.cacheHash
            << " index=0x" << target.cacheIndex
            << " capacity=0x" << target.cacheCapacity
            << " count_addr=0x" << target.cacheCountAddress
            << " old_count=0x" << tableCount
            << " new_count=0x" << newCount);

        uintptr_t parsedResource = 0;
        DWORD exceptionCode = 0;
        {
            std::lock_guard<std::mutex> lock(g_packagePathMutex);
            g_forcedGfxPatchThisReload = true;
            g_forcedGfxReloadPath = target.path;
        }

        InterlockedIncrement(&g_forcedGfxReloadDepth);
        const bool reloadSuccess = ForceReloadGfxResource(
            target.cacheManager,
            target.path,
            target.cacheParam2,
            &parsedResource,
            &exceptionCode);
        InterlockedDecrement(&g_forcedGfxReloadDepth);

        {
            std::lock_guard<std::mutex> lock(g_packagePathMutex);
            g_forcedGfxPatchThisReload = false;
            g_forcedGfxReloadPath.clear();
        }

        if (!reloadSuccess) {
            LOG_WARNING("[PakRuntime] Forced captured codex GFX reload failed path="
                << target.path
                << " manager=0x" << std::hex << target.cacheManager
                << " exception=0x" << exceptionCode);
            return false;
        }

        uintptr_t reloadedEntry = 0;
        uint32_t reloadedValue = 0;
        uint32_t reloadedState = 0;
        uint32_t reloadedCapacity = 0;
        uint32_t reloadedIndex = 0;
        TryFindGfxCacheEntry(
            target.cacheManager,
            target.cacheHash,
            &reloadedEntry,
            &reloadedValue,
            &reloadedState,
            &reloadedCapacity,
            &reloadedIndex);

        const uintptr_t sceneBefore =
            target.sceneOwner != 0 ? TryReadPointer(target.sceneOwner + 0x68) : 0;
        InterlockedExchange(&g_continueLoaderReplayAfterCacheEvict, 1);

        LOG_INFO("[PakRuntime] Forced captured codex GFX reload path=" << target.path
            << " manager=0x" << std::hex << target.cacheManager
            << " param2=0x" << target.cacheParam2
            << " parsed_resource=0x" << parsedResource
            << " runtime_patch=yes"
            << " runtime_scale=" << std::dec << scaleValue
            << " cache_entry=0x" << std::hex << reloadedEntry
            << " cache_value=0x" << reloadedValue
            << " cache_state=0x" << reloadedState
            << " cache_index=0x" << reloadedIndex
            << " cache_capacity=0x" << reloadedCapacity
            << " owner=0x" << target.sceneOwner
            << " owner_key=0x" << target.ownerKey
            << " scene_current=0x" << sceneBefore
            << " publish_reloaded_gfx=no"
            << " replay_loader_after_cache_evict=yes");

        {
            std::lock_guard<std::mutex> lock(g_packagePathMutex);
            CapturedCodexGfxTarget& updated = g_capturedCodexGfxTargets[target.path];
            updated.path = target.path;
            updated.cacheManager = target.cacheManager;
            updated.cacheEntry = reloadedEntry != 0 ? reloadedEntry : target.cacheEntry;
            updated.cacheHash = target.cacheHash;
            updated.cacheIndex = reloadedIndex;
            updated.cacheCapacity = reloadedCapacity;
            updated.cacheCountAddress = target.cacheCountAddress;
            updated.cacheParam2 = target.cacheParam2;
            updated.sceneInput = target.sceneInput;
            updated.sceneOwner = target.sceneOwner;
            updated.sceneResource = target.sceneResource;
            updated.ownerKey = target.ownerKey;
            updated.cacheCaptured = true;
            updated.sceneCaptured = target.sceneCaptured;
        }

        return true;
    }

    static bool EvictCapturedCodexGfxTargets() {
        std::vector<CapturedCodexGfxTarget> targets;
        float scaleValue = 1.0f;
        {
            std::lock_guard<std::mutex> lock(g_packagePathMutex);
            for (const auto& item : g_capturedCodexGfxTargets) {
                if (item.second.cacheCaptured && item.second.sceneCaptured) {
                    targets.push_back(item.second);
                }
            }

            if (targets.empty()) {
                return false;
            }

            if (g_pendingExplicitGfxScaleReload) {
                g_pendingExplicitGfxScaleReload = false;
            }
            else {
                g_forcedGfxScaleLarge = !g_forcedGfxScaleLarge;
                g_forcedGfxScaleValue = g_forcedGfxScaleLarge ? 5.0f : 1.0f;
            }
            scaleValue = g_forcedGfxScaleValue;
        }

        LOG_INFO("[PakRuntime] Reloading captured codex GFX target group count=0x"
            << std::hex << targets.size()
            << " runtime_scale=" << std::dec << scaleValue);

        uint32_t successCount = 0;
        for (const CapturedCodexGfxTarget& target : targets) {
            if (EvictAndReloadCapturedCodexGfxTarget(target, scaleValue)) {
                ++successCount;
            }
        }

        LOG_INFO("[PakRuntime] Captured codex GFX target group complete success=0x"
            << std::hex << successCount
            << " attempted=0x" << targets.size());
        return successCount > 0;
    }

    static bool EvictLastCacheEntry() {
        if (EvictCapturedCodexGfxTargets()) {
            return true;
        }

        std::string pathText;
        uintptr_t cacheManager = 0;
        uintptr_t cacheEntry = 0;
        uint32_t cacheHash = 0;
        uint32_t cacheIndex = 0;
        uint32_t cacheCapacity = 0;
        uintptr_t cacheCountAddress = 0;
        std::string cacheKind;
        int cacheParam2 = 0;
        {
            std::lock_guard<std::mutex> lock(g_packagePathMutex);
            cacheKind = g_lastCacheKind;
            pathText = g_lastCachePath;
            cacheManager = g_lastCacheManager;
            cacheEntry = g_lastCacheEntry;
            cacheHash = g_lastCacheHash;
            cacheIndex = g_lastCacheIndex;
            cacheCapacity = g_lastCacheCapacity;
            cacheCountAddress = g_lastCacheCountAddress;
            cacheParam2 = g_lastCacheParam2;
        }

        if (pathText.empty() || cacheEntry == 0 || cacheManager == 0 || cacheCountAddress == 0) {
            LOG_WARNING("[PakRuntime] No cache entry captured yet; cannot evict");
            return false;
        }

        uint32_t entryHash = 0;
        uint32_t entryState = 0;
        uint32_t tableCount = 0;
        TryReadDword(cacheEntry, &entryHash);
        TryReadDword(cacheEntry + 8, &entryState);
        TryReadDword(cacheCountAddress, &tableCount);

        if (entryHash != cacheHash || (entryState & 0xff) != 2) {
            LOG_WARNING("[PakRuntime] Cache entry stale before eviction path=" << pathText
                << " entry=0x" << std::hex << cacheEntry
                << " expected_hash=0x" << cacheHash
                << " actual_hash=0x" << entryHash
                << " state=0x" << (entryState & 0xff));

            uintptr_t refoundEntry = 0;
            uint32_t refoundValue = 0;
            uint32_t refoundState = 0;
            uint32_t refoundCapacity = 0;
            uint32_t refoundIndex = 0;
            uintptr_t refoundCountAddress = 0;
            if (!TryRefindCapturedCacheEntry(
                    cacheKind,
                    cacheManager,
                    cacheHash,
                    &refoundEntry,
                    &refoundValue,
                    &refoundState,
                    &refoundCapacity,
                    &refoundIndex,
                    &refoundCountAddress)) {
                LOG_WARNING("[PakRuntime] Could not re-find cache entry for eviction path="
                    << pathText
                    << " kind=" << cacheKind
                    << " manager=0x" << std::hex << cacheManager
                    << " hash=0x" << cacheHash);
                return false;
            }

            cacheEntry = refoundEntry;
            cacheIndex = refoundIndex;
            cacheCapacity = refoundCapacity;
            cacheCountAddress = refoundCountAddress;
            entryHash = cacheHash;
            entryState = refoundState;
            TryReadDword(cacheCountAddress, &tableCount);

            LOG_INFO("[PakRuntime] Re-found cache entry for eviction path=" << pathText
                << " kind=" << cacheKind
                << " entry=0x" << std::hex << cacheEntry
                << " value=0x" << refoundValue
                << " state=0x" << refoundState
                << " index=0x" << cacheIndex
                << " capacity=0x" << cacheCapacity);

            if ((entryState & 0xff) != 2) {
                LOG_WARNING("[PakRuntime] Re-found cache entry is not occupied path="
                    << pathText
                    << " entry=0x" << std::hex << cacheEntry
                    << " state=0x" << (entryState & 0xff));
                return false;
            }
        }

        if (!TryWriteByte(cacheEntry + 8, 1)) {
            LOG_WARNING("[PakRuntime] Failed to tombstone cache entry path=" << pathText
                << " entry=0x" << std::hex << cacheEntry);
            return false;
        }

        if (tableCount > 0) {
            TryWriteDword(cacheCountAddress, tableCount - 1);
        }

        const uint32_t newCount = tableCount > 0 ? tableCount - 1 : tableCount;
        LOG_INFO("[PakRuntime] Evicted cached " << cacheKind
            << " path=" << pathText
            << " manager=0x" << std::hex << cacheManager
            << " entry=0x" << cacheEntry
            << " hash=0x" << cacheHash
            << " index=0x" << cacheIndex
            << " capacity=0x" << cacheCapacity
            << " count_addr=0x" << cacheCountAddress
            << " old_count=0x" << tableCount
            << " new_count=0x" << newCount);

        if (cacheKind == "GfxResource") {
            uintptr_t parsedResource = 0;
            DWORD exceptionCode = 0;
            bool runtimePatchEnabled = false;
            float runtimePatchScale = 1.0f;
            if (ToLowerCopy(pathText).find("codex") != std::string::npos) {
                std::lock_guard<std::mutex> lock(g_packagePathMutex);
                if (g_pendingExplicitGfxScaleReload) {
                    g_pendingExplicitGfxScaleReload = false;
                }
                else {
                    g_forcedGfxScaleLarge = !g_forcedGfxScaleLarge;
                    g_forcedGfxScaleValue = g_forcedGfxScaleLarge ? 5.0f : 1.0f;
                }
                g_forcedGfxPatchThisReload = true;
                g_forcedGfxReloadPath = pathText;
                runtimePatchEnabled = true;
                runtimePatchScale = g_forcedGfxScaleValue;
            }
            else {
                std::lock_guard<std::mutex> lock(g_packagePathMutex);
                g_forcedGfxPatchThisReload = false;
                g_forcedGfxReloadPath.clear();
            }

            InterlockedIncrement(&g_forcedGfxReloadDepth);
            const bool reloadSuccess = ForceReloadGfxResource(
                cacheManager,
                pathText,
                cacheParam2,
                &parsedResource,
                &exceptionCode);
            InterlockedDecrement(&g_forcedGfxReloadDepth);

            {
                std::lock_guard<std::mutex> lock(g_packagePathMutex);
                g_forcedGfxPatchThisReload = false;
                g_forcedGfxReloadPath.clear();
            }

            if (reloadSuccess) {
                uintptr_t reloadedEntry = 0;
                uint32_t reloadedValue = 0;
                uint32_t reloadedState = 0;
                uint32_t reloadedCapacity = 0;
                uint32_t reloadedIndex = 0;
                TryFindGfxCacheEntry(
                    cacheManager,
                    cacheHash,
                    &reloadedEntry,
                    &reloadedValue,
                    &reloadedState,
                    &reloadedCapacity,
                    &reloadedIndex);
                LOG_INFO("[PakRuntime] Forced GfxResource reload path=" << pathText
                    << " manager=0x" << std::hex << cacheManager
                    << " param2=0x" << cacheParam2
                    << " parsed_resource=0x" << parsedResource
                    << " runtime_patch=" << (runtimePatchEnabled ? "yes" : "no")
                    << " runtime_scale=" << std::dec << runtimePatchScale
                    << " cache_entry=0x" << std::hex << reloadedEntry
                    << " cache_value=0x" << reloadedValue
                    << " cache_state=0x" << reloadedState
                    << " cache_index=0x" << reloadedIndex
                    << " cache_capacity=0x" << reloadedCapacity);

                if (runtimePatchEnabled) {
                    uintptr_t sceneOwner = 0;
                    uintptr_t sceneBefore = 0;
                    uintptr_t sceneAfterPublish = 0;
                    std::string scenePath;
                    {
                        std::lock_guard<std::mutex> lock(g_packagePathMutex);
                        sceneOwner = g_lastCodexWorldMeshSceneOwner;
                        scenePath = g_lastCodexWorldMeshScenePath;
                    }
                    if (sceneOwner != 0 && scenePath == pathText) {
                        sceneBefore = TryReadPointer(sceneOwner + 0x68);
                    }
                    bool publishedToOwner = false;
                    if (g_publishReloadedGfxToCapturedOwner &&
                        sceneOwner != 0 &&
                        scenePath == pathText &&
                        parsedResource != 0 &&
                        sceneBefore != parsedResource) {
                        publishedToOwner = TryWriteDword(
                            sceneOwner + 0x68,
                            static_cast<uint32_t>(parsedResource));
                        sceneAfterPublish = TryReadPointer(sceneOwner + 0x68);
                    }
                    LOG_INFO("[PakRuntime] Captured WorldMeshScene left intact path="
                        << scenePath
                        << " owner=0x" << std::hex << sceneOwner
                        << " scene_current=0x" << sceneBefore
                        << " publish_reloaded_gfx=" << (publishedToOwner ? "yes" : "no")
                        << " scene_after_publish=0x" << sceneAfterPublish
                        << " parsed_resource=0x" << parsedResource
                        << " reason=no_null_invalidation_publish_probe");
                }
            }
            else {
                LOG_WARNING("[PakRuntime] Forced GfxResource reload failed path=" << pathText
                    << " manager=0x" << std::hex << cacheManager
                    << " exception=0x" << exceptionCode);
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_packagePathMutex);
            if (g_lastCacheHash == cacheHash &&
                g_lastCacheManager == cacheManager &&
                g_lastCachePath == pathText) {
                if (cacheKind == "GfxResource") {
                    g_lastCacheEntry = cacheEntry;
                    g_lastCacheIndex = cacheIndex;
                    g_lastCacheCapacity = cacheCapacity;
                    g_lastCacheCountAddress = cacheCountAddress;
                }
                else {
                    g_lastCachePath.clear();
                    g_lastCacheKind.clear();
                    g_lastCacheManager = 0;
                    g_lastCacheEntry = 0;
                    g_lastCacheHash = 0;
                    g_lastCacheIndex = 0;
                    g_lastCacheCapacity = 0;
                    g_lastCacheCountAddress = 0;
                    g_lastCachePriority = 0;
                    g_lastCacheParam2 = 0;
                }
            }
        }
        return true;
    }

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            return true;
        }

        g_baseAddress = baseAddress;

        const uintptr_t gameplayLoaderRVA = GetLoadObjectCollectionAndInitializeGameplayRVA();
        const uintptr_t objectCollectionRVA = GetLoadObjectCollectionXMLRVA();
        if (gameplayLoaderRVA == 0 || objectCollectionRVA == 0) {
            LOG_WARNING("[PakRuntime] Runtime objectcollection reload disabled for this game version");
            if (BaseAddress::IsSteamVersion()) {
                LOG_WARNING("[PakRuntime] Steam loader RVAs still need mapping");
            }
        }
        else {
            g_loadObjectCollectionAndInitializeGameplayTarget = g_baseAddress + gameplayLoaderRVA;
            g_loadObjectCollectionXMLTarget = g_baseAddress + objectCollectionRVA;

            if (IsBadCodePtr(reinterpret_cast<FARPROC>(g_loadObjectCollectionAndInitializeGameplayTarget)) ||
                IsBadCodePtr(reinterpret_cast<FARPROC>(g_loadObjectCollectionXMLTarget))) {
                LOG_ERROR("[PakRuntime] Loader pointer validation failed");
                g_loadObjectCollectionAndInitializeGameplayTarget = 0;
                g_loadObjectCollectionXMLTarget = 0;
            }
            else {
                MH_STATUS hookStatus = MH_CreateHook(
                    reinterpret_cast<LPVOID>(g_loadObjectCollectionAndInitializeGameplayTarget),
                    reinterpret_cast<LPVOID>(&Hook_LoadObjectCollectionAndInitializeGameplay),
                    reinterpret_cast<LPVOID*>(&g_originalLoadObjectCollectionAndInitializeGameplay));

                if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
                    LOG_ERROR("[PakRuntime] Failed to create gameplay loader hook: "
                        << MH_StatusToString(hookStatus));
                    g_loadObjectCollectionAndInitializeGameplayTarget = 0;
                    g_loadObjectCollectionXMLTarget = 0;
                    g_originalLoadObjectCollectionAndInitializeGameplay = nullptr;
                }
                else {
                    hookStatus = MH_EnableHook(
                        reinterpret_cast<LPVOID>(g_loadObjectCollectionAndInitializeGameplayTarget));
                    if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
                        LOG_ERROR("[PakRuntime] Failed to enable gameplay loader hook: "
                            << MH_StatusToString(hookStatus));
                        MH_RemoveHook(reinterpret_cast<LPVOID>(g_loadObjectCollectionAndInitializeGameplayTarget));
                        g_loadObjectCollectionAndInitializeGameplayTarget = 0;
                        g_loadObjectCollectionXMLTarget = 0;
                        g_originalLoadObjectCollectionAndInitializeGameplay = nullptr;
                    }
                }

                if (g_originalLoadObjectCollectionAndInitializeGameplay != nullptr) {
                    hookStatus = MH_CreateHook(
                        reinterpret_cast<LPVOID>(g_loadObjectCollectionXMLTarget),
                        reinterpret_cast<LPVOID>(&Hook_LoadObjectCollectionXML),
                        reinterpret_cast<LPVOID*>(&g_originalLoadObjectCollectionXML));

                    if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
                        LOG_ERROR("[PakRuntime] Failed to create LoadObjectCollectionXML hook: "
                            << MH_StatusToString(hookStatus));
                        MH_DisableHook(reinterpret_cast<LPVOID>(g_loadObjectCollectionAndInitializeGameplayTarget));
                        MH_RemoveHook(reinterpret_cast<LPVOID>(g_loadObjectCollectionAndInitializeGameplayTarget));
                        g_loadObjectCollectionAndInitializeGameplayTarget = 0;
                        g_loadObjectCollectionXMLTarget = 0;
                        g_originalLoadObjectCollectionAndInitializeGameplay = nullptr;
                        g_originalLoadObjectCollectionXML = nullptr;
                    }
                    else {
                        hookStatus = MH_EnableHook(reinterpret_cast<LPVOID>(g_loadObjectCollectionXMLTarget));
                        if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
                            LOG_ERROR("[PakRuntime] Failed to enable LoadObjectCollectionXML hook: "
                                << MH_StatusToString(hookStatus));
                            MH_RemoveHook(reinterpret_cast<LPVOID>(g_loadObjectCollectionXMLTarget));
                            MH_DisableHook(reinterpret_cast<LPVOID>(g_loadObjectCollectionAndInitializeGameplayTarget));
                            MH_RemoveHook(reinterpret_cast<LPVOID>(g_loadObjectCollectionAndInitializeGameplayTarget));
                            g_loadObjectCollectionAndInitializeGameplayTarget = 0;
                            g_loadObjectCollectionXMLTarget = 0;
                            g_originalLoadObjectCollectionAndInitializeGameplay = nullptr;
                            g_originalLoadObjectCollectionXML = nullptr;
                        }
                    }
                }
            }
        }

        const uintptr_t cacheLoaderRVA = GetLoadAndCacheTextureFromFileRVA();
        if (cacheLoaderRVA == 0) {
            if (BaseAddress::IsSteamVersion()) {
                LOG_WARNING("[PakRuntime] Steam cache loader RVA still needs mapping");
            }
        }
        else {
            g_loadAndCacheTextureFromFileTarget = g_baseAddress + cacheLoaderRVA;
            if (IsBadCodePtr(reinterpret_cast<FARPROC>(g_loadAndCacheTextureFromFileTarget))) {
                LOG_ERROR("[PakRuntime] Cache loader pointer validation failed");
                g_loadAndCacheTextureFromFileTarget = 0;
            }
            else {
                MH_STATUS hookStatus = MH_CreateHook(
                    reinterpret_cast<LPVOID>(g_loadAndCacheTextureFromFileTarget),
                    reinterpret_cast<LPVOID>(&Hook_LoadAndCacheTextureFromFile),
                    reinterpret_cast<LPVOID*>(&g_originalLoadAndCacheTextureFromFile));

                if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
                    LOG_ERROR("[PakRuntime] Failed to create cache loader hook: "
                        << MH_StatusToString(hookStatus));
                    g_loadAndCacheTextureFromFileTarget = 0;
                    g_originalLoadAndCacheTextureFromFile = nullptr;
                }
                else {
                    hookStatus = MH_EnableHook(
                        reinterpret_cast<LPVOID>(g_loadAndCacheTextureFromFileTarget));
                    if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
                        LOG_ERROR("[PakRuntime] Failed to enable cache loader hook: "
                            << MH_StatusToString(hookStatus));
                        MH_RemoveHook(reinterpret_cast<LPVOID>(g_loadAndCacheTextureFromFileTarget));
                        g_loadAndCacheTextureFromFileTarget = 0;
                        g_originalLoadAndCacheTextureFromFile = nullptr;
                    }
                }
            }
        }

        const uintptr_t gfxCacheLoaderRVA = GetLoadAndCacheGfxResourceRVA();
        if (gfxCacheLoaderRVA == 0) {
            if (BaseAddress::IsSteamVersion()) {
                LOG_WARNING("[PakRuntime] Steam GFX cache loader RVA still needs mapping");
            }
        }
        else {
            g_loadAndCacheGfxResourceTarget = g_baseAddress + gfxCacheLoaderRVA;
            if (IsBadCodePtr(reinterpret_cast<FARPROC>(g_loadAndCacheGfxResourceTarget))) {
                LOG_ERROR("[PakRuntime] GFX cache loader pointer validation failed");
                g_loadAndCacheGfxResourceTarget = 0;
            }
            else {
                MH_STATUS hookStatus = MH_CreateHook(
                    reinterpret_cast<LPVOID>(g_loadAndCacheGfxResourceTarget),
                    reinterpret_cast<LPVOID>(&Hook_LoadAndCacheGfxResource),
                    reinterpret_cast<LPVOID*>(&g_originalLoadAndCacheGfxResource));

                if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
                    LOG_ERROR("[PakRuntime] Failed to create GFX cache loader hook: "
                        << MH_StatusToString(hookStatus));
                    g_loadAndCacheGfxResourceTarget = 0;
                    g_originalLoadAndCacheGfxResource = nullptr;
                }
                else {
                    hookStatus = MH_EnableHook(
                        reinterpret_cast<LPVOID>(g_loadAndCacheGfxResourceTarget));
                    if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
                        LOG_ERROR("[PakRuntime] Failed to enable GFX cache loader hook: "
                            << MH_StatusToString(hookStatus));
                        MH_RemoveHook(reinterpret_cast<LPVOID>(g_loadAndCacheGfxResourceTarget));
                        g_loadAndCacheGfxResourceTarget = 0;
                        g_originalLoadAndCacheGfxResource = nullptr;
                    }
                }
            }
        }

        const uintptr_t worldMeshSceneRVA = GetLoadWorldMeshSceneRVA();
        if (worldMeshSceneRVA == 0) {
            if (BaseAddress::IsSteamVersion()) {
                LOG_WARNING("[PakRuntime] Steam WorldMeshScene RVA still needs mapping");
            }
        }
        else {
            g_loadWorldMeshSceneTarget = g_baseAddress + worldMeshSceneRVA;
            if (IsBadCodePtr(reinterpret_cast<FARPROC>(g_loadWorldMeshSceneTarget))) {
                LOG_ERROR("[PakRuntime] WorldMeshScene pointer validation failed");
                g_loadWorldMeshSceneTarget = 0;
            }
            else {
                MH_STATUS hookStatus = MH_CreateHook(
                    reinterpret_cast<LPVOID>(g_loadWorldMeshSceneTarget),
                    reinterpret_cast<LPVOID>(&Hook_LoadWorldMeshScene),
                    reinterpret_cast<LPVOID*>(&g_originalLoadWorldMeshScene));

                if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
                    LOG_ERROR("[PakRuntime] Failed to create WorldMeshScene hook: "
                        << MH_StatusToString(hookStatus));
                    g_loadWorldMeshSceneTarget = 0;
                    g_originalLoadWorldMeshScene = nullptr;
                }
                else {
                    hookStatus = MH_EnableHook(
                        reinterpret_cast<LPVOID>(g_loadWorldMeshSceneTarget));
                    if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
                        LOG_ERROR("[PakRuntime] Failed to enable WorldMeshScene hook: "
                            << MH_StatusToString(hookStatus));
                        MH_RemoveHook(reinterpret_cast<LPVOID>(g_loadWorldMeshSceneTarget));
                        g_loadWorldMeshSceneTarget = 0;
                        g_originalLoadWorldMeshScene = nullptr;
                    }
                }
            }
        }

        const uintptr_t recursivePhysicsUpdateRVA = GetRecursiveSceneObjectPhysicsStateUpdateRVA();
        if (recursivePhysicsUpdateRVA == 0) {
            if (BaseAddress::IsSteamVersion()) {
                LOG_WARNING("[PakRuntime] Steam RecursiveSceneObjectPhysicsStateUpdate RVA still needs mapping");
            }
        }
        else {
            g_recursiveSceneObjectPhysicsStateUpdateTarget = g_baseAddress + recursivePhysicsUpdateRVA;
            if (IsBadCodePtr(reinterpret_cast<FARPROC>(g_recursiveSceneObjectPhysicsStateUpdateTarget))) {
                LOG_ERROR("[PakRuntime] RecursiveSceneObjectPhysicsStateUpdate pointer validation failed");
                g_recursiveSceneObjectPhysicsStateUpdateTarget = 0;
            }
            else {
                MH_STATUS hookStatus = MH_CreateHook(
                    reinterpret_cast<LPVOID>(g_recursiveSceneObjectPhysicsStateUpdateTarget),
                    reinterpret_cast<LPVOID>(&Hook_RecursiveSceneObjectPhysicsStateUpdate),
                    reinterpret_cast<LPVOID*>(&g_originalRecursiveSceneObjectPhysicsStateUpdate));

                if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
                    LOG_ERROR("[PakRuntime] Failed to create RecursiveSceneObjectPhysicsStateUpdate hook: "
                        << MH_StatusToString(hookStatus));
                    g_recursiveSceneObjectPhysicsStateUpdateTarget = 0;
                    g_originalRecursiveSceneObjectPhysicsStateUpdate = nullptr;
                }
                else {
                    hookStatus = MH_EnableHook(
                        reinterpret_cast<LPVOID>(g_recursiveSceneObjectPhysicsStateUpdateTarget));
                    if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
                        LOG_ERROR("[PakRuntime] Failed to enable RecursiveSceneObjectPhysicsStateUpdate hook: "
                            << MH_StatusToString(hookStatus));
                        MH_RemoveHook(reinterpret_cast<LPVOID>(g_recursiveSceneObjectPhysicsStateUpdateTarget));
                        g_recursiveSceneObjectPhysicsStateUpdateTarget = 0;
                        g_originalRecursiveSceneObjectPhysicsStateUpdate = nullptr;
                    }
                }
            }
        }

        const uintptr_t objectLoaderJobCtorRVA = GetObjectLoaderJobConstructorRVA();
        if (objectLoaderJobCtorRVA == 0) {
            if (BaseAddress::IsSteamVersion()) {
                LOG_WARNING("[PakRuntime] Steam ObjectLoaderJob constructor RVA still needs mapping");
            }
        }
        else {
            g_objectLoaderJobConstructorTarget = g_baseAddress + objectLoaderJobCtorRVA;
            if (IsBadCodePtr(reinterpret_cast<FARPROC>(g_objectLoaderJobConstructorTarget))) {
                LOG_ERROR("[PakRuntime] ObjectLoaderJob constructor pointer validation failed");
                g_objectLoaderJobConstructorTarget = 0;
            }
            else {
                MH_STATUS hookStatus = MH_CreateHook(
                    reinterpret_cast<LPVOID>(g_objectLoaderJobConstructorTarget),
                    reinterpret_cast<LPVOID>(&Hook_ObjectLoaderJobConstructor),
                    reinterpret_cast<LPVOID*>(&g_originalObjectLoaderJobConstructor));

                if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
                    LOG_ERROR("[PakRuntime] Failed to create ObjectLoaderJob constructor hook: "
                        << MH_StatusToString(hookStatus));
                    g_objectLoaderJobConstructorTarget = 0;
                    g_originalObjectLoaderJobConstructor = nullptr;
                }
                else {
                    hookStatus = MH_EnableHook(
                        reinterpret_cast<LPVOID>(g_objectLoaderJobConstructorTarget));
                    if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
                        LOG_ERROR("[PakRuntime] Failed to enable ObjectLoaderJob constructor hook: "
                            << MH_StatusToString(hookStatus));
                        MH_RemoveHook(reinterpret_cast<LPVOID>(g_objectLoaderJobConstructorTarget));
                        g_objectLoaderJobConstructorTarget = 0;
                        g_originalObjectLoaderJobConstructor = nullptr;
                    }
                }
            }
        }

        const uintptr_t insertHashTableRVA = GetInsertHashTableRVA();
        if (insertHashTableRVA == 0) {
            if (BaseAddress::IsSteamVersion()) {
                LOG_WARNING("[PakRuntime] Steam hash-table insertion RVA still needs mapping");
            }
        }
        else {
            g_insertHashTableTarget = g_baseAddress + insertHashTableRVA;
            if (IsBadCodePtr(reinterpret_cast<FARPROC>(g_insertHashTableTarget))) {
                LOG_ERROR("[PakRuntime] Hash-table insertion pointer validation failed");
                g_insertHashTableTarget = 0;
            }
            else {
                MH_STATUS hookStatus = MH_CreateHook(
                    reinterpret_cast<LPVOID>(g_insertHashTableTarget),
                    reinterpret_cast<LPVOID>(&Hook_InsertHashTable),
                    reinterpret_cast<LPVOID*>(&g_originalInsertHashTable));

                if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
                    LOG_ERROR("[PakRuntime] Failed to create hash-table insertion hook: "
                        << MH_StatusToString(hookStatus));
                    g_insertHashTableTarget = 0;
                    g_originalInsertHashTable = nullptr;
                }
                else {
                    hookStatus = MH_EnableHook(
                        reinterpret_cast<LPVOID>(g_insertHashTableTarget));
                    if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
                        LOG_ERROR("[PakRuntime] Failed to enable hash-table insertion hook: "
                            << MH_StatusToString(hookStatus));
                        MH_RemoveHook(reinterpret_cast<LPVOID>(g_insertHashTableTarget));
                        g_insertHashTableTarget = 0;
                        g_originalInsertHashTable = nullptr;
                    }
                }
            }
        }

        InstallPackageApiHooks();

        g_initialized = true;
        LOG_INFO("[PakRuntime] Initialized");
        if (g_loadObjectCollectionAndInitializeGameplayTarget != 0) {
            LOG_INFO("[PakRuntime] LoadObjectCollectionAndInitializeGameplay @ 0x" << std::hex
                << g_loadObjectCollectionAndInitializeGameplayTarget);
        }
        if (g_loadObjectCollectionXMLTarget != 0) {
            LOG_INFO("[PakRuntime] LoadObjectCollectionXML @ 0x" << std::hex
                << g_loadObjectCollectionXMLTarget);
        }
        if (g_loadAndCacheTextureFromFileTarget != 0) {
            LOG_INFO("[PakRuntime] LoadAndCacheTextureFromFile @ 0x" << std::hex
                << g_loadAndCacheTextureFromFileTarget);
        }
        if (g_loadAndCacheGfxResourceTarget != 0) {
            LOG_INFO("[PakRuntime] LoadAndCacheGfxResource @ 0x" << std::hex
                << g_loadAndCacheGfxResourceTarget);
        }
        if (g_loadWorldMeshSceneTarget != 0) {
            LOG_INFO("[PakRuntime] LoadWorldMeshScene @ 0x" << std::hex
                << g_loadWorldMeshSceneTarget);
        }
        if (g_recursiveSceneObjectPhysicsStateUpdateTarget != 0) {
            LOG_INFO("[PakRuntime] RecursiveSceneObjectPhysicsStateUpdate @ 0x" << std::hex
                << g_recursiveSceneObjectPhysicsStateUpdateTarget);
        }
        if (g_objectLoaderJobConstructorTarget != 0) {
            LOG_INFO("[PakRuntime] ObjectLoaderJob_Constructor @ 0x" << std::hex
                << g_objectLoaderJobConstructorTarget);
        }
        if (g_insertHashTableTarget != 0) {
            LOG_INFO("[PakRuntime] InsertHashTable @ 0x" << std::hex
                << g_insertHashTableTarget);
        }
        return true;
    }

    void Shutdown() {
        if (g_packageApiHooksInstalled) {
            if (g_packageApi20Target != 0) {
                MH_DisableHook(reinterpret_cast<LPVOID>(g_packageApi20Target));
                MH_RemoveHook(reinterpret_cast<LPVOID>(g_packageApi20Target));
            }
            if (g_packageApi24Target != 0) {
                MH_DisableHook(reinterpret_cast<LPVOID>(g_packageApi24Target));
                MH_RemoveHook(reinterpret_cast<LPVOID>(g_packageApi24Target));
            }
        }

        if (g_resourceReadCurrentHookInstalled && g_resourceReadCurrentTarget != 0) {
            MH_DisableHook(reinterpret_cast<LPVOID>(g_resourceReadCurrentTarget));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_resourceReadCurrentTarget));
        }

        if (g_resourceReadAtHookInstalled && g_resourceReadAtTarget != 0) {
            MH_DisableHook(reinterpret_cast<LPVOID>(g_resourceReadAtTarget));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_resourceReadAtTarget));
        }

        if (g_loadObjectCollectionAndInitializeGameplayTarget != 0) {
            MH_DisableHook(reinterpret_cast<LPVOID>(g_loadObjectCollectionAndInitializeGameplayTarget));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_loadObjectCollectionAndInitializeGameplayTarget));
        }

        if (g_loadObjectCollectionXMLTarget != 0) {
            MH_DisableHook(reinterpret_cast<LPVOID>(g_loadObjectCollectionXMLTarget));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_loadObjectCollectionXMLTarget));
        }

        if (g_loadAndCacheTextureFromFileTarget != 0) {
            MH_DisableHook(reinterpret_cast<LPVOID>(g_loadAndCacheTextureFromFileTarget));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_loadAndCacheTextureFromFileTarget));
        }

        if (g_loadAndCacheGfxResourceTarget != 0) {
            MH_DisableHook(reinterpret_cast<LPVOID>(g_loadAndCacheGfxResourceTarget));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_loadAndCacheGfxResourceTarget));
        }

        if (g_loadWorldMeshSceneTarget != 0) {
            MH_DisableHook(reinterpret_cast<LPVOID>(g_loadWorldMeshSceneTarget));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_loadWorldMeshSceneTarget));
        }

        if (g_recursiveSceneObjectPhysicsStateUpdateTarget != 0) {
            MH_DisableHook(reinterpret_cast<LPVOID>(g_recursiveSceneObjectPhysicsStateUpdateTarget));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_recursiveSceneObjectPhysicsStateUpdateTarget));
        }

        if (g_objectLoaderJobConstructorTarget != 0) {
            MH_DisableHook(reinterpret_cast<LPVOID>(g_objectLoaderJobConstructorTarget));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_objectLoaderJobConstructorTarget));
        }

        if (g_insertHashTableTarget != 0) {
            MH_DisableHook(reinterpret_cast<LPVOID>(g_insertHashTableTarget));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_insertHashTableTarget));
        }

        g_initialized = false;
        g_baseAddress = 0;
        g_loadObjectCollectionAndInitializeGameplayTarget = 0;
        g_loadObjectCollectionXMLTarget = 0;
        g_loadAndCacheTextureFromFileTarget = 0;
        g_loadAndCacheGfxResourceTarget = 0;
        g_loadWorldMeshSceneTarget = 0;
        g_recursiveSceneObjectPhysicsStateUpdateTarget = 0;
        g_objectLoaderJobConstructorTarget = 0;
        g_insertHashTableTarget = 0;
        g_originalLoadObjectCollectionAndInitializeGameplay = nullptr;
        g_originalLoadObjectCollectionXML = nullptr;
        g_originalLoadAndCacheTextureFromFile = nullptr;
        g_originalLoadAndCacheGfxResource = nullptr;
        g_originalLoadWorldMeshScene = nullptr;
        g_originalRecursiveSceneObjectPhysicsStateUpdate = nullptr;
        g_originalObjectLoaderJobConstructor = nullptr;
        g_originalInsertHashTable = nullptr;
        InterlockedExchange(&g_physicsProbeScanCalls, 0);
        InterlockedExchange(&g_physicsProbeLogBudget, 0);
        g_packageManagerObjectAddress = 0;
        g_packageManagerVtable = nullptr;
        g_packageApi20Target = 0;
        g_packageApi24Target = 0;
        g_originalPackageApi20 = nullptr;
        g_originalPackageApi24 = nullptr;
        g_packageApiHooksInstalled = false;
        g_resourceReadCurrentTarget = 0;
        g_originalResourceReadCurrent = nullptr;
        g_resourceReadCurrentHookInstalled = false;
        g_resourceReadAtTarget = 0;
        g_originalResourceReadAt = nullptr;
        g_resourceReadAtHookInstalled = false;
        InterlockedExchange(&g_packageApiRetryInProgress, 0);
        g_packageApiRetryFrameCountdown = 0;
        g_packageApiRetryAttempts = 0;
        g_lastInterestingPackageResult = 0;
        g_lastInterestingPackageResultVtable = 0;
        g_lastInterestingPackagePath.clear();
        g_lastCodexWorldMeshSceneInput = 0;
        g_lastCodexWorldMeshSceneOwner = 0;
        g_lastCodexWorldMeshSceneBefore = 0;
        g_lastCodexWorldMeshSceneAfter = 0;
        g_lastCodexWorldMeshScenePath.clear();
        g_lastCodexObjectLoaderOwner = 0;
        g_lastCodexObjectLoaderKey = 0;
        g_lastCodexObjectLoaderJob = 0;
        g_lastCodexObjectLoaderPathA.clear();
        g_lastCodexObjectLoaderPathB.clear();
        InterlockedExchange(&g_cacheLoadPackageSucceeded, 0);
        g_cacheLoadPackageResult = 0;
        g_cacheLoadPackageResultVtable = 0;

        {
            std::lock_guard<std::mutex> lock(g_packagePathMutex);
            g_packagePathHitCounts.clear();
            g_resourceVtableDumpCounts.clear();
            g_resourceObjectPaths.clear();
            g_resourceReadLogCounts.clear();
            g_capturedCodexGfxTargets.clear();
            g_currentCacheLoadPath.clear();
            g_lastCachePath.clear();
            g_lastCacheKind.clear();
        }
        InterlockedExchange(&g_cacheLoadDepth, 0);
        InterlockedExchange(&g_cacheLoadOpenedPackage, 0);
        g_lastCacheManager = 0;
        g_lastCacheEntry = 0;
        g_lastCacheHash = 0;
        g_lastCacheIndex = 0;
        g_lastCacheCapacity = 0;
        g_lastCacheCountAddress = 0;
        g_lastCachePriority = 0;
        g_lastCacheParam2 = 0;

        InterlockedExchange(&g_pendingObjectCollectionReload, 0);
        InterlockedExchange(&g_reloadInProgress, 0);
        InterlockedExchange(&g_continueLoaderReplayAfterCacheEvict, 0);
        InterlockedExchange(&g_gameplayLoaderContextCaptured, 0);
        InterlockedExchange(&g_objectCollectionContextCaptured, 0);
    }

    void QueueObjectCollectionReload() {
        if (!g_initialized) {
            LOG_ERROR("[PakRuntime] Cannot queue reload: module not initialized");
            return;
        }

        InterlockedExchange(&g_pendingObjectCollectionReload, 1);
        LOG_INFO("[PakRuntime] Queued objectcollection reload for next game frame");
    }

    void SetRuntimeCodexGfxScale(float scale, bool queueReload) {
        if (scale < 0.01f) {
            scale = 0.01f;
        }
        if (scale > 50.0f) {
            scale = 50.0f;
        }

        {
            std::lock_guard<std::mutex> lock(g_packagePathMutex);
            g_forcedGfxScaleValue = scale;
            g_forcedGfxScaleLarge = scale > 1.0f;
            g_pendingExplicitGfxScaleReload = queueReload;
        }

        LOG_INFO("[PakRuntime] Runtime codex GFX scale set to " << std::dec << scale);
        if (queueReload) {
            QueueObjectCollectionReload();
        }
    }

    float GetRuntimeCodexGfxScale() {
        std::lock_guard<std::mutex> lock(g_packagePathMutex);
        return g_forcedGfxScaleValue;
    }

    void CheckHotkey() {
        if (Keybindings::IsActionPressed(Keybindings::Action::ReloadObjectCollection)) {
            LOG_INFO("[PakRuntime] ReloadObjectCollection hotkey detected");
            QueueObjectCollectionReload();
        }
    }

    void UpdateOnGameFrame() {
        if (!g_initialized) {
            return;
        }

        RetryPackageApiHooksOnFrame();

        if (InterlockedExchange(&g_pendingObjectCollectionReload, 0) == 0) {
            return;
        }

        LOG_INFO("[PakRuntime] === Runtime resource reload probe ===");
        if (EvictLastCacheEntry()) {
            if (InterlockedExchange(&g_continueLoaderReplayAfterCacheEvict, 0) == 0) {
                LOG_INFO("[PakRuntime] Cache eviction completed; skipping legacy objectcollection replay");
                return;
            }

            LOG_INFO("[PakRuntime] Cache eviction completed; continuing into loader replay");
        }

        LOG_INFO("[PakRuntime] Gameplay context captured="
            << (InterlockedCompareExchange(&g_gameplayLoaderContextCaptured, 0, 0) ? "yes" : "no")
            << " param1=0x" << std::hex << g_lastGameplayLoaderParam1);
        LOG_INFO("[PakRuntime] Objectcollection context captured="
            << (InterlockedCompareExchange(&g_objectCollectionContextCaptured, 0, 0) ? "yes" : "no")
            << " ecx=0x" << std::hex << g_lastObjectCollectionECX
            << " edx=0x" << g_lastObjectCollectionEDX
            << " edi=0x" << g_lastObjectCollectionEDI);

        if (!g_lastInterestingPackagePath.empty()) {
            LOG_INFO("[PakRuntime] Last interesting package path=" << g_lastInterestingPackagePath
                << " result=0x" << std::hex << g_lastInterestingPackageResult
                << " result_vtable=0x" << g_lastInterestingPackageResultVtable);
        }

        DWORD exceptionCode = 0;
        bool success = false;

        InterlockedExchange(&g_reloadInProgress, 1);
        if (InterlockedCompareExchange(&g_gameplayLoaderContextCaptured, 0, 0) != 0) {
            success = CallLoadObjectCollectionAndInitializeGameplay_Inner(
                g_lastGameplayLoaderParam1, &exceptionCode);
        }
        else if (InterlockedCompareExchange(&g_objectCollectionContextCaptured, 0, 0) != 0) {
            success = CallLoadObjectCollectionXML_Inner(&exceptionCode);
        }
        else {
            LOG_WARNING("[PakRuntime] No captured loader context yet; skipping replay");
            InterlockedExchange(&g_reloadInProgress, 0);
            return;
        }
        InterlockedExchange(&g_reloadInProgress, 0);

        if (success) {
            LOG_INFO("[PakRuntime] Loader replay completed");
        }
        else {
            LOG_ERROR("[PakRuntime] Loader replay crashed with exception 0x"
                << std::hex << exceptionCode);
        }
    }

}
