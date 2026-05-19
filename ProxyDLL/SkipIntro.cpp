// SkipIntro.cpp
// Hook Windows file APIs to block intro videos at the OS level
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <sstream>
#include <string>
#include "SkipIntro.h"
#include "logging.h"
#include "MinHook.h"

namespace SkipIntro {
    // SetSkipIntroVideos function: 0x00c28e70 -> RVA = 0x00c28e70 - 0x700000 = 0x00528e70
    static constexpr DWORD RVA_SET_SKIP_INTRO = 0x00528e70;
    
    // DAT_0174b33c (Ghidra/Uplay) -> RVA = 0x0174b33c - 0x700000 = 0x0104b33c.
    // Steam globals in this project are consistently +0x2000 from Uplay for this region.
    static constexpr DWORD RVA_GAME_STATE_PTR_UPLAY = 0x0104b33c;
    static constexpr DWORD RVA_GAME_STATE_PTR_STEAM = 0x0104d33c;
    static constexpr DWORD OFFSET_SKIP_INTRO_FLAGS = 0x51c0;
    static constexpr DWORD SKIP_INTRO_FLAG_BIT = 0x2;

    // FUN_00e7ac80(pathString, loopFlag): ActionScript loadVideo's native video factory.
    // Returning null here skips the video object entirely, including the logo audio cues
    // that the Bink constructor attaches for video/ubisoft.bik and video/redlynx.bik.
    static constexpr DWORD RVA_CREATE_VIDEO_FROM_PATH_UPLAY = 0x0077ac80;
    
    // Forward declaration
    void TrySetSkipIntroFlag();
    
    // Hook CreateFileA to block video file access
    typedef HANDLE (WINAPI* CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    static CreateFileA_t g_originalCreateFileA = nullptr;
    
    // Hook CreateFileW as well
    typedef HANDLE (WINAPI* CreateFileW_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    static CreateFileW_t g_originalCreateFileW = nullptr;

    typedef void* (__cdecl* CreateVideoFromPath_t)(int pathString, char loop);
    typedef void(__cdecl* LoadVideo_t)(int actionScriptCall);
    typedef void(__cdecl* VideoFinishedCallback_t)(int* serializedValue);
    static CreateVideoFromPath_t g_originalCreateVideoFromPath = nullptr;
    static LoadVideo_t g_originalLoadVideo = nullptr;
    static void* g_createVideoFromPathTarget = nullptr;
    static void* g_loadVideoTarget = nullptr;
    static void* g_lastIntroVideoObject = nullptr;
    
    static bool g_hooked = false;

    static bool IsSteamVersion() {
        if (GetModuleHandleA("steam_api.dll") || GetModuleHandleA("steamclient.dll")) {
            return true;
        }

        char exePath[MAX_PATH] = {};
        if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0) {
            return false;
        }

        std::string path(exePath);
        std::transform(path.begin(), path.end(), path.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return path.find("steam") != std::string::npos;
    }

    static DWORD GetGameStatePtrRva() {
        return IsSteamVersion() ? RVA_GAME_STATE_PTR_STEAM : RVA_GAME_STATE_PTR_UPLAY;
    }

    static bool ReadGameString(int stringObject, std::string& out) {
        out.clear();
        if (stringObject == 0 || IsBadReadPtr(reinterpret_cast<void*>(stringObject), 0x0c)) {
            return false;
        }

        uint16_t length = *reinterpret_cast<uint16_t*>(stringObject + 0x06);
        const char* data = *reinterpret_cast<const char**>(stringObject + 0x08);
        if (length == 0 || data == nullptr || IsBadReadPtr(data, length)) {
            return false;
        }

        out.assign(data, data + length);
        std::transform(out.begin(), out.end(), out.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return true;
    }

    static bool IsIntroVideoPath(const std::string& path) {
        return path.find("video/ubisoft.bik") != std::string::npos ||
            path.find("video\\ubisoft.bik") != std::string::npos ||
            path.find("ubisoft.bik") != std::string::npos ||
            path.find("video/redlynx.bik") != std::string::npos ||
            path.find("video\\redlynx.bik") != std::string::npos ||
            path.find("redlynx.bik") != std::string::npos;
    }

    static bool IsIntroVideoPath(const char* path) {
        if (path == nullptr) {
            return false;
        }

        std::string lower(path);
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return IsIntroVideoPath(lower);
    }

    static uint8_t* FindBytes(uint8_t* start, size_t size, const char* bytes, size_t byteCount) {
        if (start == nullptr || bytes == nullptr || byteCount == 0 || size < byteCount) {
            return nullptr;
        }

        for (size_t i = 0; i <= size - byteCount; ++i) {
            if (memcmp(start + i, bytes, byteCount) == 0) {
                return start + i;
            }
        }
        return nullptr;
    }

    static bool GetSection(HMODULE module, const char* name, uint8_t** outStart, size_t* outSize) {
        if (!module || !name || !outStart || !outSize) {
            return false;
        }

        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
            return false;
        }

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
            reinterpret_cast<uint8_t*>(module) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) {
            return false;
        }

        auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            char sectionName[9] = {};
            memcpy(sectionName, section->Name, 8);
            if (strcmp(sectionName, name) == 0) {
                *outStart = reinterpret_cast<uint8_t*>(module) + section->VirtualAddress;
                *outSize = section->Misc.VirtualSize;
                return true;
            }
        }

        return false;
    }

    static void* ResolveCreateVideoFromPath(HMODULE module) {
        uint8_t* text = nullptr;
        size_t textSize = 0;
        uint8_t* rdata = nullptr;
        size_t rdataSize = 0;

        if (GetSection(module, ".text", &text, &textSize) &&
            GetSection(module, ".rdata", &rdata, &rdataSize)) {
            static constexpr char kFallbackVideo[] = "video/events/VideoNotFound.bik";
            uint8_t* fallbackString = FindBytes(rdata, rdataSize, kFallbackVideo, sizeof(kFallbackVideo));
            if (fallbackString != nullptr) {
                uintptr_t fallbackAddress = reinterpret_cast<uintptr_t>(fallbackString);
                for (size_t i = 0; i + sizeof(uintptr_t) <= textSize; ++i) {
                    uintptr_t immediate = *reinterpret_cast<uintptr_t*>(text + i);
                    if (immediate != fallbackAddress) {
                        continue;
                    }

                    uint8_t* xref = text + i;
                    uint8_t* scanStart = (xref - text > 0x200) ? xref - 0x200 : text;
                    for (uint8_t* p = xref; p >= scanStart + 10; --p) {
                        if (p[0] == 0x6a && p[1] == 0xff && p[2] == 0x68 &&
                            p[7] == 0x64 && p[8] == 0xa1 &&
                            *reinterpret_cast<uint32_t*>(p + 9) == 0) {
                            LOG_VERBOSE("[SkipIntro] Resolved video factory by string xref at 0x"
                                << std::hex << reinterpret_cast<uintptr_t>(p));
                            return p;
                        }
                    }
                }
            }
        }

        if (!IsSteamVersion()) {
            return reinterpret_cast<uint8_t*>(module) + RVA_CREATE_VIDEO_FROM_PATH_UPLAY;
        }

        return nullptr;
    }

    static void* FindFunctionStart(uint8_t* text, uint8_t* from, size_t maxBack) {
        if (text == nullptr || from == nullptr || from < text) {
            return nullptr;
        }

        auto bytesBefore = static_cast<size_t>(from - text);
        uint8_t* scanStart = (bytesBefore > maxBack) ? from - maxBack : text;
        for (uint8_t* p = from; p >= scanStart + 10; --p) {
            if (p[0] == 0x6a && p[1] == 0xff && p[2] == 0x68 &&
                p[7] == 0x64 && p[8] == 0xa1 &&
                *reinterpret_cast<uint32_t*>(p + 9) == 0) {
                return p;
            }
        }

        return nullptr;
    }

    static void* ResolveLoadVideoHandler(HMODULE module, void* createVideoFromPath) {
        uint8_t* text = nullptr;
        size_t textSize = 0;
        if (!GetSection(module, ".text", &text, &textSize) || createVideoFromPath == nullptr) {
            return nullptr;
        }

        uintptr_t target = reinterpret_cast<uintptr_t>(createVideoFromPath);
        for (size_t i = 0; i + 5 <= textSize; ++i) {
            if (text[i] != 0xe8) {
                continue;
            }

            int32_t rel = *reinterpret_cast<int32_t*>(text + i + 1);
            uintptr_t callTarget = reinterpret_cast<uintptr_t>(text + i + 5) + rel;
            if (callTarget != target) {
                continue;
            }

            void* start = FindFunctionStart(text, text + i, 0x400);
            if (start != nullptr) {
                LOG_VERBOSE("[SkipIntro] Resolved loadVideo bridge by video factory call xref at 0x"
                    << std::hex << reinterpret_cast<uintptr_t>(start));
                return start;
            }
        }

        return nullptr;
    }

    void* __cdecl HookedCreateVideoFromPath(int pathString, char loop) {
        std::string path;
        if (ReadGameString(pathString, path) && IsIntroVideoPath(path)) {
            LOG_VERBOSE("[SkipIntro] Startup logo video load detected: " << path);
            TrySetSkipIntroFlag();
            void* videoObject = g_originalCreateVideoFromPath(pathString, loop);
            g_lastIntroVideoObject = videoObject;
            return videoObject;
        }

        return g_originalCreateVideoFromPath(pathString, loop);
    }

    void __cdecl HookedLoadVideo(int actionScriptCall) {
        g_lastIntroVideoObject = nullptr;
        g_originalLoadVideo(actionScriptCall);

        void* videoObject = g_lastIntroVideoObject;
        g_lastIntroVideoObject = nullptr;
        if (videoObject == nullptr || IsBadReadPtr(videoObject, 0x08)) {
            return;
        }

        int* serializedValue = *reinterpret_cast<int**>(reinterpret_cast<uint8_t*>(videoObject) + 0x04);
        if (serializedValue == nullptr || IsBadReadPtr(serializedValue, 0x11c)) {
            return;
        }

        auto callback = *reinterpret_cast<VideoFinishedCallback_t*>(
            reinterpret_cast<uint8_t*>(serializedValue) + 0x118);
        if (callback == nullptr || IsBadCodePtr(reinterpret_cast<FARPROC>(callback))) {
            return;
        }

        LOG_VERBOSE("[SkipIntro] Completing startup logo video immediately via native finish callback");
        callback(serializedValue);
    }
    
    // Our CreateFileA hook
    HANDLE WINAPI HookedCreateFileA(
        LPCSTR lpFileName,
        DWORD dwDesiredAccess,
        DWORD dwShareMode,
        LPSECURITY_ATTRIBUTES lpSecurityAttributes,
        DWORD dwCreationDisposition,
        DWORD dwFlagsAndAttributes,
        HANDLE hTemplateFile)
    {
        // Check if this is an intro video
        if (IsIntroVideoPath(lpFileName)) {
            
            LOG_VERBOSE("[SkipIntro] *** BLOCKED FILE ACCESS: " << lpFileName << " ***");
            
            // IMMEDIATELY set the skip flag when we detect intro video attempt
            TrySetSkipIntroFlag();
            
            // Return invalid handle - file not found
            SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE_VALUE;
        }
        
        // Not an intro video - allow normal access
        return g_originalCreateFileA(lpFileName, dwDesiredAccess, dwShareMode,
            lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }
    
    // Our CreateFileW hook
    HANDLE WINAPI HookedCreateFileW(
        LPCWSTR lpFileName,
        DWORD dwDesiredAccess,
        DWORD dwShareMode,
        LPSECURITY_ATTRIBUTES lpSecurityAttributes,
        DWORD dwCreationDisposition,
        DWORD dwFlagsAndAttributes,
        HANDLE hTemplateFile)
    {
        // Check if this is an intro video (convert to narrow string for check)
        if (lpFileName) {
            char narrowPath[512];
            WideCharToMultiByte(CP_ACP, 0, lpFileName, -1, narrowPath, sizeof(narrowPath), NULL, NULL);
            
            if (IsIntroVideoPath(narrowPath)) {
                
                LOG_VERBOSE("[SkipIntro] *** BLOCKED FILE ACCESS (W): " << narrowPath << " ***");
                
                // IMMEDIATELY set the skip flag when we detect intro video attempt
                TrySetSkipIntroFlag();
                
                // Return invalid handle - file not found
                SetLastError(ERROR_FILE_NOT_FOUND);
                return INVALID_HANDLE_VALUE;
            }
        }
        
        // Not an intro video - allow normal access
        return g_originalCreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
            lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    }
    
    // Try to set the skip intro flag
    void TrySetSkipIntroFlag() {
        HMODULE hModule = GetModuleHandleA("trials_fusion.exe");
        if (!hModule) {
            return;
        }
        
        DWORD baseAddress = (DWORD)hModule;
        DWORD** gameStatePtrPtr = (DWORD**)(baseAddress + GetGameStatePtrRva());
        
        // Check if pointer is valid
        if (IsBadReadPtr(gameStatePtrPtr, sizeof(DWORD*))) {
            return;
        }
        
        DWORD* gameStatePtr = *gameStatePtrPtr;
        if (!gameStatePtr || IsBadReadPtr(gameStatePtr, sizeof(DWORD))) {
            return;
        }
        
        // Calculate flags address
        DWORD* flagsPtr = (DWORD*)((BYTE*)gameStatePtr + OFFSET_SKIP_INTRO_FLAGS);
        
        if (IsBadWritePtr(flagsPtr, sizeof(DWORD))) {
            return;
        }
        
        // Set the skip intro flag (bit 1 = 0x2)
        // This is what SetSkipIntroVideos does internally anyway
        DWORD currentFlags = *flagsPtr;
        DWORD newFlags = currentFlags | SKIP_INTRO_FLAG_BIT;
        
        if (currentFlags != newFlags) {
            *flagsPtr = newFlags;
            LOG_VERBOSE("[SkipIntro] Skip intro flag SET: 0x" << std::hex << currentFlags << " -> 0x" << newFlags);
        }
    }
    
    bool Initialize() {
        if (g_hooked) {
            LOG_VERBOSE("[SkipIntro] Already hooked");
            return true;
        }
        
        LOG_VERBOSE("[SkipIntro] Hooking Windows file APIs...");
        
        // Initialize MinHook
        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            LOG_ERROR("[SkipIntro] ERROR: MinHook init failed: " << status);
            return false;
        }
        
        // Hook CreateFileA
        status = MH_CreateHook(
            &CreateFileA,
            &HookedCreateFileA,
            reinterpret_cast<LPVOID*>(&g_originalCreateFileA)
        );
        
        if (status != MH_OK) {
            LOG_ERROR("[SkipIntro] ERROR: CreateHook(CreateFileA) failed: " << status);
            return false;
        }
        
        status = MH_EnableHook(&CreateFileA);
        if (status != MH_OK) {
            LOG_ERROR("[SkipIntro] ERROR: EnableHook(CreateFileA) failed: " << status);
            return false;
        }
        
        // Hook CreateFileW
        status = MH_CreateHook(
            &CreateFileW,
            &HookedCreateFileW,
            reinterpret_cast<LPVOID*>(&g_originalCreateFileW)
        );
        
        if (status != MH_OK) {
            LOG_ERROR("[SkipIntro] ERROR: CreateHook(CreateFileW) failed: " << status);
            return false;
        }
        
        status = MH_EnableHook(&CreateFileW);
        if (status != MH_OK) {
            LOG_ERROR("[SkipIntro] ERROR: EnableHook(CreateFileW) failed: " << status);
            return false;
        }

        HMODULE gameModule = GetModuleHandleA("trials_fusion.exe");
        if (!gameModule) {
            gameModule = GetModuleHandleA(nullptr);
        }

        void* createVideoFromPath = ResolveCreateVideoFromPath(gameModule);
        if (createVideoFromPath != nullptr) {
            g_createVideoFromPathTarget = createVideoFromPath;
            status = MH_CreateHook(
                createVideoFromPath,
                &HookedCreateVideoFromPath,
                reinterpret_cast<LPVOID*>(&g_originalCreateVideoFromPath)
            );

            if (status == MH_OK) {
                status = MH_EnableHook(createVideoFromPath);
                if (status == MH_OK) {
                    LOG_VERBOSE("[SkipIntro] Video factory hook installed at 0x"
                        << std::hex << reinterpret_cast<uintptr_t>(createVideoFromPath));
                }
                else {
                    LOG_ERROR("[SkipIntro] ERROR: EnableHook(video factory) failed: " << status);
                }
            }
            else {
                LOG_ERROR("[SkipIntro] ERROR: CreateHook(video factory) failed: " << status);
            }

            void* loadVideo = ResolveLoadVideoHandler(gameModule, createVideoFromPath);
            if (loadVideo != nullptr) {
                g_loadVideoTarget = loadVideo;
                status = MH_CreateHook(
                    loadVideo,
                    &HookedLoadVideo,
                    reinterpret_cast<LPVOID*>(&g_originalLoadVideo)
                );

                if (status == MH_OK) {
                    status = MH_EnableHook(loadVideo);
                    if (status == MH_OK) {
                        LOG_VERBOSE("[SkipIntro] loadVideo bridge hook installed at 0x"
                            << std::hex << reinterpret_cast<uintptr_t>(loadVideo));
                    }
                    else {
                        LOG_ERROR("[SkipIntro] ERROR: EnableHook(loadVideo) failed: " << status);
                    }
                }
                else {
                    LOG_ERROR("[SkipIntro] ERROR: CreateHook(loadVideo) failed: " << status);
                }
            }
            else {
                LOG_ERROR("[SkipIntro] Could not resolve loadVideo hook target");
            }
        }
        else {
            LOG_ERROR("[SkipIntro] Could not resolve video factory hook target");
        }
        
        g_hooked = true;
        LOG_VERBOSE("[SkipIntro] Successfully hooked CreateFileA/W!");
        LOG_VERBOSE("[SkipIntro] Intro videos will be blocked at Windows API level");
        
        // Start a background thread to set the skip intro flag
        // This will keep trying until it succeeds
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            LOG_VERBOSE("[SkipIntro] Starting flag setter thread...");
            
            // Try for up to 30 seconds
            for (int i = 0; i < 300; i++) {
                Sleep(100);
                TrySetSkipIntroFlag();
                
                // Check if we succeeded
                HMODULE hModule = GetModuleHandleA("trials_fusion.exe");
                if (hModule) {
                    DWORD baseAddress = (DWORD)hModule;
                    DWORD** gameStatePtrPtr = (DWORD**)(baseAddress + GetGameStatePtrRva());
                    
                    if (!IsBadReadPtr(gameStatePtrPtr, sizeof(DWORD*))) {
                        DWORD* gameStatePtr = *gameStatePtrPtr;
                        if (gameStatePtr && !IsBadReadPtr(gameStatePtr, sizeof(DWORD))) {
                            DWORD* flagsPtr = (DWORD*)((BYTE*)gameStatePtr + OFFSET_SKIP_INTRO_FLAGS);
                            if (!IsBadReadPtr(flagsPtr, sizeof(DWORD))) {
                                if ((*flagsPtr & SKIP_INTRO_FLAG_BIT) != 0) {
                                    LOG_VERBOSE("[SkipIntro] Flag successfully set and verified!");
                                    return 0;
                                }
                            }
                        }
                    }
                }
            }
            
            LOG_VERBOSE("[SkipIntro] Flag setter thread timed out (flag may still work)");
            return 0;
        }, nullptr, 0, nullptr);
        
        return true;
    }
    
    void Shutdown() {
        if (!g_hooked) {
            return;
        }
        
        MH_DisableHook(&CreateFileA);
        MH_DisableHook(&CreateFileW);
        if (g_createVideoFromPathTarget) {
            MH_DisableHook(g_createVideoFromPathTarget);
        }
        if (g_loadVideoTarget) {
            MH_DisableHook(g_loadVideoTarget);
        }
        MH_RemoveHook(&CreateFileA);
        MH_RemoveHook(&CreateFileW);
        if (g_createVideoFromPathTarget) {
            MH_RemoveHook(g_createVideoFromPathTarget);
            g_createVideoFromPathTarget = nullptr;
            g_originalCreateVideoFromPath = nullptr;
        }
        if (g_loadVideoTarget) {
            MH_RemoveHook(g_loadVideoTarget);
            g_loadVideoTarget = nullptr;
            g_originalLoadVideo = nullptr;
        }
        
        g_hooked = false;
        LOG_VERBOSE("[SkipIntro] Hooks removed");
    }
}
