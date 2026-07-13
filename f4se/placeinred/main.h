#pragma once
#pragma warning(disable: 4200) // Non-standard extension used: zero-sized array

#define pirlog(...) pir.Log(__func__, __VA_ARGS__)

constexpr auto PI_20_DIGITS = 3.14159265358979323846;

// =========================================================================================
// INCLUDES
// =========================================================================================
#include <Windows.h>
#include <corecrt.h>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <future>
#include <string>
#include <vector>

// F4SE API
#include "common/IDebugLog.h"
#include "common/ITypes.h"
#include "f4se/GameReferences.h"
#include "f4se/PapyrusVM.h"
#include "f4se/PluginAPI.h"


// =========================================================================================
// TYPE ALIASES (FUNCTION POINTERS)
// =========================================================================================
using _ParseConsoleArg_Native = bool (*)(void* paramInfo, void* scriptData, void* opcodeOffsetPtr, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, ...);
using _SetMotionType_Native = void (*)(VirtualMachine* vm, uint32_t stackID, TESObjectREFR* objectReference, int motionType, bool allowActivate);
using _PlayUISound_Native = void (*)(const char*);
using _PlayFileSound_Native = void (*)(const char*);
using _GetScale_Native = float (*)(TESObjectREFR* objRef);
using _SetScale_Native = void (*)(TESObjectREFR* objRef, float scale);

// =========================================================================================
// GLOBAL FUNCTION POINTERS
// =========================================================================================

// Marked inline to prevent multiply-defined symbol errors if included in multiple TUs
inline _SetMotionType_Native   SetMotionType_native = nullptr;
inline _ParseConsoleArg_Native ParseConsoleArg_native = nullptr;

// =========================================================================================
// MAIN CLASS
// =========================================================================================

class PlaceInRed
{
public:
    // -------------------------------------------------------------------------------------
    // Init
    // -------------------------------------------------------------------------------------
    PlaceInRed()
        : FO4BaseAddr(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)))
        , vec_patscan()
    {
        vec_patscan.reserve(40);
    }

    void Log(const char* callerFunc, const char* fmt, ...) const
    {
        // 1. Thread-safe cache of the last "real" function
        static thread_local const char* s_enclosingFunc = "PlaceInRed";

        // 2. If C++ hands us an anonymous lambda operator, throw it away and use the cache
        if (strcmp(callerFunc, "operator ()") != 0 && strcmp(callerFunc, "operator()") != 0)
        {
            s_enclosingFunc = callerFunc;
        }
        else
        {
            callerFunc = s_enclosingFunc;
        }
        static const auto s_bootTime = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();

        // 1. Get total milliseconds elapsed since DLL boot
        const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_bootTime).count();

        const auto ms = totalMs % 1000;
        const auto s = (totalMs / 1000) % 60;
        const auto m = (totalMs / 60000) % 60;
        const auto h = (totalMs / 3600000);

        char userMsg[1024];
        va_list args;
        va_start(args, fmt);
        _vsnprintf_s(userMsg, sizeof(userMsg), _TRUNCATE, fmt, args);
        va_end(args);

        char finalBuf[2048];
        _snprintf_s(finalBuf, sizeof(finalBuf), _TRUNCATE,
            "[%02lld:%02lld:%02lld.%03lld] [%s] %s",
            h, m, s, ms, callerFunc, userMsg);

        debuglog.FormattedMessage(finalBuf);
    }

	// base address set when the DLL is loaded, used for calculating offsets
    uintptr_t FO4BaseAddr = 0;
    uintptr_t GetFO4BaseAddress() const { return FO4BaseAddr; }
    std::vector<std::future<void>> vec_patscan;   // member of the class

    // -------------------------------------------------------------------------------------
    // F4SE Interfaces & Handles
    // -------------------------------------------------------------------------------------
    IDebugLog               debuglog;
    PluginHandle            pluginHandle = kPluginHandle_Invalid;
    F4SEMessagingInterface* g_messaging = nullptr;
    F4SEObjectInterface*    g_object = nullptr;

    const char* plugin_log_file = "\\My Games\\Fallout4\\F4SE\\PlaceInRed.log";
    std::string plugin_ini_path = "Data\\F4SE\\Plugins\\PlaceInRed.ini";
    const char* sLockObjectSound = "UIQuestInactive";

    // -------------------------------------------------------------------------------------
    // Settings & State Flags
    // -------------------------------------------------------------------------------------
    bool bF4SEGameDataIsReady = false;
    bool PLACEINRED_ENABLED = false; // pir 1
    bool OBJECTSNAP_ENABLED = true;  // pir 2
    bool GROUNDSNAP_ENABLED = true;  // pir 3
    bool SLOW_ENABLED = false; // pir 4
    bool WORKSHOPSIZE_ENABLED = false; // pir 5
    bool OUTLINES_ENABLED = true;  // pir 6
    bool ACHIEVEMENTS_ENABLED = false; // pir 7
    bool ConsoleNameRef_ENABLED = false; // pir cnref
    bool PrintConsoleMessages = true;
    bool bAllowConsoleInSurvival = false;
	UInt32 uLockUnlockLastMotionType = 2; // track the last motion type when using the lockunlock command (default to Motion_Keyframed)

    // -------------------------------------------------------------------------------------
    // Transformation Constants
    // -------------------------------------------------------------------------------------
    Float32 fOriginalZOOM = 10.0000F; // Workshop Default
    Float32 fOriginalROTATE = 5.0000F;  // Workshop Default
    Float32 fSlowerZOOM = 1.0000F;  // INI Default
    Float32 fSlowerROTATE = 0.5000F;  // INI Default

    // Custom Rotation increments
    Float32 fRotateDegreesCustomX = 3.6000F;
    Float32 fRotateDegreesCustomY = 3.6000F;
    Float32 fRotateDegreesCustomZ = 3.6000F;



    // -------------------------------------------------------------------------------------
    // Native Hook Data
    // -------------------------------------------------------------------------------------

    // Scale Functions
    _GetScale_Native  GetScale_func = nullptr;
    uintptr_t* GetScale_pattern = nullptr;
    SInt32            GetScale_r32 = 0;

    _SetScale_Native  SetScale_func = nullptr;
    uintptr_t* SetScale_pattern = nullptr;
    SInt32            SetScale_s32 = 0;

    // Sound Functions
    _PlayUISound_Native   PlaySound_UI_func = nullptr;
    uintptr_t* PlaySound_UI_pattern = nullptr;
    SInt32                PlaySound_UI_r32 = 0;

    _PlayFileSound_Native PlaySound_File_func = nullptr;
    uintptr_t* PlaySound_File_pattern = nullptr;
    SInt32                PlaySound_File_r32 = 0;

    // Console Reference Name Hook
    uintptr_t* cnref_original_call_pattern = nullptr; // Call site when ref is clicked
    uintptr_t* cnref_GetRefName_pattern = nullptr; // Target function pattern
    uintptr_t  cnref_GetRefName_addr = 0;       // Target function address
    SInt32     cnref_GetRefName_r32 = 0;       // Rel32 offset


    inline static constexpr char ConsoleHelpMSG[] =
        "PlaceInRed (pir) - Command Reference\n"
        "==============================================================\n"
        "[ Toggles ]\n"
        "  pir toggle            Toggle Place in Red\n"
        "  pir osnap             Toggle object snapping\n"
        "  pir gsnap             Toggle ground snapping\n"
        "  pir slow              Toggle slower rotate/zoom speed\n"
        "  pir workshopsize      Toggle unlimited workshop build size\n"
        "  pir outlines          Toggle object outlines\n"
        "  pir achievements      Toggle achievements while using mods\n"
        "\n"
        "[ Scaling ]\n"
        "  pir scaleup<N>        Scale selected object up by N percent\n"
        "  pir scaledown<N>      Scale selected object down by N percent\n"
        "                       N = 1, 2, 5, 10, 25, 50, 75, 100\n"
        "                       Example: pir scaleup10\n"
        "\n"
        "[ Rotation ]\n"
        "  pir x<N>              Rotate +N degrees on the X axis\n"
        "  pir x-<N>             Rotate -N degrees on the X axis\n"
        "  pir y<N>              Rotate +N degrees on the Y axis\n"
        "  pir y-<N>             Rotate -N degrees on the Y axis\n"
        "  pir z<N>              Rotate +N degrees on the Z axis\n"
        "  pir z-<N>             Rotate -N degrees on the Z axis\n"
        "                       N = 0.1, 0.5, 1, 2, 5, 10, 15, 30, 45\n"
        "                       Example: pir z45\n"
        "\n"
        "[ Custom Rotation ]\n"
        "  pir xc                Rotate +custom degrees on the X axis\n"
        "  pir x-c               Rotate -custom degrees on the X axis\n"
        "  pir yc                Rotate +custom degrees on the Y axis\n"
        "  pir y-c               Rotate -custom degrees on the Y axis\n"
        "  pir zc                Rotate +custom degrees on the Z axis\n"
        "  pir z-c               Rotate -custom degrees on the Z axis\n"
        "                       Custom degrees are set in placeinred.ini\n"
        "\n"
        "[ Object Physics ]\n"
        "  pir lock              Lock object and disable physics\n"
        "  pir lockq             Lock object silently, with no sound FX\n"
        "  pir unlock            Unlock object and enable physics\n"
        "\n"
        "[ Miscellaneous ]\n"
        "  pir wb                Toggle allowing the workbench to be moved\n"
        "  pir cnref             Show ref name in console when clicked\n"
        "==============================================================\n";


private:
    // private things
};

