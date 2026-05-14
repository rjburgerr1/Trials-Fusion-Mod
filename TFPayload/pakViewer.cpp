#include "pch.h"
#include "pakViewer.h"
#include "imgui/imgui.h"
#include "logging.h"
#include "base-address.h"

#include <Windows.h>
#include <MinHook.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr uint32_t kPakMagic = 0x12345678;
constexpr size_t kPakHeaderSize = 12;
constexpr size_t kPakEntrySize = 17;
constexpr uintptr_t kLoadObjectCollectionXmlRvaUplay = 0x395890;
constexpr uintptr_t kLoadObjectCollectionXmlRvaSteam = 0;
constexpr uintptr_t kPackageUnmountByNameRvaUplay = 0x7b6320;
constexpr uintptr_t kPackageUnmountByNameRvaSteam = 0;
constexpr uintptr_t kGameManagerPtrRvaUplay = 0x104b308;
constexpr uintptr_t kGameManagerPtrRvaSteam = 0x104d308;
constexpr uintptr_t kPackageManagerGlobalRvaUplay = 0x1055298;
constexpr uintptr_t kPackageManagerGlobalRvaSteam = 0;
constexpr uintptr_t kLoadAndCacheTextureFromFileRvaUplay = 0x394290;
constexpr uintptr_t kLoadAndCacheGfxResourceRvaUplay = 0x737880;
constexpr uintptr_t kHashStringConstructorRvaUplay = 0x679e30;
constexpr uintptr_t kHashStringDestructorRvaUplay = 0x679780;
constexpr size_t kPackageOpen24VtableOffset = 0x24;
constexpr size_t kPackageMount64VtableOffset = 0x64;
constexpr size_t kResourceStreamReadAtVtableOffset = 0x1c;
constexpr size_t kResourceStreamReadCurrentVtableOffset = 0x20;

using PackageOpen24Func = void*(__thiscall*)(void* thisPtr, int pathString, int mode, int reserved);
using PackageMount64Func = int(__thiscall*)(void* thisPtr, int pakPathString, int packageNameString, int flags);
using PackageUnmountByNameFunc = int(__thiscall*)(void* thisPtr, int packageNameString);
using ResourceStreamReadAtFunc = int(__thiscall*)(void* thisPtr, void* buffer, uint32_t offset, uint32_t size);
using ResourceStreamReadCurrentFunc = int(__thiscall*)(void* thisPtr, void* buffer, uint32_t size);
using LoadAndCacheTextureFromFileFunc = void(__thiscall*)(void* thisPtr, int pathString, int* param2);
using LoadAndCacheGfxResourceFunc = int*(__thiscall*)(void* thisPtr, int* pathString, int param2);
using HashStringConstructorFunc = int*(__thiscall*)(void* thisPtr, int pathString, char flag);
using HashStringDestructorFunc = void(__thiscall*)(void* thisPtr);

struct PakEntry {
    int index = 0;
    uint32_t pathHash = 0;
    uint32_t storedSize = 0;
    uint32_t unpackedSize = 0;
    uint8_t flags = 0;
    uint32_t dataOffset = 0;
    std::string name;
};

struct Attribute {
    std::string key;
    uint32_t keyHash = 0;
    int valueType = 0;
    size_t valueOffset = 0;
    size_t originalSize = 0;
    std::vector<uint8_t> rawValue;
};

struct Node {
    std::string name;
    uint32_t nameHash = 0;
    size_t start = 0;
    size_t end = 0;
    std::vector<Attribute> attrs;
    std::vector<Node> children;
};

struct ObjectRow {
    int index = 0;
    size_t offset = 0;
    size_t size = 0;
    Node* node = nullptr;
    std::string name;
    std::string id;
    std::string type;
    std::string packageId;
    std::string exclusive;
    std::string categoryPath;
    std::string kind;
    std::string exportValue;
    std::string filename;
    std::string originalFilename;
};

struct PakTreeNode {
    std::string name;
    std::string fullPath;
    std::vector<PakTreeNode> children;
    std::vector<int> entries;
};

struct HuffmanEntry {
    int symbol = 0;
    int length = 0;
    uint32_t code = 0;
};

struct HuffmanTree {
    std::vector<HuffmanEntry> entries;
};

struct GameStringArg {
    void* vtable = reinterpret_cast<void*>(0x0147f404);
    uint32_t lengthCapacity = 0;
    const char* text = "";
    uint16_t flags = 0;
    uint16_t reserved = 0;
};

struct RuntimeMountArgs {
    std::string label;
    std::string path;
    std::string packageName;
    int flags = 0;
};

bool g_visible = false;
bool g_hashesLoaded = false;
std::vector<uint8_t> g_pakBytes;
std::vector<PakEntry> g_entries;
std::vector<ObjectRow> g_objects;
PakTreeNode g_treeRoot;
Node g_decodedRoot;
bool g_hasDecodedRoot = false;
int g_selectedObject = -1;
bool g_hasPendingObjectEdit = false;
int g_editGeneration = 0;
std::string g_lastEditSummary;
std::vector<uint8_t> g_originalObjectCollectionPayload;
std::vector<uint8_t> g_serializedObjectCollectionPayload;
std::vector<uint8_t> g_runtimeOverrideObjectCollectionPayload;
std::string g_runtimeOverrideObjectCollectionPath;
uint32_t g_runtimeOverrideGeneration = 0;
bool g_hasRuntimeObjectCollectionOverride = false;
int g_decodedEntryIndex = -1;
std::unordered_map<uint32_t, std::string> g_hashes;
std::string g_status = "Ready.";
std::string g_lastSubmitStatus;
std::string g_loadedPak;
int g_selectedEntry = -1;
uintptr_t g_baseAddress = 0;
bool g_packageOpenHookInstalled = false;
bool g_packageMountHookInstalled = false;
bool g_readAtHookInstalled = false;
bool g_readCurrentHookInstalled = false;
bool g_textureLoadHookInstalled = false;
bool g_gfxLoadHookInstalled = false;
void* g_packageOpen24Target = nullptr;
void* g_packageMount64Target = nullptr;
void* g_readAtTarget = nullptr;
void* g_readCurrentTarget = nullptr;
void* g_textureLoadTarget = nullptr;
void* g_gfxLoadTarget = nullptr;
PackageOpen24Func g_originalPackageOpen24 = nullptr;
PackageMount64Func g_originalPackageMount64 = nullptr;
ResourceStreamReadAtFunc g_originalResourceStreamReadAt = nullptr;
ResourceStreamReadCurrentFunc g_originalResourceStreamReadCurrent = nullptr;
LoadAndCacheTextureFromFileFunc g_originalLoadAndCacheTextureFromFile = nullptr;
LoadAndCacheGfxResourceFunc g_originalLoadAndCacheGfxResource = nullptr;
std::mutex g_runtimeOverrideMutex;
std::unordered_map<void*, uint32_t> g_runtimeOverrideStreams;
std::unordered_map<void*, uint32_t> g_runtimeOverrideStreamOffsets;
std::unordered_map<void*, std::string> g_traceStreams;
std::unordered_set<void*> g_traceStreamsWithMaterialHit;
uint32_t g_runtimeOverrideTaggedStreams = 0;
uint32_t g_runtimeOverridePatchedReads = 0;
uint32_t g_runtimeOverrideCacheEvictions = 0;

char g_pakPath[512] = "";
char g_runtimePakPath[512] = "";
char g_hashPath[512] = "";
char g_filter[256] = "";
char g_objectFilter[256] = "";
char g_selectedObjectFilter[256] = "";
char g_decodedTreeFilterDraft[256] = "";
char g_decodedTreeFilter[256] = "";
bool g_showFullDecodedTree = false;
bool g_allowBasePakLoad = false;
bool g_reloadObjectCollectionAfterRemount = false;
std::unordered_set<const Node*> g_decodedTreeFilterMatches;
std::unordered_map<Attribute*, std::string> g_stringEditBuffers;

void* __fastcall Hook_PackageOpen24(void* thisPtr, void* edx, int pathString, int mode, int reserved);
int __fastcall Hook_PackageMount64(void* thisPtr, void* edx, int pakPathString, int packageNameString, int flags);
int __fastcall Hook_ResourceStreamReadAt(void* thisPtr, void* edx, void* buffer, uint32_t offset, uint32_t size);
int __fastcall Hook_ResourceStreamReadCurrent(void* thisPtr, void* edx, void* buffer, uint32_t size);
void __fastcall Hook_LoadAndCacheTextureFromFile(void* thisPtr, void* edx, int pathString, int* param2);
int* __fastcall Hook_LoadAndCacheGfxResource(void* thisPtr, void* edx, int* pathString, int param2);
std::string FindDefaultDlcPakPath();
std::string StripVirtualPrefix(std::string value);

std::string ParentDirectoryOf(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return {};
    }
    std::string value(path);
    size_t slash = value.find_last_of("\\/");
    if (slash == std::string::npos) {
        return {};
    }
    return value.substr(0, slash + 1);
}

void AppendPakViewerTraceLine(const std::string& line) {
    auto appendTo = [&line](const std::string& path) {
        if (path.empty()) {
            return;
        }
        FILE* file = nullptr;
        fopen_s(&file, path.c_str(), "a");
        if (file) {
            SYSTEMTIME st{};
            GetLocalTime(&st);
            fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\n",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                line.c_str());
            fclose(file);
        }
    };

    char modulePath[MAX_PATH] = {};
    HMODULE module = GetModuleHandleA("TFPayload.dll");
    if (module != nullptr && GetModuleFileNameA(module, modulePath, MAX_PATH) != 0) {
        appendTo(ParentDirectoryOf(modulePath) + "pakviewer_runtime.log");
    }

    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) != 0) {
        appendTo(ParentDirectoryOf(exePath) + "datapack\\pakviewer_runtime.log");
    }
}

void PakTrace(const std::string& line) {
    AppendPakViewerTraceLine(line);
}

std::string ReadGameString(int stringObject) {
    if (stringObject == 0 || IsBadReadPtr(reinterpret_cast<const void*>(stringObject), 12)) {
        return {};
    }

    uint16_t length = *reinterpret_cast<uint16_t*>(stringObject + 6);
    const char* text = *reinterpret_cast<const char**>(stringObject + 8);
    if (length == 0 || text == nullptr || IsBadReadPtr(text, 1)) {
        return {};
    }

    size_t safeLength = 0;
    while (safeLength < length && safeLength < 1024 && text[safeLength] != '\0') {
        ++safeLength;
    }
    return std::string(text, safeLength);
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string NormalizeRuntimePackagePath(std::string value) {
    value = LowerAscii(value);
    std::replace(value.begin(), value.end(), '\\', '/');
    while (value.rfind("./", 0) == 0) {
        value.erase(0, 2);
    }
    while (!value.empty() && value.front() == '/') {
        value.erase(value.begin());
    }
    return value;
}

bool IsDlcPakPath(const std::string& path) {
    return NormalizeRuntimePackagePath(path).find("dlccontent/pack") != std::string::npos;
}

bool IsBaseDataPakPath(const std::string& path) {
    std::string normalized = NormalizeRuntimePackagePath(path);
    return !IsDlcPakPath(path)
        && normalized.size() >= strlen("/data.pak")
        && normalized.rfind("/data.pak") == normalized.size() - strlen("/data.pak");
}

bool TryParsePositiveInt(const std::string& text, size_t offset, size_t& endOffset, int& value) {
    if (offset >= text.size() || !std::isdigit(static_cast<unsigned char>(text[offset]))) {
        return false;
    }

    int parsed = 0;
    size_t pos = offset;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        parsed = parsed * 10 + (text[pos] - '0');
        ++pos;
    }

    value = parsed;
    endOffset = pos;
    return value > 0;
}

std::string IncrementTrailingNumber(const std::string& value, int fallbackNumber) {
    size_t suffixStart = value.size();
    while (suffixStart > 0 && std::isdigit(static_cast<unsigned char>(value[suffixStart - 1]))) {
        --suffixStart;
    }

    if (suffixStart == value.size()) {
        return value + std::to_string(fallbackNumber);
    }

    int suffix = 0;
    for (size_t i = suffixStart; i < value.size(); ++i) {
        suffix = suffix * 10 + (value[i] - '0');
    }
    return value.substr(0, suffixStart) + std::to_string(suffix + 1);
}

GameStringArg MakeGameStringArg(const std::string& value) {
    GameStringArg arg;
    arg.lengthCapacity = static_cast<uint32_t>((value.size() << 16) | value.size());
    arg.text = value.c_str();
    return arg;
}

std::string ResolveRemountPakPath() {
    if (g_runtimePakPath[0] != '\0') {
        return g_runtimePakPath;
    }

    if (!g_loadedPak.empty() && (IsDlcPakPath(g_loadedPak) || IsBaseDataPakPath(g_loadedPak))) {
        return g_loadedPak;
    }

    if (g_pakPath[0] != '\0') {
        std::string currentPath = g_pakPath;
        if (IsDlcPakPath(currentPath) || IsBaseDataPakPath(currentPath)) {
            return currentPath;
        }
    }

    return FindDefaultDlcPakPath();
}

bool DeriveRuntimeMountArgCandidatesForPakPath(const std::string& pakPath, std::vector<RuntimeMountArgs>& candidates, std::string& error) {
    candidates.clear();

    std::string normalized = NormalizeRuntimePackagePath(pakPath);
    if (IsBaseDataPakPath(pakPath)) {
        constexpr int flags = 0;
        candidates.push_back({ "startup base data path", "..\\build\\data_pc\\data.pak", "data:", flags });
        candidates.push_back({ "startup base data bare package", "..\\build\\data_pc\\data.pak", "data", flags });
        candidates.push_back({ "selected base data path", pakPath, "data:", flags });
        candidates.push_back({ "selected base data bare package", pakPath, "data", flags });
        return true;
    }

    size_t packPos = normalized.find("dlccontent/pack");
    if (packPos == std::string::npos) {
        error = "only DLCContent/packN/data/*.pak paths are mapped for remount tests";
        return false;
    }

    size_t packNumberOffset = packPos + strlen("dlccontent/pack");
    size_t packNumberEnd = packNumberOffset;
    int packNumber = 0;
    if (!TryParsePositiveInt(normalized, packNumberOffset, packNumberEnd, packNumber)) {
        error = "could not parse DLC pack number from loaded pak path";
        return false;
    }

    const std::string dataMarker = "/data/";
    size_t dataPos = normalized.find(dataMarker, packNumberEnd);
    if (dataPos == std::string::npos || normalized.rfind(".pak") != normalized.size() - 4) {
        error = "loaded pak path does not look like DLCContent/packN/data/name.pak";
        return false;
    }

    std::string fileName = normalized.substr(dataPos + dataMarker.size());
    std::string packageBase = fileName.substr(0, fileName.size() - 4);
    int packageNumber = packNumber + 1;
    int flags = packNumber * 0x0a;
    std::string physicalPath = "DLCContent/pack" + std::to_string(packNumber) + "/data/" + fileName;
    std::string legacyPath = "content" + std::to_string(packageNumber) + ":/data/" + fileName;
    std::string legacyPackageName = IncrementTrailingNumber(packageBase, packageNumber);

    candidates.push_back({ "physical path + colon package", physicalPath, packageBase + ":", flags });
    candidates.push_back({ "physical path + bare package", physicalPath, packageBase, flags });
    candidates.push_back({ "dot physical path + colon package", "./" + physicalPath, packageBase + ":", flags });
    candidates.push_back({ "legacy logical path", legacyPath, legacyPackageName, flags });
    return true;
}

bool DeriveRuntimeMountArgCandidatesForLoadedPak(std::vector<RuntimeMountArgs>& candidates, std::string& error) {
    std::string pakPath = ResolveRemountPakPath();
    if (pakPath.empty()) {
        error = "no loaded pak and no default DLC pak path found";
        return false;
    }
    return DeriveRuntimeMountArgCandidatesForPakPath(pakPath, candidates, error);
}

bool DeriveRuntimeMountArgsForLoadedPak(RuntimeMountArgs& args, std::string& error) {
    std::vector<RuntimeMountArgs> candidates;
    if (!DeriveRuntimeMountArgCandidatesForLoadedPak(candidates, error)) {
        return false;
    }
    if (candidates.empty()) {
        error = "no remount candidates were derived";
        return false;
    }

    args = candidates.front();
    return true;
}

bool IsObjectCollectionPath(const std::string& path) {
    std::string normalized = NormalizeRuntimePackagePath(path);
    return normalized.find("objectcollection") != std::string::npos
        && normalized.size() >= 4
        && normalized.rfind(".xml") == normalized.size() - 4;
}

bool IsInterestingRuntimePath(const std::string& path) {
    std::string normalized = NormalizeRuntimePackagePath(StripVirtualPrefix(path));
    return normalized.find("objectcollection") != std::string::npos
        || normalized.find("cheetah") != std::string::npos
        || normalized.find("tire") != std::string::npos
        || normalized.find("tyre") != std::string::npos
        || normalized.find("wheel") != std::string::npos
        || normalized.find("material") != std::string::npos;
}

bool IsCheetahWheelMaterialProbePath(const std::string& path) {
    std::string normalized = NormalizeRuntimePackagePath(StripVirtualPrefix(path));
    return normalized.find("objects/pack0/cheetah_front_wheel_2720419641.mdl") != std::string::npos
        || normalized.find("objects/pack0/cheetah_rear_wheel_4221031662.mdl") != std::string::npos
        || normalized.find("objects/pack0/cheetah_rim_front_329403873.mdl") != std::string::npos
        || normalized.find("objects/pack0/cheetah_rim_rear_3870804077.mdl") != std::string::npos;
}

bool IsCheetahWheelGfxPath(const std::string& path) {
    std::string normalized = NormalizeRuntimePackagePath(StripVirtualPrefix(path));
    return normalized.find("vehicles/bikes/cheetah/cheetah_wheel_front_1.gfx") != std::string::npos
        || normalized.find("vehicles/bikes/cheetah/cheetah_wheel_rear_1.gfx") != std::string::npos
        || normalized.find("vehicles/bikes/cheetah/cheetah_rim_front_1.gfx") != std::string::npos
        || normalized.find("vehicles/bikes/cheetah/cheetah_rim_rear_1.gfx") != std::string::npos;
}

uint32_t RuntimeOverrideGeneration() {
    if (!g_hasRuntimeObjectCollectionOverride) {
        return 0;
    }
    return g_runtimeOverrideGeneration;
}

bool RuntimeOverrideMatchesPath(const std::string& path) {
    if (!g_hasRuntimeObjectCollectionOverride || g_runtimeOverrideObjectCollectionPath.empty()) {
        return false;
    }

    std::string requested = NormalizeRuntimePackagePath(StripVirtualPrefix(path));
    std::string staged = NormalizeRuntimePackagePath(StripVirtualPrefix(g_runtimeOverrideObjectCollectionPath));
    auto stripPackagePrefix = [](std::string& value) {
        size_t colonSlash = value.find(":/");
        if (colonSlash != std::string::npos) {
            value.erase(0, colonSlash + 2);
        }
    };
    stripPackagePrefix(requested);
    stripPackagePrefix(staged);

    if (requested == staged) {
        return true;
    }

    return IsObjectCollectionPath(requested)
        && IsObjectCollectionPath(staged)
        && ((requested.size() > staged.size()
                && requested.compare(requested.size() - staged.size(), staged.size(), staged) == 0)
            || (staged.size() > requested.size()
                && staged.compare(staged.size() - requested.size(), requested.size(), requested) == 0));
}

void ClearRuntimeOverrideState() {
    std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
    g_runtimeOverrideObjectCollectionPayload.clear();
    g_runtimeOverrideObjectCollectionPath.clear();
    g_hasRuntimeObjectCollectionOverride = false;
    ++g_runtimeOverrideGeneration;
    g_runtimeOverrideStreams.clear();
    g_runtimeOverrideStreamOffsets.clear();
    g_traceStreams.clear();
    g_traceStreamsWithMaterialHit.clear();
    g_runtimeOverrideTaggedStreams = 0;
    g_runtimeOverridePatchedReads = 0;
    g_runtimeOverrideCacheEvictions = 0;
}

bool ComputeGamePathHash(int pathString, uint32_t& hash) {
    if (g_baseAddress == 0 || kHashStringConstructorRvaUplay == 0 || kHashStringDestructorRvaUplay == 0) {
        return false;
    }

    uint8_t hashString[32] = {};
    HashStringConstructorFunc construct = reinterpret_cast<HashStringConstructorFunc>(g_baseAddress + kHashStringConstructorRvaUplay);
    HashStringDestructorFunc destruct = reinterpret_cast<HashStringDestructorFunc>(g_baseAddress + kHashStringDestructorRvaUplay);

    int* hashPtr = nullptr;
    __try {
        hashPtr = construct(hashString, pathString, 1);
        if (hashPtr == nullptr || IsBadReadPtr(hashPtr, sizeof(int))) {
            destruct(hashString);
            return false;
        }
        hash = static_cast<uint32_t>(*hashPtr);
        destruct(hashString);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

bool EvictTextureLoadCacheEntry(void* thisPtr, int pathString, const std::string& path) {
    if (thisPtr == nullptr || IsBadReadPtr(thisPtr, 0x26c)) {
        return false;
    }

    uint32_t hash = 0;
    if (!ComputeGamePathHash(pathString, hash)) {
        LOG_WARNING("[PakViewer] Could not hash texture cache path for eviction path=" << path);
        return false;
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(thisPtr);
    uint32_t capacity = *reinterpret_cast<uint32_t*>(base + 0x264);
    void* tablePtr = *reinterpret_cast<void**>(base + 0x268);
    if (capacity == 0 || (capacity & (capacity - 1)) != 0 || tablePtr == nullptr
        || IsBadReadPtr(tablePtr, capacity * 0x0c)) {
        LOG_WARNING("[PakViewer] Texture cache table unavailable for eviction path=" << path
            << " capacity=" << capacity << " table=0x" << std::hex << reinterpret_cast<uintptr_t>(tablePtr));
        return false;
    }

    uint32_t mixed = ~(hash << 15) + hash;
    mixed = ((mixed >> 10) ^ mixed) * 9;
    mixed = mixed ^ (mixed >> 6);
    mixed = mixed + ~(mixed << 11);
    uint32_t index = ((mixed >> 16) ^ mixed) & (capacity - 1);
    uint8_t* table = reinterpret_cast<uint8_t*>(tablePtr);

    for (uint32_t probes = 0; probes < capacity; ++probes) {
        uint8_t* slot = table + index * 0x0c;
        uint32_t slotHash = *reinterpret_cast<uint32_t*>(slot);
        uint8_t state = *(slot + 8);
        if (state == 0) {
            return false;
        }
        if (state == 2 && slotHash == hash) {
            *(slot + 8) = 1;
            ++g_runtimeOverrideCacheEvictions;
            std::ostringstream oss;
            oss << "[PakViewer] Evicted texture/objectcollection cache entry path=" << path
                << " hash=0x" << std::hex << hash
                << " index=" << std::dec << index;
            PakTrace(oss.str());
            return true;
        }
        index = (index + 1) & (capacity - 1);
    }
    return false;
}

bool EvictGfxLoadCacheEntry(void* thisPtr, int pathString, const std::string& path) {
    if (thisPtr == nullptr || IsBadReadPtr(thisPtr, 0x28)) {
        return false;
    }

    uint32_t hash = 0;
    if (!ComputeGamePathHash(pathString, hash)) {
        PakTrace("[PakViewer/GfxLoad] could not hash path for cache probe path=" + path);
        return false;
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(thisPtr);
    uint32_t capacity = *reinterpret_cast<uint32_t*>(base + 0x20);
    void* tablePtr = *reinterpret_cast<void**>(base + 0x24);
    if (capacity == 0 || (capacity & (capacity - 1)) != 0 || tablePtr == nullptr
        || IsBadReadPtr(tablePtr, capacity * 0x0c)) {
        std::ostringstream oss;
        oss << "[PakViewer/GfxLoad] cache table unavailable path=" << path
            << " this=0x" << std::hex << reinterpret_cast<uintptr_t>(thisPtr)
            << " capacity=" << std::dec << capacity
            << " table=0x" << std::hex << reinterpret_cast<uintptr_t>(tablePtr);
        PakTrace(oss.str());
        return false;
    }

    uint32_t mixed = ~(hash << 15) + hash;
    mixed = ((mixed >> 10) ^ mixed) * 9;
    mixed = mixed ^ (mixed >> 6);
    mixed = mixed + ~(mixed << 11);
    uint32_t index = ((mixed >> 16) ^ mixed) & (capacity - 1);
    uint8_t* table = reinterpret_cast<uint8_t*>(tablePtr);

    for (uint32_t probes = 0; probes < capacity; ++probes) {
        uint8_t* slot = table + index * 0x0c;
        uint32_t slotHash = *reinterpret_cast<uint32_t*>(slot);
        uint8_t state = *(slot + 8);
        if (state == 0) {
            std::ostringstream oss;
            oss << "[PakViewer/GfxLoad] cache miss path=" << path
                << " hash=0x" << std::hex << hash
                << " first_empty_index=" << std::dec << index;
            PakTrace(oss.str());
            return false;
        }
        if (state == 2 && slotHash == hash) {
            *(slot + 8) = 1;
            ++g_runtimeOverrideCacheEvictions;
            std::ostringstream oss;
            oss << "[PakViewer/GfxLoad] evicted parsed GFX cache path=" << path
                << " hash=0x" << std::hex << hash
                << " index=" << std::dec << index;
            PakTrace(oss.str());
            return true;
        }
        index = (index + 1) & (capacity - 1);
    }

    PakTrace("[PakViewer/GfxLoad] cache probe exhausted path=" + path);
    return false;
}

bool PatchRuntimeOverrideRead(void* stream, void* buffer, uint32_t offset, uint32_t size) {
    if (stream == nullptr || buffer == nullptr || size == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
    auto it = g_runtimeOverrideStreams.find(stream);
    if (it == g_runtimeOverrideStreams.end() || it->second != RuntimeOverrideGeneration()) {
        return false;
    }

    if (!g_hasRuntimeObjectCollectionOverride || offset >= g_runtimeOverrideObjectCollectionPayload.size()) {
        return false;
    }

    size_t available = g_runtimeOverrideObjectCollectionPayload.size() - offset;
    size_t copySize = std::min<size_t>(size, available);
    memcpy(buffer, g_runtimeOverrideObjectCollectionPayload.data() + offset, copySize);
    ++g_runtimeOverridePatchedReads;
    return true;
}

void TraceCheetahMaterialIdInRead(void* stream, void* buffer, uint32_t offset, uint32_t size) {
    if (stream == nullptr || buffer == nullptr || size < sizeof(uint32_t)) {
        return;
    }

    std::string path;
    {
        std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
        auto it = g_traceStreams.find(stream);
        if (it == g_traceStreams.end() || g_traceStreamsWithMaterialHit.count(stream) != 0) {
            return;
        }
        path = it->second;
    }

    constexpr uint32_t kCheetahTireMaterialId = 2768512889u;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(buffer);
    for (uint32_t i = 0; i + sizeof(uint32_t) <= size; ++i) {
        uint32_t value = static_cast<uint32_t>(bytes[i])
            | (static_cast<uint32_t>(bytes[i + 1]) << 8)
            | (static_cast<uint32_t>(bytes[i + 2]) << 16)
            | (static_cast<uint32_t>(bytes[i + 3]) << 24);
        if (value == kCheetahTireMaterialId) {
            {
                std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
                g_traceStreamsWithMaterialHit.insert(stream);
            }
            std::ostringstream oss;
            oss << "[PakViewer/CheetahProbe] material id 2768512889 found in stream path=" << path
                << " read_offset=0x" << std::hex << offset
                << " buffer_offset=0x" << i
                << " absolute=0x" << (offset + i)
                << " size=0x" << size;
            PakTrace(oss.str());
            return;
        }
    }
}

void InstallRuntimeReadHooksForStream(void* stream) {
    if (stream == nullptr || IsBadReadPtr(stream, sizeof(void*))) {
        return;
    }

    void** vtable = *reinterpret_cast<void***>(stream);
    if (vtable == nullptr || IsBadReadPtr(vtable, kResourceStreamReadCurrentVtableOffset + sizeof(void*))) {
        return;
    }

    void* readAtTarget = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(vtable) + kResourceStreamReadAtVtableOffset);
    void* readCurrentTarget = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(vtable) + kResourceStreamReadCurrentVtableOffset);

    if (!g_readAtHookInstalled && readAtTarget != nullptr) {
        MH_STATUS status = MH_CreateHook(
            readAtTarget,
            reinterpret_cast<LPVOID>(&Hook_ResourceStreamReadAt),
            reinterpret_cast<LPVOID*>(&g_originalResourceStreamReadAt));
        if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
            status = MH_EnableHook(readAtTarget);
            if (status == MH_OK || status == MH_ERROR_ENABLED) {
                g_readAtHookInstalled = true;
                g_readAtTarget = readAtTarget;
                LOG_INFO("[PakViewer] Resource stream read-at hook installed");
            }
        }
        if (!g_readAtHookInstalled) {
            LOG_WARNING("[PakViewer] Failed to install resource stream read-at hook: " << MH_StatusToString(status));
        }
    }

    if (!g_readCurrentHookInstalled && readCurrentTarget != nullptr) {
        MH_STATUS status = MH_CreateHook(
            readCurrentTarget,
            reinterpret_cast<LPVOID>(&Hook_ResourceStreamReadCurrent),
            reinterpret_cast<LPVOID*>(&g_originalResourceStreamReadCurrent));
        if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
            status = MH_EnableHook(readCurrentTarget);
            if (status == MH_OK || status == MH_ERROR_ENABLED) {
                g_readCurrentHookInstalled = true;
                g_readCurrentTarget = readCurrentTarget;
                LOG_INFO("[PakViewer] Resource stream read-current hook installed");
            }
        }
        if (!g_readCurrentHookInstalled) {
            LOG_WARNING("[PakViewer] Failed to install resource stream read-current hook: " << MH_StatusToString(status));
        }
    }
}

void* __fastcall Hook_PackageOpen24(void* thisPtr, void* /*edx*/, int pathString, int mode, int reserved) {
    std::string path = ReadGameString(pathString);
    void* stream = g_originalPackageOpen24
        ? g_originalPackageOpen24(thisPtr, pathString, mode, reserved)
        : nullptr;

    if (IsInterestingRuntimePath(path)) {
        std::ostringstream oss;
        oss << "[PakViewer/Pkg24] path=" << path
            << " mode=0x" << std::hex << mode
            << " result=0x" << reinterpret_cast<uintptr_t>(stream);
        PakTrace(oss.str());
    }

    if (stream != nullptr && IsObjectCollectionPath(path)) {
        std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
        if (RuntimeOverrideMatchesPath(path) && !g_runtimeOverrideObjectCollectionPayload.empty()) {
            uint32_t generation = RuntimeOverrideGeneration();
            g_runtimeOverrideStreams[stream] = generation;
            g_runtimeOverrideStreamOffsets[stream] = 0;
            ++g_runtimeOverrideTaggedStreams;
            std::ostringstream oss;
            oss << "[PakViewer] Tagged objectcollection stream for runtime override path="
                << path << " bytes=" << g_runtimeOverrideObjectCollectionPayload.size();
            PakTrace(oss.str());
        }
        else if (g_hasRuntimeObjectCollectionOverride) {
            LOG_VERBOSE("[PakViewer] Ignoring objectcollection stream path=" << path
                << " override_path=" << g_runtimeOverrideObjectCollectionPath);
        }
    }

    if (stream != nullptr && IsObjectCollectionPath(path)) {
        InstallRuntimeReadHooksForStream(stream);
    }

    if (stream != nullptr && IsCheetahWheelMaterialProbePath(path)) {
        {
            std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
            g_traceStreams[stream] = NormalizeRuntimePackagePath(StripVirtualPrefix(path));
        }
        InstallRuntimeReadHooksForStream(stream);
        PakTrace("[PakViewer/CheetahProbe] tagged stream path=" + NormalizeRuntimePackagePath(StripVirtualPrefix(path)));
    }

    return stream;
}

void __fastcall Hook_LoadAndCacheTextureFromFile(void* thisPtr, void* /*edx*/, int pathString, int* param2) {
    std::string path = ReadGameString(pathString);
    bool shouldForceReload = false;
    {
        std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
        shouldForceReload = g_hasRuntimeObjectCollectionOverride && RuntimeOverrideMatchesPath(path);
    }

    if (IsInterestingRuntimePath(path)) {
        std::ostringstream oss;
        oss << "[PakViewer/TextureLoad] path=" << path
            << " this=0x" << std::hex << reinterpret_cast<uintptr_t>(thisPtr)
            << " param2=0x" << reinterpret_cast<uintptr_t>(param2)
            << " force_reload=" << std::dec << (shouldForceReload ? 1 : 0);
        PakTrace(oss.str());
    }

    if (shouldForceReload) {
        EvictTextureLoadCacheEntry(thisPtr, pathString, path);
    }

    if (g_originalLoadAndCacheTextureFromFile) {
        g_originalLoadAndCacheTextureFromFile(thisPtr, pathString, param2);
    }
}

int* __fastcall Hook_LoadAndCacheGfxResource(void* thisPtr, void* /*edx*/, int* pathString, int param2) {
    int rawPathString = reinterpret_cast<int>(pathString);
    std::string path = ReadGameString(rawPathString);
    bool isCheetahWheelGfx = IsCheetahWheelGfxPath(path);
    bool hasRuntimeOverride = false;
    {
        std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
        hasRuntimeOverride = g_hasRuntimeObjectCollectionOverride;
    }

    if (isCheetahWheelGfx) {
        std::ostringstream oss;
        oss << "[PakViewer/GfxLoad] path=" << path
            << " this=0x" << std::hex << reinterpret_cast<uintptr_t>(thisPtr)
            << " pathString=0x" << reinterpret_cast<uintptr_t>(pathString)
            << " param2=0x" << param2
            << " force_reload=" << std::dec << (hasRuntimeOverride ? 1 : 0);
        PakTrace(oss.str());

        if (hasRuntimeOverride) {
            EvictGfxLoadCacheEntry(thisPtr, rawPathString, path);
        }
    }

    int* result = g_originalLoadAndCacheGfxResource
        ? g_originalLoadAndCacheGfxResource(thisPtr, pathString, param2)
        : nullptr;

    if (isCheetahWheelGfx) {
        std::ostringstream oss;
        oss << "[PakViewer/GfxLoad] result path=" << path
            << " resource=0x" << std::hex << reinterpret_cast<uintptr_t>(result);
        PakTrace(oss.str());
    }
    return result;
}

int __fastcall Hook_PackageMount64(void* thisPtr, void* /*edx*/, int pakPathString, int packageNameString, int flags) {
    std::string pakPath = ReadGameString(pakPathString);
    std::string packageName = ReadGameString(packageNameString);
    LOG_INFO("[PakViewer] Package mount/load call path=" << pakPath
        << " package=" << packageName
        << " flags=0x" << std::hex << flags);

    int result = g_originalPackageMount64
        ? g_originalPackageMount64(thisPtr, pakPathString, packageNameString, flags)
        : 0;

    LOG_INFO("[PakViewer] Package mount/load returned " << std::dec << result
        << " path=" << pakPath
        << " package=" << packageName);
    return result;
}

int __fastcall Hook_ResourceStreamReadAt(void* thisPtr, void* /*edx*/, void* buffer, uint32_t offset, uint32_t size) {
    int result = g_originalResourceStreamReadAt
        ? g_originalResourceStreamReadAt(thisPtr, buffer, offset, size)
        : 0;

    if (PatchRuntimeOverrideRead(thisPtr, buffer, offset, size)) {
        LOG_VERBOSE("[PakViewer] Patched objectcollection read-at offset=" << offset << " size=" << size);
    }
    TraceCheetahMaterialIdInRead(thisPtr, buffer, offset, size);
    return result;
}

int __fastcall Hook_ResourceStreamReadCurrent(void* thisPtr, void* /*edx*/, void* buffer, uint32_t size) {
    uint32_t offset = 0;
    {
        std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
        auto it = g_runtimeOverrideStreamOffsets.find(thisPtr);
        if (it != g_runtimeOverrideStreamOffsets.end()) {
            offset = it->second;
        }
    }

    int result = g_originalResourceStreamReadCurrent
        ? g_originalResourceStreamReadCurrent(thisPtr, buffer, size)
        : 0;

    if (PatchRuntimeOverrideRead(thisPtr, buffer, offset, size)) {
        std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
        g_runtimeOverrideStreamOffsets[thisPtr] = offset + size;
        LOG_VERBOSE("[PakViewer] Patched objectcollection read-current offset=" << offset << " size=" << size);
    }
    TraceCheetahMaterialIdInRead(thisPtr, buffer, offset, size);
    return result;
}

uint16_t ReadU16(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}

uint32_t ReadU32(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint32_t>(data[offset])
        | (static_cast<uint32_t>(data[offset + 1]) << 8)
        | (static_cast<uint32_t>(data[offset + 2]) << 16)
        | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

void AppendU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void AppendU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void WriteU32At(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    if (offset + 4 > data.size()) {
        return;
    }
    data[offset] = static_cast<uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

uint32_t ReverseBits(uint32_t value, int bits) {
    uint32_t reversed = 0;
    for (int i = 0; i < bits; ++i) {
        reversed = (reversed << 1) | (value & 1);
        value >>= 1;
    }
    return reversed;
}

std::string Hex32(uint32_t value) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return oss.str();
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string NormalizePakPath(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    while (!value.empty() && value.front() == '/') {
        value.erase(value.begin());
    }
    return value;
}

std::string StripVirtualPrefix(std::string value) {
    const char* prefixes[] = {"<evo2_devfiles>", "<evo2_sourcefiles>"};
    for (const char* prefix : prefixes) {
        size_t length = strlen(prefix);
        if (value.compare(0, length, prefix) == 0) {
            return value.substr(length);
        }
    }
    return value;
}

std::vector<std::string> SplitPath(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        size_t end = slash == std::string::npos ? path.size() : slash;
        if (end > start) {
            parts.push_back(path.substr(start, end - start));
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return parts;
}

PakTreeNode* FindOrAddChild(PakTreeNode& node, const std::string& name) {
    for (PakTreeNode& child : node.children) {
        if (child.name == name) {
            return &child;
        }
    }

    PakTreeNode child;
    child.name = name;
    child.fullPath = node.fullPath.empty() ? name : node.fullPath + "/" + name;
    node.children.push_back(child);
    return &node.children.back();
}

int TreeEntryCount(const PakTreeNode& node) {
    int count = static_cast<int>(node.entries.size());
    for (const PakTreeNode& child : node.children) {
        count += TreeEntryCount(child);
    }
    return count;
}

void SortTree(PakTreeNode& node) {
    std::sort(node.children.begin(), node.children.end(), [](const PakTreeNode& a, const PakTreeNode& b) {
        return Lower(a.name) < Lower(b.name);
    });
    std::sort(node.entries.begin(), node.entries.end(), [](int a, int b) {
        return Lower(g_entries[a].name) < Lower(g_entries[b].name);
    });
    for (PakTreeNode& child : node.children) {
        SortTree(child);
    }
}

void BuildTree() {
    g_treeRoot = PakTreeNode{};
    g_treeRoot.name = "/";

    for (const PakEntry& entry : g_entries) {
        std::string path = NormalizePakPath(entry.name);
        std::vector<std::string> parts = SplitPath(path);

        PakTreeNode* node = &g_treeRoot;
        if (parts.empty()) {
            node = FindOrAddChild(*node, "<unnamed>");
        }
        else if (parts.size() == 1 && !path.empty() && path[0] == '#') {
            node = FindOrAddChild(*node, "<unresolved>");
            node->entries.push_back(entry.index);
            continue;
        }
        else {
            for (size_t i = 0; i + 1 < parts.size(); ++i) {
                node = FindOrAddChild(*node, parts[i]);
            }
        }

        node->entries.push_back(entry.index);
    }

    SortTree(g_treeRoot);
}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();
    if (size < 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);

    out.assign(static_cast<size_t>(size), 0);
    if (!out.empty()) {
        file.read(reinterpret_cast<char*>(out.data()), size);
    }
    return file.good() || file.eof();
}

bool WriteFileBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    if (!bytes.empty()) {
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return file.good();
}

std::string DirectoryOfModule() {
    char path[MAX_PATH] = {};
    HMODULE module = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&DirectoryOfModule),
            &module)) {
        GetModuleFileNameA(module, path, MAX_PATH);
        std::string value(path);
        size_t slash = value.find_last_of("\\/");
        return slash == std::string::npos ? std::string() : value.substr(0, slash);
    }
    return std::string();
}

std::string ParentDirectory(const std::string& path) {
    size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return std::string();
    }
    return path.substr(0, slash);
}

std::string DriveRootOf(const std::string& path) {
    if (path.size() >= 2 && path[1] == ':') {
        return path.substr(0, 2) + "\\";
    }
    return std::string();
}

bool FileExists(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void AddDefaultDlcPakCandidates(std::vector<std::string>& candidates) {
    std::string moduleDir = DirectoryOfModule();
    if (!moduleDir.empty()) {
        candidates.push_back(moduleDir + "\\DLCContent\\pack6\\data\\data6.pak");
    }
    candidates.push_back("F:\\Trials Fusion\\datapack\\DLCContent\\pack6\\data\\data6.pak");
    candidates.push_back("F:\\SteamLibrary\\steamapps\\common\\Trials Fusion\\datapack\\DLCContent\\pack6\\data\\data6.pak");
    candidates.push_back("datapack\\DLCContent\\pack6\\data\\data6.pak");
    candidates.push_back("DLCContent\\pack6\\data\\data6.pak");
}

std::string FindDefaultDlcPakPath() {
    std::vector<std::string> candidates;
    AddDefaultDlcPakCandidates(candidates);
    for (const std::string& candidate : candidates) {
        if (FileExists(candidate)) {
            return candidate;
        }
    }
    return candidates.empty() ? std::string() : candidates.front();
}

void AddGameDataPakCandidatesFromPath(std::vector<std::string>& candidates, const std::string& path) {
    if (path.empty()) {
        return;
    }

    std::string current = path;
    for (int i = 0; i < 4 && !current.empty(); ++i) {
        candidates.push_back(current + "\\build\\data_pc\\data.pak");
        candidates.push_back(current + "\\data_pc\\data.pak");
        current = ParentDirectory(current);
    }

    std::string driveRoot = DriveRootOf(path);
    if (!driveRoot.empty()) {
        candidates.push_back(driveRoot + "Trials Fusion\\build\\data_pc\\data.pak");
        candidates.push_back(driveRoot + "SteamLibrary\\steamapps\\common\\Trials Fusion\\build\\data_pc\\data.pak");
    }
}

void SetDefaultPaths() {
    std::string defaultDlcPak = FindDefaultDlcPakPath();

    if (g_runtimePakPath[0] == '\0' && !defaultDlcPak.empty()) {
        strncpy_s(g_runtimePakPath, defaultDlcPak.c_str(), _TRUNCATE);
    }

    if (g_pakPath[0] == '\0') {
        std::string moduleDir = DirectoryOfModule();
        std::vector<std::string> candidates;
        if (!defaultDlcPak.empty()) {
            candidates.push_back(defaultDlcPak);
        }
        AddDefaultDlcPakCandidates(candidates);
        if (!moduleDir.empty()) {
            AddGameDataPakCandidatesFromPath(candidates, moduleDir);
            candidates.push_back(moduleDir + "\\data.pak");
        }
        AddGameDataPakCandidatesFromPath(candidates, ".");
        candidates.push_back("F:\\Trials Fusion\\build\\data_pc\\data.pak");
        candidates.push_back("F:\\SteamLibrary\\steamapps\\common\\Trials Fusion\\build\\data_pc\\data.pak");
        candidates.push_back("datapack\\data.pak");
        AddDefaultDlcPakCandidates(candidates);

        for (const std::string& candidate : candidates) {
            if (FileExists(candidate)) {
                strncpy_s(g_pakPath, candidate.c_str(), _TRUNCATE);
                break;
            }
        }
        if (g_pakPath[0] == '\0') {
            strncpy_s(g_pakPath,
                !defaultDlcPak.empty() ? defaultDlcPak.c_str() : "F:\\Trials Fusion\\build\\data_pc\\data.pak",
                _TRUNCATE);
        }
    }

    if (g_hashPath[0] == '\0') {
        std::string moduleDir = DirectoryOfModule();
        std::vector<std::string> candidates;
        if (!moduleDir.empty()) {
            candidates.push_back(moduleDir + "\\..\\reverse_notes\\trials_hashes.csv");
            candidates.push_back(moduleDir + "\\reverse_notes\\trials_hashes.csv");
        }
        candidates.push_back("reverse_notes\\trials_hashes.csv");
        candidates.push_back("F:\\VSProjects\\Trials-Fusion-Mod\\reverse_notes\\trials_hashes.csv");

        for (const std::string& candidate : candidates) {
            if (FileExists(candidate)) {
                strncpy_s(g_hashPath, candidate.c_str(), _TRUNCATE);
                break;
            }
        }
    }
}

void AddBuiltInHashes() {
    const std::pair<uint32_t, const char*> values[] = {
        {0x68C4C754, "collection"}, {0x86CE3990, "categories"}, {0xF71D1550, "category"},
        {0xCA82E140, "name"}, {0xAC346ED5, "objects"}, {0xD8D20FE4, "object"},
        {0x1A267D88, "filename"}, {0x94A26A5C, "originalFilename"}, {0x00255B12, "ID"},
        {0x0036EF52, "id"}, {0xDF6B6B5B, "type"}, {0xA4016384, "packageID"},
        {0x522C090E, "exclusive"}, {0x804C4199, "export"}, {0xCF97D2E5, "icon"},
        {0xFB0E9D1F, "date"}, {0x26C5EF60, "creationDate"}, {0xEE0FE53A, "value"},
        {0x186A5856, "container"}, {0x0CB17821, "lods"}, {0x0CBA931A, "bounds"},
        {0x8EF7A80A, "containers"}, {0xEAAF31A4, "decalContainers"}, {0x854F8C9B, "materials"},
        {0xD955D183, "surface"}, {0xA4512B1B, "meshes"}, {0x3F427CA3, "source"},
        {0x6A582B65, "package"}, {0xEB739B94, "directory"}, {0xCC42E703, "material"},
        {0x55E806B2, "diffuse"}, {0x1B80EC1E, "color"}, {0x78CA42FA, "sources"},
        {0x8D306823, "data"}, {0x89B43684, "tiling"}, {0xD72D5372, "value0"},
        {0xE5EF56EB, "value1"}, {0xF4B15A64, "value2"}
    };
    for (const auto& value : values) {
        g_hashes[value.first] = value.second;
    }
}

bool LoadHashes() {
    g_hashes.clear();
    AddBuiltInHashes();

    if (g_hashPath[0] == '\0') {
        g_hashesLoaded = true;
        return true;
    }

    std::ifstream file(g_hashPath);
    if (!file.is_open()) {
        g_hashesLoaded = true;
        g_status = "Hash CSV not found; using built-in objectcollection names only.";
        return false;
    }

    std::string line;
    std::getline(file, line);
    int loaded = 0;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string hashText;
        std::string value;
        if (!std::getline(ss, hashText, ',') || !std::getline(ss, value, ',')) {
            continue;
        }
        if (hashText.empty() || value.empty()) {
            continue;
        }
        uint32_t hashValue = 0;
        if (sscanf_s(hashText.c_str(), "%x", &hashValue) == 1) {
            g_hashes[hashValue] = value;
            ++loaded;
        }
    }

    g_hashesLoaded = true;
    LOG_INFO("[PakViewer] Loaded " << loaded << " hashes from " << g_hashPath);
    return true;
}

std::string ResolveHash(uint32_t hash) {
    auto it = g_hashes.find(hash);
    if (it != g_hashes.end()) {
        return it->second;
    }
    return "#" + Hex32(hash).substr(2);
}

class DeflateReader {
public:
    DeflateReader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size) {}

    bool ReadBits(int count, uint32_t& value) {
        value = 0;
        for (int i = 0; i < count; ++i) {
            if (m_byteOffset >= m_size) {
                return false;
            }
            value |= ((m_data[m_byteOffset] >> m_bitOffset) & 1u) << i;
            ++m_bitOffset;
            if (m_bitOffset == 8) {
                m_bitOffset = 0;
                ++m_byteOffset;
            }
        }
        return true;
    }

    void AlignByte() {
        if (m_bitOffset != 0) {
            m_bitOffset = 0;
            ++m_byteOffset;
        }
    }

    bool ReadByte(uint8_t& value) {
        AlignByte();
        if (m_byteOffset >= m_size) {
            return false;
        }
        value = m_data[m_byteOffset++];
        return true;
    }

private:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
    size_t m_byteOffset = 0;
    int m_bitOffset = 0;
};

bool BuildHuffmanTree(const std::vector<int>& lengths, HuffmanTree& tree, std::string& error) {
    tree.entries.clear();

    int maxBits = 0;
    for (int length : lengths) {
        maxBits = (std::max)(maxBits, length);
    }
    if (maxBits == 0) {
        return true;
    }

    std::vector<int> blCount(maxBits + 1, 0);
    for (int length : lengths) {
        if (length < 0 || length > maxBits) {
            error = "invalid huffman code length";
            return false;
        }
        if (length != 0) {
            ++blCount[length];
        }
    }

    std::vector<int> nextCode(maxBits + 1, 0);
    int code = 0;
    for (int bits = 1; bits <= maxBits; ++bits) {
        code = (code + blCount[bits - 1]) << 1;
        nextCode[bits] = code;
    }

    for (size_t symbol = 0; symbol < lengths.size(); ++symbol) {
        int length = lengths[symbol];
        if (length == 0) {
            continue;
        }

        HuffmanEntry entry;
        entry.symbol = static_cast<int>(symbol);
        entry.length = length;
        entry.code = ReverseBits(static_cast<uint32_t>(nextCode[length]++), length);
        tree.entries.push_back(entry);
    }

    return true;
}

bool DecodeSymbol(DeflateReader& reader, const HuffmanTree& tree, int& symbol, std::string& error) {
    uint32_t code = 0;
    for (int length = 1; length <= 15; ++length) {
        uint32_t bit = 0;
        if (!reader.ReadBits(1, bit)) {
            error = "unexpected end of deflate stream";
            return false;
        }
        code |= bit << (length - 1);

        for (const HuffmanEntry& entry : tree.entries) {
            if (entry.length == length && entry.code == code) {
                symbol = entry.symbol;
                return true;
            }
        }
    }

    error = "invalid huffman code";
    return false;
}

bool BuildFixedTrees(HuffmanTree& litLenTree, HuffmanTree& distTree, std::string& error) {
    std::vector<int> litLenLengths(288, 0);
    for (int i = 0; i <= 143; ++i) litLenLengths[i] = 8;
    for (int i = 144; i <= 255; ++i) litLenLengths[i] = 9;
    for (int i = 256; i <= 279; ++i) litLenLengths[i] = 7;
    for (int i = 280; i <= 287; ++i) litLenLengths[i] = 8;

    std::vector<int> distLengths(32, 5);
    return BuildHuffmanTree(litLenLengths, litLenTree, error)
        && BuildHuffmanTree(distLengths, distTree, error);
}

bool BuildDynamicTrees(DeflateReader& reader, HuffmanTree& litLenTree, HuffmanTree& distTree, std::string& error) {
    uint32_t hlit = 0;
    uint32_t hdist = 0;
    uint32_t hclen = 0;
    if (!reader.ReadBits(5, hlit) || !reader.ReadBits(5, hdist) || !reader.ReadBits(4, hclen)) {
        error = "truncated dynamic huffman header";
        return false;
    }
    hlit += 257;
    hdist += 1;
    hclen += 4;

    const int order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    std::vector<int> codeLengthLengths(19, 0);
    for (uint32_t i = 0; i < hclen; ++i) {
        uint32_t length = 0;
        if (!reader.ReadBits(3, length)) {
            error = "truncated code-length alphabet";
            return false;
        }
        codeLengthLengths[order[i]] = static_cast<int>(length);
    }

    HuffmanTree codeLengthTree;
    if (!BuildHuffmanTree(codeLengthLengths, codeLengthTree, error)) {
        return false;
    }

    std::vector<int> lengths;
    lengths.reserve(hlit + hdist);
    while (lengths.size() < hlit + hdist) {
        int symbol = 0;
        if (!DecodeSymbol(reader, codeLengthTree, symbol, error)) {
            return false;
        }

        if (symbol <= 15) {
            lengths.push_back(symbol);
        }
        else if (symbol == 16) {
            if (lengths.empty()) {
                error = "repeat code without previous length";
                return false;
            }
            uint32_t extra = 0;
            if (!reader.ReadBits(2, extra)) {
                error = "truncated repeat length";
                return false;
            }
            int repeat = static_cast<int>(extra) + 3;
            int previous = lengths.back();
            for (int i = 0; i < repeat && lengths.size() < hlit + hdist; ++i) {
                lengths.push_back(previous);
            }
        }
        else if (symbol == 17 || symbol == 18) {
            int bits = symbol == 17 ? 3 : 7;
            int base = symbol == 17 ? 3 : 11;
            uint32_t extra = 0;
            if (!reader.ReadBits(bits, extra)) {
                error = "truncated zero repeat length";
                return false;
            }
            int repeat = static_cast<int>(extra) + base;
            for (int i = 0; i < repeat && lengths.size() < hlit + hdist; ++i) {
                lengths.push_back(0);
            }
        }
        else {
            error = "invalid code-length symbol";
            return false;
        }
    }

    std::vector<int> litLenLengths(lengths.begin(), lengths.begin() + hlit);
    std::vector<int> distLengths(lengths.begin() + hlit, lengths.end());
    return BuildHuffmanTree(litLenLengths, litLenTree, error)
        && BuildHuffmanTree(distLengths, distTree, error);
}

bool DecodeCompressedBlock(DeflateReader& reader, const HuffmanTree& litLenTree, const HuffmanTree& distTree, std::vector<uint8_t>& out, std::string& error) {
    static const int lengthBase[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static const int lengthExtra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    static const int distBase[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
        6145, 8193, 12289, 16385, 24577
    };
    static const int distExtra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
        6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };

    for (;;) {
        int symbol = 0;
        if (!DecodeSymbol(reader, litLenTree, symbol, error)) {
            return false;
        }

        if (symbol < 256) {
            out.push_back(static_cast<uint8_t>(symbol));
        }
        else if (symbol == 256) {
            return true;
        }
        else if (symbol >= 257 && symbol <= 285) {
            int lengthIndex = symbol - 257;
            uint32_t extraLength = 0;
            if (lengthExtra[lengthIndex] != 0 && !reader.ReadBits(lengthExtra[lengthIndex], extraLength)) {
                error = "truncated length extra bits";
                return false;
            }
            int length = lengthBase[lengthIndex] + static_cast<int>(extraLength);

            int distSymbol = 0;
            if (!DecodeSymbol(reader, distTree, distSymbol, error)) {
                return false;
            }
            if (distSymbol < 0 || distSymbol >= 30) {
                error = "invalid distance symbol";
                return false;
            }

            uint32_t extraDistance = 0;
            if (distExtra[distSymbol] != 0 && !reader.ReadBits(distExtra[distSymbol], extraDistance)) {
                error = "truncated distance extra bits";
                return false;
            }
            int distance = distBase[distSymbol] + static_cast<int>(extraDistance);
            if (distance <= 0 || static_cast<size_t>(distance) > out.size()) {
                error = "invalid back-reference distance";
                return false;
            }

            for (int i = 0; i < length; ++i) {
                out.push_back(out[out.size() - distance]);
            }
        }
        else {
            error = "invalid literal/length symbol";
            return false;
        }
    }
}

bool InflateZlibPayload(const uint8_t* data, size_t size, uint32_t expectedSize, std::vector<uint8_t>& out, std::string& error) {
    out.clear();
    if (size < 6) {
        error = "zlib stream is too small";
        return false;
    }

    uint8_t cmf = data[0];
    uint8_t flg = data[1];
    if ((cmf & 0x0F) != 8) {
        error = "zlib stream is not deflate";
        return false;
    }
    if (((static_cast<int>(cmf) << 8) + flg) % 31 != 0) {
        error = "zlib header check failed";
        return false;
    }
    if (flg & 0x20) {
        error = "zlib preset dictionary is unsupported";
        return false;
    }

    DeflateReader reader(data + 2, size - 6);
    out.reserve(expectedSize);

    bool finalBlock = false;
    while (!finalBlock) {
        uint32_t finalBit = 0;
        uint32_t blockType = 0;
        if (!reader.ReadBits(1, finalBit) || !reader.ReadBits(2, blockType)) {
            error = "truncated deflate block header";
            return false;
        }
        finalBlock = finalBit != 0;

        if (blockType == 0) {
            reader.AlignByte();
            uint8_t len0 = 0, len1 = 0, nlen0 = 0, nlen1 = 0;
            if (!reader.ReadByte(len0) || !reader.ReadByte(len1) || !reader.ReadByte(nlen0) || !reader.ReadByte(nlen1)) {
                error = "truncated stored block header";
                return false;
            }
            uint16_t length = static_cast<uint16_t>(len0 | (len1 << 8));
            uint16_t nlength = static_cast<uint16_t>(nlen0 | (nlen1 << 8));
            if (static_cast<uint16_t>(length ^ 0xFFFF) != nlength) {
                error = "stored block length check failed";
                return false;
            }
            for (uint16_t i = 0; i < length; ++i) {
                uint8_t value = 0;
                if (!reader.ReadByte(value)) {
                    error = "truncated stored block data";
                    return false;
                }
                out.push_back(value);
            }
        }
        else if (blockType == 1 || blockType == 2) {
            HuffmanTree litLenTree;
            HuffmanTree distTree;
            bool ok = blockType == 1
                ? BuildFixedTrees(litLenTree, distTree, error)
                : BuildDynamicTrees(reader, litLenTree, distTree, error);
            if (!ok) {
                return false;
            }
            if (!DecodeCompressedBlock(reader, litLenTree, distTree, out, error)) {
                return false;
            }
        }
        else {
            error = "reserved deflate block type";
            return false;
        }
    }

    if (expectedSize != 0 && out.size() != expectedSize) {
        std::ostringstream oss;
        oss << "inflated " << out.size() << " bytes, expected " << expectedSize;
        error = oss.str();
        return false;
    }

    return true;
}

std::string AttrValue(const Attribute& attr);
std::string AttrByName(const Node& node, const char* name);

std::string NodeLabel(const Node& node) {
    std::string label = node.name;
    std::string nameAttr = AttrByName(node, "name");
    if (!nameAttr.empty()) {
        label += " name=\"" + nameAttr + "\"";
    }
    std::string idAttr = AttrByName(node, "ID");
    if (idAttr.empty()) {
        idAttr = AttrByName(node, "id");
    }
    if (!idAttr.empty()) {
        label += " ID=" + idAttr;
    }
    return label;
}

void RenderAttributesInline(const Node& node, const char* filter = "") {
    if (node.attrs.empty()) {
        ImGui::TextDisabled("No attributes");
        return;
    }

    std::string needle = filter && filter[0] != '\0' ? Lower(filter) : std::string();
    if (ImGui::BeginTable("NodeAttributes", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Attribute", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        for (const Attribute& attr : node.attrs) {
            std::string value = AttrValue(attr);
            if (!needle.empty()) {
                if (Lower(attr.key).find(needle) == std::string::npos
                    && Lower(value).find(needle) == std::string::npos) {
                    continue;
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(attr.key.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(value.c_str());
        }
        ImGui::EndTable();
    }
}

bool NodeSelfMatchesFilter(const Node& node, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }

    if (Lower(NodeLabel(node)).find(needle) != std::string::npos) {
        return true;
    }
    for (const Attribute& attr : node.attrs) {
        if (Lower(attr.key).find(needle) != std::string::npos
            || Lower(AttrValue(attr)).find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool CollectDecodedTreeFilterMatches(const Node& node, const std::string& needle) {
    bool matched = NodeSelfMatchesFilter(node, needle);
    for (const Node& child : node.children) {
        if (CollectDecodedTreeFilterMatches(child, needle)) {
            matched = true;
        }
    }
    if (matched) {
        g_decodedTreeFilterMatches.insert(&node);
    }
    return matched;
}

void RenderDecodedNode(const Node& node, const char* filter = "") {
    if (filter && filter[0] != '\0' && g_decodedTreeFilterMatches.find(&node) == g_decodedTreeFilterMatches.end()) {
        return;
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
    if (node.children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (filter && filter[0] != '\0') {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    std::string label = NodeLabel(node) + "##node" + std::to_string(reinterpret_cast<uintptr_t>(&node));
    bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemHovered() && !node.attrs.empty()) {
        ImGui::BeginTooltip();
        for (const Attribute& attr : node.attrs) {
            std::string value = AttrValue(attr);
            ImGui::Text("%s = %s", attr.key.c_str(), value.c_str());
        }
        ImGui::EndTooltip();
    }

    if (open) {
        if (!node.attrs.empty() && ImGui::TreeNodeEx("Attributes", ImGuiTreeNodeFlags_DefaultOpen)) {
            RenderAttributesInline(node, filter);
            ImGui::TreePop();
        }
        for (const Node& child : node.children) {
            RenderDecodedNode(child, filter);
        }
        ImGui::TreePop();
    }
}

bool EntryPayload(const PakEntry& entry, std::vector<uint8_t>& out, std::string& error) {
    if (entry.dataOffset > g_pakBytes.size() || entry.storedSize > g_pakBytes.size() - entry.dataOffset) {
        error = "entry points outside archive";
        return false;
    }

    if (entry.flags == 0x00) {
        out.assign(g_pakBytes.begin() + entry.dataOffset, g_pakBytes.begin() + entry.dataOffset + entry.storedSize);
        return true;
    }

    if (entry.flags == 0x01) {
        return InflateZlibPayload(
            g_pakBytes.data() + entry.dataOffset,
            entry.storedSize,
            entry.unpackedSize,
            out,
            error);
    }

    if (entry.flags & 0x10) {
        error = "protected/encrypted payload";
        return false;
    }

    error = "unsupported flags " + Hex32(entry.flags);
    return false;
}

bool ParseNameList(const PakEntry& nameEntry, std::vector<std::string>& names, std::string& error) {
    std::vector<uint8_t> raw;
    if (!EntryPayload(nameEntry, raw, error)) {
        return false;
    }
    if (raw.size() < 4) {
        error = "name table is too small";
        return false;
    }

    uint32_t count = ReadU32(raw, 0);
    size_t offset = 4;
    names.clear();
    for (uint32_t i = 0; i < count; ++i) {
        if (offset + 2 > raw.size()) {
            error = "truncated name table";
            return false;
        }
        uint16_t size = ReadU16(raw, offset);
        offset += 2;
        if (offset + size > raw.size()) {
            error = "truncated name string";
            return false;
        }
        names.emplace_back(reinterpret_cast<const char*>(raw.data() + offset), size);
        offset += size;
    }
    return true;
}

bool RebuildPakWithReplacement(int replacementIndex, const std::vector<uint8_t>& replacementPayload, std::vector<uint8_t>& rebuilt, std::string& error) {
    if (replacementIndex < 0 || replacementIndex >= static_cast<int>(g_entries.size())) {
        error = "invalid replacement entry";
        return false;
    }
    if (g_entries.empty()) {
        error = "no loaded pak";
        return false;
    }

    const uint32_t entryCount = static_cast<uint32_t>(g_entries.size());
    const uint32_t dataStart = static_cast<uint32_t>(kPakHeaderSize + entryCount * kPakEntrySize);
    std::vector<std::vector<uint8_t>> payloads;
    payloads.reserve(g_entries.size());

    for (const PakEntry& entry : g_entries) {
        if (entry.index == replacementIndex) {
            payloads.push_back(replacementPayload);
            continue;
        }

        if (entry.dataOffset > g_pakBytes.size() || entry.storedSize > g_pakBytes.size() - entry.dataOffset) {
            error = "entry points outside archive while rebuilding";
            return false;
        }
        payloads.emplace_back(g_pakBytes.begin() + entry.dataOffset, g_pakBytes.begin() + entry.dataOffset + entry.storedSize);
    }

    rebuilt.clear();
    rebuilt.reserve(dataStart + g_pakBytes.size());
    AppendU32(rebuilt, kPakMagic);
    AppendU32(rebuilt, dataStart);
    AppendU32(rebuilt, entryCount);

    uint32_t dataOffset = dataStart;
    for (size_t i = 0; i < g_entries.size(); ++i) {
        const PakEntry& entry = g_entries[i];
        const bool replaced = static_cast<int>(i) == replacementIndex;
        const uint32_t size = static_cast<uint32_t>(payloads[i].size());

        AppendU32(rebuilt, entry.pathHash);
        AppendU32(rebuilt, size);
        AppendU32(rebuilt, size);
        rebuilt.push_back(replaced ? 0 : entry.flags);
        AppendU32(rebuilt, dataOffset);
        dataOffset += size;
    }

    for (const std::vector<uint8_t>& payload : payloads) {
        rebuilt.insert(rebuilt.end(), payload.begin(), payload.end());
    }
    return true;
}

bool SavePatchedPakInPlace(const std::vector<uint8_t>& rebuilt, std::string& error) {
    if (g_loadedPak.empty()) {
        error = "no loaded pak path";
        return false;
    }

    std::string backupPath = g_loadedPak + ".tfpayload.bak";
    if (!FileExists(backupPath) && !WriteFileBytes(backupPath, g_pakBytes)) {
        error = "failed to write backup";
        return false;
    }

    if (!WriteFileBytes(g_loadedPak, rebuilt)) {
        error = "failed to write patched pak; file may be locked";
        return false;
    }

    g_pakBytes = rebuilt;
    return true;
}

bool WritePatchedPakInPlaceStreamed(int replacementIndex, const std::vector<uint8_t>& replacementPayload, std::string& error) {
    if (replacementIndex < 0 || replacementIndex >= static_cast<int>(g_entries.size())) {
        error = "invalid replacement entry";
        return false;
    }
    if (g_loadedPak.empty() || g_entries.empty()) {
        error = "no loaded pak";
        return false;
    }
    if (replacementPayload.size() > 0xFFFFFFFFu) {
        error = "replacement payload is too large";
        return false;
    }

    uint32_t entryCount = static_cast<uint32_t>(g_entries.size());
    uint32_t dataStart = static_cast<uint32_t>(kPakHeaderSize + entryCount * kPakEntrySize);
    uint32_t dataOffset = dataStart;
    uint32_t maxOriginalEnd = 0;

    std::vector<uint32_t> storedSizes(g_entries.size(), 0);
    std::vector<uint32_t> unpackedSizes(g_entries.size(), 0);
    std::vector<uint8_t> flags(g_entries.size(), 0);
    std::vector<uint32_t> offsets(g_entries.size(), 0);

    for (size_t i = 0; i < g_entries.size(); ++i) {
        const PakEntry& entry = g_entries[i];
        if (entry.dataOffset > g_pakBytes.size() || entry.storedSize > g_pakBytes.size() - entry.dataOffset) {
            error = "entry points outside archive while writing patched pak";
            return false;
        }

        uint32_t payloadSize = static_cast<int>(i) == replacementIndex
            ? static_cast<uint32_t>(replacementPayload.size())
            : entry.storedSize;
        storedSizes[i] = payloadSize;
        unpackedSizes[i] = static_cast<int>(i) == replacementIndex ? payloadSize : entry.unpackedSize;
        flags[i] = static_cast<int>(i) == replacementIndex ? 0 : entry.flags;
        offsets[i] = dataOffset;

        if (payloadSize > 0xFFFFFFFFu - dataOffset) {
            error = "patched pak would exceed 4GB offset range";
            return false;
        }
        dataOffset += payloadSize;

        uint32_t originalEnd = entry.dataOffset + entry.storedSize;
        if (originalEnd > maxOriginalEnd) {
            maxOriginalEnd = originalEnd;
        }
    }

    std::string backupPath = g_loadedPak + ".tfpayload.bak";
    if (!FileExists(backupPath) && !CopyFileA(g_loadedPak.c_str(), backupPath.c_str(), FALSE)) {
        error = "failed to create backup";
        return false;
    }

    std::string tempPath = g_loadedPak + ".tfpayload.tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            error = "failed to open temp pak for writing";
            return false;
        }

        auto writeByte = [&file](uint8_t value) {
            file.put(static_cast<char>(value));
        };
        auto writeU32 = [&file](uint32_t value) {
            char bytes[4] = {
                static_cast<char>(value & 0xFF),
                static_cast<char>((value >> 8) & 0xFF),
                static_cast<char>((value >> 16) & 0xFF),
                static_cast<char>((value >> 24) & 0xFF)
            };
            file.write(bytes, sizeof(bytes));
        };

        writeU32(kPakMagic);
        writeU32(dataStart);
        writeU32(entryCount);

        for (size_t i = 0; i < g_entries.size(); ++i) {
            writeU32(g_entries[i].pathHash);
            writeU32(storedSizes[i]);
            writeU32(unpackedSizes[i]);
            writeByte(flags[i]);
            writeU32(offsets[i]);
        }

        for (size_t i = 0; i < g_entries.size(); ++i) {
            if (static_cast<int>(i) == replacementIndex) {
                if (!replacementPayload.empty()) {
                    file.write(reinterpret_cast<const char*>(replacementPayload.data()), static_cast<std::streamsize>(replacementPayload.size()));
                }
            }
            else if (g_entries[i].storedSize != 0) {
                file.write(
                    reinterpret_cast<const char*>(g_pakBytes.data() + g_entries[i].dataOffset),
                    static_cast<std::streamsize>(g_entries[i].storedSize));
            }
        }

        if (maxOriginalEnd < g_pakBytes.size()) {
            file.write(
                reinterpret_cast<const char*>(g_pakBytes.data() + maxOriginalEnd),
                static_cast<std::streamsize>(g_pakBytes.size() - maxOriginalEnd));
        }

        if (!file.good()) {
            error = "failed while writing temp pak";
            return false;
        }
    }

    if (!MoveFileExA(tempPath.c_str(), g_loadedPak.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tempPath.c_str());
        error = "failed to replace pak; file may be locked";
        return false;
    }

    if (!ReadFileBytes(g_loadedPak, g_pakBytes)) {
        error = "patched pak written but reload into viewer failed";
        return false;
    }
    return true;
}

DWORD CallVoidFunctionWithSeh(void(__cdecl* func)()) {
    DWORD exceptionCode = 0;
    __try {
        func();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        exceptionCode = GetExceptionCode();
    }
    return exceptionCode;
}

DWORD CallThiscallVoidFunctionWithSeh(void(__thiscall* func)(void*), void* thisPtr) {
    DWORD exceptionCode = 0;
    __try {
        func(thisPtr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        exceptionCode = GetExceptionCode();
    }
    return exceptionCode;
}

DWORD CallPackageMountWithSeh(PackageMount64Func func, void* packageManager, int pathArg, int packageArg, int flags, int* result) {
    DWORD exceptionCode = 0;
    __try {
        *result = func(packageManager, pathArg, packageArg, flags);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        exceptionCode = GetExceptionCode();
    }
    return exceptionCode;
}

DWORD CallPackageUnmountWithSeh(PackageUnmountByNameFunc func, void* packageManager, int packageArg, int* result) {
    DWORD exceptionCode = 0;
    __try {
        *result = func(packageManager, packageArg);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        exceptionCode = GetExceptionCode();
    }
    return exceptionCode;
}

bool TryReloadObjectCollectionXml(std::string& error) {
    if (BaseAddress::IsSteamVersion()) {
        error = "runtime reload call is only mapped for Uplay in this build";
        return false;
    }

    uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (baseAddress == 0 || kLoadObjectCollectionXmlRvaUplay == 0) {
        error = "game base address unavailable";
        return false;
    }

    void** ppGameManager = reinterpret_cast<void**>(baseAddress + kGameManagerPtrRvaUplay);
    if (IsBadReadPtr(ppGameManager, sizeof(void*)) || *ppGameManager == nullptr) {
        error = "game manager pointer unavailable";
        return false;
    }

    void* gameManager = *ppGameManager;
    if (IsBadReadPtr(gameManager, 0x260)) {
        error = "game manager is unreadable";
        return false;
    }

    using LoadObjectCollectionXmlFunc = void(__thiscall*)(void* thisPtr);
    LoadObjectCollectionXmlFunc reload = reinterpret_cast<LoadObjectCollectionXmlFunc>(baseAddress + kLoadObjectCollectionXmlRvaUplay);
    DWORD exceptionCode = CallThiscallVoidFunctionWithSeh(reload, gameManager);

    if (exceptionCode != 0) {
        std::ostringstream oss;
        oss << "LoadObjectCollectionXML thiscall exception 0x" << std::hex << exceptionCode;
        error = oss.str();
        return false;
    }
    return true;
}

bool TryRemountLoadedPak(std::string& error) {
    error.clear();

    if (BaseAddress::IsSteamVersion()) {
        error = "package remount test is only mapped for Uplay in this build";
        return false;
    }
    if (IsBaseDataPakPath(ResolveRemountPakPath())) {
        error = "base data.pak remount is blocked; it is a startup archive and forcing it at runtime can freeze the game";
        return false;
    }
    std::vector<RuntimeMountArgs> candidates;
    if (!DeriveRuntimeMountArgCandidatesForLoadedPak(candidates, error)) {
        return false;
    }
    if (candidates.empty()) {
        error = "no package remount candidates were derived";
        return false;
    }

    uintptr_t baseAddress = g_baseAddress != 0 ? g_baseAddress : reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (baseAddress == 0 || kPackageManagerGlobalRvaUplay == 0) {
        error = "package manager base address is unavailable";
        return false;
    }

    void** packageManagerGlobal = reinterpret_cast<void**>(baseAddress + kPackageManagerGlobalRvaUplay);
    if (IsBadReadPtr(packageManagerGlobal, sizeof(void*)) || *packageManagerGlobal == nullptr) {
        error = "package manager is not ready";
        return false;
    }

    void* packageManager = *packageManagerGlobal;
    if (IsBadReadPtr(packageManager, sizeof(void*))) {
        error = "package manager pointer is invalid";
        return false;
    }

    PackageMount64Func mountFunc = g_originalPackageMount64;
    void* mountTarget = g_packageMount64Target;
    if (mountFunc == nullptr) {
        void** vtable = *reinterpret_cast<void***>(packageManager);
        if (vtable == nullptr || IsBadReadPtr(vtable, kPackageMount64VtableOffset + sizeof(void*))) {
            error = "package manager vtable is invalid";
            return false;
        }
        mountTarget = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(vtable) + kPackageMount64VtableOffset);
        mountFunc = reinterpret_cast<PackageMount64Func>(mountTarget);
    }

    if (mountFunc == nullptr) {
        error = "package mount/load function is unavailable";
        return false;
    }
    uintptr_t mountTargetAddress = reinterpret_cast<uintptr_t>(mountTarget != nullptr ? mountTarget : reinterpret_cast<void*>(mountFunc));
    LOG_INFO("[PakViewer] Remount using packageManager=0x" << std::hex
        << reinterpret_cast<uintptr_t>(packageManager)
        << " vtable=0x" << reinterpret_cast<uintptr_t>(*reinterpret_cast<void***>(packageManager))
        << " mountTarget=0x" << mountTargetAddress
        << " mountRva=0x" << (mountTargetAddress - baseAddress)
        << " callable=0x" << reinterpret_cast<uintptr_t>(reinterpret_cast<void*>(mountFunc)));

    std::ostringstream failures;
    for (const RuntimeMountArgs& args : candidates) {
        GameStringArg pathArg = MakeGameStringArg(args.path);
        GameStringArg packageArg = MakeGameStringArg(args.packageName);
        LOG_INFO("[PakViewer] Remount test candidate=" << args.label
            << " path=" << args.path
            << " package=" << args.packageName
            << " flags=0x" << std::hex << args.flags);

        int result = 0;
        DWORD exceptionCode = CallPackageMountWithSeh(mountFunc,
            packageManager,
            reinterpret_cast<int>(&pathArg),
            reinterpret_cast<int>(&packageArg),
            args.flags,
            &result);
        if (exceptionCode != 0) {
            LOG_WARNING("[PakViewer] Remount test exception candidate=" << args.label
                << " code=0x" << std::hex << exceptionCode);
            failures << args.label << ": exception 0x" << std::hex << exceptionCode << "; ";
            continue;
        }

        LOG_INFO("[PakViewer] Remount test returned " << std::dec << result
            << " candidate=" << args.label
            << " path=" << args.path
            << " package=" << args.packageName);
        if (result != 0) {
            return true;
        }

        failures << args.label << ": returned 0; ";
    }

    error = "all package mount/load candidates failed: " + failures.str();
    return false;
}

bool TryUnmountAndRemountLoadedPak(std::string& error) {
    error.clear();

    if (BaseAddress::IsSteamVersion()) {
        error = "package unmount/remount test is only mapped for Uplay in this build";
        return false;
    }
    if (IsBaseDataPakPath(ResolveRemountPakPath())) {
        error = "base data.pak unmount/remount is blocked; it is a startup archive and forcing it at runtime can freeze the game";
        return false;
    }

    std::vector<RuntimeMountArgs> candidates;
    if (!DeriveRuntimeMountArgCandidatesForLoadedPak(candidates, error)) {
        return false;
    }
    if (candidates.empty()) {
        error = "no package remount candidates were derived";
        return false;
    }

    uintptr_t baseAddress = g_baseAddress != 0 ? g_baseAddress : reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (baseAddress == 0 || kPackageManagerGlobalRvaUplay == 0 || kPackageUnmountByNameRvaUplay == 0) {
        error = "package manager base address is unavailable";
        return false;
    }

    void** packageManagerGlobal = reinterpret_cast<void**>(baseAddress + kPackageManagerGlobalRvaUplay);
    if (IsBadReadPtr(packageManagerGlobal, sizeof(void*)) || *packageManagerGlobal == nullptr) {
        error = "package manager is not ready";
        return false;
    }

    void* packageManager = *packageManagerGlobal;
    if (IsBadReadPtr(packageManager, sizeof(void*))) {
        error = "package manager pointer is invalid";
        return false;
    }

    PackageMount64Func mountFunc = g_originalPackageMount64;
    void* mountTarget = g_packageMount64Target;
    if (mountFunc == nullptr) {
        void** vtable = *reinterpret_cast<void***>(packageManager);
        if (vtable == nullptr || IsBadReadPtr(vtable, kPackageMount64VtableOffset + sizeof(void*))) {
            error = "package manager vtable is invalid";
            return false;
        }
        mountTarget = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(vtable) + kPackageMount64VtableOffset);
        mountFunc = reinterpret_cast<PackageMount64Func>(mountTarget);
    }
    if (mountFunc == nullptr) {
        error = "package mount/load function is unavailable";
        return false;
    }
    uintptr_t mountTargetAddress = reinterpret_cast<uintptr_t>(mountTarget != nullptr ? mountTarget : reinterpret_cast<void*>(mountFunc));
    LOG_INFO("[PakViewer] Force reload using packageManager=0x" << std::hex
        << reinterpret_cast<uintptr_t>(packageManager)
        << " mountTarget=0x" << mountTargetAddress
        << " mountRva=0x" << (mountTargetAddress - baseAddress)
        << " callable=0x" << reinterpret_cast<uintptr_t>(reinterpret_cast<void*>(mountFunc)));

    PackageUnmountByNameFunc unmountFunc = reinterpret_cast<PackageUnmountByNameFunc>(baseAddress + kPackageUnmountByNameRvaUplay);
    const RuntimeMountArgs& args = candidates.front();
    std::string unmountName = args.packageName;
    if (!unmountName.empty() && unmountName.back() != ':') {
        unmountName.push_back(':');
    }

    GameStringArg unmountArg = MakeGameStringArg(unmountName);
    LOG_WARNING("[PakViewer] Force package reload unmount package=" << unmountName
        << " then mount path=" << args.path
        << " package=" << args.packageName
        << " flags=0x" << std::hex << args.flags);

    int unmountResult = 0;
    DWORD exceptionCode = CallPackageUnmountWithSeh(unmountFunc,
        packageManager,
        reinterpret_cast<int>(&unmountArg),
        &unmountResult);
    if (exceptionCode != 0) {
        std::ostringstream oss;
        oss << "package unmount exception 0x" << std::hex << exceptionCode;
        error = oss.str();
        return false;
    }
    LOG_INFO("[PakViewer] Force package reload unmount returned " << std::dec << unmountResult
        << " package=" << unmountName);
    if (unmountResult == 0) {
        LOG_WARNING("[PakViewer] Force package reload unmount returned 0; package may not have been mounted, continuing to mount");
    }

    GameStringArg pathArg = MakeGameStringArg(args.path);
    GameStringArg packageArg = MakeGameStringArg(args.packageName);
    int mountResult = 0;
    exceptionCode = CallPackageMountWithSeh(mountFunc,
        packageManager,
        reinterpret_cast<int>(&pathArg),
        reinterpret_cast<int>(&packageArg),
        args.flags,
        &mountResult);
    if (exceptionCode != 0) {
        std::ostringstream oss;
        oss << "package remount exception 0x" << std::hex << exceptionCode;
        error = oss.str();
        return false;
    }
    LOG_INFO("[PakViewer] Force package reload mount returned " << std::dec << mountResult
        << " path=" << args.path
        << " package=" << args.packageName);
    if (mountResult == 0) {
        error = "package remount returned 0 after successful unmount";
        return false;
    }

    return true;
}

bool LoadPak() {
    if (IsBaseDataPakPath(g_pakPath) && !g_allowBasePakLoad) {
        g_status = "Base data.pak load blocked. Enable the checkbox first if you really want to load it in-game.";
        LOG_WARNING("[PakViewer] Refusing to load base data.pak in-game without explicit opt-in path=" << g_pakPath);
        return false;
    }

    g_entries.clear();
    g_objects.clear();
    g_treeRoot = PakTreeNode{};
    g_hasDecodedRoot = false;
    g_selectedObject = -1;
    g_selectedEntry = -1;
    g_loadedPak.clear();
    ClearRuntimeOverrideState();
    if (!g_hashesLoaded) {
        LoadHashes();
    }

    if (!ReadFileBytes(g_pakPath, g_pakBytes)) {
        g_status = "Failed to open pak file.";
        return false;
    }
    if (g_pakBytes.size() < kPakHeaderSize || ReadU32(g_pakBytes, 0) != kPakMagic) {
        g_status = "Not a Trials Fusion pak.";
        return false;
    }

    uint32_t dataStart = ReadU32(g_pakBytes, 4);
    uint32_t entryCount = ReadU32(g_pakBytes, 8);
    if (dataStart != kPakHeaderSize + entryCount * kPakEntrySize) {
        g_status = "Pak has an unexpected data-start offset.";
        return false;
    }
    if (g_pakBytes.size() < dataStart || entryCount == 0) {
        g_status = "Pak entry table is truncated.";
        return false;
    }

    g_entries.reserve(entryCount);
    size_t offset = kPakHeaderSize;
    for (uint32_t i = 0; i < entryCount; ++i) {
        PakEntry entry;
        entry.index = static_cast<int>(i);
        entry.pathHash = ReadU32(g_pakBytes, offset);
        entry.storedSize = ReadU32(g_pakBytes, offset + 4);
        entry.unpackedSize = ReadU32(g_pakBytes, offset + 8);
        entry.flags = g_pakBytes[offset + 12];
        entry.dataOffset = ReadU32(g_pakBytes, offset + 13);
        entry.name = ResolveHash(entry.pathHash);
        g_entries.push_back(entry);
        offset += kPakEntrySize;
    }

    std::string nameError;
    std::vector<std::string> names;
    if (ParseNameList(g_entries.back(), names, nameError)) {
        for (size_t i = 0; i < names.size() && i < g_entries.size(); ++i) {
            g_entries[i].name = names[i];
        }
    }
    else {
        LOG_WARNING("[PakViewer] Could not parse pak name table: " << nameError);
    }

    g_loadedPak = g_pakPath;
    BuildTree();
    std::ostringstream oss;
    oss << "Loaded " << g_entries.size() << " entries";
    if (!nameError.empty()) {
        oss << " (name table unavailable: " << nameError << ")";
    }
    g_status = oss.str();
    return true;
}

class PropReader {
public:
    explicit PropReader(const std::vector<uint8_t>& data) : m_data(data) {}

    bool ReadNode(Node& node, std::string& error) {
        node.start = m_offset;
        if (!ReadToken(true, node.name, node.nameHash, error)) {
            return false;
        }

        uint8_t hasAttr = 0;
        while (ReadU8(hasAttr, error) && hasAttr) {
            Attribute attr;
            if (!ReadToken(false, attr.key, attr.keyHash, error)) {
                return false;
            }
            uint8_t valueType = 0;
            uint16_t size = 0;
            if (!ReadU8(valueType, error) || !ReadU16Value(size, error)) {
                return false;
            }
            attr.valueType = valueType;
            if (m_offset + size > m_data.size()) {
                error = "attribute value overruns payload";
                return false;
            }
            attr.valueOffset = m_offset;
            attr.originalSize = size;
            attr.rawValue.assign(m_data.begin() + m_offset, m_data.begin() + m_offset + size);
            m_offset += size;
            node.attrs.push_back(attr);
        }
        if (!error.empty()) {
            return false;
        }

        uint8_t hasChild = 0;
        while (ReadU8(hasChild, error) && hasChild) {
            Node child;
            if (!ReadNode(child, error)) {
                return false;
            }
            node.children.push_back(child);
        }
        if (!error.empty()) {
            return false;
        }

        node.end = m_offset;
        return true;
    }

    size_t Offset() const { return m_offset; }

private:
    bool ReadU8(uint8_t& value, std::string& error) {
        if (m_offset >= m_data.size()) {
            error = "unexpected end of property tree";
            return false;
        }
        value = m_data[m_offset++];
        return true;
    }

    bool ReadU16Value(uint16_t& value, std::string& error) {
        if (m_offset + 2 > m_data.size()) {
            error = "unexpected end of property tree";
            return false;
        }
        value = ReadU16(m_data, m_offset);
        m_offset += 2;
        return true;
    }

    bool ReadU32Value(uint32_t& value, std::string& error) {
        if (m_offset + 4 > m_data.size()) {
            error = "unexpected end of property tree";
            return false;
        }
        value = ReadU32(m_data, m_offset);
        m_offset += 4;
        return true;
    }

    bool ReadToken(bool nodeName, std::string& text, uint32_t& hash, std::string& error) {
        uint8_t marker = 0;
        if (nodeName && !ReadU8(marker, error)) {
            return false;
        }

        uint8_t size = 0;
        if (!ReadU8(size, error)) {
            return false;
        }
        if (size == 0) {
            if (!ReadU32Value(hash, error)) {
                return false;
            }
            text = ResolveHash(hash);
            return true;
        }

        if (m_offset + size > m_data.size()) {
            error = "token overruns payload";
            return false;
        }
        text.assign(reinterpret_cast<const char*>(m_data.data() + m_offset), size);
        hash = 0;
        m_offset += size;
        return true;
    }

    const std::vector<uint8_t>& m_data;
    size_t m_offset = 0;
};

std::string AttrValue(const Attribute& attr) {
    if ((attr.valueType == 3 || attr.valueType == 4) && attr.rawValue.size() == 4 && attr.key == "color") {
        return Hex32(ReadU32(attr.rawValue, 0));
    }
    if ((attr.valueType == 3 || attr.valueType == 4) && attr.rawValue.size() == 4) {
        return std::to_string(ReadU32(attr.rawValue, 0));
    }
    if (attr.valueType == 5 && attr.rawValue.size() == 4) {
        float value = 0.0f;
        memcpy(&value, attr.rawValue.data(), sizeof(float));
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }
    if (attr.valueType == 6) {
        std::string value(reinterpret_cast<const char*>(attr.rawValue.data()), attr.rawValue.size());
        while (!value.empty() && value.back() == '\0') {
            value.pop_back();
        }
        return value;
    }
    if (attr.valueType == 2 && attr.rawValue.size() == 1) {
        return attr.rawValue[0] ? "1" : "0";
    }
    std::ostringstream oss;
    for (uint8_t byte : attr.rawValue) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

void WriteU32(std::vector<uint8_t>& data, uint32_t value) {
    data.resize(4);
    data[0] = static_cast<uint8_t>(value & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

float ReadFloat(const std::vector<uint8_t>& data) {
    float value = 0.0f;
    if (data.size() == 4) {
        memcpy(&value, data.data(), sizeof(float));
    }
    return value;
}

void WriteFloat(std::vector<uint8_t>& data, float value) {
    data.resize(4);
    memcpy(data.data(), &value, sizeof(float));
}

Attribute* FindAttribute(Node& node, const char* key) {
    for (Attribute& attr : node.attrs) {
        if (attr.key == key) {
            return &attr;
        }
    }
    return nullptr;
}

bool IsFloatAttribute(const Attribute* attr) {
    return attr && attr->valueType == 5 && attr->rawValue.size() == 4;
}

bool HasRgbFloatAttributes(Node& node, Attribute*& r, Attribute*& g, Attribute*& b) {
    r = FindAttribute(node, "value0");
    g = FindAttribute(node, "value1");
    b = FindAttribute(node, "value2");
    return IsFloatAttribute(r) && IsFloatAttribute(g) && IsFloatAttribute(b);
}

void WriteToken(std::vector<uint8_t>& out, const std::string& text, uint32_t hash) {
    if (hash == 0 && !text.empty() && text[0] != '#') {
        out.push_back(static_cast<uint8_t>((std::min)(text.size(), static_cast<size_t>(255))));
        out.insert(out.end(), text.begin(), text.begin() + out.back());
        return;
    }

    out.push_back(0);
    AppendU32(out, hash);
}

void SerializeNode(const Node& node, std::vector<uint8_t>& out) {
    out.push_back(1);
    WriteToken(out, node.name, node.nameHash);

    for (const Attribute& attr : node.attrs) {
        out.push_back(1);
        WriteToken(out, attr.key, attr.keyHash);
        out.push_back(static_cast<uint8_t>(attr.valueType));
        AppendU16(out, static_cast<uint16_t>(attr.rawValue.size()));
        out.insert(out.end(), attr.rawValue.begin(), attr.rawValue.end());
    }
    out.push_back(0);

    for (const Node& child : node.children) {
        out.push_back(1);
        SerializeNode(child, out);
    }
    out.push_back(0);
}

bool SerializeDecodedObjectCollection(std::vector<uint8_t>& out, std::string& error) {
    out.clear();
    if (!g_hasDecodedRoot) {
        error = "no decoded objectcollection";
        return false;
    }

    SerializeNode(g_decodedRoot, out);
    if (out.empty()) {
        error = "serialized payload is empty";
        return false;
    }
    return true;
}

bool ApplyNodeValuePatches(const Node& node, std::vector<uint8_t>& out, std::string& error) {
    for (const Attribute& attr : node.attrs) {
        if (attr.rawValue.size() != attr.originalSize) {
            error = "attribute '" + attr.key + "' changed size from "
                + std::to_string(attr.originalSize) + " to " + std::to_string(attr.rawValue.size());
            return false;
        }
        if (attr.valueOffset > out.size() || attr.rawValue.size() > out.size() - attr.valueOffset) {
            error = "attribute '" + attr.key + "' patch offset is outside original payload";
            return false;
        }
        memcpy(out.data() + attr.valueOffset, attr.rawValue.data(), attr.rawValue.size());
    }

    for (const Node& child : node.children) {
        if (!ApplyNodeValuePatches(child, out, error)) {
            return false;
        }
    }
    return true;
}

bool BuildPatchedObjectCollectionPayload(std::vector<uint8_t>& out, std::string& error) {
    out.clear();
    if (!g_hasDecodedRoot) {
        error = "no decoded objectcollection";
        return false;
    }
    if (g_originalObjectCollectionPayload.empty()) {
        error = "original objectcollection payload is unavailable";
        return false;
    }

    out = g_originalObjectCollectionPayload;
    return ApplyNodeValuePatches(g_decodedRoot, out, error);
}

std::string AttrByName(const Node& node, const char* name) {
    for (const Attribute& attr : node.attrs) {
        if (attr.key == name) {
            return AttrValue(attr);
        }
    }
    return std::string();
}

const Node* FirstAssetChild(const Node& node) {
    for (const Node& child : node.children) {
        if (child.name != "creationDate") {
            return &child;
        }
    }
    return nullptr;
}

std::string JoinCategoryPath(const std::vector<std::string>& categories) {
    std::string value;
    for (const std::string& category : categories) {
        if (category.empty()) {
            continue;
        }
        if (!value.empty()) {
            value += "/";
        }
        value += category;
    }
    return value;
}

void CollectCatalogObjects(Node& node, const Node* parent, std::vector<std::string>& categories, std::vector<ObjectRow>& rows, std::string& lastFilename) {
    bool pushedCategory = false;
    if (node.name == "category") {
        categories.push_back(AttrByName(node, "name"));
        pushedCategory = true;
    }

    if (node.name == "object" && parent && parent->name == "objects") {
        ObjectRow row;
        row.index = static_cast<int>(rows.size());
        row.offset = node.start;
        row.size = node.end > node.start ? node.end - node.start : 0;
        row.node = &node;
        row.name = AttrByName(node, "name");
        row.id = AttrByName(node, "ID");
        if (row.id.empty()) {
            row.id = AttrByName(node, "id");
        }
        row.type = AttrByName(node, "type");
        row.packageId = AttrByName(node, "packageID");
        row.exclusive = AttrByName(node, "exclusive");
        row.categoryPath = JoinCategoryPath(categories);
        row.filename = AttrByName(node, "filename");
        row.originalFilename = AttrByName(node, "originalFilename");
        if (!row.filename.empty()) {
            lastFilename = row.filename;
        }
        else {
            row.filename = lastFilename;
        }

        const Node* asset = FirstAssetChild(node);
        if (asset) {
            row.kind = asset->name;
            row.exportValue = AttrByName(*asset, "export");
        }
        rows.push_back(row);
    }

    for (Node& child : node.children) {
        CollectCatalogObjects(child, &node, categories, rows, lastFilename);
    }

    if (pushedCategory) {
        categories.pop_back();
    }
}

void ParseSelectedObjectCollection() {
    g_objects.clear();
    if (g_selectedEntry < 0 || g_selectedEntry >= static_cast<int>(g_entries.size())) {
        g_status = "No entry selected.";
        return;
    }

    const PakEntry& entry = g_entries[g_selectedEntry];
    std::vector<uint8_t> payload;
    std::string error;
    if (!EntryPayload(entry, payload, error)) {
        g_status = "Cannot decode " + entry.name + ": " + error + ".";
        return;
    }
    g_originalObjectCollectionPayload = payload;
    g_serializedObjectCollectionPayload.clear();
    g_decodedEntryIndex = g_selectedEntry;

    PropReader reader(payload);
    Node root;
    if (!reader.ReadNode(root, error)) {
        g_status = "Objectcollection parse failed: " + error + ".";
        return;
    }
    if (reader.Offset() != payload.size()) {
        std::ostringstream oss;
        oss << "Objectcollection parse stopped at 0x" << std::hex << reader.Offset()
            << " of 0x" << payload.size() << ".";
        g_status = oss.str();
        return;
    }

    g_decodedRoot = root;
    g_hasDecodedRoot = true;
    g_selectedObject = -1;
    g_hasPendingObjectEdit = false;
    g_editGeneration = 0;
    g_lastEditSummary.clear();
    g_decodedTreeFilterDraft[0] = '\0';
    g_decodedTreeFilter[0] = '\0';
    g_showFullDecodedTree = false;
    g_decodedTreeFilterMatches.clear();
    g_stringEditBuffers.clear();
    g_serializedObjectCollectionPayload.clear();
    std::string lastFilename;
    std::vector<std::string> categories;
    CollectCatalogObjects(g_decodedRoot, nullptr, categories, g_objects, lastFilename);
    std::ostringstream oss;
    oss << "Decoded " << g_objects.size() << " object rows from " << entry.name << ".";
    g_status = oss.str();
}

bool EntryMatchesFilter(const PakEntry& entry) {
    if (g_filter[0] == '\0') {
        return true;
    }

    std::string needle = Lower(g_filter);
    return Lower(entry.name).find(needle) != std::string::npos
        || Lower(Hex32(entry.pathHash)).find(needle) != std::string::npos;
}

bool TreeMatchesFilter(const PakTreeNode& node) {
    if (g_filter[0] == '\0') {
        return true;
    }

    std::string needle = Lower(g_filter);
    if (Lower(node.fullPath).find(needle) != std::string::npos) {
        return true;
    }

    for (int entryIndex : node.entries) {
        if (entryIndex >= 0 && entryIndex < static_cast<int>(g_entries.size()) && EntryMatchesFilter(g_entries[entryIndex])) {
            return true;
        }
    }
    for (const PakTreeNode& child : node.children) {
        if (TreeMatchesFilter(child)) {
            return true;
        }
    }
    return false;
}

void SelectEntry(int entryIndex) {
    if (entryIndex < 0 || entryIndex >= static_cast<int>(g_entries.size())) {
        return;
    }
    g_selectedEntry = entryIndex;
    g_objects.clear();
    g_hasDecodedRoot = false;
    g_selectedObject = -1;
}

void RenderEntryRow(const PakEntry& entry) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    bool selected = g_selectedEntry == entry.index;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    std::string path = NormalizePakPath(entry.name);
    std::vector<std::string> parts = SplitPath(path);
    std::string fileName = parts.empty() ? entry.name : parts.back();
    std::string label = fileName + "##entry" + std::to_string(entry.index);
    ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked()) {
        SelectEntry(entry.index);
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%d", entry.index);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(Hex32(entry.pathHash).c_str());
    ImGui::TableSetColumnIndex(3);
    ImGui::Text("%u", entry.storedSize);
    ImGui::TableSetColumnIndex(4);
    ImGui::Text("%u", entry.unpackedSize);
    ImGui::TableSetColumnIndex(5);
    ImGui::Text("0x%02X", entry.flags);
}

void RenderTreeNode(const PakTreeNode& node) {
    if (!TreeMatchesFilter(node)) {
        return;
    }

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    int count = TreeEntryCount(node);
    std::string label = node.name + " (" + std::to_string(count) + ")##dir" + node.fullPath;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
    if (g_filter[0] != '\0') {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("dir");

    if (open) {
        for (const PakTreeNode& child : node.children) {
            RenderTreeNode(child);
        }
        for (int entryIndex : node.entries) {
            if (entryIndex >= 0 && entryIndex < static_cast<int>(g_entries.size())) {
                const PakEntry& entry = g_entries[entryIndex];
                if (EntryMatchesFilter(entry)) {
                    RenderEntryRow(entry);
                }
            }
        }
        ImGui::TreePop();
    }
}

void RenderEntriesTree() {
    if (!ImGui::BeginTable("PakEntriesTree", 6, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 320))) {
        return;
    }

    ImGui::TableSetupColumn("Path");
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 46.0f);
    ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_WidthFixed, 96.0f);
    ImGui::TableSetupColumn("Stored", ImGuiTableColumnFlags_WidthFixed, 74.0f);
    ImGui::TableSetupColumn("Unpacked", ImGuiTableColumnFlags_WidthFixed, 74.0f);
    ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableHeadersRow();

    for (const PakTreeNode& child : g_treeRoot.children) {
        RenderTreeNode(child);
    }
    for (int entryIndex : g_treeRoot.entries) {
        if (entryIndex >= 0 && entryIndex < static_cast<int>(g_entries.size())) {
            const PakEntry& entry = g_entries[entryIndex];
            if (EntryMatchesFilter(entry)) {
                RenderEntryRow(entry);
            }
        }
    }

    ImGui::EndTable();
}

bool ObjectRowMatchesFilter(const ObjectRow& row) {
    if (g_objectFilter[0] == '\0') {
        return true;
    }

    std::string needle = Lower(g_objectFilter);
    std::string devFile = StripVirtualPrefix(row.filename);
    std::string sourceFile = StripVirtualPrefix(row.originalFilename);
    return Lower(row.name).find(needle) != std::string::npos
        || Lower(row.categoryPath).find(needle) != std::string::npos
        || Lower(row.kind).find(needle) != std::string::npos
        || Lower(row.id).find(needle) != std::string::npos
        || Lower(row.type).find(needle) != std::string::npos
        || Lower(row.packageId).find(needle) != std::string::npos
        || Lower(row.exclusive).find(needle) != std::string::npos
        || Lower(row.exportValue).find(needle) != std::string::npos
        || Lower(devFile).find(needle) != std::string::npos
        || Lower(sourceFile).find(needle) != std::string::npos;
}

void RenderObjectsTable() {
    if (g_objects.empty()) {
        return;
    }

    ImGui::SeparatorText("Decoded Objects");
    ImGui::InputText("Object Filter", g_objectFilter, sizeof(g_objectFilter));
    if (!ImGui::BeginTable("ObjectRows", 9, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 260))) {
        return;
    }

    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Category");
    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 82.0f);
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Package", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Export", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("Dev File");
    ImGui::TableHeadersRow();

    for (const ObjectRow& row : g_objects) {
        if (!ObjectRowMatchesFilter(row)) {
            continue;
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool selected = g_selectedObject == row.index;
        std::string label = std::to_string(row.index) + "##object" + std::to_string(row.index);
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
            g_selectedObject = row.index;
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(row.name.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(row.categoryPath.c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(row.kind.c_str());
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(row.id.c_str());
        ImGui::TableSetColumnIndex(5);
        ImGui::TextUnformatted(row.type.c_str());
        ImGui::TableSetColumnIndex(6);
        ImGui::TextUnformatted(row.packageId.c_str());
        ImGui::TableSetColumnIndex(7);
        ImGui::TextUnformatted(row.exportValue.c_str());
        ImGui::TableSetColumnIndex(8);
        std::string devFile = StripVirtualPrefix(row.filename);
        ImGui::TextUnformatted(devFile.c_str());
    }

    ImGui::EndTable();
}

void MarkObjectEditStaged(const Attribute& attr) {
    g_hasPendingObjectEdit = true;
    ++g_editGeneration;
    g_lastEditSummary = attr.key + " = " + AttrValue(attr);
    g_status = "Edited " + g_lastEditSummary + ".";
}

void SetSubmitStatus(const std::string& status, bool error = false) {
    g_lastSubmitStatus = status;
    g_status = status;
    if (error) {
        LOG_WARNING("[PakViewer] " << status);
    }
    else {
        LOG_INFO("[PakViewer] " << status);
    }
}

void RenderSingleAttributeEditor(Attribute& attr, const std::string& id) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(attr.key.c_str());
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%d", attr.valueType);
    ImGui::TableSetColumnIndex(2);

    if ((attr.valueType == 3 || attr.valueType == 4) && attr.rawValue.size() == 4) {
        uint32_t value = ReadU32(attr.rawValue, 0);
        std::string label = "##u32" + id;
        if (ImGui::InputScalar(label.c_str(), ImGuiDataType_U32, &value, nullptr, nullptr, attr.key == "color" ? "%08X" : "%u",
                attr.key == "color" ? ImGuiInputTextFlags_CharsHexadecimal : 0)) {
            WriteU32(attr.rawValue, value);
            MarkObjectEditStaged(attr);
        }
        return;
    }

    if (attr.valueType == 5 && attr.rawValue.size() == 4) {
        float value = ReadFloat(attr.rawValue);
        std::string label = "##float" + id;
        if (ImGui::InputFloat(label.c_str(), &value, 0.0f, 0.0f, "%.6f")) {
            WriteFloat(attr.rawValue, value);
            MarkObjectEditStaged(attr);
        }
        return;
    }

    if (attr.valueType == 6) {
        std::string& buffer = g_stringEditBuffers[&attr];
        if (buffer.empty()) {
            buffer.assign(reinterpret_cast<const char*>(attr.rawValue.data()), attr.rawValue.size());
            while (!buffer.empty() && buffer.back() == '\0') {
                buffer.pop_back();
            }
        }
        std::string label = "##str" + id;
        char editBuffer[512] = {};
        strncpy_s(editBuffer, buffer.c_str(), _TRUNCATE);
        if (ImGui::InputText(label.c_str(), editBuffer, sizeof(editBuffer))) {
            buffer = editBuffer;
            size_t targetSize = attr.originalSize != 0 ? attr.originalSize : attr.rawValue.size();
            if (targetSize != 0 && buffer.size() < targetSize) {
                attr.rawValue.assign(targetSize, 0);
                memcpy(attr.rawValue.data(), buffer.data(), buffer.size());
            }
            else {
                attr.rawValue.assign(buffer.begin(), buffer.end());
            }
            MarkObjectEditStaged(attr);
        }
        return;
    }

    ImGui::TextUnformatted(AttrValue(attr).c_str());
}

void RenderNodeAttributeEditors(Node& node, int& nodeIndex) {
    int thisNodeIndex = nodeIndex++;
    if (!node.attrs.empty()) {
        std::string label = NodeLabel(node) + "##editnode" + std::to_string(thisNodeIndex);
        if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            Attribute* r = nullptr;
            Attribute* g = nullptr;
            Attribute* b = nullptr;
            if (HasRgbFloatAttributes(node, r, g, b)) {
                float rgb[3] = { ReadFloat(r->rawValue), ReadFloat(g->rawValue), ReadFloat(b->rawValue) };
                std::string colorLabel = "RGB value0/value1/value2##rgb" + std::to_string(thisNodeIndex);
                if (ImGui::ColorEdit3(colorLabel.c_str(), rgb, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB)) {
                    WriteFloat(r->rawValue, rgb[0]);
                    WriteFloat(g->rawValue, rgb[1]);
                    WriteFloat(b->rawValue, rgb[2]);
                    MarkObjectEditStaged(*r);
                    g_lastEditSummary = NodeLabel(node) + " RGB = "
                        + AttrValue(*r) + ", " + AttrValue(*g) + ", " + AttrValue(*b);
                    g_status = "Edited " + g_lastEditSummary + ".";
                }
            }

            if (ImGui::BeginTable(("AttrEditors" + std::to_string(thisNodeIndex)).c_str(), 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Attribute", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 44.0f);
                ImGui::TableSetupColumn("Value");
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < node.attrs.size(); ++i) {
                    RenderSingleAttributeEditor(node.attrs[i], std::to_string(thisNodeIndex) + "_" + std::to_string(i));
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
    }

    for (Node& child : node.children) {
        RenderNodeAttributeEditors(child, nodeIndex);
    }
}

void RenderPackageRemountControls() {
    ImGui::InputText("Runtime Pak", g_runtimePakPath, sizeof(g_runtimePakPath));
    if (!g_loadedPak.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Use Loaded Pak")) {
            strncpy_s(g_runtimePakPath, g_loadedPak.c_str(), _TRUNCATE);
        }
    }
    ImGui::Checkbox("Reload objectcollection after force remount", &g_reloadObjectCollectionAfterRemount);

    std::vector<RuntimeMountArgs> mountPreviewCandidates;
    std::string mountPreviewError;
    std::string remountSource = ResolveRemountPakPath();
    bool baseRemountSource = IsBaseDataPakPath(remountSource);
    bool canPreviewMount = DeriveRuntimeMountArgCandidatesForLoadedPak(mountPreviewCandidates, mountPreviewError);
    if (canPreviewMount) {
        ImGui::TextDisabled("Remount source: %s", remountSource.c_str());
        ImGui::TextDisabled("Remount candidates: %d, first %s -> %s flags=0x%X",
            static_cast<int>(mountPreviewCandidates.size()),
            mountPreviewCandidates.front().path.c_str(),
            mountPreviewCandidates.front().packageName.c_str(),
            mountPreviewCandidates.front().flags);
        if (baseRemountSource) {
            ImGui::TextWrapped("Base data.pak remount is disabled here. It is a startup archive; use Submit Runtime Reload only, then inspect hook logs/status.");
        }
    }
    else {
        ImGui::TextDisabled("Remount unavailable: %s", mountPreviewError.c_str());
    }

    ImGui::BeginDisabled(baseRemountSource);
    if (ImGui::Button("Remount Loaded/Default Pak")) {
        std::string error;
        if (!g_packageOpenHookInstalled) {
            PakViewer::InitializeRuntimeHooks(g_baseAddress);
        }
        if (TryRemountLoadedPak(error)) {
            SetSubmitStatus("Package remount/load returned success.");
        }
        else {
            SetSubmitStatus("Package remount/load failed: " + error + ".", true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Force Unmount+Remount")) {
        std::string error;
        if (!g_packageOpenHookInstalled) {
            PakViewer::InitializeRuntimeHooks(g_baseAddress);
        }
        if (TryUnmountAndRemountLoadedPak(error)) {
            ClearRuntimeOverrideState();
            if (g_reloadObjectCollectionAfterRemount) {
                if (TryReloadObjectCollectionXml(error)) {
                    SetSubmitStatus("Package unmount/remount and objectcollection reload returned success.");
                }
                else {
                    SetSubmitStatus("Package unmount/remount returned success; objectcollection reload failed: " + error + ".", true);
                }
            }
            else {
                SetSubmitStatus("Package unmount/remount returned success.");
            }
        }
        else {
            SetSubmitStatus("Package unmount/remount failed: " + error + ".", true);
        }
    }
    ImGui::EndDisabled();
    if (!g_lastSubmitStatus.empty()) {
        ImGui::SameLine();
        ImGui::TextWrapped("Last submit: %s", g_lastSubmitStatus.c_str());
    }
}

void RenderSelectedObjectEditor(ObjectRow& row) {
    if (!row.node) {
        return;
    }

    ImGui::SeparatorText("Object Edit");
    int nodeIndex = 0;
    ImGui::BeginChild("ObjectAttributeEditors", ImVec2(0, 260), true, ImGuiWindowFlags_HorizontalScrollbar);
    RenderNodeAttributeEditors(*row.node, nodeIndex);
    ImGui::EndChild();

    if (ImGui::Button("Submit Runtime Reload")) {
        std::string error;
        try {
            std::vector<uint8_t> serialized;
            if (!BuildPatchedObjectCollectionPayload(serialized, error)) {
                SetSubmitStatus("Patch build failed: " + error + ".", true);
            }
            else if (serialized == g_originalObjectCollectionPayload) {
                SetSubmitStatus("Submit skipped: serialized objectcollection has no binary changes. Last edit: "
                    + (g_lastEditSummary.empty() ? std::string("<none>") : g_lastEditSummary) + ".", true);
            }
            else {
                PropReader validationReader(serialized);
                Node validationRoot;
                if (!validationReader.ReadNode(validationRoot, error) || validationReader.Offset() != serialized.size()) {
                    SetSubmitStatus("Serialized objectcollection failed validation: " + error + ".", true);
                }
                else if (!WritePatchedPakInPlaceStreamed(g_decodedEntryIndex, serialized, error)) {
                    if (serialized.size() != g_originalObjectCollectionPayload.size()) {
                        SetSubmitStatus("Mounted pak is locked, and live memory override cannot change objectcollection stream size yet. Original="
                            + std::to_string(g_originalObjectCollectionPayload.size())
                            + " serialized=" + std::to_string(serialized.size())
                            + ". Close the game to write the pak, or make a same-size edit.", true);
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
                        g_runtimeOverrideObjectCollectionPayload = serialized;
                        g_runtimeOverrideObjectCollectionPath = (g_decodedEntryIndex >= 0 && g_decodedEntryIndex < static_cast<int>(g_entries.size()))
                            ? NormalizeRuntimePackagePath(g_entries[g_decodedEntryIndex].name)
                            : std::string();
                        ++g_runtimeOverrideGeneration;
                        g_hasRuntimeObjectCollectionOverride = true;
                        g_runtimeOverrideStreams.clear();
                        g_runtimeOverrideStreamOffsets.clear();
                        g_runtimeOverrideTaggedStreams = 0;
                        g_runtimeOverridePatchedReads = 0;
                        g_runtimeOverrideCacheEvictions = 0;
                    }
                    if (!g_packageOpenHookInstalled) {
                        PakViewer::InitializeRuntimeHooks(g_baseAddress);
                    }

                    LOG_INFO("[PakViewer] Mounted pak write failed; staged objectcollection runtime override path="
                        << g_runtimeOverrideObjectCollectionPath << " bytes=" << serialized.size()
                        << " generation=" << g_runtimeOverrideGeneration << ". Write error: " << error);
                    {
                        std::ostringstream oss;
                        oss << "[PakViewer] Staged objectcollection runtime override path="
                            << g_runtimeOverrideObjectCollectionPath << " bytes=" << serialized.size()
                            << " generation=" << g_runtimeOverrideGeneration << " write_error=" << error;
                        PakTrace(oss.str());
                    }

                    if (!g_packageOpenHookInstalled) {
                        SetSubmitStatus("Mounted pak is locked, and package-open hook is not installed. Staged bytes cannot live reload. Write error: " + error + ".", true);
                    }
                    else if (TryReloadObjectCollectionXml(error)) {
                        uint32_t taggedStreams = 0;
                        uint32_t patchedReads = 0;
                        uint32_t cacheEvictions = 0;
                        {
                            std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
                            taggedStreams = g_runtimeOverrideTaggedStreams;
                            patchedReads = g_runtimeOverridePatchedReads;
                            cacheEvictions = g_runtimeOverrideCacheEvictions;
                        }
                        if (patchedReads != 0) {
                            g_serializedObjectCollectionPayload = serialized;
                            g_originalObjectCollectionPayload = serialized;
                            g_hasPendingObjectEdit = false;
                            g_editGeneration = 0;
                            SetSubmitStatus("Mounted pak is locked; staged bytes were served through package-open hook; objectcollection reload returned. Streams="
                                + std::to_string(taggedStreams)
                                + " reads=" + std::to_string(patchedReads)
                                + " evictions=" + std::to_string(cacheEvictions) + ".");
                            PakTrace("[PakViewer] Submit result served streams=" + std::to_string(taggedStreams)
                                + " reads=" + std::to_string(patchedReads)
                                + " evictions=" + std::to_string(cacheEvictions));
                        }
                        else {
                            SetSubmitStatus("Mounted pak is locked; objectcollection reload returned, but staged bytes were not read. Streams="
                                + std::to_string(taggedStreams)
                                + " reads=0 evictions=" + std::to_string(cacheEvictions)
                                + ". No live change is expected; check package-open paths in the log.", true);
                            PakTrace("[PakViewer] Submit result not-read streams=" + std::to_string(taggedStreams)
                                + " reads=0 evictions=" + std::to_string(cacheEvictions));
                        }
                    }
                    else {
                        SetSubmitStatus("Mounted pak is locked; staged bytes are ready, but runtime reload failed: " + error + ".", true);
                    }
                }
                else {
                    {
                        std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
                        g_runtimeOverrideObjectCollectionPayload.clear();
                        g_runtimeOverrideObjectCollectionPath.clear();
                        g_hasRuntimeObjectCollectionOverride = false;
                        g_runtimeOverrideStreams.clear();
                        g_runtimeOverrideStreamOffsets.clear();
                    }
                    g_serializedObjectCollectionPayload = serialized;
                    g_originalObjectCollectionPayload = serialized;
                    g_hasPendingObjectEdit = false;
                    g_editGeneration = 0;
                    LOG_INFO("[PakViewer] Wrote patched objectcollection to " << g_loadedPak
                        << " (" << serialized.size() << " bytes; backup .tfpayload.bak)");

                    if (TryReloadObjectCollectionXml(error)) {
                        SetSubmitStatus("Patched pak written; objectcollection reload returned.");
                    }
                    else {
                        SetSubmitStatus("Patched pak written, runtime reload failed: " + error + ".", true);
                    }
                }
            }
        }
        catch (const std::bad_alloc&) {
            SetSubmitStatus("Runtime reload failed: out of memory.", true);
        }
        catch (const std::exception& e) {
            SetSubmitStatus(std::string("Runtime reload failed: ") + e.what() + ".", true);
        }
        catch (...) {
            SetSubmitStatus("Runtime reload failed with an unknown exception.", true);
        }
    }
    ImGui::SameLine();
    if (g_hasPendingObjectEdit) {
        ImGui::Text("Pending edits staged (%d): %s", g_editGeneration, g_lastEditSummary.c_str());
    }
    else {
        ImGui::TextDisabled("No staged edits");
    }
    if (!g_lastSubmitStatus.empty()) {
        ImGui::TextWrapped("Last submit: %s", g_lastSubmitStatus.c_str());
    }
}

void ApplyDecodedTreeFilter() {
    size_t length = strlen(g_decodedTreeFilterDraft);
    if (length == 0) {
        g_decodedTreeFilter[0] = '\0';
        g_decodedTreeFilterMatches.clear();
        g_status = "Decoded tree filter cleared.";
        return;
    }

    if (length < 3) {
        g_decodedTreeFilter[0] = '\0';
        g_decodedTreeFilterMatches.clear();
        g_status = "Decoded tree filter needs at least 3 characters.";
        return;
    }

    strncpy_s(g_decodedTreeFilter, g_decodedTreeFilterDraft, _TRUNCATE);
    g_decodedTreeFilterMatches.clear();
    CollectDecodedTreeFilterMatches(g_decodedRoot, Lower(g_decodedTreeFilter));

    std::ostringstream oss;
    oss << "Decoded tree filter applied: " << g_decodedTreeFilterMatches.size() << " matching/parent nodes.";
    g_status = oss.str();
}

void RenderDecodedDetails() {
    if (!g_hasDecodedRoot) {
        return;
    }

    if (ImGui::BeginTabBar("ObjectCollectionViews")) {
        if (ImGui::BeginTabItem("Selected Object")) {
            ImGui::InputText("Selected Object Filter", g_selectedObjectFilter, sizeof(g_selectedObjectFilter));
            if (g_selectedObject >= 0 && g_selectedObject < static_cast<int>(g_objects.size())) {
                ObjectRow& row = g_objects[g_selectedObject];
                ImGui::Text("Name: %s", row.name.c_str());
                ImGui::Text("Category: %s", row.categoryPath.c_str());
                ImGui::Text("Kind: %s | ID: %s | Type: %s | Package: %s | Exclusive: %s",
                    row.kind.c_str(), row.id.c_str(), row.type.c_str(), row.packageId.c_str(), row.exclusive.c_str());
                std::string devFile = StripVirtualPrefix(row.filename);
                std::string sourceFile = StripVirtualPrefix(row.originalFilename);
                ImGui::TextWrapped("Dev File: %s", devFile.c_str());
                ImGui::TextWrapped("Source File: %s", sourceFile.c_str());
                ImGui::Text("Offset: 0x%zX | Size: %zu", row.offset, row.size);
                RenderSelectedObjectEditor(row);
                ImGui::Separator();
                if (row.node) {
                    RenderDecodedNode(*row.node, g_selectedObjectFilter);
                }
            }
            else {
                ImGui::TextDisabled("Select an object row to inspect its decoded attributes and children.");
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Decoded Tree")) {
            if (ImGui::InputText("Decoded Tree Filter", g_decodedTreeFilterDraft, sizeof(g_decodedTreeFilterDraft), ImGuiInputTextFlags_EnterReturnsTrue)) {
                ApplyDecodedTreeFilter();
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply")) {
                ApplyDecodedTreeFilter();
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                g_decodedTreeFilterDraft[0] = '\0';
                g_decodedTreeFilter[0] = '\0';
                g_decodedTreeFilterMatches.clear();
                g_status = "Decoded tree filter cleared.";
            }
            ImGui::Checkbox("Show full tree", &g_showFullDecodedTree);

            ImGui::BeginChild("DecodedTree", ImVec2(0, 300), false, ImGuiWindowFlags_HorizontalScrollbar);
            if (g_decodedTreeFilter[0] != '\0' || g_showFullDecodedTree) {
                RenderDecodedNode(g_decodedRoot, g_decodedTreeFilter);
            }
            else {
                ImGui::TextDisabled("Enter 3+ characters and Apply, or enable Show full tree.");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

} // namespace

namespace PakViewer {

bool InitializeRuntimeHooks(uintptr_t baseAddress) {
    g_baseAddress = baseAddress;

    if (BaseAddress::IsSteamVersion()) {
        LOG_WARNING("[PakViewer] Runtime package-open hook is only mapped for Uplay in this build");
        return false;
    }

    if (g_packageOpenHookInstalled) {
        return true;
    }

    if (baseAddress == 0 || kPackageManagerGlobalRvaUplay == 0) {
        LOG_WARNING("[PakViewer] Runtime package-open hook unavailable: missing base address");
        return false;
    }

    void** packageManagerGlobal = reinterpret_cast<void**>(baseAddress + kPackageManagerGlobalRvaUplay);
    if (IsBadReadPtr(packageManagerGlobal, sizeof(void*)) || *packageManagerGlobal == nullptr) {
        LOG_WARNING("[PakViewer] Runtime package-open hook unavailable: package manager not ready");
        return false;
    }

    void* packageManager = *packageManagerGlobal;
    if (IsBadReadPtr(packageManager, sizeof(void*))) {
        LOG_WARNING("[PakViewer] Runtime package-open hook unavailable: invalid package manager");
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(packageManager);
    if (vtable == nullptr || IsBadReadPtr(vtable, kPackageMount64VtableOffset + sizeof(void*))) {
        LOG_WARNING("[PakViewer] Runtime package-open hook unavailable: invalid package manager vtable");
        return false;
    }

    void* target = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(vtable) + kPackageOpen24VtableOffset);
    if (target == nullptr) {
        LOG_WARNING("[PakViewer] Runtime package-open hook unavailable: null PackageApi24 target");
        return false;
    }

    void* mountTarget = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(vtable) + kPackageMount64VtableOffset);
    void* existsTarget = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(vtable) + 0x20);
    void* mapTarget = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(vtable) + 0x70);
    void* finalizeTarget = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(vtable) + 0xd8);
    LOG_INFO("[PakViewer] Package manager vtable slots open24=0x" << std::hex
        << reinterpret_cast<uintptr_t>(target)
        << " exists20=0x" << reinterpret_cast<uintptr_t>(existsTarget)
        << " mount64=0x" << reinterpret_cast<uintptr_t>(mountTarget)
        << " map70=0x" << reinterpret_cast<uintptr_t>(mapTarget)
        << " finalizeD8=0x" << reinterpret_cast<uintptr_t>(finalizeTarget));

    MH_STATUS status = MH_CreateHook(
        target,
        reinterpret_cast<LPVOID>(&Hook_PackageOpen24),
        reinterpret_cast<LPVOID*>(&g_originalPackageOpen24));
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
        LOG_WARNING("[PakViewer] Failed to create package-open hook: " << MH_StatusToString(status));
        return false;
    }

    status = MH_EnableHook(target);
    if (status != MH_OK && status != MH_ERROR_ENABLED) {
        LOG_WARNING("[PakViewer] Failed to enable package-open hook: " << MH_StatusToString(status));
        return false;
    }

    g_packageOpen24Target = target;
    g_packageOpenHookInstalled = true;
    LOG_INFO("[PakViewer] Package-open hook installed");
    PakTrace("[PakViewer] Package-open hook installed");

    if (!g_packageMountHookInstalled && mountTarget != nullptr) {
        status = MH_CreateHook(
            mountTarget,
            reinterpret_cast<LPVOID>(&Hook_PackageMount64),
            reinterpret_cast<LPVOID*>(&g_originalPackageMount64));
        if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
            status = MH_EnableHook(mountTarget);
            if (status == MH_OK || status == MH_ERROR_ENABLED) {
                g_packageMount64Target = mountTarget;
                g_packageMountHookInstalled = true;
                LOG_INFO("[PakViewer] Package mount/load hook installed");
            }
            else {
                LOG_WARNING("[PakViewer] Failed to enable package mount/load hook: " << MH_StatusToString(status));
            }
        }
        else {
            LOG_WARNING("[PakViewer] Failed to create package mount/load hook: " << MH_StatusToString(status));
        }
    }

    if (!g_textureLoadHookInstalled && kLoadAndCacheTextureFromFileRvaUplay != 0) {
        void* textureLoadTarget = reinterpret_cast<void*>(baseAddress + kLoadAndCacheTextureFromFileRvaUplay);
        status = MH_CreateHook(
            textureLoadTarget,
            reinterpret_cast<LPVOID>(&Hook_LoadAndCacheTextureFromFile),
            reinterpret_cast<LPVOID*>(&g_originalLoadAndCacheTextureFromFile));
        if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
            status = MH_EnableHook(textureLoadTarget);
            if (status == MH_OK || status == MH_ERROR_ENABLED) {
                g_textureLoadTarget = textureLoadTarget;
                g_textureLoadHookInstalled = true;
                LOG_INFO("[PakViewer] LoadAndCacheTextureFromFile hook installed");
                PakTrace("[PakViewer] LoadAndCacheTextureFromFile hook installed");
            }
            else {
                LOG_WARNING("[PakViewer] Failed to enable LoadAndCacheTextureFromFile hook: " << MH_StatusToString(status));
            }
        }
        else {
            LOG_WARNING("[PakViewer] Failed to create LoadAndCacheTextureFromFile hook: " << MH_StatusToString(status));
        }
    }

    if (!g_gfxLoadHookInstalled && kLoadAndCacheGfxResourceRvaUplay != 0) {
        void* gfxLoadTarget = reinterpret_cast<void*>(baseAddress + kLoadAndCacheGfxResourceRvaUplay);
        status = MH_CreateHook(
            gfxLoadTarget,
            reinterpret_cast<LPVOID>(&Hook_LoadAndCacheGfxResource),
            reinterpret_cast<LPVOID*>(&g_originalLoadAndCacheGfxResource));
        if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
            status = MH_EnableHook(gfxLoadTarget);
            if (status == MH_OK || status == MH_ERROR_ENABLED) {
                g_gfxLoadTarget = gfxLoadTarget;
                g_gfxLoadHookInstalled = true;
                LOG_INFO("[PakViewer] LoadAndCacheGfxResource hook installed");
                PakTrace("[PakViewer] LoadAndCacheGfxResource hook installed");
            }
            else {
                LOG_WARNING("[PakViewer] Failed to enable LoadAndCacheGfxResource hook: " << MH_StatusToString(status));
            }
        }
        else {
            LOG_WARNING("[PakViewer] Failed to create LoadAndCacheGfxResource hook: " << MH_StatusToString(status));
        }
    }

    return true;
}

void ShutdownRuntimeHooks() {
    if (g_gfxLoadHookInstalled && g_gfxLoadTarget != nullptr) {
        MH_DisableHook(g_gfxLoadTarget);
        MH_RemoveHook(g_gfxLoadTarget);
    }
    if (g_textureLoadHookInstalled && g_textureLoadTarget != nullptr) {
        MH_DisableHook(g_textureLoadTarget);
        MH_RemoveHook(g_textureLoadTarget);
    }
    if (g_readCurrentHookInstalled && g_readCurrentTarget != nullptr) {
        MH_DisableHook(g_readCurrentTarget);
        MH_RemoveHook(g_readCurrentTarget);
    }
    if (g_readAtHookInstalled && g_readAtTarget != nullptr) {
        MH_DisableHook(g_readAtTarget);
        MH_RemoveHook(g_readAtTarget);
    }
    if (g_packageOpenHookInstalled && g_packageOpen24Target != nullptr) {
        MH_DisableHook(g_packageOpen24Target);
        MH_RemoveHook(g_packageOpen24Target);
    }
    if (g_packageMountHookInstalled && g_packageMount64Target != nullptr) {
        MH_DisableHook(g_packageMount64Target);
        MH_RemoveHook(g_packageMount64Target);
    }

    std::lock_guard<std::mutex> lock(g_runtimeOverrideMutex);
    g_runtimeOverrideStreams.clear();
    g_runtimeOverrideStreamOffsets.clear();
    g_packageOpenHookInstalled = false;
    g_packageMountHookInstalled = false;
    g_readAtHookInstalled = false;
    g_readCurrentHookInstalled = false;
    g_textureLoadHookInstalled = false;
    g_gfxLoadHookInstalled = false;
    g_packageOpen24Target = nullptr;
    g_packageMount64Target = nullptr;
    g_readAtTarget = nullptr;
    g_readCurrentTarget = nullptr;
    g_textureLoadTarget = nullptr;
    g_gfxLoadTarget = nullptr;
    g_originalPackageOpen24 = nullptr;
    g_originalPackageMount64 = nullptr;
    g_originalResourceStreamReadAt = nullptr;
    g_originalResourceStreamReadCurrent = nullptr;
    g_originalLoadAndCacheTextureFromFile = nullptr;
    g_originalLoadAndCacheGfxResource = nullptr;
}

void Toggle() {
    SetDefaultPaths();
    g_visible = !g_visible;
}

bool IsVisible() {
    return g_visible;
}

void Render() {
    if (!g_visible) {
        return;
    }

    SetDefaultPaths();
    ImGui::SetNextWindowSize(ImVec2(980, 720), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Pak Viewer", &g_visible)) {
        ImGui::End();
        return;
    }

    ImGui::InputText("Pak", g_pakPath, sizeof(g_pakPath));
    ImGui::InputText("Hashes", g_hashPath, sizeof(g_hashPath));
    if (IsBaseDataPakPath(g_pakPath)) {
        ImGui::Checkbox("Allow base data.pak load", &g_allowBasePakLoad);
        ImGui::TextDisabled("Base data.pak is large; load it only when you need to inspect that archive.");
    }
    if (ImGui::Button("Load Pak")) {
        LoadHashes();
        LoadPak();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Hashes")) {
        LoadHashes();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(g_status.c_str());

    ImGui::SeparatorText("Runtime Package Test");
    RenderPackageRemountControls();

    ImGui::InputText("Filter", g_filter, sizeof(g_filter));
    RenderEntriesTree();

    if (g_selectedEntry >= 0 && g_selectedEntry < static_cast<int>(g_entries.size())) {
        const PakEntry& entry = g_entries[g_selectedEntry];
        ImGui::SeparatorText("Selection");
        ImGui::Text("Entry %d: %s", entry.index, entry.name.c_str());
        ImGui::Text("Hash %s | stored %u | unpacked %u | flags 0x%02X | offset 0x%08X",
            Hex32(entry.pathHash).c_str(), entry.storedSize, entry.unpackedSize, entry.flags, entry.dataOffset);

        bool looksLikeObjectCollection = Lower(entry.name).find("objectcollection") != std::string::npos;
        if (looksLikeObjectCollection) {
            if (ImGui::Button("Decode ObjectCollection")) {
                ParseSelectedObjectCollection();
            }
        }
        else {
            ImGui::TextDisabled("Select an objectcollection*.xml entry to decode object rows.");
        }
    }

    RenderObjectsTable();
    RenderDecodedDetails();
    ImGui::End();
}

} // namespace PakViewer
