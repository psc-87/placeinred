#pragma once
#pragma warning(disable: 4200) // Non-standard extension used: zero-sized array

// =========================================================================================
// INCLUDES
// =========================================================================================
#include "shlobj.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <future>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// F4SE Common
#include "f4se_common/BranchTrampoline.h"
#include "f4se_common/f4se_version.h"
#include "f4se_common/Relocation.h"
#include "f4se_common/SafeWrite.h"
#include "f4se_common/Utilities.h"

// F4SE API
#include "f4se/GameExtraData.h"
#include "f4se/GameData.h"
#include "f4se/GameForms.h"
#include "f4se/GameObjects.h"
#include "f4se/GameReferences.h"
#include "f4se/ObScript.h"
#include "f4se/PapyrusVM.h"
#include "f4se/PluginAPI.h"
#include "f4se/GameMenus.h"
#include "f4se/ScaleformLoader.h"


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
// HELPER STRUCTS
// =========================================================================================

struct SimpleFinder
{
    uintptr_t* ptr = nullptr; // Pointer to a pattern match
    SInt32           r32 = 0;       // Rel32 offset
    uintptr_t        addr = 0;       // Final resolved address
    ObScriptCommand* cmd = nullptr; // For console commands
};

// =========================================================================================
// MAIN CLASS
// =========================================================================================

class PlaceInRed
{
public:
    // -------------------------------------------------------------------------------------
    // Initialization & Base
    // -------------------------------------------------------------------------------------
    PlaceInRed()
        : FO4BaseAddr(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)))
    {
    }

    void Log(const char* callerFunc, const char* fmt, ...)
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


    uintptr_t FO4BaseAddr = 0;
    uintptr_t GetFO4BaseAddress() const { return FO4BaseAddr; }

    // -------------------------------------------------------------------------------------
    // Performance Tracking
    // -------------------------------------------------------------------------------------
    UInt64 start_tickcount = GetTickCount64();
    UInt64 end_tickcount = 0;

    // -------------------------------------------------------------------------------------
    // F4SE Interfaces & Handles
    // -------------------------------------------------------------------------------------
    IDebugLog               debuglog;
    PluginHandle            pluginHandle = kPluginHandle_Invalid;
    F4SEPapyrusInterface*   g_papyrus = nullptr;
    F4SEMessagingInterface* g_messaging = nullptr;
    F4SEObjectInterface*    g_object = nullptr;
    F4SETaskInterface*      g_task = nullptr;

    const char* plugin_log_file = "\\My Games\\Fallout4\\F4SE\\PlaceInRed.log";
    std::string plugin_ini_path = "Data\\F4SE\\Plugins\\PlaceInRed.ini";
    const char* sLockObjectSound = "UIQuestInactive";

    // -------------------------------------------------------------------------------------
    // Settings & State Flags
    // -------------------------------------------------------------------------------------
    bool    bF4SEGameDataIsReady = false;
    bool    PLACEINRED_ENABLED = false; // pir 1w
    bool    OBJECTSNAP_ENABLED = true;  // pir 2
    bool    GROUNDSNAP_ENABLED = true;  // pir 3
    bool    SLOW_ENABLED = false; // pir 4
    bool    WORKSHOPSIZE_ENABLED = false; // pir 5
    bool    OUTLINES_ENABLED = true;  // pir 6
    bool    ACHIEVEMENTS_ENABLED = false; // pir 7
    bool    ConsoleNameRef_ENABLED = false; // pir cnref
    bool    PrintConsoleMessages = true;
    bool    bAllowConsoleInSurvival = false;

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
    // Memory Scanning Pointers (Patterns)
    // -------------------------------------------------------------------------------------
    uintptr_t* A = nullptr;
    uintptr_t* B = nullptr;
    uintptr_t* C = nullptr;
    uintptr_t* D = nullptr;
    uintptr_t* E = nullptr;
    uintptr_t* F = nullptr;
    uintptr_t* G = nullptr;
    uintptr_t* H = nullptr;
    uintptr_t* J = nullptr;
    uintptr_t* R = nullptr;
    uintptr_t* RC = nullptr;
    uintptr_t* Y = nullptr;
    uintptr_t* CORRECT = nullptr;
    uintptr_t* wstimer = nullptr;
    uintptr_t* gsnap = nullptr;
    uintptr_t* osnap = nullptr;
    uintptr_t* outlines = nullptr;
    uintptr_t* achievements = nullptr;
    uintptr_t* moveworkbench = nullptr;
    uintptr_t* survivalconsole = nullptr;

    // -------------------------------------------------------------------------------------
    // Patch Bytes (Ops Codes)
    // -------------------------------------------------------------------------------------

    // Standard NOPs
    UInt8  NOP3[3] = { 0x0F, 0x1F, 0x00 };
    UInt8  NOP4[4] = { 0x0F, 0x1F, 0x40, 0x00 };
    UInt8  NOP5[5] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };
    UInt8  NOP6[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };
    UInt8  NOP7[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };
    UInt8  NOP8[8] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };

    // Function Specific Patches
    UInt8  C_OLD[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };       // init NOP7 -> movzx eax, byte ptr [...]
    UInt8  C_NEW[7] = { 0x31, 0xC0, 0x0F, 0x1F, 0x44, 0x00, 0x00 };       // xor al,al; nop x5

    UInt8  CC_OLD[2] = { 0x75, 0x11 };                                     // JNE 0x11
    UInt8  CC_NEW[2] = { 0xEB, 0x1C };                                     // JMP 0x1C

    UInt8  D_OLD[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };       // init NOP7 -> movzx eax, byte ptr [...]
    UInt8  D_NEW[7] = { 0x31, 0xC0, 0xB0, 0x01, 0x90, 0x90, 0x90 };       // xor al,al; mov al,01; nop x3

    UInt8  F_OLD[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };             // init NOP6 -> mov [...], al

    UInt8  J_OLD[2] = { 0x74, 0x35 };                                     // JE 0x35
    UInt8  J_NEW[2] = { 0xEB, 0x30 };                                     // JMP 0x30

    UInt8  Y_OLD[3] = { 0x8B, 0x58, 0x14 };                               // mov rbx, [rax+14]

    UInt8  TIMER_OLD[6] = { 0x0F, 0x85, 0xAB, 0x00, 0x00, 0x00 };             // JNE
    UInt8  TIMER_NEW[6] = { 0xE9, 0xAC, 0x00, 0x00, 0x00, 0x90 };             // JMP + NOP

    UInt8  RC_OLD[5] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };                   // init NOP5 -> call (relative)

    UInt8  ACHIEVE_OLD[4] = { 0x48, 0x83, 0xEC, 0x28 };                         // sub rsp, 28
    UInt8  ACHIEVE_NEW[3] = { 0x30, 0xC0, 0xC3 };                               // xor al, al; ret

    UInt8  OSNAP_OLD[8] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 }; // init NOP8 -> movss xmm, [...]
    UInt8  OSNAP_NEW[8] = { 0x0F, 0x57, 0xF6, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // xorps xmm6, xmm6; NOP5

    UInt8  DRAWS_OLD[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };             // init NOP6 -> add [...], ...
    UInt8  TRIS_OLD[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };             // init NOP6 -> add [...], ...

    // Workshop & References
    UInt8  CNameRef_OLD[6] = { 0xFF, 0x90, 0xD0, 0x01, 0x00, 0x00 };            // call qword [rax+1D0]
    size_t CNameRef_OLD_Size = sizeof(CNameRef_OLD) / sizeof(CNameRef_OLD[0]);

    UInt32 CurrentWSRef_Offsets[4] = { 0x0, 0x0, 0x10, 0x110 };
    size_t CurrentWSRef_OffsetsSize = sizeof(CurrentWSRef_Offsets) / sizeof(CurrentWSRef_Offsets[0]);

    UInt8  TWO_ZEROS[2] = { 0x00, 0x00 }; // written to bWSMode+0x03
    UInt8  TWO_ONES[2] = { 0x01, 0x01 }; // written to bWSMode+0x09

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

    // -------------------------------------------------------------------------------------
    // Help Message
    // -------------------------------------------------------------------------------------
    const char* ConsoleHelpMSG =
        "PlaceInRed (pir) - Command Reference\n"
        "==============================================================\n"
        "[ Toggles ]\n"
        "  pir toggle              Toggle Place in Red\n"
        "  pir osnap               Toggle object snapping\n"
        "  pir gsnap               Toggle ground snapping\n"
        "  pir slow                Toggle slower rotate/zoom speed\n"
        "  pir workshopsize        Toggle unlimited workshop build size\n"
        "  pir outlines            Toggle object outlines\n"
        "  pir achievements        Toggle achievements with mods\n"
        "[ Scaling ]\n"
        "  pir scaleup<N>          Scale up by N percent\n"
        "  pir scaledown<N>        Scale down by N percent\n"
        "                          (N = 1, 2, 5, 10, 25, 50, 75, 100)\n"
        "[ Rotation ]\n"
        "  pir x<N>                Rotate +N degrees (X-axis)\n"
        "  pir x-<N>               Rotate -N degrees (X-axis)\n"
        "  pir y<N>                Rotate +N degrees (Y-axis)\n"
        "  pir y-<N>               Rotate -N degrees (Y-axis)\n"
        "  pir z<N>                Rotate +N degrees (Z-axis)\n"
        "  pir z-<N>               Rotate -N degrees (Z-axis)\n"
        "                          (N = 0.1, 0.5, 1, 2, 5, 10, 15, 30, 45)\n"
        "                          (N = c for custom degrees in .ini)\n"
        "[ Object Physics ]\n"
        "  pir lock                Lock object (disable physics)\n"
        "  pir lockq               Lock object (no sound FX)\n"
        "  pir unlock              Unlock object (enable physics)\n"
        "[ Miscellaneous ]\n"
        "  pir wb                  Toggle allow moving workbench\n"
        "  pir cnref               Show ref name in console when clicked\n"
        "==============================================================\n";


private:
    // private things
};



extern PlaceInRed pir;
#define pirlog(...) pir.Log(__func__, __VA_ARGS__)