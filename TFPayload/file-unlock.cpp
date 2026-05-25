#include "pch.h"
#include "file-unlock.h"
#include "base-address.h"
#include "logging.h"
#include "miniz/miniz.h"

#include <Windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(linker, "/EXPORT:RunFileUnlockPatch=_RunFileUnlockPatch@16")

namespace FileUnlock {
namespace {
    constexpr uint32_t kPakMagic = 0x12345678;
    constexpr size_t kPakHeaderSize = 12;
    constexpr size_t kPakEntrySize = 17;
    constexpr size_t kRsaBlockSize = 128;
    constexpr size_t kAesKeySize = 32;
    constexpr uint8_t kGcmIv[] = {
        '5', '4', '8', '4', '2', '5', '8', '4',
        '6', '1', '0', '5', '6', '4', '9', '6', 0
    };
    constexpr uint8_t kRsaModulus[kRsaBlockSize] = {
        0xac, 0x7b, 0xbb, 0xe3, 0x6e, 0xdf, 0x9e, 0xd7, 0x5e, 0x35, 0x13, 0x56, 0x9d, 0xad, 0x2d, 0x89,
        0x08, 0x1e, 0xb3, 0x3d, 0x9c, 0xfb, 0x02, 0x2e, 0xca, 0xbe, 0x1d, 0x39, 0x44, 0xb5, 0xe2, 0x6d,
        0x43, 0xe4, 0xd3, 0xa2, 0xb6, 0xc4, 0x66, 0xa5, 0xc7, 0xfc, 0x93, 0x98, 0xf4, 0x79, 0xd7, 0x89,
        0x47, 0xc5, 0x7f, 0x21, 0xc6, 0x61, 0x39, 0xef, 0x00, 0xd4, 0xbd, 0x9e, 0x8b, 0x65, 0xac, 0xe5,
        0xf3, 0x15, 0x81, 0x8d, 0x0a, 0x03, 0x06, 0x95, 0x72, 0x5c, 0x5f, 0xf4, 0xd9, 0x89, 0xbb, 0x04,
        0xd3, 0xde, 0x74, 0xb8, 0xf3, 0x38, 0x23, 0xa6, 0x62, 0x02, 0xac, 0x9c, 0x0d, 0x44, 0xd7, 0x28,
        0x45, 0x71, 0x3a, 0xb3, 0xa4, 0x0b, 0x9c, 0x14, 0x1a, 0x0d, 0xe3, 0x84, 0x2f, 0x0f, 0x45, 0x25,
        0x8f, 0xfd, 0x57, 0x17, 0xc3, 0xf9, 0x7b, 0xa5, 0x0e, 0xc1, 0xd2, 0x4a, 0xca, 0xf3, 0xf2, 0x71
    };

    struct PakEntry {
        uint32_t pathHash = 0;
        uint32_t storedSize = 0;
        uint32_t unpackedSize = 0;
        uint8_t flags = 0;
        uint32_t dataOffset = 0;
    };

    struct LoadedPak {
        std::vector<uint8_t> data;
        std::vector<PakEntry> entries;
    };

    struct PatchOptions {
        std::string sourcePath;
        std::string outputPath;
        std::string installTarget;
        std::string launcherUri;
        std::string restartExe;
        std::string restartCwd;
        std::string logPath;
        DWORD waitPid = 0;
        DWORD waitTimeoutSeconds = 60;
        DWORD restartDelaySeconds = 8;
    };

    uint32_t ReadU32(const std::vector<uint8_t>& data, size_t offset) {
        if (offset + 4 > data.size()) {
            throw std::runtime_error("unexpected EOF while reading u32");
        }
        return static_cast<uint32_t>(data[offset])
            | (static_cast<uint32_t>(data[offset + 1]) << 8)
            | (static_cast<uint32_t>(data[offset + 2]) << 16)
            | (static_cast<uint32_t>(data[offset + 3]) << 24);
    }

    uint16_t ReadU16(const std::vector<uint8_t>& data, size_t offset) {
        if (offset + 2 > data.size()) {
            throw std::runtime_error("unexpected EOF while reading u16");
        }
        return static_cast<uint16_t>(data[offset])
            | (static_cast<uint16_t>(data[offset + 1]) << 8);
    }

    void WriteU32(std::vector<uint8_t>& out, uint32_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xff));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    }

    void WriteEntry(std::vector<uint8_t>& out, const PakEntry& entry) {
        WriteU32(out, entry.pathHash);
        WriteU32(out, entry.storedSize);
        WriteU32(out, entry.unpackedSize);
        out.push_back(entry.flags);
        WriteU32(out, entry.dataOffset);
    }

    std::string DirectoryOf(const std::string& path) {
        const size_t slash = path.find_last_of("\\/");
        return slash == std::string::npos ? "." : path.substr(0, slash);
    }

    std::string FullPathOf(const std::string& path) {
        char buffer[MAX_PATH] = {};
        const DWORD length = GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr);
        return (length == 0 || length >= MAX_PATH) ? path : std::string(buffer, length);
    }

    bool FileExists(const std::string& path) {
        const DWORD attrs = GetFileAttributesA(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    bool GetCurrentExePath(std::string& outPath) {
        char buffer[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return false;
        }
        outPath.assign(buffer, length);
        return true;
    }

    bool GetThisModulePath(std::string& outPath) {
        HMODULE module = nullptr;
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&GetThisModulePath),
                &module)) {
            return false;
        }

        char buffer[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameA(module, buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return false;
        }
        outPath.assign(buffer, length);
        return true;
    }

    std::vector<uint8_t> ReadFileBytes(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("failed to open input: " + path);
        }
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size < 0) {
            throw std::runtime_error("failed to size input: " + path);
        }
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(static_cast<size_t>(size));
        if (!data.empty()) {
            file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        if (!file && !data.empty()) {
            throw std::runtime_error("failed to read input: " + path);
        }
        return data;
    }

    void WriteFileBytes(const std::string& path, const std::vector<uint8_t>& data) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("failed to open output: " + path);
        }
        if (!data.empty()) {
            file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        if (!file) {
            throw std::runtime_error("failed to write output: " + path);
        }
    }

    std::string QuoteArg(const std::string& value) {
        std::string quoted = "\"";
        unsigned int backslashes = 0;
        for (char ch : value) {
            if (ch == '\\') {
                ++backslashes;
                continue;
            }
            if (ch == '"') {
                quoted.append(backslashes * 2 + 1, '\\');
                quoted.push_back(ch);
                backslashes = 0;
                continue;
            }
            quoted.append(backslashes, '\\');
            backslashes = 0;
            quoted.push_back(ch);
        }
        quoted.append(backslashes * 2, '\\');
        quoted.push_back('"');
        return quoted;
    }

    bool StartProcess(const std::string& commandLine, const std::string& workingDirectory) {
        STARTUPINFOA startupInfo = {};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo = {};
        std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back('\0');

        const BOOL started = CreateProcessA(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
            &startupInfo,
            &processInfo);
        if (!started) {
            return false;
        }

        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return true;
    }

    std::vector<std::string> ParseArgs(const std::string& commandLine) {
        std::vector<std::string> args;
        std::string current;
        bool inQuote = false;
        for (size_t i = 0; i < commandLine.size(); ++i) {
            const char ch = commandLine[i];
            if (ch == '"') {
                inQuote = !inQuote;
                continue;
            }
            if (!inQuote && (ch == ' ' || ch == '\t')) {
                if (!current.empty()) {
                    args.push_back(current);
                    current.clear();
                }
                continue;
            }
            current.push_back(ch);
        }
        if (!current.empty()) {
            args.push_back(current);
        }
        return args;
    }

    std::string RequireArg(const std::vector<std::string>& args, const char* name) {
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == name) {
                return args[i + 1];
            }
        }
        throw std::runtime_error(std::string("missing argument ") + name);
    }

    std::string OptionalArg(const std::vector<std::string>& args, const char* name) {
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == name) {
                return args[i + 1];
            }
        }
        return {};
    }

    DWORD OptionalDwordArg(const std::vector<std::string>& args, const char* name, DWORD defaultValue) {
        const std::string value = OptionalArg(args, name);
        return value.empty() ? defaultValue : static_cast<DWORD>(std::stoul(value));
    }

    PatchOptions ParsePatchOptions(const std::string& commandLine) {
        const std::vector<std::string> args = ParseArgs(commandLine);
        PatchOptions options;
        options.sourcePath = RequireArg(args, "--source");
        options.outputPath = RequireArg(args, "--output");
        options.installTarget = RequireArg(args, "--install-target");
        options.logPath = RequireArg(args, "--log");
        options.waitPid = OptionalDwordArg(args, "--wait-pid", 0);
        options.waitTimeoutSeconds = OptionalDwordArg(args, "--wait-timeout", 60);
        options.restartDelaySeconds = OptionalDwordArg(args, "--restart-delay", 8);
        options.launcherUri = OptionalArg(args, "--launcher-uri");
        options.restartExe = OptionalArg(args, "--restart-exe");
        options.restartCwd = OptionalArg(args, "--restart-cwd");
        return options;
    }

    LoadedPak ReadPak(const std::string& path) {
        LoadedPak pak;
        pak.data = ReadFileBytes(path);
        if (pak.data.size() < kPakHeaderSize) {
            throw std::runtime_error("pak is too small");
        }
        const uint32_t magic = ReadU32(pak.data, 0);
        const uint32_t dataStart = ReadU32(pak.data, 4);
        const uint32_t entryCount = ReadU32(pak.data, 8);
        if (magic != kPakMagic) {
            throw std::runtime_error("pak magic mismatch");
        }
        const uint32_t expectedDataStart = static_cast<uint32_t>(kPakHeaderSize + entryCount * kPakEntrySize);
        if (dataStart != expectedDataStart || dataStart > pak.data.size()) {
            throw std::runtime_error("invalid pak entry table");
        }

        pak.entries.reserve(entryCount);
        for (uint32_t i = 0; i < entryCount; ++i) {
            const size_t offset = kPakHeaderSize + i * kPakEntrySize;
            PakEntry entry;
            entry.pathHash = ReadU32(pak.data, offset);
            entry.storedSize = ReadU32(pak.data, offset + 4);
            entry.unpackedSize = ReadU32(pak.data, offset + 8);
            entry.flags = pak.data[offset + 12];
            entry.dataOffset = ReadU32(pak.data, offset + 13);
            if (entry.dataOffset > pak.data.size() || entry.storedSize > pak.data.size() - entry.dataOffset) {
                throw std::runtime_error("pak entry points outside file");
            }
            pak.entries.push_back(entry);
        }
        return pak;
    }

    std::vector<uint8_t> StoredPayload(const LoadedPak& pak, const PakEntry& entry) {
        return std::vector<uint8_t>(
            pak.data.begin() + entry.dataOffset,
            pak.data.begin() + entry.dataOffset + entry.storedSize);
    }

    std::vector<uint8_t> AesEncryptBlock(const std::vector<uint8_t>& key, const uint8_t block[16]) {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_KEY_HANDLE keyHandle = nullptr;
        std::vector<uint8_t> objectBuffer;
        std::vector<uint8_t> out(16);
        try {
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0) {
                throw std::runtime_error("BCryptOpenAlgorithmProvider AES failed");
            }
            if (BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                    reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
                    static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_ECB) + 1) * sizeof(wchar_t)), 0) < 0) {
                throw std::runtime_error("BCryptSetProperty AES ECB failed");
            }

            DWORD objectLength = 0;
            DWORD resultLength = 0;
            if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) < 0) {
                throw std::runtime_error("BCryptGetProperty AES object length failed");
            }
            objectBuffer.resize(objectLength);
            if (BCryptGenerateSymmetricKey(algorithm, &keyHandle, objectBuffer.data(), objectLength,
                    const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0) < 0) {
                throw std::runtime_error("BCryptGenerateSymmetricKey failed");
            }
            ULONG bytesDone = 0;
            if (BCryptEncrypt(keyHandle, const_cast<PUCHAR>(block), 16, nullptr, nullptr, 0,
                    out.data(), static_cast<ULONG>(out.size()), &bytesDone, 0) < 0 || bytesDone != 16) {
                throw std::runtime_error("BCryptEncrypt AES block failed");
            }
        }
        catch (...) {
            if (keyHandle) BCryptDestroyKey(keyHandle);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
            throw;
        }
        BCryptDestroyKey(keyHandle);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return out;
    }

    std::vector<uint8_t> RsaPublicUnwrapKey(const std::vector<uint8_t>& block) {
        if (block.size() != kRsaBlockSize) {
            throw std::runtime_error("invalid RSA block size");
        }

        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_KEY_HANDLE keyHandle = nullptr;
        std::vector<uint8_t> blob(sizeof(BCRYPT_RSAKEY_BLOB) + 3 + kRsaBlockSize);
        try {
            auto* header = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(blob.data());
            header->Magic = BCRYPT_RSAPUBLIC_MAGIC;
            header->BitLength = 1024;
            header->cbPublicExp = 3;
            header->cbModulus = static_cast<ULONG>(kRsaBlockSize);
            header->cbPrime1 = 0;
            header->cbPrime2 = 0;
            uint8_t* cursor = blob.data() + sizeof(BCRYPT_RSAKEY_BLOB);
            cursor[0] = 0x01;
            cursor[1] = 0x00;
            cursor[2] = 0x01;
            memcpy(cursor + 3, kRsaModulus, kRsaBlockSize);

            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) < 0) {
                throw std::runtime_error("BCryptOpenAlgorithmProvider RSA failed");
            }
            if (BCryptImportKeyPair(algorithm, nullptr, BCRYPT_RSAPUBLIC_BLOB, &keyHandle,
                    blob.data(), static_cast<ULONG>(blob.size()), 0) < 0) {
                throw std::runtime_error("BCryptImportKeyPair RSA failed");
            }

            std::vector<uint8_t> plain(kRsaBlockSize);
            ULONG bytesDone = 0;
            if (BCryptEncrypt(keyHandle, const_cast<PUCHAR>(block.data()), static_cast<ULONG>(block.size()),
                    nullptr, nullptr, 0, plain.data(), static_cast<ULONG>(plain.size()), &bytesDone, BCRYPT_PAD_NONE) < 0) {
                throw std::runtime_error("BCryptEncrypt RSA public operation failed");
            }
            if (bytesDone != kRsaBlockSize || plain[0] != 0x00 || plain[1] != 0x01) {
                throw std::runtime_error("RSA block has invalid PKCS#1 header");
            }

            size_t delimiter = 2;
            while (delimiter < plain.size() && plain[delimiter] == 0xff) {
                ++delimiter;
            }
            if (delimiter >= plain.size() || plain[delimiter] != 0x00 || plain.size() - delimiter - 1 != kAesKeySize) {
                throw std::runtime_error("RSA block has invalid PKCS#1 padding");
            }
            return std::vector<uint8_t>(plain.begin() + delimiter + 1, plain.end());
        }
        catch (...) {
            if (keyHandle) BCryptDestroyKey(keyHandle);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
            throw;
        }
    }

    void XorBlock(uint8_t out[16], const uint8_t left[16], const uint8_t right[16]) {
        for (size_t i = 0; i < 16; ++i) {
            out[i] = left[i] ^ right[i];
        }
    }

    void ShiftRightOne(uint8_t block[16]) {
        uint8_t carry = 0;
        for (size_t i = 0; i < 16; ++i) {
            const uint8_t nextCarry = block[i] & 1;
            block[i] = static_cast<uint8_t>((block[i] >> 1) | (carry << 7));
            carry = nextCarry;
        }
    }

    void GhashMultiply(uint8_t x[16], const uint8_t h[16]) {
        uint8_t z[16] = {};
        uint8_t v[16] = {};
        memcpy(v, h, 16);
        for (int bit = 0; bit < 128; ++bit) {
            if (x[bit / 8] & (0x80 >> (bit % 8))) {
                for (size_t i = 0; i < 16; ++i) {
                    z[i] ^= v[i];
                }
            }
            const bool lsb = (v[15] & 1) != 0;
            ShiftRightOne(v);
            if (lsb) {
                v[0] ^= 0xe1;
            }
        }
        memcpy(x, z, 16);
    }

    void GhashUpdate(uint8_t y[16], const uint8_t h[16], const uint8_t block[16]) {
        uint8_t x[16] = {};
        XorBlock(x, y, block);
        GhashMultiply(x, h);
        memcpy(y, x, 16);
    }

    std::vector<uint8_t> AesGcmUpdateOnly(const std::vector<uint8_t>& key, const std::vector<uint8_t>& ciphertext) {
        const uint8_t zero[16] = {};
        const std::vector<uint8_t> hVec = AesEncryptBlock(key, zero);
        uint8_t h[16] = {};
        memcpy(h, hVec.data(), 16);

        uint8_t y[16] = {};
        for (size_t offset = 0; offset < sizeof(kGcmIv); offset += 16) {
            uint8_t block[16] = {};
            const size_t count = std::min<size_t>(16, sizeof(kGcmIv) - offset);
            memcpy(block, kGcmIv + offset, count);
            GhashUpdate(y, h, block);
        }

        uint8_t lengthBlock[16] = {};
        const uint64_t ivBits = static_cast<uint64_t>(sizeof(kGcmIv)) * 8;
        for (int i = 0; i < 8; ++i) {
            lengthBlock[15 - i] = static_cast<uint8_t>((ivBits >> (i * 8)) & 0xff);
        }
        GhashUpdate(y, h, lengthBlock);

        std::vector<uint8_t> plaintext(ciphertext.size());
        uint8_t counter[16] = {};
        memcpy(counter, y, 16);
        for (size_t offset = 0; offset < ciphertext.size(); offset += 16) {
            for (int i = 15; i >= 12; --i) {
                counter[i] = static_cast<uint8_t>(counter[i] + 1);
                if (counter[i] != 0) {
                    break;
                }
            }
            const std::vector<uint8_t> stream = AesEncryptBlock(key, counter);
            const size_t count = std::min<size_t>(16, ciphertext.size() - offset);
            for (size_t i = 0; i < count; ++i) {
                plaintext[offset + i] = ciphertext[offset + i] ^ stream[i];
            }
        }
        return plaintext;
    }

    std::vector<uint8_t> DecodeEntry(const LoadedPak& pak, const PakEntry& entry) {
        std::vector<uint8_t> working = StoredPayload(pak, entry);
        if (entry.flags & 0x10) {
            if (working.size() < kRsaBlockSize) {
                throw std::runtime_error("protected payload is smaller than RSA block");
            }
            const std::vector<uint8_t> rsaBlock(working.begin(), working.begin() + kRsaBlockSize);
            const std::vector<uint8_t> key = RsaPublicUnwrapKey(rsaBlock);
            const std::vector<uint8_t> encrypted(working.begin() + kRsaBlockSize, working.end());
            working = AesGcmUpdateOnly(key, encrypted);
        }

        if (entry.flags & 0x07) {
            std::vector<uint8_t> decoded(entry.unpackedSize);
            mz_ulong decodedSize = static_cast<mz_ulong>(decoded.size());
            const int result = mz_uncompress(decoded.data(), &decodedSize, working.data(), static_cast<mz_ulong>(working.size()));
            if (result != MZ_OK || decodedSize != entry.unpackedSize) {
                throw std::runtime_error("zlib decode failed");
            }
            return decoded;
        }

        if (working.size() < entry.unpackedSize) {
            throw std::runtime_error("raw payload shorter than unpacked size");
        }
        working.resize(entry.unpackedSize);
        return working;
    }

    std::vector<std::string> ParseNameList(const std::vector<uint8_t>& raw) {
        const uint32_t count = ReadU32(raw, 0);
        size_t offset = 4;
        std::vector<std::string> names;
        names.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const uint16_t size = ReadU16(raw, offset);
            offset += 2;
            if (offset + size > raw.size()) {
                throw std::runtime_error("truncated filename list");
            }
            names.emplace_back(reinterpret_cast<const char*>(raw.data() + offset), size);
            offset += size;
        }
        return names;
    }

    bool EndsWithItemsXml(std::string name) {
        std::replace(name.begin(), name.end(), '\\', '/');
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
            return static_cast<char>(tolower(ch));
        });
        return name.size() >= 9 && name.compare(name.size() - 9, 9, "items.xml") == 0;
    }

    size_t RemoveAttribute(std::string& text, const std::string& attribute) {
        const std::string needle = attribute + "=\"1\"";
        size_t count = 0;
        size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos) {
            size_t eraseStart = pos;
            while (eraseStart > 0 && (text[eraseStart - 1] == ' ' || text[eraseStart - 1] == '\t')) {
                --eraseStart;
            }
            text.erase(eraseStart, pos + needle.size() - eraseStart);
            pos = eraseStart;
            ++count;
        }
        return count;
    }

    std::vector<uint8_t> PatchItemsXml(const std::vector<uint8_t>& raw, size_t& alwaysLocked, size_t& hidden, size_t& storeProduct) {
        std::string text(raw.begin(), raw.end());
        alwaysLocked = RemoveAttribute(text, "alwaysLocked");
        hidden = RemoveAttribute(text, "hidden");
        storeProduct = RemoveAttribute(text, "isInGameStoreProduct");
        storeProduct += RemoveAttribute(text, "isIngameStoreProduct");
        storeProduct += RemoveAttribute(text, "inGameStoreProduct");
        storeProduct += RemoveAttribute(text, "ingameStoreProduct");
        return std::vector<uint8_t>(text.begin(), text.end());
    }

    void BuildPatchedPak(const std::string& inputPath, const std::string& outputPath, std::ostream& log) {
        const LoadedPak pak = ReadPak(inputPath);
        if (pak.entries.empty()) {
            throw std::runtime_error("pak has no entries");
        }

        const std::vector<uint8_t> nameListPayload = DecodeEntry(pak, pak.entries.back());
        const std::vector<std::string> names = ParseNameList(nameListPayload);
        if (names.size() + 1 != pak.entries.size()) {
            throw std::runtime_error("filename list count does not match entry table");
        }

        size_t itemIndex = names.size();
        for (size_t i = 0; i < names.size(); ++i) {
            if (EndsWithItemsXml(names[i])) {
                itemIndex = i;
                break;
            }
        }
        if (itemIndex >= names.size()) {
            throw std::runtime_error("items.xml not found");
        }

        size_t alwaysLocked = 0;
        size_t hidden = 0;
        size_t storeProduct = 0;
        const std::vector<uint8_t> decodedItems = DecodeEntry(pak, pak.entries[itemIndex]);
        const std::vector<uint8_t> patchedItems = PatchItemsXml(decodedItems, alwaysLocked, hidden, storeProduct);

        std::vector<std::vector<uint8_t>> payloads;
        std::vector<PakEntry> outputEntries = pak.entries;
        payloads.reserve(pak.entries.size());
        for (size_t i = 0; i < pak.entries.size(); ++i) {
            if (i == itemIndex) {
                payloads.push_back(patchedItems);
                outputEntries[i].storedSize = static_cast<uint32_t>(patchedItems.size());
                outputEntries[i].unpackedSize = static_cast<uint32_t>(patchedItems.size());
                outputEntries[i].flags = 0x00;
            }
            else {
                payloads.push_back(StoredPayload(pak, pak.entries[i]));
            }
        }

        uint32_t cursor = static_cast<uint32_t>(kPakHeaderSize + outputEntries.size() * kPakEntrySize);
        for (size_t i = 0; i < outputEntries.size(); ++i) {
            outputEntries[i].dataOffset = cursor;
            cursor += static_cast<uint32_t>(payloads[i].size());
        }

        std::vector<uint8_t> out;
        out.reserve(cursor);
        WriteU32(out, kPakMagic);
        WriteU32(out, static_cast<uint32_t>(kPakHeaderSize + outputEntries.size() * kPakEntrySize));
        WriteU32(out, static_cast<uint32_t>(outputEntries.size()));
        for (const PakEntry& entry : outputEntries) {
            WriteEntry(out, entry);
        }
        for (const auto& payload : payloads) {
            out.insert(out.end(), payload.begin(), payload.end());
        }
        WriteFileBytes(outputPath, out);

        const size_t protectedCount = std::count_if(pak.entries.begin(), pak.entries.end(), [](const PakEntry& entry) {
            return (entry.flags & 0x10) != 0;
        });
        log << "read:  " << inputPath << "\n";
        log << "wrote: " << outputPath << "\n";
        log << "entries: " << pak.entries.size() << " (" << names.size() << " named resources + filename list)\n";
        log << "decoded protected entries: " << protectedCount << "\n";
        log << "items.xml entry: " << names[itemIndex] << "\n";
        log << "items.xml unlock edits: " << alwaysLocked << " alwaysLocked, "
            << hidden << " hidden, " << storeProduct << " isInGameStoreProduct\n";
        log << "output resource flags: one raw patched items.xml entry; other entries copied unchanged\n";
        log << "footer: omitted\n";
    }

    void WaitForProcessExit(DWORD pid, DWORD timeoutSeconds) {
        if (pid == 0) {
            return;
        }
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (!process) {
            return;
        }
        const DWORD waitResult = WaitForSingleObject(process, timeoutSeconds * 1000);
        CloseHandle(process);
        if (waitResult == WAIT_TIMEOUT) {
            throw std::runtime_error("game process did not exit before timeout");
        }
        if (waitResult == WAIT_FAILED) {
            throw std::runtime_error("failed while waiting for game process");
        }
    }

    std::string InstallPatchedPak(const std::string& outputPath, const std::string& installTarget) {
        if (!FileExists(outputPath)) {
            throw std::runtime_error("patched pak does not exist: " + outputPath);
        }
        if (!FileExists(installTarget)) {
            throw std::runtime_error("install target does not exist: " + installTarget);
        }

        const std::string backupPath = DirectoryOf(installTarget) + "\\data_patch.pre_unlock_backup.pak";
        if (!FileExists(backupPath)) {
            if (!MoveFileExA(installTarget.c_str(), backupPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
                throw std::runtime_error("failed to create backup");
            }
        }
        else if (!DeleteFileA(installTarget.c_str())) {
            throw std::runtime_error("failed to remove previous target pak");
        }

        if (!MoveFileExA(outputPath.c_str(), installTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            throw std::runtime_error("failed to install patched pak");
        }
        return backupPath;
    }

    void RestartGame(const PatchOptions& options, std::ostream& log) {
        if (options.restartDelaySeconds > 0) {
            log << "waiting " << options.restartDelaySeconds << "s before restart\n";
            Sleep(options.restartDelaySeconds * 1000);
        }

        if (!options.launcherUri.empty()) {
            HINSTANCE result = ShellExecuteA(nullptr, "open", options.launcherUri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<uintptr_t>(result) > 32) {
                log << "restarted via launcher: " << options.launcherUri << "\n";
                return;
            }
            log << "launcher restart failed with ShellExecute code " << reinterpret_cast<uintptr_t>(result) << "; trying exe fallback\n";
        }

        if (!options.restartExe.empty()) {
            HINSTANCE result = ShellExecuteA(nullptr, "open", options.restartExe.c_str(), nullptr,
                options.restartCwd.empty() ? nullptr : options.restartCwd.c_str(), SW_SHOWNORMAL);
            if (reinterpret_cast<uintptr_t>(result) > 32) {
                log << "restarted: " << options.restartExe << "\n";
                return;
            }
            throw std::runtime_error("exe fallback restart failed");
        }
    }

    void RunPatch(const PatchOptions& options) {
        std::ofstream log(options.logPath, std::ios::out | std::ios::trunc);
        if (!log) {
            throw std::runtime_error("failed to open patch log");
        }

        BuildPatchedPak(options.sourcePath, options.outputPath, log);
        WaitForProcessExit(options.waitPid, options.waitTimeoutSeconds);
        const std::string backupPath = InstallPatchedPak(options.outputPath, options.installTarget);
        log << "installed: " << options.installTarget << "\n";
        log << "backup:    " << backupPath << "\n";
        RestartGame(options, log);
    }

    std::string Rundll32Path() {
        char windowsDirectory[MAX_PATH] = {};
        if (GetWindowsDirectoryA(windowsDirectory, MAX_PATH) == 0) {
            return "rundll32.exe";
        }
        std::string path = std::string(windowsDirectory) + "\\SysWOW64\\rundll32.exe";
        return FileExists(path) ? path : "rundll32.exe";
    }

    std::string BuildRundllCommand(
        const std::string& modulePath,
        const std::string& sourcePakPath,
        const std::string& outputPakPath,
        const std::string& logPath,
        const std::string& launcherUri,
        const std::string& exePath,
        const std::string& exeDirectory) {
        std::string command = QuoteArg(Rundll32Path()) + " ";
        command += QuoteArg(modulePath) + ",RunFileUnlockPatch";
        command += " --source " + QuoteArg(sourcePakPath);
        command += " --output " + QuoteArg(outputPakPath);
        command += " --install-target " + QuoteArg(sourcePakPath);
        command += " --wait-pid " + std::to_string(GetCurrentProcessId());
        command += " --wait-timeout 60 --restart-delay 8";
        command += " --launcher-uri " + QuoteArg(launcherUri);
        command += " --restart-exe " + QuoteArg(exePath);
        command += " --restart-cwd " + QuoteArg(exeDirectory);
        command += " --log " + QuoteArg(logPath);
        return command;
    }
}

void UnlockAllItems() {
    std::string exePath;
    if (!GetCurrentExePath(exePath)) {
        LOG_ERROR("[FileUnlock] Failed to resolve Trials Fusion executable path");
        return;
    }

    const std::string exeDirectory = DirectoryOf(exePath);
    const std::string gameDirectory = FullPathOf(exeDirectory + "\\..");
    const std::string dataPatchPath = gameDirectory + "\\build\\data_pc\\data_patch.pak";
    if (!FileExists(dataPatchPath)) {
        LOG_ERROR("[FileUnlock] data_patch.pak not found at " << dataPatchPath);
        return;
    }

    std::string modulePath;
    if (!GetThisModulePath(modulePath)) {
        LOG_ERROR("[FileUnlock] Failed to resolve TFPayload module path");
        return;
    }

    const std::string outputPakPath = dataPatchPath + ".unlock_tmp";
    const std::string logPath = gameDirectory + "\\build\\data_pc\\data_patch_unlock.log";
    const std::string launcherUri = BaseAddress::IsSteamVersion()
        ? "steam://rungameid/245490"
        : "uplay://launch/297/0";
    const std::string command = BuildRundllCommand(
        modulePath,
        dataPatchPath,
        outputPakPath,
        logPath,
        launcherUri,
        exePath,
        exeDirectory);

    LOG_INFO("[FileUnlock] Launching native item unlock patcher");
    LOG_INFO("[FileUnlock] Trials Fusion will close, data_patch.pak will be patched, then the game will restart");
    LOG_INFO("[FileUnlock] Restart target: " << launcherUri);
    LOG_INFO("[FileUnlock] Patcher log: " << logPath);

    SetEnvironmentVariableA("TFPAYLOAD_PATCHER_MODE", "1");
    const bool started = StartProcess(command, DirectoryOf(modulePath));
    SetEnvironmentVariableA("TFPAYLOAD_PATCHER_MODE", nullptr);
    if (!started) {
        LOG_ERROR("[FileUnlock] Failed to launch native patcher via rundll32");
        return;
    }

    ExitProcess(0);
}
}

extern "C" __declspec(dllexport) void CALLBACK RunFileUnlockPatch(HWND, HINSTANCE, LPSTR commandLine, int) {
    try {
        FileUnlock::RunPatch(FileUnlock::ParsePatchOptions(commandLine ? commandLine : ""));
    }
    catch (const std::exception& exc) {
        const std::string command = commandLine ? commandLine : "";
        std::string logPath;
        try {
            logPath = FileUnlock::OptionalArg(FileUnlock::ParseArgs(command), "--log");
        }
        catch (...) {
        }
        if (!logPath.empty()) {
            std::ofstream log(logPath, std::ios::out | std::ios::app);
            if (log) {
                log << "error: " << exc.what() << "\n";
            }
        }
    }
}
