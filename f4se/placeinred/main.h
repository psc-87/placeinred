#pragma once
#include "shlobj.h"
#include "pattern.h"
#include "f4se.h"
#include "papyrus.h"

#define SET_CURRENT_FUNCTION_STRING const char* thisfunc = __func__;

//typedefs
typedef void  (*_SetScale)              (TESObjectREFR* objRef, float scale);
typedef float (*_GetScale)              (TESObjectREFR* objRef);
typedef bool  (*_GetConsoleArg)         (void* paramInfo, void* scriptData, void* opcodeOffsetPtr, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, ...);
typedef bool  (*_SetMotionType_Native)  (VirtualMachine* vm, UInt32 stackId, TESObjectREFR* ref, SInt32 motiontype, bool akAllowActivate);
typedef void  (*_ExecuteCommand)        (const char* str);

// Plugin specific
extern IDebugLog pirlog;
static PluginHandle pluginHandle = kPluginHandle_Invalid;
static UInt32 pluginVersion = 8;
static const char pluginName[] = { "Place In Red" };
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
	// rel32start=bytes to reach start of rel32 from pattern
	// rel32end=bytes to reach end of rel32 from pattern
	if (pattern) {
		SInt32 relish32 = 0;
		if (!ReadMemory(uintptr_t(pattern) + rel32start, &relish32, sizeof(SInt32))) {
			return false;
		}
		relish32 = (((uintptr_t(pattern) + rel32end) + relish32) - RelocationManager::s_baseAddr) + (specialmodify);
		if (relish32 > 0) {
			return relish32;
		}
		else {
			return 0;
		}
	}
	return 0;
}

// get pointer from address and offset
static uintptr_t* ReadPointer(uintptr_t address, uintptr_t offset) {
	uintptr_t* result = nullptr;
	if (ReadMemory(address + offset, &result, sizeof(uintptr_t))) {
		return result;
	}
	else {
		return nullptr;
	}
}

// get multi level pointer
static uintptr_t* GetMultiLevelPointer(uintptr_t baseAddress, uintptr_t* offsets, size_t numOffsets) {
	uintptr_t address = baseAddress;
	for (size_t i = 0; i < numOffsets; ++i) {
		address = uintptr_t(ReadPointer(address, offsets[i]));
		if (!address)
			break;  // Break if encountered a nullptr
	}
	return reinterpret_cast<uintptr_t*>(address);
}

// To switch with strings
static constexpr unsigned int PIR_Switch(const char* s, int off = 0) {
	return !s[off] ? 5381 : (PIR_Switch(s, off + 1) * 33) ^ s[off];
}
