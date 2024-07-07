#pragma once
#include "shlobj.h"
#include "pattern.h"
#include "f4se.h"
#include <string.h>

// macros
#define PIR_LOG_PREP const char* thisfunc = __func__;

// f4se plugin
static UInt32 pluginVersion = 9;
static const char* pluginLogFile = { "\\My Games\\Fallout4\\F4SE\\PlaceInRed.log" };
static std::string pluginINI = "Data\\F4SE\\Plugins\\PlaceInRed.ini";
static IDebugLog pirlog;
static PluginHandle pirPluginHandle = kPluginHandle_Invalid;
static F4SEPapyrusInterface* g_papyrus = nullptr;
static F4SEMessagingInterface* g_messaging = nullptr;
static F4SEObjectInterface* g_object = nullptr;
static F4SETaskInterface* g_task = nullptr;

// typedefs
typedef bool  (*_GetConsoleArg_Native) (void* paramInfo, void* scriptData, void* opcodeOffsetPtr, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, ...);
typedef void  (*_SetMotionType_Native) (VirtualMachine* vm, uint32_t stackID, TESObjectREFR* objectReference, int motionType, bool allowActivate);
typedef void  (*_PlayUISound_Native)   (const char*);
typedef void  (*_PlayFileSound_Native) (const char*);
typedef float (*_GetScale_Native)      (TESObjectREFR* objRef);
typedef void  (*_SetScale_Native)      (TESObjectREFR* objRef, float scale);

struct _PlaySounds
{
	uintptr_t* UIpattern = nullptr;
	SInt32                UI_r32 = 0;
	_PlayUISound_Native   UI_func = nullptr;

	uintptr_t* Filepattern = nullptr;
	SInt32                File_r32 = 0;
	_PlayFileSound_Native File_func = nullptr;
};

// struct for tracking the plugin settings
struct _SETTINGS
{
	bool    GameDataIsReady = false; // set to true when F4SE tells us its ready
	bool    PLACEINRED_ENABLED = false; //pir 1
	bool    OBJECTSNAP_ENABLED = true; //pir 2
	bool    GROUNDSNAP_ENABLED = true; //pir 3
	bool    SLOW_ENABLED = false; //pir 4
	bool    WORKSHOPSIZE_ENABLED = false; //pir 5
	bool    OUTLINES_ENABLED = true; //pir 6
	bool    ACHIEVEMENTS_ENABLED = false; //pir 7
	bool    ConsoleNameRef_ENABLED = false; //pir cnref
	bool    PrintConsoleMessages = true; //ini
	Float32 fOriginalZOOM = 10.0F;  //updated later to fItemHoldDistantSpeed:Workshop
	Float32 fOriginalROTATE = 5.0F; //updated later to fItemRotationSpeed:Workshop
	Float32 fSlowerZOOM = 1.0F;     //updated later to plugin ini value
	Float32 fSlowerROTATE = 0.5F;   //updated later to plugin ini value
};

// struct for pointers to memory patterns
struct _POINTERS
{
	uintptr_t* A = nullptr;
	uintptr_t* B = nullptr;
	uintptr_t* C = nullptr;
	uintptr_t* D = nullptr;
	uintptr_t* E = nullptr;
	uintptr_t* F = nullptr;
	uintptr_t* G = nullptr;
	uintptr_t* H = nullptr;
	uintptr_t* J = nullptr;
	uintptr_t* red = nullptr;
	uintptr_t* redcall = nullptr;
	uintptr_t* yellow = nullptr;
	uintptr_t* wstimer = nullptr;
	uintptr_t* gsnap = nullptr;
	uintptr_t* osnap = nullptr;
	uintptr_t* outlines = nullptr;
	uintptr_t* achievements = nullptr;
	uintptr_t* playui = nullptr;
	uintptr_t* playany = nullptr;
};

// struct for various bytes used for patching memory
struct _PATCHES
{
	UInt8  NOP3[3] = { 0x0F, 0x1F, 0x00 }; // 3 byte nop
	UInt8  NOP4[4] = { 0x0F, 0x1F, 0x40, 0x00 }; // 4 byte nop
	UInt8  NOP5[5] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // 5 byte nop
	UInt8  NOP6[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // 6 byte nop
	UInt8  NOP7[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 }; // 7 byte nop
	UInt8  NOP8[8] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 }; // 8 byte nop
	UInt8  C_OLD[7];
	UInt8  C_NEW[7] = { 0x31, 0xC0, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; //xor al,al;nop x5
	UInt8  D_OLD[7];
	UInt8  D_NEW[7] = { 0x31, 0xC0, 0xB0, 0x01, 0x90, 0x90, 0x90 }; //xor al,al;mov al,01;nop x3
	UInt8  F_OLD[6];
	UInt8  J_OLD[2] = { 0x74, 0x35 };
	UInt8  J_NEW[2] = { 0xEB, 0x30 };
	UInt8  K_OLD[2] = { 0x75, 0x11 }; // jne
	UInt8  K_NEW[2] = { 0xEB, 0x1C }; // jmp 
	UInt8  redcall_OLD[5];
	UInt8  Y_OLD[3] = { 0x8B, 0x58, 0x14 }; // mov rbx,[rax+0x14]
	UInt8  TIMER_OLD[6] = { 0x0F, 0x85, 0xAB, 0x00, 0x00, 0x00 }; //original is jne
	UInt8  TIMER_NEW[6] = { 0xE9, 0xAC, 0x00, 0x00, 0x00, 0x90 }; //jmp instead
	UInt8  achievements_new[3] = { 0x30, 0xC0, 0xC3 }; // xor al, al; ret
	UInt8  achievements_old[4] = { 0x48, 0x83, 0xEC, 0x28 }; // sub rsp,28
	UInt8  OSNAP_OLD[8];
	UInt8  OSNAP_NEW[8] = { 0x0F, 0x57, 0xF6, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // xorps xmm6, xmm6; NOP5;
	UInt8  DRAWS_OLD[6];
	UInt8  TRIS_OLD[6];
	UInt8  CNameRef_OLD[6] = { 0xFF, 0x90, 0xD0, 0x01, 0x00, 0x00 }; //call qword finder [rax+000001D0]
	size_t CNameRef_OLD_Size = sizeof(CNameRef_OLD) / sizeof(CNameRef_OLD[0]);
	UInt32 CurrentWSRef_Offsets[4] = { 0x0, 0x0, 0x10, 0x110 };
	size_t CurrentWSRef_OffsetsSize = sizeof(CurrentWSRef_Offsets)/sizeof(CurrentWSRef_Offsets[0]);
};

// Struct to store things for simple
struct SimpleFinder
{
	uintptr_t* func = nullptr; // pointer to a pattern match
	SInt32 r32 = 0; // rel32 of what were finding
	uintptr_t addr = 0; // final address
	ObScriptCommand* cmd; // for first console commands
};
// Scale functions
struct _ScaleFuncs
{
	_GetScale_Native  GetScale = nullptr;
	uintptr_t*        getpattern = nullptr;
	SInt32            g32 = 0;
	_SetScale_Native  SetScale = nullptr;
	uintptr_t*        setpattern = nullptr;
	SInt32            s32 = 0;
};

// structure to group consolenameref stuff
struct _CNameRef
{
	uintptr_t* call = nullptr; // the call when references are clicked
	uintptr_t* goodfinder = nullptr; // the good function we will point to instead
	uintptr_t  goodfunc = 0; // the good function full address
	SInt32     goodfinder_r32 = 0; // rel32 of the good function
};

