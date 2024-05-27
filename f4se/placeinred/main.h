#pragma once
#include "shlobj.h"
#include "pattern.h"
#include "papyrus.h"
#include "f4se.h"

/*
typedef unsigned char		UInt8;		//!< An unsigned 8-bit integer value
typedef unsigned short		UInt16;		//!< An unsigned 16-bit integer value
typedef unsigned long		UInt32;		//!< An unsigned 32-bit integer value
typedef unsigned long long	UInt64;		//!< An unsigned 64-bit integer value
typedef signed char			SInt8;		//!< A signed 8-bit integer value
typedef signed short		SInt16;		//!< A signed 16-bit integer value
typedef signed long			SInt32;		//!< A signed 32-bit integer value
typedef signed long long	SInt64;		//!< A signed 64-bit integer value
typedef float				Float32;	//!< A 32-bit floating point value
typedef double				Float64;	//!< A 64-bit floating point value
*/

// Plugin stuff
extern IDebugLog pluginLog;
static PluginHandle pluginHandle = kPluginHandle_Invalid;
static UInt32 pluginVersion = 8;
static const char pluginName[] = { "Place In Red" };
static const char pluginLogFile[] = { "\\My Games\\Fallout4\\F4SE\\PlaceInRed.log" };

// F4SE interfaces
static F4SEPapyrusInterface* g_papyrus = 0;
static F4SEMessagingInterface* g_messaging = 0;
static F4SEObjectInterface* g_object = nullptr;
static F4SETaskInterface* g_task = nullptr;

// SetScale
typedef void (*_SetScale) (TESObjectREFR* objRef, float scale);
static _SetScale SetScale = nullptr;
static uintptr_t* SetScaleFinder = nullptr;
static SInt32 SetScaleRel32 = 0;

// GetScale
typedef float (*_GetScale) (TESObjectREFR* objRef);
static _GetScale GetScale = nullptr;
static uintptr_t* GetScaleFinder = nullptr;
static SInt32 GetScaleRel32 = 0;

// Console command creation and parsing. Credit to reg2k
typedef bool (*_GetConsoleArg) (void* paramInfo, void* scriptData, void* opcodeOffsetPtr, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, ...);
static _GetConsoleArg GetConsoleArg; // to view console command arguments from user after execution
static uintptr_t* ConsoleArgFinder = nullptr; // memory pattern to find the real function
static SInt32 ConsoleArgRel32 = 0; // rel32 set later on
static const char* s_CommandToBorrow = "GameComment"; // the command we will replace (full name)
static ObScriptCommand* s_hijackedCommand = nullptr;
static ObScriptParam* s_hijackedCommandParams = nullptr;

// First Console Command
static uintptr_t* FirstConsoleFinder = nullptr; // pattern where the first console command is referenced
static ObScriptCommand* FirstConsole = nullptr; // obscriptcommand first console command
static SInt32 FirstConsoleRel32 = 0; // rel32 set later on

// First ObScript Command
static uintptr_t* FirstObScriptFinder = nullptr; // pattern where the first Obscript command is referenced
static ObScriptCommand* FirstObScript = nullptr; // obscriptcommand first console command
static SInt32 FirstObScriptRel32 = 0; // rel32 set later on

//gconsole console print
static uintptr_t* GConsoleFinder = nullptr; //pattern to find g_console
static SInt32 GConsoleRel32 = 0; // rel32 set later on
static uintptr_t GConsoleStatic; // g_console

// Currently grabbed or highlighted workshop reference
// multi level pointer. always contains the highlighted or grabbed ref
static uintptr_t* CurrentRefFinder = nullptr; // pattern to help us find it
static uintptr_t CurrentRefBase; // base address
static SInt32 CurrentRefBaseRel32 = 0; // rel32 of base address

// offsets from CurrentRefFinder
static uintptr_t BSFadeNode_Offsets[] = { 0x0, 0x0, 0x10 }; // bsfadenode offsets
static size_t BSFadeNodeOffsetCount = sizeof(BSFadeNode_Offsets) / sizeof(BSFadeNode_Offsets[0]);

//TESObjectREFR
static uintptr_t RefOffsets[] = { 0x0, 0x0, 0x10, 0x110 };
static size_t RefOffsetCount = sizeof(RefOffsets) / sizeof(RefOffsets[0]);

//bhkNiCollisionObject
static uintptr_t bhkNiCollOffsets[] = { 0x0, 0x0, 0x10, 0x100 };
static size_t bhkNiCollOffsetCount = sizeof(bhkNiCollOffsets) / sizeof(bhkNiCollOffsets[0]);

// workshop mode finder
static uintptr_t* WorkshopModeFinder = nullptr;
static SInt32 WorkshopModeFinderRel32 = 0;
static uintptr_t WorkshopModeBoolAddress;

// Pointers to memory patterns
static uintptr_t* CHANGE_A = nullptr;
static uintptr_t* CHANGE_B = nullptr;
static uintptr_t* CHANGE_C = nullptr;
static uintptr_t* CHANGE_D = nullptr;
static uintptr_t* CHANGE_E = nullptr;
static uintptr_t* CHANGE_F = nullptr;
static uintptr_t* CHANGE_G = nullptr;
static uintptr_t* CHANGE_H = nullptr;
static uintptr_t* CHANGE_I = nullptr;
static uintptr_t* RED = nullptr;
static uintptr_t* YELLOW = nullptr;
static uintptr_t* WSTIMER = nullptr;
static uintptr_t* WSTIMER2 = nullptr;
static uintptr_t* GROUNDSNAP = nullptr;
static uintptr_t* OBJECTSNAP = nullptr;
static uintptr_t* WORKSHOPSIZE = nullptr;
static uintptr_t* OUTLINES = nullptr;
static uintptr_t* ACHIEVEMENTS = nullptr;
static uintptr_t* ZOOM = nullptr;
static uintptr_t* ROTATE = nullptr;

// For proper toggling
static UInt8 CHANGE_C_OLDCODE[7];
static UInt8 CHANGE_C_NEWCODE[7] = { 0x31, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90 }; //xor al,al;nop x5
static UInt8 CHANGE_D_OLDCODE[7];
static UInt8 CHANGE_D_NEWCODE[7] = { 0x31, 0xC0, 0xB0, 0x01, 0x90, 0x90, 0x90 }; //xor al,al;mov al,01;nop x3
static UInt8 CHANGE_F_OLDCODE[2] = { 0x88, 0x05 };
static UInt8 CHANGE_F_NEWCODE[2] = { 0xEB, 0x04 };
static UInt8 CHANGE_I_OLDCODE[2] = { 0x74, 0x35 };
static UInt8 CHANGE_I_NEWCODE[2] = { 0xEB, 0x30 };
static UInt8 YELLOW_NEWCODE[3] = { 0x90, 0x90, 0x90 }; //nop x3
static UInt8 YELLOW_OLDCODE[3] = { 0x8B, 0x58, 0x14 };
static UInt8 WSTIMER2_OLDCODE[8];
static UInt8 WSTIMER2_NEWCODE[8] = { 0x0F, 0x57, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90 }; //xorps xmm0,xmm0; nop x5


// Allows achievements with mods and prevents game adding [MODS] in save file name
static UInt8 ACHIEVEMENTS_NEWCODE[3] = { 0x30, 0xC0, 0xC3 }; // xor al, al; ret
static UInt8 ACHIEVEMENTS_OLDCODE[4] = { 0x48, 0x83, 0xEC, 0x28 }; // sub rsp,28

// Object snap
static UInt8 OBJECTSNAP_OLDCODE[8];
static UInt64 OBJECTSNAP_NEWCODE = 0x9090909090F6570F; // xorps xmm6, xmm6; nop x5

// Workshop size
static SInt32 WORKSHOPSIZE_REL32 = 0;
static UInt8 WORKSHOPSIZE_DRAWS_OLDCODE[6];
static UInt8 WORKSHOPSIZE_DRAWS_NEWCODE[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
static UInt8 WORKSHOPSIZE_TRIANGLES_OLDCODE[6];
static UInt8 WORKSHOPSIZE_TRIANGLES_NEWCODE[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

// zoom and rotate
static SInt32 ZOOM_REL32 = 0;
static SInt32 ROTATE_REL32 = 0;
static UInt8 fZOOM_DEFAULT[4] = { 0x00, 0x00, 0x20, 0x41 }; // 10.0f
static UInt8 fZOOM_SLOWED[4] = { 0x00, 0x00, 0x80, 0x3F }; // 1.0f
static UInt8 fROTATE_DEFAULT[4] = { 0x00, 0x00, 0xA0, 0x40 }; // 5.0f
static UInt8 fROTATE_SLOWED[4] = { 0x00, 0x00, 0x00, 0x3F }; // 0.5f

// On and off switches for toggling. These are the baked in defaults
static bool PLACEINRED_ENABLED = false; //false, toggled on during F4SEPlugin_Load
static bool ACHIEVEMENTS_ENABLED = false; //false, toggled on during F4SEPlugin_Load
static bool OBJECTSNAP_ENABLED = true; //true, game default
static bool GROUNDSNAP_ENABLED = true; // true, game default
static bool SLOW_ENABLED = false; // false, game default
static bool WORKSHOPSIZE_ENABLED = false; // false, game default
static bool OUTLINES_ENABLED = true; // true, game default