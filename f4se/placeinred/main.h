#pragma once
#include "shlobj.h"
#include "pattern.h"
#include "f4se.h"
#include "papyrus.h"

//cool purple macro to set this
#define PIR_LOG_PREP const char* thisfunc = __func__;

//typedefs
typedef void  (*_SetScale)              (TESObjectREFR* objRef, float scale);
typedef void  (*_ModAngleXYZ)           (TESObjectREFR* objRef, float scale);
typedef float (*_GetScale)              (TESObjectREFR* objRef);
typedef bool  (*_GetConsoleArg)         (void* paramInfo, void* scriptData, void* opcodeOffsetPtr, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, ...);
typedef bool  (*_SetMotionType_Native)  (VirtualMachine* vm, UInt32 stackId, TESObjectREFR* ref, SInt32 motiontype, bool akAllowActivate);

// Plugin specific
extern IDebugLog pirlog;
static PluginHandle pluginHandle = kPluginHandle_Invalid;
static UInt32 pluginVersion = 8;
static const char pluginName[] = { "PlaceInRed" };
static const char pluginLogFile[] = { "\\My Games\\Fallout4\\F4SE\\PlaceInRed.log" };
static const char* pirunknowncommandmsg = { "PlaceInRed (pir) usage:\n pir toggle (pir 1) toggle place in red\n pir osnap (pir 2) toggle object snapping\n pir gsnap (pir 3) toggle ground snapping\n pir slow (pir 4) slow object rotation and zoom speed\n pir workshopsize (pir 5) unlimited workshop build size\n pir outlines (pir 6) toggle object outlines\n pir achievements (pir 7) toggle achievement feature\n pir scaleup1   (also: 1, 5, 10, 25, 50, 100) scale up percent\n pir scaledown1   (also: 1, 5, 10, 25, 50, 75) scale down percent\n" };

// F4SE interfaces
static F4SEPapyrusInterface* g_papyrus = nullptr;
static F4SEMessagingInterface* g_messaging = nullptr;
static F4SEObjectInterface* g_object = nullptr;
static F4SETaskInterface* g_task = nullptr;


// Simple function to read memory (credit reg2k).
static bool ReadMemory(uintptr_t addr, void* data, size_t len) {
	UInt32 oldProtect;
	if (VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		memcpy(data, (void*)addr, len);
		if (VirtualProtect((void*)addr, len, oldProtect, &oldProtect)) {
			return true;
		}
	}
	return false;
}

// return rel32 from a pattern match
static SInt32 GetRel32FromPattern(uintptr_t* pattern, UInt64 rel32start, UInt64 rel32end, UInt64 specialmodify = 0x0)
{
	// pattern: pattern match pointer
	// rel32start:to reach start of rel32 from pattern
	// rel32end: to reach end of rel32 from pattern
	// specifymodify: bytes to shift the result by, default 0 no change
	if (pattern) {
		SInt32 relish32 = 0;
		if (!ReadMemory(uintptr_t(pattern) + rel32start, &relish32, sizeof(SInt32))) {
			return 0;
		} else {
			relish32 = (((uintptr_t(pattern) + rel32end) + relish32) - RelocationManager::s_baseAddr) + (specialmodify);
			return relish32;
		}
	}
	return 0;
}

//read the pointer at an address+offset
static uintptr_t GimmeSinglePointer(uintptr_t address, UInt64 offset) {
	uintptr_t result = 0;
	if (ReadMemory(address + offset, &result, sizeof(uintptr_t))) {
		return result;
	}
	else {
		return 0;
	}
}

// get a multi level pointer from a base address
static uintptr_t* GimmeMultiPointer(uintptr_t baseAddress, UInt64* offsets, UInt64 numOffsets) {
	if (!baseAddress || baseAddress == 0) { 
		return nullptr;
	}
	uintptr_t address = baseAddress;

	for (UInt64 i = 0; i < numOffsets; i++) {
		address = GimmeSinglePointer(address, offsets[i]);
		if (!address || address == 0) {
			return nullptr;
		}
	}
	return reinterpret_cast<uintptr_t*>(address);
}