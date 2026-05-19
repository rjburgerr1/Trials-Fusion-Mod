#include "pch.h"
#include "ui-view-explorer.h"
#include "actionscript.h"
#include "base-address.h"
#include "logging.h"
#include "imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace UIViewExplorer {
    namespace {
        struct PointerField {
            uintptr_t offset;
            uintptr_t value;
            bool readable;
            std::string ascii;
        };

        struct VTableSlot {
            uintptr_t offset;
            uintptr_t function;
            bool executable;
            std::string moduleRva;
            std::string ghidraAddress;
        };

        char g_addressText[32] = "";
        char g_parentOffsetText[16] = "0";
        uintptr_t g_candidateAddress = 0;
        uintptr_t g_lastScanAddress = 0;
        uintptr_t g_nameStringObjectAddress = 0;
        std::vector<PointerField> g_pointerFields;
        std::vector<VTableSlot> g_vtableSlots;
        std::vector<std::string> g_parentWalkLines;
        std::vector<std::string> g_lastResolvedPath;
        bool g_autoScan = true;
        bool g_showMovieClipTree = true;
        bool g_triedInitialMovieClipAdopt = false;
        bool g_triedResolveNameStringObject = false;
        int g_maxTreeDepth = 8;
        int g_maxChildrenPerNode = 128;

        namespace Uplay {
            constexpr uintptr_t GAME_MANAGER_PTR = 0x0174b308 - 0x00700000;
            constexpr uintptr_t FLASH_PROP_VISIBLE_STRING = 0x016e696c - 0x00700000;
            constexpr uintptr_t FLASH_PROP_NAME_STRING = FLASH_PROP_VISIBLE_STRING + ((0x0d - 0x07) * 0x1c);
        }

        namespace Steam {
            constexpr uintptr_t GAME_MANAGER_PTR = 0x0118d308 - 0x00140000;
            // TODO: map these from the Steam Ghidra database if the Steam build needs UI tree writes.
            constexpr uintptr_t FLASH_PROP_VISIBLE_STRING = 0;
            constexpr uintptr_t FLASH_PROP_NAME_STRING = 0;
        }

        constexpr size_t FLASH_VARIANT_SIZE = 0x34;
        constexpr size_t FLASH_PROP_STRING_SIZE = 0x1c;
        constexpr int FLASH_PROP_VISIBLE_ID = 0x07;
        constexpr int FLASH_PROP_NAME_ID = 0x0d;

        typedef bool (__thiscall* MovieClipSetProperty_t)(void* thisPtr, void* nameString, void* valueVariant);
        typedef bool (__thiscall* MovieClipGetProperty_t)(void* thisPtr, void* nameString, void* outVariant);
        typedef void* (__thiscall* MovieClipGetChild_t)(void* thisPtr, int index);
        typedef const char* (__thiscall* MovieClipGetTypeName_t)(void* thisPtr);
        typedef bool (__thiscall* MovieClipHasProperty_t)(void* thisPtr, int propertyId);
        typedef bool (__thiscall* MovieClipVisibilityCandidate_t)(void* thisPtr);

        std::string Hex(uintptr_t value) {
            std::ostringstream oss;
            oss << "0x" << std::hex << std::setw(sizeof(uintptr_t) * 2) << std::setfill('0') << value;
            return oss.str();
        }

        bool ParseHex(const char* text, uintptr_t& outValue) {
            if (!text || !*text) {
                return false;
            }

            char* end = nullptr;
            unsigned long value = strtoul(text, &end, 16);
            if (end == text) {
                return false;
            }

            while (*end != '\0') {
                if (!std::isspace(static_cast<unsigned char>(*end))) {
                    return false;
                }
                ++end;
            }

            outValue = static_cast<uintptr_t>(value);
            return true;
        }

        bool IsReadableAddress(uintptr_t address, size_t size = sizeof(uintptr_t)) {
            if (address < 0x10000) {
                return false;
            }

            MEMORY_BASIC_INFORMATION mbi = {};
            if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == 0) {
                return false;
            }

            if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) {
                return false;
            }

            uintptr_t regionStart = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            uintptr_t regionEnd = regionStart + mbi.RegionSize;
            return address >= regionStart && address + size <= regionEnd;
        }

        bool IsExecutableAddress(uintptr_t address) {
            if (address < 0x10000) {
                return false;
            }

            MEMORY_BASIC_INFORMATION mbi = {};
            if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == 0) {
                return false;
            }

            if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) {
                return false;
            }

            DWORD protect = mbi.Protect & 0xff;
            return protect == PAGE_EXECUTE
                || protect == PAGE_EXECUTE_READ
                || protect == PAGE_EXECUTE_READWRITE
                || protect == PAGE_EXECUTE_WRITECOPY;
        }

        bool GetMainModuleRange(uintptr_t& outBase, uintptr_t& outEnd) {
            HMODULE module = GetModuleHandle(NULL);
            if (!module) {
                return false;
            }

            uintptr_t base = reinterpret_cast<uintptr_t>(module);
            if (!IsReadableAddress(base, sizeof(IMAGE_DOS_HEADER))) {
                return false;
            }

            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
                return false;
            }

            uintptr_t ntAddress = base + static_cast<uintptr_t>(dos->e_lfanew);
            if (!IsReadableAddress(ntAddress, sizeof(IMAGE_NT_HEADERS))) {
                return false;
            }

            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(ntAddress);
            if (nt->Signature != IMAGE_NT_SIGNATURE) {
                return false;
            }

            outBase = base;
            outEnd = base + nt->OptionalHeader.SizeOfImage;
            return outEnd > outBase;
        }

        std::string FormatModuleRva(uintptr_t address) {
            uintptr_t moduleBase = 0;
            uintptr_t moduleEnd = 0;
            if (!GetMainModuleRange(moduleBase, moduleEnd) || address < moduleBase || address >= moduleEnd) {
                return "";
            }

            std::ostringstream oss;
            oss << "trials_fusion.exe+0x"
                << std::hex << std::setw(8) << std::setfill('0') << (address - moduleBase);
            return oss.str();
        }

        std::string FormatGhidraAddress(uintptr_t address) {
            uintptr_t moduleBase = 0;
            uintptr_t moduleEnd = 0;
            if (!GetMainModuleRange(moduleBase, moduleEnd) || address < moduleBase || address >= moduleEnd) {
                return "";
            }

            uintptr_t imageBase = BaseAddress::IsSteamVersion()
                ? ActionScript::STEAM_GAME_BASE
                : ActionScript::UPLAY_GAME_BASE;

            std::ostringstream oss;
            oss << "0x" << std::hex << std::setw(8) << std::setfill('0') << (imageBase + (address - moduleBase));
            return oss.str();
        }

        template <typename T>
        bool SafeRead(uintptr_t address, T& outValue) {
            if (!IsReadableAddress(address, sizeof(T))) {
                return false;
            }

            __try {
                outValue = *reinterpret_cast<T*>(address);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool SafeReadBytes(uintptr_t address, unsigned char* outBytes, size_t size) {
            if (!IsReadableAddress(address, size)) {
                return false;
            }

            __try {
                memcpy(outBytes, reinterpret_cast<void*>(address), size);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool SafeReadVFunc(uintptr_t object, uintptr_t vtableOffset, uintptr_t& outFunction) {
            uintptr_t vtable = 0;
            if (!SafeRead<uintptr_t>(object, vtable) || !IsReadableAddress(vtable + vtableOffset, sizeof(uintptr_t))) {
                return false;
            }

            return SafeRead<uintptr_t>(vtable + vtableOffset, outFunction)
                && IsReadableAddress(outFunction, 1);
        }

        void* SafeCallGetChild(void* object, int index) {
            uintptr_t fn = 0;
            if (!SafeReadVFunc(reinterpret_cast<uintptr_t>(object), 0x74, fn)) {
                return nullptr;
            }

            __try {
                return reinterpret_cast<MovieClipGetChild_t>(fn)(object, index);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return nullptr;
            }
        }

        const char* SafeCallGetTypeName(void* object) {
            uintptr_t fn = 0;
            if (!SafeReadVFunc(reinterpret_cast<uintptr_t>(object), 0x18, fn)) {
                return nullptr;
            }

            __try {
                return reinterpret_cast<MovieClipGetTypeName_t>(fn)(object);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return nullptr;
            }
        }

        bool SafeCallHasProperty(void* object, int propertyId, bool& outValue) {
            uintptr_t fn = 0;
            if (!SafeReadVFunc(reinterpret_cast<uintptr_t>(object), 0x04, fn)) {
                return false;
            }

            __try {
                outValue = reinterpret_cast<MovieClipHasProperty_t>(fn)(object, propertyId);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        uintptr_t GetFlashPropertyStringObjectAddress(int propertyId) {
            uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
            if (moduleBase == 0 || BaseAddress::IsSteamVersion()) {
                return 0;
            }

            if (propertyId == FLASH_PROP_VISIBLE_ID) {
                return moduleBase + Uplay::FLASH_PROP_VISIBLE_STRING;
            }
            if (propertyId == FLASH_PROP_NAME_ID) {
                return moduleBase + Uplay::FLASH_PROP_NAME_STRING;
            }
            return 0;
        }

        void InitBoolVariant(char outVariant[FLASH_VARIANT_SIZE], bool value) {
            memset(outVariant, 0, FLASH_VARIANT_SIZE);
            *reinterpret_cast<uint32_t*>(outVariant) = 1;
            outVariant[0x1f] = 0x0f;
            outVariant[0x20] = value ? 1 : 0;
        }

        bool TryReadBoolVariant(const char variant[FLASH_VARIANT_SIZE], bool& outValue) {
            uint32_t type = *reinterpret_cast<const uint32_t*>(variant);
            if (type != 1) {
                return false;
            }

            outValue = variant[0x20] != 0;
            return true;
        }

        bool SafeGetPropertyVariant(void* object, int propertyId, char outVariant[FLASH_VARIANT_SIZE]) {
            uintptr_t propertyString = GetFlashPropertyStringObjectAddress(propertyId);
            if (propertyString == 0 || !IsReadableAddress(propertyString, FLASH_PROP_STRING_SIZE)) {
                return false;
            }

            uintptr_t fn = 0;
            if (!SafeReadVFunc(reinterpret_cast<uintptr_t>(object), 0x20, fn)) {
                return false;
            }

            memset(outVariant, 0, FLASH_VARIANT_SIZE);
            __try {
                return reinterpret_cast<MovieClipGetProperty_t>(fn)(
                    object,
                    reinterpret_cast<void*>(propertyString),
                    outVariant
                );
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool SafeGetVisibleProperty(void* object, bool& outValue) {
            char variant[FLASH_VARIANT_SIZE] = {};
            return SafeGetPropertyVariant(object, FLASH_PROP_VISIBLE_ID, variant)
                && TryReadBoolVariant(variant, outValue);
        }

        bool SafeSetVisibleProperty(void* object, bool value) {
            uintptr_t visibleString = GetFlashPropertyStringObjectAddress(FLASH_PROP_VISIBLE_ID);
            if (visibleString == 0 || !IsReadableAddress(visibleString, FLASH_PROP_STRING_SIZE)) {
                return false;
            }

            uintptr_t fn = 0;
            if (!SafeReadVFunc(reinterpret_cast<uintptr_t>(object), 0x1c, fn)) {
                return false;
            }

            char variant[FLASH_VARIANT_SIZE] = {};
            InitBoolVariant(variant, value);

            __try {
                reinterpret_cast<MovieClipSetProperty_t>(fn)(
                    object,
                    reinterpret_cast<void*>(visibleString),
                    variant
                );
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool SafeCallVisibilityCandidate(void* object, bool& outValue) {
            uintptr_t fn = 0;
            if (!SafeReadVFunc(reinterpret_cast<uintptr_t>(object), 0x15c, fn)) {
                return false;
            }

            __try {
                outValue = reinterpret_cast<MovieClipVisibilityCandidate_t>(fn)(object);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        std::string TryReadAscii(uintptr_t address) {
            if (!IsReadableAddress(address, 4)) {
                return "";
            }

            char buffer[65] = {};
            size_t count = 0;
            for (; count < sizeof(buffer) - 1; ++count) {
                char ch = 0;
                if (!SafeRead<char>(address + count, ch)) {
                    break;
                }
                if (ch == '\0') {
                    break;
                }
                if (!std::isprint(static_cast<unsigned char>(ch))) {
                    return "";
                }
                buffer[count] = ch;
            }

            if (count < 3) {
                return "";
            }

            return std::string(buffer, count);
        }

        bool TryReadRefCountedStringData(uintptr_t stringData, std::string& outValue) {
            int length = 0;
            uintptr_t textAddress = 0;
            if (SafeRead<int>(stringData + 4, length) && length > 0 && length < 256) {
                textAddress = stringData + 8;
                int capacity = 0;
                if (SafeRead<int>(stringData + 8, capacity) && capacity >= length && capacity < 4096) {
                    textAddress = stringData + 12;
                }
            }
            else {
                return false;
            }

            if (!IsReadableAddress(textAddress, static_cast<size_t>(length))) {
                return false;
            }

            std::string value;
            value.reserve(static_cast<size_t>(length));
            for (int i = 0; i < length; ++i) {
                char ch = 0;
                if (!SafeRead<char>(textAddress + i, ch) || !std::isprint(static_cast<unsigned char>(ch))) {
                    return false;
                }
                value.push_back(ch);
            }

            outValue = value;
            return true;
        }

        bool TryReadGameStringObject(uintptr_t stringObject, std::string& outValue) {
            uintptr_t stringData = 0;
            return SafeRead<uintptr_t>(stringObject, stringData)
                && IsReadableAddress(stringData, 0x0c)
                && TryReadRefCountedStringData(stringData, outValue);
        }

        uintptr_t ResolveNameStringObjectAddress() {
            if (g_nameStringObjectAddress != 0) {
                return g_nameStringObjectAddress;
            }
            if (g_triedResolveNameStringObject) {
                return 0;
            }
            g_triedResolveNameStringObject = true;

            uintptr_t directCandidate = GetFlashPropertyStringObjectAddress(FLASH_PROP_NAME_ID);
            std::string text;
            if (directCandidate != 0
                && IsReadableAddress(directCandidate, FLASH_PROP_STRING_SIZE)
                && TryReadGameStringObject(directCandidate, text)
                && text == "_name") {
                g_nameStringObjectAddress = directCandidate;
                LOG_INFO("[UIViewExplorer] Resolved _name property string at " << Hex(g_nameStringObjectAddress));
                return g_nameStringObjectAddress;
            }

            uintptr_t visibleString = GetFlashPropertyStringObjectAddress(FLASH_PROP_VISIBLE_ID);
            if (visibleString == 0) {
                return 0;
            }

            uintptr_t scanStart = visibleString > 0x400 ? visibleString - 0x400 : visibleString;
            uintptr_t scanEnd = visibleString + 0x1000;
            for (uintptr_t address = scanStart; address < scanEnd; address += sizeof(uintptr_t)) {
                if (!IsReadableAddress(address, FLASH_PROP_STRING_SIZE)) {
                    continue;
                }

                text.clear();
                if (TryReadGameStringObject(address, text) && text == "_name") {
                    g_nameStringObjectAddress = address;
                    LOG_INFO("[UIViewExplorer] Resolved _name property string at " << Hex(g_nameStringObjectAddress));
                    return g_nameStringObjectAddress;
                }
            }

            LOG_WARNING("[UIViewExplorer] Failed to resolve _name property string near _visible");
            return 0;
        }

        bool SafeGetPropertyVariantByStringObject(void* object, uintptr_t propertyString, char outVariant[FLASH_VARIANT_SIZE]) {
            if (propertyString == 0 || !IsReadableAddress(propertyString, FLASH_PROP_STRING_SIZE)) {
                return false;
            }

            uintptr_t fn = 0;
            if (!SafeReadVFunc(reinterpret_cast<uintptr_t>(object), 0x20, fn)) {
                return false;
            }

            memset(outVariant, 0, FLASH_VARIANT_SIZE);
            __try {
                return reinterpret_cast<MovieClipGetProperty_t>(fn)(
                    object,
                    reinterpret_cast<void*>(propertyString),
                    outVariant
                );
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool TryReadStringVariant(const char variant[FLASH_VARIANT_SIZE], std::string& outValue) {
            for (size_t offset = 0; offset + sizeof(uintptr_t) <= FLASH_VARIANT_SIZE; offset += sizeof(uintptr_t)) {
                uintptr_t pointer = 0;
                memcpy(&pointer, variant + offset, sizeof(pointer));
                if (!IsReadableAddress(pointer, sizeof(uintptr_t))) {
                    continue;
                }
                if (TryReadGameStringObject(pointer, outValue) || TryReadRefCountedStringData(pointer, outValue)) {
                    return true;
                }
            }
            return false;
        }

        std::string SafeReadObjectTypeName(uintptr_t object) {
            const char* namePtr = SafeCallGetTypeName(reinterpret_cast<void*>(object));
            if (!namePtr) {
                return "?";
            }

            std::string name = TryReadAscii(reinterpret_cast<uintptr_t>(namePtr));
            return name.empty() ? "?" : name;
        }

        std::string SafeReadObjectInstanceName(uintptr_t object) {
            bool hasNameProperty = false;
            bool hasNamePropertyResult = SafeCallHasProperty(reinterpret_cast<void*>(object), FLASH_PROP_NAME_ID, hasNameProperty);

            char variant[FLASH_VARIANT_SIZE] = {};
            std::string name;
            uintptr_t nameString = ResolveNameStringObjectAddress();
            if (!SafeGetPropertyVariantByStringObject(reinterpret_cast<void*>(object), nameString, variant)
                || !TryReadStringVariant(variant, name)) {
                return (hasNamePropertyResult && !hasNameProperty) ? "<none>" : "?";
            }
            return name;
        }

        void ScanCandidate(uintptr_t address) {
            g_pointerFields.clear();
            g_vtableSlots.clear();
            g_lastResolvedPath.clear();
            g_lastScanAddress = address;

            if (!IsReadableAddress(address, 0x100)) {
                LOG_WARNING("[UIViewExplorer] Candidate address is not readable: " << Hex(address));
                return;
            }

            for (uintptr_t offset = 0; offset <= 0x100; offset += sizeof(uintptr_t)) {
                uintptr_t value = 0;
                if (!SafeRead<uintptr_t>(address + offset, value)) {
                    continue;
                }

                bool readable = IsReadableAddress(value, sizeof(uintptr_t));
                std::string ascii = TryReadAscii(value);
                if (readable || !ascii.empty() || value != 0) {
                    g_pointerFields.push_back({ offset, value, readable, ascii });
                }
            }

            LOG_INFO("[UIViewExplorer] Scanned candidate " << Hex(address) << " pointer-like fields=" << g_pointerFields.size());
        }

        void DumpVTable(uintptr_t object) {
            g_vtableSlots.clear();

            uintptr_t vtable = 0;
            if (!SafeRead<uintptr_t>(object, vtable) || !IsReadableAddress(vtable, sizeof(uintptr_t))) {
                LOG_WARNING("[UIViewExplorer] Could not read vtable for " << Hex(object));
                return;
            }

            LOG_INFO("[UIViewExplorer] VTable dump for object " << Hex(object) << " vtable=" << Hex(vtable));
            for (uintptr_t offset = 0; offset <= 0x180; offset += sizeof(uintptr_t)) {
                uintptr_t function = 0;
                if (!SafeRead<uintptr_t>(vtable + offset, function)) {
                    break;
                }

                bool executable = IsExecutableAddress(function);
                std::string moduleRva = FormatModuleRva(function);
                std::string ghidraAddress = FormatGhidraAddress(function);
                if (executable || !moduleRva.empty()) {
                    g_vtableSlots.push_back({ offset, function, executable, moduleRva, ghidraAddress });
                    LOG_INFO("[UIViewExplorer]   vtable+" << Hex(offset)
                        << " -> " << Hex(function)
                        << (moduleRva.empty() ? "" : (" " + moduleRva))
                        << (ghidraAddress.empty() ? "" : (" ghidra=" + ghidraAddress))
                        << (executable ? " executable" : ""));
                }
            }
        }

        void LogCandidateBytes(uintptr_t address) {
            unsigned char bytes[0x100] = {};
            if (!SafeReadBytes(address, bytes, sizeof(bytes))) {
                LOG_WARNING("[UIViewExplorer] Could not read 0x100 bytes at " << Hex(address));
                return;
            }

            LOG_INFO("[UIViewExplorer] 0x100-byte dump for " << Hex(address));
            for (size_t row = 0; row < sizeof(bytes); row += 16) {
                std::ostringstream oss;
                oss << "[UIViewExplorer] +" << std::hex << std::setw(2) << std::setfill('0') << row << "  ";
                for (size_t col = 0; col < 16; ++col) {
                    oss << std::setw(2) << static_cast<int>(bytes[row + col]) << ' ';
                }
                oss << " ";
                for (size_t col = 0; col < 16; ++col) {
                    unsigned char ch = bytes[row + col];
                    oss << (std::isprint(ch) ? static_cast<char>(ch) : '.');
                }
                LOG_INFO(oss.str());
            }
        }

        void WalkParentOffset(uintptr_t address, uintptr_t offset) {
            g_parentWalkLines.clear();

            if (!IsReadableAddress(address, 0x20)) {
                g_parentWalkLines.push_back("candidate is not readable");
                return;
            }

            uintptr_t current = address;
            std::vector<uintptr_t> seen;
            for (int depth = 0; depth < 16; ++depth) {
                std::ostringstream line;
                line << depth << ": " << Hex(current);

                if (std::find(seen.begin(), seen.end(), current) != seen.end()) {
                    line << " cycle";
                    g_parentWalkLines.push_back(line.str());
                    break;
                }
                seen.push_back(current);

                uintptr_t parent = 0;
                if (!SafeRead<uintptr_t>(current + offset, parent)) {
                    line << " parent unreadable at +" << Hex(offset);
                    g_parentWalkLines.push_back(line.str());
                    break;
                }

                line << " -> " << Hex(parent);
                g_parentWalkLines.push_back(line.str());

                if (parent == 0 || !IsReadableAddress(parent, 0x20)) {
                    break;
                }
                current = parent;
            }

            LOG_INFO("[UIViewExplorer] Parent walk from " << Hex(address) << " using offset +" << Hex(offset));
            for (const std::string& line : g_parentWalkLines) {
                LOG_INFO("[UIViewExplorer]   " << line);
            }
        }

        void ScanParentOffsets(uintptr_t address) {
            LOG_INFO("[UIViewExplorer] Parent offset candidates for " << Hex(address));
            int logged = 0;
            for (uintptr_t offset = 0; offset <= 0x100; offset += sizeof(uintptr_t)) {
                uintptr_t first = 0;
                uintptr_t second = 0;
                if (!SafeRead<uintptr_t>(address + offset, first) || !IsReadableAddress(first, 0x20)) {
                    continue;
                }
                if (!SafeRead<uintptr_t>(first + offset, second)) {
                    continue;
                }

                LOG_INFO("[UIViewExplorer]   +" << Hex(offset) << " " << Hex(address) << " -> " << Hex(first) << " -> " << Hex(second));
                ++logged;
            }
            if (logged == 0) {
                LOG_INFO("[UIViewExplorer]   no readable recursive pointer offsets found in first 0x100 bytes");
            }
        }

        void AdoptAddress(uintptr_t address) {
            g_candidateAddress = address;
            sprintf_s(g_addressText, "0x%08X", static_cast<unsigned int>(address));
            ScanCandidate(address);
        }

        bool ResolveCurrentMovieClipCandidate(uintptr_t& outObject) {
            uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
            if (moduleBase == 0) {
                return false;
            }

            uintptr_t gameManagerRva = BaseAddress::IsSteamVersion()
                ? Steam::GAME_MANAGER_PTR
                : Uplay::GAME_MANAGER_PTR;

            uintptr_t gameManager = 0;
            uintptr_t uiInputObject = 0;
            uintptr_t movieClipWrapper = 0;
            uintptr_t movieClipObject = 0;

            if (!SafeRead<uintptr_t>(moduleBase + gameManagerRva, gameManager)
                || !SafeRead<uintptr_t>(gameManager + 0x1dc, uiInputObject)
                || !SafeRead<uintptr_t>(uiInputObject + 0x04, movieClipWrapper)
                || !SafeRead<uintptr_t>(movieClipWrapper, movieClipObject)
                || !IsReadableAddress(movieClipObject, 0x20)) {
                return false;
            }

            outObject = movieClipObject;
            return true;
        }

        bool IsVisited(const std::vector<uintptr_t>& visited, uintptr_t address) {
            return std::find(visited.begin(), visited.end(), address) != visited.end();
        }

        std::string DescribeMovieClip(uintptr_t object) {
            std::string label = SafeReadObjectTypeName(object);
            std::string instanceName = SafeReadObjectInstanceName(object);
            label += " _name=";
            if (instanceName == "?" || instanceName == "<none>") {
                label += instanceName;
            }
            else {
                label += "\"";
                label += instanceName;
                label += "\"";
            }
            label += " ";
            label += Hex(object);
            return label;
        }

        bool TryReadTaggedPointerField(uintptr_t object, uintptr_t offset, uintptr_t& outPointer);

        bool FindObjectPathRecursive(uintptr_t current, uintptr_t target, int depth, std::vector<uintptr_t>& visited, std::vector<std::string>& path) {
            if (current == 0 || !IsReadableAddress(current, 0x20) || IsVisited(visited, current) || depth > g_maxTreeDepth) {
                return false;
            }

            visited.push_back(current);
            path.push_back(DescribeMovieClip(current));

            if (current == target) {
                return true;
            }

            for (int i = 0; i < g_maxChildrenPerNode; ++i) {
                void* child = SafeCallGetChild(reinterpret_cast<void*>(current), i);
                uintptr_t childAddress = reinterpret_cast<uintptr_t>(child);
                if (childAddress == 0) {
                    break;
                }
                if (FindObjectPathRecursive(childAddress, target, depth + 1, visited, path)) {
                    return true;
                }
            }

            path.pop_back();
            visited.pop_back();
            return false;
        }

        bool TryResolvePathFromRoot(uintptr_t root, uintptr_t target, const char* rootLabel) {
            std::vector<uintptr_t> visited;
            std::vector<std::string> path;
            if (FindObjectPathRecursive(root, target, 0, visited, path)) {
                path.insert(path.begin(), rootLabel);
                g_lastResolvedPath = path;
                LOG_INFO("[UIViewExplorer] Resolved tree path for " << Hex(target));
                for (const std::string& line : g_lastResolvedPath) {
                    LOG_INFO("[UIViewExplorer]   " << line);
                }
                return true;
            }

            return false;
        }

        void ResolvePathFromCurrentRoot(uintptr_t target) {
            g_lastResolvedPath.clear();

            uintptr_t root = 0;
            if (!ResolveCurrentMovieClipCandidate(root)) {
                g_lastResolvedPath.push_back("current MovieClip root unavailable");
                LOG_WARNING("[UIViewExplorer] Cannot resolve path; current MovieClip root unavailable");
                return;
            }

            if (TryResolvePathFromRoot(root, target, "current MovieClip root")) {
                return;
            }

            uintptr_t childContainer = 0;
            if (TryReadTaggedPointerField(root, 0x18, childContainer)
                && TryResolvePathFromRoot(childContainer, target, "current MovieClip +0x18 child container")) {
                return;
            }

            uintptr_t focusedNode = 0;
            if (TryReadTaggedPointerField(root, 0x8c, focusedNode)
                && TryResolvePathFromRoot(focusedNode, target, "current MovieClip +0x8c focused/current node")) {
                return;
            }

            g_lastResolvedPath.push_back("not found from current MovieClip/controller roots within current depth/child limits");
            LOG_WARNING("[UIViewExplorer] Could not resolve tree path for " << Hex(target));
        }

        bool TryReadTaggedPointerField(uintptr_t object, uintptr_t offset, uintptr_t& outPointer) {
            uintptr_t taggedPointer = 0;
            if (!SafeRead<uintptr_t>(object + offset, taggedPointer)) {
                return false;
            }

            outPointer = taggedPointer & ~static_cast<uintptr_t>(3);
            return IsReadableAddress(outPointer, 0x20);
        }

        void RenderMovieClipNode(uintptr_t object, int depth, std::vector<uintptr_t>& visited) {
            if (object == 0 || !IsReadableAddress(object, 0x20)) {
                return;
            }

            std::string typeName = SafeReadObjectTypeName(object);
            std::string instanceName = SafeReadObjectInstanceName(object);
            bool hasVisibleProperty = false;
            bool hasVisiblePropertyResult = SafeCallHasProperty(reinterpret_cast<void*>(object), FLASH_PROP_VISIBLE_ID, hasVisibleProperty);
            bool visible = false;
            bool hasVisibleValue = SafeGetVisibleProperty(reinterpret_cast<void*>(object), visible);
            bool cycle = IsVisited(visited, object);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (cycle || depth >= g_maxTreeDepth) {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }

            ImGui::PushID(reinterpret_cast<void*>(object));

            bool checkboxValue = hasVisibleValue ? visible : false;
            if (!hasVisibleValue) {
                ImGui::BeginDisabled();
            }

            if (ImGui::Checkbox("##visible", &checkboxValue) && hasVisibleValue) {
                if (!SafeSetVisibleProperty(reinterpret_cast<void*>(object), checkboxValue)) {
                    LOG_WARNING("[UIViewExplorer] Failed to set _visible for " << Hex(object));
                }
                else {
                    LOG_INFO("[UIViewExplorer] Set _visible=" << (checkboxValue ? "true" : "false") << " for " << Hex(object));
                }
            }

            if (!hasVisibleValue) {
                ImGui::EndDisabled();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("_visible=%s, has _visible prop=%s",
                    hasVisibleValue ? (visible ? "true" : "false") : "unknown",
                    hasVisiblePropertyResult ? (hasVisibleProperty ? "true" : "false") : "unknown");
            }

            ImGui::SameLine();

            std::string label = typeName;
            label += " _name=";
            if (instanceName == "?" || instanceName == "<none>") {
                label += instanceName;
            }
            else {
                label += "\"";
                label += instanceName;
                label += "\"";
            }
            label += " ";
            label += Hex(object);
            if (cycle) {
                label += " (cycle)";
            }

            bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(object), flags, "%s", label.c_str());

            if (!open) {
                ImGui::PopID();
                return;
            }

            if (!cycle && depth < g_maxTreeDepth) {
                visited.push_back(object);
                int childCount = 0;
                for (int i = 0; i < g_maxChildrenPerNode; ++i) {
                    void* child = SafeCallGetChild(reinterpret_cast<void*>(object), i);
                    uintptr_t childAddress = reinterpret_cast<uintptr_t>(child);
                    if (childAddress == 0) {
                        break;
                    }
                    RenderMovieClipNode(childAddress, depth + 1, visited);
                    ++childCount;
                }

                if (childCount == 0) {
                    ImGui::TextDisabled("no children");
                }
                visited.pop_back();
            }

            ImGui::TreePop();
            ImGui::PopID();
        }

        void RenderControllerTree(uintptr_t controller) {
            uintptr_t childContainer = 0;
            uintptr_t focusedNode = 0;

            if (TryReadTaggedPointerField(controller, 0x18, childContainer)) {
                ImGui::TextDisabled("controller +0x18 child container: %s", Hex(childContainer).c_str());
                std::vector<uintptr_t> visited;
                RenderMovieClipNode(childContainer, 0, visited);
            }
            else {
                ImGui::TextDisabled("controller +0x18 child container: unavailable");
            }

            if (TryReadTaggedPointerField(controller, 0x8c, focusedNode)) {
                ImGui::Separator();
                ImGui::TextDisabled("controller +0x8c focused/current node: %s", Hex(focusedNode).c_str());
                std::vector<uintptr_t> visited;
                RenderMovieClipNode(focusedNode, 0, visited);
            }
        }
    }

    void RenderImGui() {
        if (!g_triedInitialMovieClipAdopt && g_candidateAddress == 0) {
            g_triedInitialMovieClipAdopt = true;
            uintptr_t movieClipObject = 0;
            if (ResolveCurrentMovieClipCandidate(movieClipObject)) {
                AdoptAddress(movieClipObject);
                LOG_INFO("[UIViewExplorer] Auto-adopted initial movieclip candidate: " << Hex(movieClipObject));
            }
        }

        ImGui::TextDisabled("Read-only AVM1/UI object probe");
        ImGui::TextDisabled("Known AVM1 property ids: _visible=7, _name=0x0d, _parent=0x15, _root=0x20");
        ImGui::Checkbox("Auto scan", &g_autoScan);

        ImGui::PushItemWidth(190.0f);
        bool changed = ImGui::InputText("Address", g_addressText, sizeof(g_addressText), ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopItemWidth();

        uintptr_t parsedAddress = 0;
        bool hasAddress = ParseHex(g_addressText, parsedAddress);
        if (changed && hasAddress) {
            g_candidateAddress = parsedAddress;
            if (g_autoScan) {
                ScanCandidate(g_candidateAddress);
            }
        }

        if (ImGui::Button("Scan") && hasAddress) {
            AdoptAddress(parsedAddress);
        }
        ImGui::SameLine();
        if (ImGui::Button("Current MovieClip")) {
            uintptr_t movieClipObject = 0;
            if (ResolveCurrentMovieClipCandidate(movieClipObject)) {
                AdoptAddress(movieClipObject);
                LOG_INFO("[UIViewExplorer] Adopted current movieclip candidate: " << Hex(movieClipObject));
            }
            else {
                LOG_WARNING("[UIViewExplorer] Failed to resolve current movieclip candidate");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Message Handler")) {
            void* handler = ActionScript::GetMessageHandler();
            if (handler) {
                AdoptAddress(reinterpret_cast<uintptr_t>(handler));
                LOG_INFO("[UIViewExplorer] Adopted ActionScript message handler as candidate: " << Hex(g_candidateAddress));
            }
            else {
                LOG_WARNING("[UIViewExplorer] ActionScript message handler is null");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Dump Bytes") && hasAddress) {
            LogCandidateBytes(parsedAddress);
        }
        ImGui::SameLine();
        if (ImGui::Button("Dump VTable") && hasAddress) {
            DumpVTable(parsedAddress);
        }
        ImGui::SameLine();
        if (ImGui::Button("Find Path") && hasAddress) {
            ResolvePathFromCurrentRoot(parsedAddress);
        }

        ImGui::PushItemWidth(80.0f);
        ImGui::InputText("Parent +", g_parentOffsetText, sizeof(g_parentOffsetText), ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopItemWidth();
        uintptr_t parentOffset = 0;
        bool hasParentOffset = ParseHex(g_parentOffsetText, parentOffset);
        if (ImGui::Button("Walk Parent") && hasAddress && hasParentOffset) {
            WalkParentOffset(parsedAddress, parentOffset);
        }
        ImGui::SameLine();
        if (ImGui::Button("Scan Parent Offsets") && hasAddress) {
            ScanParentOffsets(parsedAddress);
        }

        if (!hasAddress) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Enter a hex address from CE/Ghidra.");
            return;
        }

        bool readable = IsReadableAddress(parsedAddress, 0x20);
        ImGui::Text("Candidate: %s  %s", Hex(parsedAddress).c_str(), readable ? "readable" : "unreadable");
        std::string candidateRva = FormatModuleRva(parsedAddress);
        if (!candidateRva.empty()) {
            ImGui::TextDisabled("%s", candidateRva.c_str());
        }

        ImGui::Separator();
        ImGui::Checkbox("MovieClip tree", &g_showMovieClipTree);
        ImGui::SameLine();
        ImGui::PushItemWidth(90.0f);
        ImGui::InputInt("Depth", &g_maxTreeDepth);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::PushItemWidth(90.0f);
        ImGui::InputInt("Children", &g_maxChildrenPerNode);
        ImGui::PopItemWidth();
        g_maxTreeDepth = (std::max)(1, (std::min)(g_maxTreeDepth, 32));
        g_maxChildrenPerNode = (std::max)(1, (std::min)(g_maxChildrenPerNode, 1024));

        if (g_showMovieClipTree && readable) {
            if (ImGui::CollapsingHeader("Controller tree", ImGuiTreeNodeFlags_DefaultOpen)) {
                RenderControllerTree(parsedAddress);
            }
        }

        if (g_lastScanAddress == parsedAddress && !g_pointerFields.empty()) {
            if (ImGui::BeginTable("ui_view_pointer_fields", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 220.0f))) {
                ImGui::TableSetupColumn("Offset");
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Readable");
                ImGui::TableSetupColumn("ASCII");
                ImGui::TableHeadersRow();

                for (const PointerField& field : g_pointerFields) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("+%s", Hex(field.offset).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", Hex(field.value).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%s", field.readable ? "yes" : "no");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(field.ascii.c_str());
                }

                ImGui::EndTable();
            }
        }

        if (!g_vtableSlots.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Last vtable dump");
            if (ImGui::BeginTable("ui_view_vtable_slots", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 220.0f))) {
                ImGui::TableSetupColumn("Slot");
                ImGui::TableSetupColumn("Function");
                ImGui::TableSetupColumn("Executable");
                ImGui::TableSetupColumn("Main module RVA");
                ImGui::TableSetupColumn("Ghidra");
                ImGui::TableHeadersRow();

                for (const VTableSlot& slot : g_vtableSlots) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("+%s", Hex(slot.offset).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", Hex(slot.function).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%s", slot.executable ? "yes" : "no");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(slot.moduleRva.c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(slot.ghidraAddress.c_str());
                }

                ImGui::EndTable();
            }
        }

        if (!g_lastResolvedPath.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Last resolved tree path");
            for (size_t i = 0; i < g_lastResolvedPath.size(); ++i) {
                ImGui::Text("%u: %s", static_cast<unsigned int>(i), g_lastResolvedPath[i].c_str());
            }
        }

        if (!g_parentWalkLines.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Last parent walk");
            for (const std::string& line : g_parentWalkLines) {
                ImGui::TextUnformatted(line.c_str());
            }
        }
    }
}
