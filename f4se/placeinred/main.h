#pragma once
#include "shlobj.h"
#include "pattern.h"
#include "f4se.h"
#include <string.h>

// macros
#define PIR_LOG_PREP const char* thisfunc = __func__;

// misc strings and vars
static UInt32 pluginVersion = 9;
static const char* pluginLogFile = { "\\My Games\\Fallout4\\F4SE\\PlaceInRed.log" };
static const char* pirunknowncommandmsg = {
  "PlaceInRed (pir) usage:\n"
  "pir toggle       (pir 1) toggle place in red\n"
  "pir osnap        (pir 2) toggle object snapping\n"
  "pir gsnap        (pir 3) toggle ground snapping\n"
  "pir slow         (pir 4) toggle slower object rotation and zoom speed\n"
  "pir workshopsize (pir 5) toggle unlimited workshop build size\n"
  "pir outlines     (pir 6) toggle object outlines\n"
  "pir achievements (pir 7) toggle achievement with mods\n"
  "pir scaleup1     (and 2, 5, 10, 25, 50, 100) scale up percent\n"
  "pir scaledown1   (and 2, 5, 10, 25, 50, 75) scale down percent\n"
  "pir lock         (pir l) lock object in place (motiontype keyframed)\n"
  "pir unlock       (pir u) unlock object (motiontype dynamic)"
};

// f4se plugin
static IDebugLog pirlog;
static PluginHandle pirPluginHandle = kPluginHandle_Invalid;
static F4SEPapyrusInterface* g_papyrus = nullptr;
static F4SEMessagingInterface* g_messaging = nullptr;
static F4SEObjectInterface* g_object = nullptr;
static F4SETaskInterface* g_task = nullptr;

//typedefs
typedef float (*_GetScale_Native)             (TESObjectREFR* objRef);
typedef void  (*_SetScale_Native)             (TESObjectREFR* objRef, float scale);
typedef bool  (*_GetConsoleArg_Native) (void* paramInfo, void* scriptData, void* opcodeOffsetPtr, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, ...);
typedef void  (*_SetMotionType_Native) (VirtualMachine* vm, uint32_t stackID, TESObjectREFR* objectReference, int motionType, bool allowActivate);
typedef void  (*_PlayUISound_Native)   (const char*);
typedef void  (*_PlayFileSound_Native) (const char*);

typedef void  (*_Disable_Native) (VirtualMachine* vm, uint32_t stackID, TESObjectREFR* ref);

static _SetMotionType_Native SetMotionType_Native = nullptr;
static _GetConsoleArg_Native GetConsoleArg_Native = nullptr;

static _Disable_Native Disable_Native = nullptr;

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
	uintptr_t* redCALL = nullptr;
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
	UInt8 NOP3[3] = { 0x0F, 0x1F, 0x00 }; // 3 byte nop
	UInt8 NOP4[4] = { 0x0F, 0x1F, 0x40, 0x00 }; // 4 byte nop
	UInt8 NOP5[5] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // 5 byte nop
	UInt8 NOP6[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // 6 byte nop
	UInt8 NOP7[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 }; // 7 byte nop
	UInt8 NOP8[8] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 }; // 8 byte nop
	UInt8 C_OLD[7];
	UInt8 C_NEW[7] = { 0x31, 0xC0, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; //xor al,al;nop x5
	UInt8 D_OLD[7];
	UInt8 D_NEW[7] = { 0x31, 0xC0, 0xB0, 0x01, 0x90, 0x90, 0x90 }; //xor al,al;mov al,01;nop x3
	UInt8 F_OLD[6];
	UInt8 I_OLD[2] = { 0x74, 0x35 };
	UInt8 I_NEW[2] = { 0xEB, 0x30 };
	UInt8 yellow_old[3] = { 0x8B, 0x58, 0x14 };
	UInt8 redcall_old[5];
	UInt8 wstimer_old[6] = { 0x0F, 0x85, 0xAB, 0x00, 0x00, 0x00 }; //original is jne
	UInt8 wstimer_new[6] = { 0xE9, 0xAC, 0x00, 0x00, 0x00, 0x90 }; //jmp instead
	UInt8 achievements_new[3] = { 0x30, 0xC0, 0xC3 }; // xor al, al; ret
	UInt8 achievements_old[4] = { 0x48, 0x83, 0xEC, 0x28 }; // sub rsp,28
	UInt8 osnap_old[8];
	UInt8 osnap_new[8] = { 0x0F, 0x57, 0xF6, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // xorps xmm6, xmm6; NOP5;
	UInt8 wsdraws_old[6];
	UInt8 wstris_old[6];
	UInt32 currentwsref_offsets[4] = { 0x0, 0x0, 0x10, 0x110 };
	size_t currentwsref_offsets_size = sizeof(currentwsref_offsets) / sizeof(currentwsref_offsets[0]);
	UInt8 cnameref_old[6] = { 0xFF, 0x90, 0xD0, 0x01, 0x00, 0x00 }; //call qword finder [rax+000001D0]
	size_t cnameref_old_size = sizeof(cnameref_old) / sizeof(cnameref_old[0]);
};

// Struct to store things for simple
struct SimpleFinder
{
	uintptr_t* func = nullptr; // pointer to a pattern match
	SInt32 r32 = 0; // rel32 of what were finding
	uintptr_t addr = 0; // final address
	ObScriptCommand* cmd; // for first console commands
};

struct _ScaleFuncs
{
	_GetScale_Native  GetScale = nullptr;
	uintptr_t*        getpattern = nullptr;
	SInt32            getscale_r32 = 0;

	_SetScale_Native  SetScale = nullptr;
	uintptr_t*        setpattern = nullptr;
	SInt32            setscale_r32 = 0;
};

// structure to group consolenameref stuff
struct _CNameRef
{
	uintptr_t* call = nullptr; // the call when references are clicked
	uintptr_t* goodfinder = nullptr; // the good function we will point to instead
	uintptr_t  goodfunc = 0; // the good function full address
	SInt32     goodfinder_r32 = 0; // rel32 of the good function
};

// functions for playing sounds
struct _PlaySounds
{
	uintptr_t*            UIpattern = nullptr;
	SInt32                UI_r32 = 0;
	_PlayUISound_Native   UI_func = nullptr;
	
	uintptr_t*            Filepattern = nullptr;
	SInt32                File_r32 = 0;
	_PlayFileSound_Native File_func = nullptr;
};

// create instances of the structures
static _SETTINGS    Settings;
static _PATCHES     Patches;
static _POINTERS    Pointers;
static _PlaySounds  PlaySounds;
static _ScaleFuncs  SCALE;
static _CNameRef    CNameRef;
static SimpleFinder FirstConsole;
static SimpleFinder FirstObScript;
static SimpleFinder WSMode;
static SimpleFinder WSSize;
static SimpleFinder gConsole;
static SimpleFinder gDataHandler;
static SimpleFinder CurrentWSRef;
static SimpleFinder Zoom;
static SimpleFinder Rotate;
static SimpleFinder SetMotionType;
static SimpleFinder GetConsoleArg;


// return the ini path as a std string
const std::string& GetPIRConfigPath()
{
	static std::string s_configPath;

	if (s_configPath.empty())
	{
		std::string	runtimePath = GetRuntimeDirectory();
		if (!runtimePath.empty())
		{
			s_configPath = runtimePath + "Data\\F4SE\\Plugins\\PlaceInRed.ini";
		}
	}
	return s_configPath;
}

// return an ini setting as a std string
std::string GetPIRConfigOption(const char* section, const char* key)
{
	std::string	result;
	const std::string& configPath = GetPIRConfigPath();
	if (!configPath.empty())
	{
		char resultBuf[2048];
		resultBuf[0] = 0;
		UInt32 resultLen = GetPrivateProfileString(section, key, NULL, resultBuf, sizeof(resultBuf), configPath.c_str());
		result = resultBuf;
	}
	return result;
}
