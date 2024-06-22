#pragma once
#include "shlobj.h"
#include "pattern.h"
#include "f4se.h"
#include <string.h>

//cool purple macro to set this
#define PIR_LOG_PREP const char* thisfunc = __func__;

// misc strings and vars
static UInt32 pluginVersion = 8;
static const char pluginName[] = { "PlaceInRed" };
static const char pluginLogFile[] = { "\\My Games\\Fallout4\\F4SE\\PlaceInRed.log" };
static const char* pirunknowncommandmsg = { "PlaceInRed (pir) usage:\n pir toggle (pir 1) toggle place in red\n pir osnap (pir 2) toggle object snapping\n pir gsnap (pir 3) toggle ground snapping\n pir slow (pir 4) toggle slower object rotation and zoom speed\n pir workshopsize (pir 5) toggle unlimited workshop build size\n pir outlines (pir 6) toggle object outlines\n pir achievements (pir 7) toggle achievement with mods\n pir scaleup1 (also: 1, 5, 10, 25, 50, 100) scale up percent\n pir scaledown1 (also: 1, 5, 10, 25, 50, 75) scale down percent\n pir lock (pir l) lock object in place (motiontype keyframed)\n pir unlock (pir u) unlock object (motiontype dynamic)" };

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

// ini stuff
// default settings before ini is read
static bool PLACEINRED_ENABLED = false;
static bool ACHIEVEMENTS_ENABLED = false;
static bool ConsoleNameRef_ENABLED = false;
static bool OBJECTSNAP_ENABLED = true;
static bool GROUNDSNAP_ENABLED = true;
static bool SLOW_ENABLED = false;
static bool WORKSHOPSIZE_ENABLED = false;
static bool OUTLINES_ENABLED = true;

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
		char	resultBuf[256];
		resultBuf[0] = 0;
		UInt32	resultLen = GetPrivateProfileString(section, key, NULL, resultBuf, sizeof(resultBuf), configPath.c_str());
		result = resultBuf;
	}
	return result;
}
