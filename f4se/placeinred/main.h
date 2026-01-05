#pragma once
#pragma warning(disable: 4200)

#include "shlobj.h"
#include "pattern.h"
#include <cmath>
#include <vector>
#include <future>
#include <array>

#include "f4se_common/BranchTrampoline.h"
#include "f4se_common/f4se_version.h"
#include "f4se_common/Relocation.h"
#include "f4se_common/SafeWrite.h"
#include "f4se_common/Utilities.h"

#include "f4se/GameExtraData.h"
#include "f4se/GameReferences.h"
#include "f4se/ObScript.h"
#include "f4se/PapyrusVM.h"
#include "f4se/PluginAPI.h"


// log wrapper with function name
static thread_local const char* g_pir_func = nullptr;
#define pirlog(fmt, ...)\
do {\
    const char* _func = g_pir_func ? g_pir_func : __func__;\
    char _pirbuf[2048];\
    _snprintf_s(\
        _pirbuf, sizeof(_pirbuf), _TRUNCATE,\
        "[%s] " fmt, _func, ##__VA_ARGS__\
    );\
    pir.debuglog.FormattedMessage(_pirbuf);\
} while (0)

// typedefs
typedef bool  (*_ParseConsoleArg_Native) (void* paramInfo, void* scriptData, void* opcodeOffsetPtr, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, ...);
typedef void  (*_SetMotionType_Native) (VirtualMachine* vm, uint32_t stackID, TESObjectREFR* objectReference, int motionType, bool allowActivate);
typedef void  (*_PlayUISound_Native)   (const char*);
typedef void  (*_PlayFileSound_Native) (const char*);
typedef float (*_GetScale_Native)      (TESObjectREFR* objRef);
typedef void  (*_SetScale_Native)      (TESObjectREFR* objRef, float scale);


// unique function pointers
_SetMotionType_Native SetMotionType_native = nullptr;
_ParseConsoleArg_Native ParseConsoleArg_native = nullptr;

// Struct to store a pointer, rel32, and final address, and optionally an ObScriptCommand pointers
// needed a lot of these so made a struct
struct SimpleFinder
{
	uintptr_t* ptr = nullptr; // pointer to a pattern match
	SInt32           r32 = 0; // rel32 of what were finding
	uintptr_t        addr = 0; // final address
	ObScriptCommand* cmd = nullptr; // for first console commands
};

class PlaceInRed {

public:

	// track plugin performance
	UInt64 start_tickcount = GetTickCount64(); // set during initialization
	UInt64 end_tickcount = 0; // updated when plugin load finishes

	// F4SE interfaces and handles
	IDebugLog               debuglog;
	const char*             plugin_log_file =  {"\\My Games\\Fallout4\\F4SE\\PlaceInRed.log"};
	std::string             plugin_ini_path =   "Data\\F4SE\\Plugins\\PlaceInRed.ini";
	const char*             sLockObjectSound =  "UIQuestInactive";
	PluginHandle            pluginHandle = kPluginHandle_Invalid;
	F4SEPapyrusInterface*   g_papyrus =       nullptr;
	F4SEMessagingInterface* g_messaging =     nullptr;
	F4SEObjectInterface*    g_object =        nullptr;
	F4SETaskInterface*      g_task =          nullptr;

	// settings and states
	bool    bF4SEGameDataIsReady = false; // set to true when F4SE tells us its ready
	bool    PLACEINRED_ENABLED = false; //pir 1w
	bool    OBJECTSNAP_ENABLED = true; //pir 2
	bool    GROUNDSNAP_ENABLED = true; //pir 3
	bool    SLOW_ENABLED = false; //pir 4
	bool    WORKSHOPSIZE_ENABLED = false; //pir 5
	bool    OUTLINES_ENABLED = true; //pir 6
	bool    ACHIEVEMENTS_ENABLED = false; //pir 7
	bool    ConsoleNameRef_ENABLED = false; //pir cnref
	bool    PrintConsoleMessages = true; // updated later to ini value and when toggled
	Float32 fOriginalZOOM = 10.0000F;  //updated later to fItemHoldDistantSpeed:Workshop
	Float32 fOriginalROTATE = 5.0000F; //updated later to fItemRotationSpeed:Workshop
	Float32 fSlowerZOOM = 1.0000F;     //updated later to plugin ini value
	Float32 fSlowerROTATE = 0.5000F;   //updated later to plugin ini value
	Float32 fRotateDegreesCustomX = 3.6000F;   //updated later to plugin ini value
	Float32 fRotateDegreesCustomY = 3.6000F;   //updated later to plugin ini value
	Float32 fRotateDegreesCustomZ = 3.6000F;   //updated later to plugin ini value

	// pointers
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

	// patch bytes
	UInt8  NOP3[3] = { 0x0F, 0x1F, 0x00 }; // 3 byte nop
	UInt8  NOP4[4] = { 0x0F, 0x1F, 0x40, 0x00 }; // 4 byte nop
	UInt8  NOP5[5] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // 5 byte nop
	UInt8  NOP6[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // 6 byte nop
	UInt8  NOP7[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 }; // 7 byte nop
	UInt8  NOP8[8] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 }; // 8 byte nop
	UInt8  C_OLD[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 }; // init NOP7 -> 0FB605???? movzx eax,byte ptr [Fallout4.exe+2E74998]
	UInt8  C_NEW[7] = { 0x31, 0xC0, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; //xor al,al;nop x5
	UInt8  CC_OLD[2] = { 0x75, 0x11 }; // JNE 0x11
	UInt8  CC_NEW[2] = { 0xEB, 0x1C }; // JMP 0x1C
	UInt8  D_OLD[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 }; // init NOP7 -> 0FB605???? movzx eax,byte ptr [Fallout4.exe+2E7499D]
	UInt8  D_NEW[7] = { 0x31, 0xC0, 0xB0, 0x01, 0x90, 0x90, 0x90 }; //xor al,al;mov al,01;nop x3
	UInt8  F_OLD[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };  // init NOP6 -> 8805???? mov [Fallout4.exe+2E74999],al
	UInt8  J_OLD[2] = { 0x74, 0x35 }; // JE 0x35
	UInt8  J_NEW[2] = { 0xEB, 0x30 }; // JMP 0x30
	UInt8  Y_OLD[3] = { 0x8B, 0x58, 0x14 }; // mov rbx,[rax+14]
	UInt8  TIMER_OLD[6] = { 0x0F, 0x85, 0xAB, 0x00, 0x00, 0x00 }; // original is jne
	UInt8  TIMER_NEW[6] = { 0xE9, 0xAC, 0x00, 0x00, 0x00, 0x90 }; // jmp instead
	UInt8  RC_OLD[5] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // init NOP5 -> E8????  redcall
	UInt8  ACHIEVE_NEW[3] = { 0x30, 0xC0, 0xC3 }; // xor al, al; ret
	UInt8  ACHIEVE_OLD[4] = { 0x48, 0x83, 0xEC, 0x28 }; // sub rsp,28
	UInt8  OSNAP_OLD[8] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 }; // init NOP8 -> F30F1035????
	UInt8  OSNAP_NEW[8] = { 0x0F, 0x57, 0xF6, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // xorps xmm6, xmm6; NOP5;
	UInt8  DRAWS_OLD[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };  // init NOP6 -> 0105???? 
	UInt8  TRIS_OLD[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };  // init NOP6 -> 0105????

	UInt8  CNameRef_OLD[6] = { 0xFF, 0x90, 0xD0, 0x01, 0x00, 0x00 }; //call qword finder [rax+000001D0]
	size_t CNameRef_OLD_Size = sizeof(CNameRef_OLD) / sizeof(CNameRef_OLD[0]);

	UInt32 CurrentWSRef_Offsets[4] = { 0x0, 0x0, 0x10, 0x110 };
	size_t CurrentWSRef_OffsetsSize = sizeof(CurrentWSRef_Offsets) / sizeof(CurrentWSRef_Offsets[0]);

	UInt8  TWO_ZEROS[2] = { 0x00, 0x00 }; //good place. written to bWSMode+0x03
	UInt8  TWO_ONES[2] = { 0x01, 0x01 }; //good place. written to bWSMode+0x09

	// getscale and setscale function pointers and related data
	_GetScale_Native  GetScale_func = nullptr;
	uintptr_t*        GetScale_pattern = nullptr;
	SInt32            GetScale_r32 = 0;
	_SetScale_Native  SetScale_func = nullptr;
	uintptr_t*        SetScale_pattern = nullptr;
	SInt32            SetScale_s32 = 0;

	// play sound function pointers and related data
	uintptr_t*            PlaySound_UI_pattern = nullptr;
	SInt32                PlaySound_UI_r32 = 0;
	_PlayUISound_Native   PlaySound_UI_func = nullptr;
	uintptr_t*            PlaySound_File_pattern = nullptr;
	SInt32                PlaySound_File_r32 = 0;
	_PlayFileSound_Native PlaySound_File_func = nullptr;

	// consolenameref function pointers and related data
	uintptr_t* cnref_original_call_pattern = nullptr; // the call when references are clicked
	uintptr_t* cnref_GetRefName_pattern = nullptr; // the good function we will point to instead
	uintptr_t  cnref_GetRefName_addr = 0; // the good function full address
	SInt32     cnref_GetRefName_r32 = 0; // rel32 of the good function



	// store base address
	uintptr_t FO4BaseAddr = 0;
	uintptr_t GetFO4BaseAddress() const
	{
		return FO4BaseAddr;
	}
	PlaceInRed()
		: FO4BaseAddr(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)))
	{
	}

	// help message
	const char* ConsoleHelpMSG =
	{
		"PlaceInRed (pir) — Command Reference\n"
		"-----------------------------------\n"
		"\n"
		"Toggles:\n"
		"  pir toggle            Toggle Place in Red\n"
		"  pir osnap             Toggle object snapping\n"
		"  pir gsnap             Toggle ground snapping\n"
		"  pir slow              Toggle slower rotate/zoom speed\n"
		"  pir workshopsize      Toggle unlimited workshop build size\n"
		"  pir outlines          Toggle object outlines\n"
		"  pir achievements      Toggle achievements with mods\n"
		"Scaling:\n"
		"  pir scaleup<N>        Scale up by N percent\n"
		"  pir scaledown<N>      Scale down by N percent\n"
		"    N = 1, 2, 5, 10, 25, 50, 75, 100\n"
		"Rotation (Degrees):\n"
		"  pir x<N>              Rotate +N degrees (X axis)\n"
		"  pir x-<N>             Rotate -N degrees (X axis)\n"
		"  pir y<N>              Rotate +N degrees (Y axis)\n"
		"  pir y-<N>             Rotate -N degrees (Y axis)\n"
		"  pir z<N>              Rotate +N degrees (Z axis)\n"
		"  pir z-<N>             Rotate -N degrees (Z axis)\n"
		"    N = 0.1, 0.5, 1, 2, 5, 10, 15, 30, 45\n"
		"Lock object in place:\n"
		"  pir lock              Lock object (disable physics)\n"
		"  pir lockq             Lock object (no sound fx)\n"
		"  pir unlock            Unlock object (enable physics)\n"
		"Misc:\n"
		"  pir wb                Toggle allow moving workbench\n"
		"  pir cnref             Show ref name in console when clicked\n"
	};

private:
	// Wrapper to handle asynchronous Utility::pattern
	//template <typename T, size_t N>
	//std::future<void> zzzFindPatternAsync(T& ptr_address, const char(&pattern)[N])
	//{
	//	return std::async(std::launch::async, [&ptr_address, &pattern] {
	//		ptr_address = Utility::pattern(pattern).count(1).get(0).get<uintptr_t>();
	//		});
	//}

};






/* helpful notes
  interesting bytes starting at bWSMode (Fallout4.exe+2E74994)

example               01      00        ??   00  00        ??     00      ?? ??    01  01       01
label                 bwsmode holdingE       zerochecks           exitws           onechecks    something grabbed

Fallout4.exe+2E749??  94      95        96   97  98        99     9A      9B 9C    9D  9E       0x9F
bwsmode offset        +0      +1        +2   +3  +4        +5     +6      +7 +8    +9  +A       +B
*/