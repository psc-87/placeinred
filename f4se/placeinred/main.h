#pragma once
#include "shlobj.h"
#include "pattern.h"
#include "f4se.h"
#include <string.h>

// set thisfunc to __func__
#define PIR_LOG_PREP const char* thisfunc = __func__;

// misc strings and vars
static UInt32 pluginVersion = 9;
static const char* pluginLogFile = { "\\My Games\\Fallout4\\F4SE\\PlaceInRed.log" };
static const char* pirunknowncommandmsg =
{
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
extern IDebugLog pirlog;
static PluginHandle pirPluginHandle = kPluginHandle_Invalid;
static F4SEPapyrusInterface* g_papyrus = nullptr;
static F4SEMessagingInterface* g_messaging = nullptr;
static F4SEObjectInterface* g_object = nullptr;
static F4SETaskInterface* g_task = nullptr;

//typedefs
typedef float (*_GetScale)             (TESObjectREFR* objRef);
typedef void  (*_SetScale)             (TESObjectREFR* objRef, float scale);
typedef bool  (*_GetConsoleArg)        (void* paramInfo, void* scriptData, void* opcodeOffsetPtr, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, ...);
typedef void  (*_SetMotionType_Native) (VirtualMachine* vm, uint32_t stackID, TESObjectREFR* objectReference, int motionType, bool allowActivate);

//current ws ref offset


struct PluginSettings
{
	bool PLACEINRED_ENABLED = false;
	bool OBJECTSNAP_ENABLED = true;
	bool GROUNDSNAP_ENABLED = true;
	bool SLOW_ENABLED = false;
	bool WORKSHOPSIZE_ENABLED = false;
	bool OUTLINES_ENABLED = true;
	bool ACHIEVEMENTS_ENABLED = false;
	bool ConsoleNameRef_ENABLED = false;
	bool PrintConsoleMessages = true;
	Float32 fSlowerZOOM = 1.0F; //plugin default. updated later to plugin ini value
	Float32 fSlowerROTATE = 0.5F; //plugin default. updated later to plugin ini value
	Float32 fOriginalZOOM = 10.0F; //game default. updated later to fItemHoldDistantSpeed:Workshop
	Float32 fOriginalROTATE = 5.0F; //game default. updated later to fItemRotationSpeed:Workshop
	bool GameDataIsReady = false; //set to true when F4SE tells us its ready
};

struct _Patches
{
	UInt8 NOP3[3] = { 0x0F, 0x1F, 0x00 }; // 3 byte nop
	UInt8 NOP4[4] = { 0x0F, 0x1F, 0x40, 0x00 }; // 4 byte nop
	UInt8 NOP5[5] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // 5 byte nop
	UInt8 NOP6[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // 6 byte nop
	UInt8 NOP7[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 }; // 7 byte nop
	UInt8 NOP8[8] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 }; // 8 byte nop

	UInt8 C_OLD[7];
	UInt8 C_NEW[7] = { 0x31, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90 }; //xor al,al;nop x5

	UInt8 D_OLD[7];
	UInt8 D_NEW[7] = { 0x31, 0xC0, 0xB0, 0x01, 0x90, 0x90, 0x90 }; //xor al,al;mov al,01;nop x3

	UInt8 F_OLD[6];

	UInt8 I_OLD[2] = { 0x74, 0x35 };
	UInt8 I_NEW[2] = { 0xEB, 0x30 };

	UInt8 YELLOW_OLD[3] = { 0x8B, 0x58, 0x14 };
	UInt8 REDCALL_OLD[5];

	UInt8 WSTIMER_OLD[6] = { 0x0F, 0x85, 0xAB, 0x00, 0x00, 0x00 }; //original is jne
	UInt8 WSTIMER_NEW[6] = { 0xE9, 0xAC, 0x00, 0x00, 0x00, 0x90 }; //jmp instead

	UInt8 Achieve_NEW[3] = { 0x30, 0xC0, 0xC3 }; // xor al, al; ret
	UInt8 Achieve_OLD[4] = { 0x48, 0x83, 0xEC, 0x28 }; // sub rsp,28

	UInt8 OSNAP_OLD[8];
	UInt64 OSNAP_NEW = 0x9090909090F6570F; // xorps xmm6, xmm6; nop x5

	UInt64 currentwsref_offsets[4] = { 0x0, 0x0, 0x10, 0x110 };
	size_t currentwsref_offsets_size = sizeof(currentwsref_offsets) / sizeof(currentwsref_offsets[0]);

	UInt8 WSDRAWS_OLD[6];
	UInt8 WSTRIS_OLD[6];
};


struct SimpleFinder
{
	uintptr_t* finder = nullptr; // pointer to a pattern match
	SInt32 r32 = 0; // rel32 of what were finding
	uintptr_t addr = 0; // final address
};

struct _SetMotionType
{
	uintptr_t* finder = nullptr; //pattern to find setmotiontype
	SInt32 r32 = 0; //setmotiontype rel32
	_SetMotionType_Native func = nullptr; //native procedure
};

struct ConsoleStuff
{
	// processes executed console commands
	uintptr_t* ConsoleArgFinder = nullptr;
	_GetConsoleArg GetConsoleArg = nullptr;
	SInt32 ConsoleArgRel32 = 0;

	// first commands
	uintptr_t* consolefinder = nullptr; // pattern where first console is referenced
	uintptr_t* obscriptfinder = nullptr; // pattern where first obscript is referenced
	ObScriptCommand* consolecmd = nullptr; // first console command
	ObScriptCommand* obscriptcmd = nullptr; // first obscript command
	SInt32 consolecmd_r32 = 0; // rel32 set later on
	SInt32 obscriptcmd_r32 = 0; // rel32 set later on
	
};

struct ScaleFunctions
{
	uintptr_t* setFinder = nullptr;
	uintptr_t* getFinder = nullptr;
	SInt32 setR32 = 0;
	SInt32 getR32 = 0;
	_GetScale GetScale = nullptr;
	_SetScale SetScale = nullptr;
};

struct ConsoleNameRefStuff
{
	uintptr_t* callfinder = nullptr; // find the function called when references are clicked
	uintptr_t* funcfinder = nullptr; // secret function that gives us a ref name
	SInt32 funcfinderR32 = 0; // rel32
	uintptr_t addr; //the address for the function that really gives us the ref name
	UInt8 oldbytes[6] = { 0xFF, 0x90, 0xD0, 0x01, 0x00, 0x00 }; //call qword ptr [rax+000001D0]
};



static PluginSettings PIRSettings;
static SimpleFinder WSMode;
static SimpleFinder WSSize;
static SimpleFinder gConsole;
static SimpleFinder gDataHandler;
static SimpleFinder CurrentWSRef;
static SimpleFinder Zoom;
static SimpleFinder Rotate;
static _SetMotionType SetMotionType;
static ConsoleStuff PIRConsole;
static ScaleFunctions ScaleFuncs;
static ConsoleNameRefStuff ConsoleNameRef;
static _Patches Patches;

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
static uintptr_t* REDCALL = nullptr;
static uintptr_t* YELLOW = nullptr;
static uintptr_t* WSTIMER = nullptr;
static uintptr_t* GROUNDSNAP = nullptr;
static uintptr_t* OBJECTSNAP = nullptr;
static uintptr_t* OUTLINES = nullptr;
static uintptr_t* AchievementsFinder = nullptr;


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

// string to float for ini conversion
static Float32 FloatFromString(std::string fString, Float32 min = 0.001, Float32 max = 999.999)
{
	Float32 theFloat = 0;
	try
	{
		theFloat = std::stof(fString);
	}
	catch (...)
	{
		return 0;
	}

	if (theFloat > min && theFloat < max) {
		return theFloat;
	} else {
		return 0;
	}
}