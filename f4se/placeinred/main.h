#pragma once
#pragma warning(disable: 4200) // Non-standard extension used: zero-sized array

// INCLUDES
#include <Windows.h>
#include <corecrt.h>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <future>
#include <string>
#include <vector>
#include <source_location> //std::source_location::current()


// F4SE API
#include "common/IDebugLog.h"
#include "common/ITypes.h"
#include "f4se/PluginAPI.h"

#define pirlog(...) pir.Log(std::source_location::current(), __VA_ARGS__)
//constexpr auto PI_20_DIGITS = 3.14159265358979323846;

// =========================================================================================
// PlaceInRed Class
// =========================================================================================
class PlaceInRed
{
public:
    // -------------------------------------------------------------------------------------
    // Configuration / State Flags
    // -------------------------------------------------------------------------------------
    UInt32 uLockUnlockLastMotionType = 2; // track the last motion type when using the lockunlock command (default to Motion_Keyframed)

	bool bF4SEGameDataIsReady = false; // set true when f4se sends kMessage_GameDataReady
    bool PLACEINRED_ENABLED = false; // pir 1
    bool OBJECTSNAP_ENABLED = true;  // pir 2
    bool GROUNDSNAP_ENABLED = true;  // pir 3
    bool SLOW_ENABLED = false; // pir 4
    bool WORKSHOPSIZE_ENABLED = false; // pir 5
    bool OUTLINES_ENABLED = true;  // pir 6
    bool ACHIEVEMENTS_ENABLED = false; // pir 7
    bool ConsoleNameRef_ENABLED = false; // pir cnref (default false copy of another mod)
	bool PrintConsoleMessages = true; // print messages to console when pir commands are used
	bool bAllowConsoleInSurvival = false; // allow console in survival mode (default false copy of another mod)
    bool bWorkbenchMoveEnabled = false; // track if we can move the workbench

    // -------------------------------------------------------------------------------------
    // Workshop Speed / Rotation Values
    // -------------------------------------------------------------------------------------
    Float32 fOriginalZOOM = 10.0000F; // Workshop Default
    Float32 fOriginalROTATE = 5.0000F;  // Workshop Default
    Float32 fSlowerZOOM = 1.0000F;  // INI Default
    Float32 fSlowerROTATE = 0.5000F;  // INI Default
    Float32 fRotateDegreesCustomX = 3.6000F;
    Float32 fRotateDegreesCustomY = 3.6000F;
    Float32 fRotateDegreesCustomZ = 3.6000F;

    // -------------------------------------------------------------------------------------
    // F4SE Interfaces / Handles
    // -------------------------------------------------------------------------------------
    IDebugLog               debuglog;
    PluginHandle            pluginHandle = kPluginHandle_Invalid;
    F4SEMessagingInterface* g_messaging = nullptr;
    F4SEObjectInterface*    g_object = nullptr;

    // -------------------------------------------------------------------------------------
    // Paths / Addresses
    // -------------------------------------------------------------------------------------
    std::string plugin_ini_path = "Data\\F4SE\\Plugins\\PlaceInRed.ini";
    const char* plugin_log_file = "\\My Games\\Fallout4\\F4SE\\PlaceInRed.log";

    uintptr_t FO4BaseAddr = 0;
    uintptr_t GetFO4BaseAddress() const { return FO4BaseAddr; }

    std::vector<std::future<void>> vec_patscan;

    // =====================================================================================
    // Constructor
    // =====================================================================================
    PlaceInRed()
        : FO4BaseAddr(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)))
        , vec_patscan()
    {
        // reserve for upcoming pattern scans
        vec_patscan.reserve(40);
    }

    // =====================================================================================
    // Public Logging (new source_location version)
    // =====================================================================================
    void Log(const std::source_location& loc, const char* fmt, ...) const
    {
        static const auto s_bootTime = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();

        // Get total milliseconds elapsed since DLL boot
        const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_bootTime).count();

        const auto ms = totalMs % 1000;
        const auto s = (totalMs / 1000) % 60;
        const auto m = (totalMs / 60000) % 60;
        const auto h = (totalMs / 3600000);

        // --- MSVC Signature Cleanup ---
        char rawName[256];
        strncpy_s(rawName, sizeof(rawName), loc.function_name(), _TRUNCATE);

        // 1. Find where the actual function name ends
        char* endPtr = strstr(rawName, "::<lambda");
        if (endPtr != nullptr)
        {
            *endPtr = '\0'; // Cut off the lambda garbage completely
        }
        else
        {
            endPtr = strchr(rawName, '(');
            if (endPtr != nullptr)
            {
                *endPtr = '\0'; // Cut off the parameter list
            }
        }

        // 2. Find where the actual function name begins (after the last space)
        char* startPtr = strrchr(rawName, ' ');
        startPtr = (startPtr != nullptr) ? startPtr + 1 : rawName;

        // 3. Assemble the clean name
        char cleanFuncName[256];
        strncpy_s(cleanFuncName, sizeof(cleanFuncName), startPtr, _TRUNCATE);
        // ------------------------------

        char userMsg[1024];
        va_list args;
        va_start(args, fmt);
        _vsnprintf_s(userMsg, sizeof(userMsg), _TRUNCATE, fmt, args);
        va_end(args);

        char finalBuf[2048];
        _snprintf_s(finalBuf, sizeof(finalBuf), _TRUNCATE,
            "[%02lld:%02lld:%02lld.%03lld][%s] %s",
            h, m, s, ms, cleanFuncName, userMsg);

        debuglog.FormattedMessage(finalBuf);
    }

private:

};