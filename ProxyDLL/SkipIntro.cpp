// SkipIntro.cpp
// Hook Windows file APIs to block intro videos at the OS level
#include <Windows.h>
#include <iostream>
#include "SkipIntro.h"
#include "MinHook.h"

namespace SkipIntro {
    // SetSkipIntroVideos function: 0x00c28e70 -> RVA = 0x00c28e70 - 0x700000 = 0x00528e70
    static constexpr DWORD RVA_SET_SKIP_INTRO = 0x00528e70;
    
    // DAT_0174b33c (Ghidra) -> RVA = 0x0174b33c - 0x700000 = 0x0104b33c
    static constexpr DWORD RVA_GAME_STATE_PTR = 0x0104b33c;
    static constexpr DWORD OFFSET_SKIP_INTRO_FLAGS = 0x51c0;
    static constexpr DWORD SKIP_INTRO_FLAG_BIT = 0x2;
    
    // Forward declaration
    void TrySetSkipIntroFlag();
    
    // Hook CreateFileA to block video file access
    typedef HANDLE (WINAPI* CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    static CreateFileA_t g_originalCreateFileA = nullptr;
    
    // Hook CreateFileW as well
    typedef HANDLE (WINAPI* CreateFileW_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    static CreateFileW_t g_originalCreateFileW = nullptr;
    
    static bool g_hooked = false;
    
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
        if (lpFileName && (
            strstr(lpFileName, "ubisoft.bik") != nullptr ||
            strstr(lpFileName, "redlynx.bik") != nullptr)) {
            
            std::cout << "[SkipIntro] *** BLOCKED FILE ACCESS: " << lpFileName << " ***" << std::endl;
            
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
            
            if (strstr(narrowPath, "ubisoft.bik") != nullptr ||
                strstr(narrowPath, "redlynx.bik") != nullptr) {
                
                std::cout << "[SkipIntro] *** BLOCKED FILE ACCESS (W): " << narrowPath << " ***" << std::endl;
                
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
        DWORD** gameStatePtrPtr = (DWORD**)(baseAddress + RVA_GAME_STATE_PTR);
        
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
            std::cout << "[SkipIntro] ✓ Skip intro flag SET: 0x" << std::hex << currentFlags << " -> 0x" << newFlags << std::dec << std::endl;
        }
    }
    
    bool Initialize() {
        if (g_hooked) {
            std::cout << "[SkipIntro] Already hooked" << std::endl;
            return true;
        }
        
        std::cout << "[SkipIntro] Hooking Windows file APIs..." << std::endl;
        
        // Initialize MinHook
        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            std::cout << "[SkipIntro] ERROR: MinHook init failed: " << status << std::endl;
            return false;
        }
        
        // Hook CreateFileA
        status = MH_CreateHook(
            &CreateFileA,
            &HookedCreateFileA,
            reinterpret_cast<LPVOID*>(&g_originalCreateFileA)
        );
        
        if (status != MH_OK) {
            std::cout << "[SkipIntro] ERROR: CreateHook(CreateFileA) failed: " << status << std::endl;
            return false;
        }
        
        status = MH_EnableHook(&CreateFileA);
        if (status != MH_OK) {
            std::cout << "[SkipIntro] ERROR: EnableHook(CreateFileA) failed: " << status << std::endl;
            return false;
        }
        
        // Hook CreateFileW
        status = MH_CreateHook(
            &CreateFileW,
            &HookedCreateFileW,
            reinterpret_cast<LPVOID*>(&g_originalCreateFileW)
        );
        
        if (status != MH_OK) {
            std::cout << "[SkipIntro] ERROR: CreateHook(CreateFileW) failed: " << status << std::endl;
            return false;
        }
        
        status = MH_EnableHook(&CreateFileW);
        if (status != MH_OK) {
            std::cout << "[SkipIntro] ERROR: EnableHook(CreateFileW) failed: " << status << std::endl;
            return false;
        }
        
        g_hooked = true;
        std::cout << "[SkipIntro] Successfully hooked CreateFileA/W!" << std::endl;
        std::cout << "[SkipIntro] Intro videos will be blocked at Windows API level" << std::endl;
        
        // Start a background thread to set the skip intro flag
        // This will keep trying until it succeeds
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            std::cout << "[SkipIntro] Starting flag setter thread..." << std::endl;
            
            // Try for up to 30 seconds
            for (int i = 0; i < 300; i++) {
                Sleep(100);
                TrySetSkipIntroFlag();
                
                // Check if we succeeded
                HMODULE hModule = GetModuleHandleA("trials_fusion.exe");
                if (hModule) {
                    DWORD baseAddress = (DWORD)hModule;
                    DWORD** gameStatePtrPtr = (DWORD**)(baseAddress + RVA_GAME_STATE_PTR);
                    
                    if (!IsBadReadPtr(gameStatePtrPtr, sizeof(DWORD*))) {
                        DWORD* gameStatePtr = *gameStatePtrPtr;
                        if (gameStatePtr && !IsBadReadPtr(gameStatePtr, sizeof(DWORD))) {
                            DWORD* flagsPtr = (DWORD*)((BYTE*)gameStatePtr + OFFSET_SKIP_INTRO_FLAGS);
                            if (!IsBadReadPtr(flagsPtr, sizeof(DWORD))) {
                                if ((*flagsPtr & SKIP_INTRO_FLAG_BIT) != 0) {
                                    std::cout << "[SkipIntro] Flag successfully set and verified!" << std::endl;
                                    return 0;
                                }
                            }
                        }
                    }
                }
            }
            
            std::cout << "[SkipIntro] Flag setter thread timed out (flag may still work)" << std::endl;
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
        MH_RemoveHook(&CreateFileA);
        MH_RemoveHook(&CreateFileW);
        
        g_hooked = false;
        std::cout << "[SkipIntro] Hooks removed" << std::endl;
    }
}
