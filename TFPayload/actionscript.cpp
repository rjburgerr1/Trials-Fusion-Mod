#include "pch.h"
#include "actionscript.h"
#include "base-address.h"
#include "logging.h"
#include <string>
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace ActionScript {
    static uintptr_t g_BaseAddress = 0;
    static bool g_IsSteamVersion = false;
    static uintptr_t g_GameManagerPtr = 0;  // Version-specific game manager pointer RVA
    static AllocateMemory_t AllocateMemory = nullptr;
    static CreateMessageObject_t CreateMessageObject = nullptr;
    static CreateMessage2Params_t CreateMessage2Params = nullptr;
    static CreateMessageWithParameters_t CreateMessageWithParameters = nullptr;
    static SendMessage_t SendMessage = nullptr;
    static SetBoolValue_t SetBoolValue = nullptr;
    static SetFloatValue_t SetFloatValue = nullptr;
    static InitStringEmpty_t InitStringEmpty = nullptr;
    static SetStringFromCStr_t SetStringFromCStr = nullptr;
    static SetGameState_t SetGameState = nullptr;
    static HandleRaceFinish_t HandleRaceFinish = nullptr;
    static GetFirstEntity_t GetFirstEntity = nullptr;

    bool Initialize(uintptr_t baseAddress) {
        if (baseAddress == 0) {
            LOG_ERROR("[ActionScript] Initialize - Invalid base address");
            return false;
        }
        
        g_BaseAddress = baseAddress;
        g_IsSteamVersion = BaseAddress::IsSteamVersion();
        
        LOG_VERBOSE("[ActionScript] Detected game version: " << (g_IsSteamVersion ? "STEAM" : "UPLAY"));

        if (g_IsSteamVersion) {
            // Use Steam RVAs - all functions now mapped!
            g_GameManagerPtr = Steam::GAME_MANAGER_PTR;
            
            AllocateMemory = reinterpret_cast<AllocateMemory_t>(baseAddress + Steam::ALLOCATE_MEMORY_ADDR);
            CreateMessageObject = reinterpret_cast<CreateMessageObject_t>(baseAddress + Steam::CREATE_MESSAGE_ADDR);
            CreateMessage2Params = reinterpret_cast<CreateMessage2Params_t>(baseAddress + Steam::CREATE_MESSAGE_2PARAMS_ADDR);
            CreateMessageWithParameters = reinterpret_cast<CreateMessageWithParameters_t>(baseAddress + Steam::CREATE_MESSAGE_PARAMS_ADDR);
            SendMessage = reinterpret_cast<SendMessage_t>(baseAddress + Steam::SEND_MESSAGE_ADDR);
            SetBoolValue = reinterpret_cast<SetBoolValue_t>(baseAddress + Steam::SET_BOOL_VALUE_ADDR);
            SetFloatValue = reinterpret_cast<SetFloatValue_t>(baseAddress + Steam::SET_FLOAT_VALUE_ADDR);
            InitStringEmpty = reinterpret_cast<InitStringEmpty_t>(baseAddress + Steam::INIT_STRING_EMPTY_ADDR);
            SetStringFromCStr = reinterpret_cast<SetStringFromCStr_t>(baseAddress + Steam::SET_STRING_FROM_CSTR_ADDR);
            SetGameState = reinterpret_cast<SetGameState_t>(baseAddress + Steam::SET_GAME_STATE_ADDR);
            HandleRaceFinish = reinterpret_cast<HandleRaceFinish_t>(baseAddress + Steam::HANDLE_RACE_FINISH_ADDR);
            GetFirstEntity = reinterpret_cast<GetFirstEntity_t>(baseAddress + Steam::GET_FIRST_ENTITY_ADDR);
            
            LOG_VERBOSE("[ActionScript] Steam version - all functions mapped");
            LOG_VERBOSE("[ActionScript] HandleRaceFinish: 0x" << std::hex << (baseAddress + Steam::HANDLE_RACE_FINISH_ADDR) << std::dec);
            LOG_VERBOSE("[ActionScript] GameManagerPtr: 0x" << std::hex << (baseAddress + Steam::GAME_MANAGER_PTR) << std::dec);
        } else {
            // Use Uplay RVAs
            g_GameManagerPtr = Uplay::GAME_MANAGER_PTR;
            
            AllocateMemory = reinterpret_cast<AllocateMemory_t>(baseAddress + Uplay::ALLOCATE_MEMORY_ADDR);
            CreateMessageObject = reinterpret_cast<CreateMessageObject_t>(baseAddress + Uplay::CREATE_MESSAGE_ADDR);
            CreateMessage2Params = reinterpret_cast<CreateMessage2Params_t>(baseAddress + Uplay::CREATE_MESSAGE_2PARAMS_ADDR);
            CreateMessageWithParameters = reinterpret_cast<CreateMessageWithParameters_t>(baseAddress + Uplay::CREATE_MESSAGE_PARAMS_ADDR);
            SendMessage = reinterpret_cast<SendMessage_t>(baseAddress + Uplay::SEND_MESSAGE_ADDR);
            SetBoolValue = reinterpret_cast<SetBoolValue_t>(baseAddress + Uplay::SET_BOOL_VALUE_ADDR);
            SetFloatValue = reinterpret_cast<SetFloatValue_t>(baseAddress + Uplay::SET_FLOAT_VALUE_ADDR);
            InitStringEmpty = reinterpret_cast<InitStringEmpty_t>(baseAddress + Uplay::INIT_STRING_EMPTY_ADDR);
            SetStringFromCStr = reinterpret_cast<SetStringFromCStr_t>(baseAddress + Uplay::SET_STRING_FROM_CSTR_ADDR);
            SetGameState = reinterpret_cast<SetGameState_t>(baseAddress + Uplay::SET_GAME_STATE_ADDR);
            HandleRaceFinish = reinterpret_cast<HandleRaceFinish_t>(baseAddress + Uplay::HANDLE_RACE_FINISH_ADDR);
            GetFirstEntity = reinterpret_cast<GetFirstEntity_t>(baseAddress + Uplay::GET_FIRST_ENTITY_ADDR);
            
            LOG_VERBOSE("[ActionScript] HandleRaceFinish (Uplay): 0x" << std::hex << (baseAddress + Uplay::HANDLE_RACE_FINISH_ADDR) << std::dec);
        }

        LOG_VERBOSE("[ActionScript] Initialize - Successfully initialized");
        LOG_VERBOSE("  Base: 0x" << std::hex << baseAddress << std::dec);

        return true;
    }

    void* GetMessageHandler() {
        if (g_BaseAddress == 0) return nullptr;

        // Use version-specific MESSAGE_HANDLER_PTR
        uintptr_t messageHandlerRVA = g_IsSteamVersion ? Steam::MESSAGE_HANDLER_PTR : Uplay::MESSAGE_HANDLER_PTR;
        uintptr_t* ptrToHandler = reinterpret_cast<uintptr_t*>(g_BaseAddress + messageHandlerRVA);
        if (!ptrToHandler) return nullptr;

        uintptr_t firstDeref = *ptrToHandler;
        if (firstDeref == 0) return nullptr;

        uintptr_t handlerAddr = firstDeref + MESSAGE_HANDLER_OFFSET;
        void** handlerPtr = reinterpret_cast<void**>(handlerAddr);
        return *handlerPtr;
    }

    bool ShowLoadingScreen(bool show, const char* loadingText) {
        LOG_VERBOSE("[ActionScript] ShowLoadingScreen (show=" << show << ", text=\"" << loadingText << "\")");

        if (!CreateMessage2Params || !SendMessage || !AllocateMemory || !SetBoolValue || !InitStringEmpty || !SetStringFromCStr) {
            LOG_ERROR("[ActionScript] Not initialized");
            return false;
        }

        void* messageHandler = GetMessageHandler();
        if (!messageHandler) {
            LOG_ERROR("[ActionScript] Failed to get message handler");
            return false;
        }

        // Allocate message buffer
        uintptr_t returnAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());
        void* messageBuffer = AllocateMemory(MESSAGE_SIZE, static_cast<int>(returnAddr));
        if (!messageBuffer) {
            LOG_ERROR("[ActionScript] AllocateMemory failed");
            return false;
        }

        // Create parameter buffers on stack (0x34 bytes each)
        char boolParam[PARAM_SIZE];
        char stringParam[PARAM_SIZE];

        // Zero out the parameter buffers
        memset(boolParam, 0, PARAM_SIZE);
        memset(stringParam, 0, PARAM_SIZE);

        // Initialize boolean parameter
        SetBoolValue(boolParam, show);

        // Initialize string parameter
        InitStringEmpty(stringParam);
        SetStringFromCStr(stringParam, loadingText);

        // Create message with 2 parameters using CreateMessageAndSend
        LOG_VERBOSE("[ActionScript] Creating message with 2 parameters...");
        void* message = CreateMessage2Params(
            messageBuffer,
            "trialsEvo2.ViewManager.showLoadingScreen",
            boolParam,
            stringParam
        );

        if (!message) {
            LOG_ERROR("[ActionScript] CreateMessage2Params failed");
            return false;
        }

        LOG_VERBOSE("[ActionScript] Sending message...");
        SendMessage(messageHandler, reinterpret_cast<int>(message), UI_CHANNEL, 0);
        LOG_VERBOSE("[ActionScript] Success!");
        return true;
    }

    bool ShowStartCountdown(int countdownValue) {
        LOG_VERBOSE("[ActionScript] ShowStartCountdown (value=" << countdownValue << ")");

        if (!CreateMessage2Params || !SendMessage || !AllocateMemory || !SetFloatValue || !InitStringEmpty) {
            LOG_ERROR("[ActionScript] Not initialized");
            return false;
        }

        void* messageHandler = GetMessageHandler();
        if (!messageHandler) {
            LOG_ERROR("[ActionScript] Failed to get message handler");
            return false;
        }

        uintptr_t returnAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());
        void* messageBuffer = AllocateMemory(MESSAGE_SIZE, static_cast<int>(returnAddr));

        if (!messageBuffer) {
            LOG_ERROR("[ActionScript] AllocateMemory failed");
            return false;
        }

        // Create parameter buffers on stack (0x34 bytes each)
        char floatParam[PARAM_SIZE];
        char unknownParam[PARAM_SIZE];

        // Zero out the parameter buffers
        memset(floatParam, 0, PARAM_SIZE);
        memset(unknownParam, 0, PARAM_SIZE);

        // Initialize float parameter with countdown value
        SetFloatValue(floatParam, static_cast<float>(countdownValue));

        // Initialize the second parameter (appears to be an empty string/structure)
        InitStringEmpty(unknownParam);

        // Create message with 2 parameters
        LOG_VERBOSE("[ActionScript] Creating message with countdown value " << countdownValue);
        void* message = CreateMessage2Params(
            messageBuffer,
            "trialsFmx.MenuManager.execute.showStartCount",
            floatParam,
            unknownParam
        );

        if (!message) {
            LOG_ERROR("[ActionScript] CreateMessage2Params failed");
            return false;
        }

        LOG_VERBOSE("[ActionScript] Sending message...");
        SendMessage(messageHandler, reinterpret_cast<int>(message), UI_CHANNEL, 0);
        LOG_VERBOSE("[ActionScript] Success!");
        return true;
    }

    bool ShowStartCountdownDirect(int countdownValue) {
        LOG_VERBOSE("[ActionScript] ShowStartCountdownDirect (value=" << countdownValue << ")");

        if (g_BaseAddress == 0) {
            LOG_ERROR("[ActionScript] Not initialized");
            return false;
        }

        // Call the game's native ShowStartCountdown function directly at 0x00b44330
        typedef void(__cdecl* ShowStartCountdown_t)(void* param1, int param2);
        ShowStartCountdown_t nativeFunc = reinterpret_cast<ShowStartCountdown_t>(g_BaseAddress + (0x00b44330 - GAME_BASE));

        LOG_VERBOSE("[ActionScript] Calling native function at 0x" << std::hex << (g_BaseAddress + (0x00b44330 - GAME_BASE)) << std::dec);

        // Call with countdown value as first param, 0 as second
        nativeFunc(reinterpret_cast<void*>(countdownValue), 0);

        LOG_VERBOSE("[ActionScript] Native call complete!");
        return true;
    }

    bool ShowFullCountdownSequence() {
        LOG_VERBOSE("[ActionScript] ShowFullCountdownSequence - Starting countdown!");

        // Show 3
        if (ShowStartCountdownDirect(5)) {
            LOG_VERBOSE("[ActionScript] Displayed: 3");
            Sleep(400); 

            // Show 2
            if (ShowStartCountdownDirect(2)) {
                LOG_VERBOSE("[ActionScript] Displayed: 2");
                Sleep(400);

                // Show 1
                if (ShowStartCountdownDirect(1)) {
                    LOG_VERBOSE("[ActionScript] Displayed: 1");
                    Sleep(400);

                    // Show GO (0)
                    if (ShowStartCountdownDirect(0)) {
                        LOG_VERBOSE("[ActionScript] Displayed: GO");
                        Sleep(400);

                        // Hide countdown
                        ShowStartCountdownDirect(-1);
                        LOG_VERBOSE("[ActionScript] Ready!");
                        return true;
                    }
                }
            }
        }

        LOG_ERROR("[ActionScript] Countdown sequence failed");
        return false;
    }

    bool FinishRaceDirect() {
        LOG_VERBOSE("[ActionScript] FinishRaceDirect - Setting race state to 9 (RACE_FINISHED)");

        if (g_BaseAddress == 0 || !SetGameState) {
            LOG_ERROR("[ActionScript] Not initialized");
            return false;
        }

        // Get the game state manager object using version-specific RVA + 0xfc
        uintptr_t* pGameStatePtr = reinterpret_cast<uintptr_t*>(g_BaseAddress + g_GameManagerPtr);
        if (!pGameStatePtr) {
            LOG_ERROR("[ActionScript] Failed to get game state pointer");
            return false;
        }

        uintptr_t gameStateBase = *pGameStatePtr;
        if (gameStateBase == 0) {
            LOG_ERROR("[ActionScript] Game state base is null");
            return false;
        }

        void** ppGameStateManager = reinterpret_cast<void**>(gameStateBase + 0xfc);
        if (!ppGameStateManager || !*ppGameStateManager) {
            LOG_ERROR("[ActionScript] Game state manager is null");
            return false;
        }

        LOG_VERBOSE("[ActionScript] Calling SetGameState(manager=0x" << std::hex
            << reinterpret_cast<uintptr_t>(*ppGameStateManager) << std::dec
            << ", state=9)");

        // Call the SetGameState function with state 9 (RACE_FINISHED)
        SetGameState(*ppGameStateManager, 9);

        LOG_VERBOSE("[ActionScript] Successfully set game state to RACE_FINISHED!");
        return true;
    }

    // SEH-safe helper to read pointer
    static bool SafeReadPointer(void** ptr, void** outValue) {
        __try {
            *outValue = *ptr;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    
    // SEH-safe helper to read uintptr_t
    static bool SafeReadUintPtr(uintptr_t* ptr, uintptr_t* outValue) {
        __try {
            *outValue = *ptr;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    
    // SEH-safe helper to read int
    static bool SafeReadInt(int* ptr, int* outValue) {
        __try {
            *outValue = *ptr;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // SEH-safe wrapper for HandleRaceFinish call
    static bool CallHandleRaceFinishInternal(void* gameManager, void** ppStateManager, int currentState) {
        __try {
            // Get current state pointer
            int* pState = reinterpret_cast<int*>((uintptr_t)gameManager + 8);
            
            // Strategy 1: If state < 6, set it to 6 first
            if (currentState < 6 && SetGameState && ppStateManager && *ppStateManager) {
                SetGameState(*ppStateManager, 6);
                Sleep(100);
            }

            // Strategy 2: Call HandleRaceFinish
            if (HandleRaceFinish && gameManager) {
                HandleRaceFinish(gameManager, 1, nullptr, nullptr);
                return true;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool CallHandleRaceFinish() {
        if (g_BaseAddress == 0 || !HandleRaceFinish || !SetGameState) {
            LOG_ERROR("[ActionScript] Not initialized");
            return false;
        }

        // Get the game manager pointer using version-specific RVA
        uintptr_t* pGamePtr = reinterpret_cast<uintptr_t*>(g_BaseAddress + g_GameManagerPtr);
        if (!pGamePtr) {
            LOG_ERROR("[ActionScript] pGamePtr is null");
            return false;
        }
        
        LOG_VERBOSE("[ActionScript] Using " << (g_IsSteamVersion ? "Steam" : "Uplay") << " game manager ptr at 0x" << std::hex << (g_BaseAddress + g_GameManagerPtr) << std::dec);
        
        uintptr_t gameBase = 0;
        if (!SafeReadUintPtr(pGamePtr, &gameBase)) {
            LOG_ERROR("[ActionScript] Crash reading game pointer");
            return false;
        }
        
        if (gameBase == 0) {
            LOG_ERROR("[ActionScript] Game pointer is null");
            return false;
        }
        
        LOG_VERBOSE("[ActionScript] Game base: 0x" << std::hex << gameBase << std::dec);

        void** ppGameManager = reinterpret_cast<void**>(gameBase + 0xdc);
        if (!ppGameManager) {
            LOG_ERROR("[ActionScript] ppGameManager is null");
            return false;
        }

        void* gameManager = nullptr;
        if (!SafeReadPointer(ppGameManager, &gameManager)) {
            LOG_ERROR("[ActionScript] Crash reading game manager");
            return false;
        }
        
        if (!gameManager) {
            LOG_ERROR("[ActionScript] Game manager is null");
            return false;
        }
        
        LOG_VERBOSE("[ActionScript] Game manager: 0x" << std::hex << (uintptr_t)gameManager << std::dec);

        // Get current state
        int currentState = 0;
        int* pState = reinterpret_cast<int*>((uintptr_t)gameManager + 8);
        if (!SafeReadInt(pState, &currentState)) {
            LOG_ERROR("[ActionScript] Crash reading game state");
            return false;
        }

        LOG_VERBOSE("[ActionScript] Current game state: " << currentState);

        // Get state manager for setting state
        void** ppStateManager = reinterpret_cast<void**>(gameBase + 0xfc);
        if (!ppStateManager) {
            LOG_ERROR("[ActionScript] ppStateManager is null");
            return false;
        }
        
        void* stateManager = nullptr;
        if (!SafeReadPointer(ppStateManager, &stateManager)) {
            LOG_ERROR("[ActionScript] Crash reading state manager");
            return false;
        }
        
        if (!stateManager) {
            LOG_ERROR("[ActionScript] State manager is null");
            return false;
        }

        LOG_VERBOSE("[ActionScript] Calling HandleRaceFinish...");
        
        bool success = CallHandleRaceFinishInternal(gameManager, ppStateManager, currentState);
        
        if (success) {
            LOG_VERBOSE("[ActionScript] HandleRaceFinish called successfully!");
        } else {
            LOG_ERROR("[ActionScript] HandleRaceFinish crashed or failed!");
            LOG_ERROR("[ActionScript] Base address was: 0x" << std::hex << g_BaseAddress << std::dec);
            LOG_ERROR("[ActionScript] HandleRaceFinish address: 0x" << std::hex << (uintptr_t)HandleRaceFinish << std::dec);
        }

        return success;
    }

}
