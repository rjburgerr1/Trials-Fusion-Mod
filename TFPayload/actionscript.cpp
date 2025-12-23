#include "pch.h"
#include "actionscript.h"
#include "logging.h"
#include <string>
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace ActionScript {
    static HMODULE gameModule = nullptr;
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

    bool Initialize() {
        gameModule = GetModuleHandleA(nullptr);
        if (!gameModule) {
            LOG_ERROR("[ActionScript] Initialize - Failed to get game module");
            return false;
        }

        uintptr_t base = reinterpret_cast<uintptr_t>(gameModule);

        // Get function pointers
        AllocateMemory = reinterpret_cast<AllocateMemory_t>(base + ALLOCATE_MEMORY_ADDR);
        CreateMessageObject = reinterpret_cast<CreateMessageObject_t>(base + CREATE_MESSAGE_ADDR);
        CreateMessage2Params = reinterpret_cast<CreateMessage2Params_t>(base + CREATE_MESSAGE_2PARAMS_ADDR);
        CreateMessageWithParameters = reinterpret_cast<CreateMessageWithParameters_t>(base + CREATE_MESSAGE_PARAMS_ADDR);
        SendMessage = reinterpret_cast<SendMessage_t>(base + SEND_MESSAGE_ADDR);
        SetBoolValue = reinterpret_cast<SetBoolValue_t>(base + SET_BOOL_VALUE_ADDR);
        SetFloatValue = reinterpret_cast<SetFloatValue_t>(base + SET_FLOAT_VALUE_ADDR);
        InitStringEmpty = reinterpret_cast<InitStringEmpty_t>(base + INIT_STRING_EMPTY_ADDR);
        SetStringFromCStr = reinterpret_cast<SetStringFromCStr_t>(base + SET_STRING_FROM_CSTR_ADDR);
        SetGameState = reinterpret_cast<SetGameState_t>(base + SET_GAME_STATE_ADDR);
        HandleRaceFinish = reinterpret_cast<HandleRaceFinish_t>(base + HANDLE_RACE_FINISH_ADDR);
        GetFirstEntity = reinterpret_cast<GetFirstEntity_t>(base + GET_FIRST_ENTITY_ADDR);

        LOG_VERBOSE("[ActionScript] Initialize - Successfully initialized");
        LOG_VERBOSE("  Base: 0x" << std::hex << base << std::dec);

        return true;
    }

    void* GetMessageHandler() {
        if (!gameModule) return nullptr;

        uintptr_t base = reinterpret_cast<uintptr_t>(gameModule);

        uintptr_t* ptrToHandler = reinterpret_cast<uintptr_t*>(base + MESSAGE_HANDLER_PTR);
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

        if (!gameModule) {
            LOG_ERROR("[ActionScript] Not initialized");
            return false;
        }

        // Call the game's native ShowStartCountdown function directly at 0x00b44330
        uintptr_t base = reinterpret_cast<uintptr_t>(gameModule);
        typedef void(__cdecl* ShowStartCountdown_t)(void* param1, int param2);
        ShowStartCountdown_t nativeFunc = reinterpret_cast<ShowStartCountdown_t>(base + (0x00b44330 - GAME_BASE));

        LOG_VERBOSE("[ActionScript] Calling native function at 0x" << std::hex << (base + (0x00b44330 - GAME_BASE)) << std::dec);

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

        if (!gameModule || !SetGameState) {
            LOG_ERROR("[ActionScript] Not initialized");
            return false;
        }

        uintptr_t base = reinterpret_cast<uintptr_t>(gameModule);

        // Get the game state manager object at DAT_0174b308 + 0xfc
        uintptr_t* pGameStatePtr = reinterpret_cast<uintptr_t*>(base + (0x0174b308 - GAME_BASE));
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

    bool CallHandleRaceFinish() {
        LOG_VERBOSE("[ActionScript] CallHandleRaceFinish - Attempting to finish race via HandleRaceFinish");

        if (!gameModule || !HandleRaceFinish || !SetGameState) {
            LOG_ERROR("[ActionScript] Not initialized");
            return false;
        }

        uintptr_t base = reinterpret_cast<uintptr_t>(gameModule);

        // Get the game manager pointer at DAT_0174b308 + 0xdc
        uintptr_t* pGamePtr = reinterpret_cast<uintptr_t*>(base + (0x0174b308 - GAME_BASE));
        if (!pGamePtr || *pGamePtr == 0) {
            LOG_ERROR("[ActionScript] Game pointer is null");
            return false;
        }

        uintptr_t gameBase = *pGamePtr;
        void** ppGameManager = reinterpret_cast<void**>(gameBase + 0xdc);

        if (!ppGameManager || !*ppGameManager) {
            LOG_ERROR("[ActionScript] Game manager is null");
            return false;
        }

        void* gameManager = *ppGameManager;

        // Get current state
        int* pState = reinterpret_cast<int*>((uintptr_t)gameManager + 8);
        int currentState = *pState;

        LOG_VERBOSE("[ActionScript] Current game state: " << currentState);

        // Get state manager for setting state
        void** ppStateManager = reinterpret_cast<void**>(gameBase + 0xfc);
        if (!ppStateManager || !*ppStateManager) {
            LOG_ERROR("[ActionScript] State manager is null");
            return false;
        }

        // Strategy 1: If state < 6, set it to 6 first
        if (currentState < 6) {
            LOG_VERBOSE("[ActionScript] State is < 6, setting to 6 first...");
            SetGameState(*ppStateManager, 6);
            Sleep(100); // Give it a moment
            currentState = *pState;
            LOG_VERBOSE("[ActionScript] New state: " << currentState);
        }

        // Strategy 2: Call HandleRaceFinish with param_1 = 1 (normal finish)
        LOG_VERBOSE("[ActionScript] Calling HandleRaceFinish(manager=0x" << std::hex
            << reinterpret_cast<uintptr_t>(gameManager) << std::dec
            << ", param_1=1, param_2=NULL, param_3=NULL)");

        HandleRaceFinish(gameManager, 1, nullptr, nullptr);

        LOG_VERBOSE("[ActionScript] HandleRaceFinish called successfully!");

        // Check final state
        currentState = *pState;
        LOG_VERBOSE("[ActionScript] Final state: " << currentState);

        return true;
    }

}
