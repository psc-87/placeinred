#pragma once
#include "shlobj.h"
#include "pattern.h"
#include "f4se.h"
#include <string.h>

//cool purple macro to set this
#define PIR_LOG_PREP const char* thisfunc = __func__;

// f4se plugin
extern IDebugLog pirlog;
static PluginHandle pirPluginHandle = kPluginHandle_Invalid;
static F4SEPapyrusInterface* g_papyrus = nullptr;
static F4SEMessagingInterface* g_messaging = nullptr;
static F4SEObjectInterface* g_object = nullptr;
static F4SETaskInterface* g_task = nullptr;

//typedefs
typedef float  (*_GetScale)              (TESObjectREFR* objRef);
typedef void   (*_SetScale)              (TESObjectREFR* objRef, float scale);
typedef bool   (*_GetConsoleArg)         (void* paramInfo, void* scriptData, void* opcodeOffsetPtr, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, ...);

typedef void(*_SetMotionType_Native)(
    VirtualMachine* vm,             // rcx - VM (BSScript::Internal::VirtualMachine)
    uint32_t stackID,               // edx - Stack
    TESObjectREFR* objectReference, // r8  - TESObjectREFR
    int motionType,                 // r9  - 00000002 (motiontype)
    bool allowActivate
);

RelocAddr <_SetMotionType_Native> SetMotionType_Native(0x010D7B50);


// misc strings and vars
static UInt32 pluginVersion = 8;
static const char pluginName[] = { "PlaceInRed" };
static const char* pluginINI_end = { "Data\\F4SE\\Plugins\\PlaceInRed.ini" };
static const char pluginLogFile[] = { "\\My Games\\Fallout4\\F4SE\\PlaceInRed.log" };
static const char* pirunknowncommandmsg = { "PlaceInRed (pir) usage:\n pir toggle (pir 1) toggle place in red\n pir osnap (pir 2) toggle object snapping\n pir gsnap (pir 3) toggle ground snapping\n pir slow (pir 4) slow object rotation and zoom speed\n pir workshopsize (pir 5) unlimited workshop build size\n pir outlines (pir 6) toggle object outlines\n pir achievements (pir 7) toggle achievement feature\n pir scaleup1   (also: 1, 5, 10, 25, 50, 100) scale up percent\n pir scaledown1   (also: 1, 5, 10, 25, 50, 75) scale down percent\n" };

