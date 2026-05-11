#include "pch.h"
#include "pak-runtime-hook.h"
#include "base-address.h"
#include "logging.h"
#include <MinHook.h>
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace PakRuntimeHook {

#if defined(DEVELOPMENT_MODE) || defined(RELEASE_AUTOLOAD_MODE)

    static constexpr uintptr_t UPLAY_FILE_READ_INFLATE_RVA = 0x007C14F0;
    static constexpr uintptr_t UPLAY_PACKAGE_MANAGER_SINGLETON_RVA = 0x01055298;
    static constexpr DWORD DEFAULT_DUMP_LIMIT = 32;
    static constexpr DWORD DEFAULT_DUMP_MAX_BYTES = 1024 * 1024;

    using FileReadInflateFn = uint32_t(__thiscall*)(void* thisPtr, int offset, void* outBuffer, size_t size);
    using PackageLookup10Fn = uintptr_t(__thiscall*)(void* thisPtr, void* name, void* out);
    using PackageExists20Fn = uint32_t(__thiscall*)(void* thisPtr, void* name, uint32_t flags);
    using PackageOpen24Fn = void* (__thiscall*)(void* thisPtr, void* name, uint32_t arg1, uint32_t arg2);

    static FileReadInflateFn g_originalFileReadInflate = nullptr;
    static PackageLookup10Fn g_originalPackageLookup10 = nullptr;
    static PackageExists20Fn g_originalPackageExists20 = nullptr;
    static PackageOpen24Fn g_originalPackageOpen24 = nullptr;
    static void* g_hookTarget = nullptr;
    static void* g_packageLookup10Target = nullptr;
    static void* g_packageExists20Target = nullptr;
    static void* g_packageOpen24Target = nullptr;
    static std::ofstream g_csvFile;
    static std::ofstream g_streamCsvFile;
    static std::ofstream g_packageApiCsvFile;
    static std::mutex g_csvMutex;
    static std::mutex g_packageApiMutex;
    static std::mutex g_dumpMutex;
    static std::unordered_set<uint32_t> g_seenStreamInfoPtrs;
    static std::unordered_set<uint32_t> g_initializedDumpStreams;
    static std::atomic<uint32_t> g_readCounter{ 0 };
    static std::atomic<bool> g_forceDumpThreadRunning{ false };
    static std::atomic<bool> g_forceDumpShutdown{ false };
    static std::atomic<bool> g_forceDumpStarted{ false };
    static std::atomic<bool> g_forceDumpInStep{ false };
    static HANDLE g_forceDumpThread = nullptr;
    static void* g_packageManager = nullptr;
    static void* g_stringVtable = nullptr;
    static bool g_binaryDumpEnabled = false;
    static DWORD g_binaryDumpLimit = DEFAULT_DUMP_LIMIT;
    static DWORD g_binaryDumpMaxBytes = DEFAULT_DUMP_MAX_BYTES;

    struct StreamInfoSnapshot {
        uint32_t ptr = 0;
        uint32_t field[8] = {};
        bool valid = false;
    };

    struct DataPatchEntry {
        uint32_t index = 0;
        std::string name;
        uint32_t storedSize = 0;
        uint32_t expandedSize = 0;
        uint32_t offset = 0;
    };

    struct GameString {
        void* vtable;
        uint32_t packedLength;
        const char* data;
        uint16_t flags;
        uint16_t padding;
    };

    static std::vector<DataPatchEntry> g_dataPatchEntries;

    static DWORD ReadEnvDword(const char* name, DWORD fallbackValue) {
        char buffer[64] = {};
        DWORD charsRead = GetEnvironmentVariableA(name, buffer, sizeof(buffer));
        if (charsRead == 0 || charsRead >= sizeof(buffer)) {
            return fallbackValue;
        }

        char* end = nullptr;
        unsigned long value = strtoul(buffer, &end, 10);
        if (end == buffer) {
            return fallbackValue;
        }

        return static_cast<DWORD>(value);
    }

    static bool EnvFlagEnabled(const char* name) {
        char buffer[16] = {};
        DWORD charsRead = GetEnvironmentVariableA(name, buffer, sizeof(buffer));
        return charsRead > 0 && charsRead < sizeof(buffer) && buffer[0] == '1';
    }

    static std::vector<std::string> ParseCsvLine(const std::string& line) {
        std::vector<std::string> fields;
        std::string current;
        bool inQuotes = false;

        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');
                    ++i;
                }
                else {
                    inQuotes = !inQuotes;
                }
            }
            else if (c == ',' && !inQuotes) {
                fields.push_back(current);
                current.clear();
            }
            else {
                current.push_back(c);
            }
        }

        fields.push_back(current);
        return fields;
    }

    static uint32_t ParseU32(const std::string& value) {
        if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
            return static_cast<uint32_t>(strtoul(value.c_str() + 2, nullptr, 16));
        }
        return static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
    }

    static bool LoadDataPatchInventory(const char* path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }

        std::string line;
        std::getline(file, line);

        std::vector<DataPatchEntry> entries;
        while (std::getline(file, line)) {
            if (line.empty()) {
                continue;
            }

            std::vector<std::string> fields = ParseCsvLine(line);
            if (fields.size() < 11) {
                continue;
            }

            DataPatchEntry entry;
            entry.index = ParseU32(fields[0]);
            entry.name = fields[1];
            entry.storedSize = ParseU32(fields[4]);
            entry.expandedSize = ParseU32(fields[5]);
            entry.offset = ParseU32(fields[9]);
            entries.push_back(entry);
        }

        g_dataPatchEntries.swap(entries);
        return !g_dataPatchEntries.empty();
    }

    static std::string SanitizeFileName(const std::string& value) {
        std::string result;
        result.reserve(value.size());

        for (char c : value) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '.' || c == '_' || c == '-') {
                result.push_back(c);
            }
            else {
                result.push_back('_');
            }
        }

        return result;
    }

    static const DataPatchEntry* MatchDataPatchEntry(const StreamInfoSnapshot& stream) {
        if (!stream.valid || stream.ptr == 0) {
            return nullptr;
        }

        for (const DataPatchEntry& entry : g_dataPatchEntries) {
            bool offsetMatches = stream.field[0] == entry.offset;
            bool sizeMatches =
                stream.field[1] == entry.expandedSize ||
                stream.field[1] == entry.storedSize ||
                (entry.storedSize >= 128 && stream.field[1] == entry.storedSize - 128);

            if (offsetMatches && sizeMatches) {
                return &entry;
            }
        }

        return nullptr;
    }

    static std::string NormalizeAssetName(const std::string& name) {
        const char* prefix = "<evo2_devfiles>";
        const size_t prefixLength = strlen(prefix);
        if (name.compare(0, prefixLength, prefix) == 0) {
            return name.substr(prefixLength);
        }
        return name;
    }

    static const DataPatchEntry* FindDataPatchEntryByName(const std::string& name) {
        std::string normalizedName = NormalizeAssetName(name);
        for (const DataPatchEntry& entry : g_dataPatchEntries) {
            if (entry.name == normalizedName) {
                return &entry;
            }
        }

        return nullptr;
    }

    static bool SafeReadU32(const void* address, uint32_t& value) {
        __try {
            value = *static_cast<const uint32_t*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            value = 0;
            return false;
        }
    }

    static bool SafeReadPointer(const void* address, void*& value) {
        __try {
            value = *static_cast<void* const*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            value = nullptr;
            return false;
        }
    }

    static bool SafeReadByte(const void* address, char& value) {
        __try {
            value = *static_cast<const char*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            value = 0;
            return false;
        }
    }

    static std::string ReadGameString(void* stringObj) {
        if (stringObj == nullptr) {
            return "";
        }

        uint32_t packedLength = 0;
        void* dataPtr = nullptr;
        SafeReadU32(static_cast<const uint8_t*>(stringObj) + 4, packedLength);
        SafeReadPointer(static_cast<const uint8_t*>(stringObj) + 8, dataPtr);

        uint32_t length = packedLength >> 16;
        if (length > 4096) {
            length = 4096;
        }

        if (dataPtr == nullptr || length == 0) {
            return "";
        }

        std::string result;
        result.reserve(length);
        const char* chars = static_cast<const char*>(dataPtr);
        for (uint32_t i = 0; i < length; ++i) {
            char c = 0;
            if (!SafeReadByte(chars + i, c)) {
                break;
            }

            if (c == '\0') {
                break;
            }
            result.push_back((c == ',' || c == '\r' || c == '\n') ? ' ' : c);
        }

        return result;
    }

    static bool BuildGameString(const std::string& text, GameString& outString) {
        if (g_stringVtable == nullptr || text.empty() || text.size() > 0xffff) {
            return false;
        }

        outString.vtable = g_stringVtable;
        outString.packedLength = static_cast<uint32_t>(text.size()) << 16;
        outString.data = text.c_str();
        outString.flags = 0;
        outString.padding = 0;
        return true;
    }

    static bool ForceDumpMarkerExists() {
        DWORD attributes = GetFileAttributesA("pak_runtime\\force_data_patch_dump.txt");
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    static StreamInfoSnapshot ReadStreamInfo(uint32_t streamInfoPtr) {
        StreamInfoSnapshot snapshot;
        snapshot.ptr = streamInfoPtr;

        if (streamInfoPtr == 0) {
            return snapshot;
        }

        bool anyRead = false;
        const uint8_t* base = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(streamInfoPtr));
        for (int i = 0; i < 8; ++i) {
            anyRead = SafeReadU32(base + (i * sizeof(uint32_t)), snapshot.field[i]) || anyRead;
        }

        snapshot.valid = anyRead;
        return snapshot;
    }

    static std::string HexSample(const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        const size_t sampleSize = size < 32 ? size : 32;

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (size_t i = 0; i < sampleSize; ++i) {
            if (i != 0) {
                oss << ' ';
            }
            oss << std::setw(2) << static_cast<unsigned int>(bytes[i]);
        }
        return oss.str();
    }

    static std::string AsciiSample(const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        const size_t sampleSize = size < 32 ? size : 32;

        std::string result;
        result.reserve(sampleSize);
        for (size_t i = 0; i < sampleSize; ++i) {
            uint8_t b = bytes[i];
            result.push_back((b >= 32 && b < 127 && b != ',') ? static_cast<char>(b) : '.');
        }
        return result;
    }

    static void DumpBuffer(uint32_t readIndex, int offset, const void* data, uint32_t size) {
        if (!g_binaryDumpEnabled || readIndex > g_binaryDumpLimit || data == nullptr || size == 0) {
            return;
        }

        DWORD bytesToWrite = size;
        if (bytesToWrite > g_binaryDumpMaxBytes) {
            bytesToWrite = g_binaryDumpMaxBytes;
        }

        char path[MAX_PATH] = {};
        sprintf_s(path, sizeof(path), "pak_runtime\\read_%06u_off_%08X_size_%u.bin", readIndex, offset, bytesToWrite);

        HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }

        DWORD bytesWritten = 0;
        WriteFile(file, data, bytesToWrite, &bytesWritten, nullptr);
        CloseHandle(file);
    }

    static void DumpNamedDataPatchChunk(uint32_t readIndex, const StreamInfoSnapshot& stream, const DataPatchEntry& entry, int offset, const void* data, uint32_t size) {
        if (data == nullptr || size == 0 || offset < 0) {
            return;
        }

        CreateDirectoryA("pak_runtime\\data_patch_named", nullptr);

        std::lock_guard<std::mutex> lock(g_dumpMutex);
        bool firstWriteForStream = g_initializedDumpStreams.insert(stream.ptr).second;
        char path[MAX_PATH] = {};
        sprintf_s(
            path,
            sizeof(path),
            "pak_runtime\\data_patch_named\\%03u_%s",
            entry.index,
            SanitizeFileName(entry.name).c_str());

        DWORD creationDisposition = (firstWriteForStream && offset == 0) ? CREATE_ALWAYS : OPEN_ALWAYS;
        HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, creationDisposition, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }

        LONG highOffset = 0;
        SetFilePointer(file, offset, &highOffset, FILE_BEGIN);

        DWORD bytesWritten = 0;
        WriteFile(file, data, size, &bytesWritten, nullptr);
        CloseHandle(file);

        if (firstWriteForStream) {
            LOG_VERBOSE("[PakRuntimeHook] Dumping data_patch entry " << entry.index << " (" << entry.name << ") from read " << readIndex);
        }
    }

    static void WriteNamedDataPatchFile(const DataPatchEntry& entry, const void* data, uint32_t size) {
        if (data == nullptr || size == 0) {
            return;
        }

        CreateDirectoryA("pak_runtime\\data_patch_named", nullptr);

        char path[MAX_PATH] = {};
        sprintf_s(
            path,
            sizeof(path),
            "pak_runtime\\data_patch_named\\%03u_%s",
            entry.index,
            SanitizeFileName(entry.name).c_str());

        HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }

        DWORD bytesWritten = 0;
        WriteFile(file, data, size, &bytesWritten, nullptr);
        CloseHandle(file);
    }

    static bool NamedDumpComplete(const DataPatchEntry& entry) {
        char path[MAX_PATH] = {};
        sprintf_s(
            path,
            sizeof(path),
            "pak_runtime\\data_patch_named\\%03u_%s",
            entry.index,
            SanitizeFileName(entry.name).c_str());

        WIN32_FILE_ATTRIBUTE_DATA data = {};
        if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
            return false;
        }

        uint64_t size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        return size == entry.expandedSize;
    }

    static void* SafePackageOpen(void* packageManager, GameString* name, uint32_t flags) {
        __try {
            return g_originalPackageOpen24(packageManager, name, flags, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    static uint32_t SafeReadInflate(void* stream, uint32_t offset, void* buffer, uint32_t size) {
        __try {
            return g_originalFileReadInflate(stream, static_cast<int>(offset), buffer, size);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    static bool TryForceDumpEntryVariant(const DataPatchEntry& entry, const std::string& name, uint32_t flags) {
        GameString gameString = {};
        if (!BuildGameString(name, gameString)) {
            return false;
        }

        void* stream = SafePackageOpen(g_packageManager, &gameString, flags);
        if (stream == nullptr) {
            return false;
        }

        std::vector<uint8_t> buffer(entry.expandedSize);
        uint32_t totalRead = 0;
        const uint32_t chunkSize = 0x10000;
        while (totalRead < entry.expandedSize) {
            uint32_t remaining = entry.expandedSize - totalRead;
            uint32_t request = remaining < chunkSize ? remaining : chunkSize;
            uint32_t read = SafeReadInflate(stream, totalRead, buffer.data() + totalRead, request);
            if (read == 0) {
                break;
            }
            totalRead += read;
        }

        if (totalRead == entry.expandedSize) {
            WriteNamedDataPatchFile(entry, buffer.data(), totalRead);
            LOG_VERBOSE("[PakRuntimeHook] Force dumped " << entry.name << " via " << name << " flags=0x" << std::hex << flags << std::dec);
            return true;
        }

        LOG_VERBOSE("[PakRuntimeHook] Force dump read mismatch for " << entry.name << " using " << name << " flags=0x" << std::hex << flags << std::dec << ": " << totalRead << " / " << entry.expandedSize);
        return false;
    }

    static bool TryDumpExistingStreamByEntry(void* stream, const DataPatchEntry& entry) {
        if (stream == nullptr || entry.expandedSize == 0 || NamedDumpComplete(entry)) {
            return false;
        }

        LOG_INFO("[PakRuntimeHook] Natural stream matched " << entry.index << " (" << entry.name << "), attempting full dump");

        std::vector<uint8_t> buffer(entry.expandedSize);
        uint32_t totalRead = 0;
        const uint32_t chunkSize = 0x10000;
        while (totalRead < entry.expandedSize) {
            uint32_t remaining = entry.expandedSize - totalRead;
            uint32_t request = remaining < chunkSize ? remaining : chunkSize;
            uint32_t read = SafeReadInflate(stream, totalRead, buffer.data() + totalRead, request);
            if (read == 0) {
                break;
            }
            totalRead += read;
        }

        if (totalRead == entry.expandedSize) {
            WriteNamedDataPatchFile(entry, buffer.data(), totalRead);
            LOG_INFO("[PakRuntimeHook] Dumped natural stream " << entry.index << " (" << entry.name << ")");
            return true;
        }

        LOG_INFO("[PakRuntimeHook] Natural stream read mismatch for " << entry.name << ": " << totalRead << " / " << entry.expandedSize);
        return false;
    }

    static void TryDumpNaturalPackageOpen(void* stream, void* nameObj) {
        if (!ForceDumpMarkerExists() || stream == nullptr) {
            return;
        }

        std::string name = ReadGameString(nameObj);
        if (name.empty()) {
            return;
        }

        const DataPatchEntry* entry = FindDataPatchEntryByName(name);
        if (entry != nullptr) {
            TryDumpExistingStreamByEntry(stream, *entry);
        }
    }

    static bool TryForceDumpEntry(const DataPatchEntry& entry) {
        std::vector<std::string> names;
        names.push_back(entry.name);
        names.push_back(std::string("<evo2_devfiles>") + entry.name);

        uint32_t flags[] = { 0x1, 0x10001, 0x50001 };
        for (const std::string& name : names) {
            for (uint32_t flagsValue : flags) {
                if (TryForceDumpEntryVariant(entry, name, flagsValue)) {
                    return true;
                }
            }
        }

        return false;
    }

    static void RunForceDumpStepOnPackageThread() {
        if (!ForceDumpMarkerExists() || g_forceDumpShutdown.load() || g_forceDumpInStep.exchange(true)) {
            return;
        }

        if (!g_forceDumpStarted.exchange(true)) {
            LOG_INFO("[PakRuntimeHook] Force data_patch dump started on package thread");
        }

        static size_t nextEntryIndex = 0;
        static uint32_t attempted = 0;
        static uint32_t dumped = 0;
        static uint32_t skipped = 0;
        static uint32_t failed = 0;

        bool didWork = false;
        for (size_t scans = 0; scans < g_dataPatchEntries.size() && nextEntryIndex < g_dataPatchEntries.size(); ++scans) {
            const DataPatchEntry& entry = g_dataPatchEntries[nextEntryIndex++];
            if (entry.name.empty() || entry.expandedSize == 0 || NamedDumpComplete(entry)) {
                ++skipped;
                continue;
            }

            ++attempted;
            if (TryForceDumpEntry(entry)) {
                ++dumped;
            }
            else {
                ++failed;
            }
            didWork = true;
            break;
        }

        if (!didWork || nextEntryIndex >= g_dataPatchEntries.size()) {
            LOG_INFO("[PakRuntimeHook] Force data_patch dump finished on package thread: attempted=" << attempted << ", dumped=" << dumped << ", skipped=" << skipped << ", failed=" << failed);
            DeleteFileA("pak_runtime\\force_data_patch_dump.txt");
        }

        g_forceDumpInStep.store(false);
    }

    static void CaptureRead(void* thisPtr, int offset, void* outBuffer, size_t requestedSize, uint32_t returnedSize) {
        uint32_t readIndex = ++g_readCounter;

        uint32_t streamInfo = 0;
        uint32_t modeFlag = 0;
        SafeReadU32(static_cast<const uint8_t*>(thisPtr) + 0x10, streamInfo);
        SafeReadU32(static_cast<const uint8_t*>(thisPtr) + 0x70, modeFlag);
        StreamInfoSnapshot stream = ReadStreamInfo(streamInfo);
        uint64_t sourceOffset = static_cast<uint64_t>(stream.field[0]) + static_cast<uint32_t>(offset);
        const DataPatchEntry* matchedEntry = MatchDataPatchEntry(stream);

        DumpBuffer(readIndex, offset, outBuffer, returnedSize);
        if (matchedEntry != nullptr) {
            DumpNamedDataPatchChunk(readIndex, stream, *matchedEntry, offset, outBuffer, returnedSize);
        }

        std::lock_guard<std::mutex> lock(g_csvMutex);
        if (!g_csvFile.is_open()) {
            return;
        }

        bool firstSeenStream = streamInfo != 0 && g_seenStreamInfoPtrs.insert(streamInfo).second;
        if (firstSeenStream && g_streamCsvFile.is_open()) {
            g_streamCsvFile
                << readIndex << ','
                << "0x" << std::hex << reinterpret_cast<uintptr_t>(thisPtr) << ','
                << "0x" << streamInfo << ','
                << "0x" << modeFlag << ','
                << "0x" << stream.field[0] << ','
                << std::dec << stream.field[1] << ','
                << "0x" << std::hex << stream.field[2] << ','
                << "0x" << stream.field[3] << ','
                << "0x" << stream.field[4] << ','
                << "0x" << stream.field[5] << ','
                << "0x" << stream.field[6] << ','
                << "0x" << stream.field[7] << ','
                << std::dec << returnedSize << ','
                << (matchedEntry != nullptr ? matchedEntry->index : 0xFFFFFFFF) << ','
                << (matchedEntry != nullptr ? matchedEntry->name : "") << ','
                << HexSample(outBuffer, returnedSize) << ','
                << AsciiSample(outBuffer, returnedSize)
                << std::dec << '\n';
            g_streamCsvFile.flush();
        }

        g_csvFile
            << readIndex << ','
            << "0x" << std::hex << reinterpret_cast<uintptr_t>(thisPtr) << ','
            << "0x" << streamInfo << ','
            << "0x" << modeFlag << ','
            << "0x" << stream.field[0] << ','
            << std::dec << stream.field[1] << ','
            << "0x" << std::hex << stream.field[2] << ','
            << "0x" << stream.field[3] << ','
            << "0x" << stream.field[4] << ','
            << "0x" << stream.field[5] << ','
            << "0x" << stream.field[6] << ','
            << "0x" << stream.field[7] << ','
            << std::dec << sourceOffset << ','
            << std::dec << offset << ','
            << requestedSize << ','
            << returnedSize << ','
            << "0x" << std::hex << reinterpret_cast<uintptr_t>(outBuffer) << ','
            << std::dec << (matchedEntry != nullptr ? matchedEntry->index : 0xFFFFFFFF) << ','
            << (matchedEntry != nullptr ? matchedEntry->name : "") << ','
            << HexSample(outBuffer, returnedSize) << ','
            << AsciiSample(outBuffer, returnedSize)
            << std::dec << '\n';
        g_csvFile.flush();
    }

    static uint32_t __fastcall HookFileReadInflate(void* thisPtr, void* edx, int offset, void* outBuffer, size_t size) {
        (void)edx;

        uint32_t result = g_originalFileReadInflate(thisPtr, offset, outBuffer, size);
        if (result != 0 && outBuffer != nullptr) {
            CaptureRead(thisPtr, offset, outBuffer, size, result);
        }
        return result;
    }

    static void LogPackageApiCall(const char* apiName, void* thisPtr, void* arg0, uintptr_t arg1, uintptr_t arg2, uintptr_t result) {
        std::lock_guard<std::mutex> lock(g_packageApiMutex);
        if (!g_packageApiCsvFile.is_open()) {
            return;
        }

        if (g_stringVtable == nullptr && arg0 != nullptr) {
            SafeReadPointer(arg0, g_stringVtable);
        }

        g_packageApiCsvFile
            << apiName << ','
            << "0x" << std::hex << reinterpret_cast<uintptr_t>(thisPtr) << ','
            << "0x" << reinterpret_cast<uintptr_t>(arg0) << ','
            << ReadGameString(arg0) << ','
            << "0x" << arg1 << ','
            << "0x" << arg2 << ','
            << "0x" << result
            << std::dec << '\n';
        g_packageApiCsvFile.flush();
    }

    static uintptr_t __fastcall HookPackageLookup10(void* thisPtr, void* edx, void* name, void* out) {
        (void)edx;
        uintptr_t result = g_originalPackageLookup10(thisPtr, name, out);
        LogPackageApiCall("vtable_10", thisPtr, name, reinterpret_cast<uintptr_t>(out), 0, result);
        return result;
    }

    static uint32_t __fastcall HookPackageExists20(void* thisPtr, void* edx, void* name, uint32_t flags) {
        (void)edx;
        uint32_t result = g_originalPackageExists20(thisPtr, name, flags);
        LogPackageApiCall("vtable_20", thisPtr, name, flags, 0, result);
        return result;
    }

    static void* __fastcall HookPackageOpen24(void* thisPtr, void* edx, void* name, uint32_t arg1, uint32_t arg2) {
        (void)edx;
        void* result = g_originalPackageOpen24(thisPtr, name, arg1, arg2);
        LogPackageApiCall("vtable_24", thisPtr, name, arg1, arg2, reinterpret_cast<uintptr_t>(result));
        TryDumpNaturalPackageOpen(result, name);
        return result;
    }

    static DWORD WINAPI ForceDumpThreadProc(LPVOID) {
        Sleep(12000);

        if (g_forceDumpShutdown.load() || !ForceDumpMarkerExists()) {
            g_forceDumpThreadRunning.store(false);
            return 0;
        }

        if (g_packageManager == nullptr || g_originalPackageOpen24 == nullptr ||
            g_originalFileReadInflate == nullptr || g_stringVtable == nullptr ||
            g_dataPatchEntries.empty()) {
            LOG_WARNING("[PakRuntimeHook] Force dump skipped: package API context is incomplete");
            g_forceDumpThreadRunning.store(false);
            return 0;
        }

        LOG_INFO("[PakRuntimeHook] Force data_patch dump started");
        uint32_t attempted = 0;
        uint32_t dumped = 0;
        uint32_t skipped = 0;
        uint32_t failed = 0;

        for (const DataPatchEntry& entry : g_dataPatchEntries) {
            if (g_forceDumpShutdown.load()) {
                break;
            }

            if (entry.name.empty() || entry.expandedSize == 0 || NamedDumpComplete(entry)) {
                ++skipped;
                continue;
            }

            ++attempted;
            if (TryForceDumpEntry(entry)) {
                ++dumped;
            }
            else {
                ++failed;
            }

            Sleep(5);
        }

        LOG_INFO("[PakRuntimeHook] Force data_patch dump finished: attempted=" << attempted << ", dumped=" << dumped << ", skipped=" << skipped << ", failed=" << failed);
        g_forceDumpThreadRunning.store(false);
        return 0;
    }

    static void StartForceDumpThreadIfRequested() {
        if (!ForceDumpMarkerExists() || g_forceDumpThreadRunning.load()) {
            return;
        }

        g_forceDumpShutdown.store(false);
        g_forceDumpThreadRunning.store(true);
        g_forceDumpThread = CreateThread(nullptr, 0, ForceDumpThreadProc, nullptr, 0, nullptr);
        if (g_forceDumpThread == nullptr) {
            g_forceDumpThreadRunning.store(false);
            LOG_WARNING("[PakRuntimeHook] Could not start force data_patch dump thread");
        }
        else {
            LOG_INFO("[PakRuntimeHook] Force data_patch dump scheduled by marker file");
        }
    }

    static bool InstallPackageManagerHooks(DWORD_PTR baseAddress) {
        void* singletonAddress = reinterpret_cast<void*>(baseAddress + UPLAY_PACKAGE_MANAGER_SINGLETON_RVA);
        void* packageManager = nullptr;
        void* vtable = nullptr;
        if (!SafeReadPointer(singletonAddress, packageManager) || packageManager == nullptr ||
            !SafeReadPointer(packageManager, vtable) || vtable == nullptr) {
            LOG_WARNING("[PakRuntimeHook] Could not resolve package manager singleton for higher-level hooks");
            return true;
        }

        g_packageManager = packageManager;

        SafeReadPointer(static_cast<const uint8_t*>(vtable) + 0x10, g_packageLookup10Target);
        SafeReadPointer(static_cast<const uint8_t*>(vtable) + 0x20, g_packageExists20Target);
        SafeReadPointer(static_cast<const uint8_t*>(vtable) + 0x24, g_packageOpen24Target);

        if (g_packageLookup10Target != nullptr &&
            MH_CreateHook(g_packageLookup10Target, &HookPackageLookup10, reinterpret_cast<void**>(&g_originalPackageLookup10)) == MH_OK) {
            MH_EnableHook(g_packageLookup10Target);
        }

        if (g_packageExists20Target != nullptr &&
            MH_CreateHook(g_packageExists20Target, &HookPackageExists20, reinterpret_cast<void**>(&g_originalPackageExists20)) == MH_OK) {
            MH_EnableHook(g_packageExists20Target);
        }

        if (g_packageOpen24Target != nullptr &&
            MH_CreateHook(g_packageOpen24Target, &HookPackageOpen24, reinterpret_cast<void**>(&g_originalPackageOpen24)) == MH_OK) {
            MH_EnableHook(g_packageOpen24Target);
        }

        LOG_INFO("[PakRuntimeHook] Package manager API hooks installed from singleton 0x" << std::hex << reinterpret_cast<uintptr_t>(packageManager) << std::dec);
        if (ForceDumpMarkerExists()) {
            LOG_INFO("[PakRuntimeHook] Force data_patch dump armed by marker file");
        }
        return true;
    }

    bool Initialize(DWORD_PTR baseAddress) {
        if (BaseAddress::IsSteamVersion()) {
            LOG_WARNING("[PakRuntimeHook] Steam RVA for file_read_inflate is not mapped yet; hook left disabled");
            return true;
        }

        CreateDirectoryA("pak_runtime", nullptr);

        {
            std::lock_guard<std::mutex> lock(g_csvMutex);
            g_csvFile.open("pak_runtime\\pak_runtime_log.csv", std::ios::out | std::ios::trunc);
            if (g_csvFile.is_open()) {
                g_csvFile << "read_index,this_ptr,stream_info_ptr,mode_flag_70,stream_base,stream_size,stream_field2,stream_field3,stream_field4,stream_field5,stream_field6,stream_field7,source_offset,offset,requested_size,returned_size,out_buffer,data_patch_index,data_patch_name,hex_sample,ascii_sample\n";
                g_csvFile.flush();
            }

            g_streamCsvFile.open("pak_runtime\\pak_streams.csv", std::ios::out | std::ios::trunc);
            if (g_streamCsvFile.is_open()) {
                g_streamCsvFile << "first_read_index,this_ptr,stream_info_ptr,mode_flag_70,stream_base,stream_size,stream_field2,stream_field3,stream_field4,stream_field5,stream_field6,stream_field7,first_returned_size,data_patch_index,data_patch_name,hex_sample,ascii_sample\n";
                g_streamCsvFile.flush();
            }

            g_packageApiCsvFile.open("pak_runtime\\pak_package_api.csv", std::ios::out | std::ios::trunc);
            if (g_packageApiCsvFile.is_open()) {
                g_packageApiCsvFile << "api,this_ptr,arg0_ptr,arg0_string,arg1,arg2,result\n";
                g_packageApiCsvFile.flush();
            }
        }

        if (!LoadDataPatchInventory("pak_runtime\\data_patch_inventory.csv") &&
            !LoadDataPatchInventory("data_patch_inventory.csv")) {
            LOG_WARNING("[PakRuntimeHook] data_patch inventory not found; named dumps disabled");
        }
        else {
            LOG_INFO("[PakRuntimeHook] Loaded " << g_dataPatchEntries.size() << " data_patch inventory entries for named dumps");
        }

        g_binaryDumpEnabled = EnvFlagEnabled("TFPAYLOAD_PAK_DUMP_BYTES");
        g_binaryDumpLimit = ReadEnvDword("TFPAYLOAD_PAK_DUMP_LIMIT", DEFAULT_DUMP_LIMIT);
        g_binaryDumpMaxBytes = ReadEnvDword("TFPAYLOAD_PAK_DUMP_MAX_BYTES", DEFAULT_DUMP_MAX_BYTES);

        g_hookTarget = reinterpret_cast<void*>(baseAddress + UPLAY_FILE_READ_INFLATE_RVA);
        MH_STATUS createStatus = MH_CreateHook(g_hookTarget, &HookFileReadInflate, reinterpret_cast<void**>(&g_originalFileReadInflate));
        if (createStatus != MH_OK) {
            LOG_ERROR("[PakRuntimeHook] MH_CreateHook failed for file_read_inflate: " << MH_StatusToString(createStatus));
            return false;
        }

        MH_STATUS enableStatus = MH_EnableHook(g_hookTarget);
        if (enableStatus != MH_OK) {
            LOG_ERROR("[PakRuntimeHook] MH_EnableHook failed for file_read_inflate: " << MH_StatusToString(enableStatus));
            MH_RemoveHook(g_hookTarget);
            g_hookTarget = nullptr;
            g_originalFileReadInflate = nullptr;
            return false;
        }

        LOG_INFO("[PakRuntimeHook] Installed file_read_inflate hook at 0x" << std::hex << reinterpret_cast<uintptr_t>(g_hookTarget) << std::dec);
        InstallPackageManagerHooks(baseAddress);
        LOG_INFO("[PakRuntimeHook] Runtime read log: pak_runtime\\pak_runtime_log.csv");
        LOG_INFO("[PakRuntimeHook] Runtime stream log: pak_runtime\\pak_streams.csv");
        LOG_INFO("[PakRuntimeHook] Runtime package API log: pak_runtime\\pak_package_api.csv");
        if (g_binaryDumpEnabled) {
            LOG_INFO("[PakRuntimeHook] Binary dumping enabled via TFPAYLOAD_PAK_DUMP_BYTES=1");
        }
        return true;
    }

    void Shutdown() {
        if (g_hookTarget != nullptr) {
            MH_DisableHook(g_hookTarget);
            MH_RemoveHook(g_hookTarget);
            g_hookTarget = nullptr;
            g_originalFileReadInflate = nullptr;
        }
        g_forceDumpShutdown.store(true);
        if (g_forceDumpThread != nullptr) {
            WaitForSingleObject(g_forceDumpThread, 2000);
            CloseHandle(g_forceDumpThread);
            g_forceDumpThread = nullptr;
        }
        g_forceDumpThreadRunning.store(false);
        if (g_packageLookup10Target != nullptr) {
            MH_DisableHook(g_packageLookup10Target);
            MH_RemoveHook(g_packageLookup10Target);
            g_packageLookup10Target = nullptr;
            g_originalPackageLookup10 = nullptr;
        }
        if (g_packageExists20Target != nullptr) {
            MH_DisableHook(g_packageExists20Target);
            MH_RemoveHook(g_packageExists20Target);
            g_packageExists20Target = nullptr;
            g_originalPackageExists20 = nullptr;
        }
        if (g_packageOpen24Target != nullptr) {
            MH_DisableHook(g_packageOpen24Target);
            MH_RemoveHook(g_packageOpen24Target);
            g_packageOpen24Target = nullptr;
            g_originalPackageOpen24 = nullptr;
        }

        std::lock_guard<std::mutex> lock(g_csvMutex);
        if (g_csvFile.is_open()) {
            g_csvFile.close();
        }
        if (g_streamCsvFile.is_open()) {
            g_streamCsvFile.close();
        }
        if (g_packageApiCsvFile.is_open()) {
            g_packageApiCsvFile.close();
        }
        g_packageManager = nullptr;
        g_stringVtable = nullptr;
        g_seenStreamInfoPtrs.clear();
        g_initializedDumpStreams.clear();
        g_dataPatchEntries.clear();
    }

#else

    bool Initialize(DWORD_PTR baseAddress) {
        (void)baseAddress;
        return true;
    }

    void Shutdown() {
    }

#endif

}
