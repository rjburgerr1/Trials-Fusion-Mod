#include "pch.h"
#include "sound.h"
#include "base-address.h"
#include "logging.h"
#include <MinHook.h>
#include <cstring>
#include <iomanip>
#include <string>

namespace Sound {
    namespace {
        // Steam RVAs from Ghidra:
        // 007a3690 mt::soundsystem::SoundSystemFMOD::run
        // 007b02f0 mt::soundsystem::SoundSystemFMOD::muteEventSystem
        // 007b52e0 mt::soundsystem::SoundSystemFMOD::beginCue
        // 007b5460 mt::soundsystem::SoundSystemFMOD::promptCue
        // 007b5d30 mt::soundsystem::SoundSystemFMOD::SoundSystemFMOD
        constexpr uintptr_t STEAM_SOUND_SYSTEM_FMOD_RUN_RVA = 0x663690;
        constexpr uintptr_t STEAM_SOUND_SYSTEM_FMOD_MUTE_EVENT_SYSTEM_RVA = 0x6702f0;
        constexpr uintptr_t STEAM_SOUND_SYSTEM_FMOD_BEGIN_CUE_RVA = 0x6752e0;
        constexpr uintptr_t STEAM_SOUND_SYSTEM_FMOD_PROMPT_CUE_RVA = 0x675460;
        constexpr uintptr_t STEAM_SOUND_SYSTEM_FMOD_CTOR_RVA = 0x675d30;

        uintptr_t g_baseAddress = 0;
        void* g_soundSystem = nullptr;
        bool g_initialized = false;
        bool g_hookInstalled = false;
        bool g_eventSystemMuted = false;
        bool g_eventLoggingEnabled = true;

        using SoundSystemCtor_t = void* (__fastcall*)(void* thisPtr, void* edx);
        using SoundSystemRun_t = void(__fastcall*)(void* thisPtr, void* edx, void* param);
        using CueCommand_t = int(__fastcall*)(void* thisPtr, void* edx, void* cue);
        using MuteEventSystem_t = void(__thiscall*)(void* thisPtr, bool mute);

        SoundSystemCtor_t g_originalCtor = nullptr;
        SoundSystemRun_t g_originalRun = nullptr;
        CueCommand_t g_originalBeginCue = nullptr;
        CueCommand_t g_originalPromptCue = nullptr;
        MuteEventSystem_t g_muteEventSystem = nullptr;

        bool SafeReadBytes(const void* ptr, void* out, size_t size) {
            if (!ptr || !out || size == 0) {
                return false;
            }

            __try {
                memcpy(out, ptr, size);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool TryReadAscii(const void* ptr, std::string& out, size_t maxLen = 160) {
            out.clear();
            if (!ptr) {
                return false;
            }

            for (size_t i = 0; i < maxLen; ++i) {
                char ch = 0;
                if (!SafeReadBytes(static_cast<const char*>(ptr) + i, &ch, sizeof(ch))) {
                    return false;
                }

                if (ch == '\0') {
                    return out.length() >= 2;
                }

                if (ch < 0x20 || ch > 0x7e) {
                    return false;
                }

                out.push_back(ch);
            }

            return false;
        }

        bool TryReadRefCountedString(void* ptr, std::string& out) {
            void* dataPtr = nullptr;
            if (!SafeReadBytes(ptr, &dataPtr, sizeof(dataPtr)) || !dataPtr) {
                return false;
            }

            int length = 0;
            if (!SafeReadBytes(static_cast<char*>(dataPtr) + 4, &length, sizeof(length))) {
                return false;
            }

            if (length <= 0 || length > 160) {
                return false;
            }

            out.resize(length);
            if (!SafeReadBytes(static_cast<char*>(dataPtr) + 8, &out[0], static_cast<size_t>(length))) {
                out.clear();
                return false;
            }

            for (char ch : out) {
                if (ch < 0x20 || ch > 0x7e) {
                    out.clear();
                    return false;
                }
            }

            return true;
        }

        std::string DecodePossibleString(void* ptr) {
            std::string result;

            if (TryReadAscii(ptr, result)) {
                return result;
            }

            if (TryReadRefCountedString(ptr, result)) {
                return result;
            }

            const int offsets[] = { 4, 8, 12, 16, 20, 24 };
            for (int offset : offsets) {
                if (TryReadAscii(static_cast<char*>(ptr) + offset, result)) {
                    return result;
                }
            }

            void* nestedPtr = nullptr;
            for (int offset : offsets) {
                if (SafeReadBytes(static_cast<char*>(ptr) + offset, &nestedPtr, sizeof(nestedPtr)) &&
                    TryReadAscii(nestedPtr, result)) {
                    return result;
                }
            }

            return "";
        }

        void LogCue(const char* method, void* thisPtr, void* cue) {
            if (!g_eventLoggingEnabled) {
                return;
            }

            std::string decoded = DecodePossibleString(cue);
            if (!decoded.empty()) {
                LOG_INFO("[Sound/Event] " << method << " cue=\"" << decoded << "\" this=0x"
                    << std::hex << reinterpret_cast<uintptr_t>(thisPtr)
                    << " cueArg=0x" << reinterpret_cast<uintptr_t>(cue) << std::dec);
            }
            else {
                LOG_INFO("[Sound/Event] " << method << " cue=<unreadable> this=0x"
                    << std::hex << reinterpret_cast<uintptr_t>(thisPtr)
                    << " cueArg=0x" << reinterpret_cast<uintptr_t>(cue) << std::dec);
            }
        }

        bool CallMuteEventSystemNoExcept(void* soundSystem, bool mute) {
            __try {
                g_muteEventSystem(soundSystem, mute);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        void CaptureSoundSystem(void* thisPtr, const char* source) {
            if (!thisPtr) {
                return;
            }

            if (g_soundSystem != thisPtr) {
                g_soundSystem = thisPtr;
                LOG_INFO("[Sound] Captured SoundSystemFMOD from " << source << ": 0x"
                    << std::hex << reinterpret_cast<uintptr_t>(thisPtr) << std::dec);

                if (g_eventSystemMuted && g_muteEventSystem) {
                    CallMuteEventSystemNoExcept(g_soundSystem, true);
                }
            }
        }

        void* __fastcall Hook_SoundSystemCtor(void* thisPtr, void* edx) {
            void* result = g_originalCtor ? g_originalCtor(thisPtr, edx) : thisPtr;
            CaptureSoundSystem(result ? result : thisPtr, "ctor");
            return result;
        }

        void __fastcall Hook_SoundSystemRun(void* thisPtr, void* edx, void* param) {
            CaptureSoundSystem(thisPtr, "run");

            if (g_originalRun) {
                g_originalRun(thisPtr, edx, param);
            }
        }

        int __fastcall Hook_BeginCue(void* thisPtr, void* edx, void* cue) {
            CaptureSoundSystem(thisPtr, "beginCue");
            LogCue("beginCue", thisPtr, cue);

            return g_originalBeginCue ? g_originalBeginCue(thisPtr, edx, cue) : 0;
        }

        int __fastcall Hook_PromptCue(void* thisPtr, void* edx, void* cue) {
            CaptureSoundSystem(thisPtr, "promptCue");
            LogCue("promptCue", thisPtr, cue);

            return g_originalPromptCue ? g_originalPromptCue(thisPtr, edx, cue) : 0;
        }

        bool InstallHook(uintptr_t targetAddr, void* hookFunc, void** originalFunc, const char* name) {
            MH_STATUS status = MH_CreateHook(
                reinterpret_cast<LPVOID>(targetAddr),
                reinterpret_cast<LPVOID>(hookFunc),
                reinterpret_cast<LPVOID*>(originalFunc));

            if (status == MH_OK) {
                status = MH_EnableHook(reinterpret_cast<LPVOID>(targetAddr));
                if (status == MH_OK) {
                    LOG_INFO("[Sound] " << name << " hook installed");
                    return true;
                }

                LOG_WARNING("[Sound] Failed to enable " << name << " hook: " << MH_StatusToString(status));
                return false;
            }

            if (status == MH_ERROR_ALREADY_CREATED) {
                LOG_WARNING("[Sound] " << name << " hook already exists");
                return true;
            }

            LOG_WARNING("[Sound] Failed to create " << name << " hook: " << MH_StatusToString(status));
            return false;
        }
    }

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            return true;
        }

        if (baseAddress == 0) {
            LOG_ERROR("[Sound] Invalid base address");
            return false;
        }

        if (!BaseAddress::IsSteamVersion()) {
            LOG_WARNING("[Sound] FMOD hooks are only mapped for Steam right now");
            return false;
        }

        g_baseAddress = baseAddress;
        g_muteEventSystem = reinterpret_cast<MuteEventSystem_t>(
            g_baseAddress + STEAM_SOUND_SYSTEM_FMOD_MUTE_EVENT_SYSTEM_RVA);

        uintptr_t ctorAddr = g_baseAddress + STEAM_SOUND_SYSTEM_FMOD_CTOR_RVA;
        uintptr_t runAddr = g_baseAddress + STEAM_SOUND_SYSTEM_FMOD_RUN_RVA;

        bool ctorHook = InstallHook(ctorAddr, &Hook_SoundSystemCtor, reinterpret_cast<void**>(&g_originalCtor), "SoundSystemFMOD ctor");
        bool runHook = InstallHook(runAddr, &Hook_SoundSystemRun, reinterpret_cast<void**>(&g_originalRun), "SoundSystemFMOD run");
        bool beginCueHook = InstallHook(
            g_baseAddress + STEAM_SOUND_SYSTEM_FMOD_BEGIN_CUE_RVA,
            &Hook_BeginCue,
            reinterpret_cast<void**>(&g_originalBeginCue),
            "SoundSystemFMOD beginCue");
        bool promptCueHook = InstallHook(
            g_baseAddress + STEAM_SOUND_SYSTEM_FMOD_PROMPT_CUE_RVA,
            &Hook_PromptCue,
            reinterpret_cast<void**>(&g_originalPromptCue),
            "SoundSystemFMOD promptCue");

        g_hookInstalled = ctorHook || runHook || beginCueHook || promptCueHook;
        g_initialized = g_hookInstalled;
        LOG_INFO("[Sound] Initialized FMOD wrapper hooks");
        return g_initialized;
    }

    bool IsAvailable() {
        return g_soundSystem != nullptr && g_muteEventSystem != nullptr;
    }

    bool IsEventSystemMuted() {
        return g_eventSystemMuted;
    }

    bool IsEventLoggingEnabled() {
        return g_eventLoggingEnabled;
    }

    void SetEventLoggingEnabled(bool enabled) {
        g_eventLoggingEnabled = enabled;
        LOG_INFO("[Sound] FMOD event logging " << (enabled ? "enabled" : "disabled"));
    }

    bool MuteEventSystem(bool mute) {
        g_eventSystemMuted = mute;

        if (!IsAvailable()) {
            LOG_WARNING("[Sound] SoundSystemFMOD is not captured yet; mute will apply after capture");
            return false;
        }

        if (!CallMuteEventSystemNoExcept(g_soundSystem, mute)) {
            LOG_ERROR("[Sound] muteEventSystem crashed");
            return false;
        }

        LOG_INFO("[Sound] FMOD event system " << (mute ? "muted" : "unmuted"));
        return true;
    }

    bool ToggleEventSystemMute() {
        return MuteEventSystem(!g_eventSystemMuted);
    }
}
