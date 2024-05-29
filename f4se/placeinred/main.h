#pragma once
#include "shlobj.h"
#include "pattern.h"
#include "f4se.h"
#include "papyrus.h"

// Plugin specific
extern IDebugLog pluginLog;
static PluginHandle pluginHandle = kPluginHandle_Invalid;
static UInt32 pluginVersion = 8;
static const char pluginName[] = { "Place In Red" };
static const char pluginLogFile[] = { "\\My Games\\Fallout4\\F4SE\\PlaceInRed.log" };

// F4SE interfaces
static F4SEPapyrusInterface* g_papyrus = nullptr;
static F4SEMessagingInterface* g_messaging = nullptr;
static F4SEObjectInterface* g_object = nullptr;
static F4SETaskInterface* g_task = nullptr;

//typedefs
typedef void  (*_SetScale)      (TESObjectREFR* objRef, float scale);
typedef float (*_GetScale)      (TESObjectREFR* objRef);
typedef bool  (*_GetConsoleArg) (void* paramInfo, void* scriptData, void* opcodeOffsetPtr, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, ...);




// Credit to reg2k. Simple function to read memory. 
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
static uintptr_t* ReadPointer(uintptr_t address, uintptr_t offset) {
	uintptr_t* result = nullptr;
	if (ReadMemory(address + offset, &result, sizeof(uintptr_t))) {
		return result;
	}
	else {
		return nullptr;
	}
}
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
