#include "main.h"
#include "patterns.h"

#include <array>
#include <cctype> // std::tolower
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <excpt.h>
#include <fstream>
#include <future>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// F4SE API
#include "common/ITypes.h"
#include "f4se_common/f4se_version.h"
#include "f4se_common/Relocation.h"
#include "f4se_common/SafeWrite.h"
#include "f4se_common/Utilities.h"
#include "f4se/ObScript.h"
#include "f4se/GameExtraData.h"
#include "f4se/GameForms.h"
#include "f4se/GameReferences.h"
#include "f4se/NiTypes.h"
#include "f4se/NiObjects.h"
#include "f4se/PluginAPI.h"
#include "f4se/GameAPI.h"

static void ReadINI();

UInt32 pluginVersion = 17;
PlaceInRed pir;

// Simple function to read memory (safe version)
static bool ReadMemory(uintptr_t addr, void* data, size_t len)
{
	if (!addr || !data || len == 0)
		return false;

	__try
	{
		memcpy(data, reinterpret_cast<const void*>(addr), len);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

// Read a UInt8 flag from WSMode safely
static bool ReadWSModeFlag(uintptr_t offset)
{
	if (!WSMode.ptr)
		return false;

	UInt8 value = 0;
	if (!ReadMemory(uintptr_t(WSMode.addr) + offset, &value, sizeof(value)))
		return false;

	return value == WSMODE_TRUE;
}

// Determine if player is in workshop mode
static bool IsPlayerInWorkshopMode()
{
	return ReadWSModeFlag(WSMODE_OFFSET_PLAYERINWSMODE);
}

// Is the player grabbing the current workshop ref
static bool IsCurrentWSRefGrabbed()
{
	return ReadWSModeFlag(WSMODE_OFFSET_PLAYERGRABBINGOBJECT);
}

// string to float
static float FloatFromString(std::string fString, float min = 0.001, float max = 999.999, float error = 0)
{
	float theFloat = 0;
	try
	{
		theFloat = std::stof(fString);
	}
	catch (...)
	{
		return error;
	}
	//if (theFloat > min && theFloat < max) {
	if (theFloat >= min && theFloat <= max) {
		return theFloat;
	}
	else {
		return error;
	}
}

static void StripNewLinesAndPipesToBuffer(const char* in, char* out, size_t outSize)
{
	if (!out || outSize == 0) return;
	out[0] = '\0';
	if (!in) return;

	size_t j = 0;
	for (size_t i = 0; in[i] && j + 1 < outSize; ++i)
	{
		const char c = in[i];
		if (c != '\n' && c != '\r' && c != '|')
			out[j++] = c;
	}
	out[j] = '\0';
}

/**
 * Resolves a 32-bit RIP-relative offset from a pattern match into a Relative Virtual Address (RVA).
 *
 * This is the classic pattern used in Fallout 4 modding / signature scanning
 * to convert a found `rel32` into an absolute RVA.
 *
 * @param instr  Absolute address of the instruction containing the rel32.
 * @param start  Byte offset from `instr` to the start of the 4-byte rel32.
 * @param end    Byte offset from `instr` to the RIP base (usually instruction length).
 * @param shift  Optional post-calculation displacement (can be negative).
 * @return       The calculated RVA (32-bit unsigned), or 0 on memory read failure or underflow.
 */
[[nodiscard]]
static std::uint32_t GetRel32FromPattern(std::uintptr_t instr, std::size_t start, std::size_t end, std::ptrdiff_t shift = 0) noexcept
{
	if (instr == 0) [[unlikely]]
		return 0;

	std::int32_t rel32 = 0;

	// Read the signed 32-bit relative offset from the pattern match
	if (!ReadMemory(instr + start, &rel32, sizeof(rel32)))
		return 0; // memory read failure

	// 1. Calculate RIP (address of the next instruction)
	const std::uintptr_t rip = instr + end;

	// 2. Calculate absolute target with correct sign-extension
	//    CRITICAL: rel32 must be cast to ptrdiff_t before adding to uintptr_t
	const std::uintptr_t absolute_target = rip + static_cast<std::ptrdiff_t>(rel32) + shift;

	// 3. Prevent underflow (target resolves before module base)
	if (absolute_target < pir.FO4BaseAddr) [[unlikely]]
		return 0;

	// 4. Convert absolute address to RVA
	return static_cast<std::uint32_t>(absolute_target - pir.FO4BaseAddr);
}

// follow multiple pointers with offsets to get final address
static uintptr_t GimmeMultiPointer(uintptr_t baseAddress, UInt32* offsets, UInt32 numOffsets)
{
	if (!baseAddress)
		return 0;

	uintptr_t address = baseAddress;

	for (UInt32 i = 0; i < numOffsets; i++) {
		if (!ReadMemory(address + offsets[i], &address, sizeof(address)))
			return 0;
	}
	return address;
}

// Returns a span covering the primary executable code section of the current process.
// The result is cached statically (computed only once) and is safe to call from any thread at any time.
static inline std::span<const uint8_t> GetFalloutCodeSection()
{
	static auto code_section = []() -> std::span<const uint8_t> {
		HMODULE mod = GetModuleHandleW(nullptr);
		if (!mod) return {};

		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(mod);
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>((uint8_t*)mod + dos->e_lfanew);

		if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
			return { reinterpret_cast<uint8_t*>(mod), nt->OptionalHeader.SizeOfImage };

		auto* section = IMAGE_FIRST_SECTION(nt);
		std::span<const uint8_t> best = {};

		for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
			if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
				(section->Characteristics & IMAGE_SCN_MEM_READ)) {
				DWORD size = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
				auto candidate = std::span<const uint8_t>{
					reinterpret_cast<uint8_t*>(mod) + section->VirtualAddress, size };
				if (candidate.size() > best.size())
					best = candidate;
			}
		}
		return best.size() != 0 ? best : std::span<const uint8_t>{ reinterpret_cast<uint8_t*>(mod), nt->OptionalHeader.SizeOfImage };
		}();
	return code_section;
}

static inline uintptr_t PatternScanV2(std::span<const uint8_t> memory, const ParsedPattern& pat)
{
	if (pat.bytes.empty() || memory.size() < pat.bytes.size()) return 0;

	const uint8_t* data = memory.data();
	const size_t   len = memory.size();
	const size_t   plen = pat.bytes.size();
	const size_t   scanEnd = len - plen;

	for (size_t i = 0; i <= scanEnd; )
	{
		const void* match = std::memchr(data + i + pat.anchorPos, pat.anchorVal, len - (i + pat.anchorPos));
		if (!match) return 0;

		i = static_cast<const uint8_t*>(match) - data - pat.anchorPos;
		if (i > scanEnd) return 0;

		bool found = true;
		for (size_t j = 0; j < plen; ++j)
		{
			if ((data[i + j] & pat.mask[j]) != (pat.bytes[j] & pat.mask[j]))
			{
				found = false;
				break;
			}
		}
		if (found) return reinterpret_cast<uintptr_t>(data + i);

		++i;
	}
	return 0;
}

template <typename T>
std::future<void> FindPatternAsyncV2(T& ptr_address, const ParsedPattern& pat)
{
	return std::async(std::launch::async,
		[&ptr_address, &pat]() noexcept
		{
			ptr_address = nullptr;
			uintptr_t match = PatternScanV2(GetFalloutCodeSection(), pat);
			if (match)
				ptr_address = reinterpret_cast<T>(match);
		});
}

// return the ini path as a std string
static const std::string& GetPluginINIPath()
{
	static const std::string s_configPath = []() {
		std::string runtimePath = GetRuntimeDirectory();
		
		// Explicitly log <empty> if the string has no characters		
		return runtimePath.empty() ? "" : runtimePath + pir.plugin_ini_path;
	}();
	return s_configPath;
}

// convert ini string to bool
static bool GetBoolFromINIString(const std::string& s, bool defaultValue = false)
{
	std::string lower;
	lower.reserve(s.size());
	for (unsigned char c : s)
		lower += static_cast<char>(std::tolower(c));

	// Trim whitespace
	const auto start = lower.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return defaultValue;

	const auto end = lower.find_last_not_of(" \t\r\n");
	const std::string trimmed = lower.substr(start, end - start + 1);

	if (trimmed == "1" || trimmed == "true" || trimmed == "yes" || trimmed == "on")
		return true;
	if (trimmed == "0" || trimmed == "false" || trimmed == "no" || trimmed == "off")
		return false;

	return defaultValue;
}

// Check if we found all the required memory patterns
static bool FoundPatterns()
{
    if (
		ParseConsoleArg.ptr     &&
        FirstConsole.ptr        &&
        FirstObScript.ptr       &&
        SetScale_pattern    &&
        GetScale_pattern    &&
        CurrentWSRef.ptr        &&
        WSMode.ptr              &&
        TheFO4Console.ptr            &&
        A && B && C             &&
        D && E && F && G && H   &&
        J && Y && R && CORRECT  &&
        wstimer && gsnap && osnap && outlines &&
        WSSize.ptr              &&
        Zoom.ptr                &&
        Rotate.ptr              &&
        SetMotionType.ptr       &&
        WorkbenchSelection.ptr  &&
		InvalidRefHandle.ptr
		)
    {
        /*
            These are allowed to be missing (non-critical):
            - achievements
            - ConsoleRefCallFinder (copy of another mod, never required)
            - GDataHandlerFinder (not using yet)
            - survivalconsole
        */
        return true;
    }

    pirlog("Couldn't find required memory patterns! Check for conflicting mods.");
    return false;
}

// log all the memory patterns to the log file
static void LogPatterns()
{
	// Cache the base address locally to prevent repeated loads/casts
	const uintptr_t base = pir.FO4BaseAddr;

	auto Log = [&](const char* label, auto value, uintptr_t rva) {
		pir.debuglog.FormattedMessage("%s:%p|Fallout4.exe+0x%08X", label, value, rva);
	};

	pir.debuglog.FormattedMessage("--------------------------------------------------------------------------------");
	pir.debuglog.FormattedMessage("Base            :%p|Fallout4.exe+0x00000000", (void*)base);
	Log("bWSMode         ", WSMode.ptr, WSMode.r32);
	Log("achievements    ", achievements, (uintptr_t)achievements - base);
	Log("survivalconsole ", survivalconsole, (uintptr_t)survivalconsole - base);
	Log("A               ", A, (uintptr_t)A - base);
	Log("B               ", B, (uintptr_t)B - base);
	pir.debuglog.FormattedMessage("C               :%p|Fallout4.exe+0x%08X (OLD: %02X%02X%02X%02X%02X%02X%02X)",C, (uintptr_t)C - base, C_OLD[0], C_OLD[1], C_OLD[2], C_OLD[3], C_OLD[4], C_OLD[5], C_OLD[6]);
	Log("CORRECT         ", CORRECT, (uintptr_t)CORRECT - base);
	pir.debuglog.FormattedMessage("D               :%p|Fallout4.exe+0x%08X (OLD: %02X%02X%02X%02X%02X%02X%02X)",D, (uintptr_t)D - base, D_OLD[0], D_OLD[1], D_OLD[2], D_OLD[3], D_OLD[4], D_OLD[5], D_OLD[6]);
	Log("E               ", E, (uintptr_t)E - base);
	Log("F               ", F, (uintptr_t)F - base);
	Log("G               ", G, (uintptr_t)G - base);
	Log("H               ", H, (uintptr_t)H - base);
	Log("J               ", J, (uintptr_t)J - base);
	Log("R               ", R, (uintptr_t)R - base);
	Log("Y               ", Y, (uintptr_t)Y - base);
	Log("CurrentWSRef    ", CurrentWSRef.ptr, CurrentWSRef.r32);
	Log("FirstConsole    ", FirstConsole.ptr, FirstConsole.r32);
	Log("FirstObScript   ", FirstObScript.ptr, FirstObScript.r32);
	Log("GetConsoleArg   ", ParseConsoleArg.ptr, ParseConsoleArg.r32);
	Log("GetScale        ", GetScale_pattern, GetScale_r32);
	Log("InvalidRefHandle", InvalidRefHandle.ptr, InvalidRefHandle.r32);
	Log("GConsole        ", TheFO4Console.ptr, TheFO4Console.r32);
	Log("gsnap           ", gsnap, (uintptr_t)gsnap - base);
	Log("osnap           ", osnap, (uintptr_t)osnap - base);
	Log("outlines        ", outlines, (uintptr_t)outlines - base);
	//Log("PlayFileSound   ", PlaySound_File_pattern, PlaySound_File_r32);
	Log("PlayUISound     ", PlaySound_UI_pattern, PlaySound_UI_r32);
	Log("SetMotionType   ", SetMotionType.ptr, SetMotionType.r32);
	Log("SetScale        ", SetScale_pattern, SetScale_s32);
	Log("WBSelect        ", WorkbenchSelection.ptr, WorkbenchSelection.r32);
	Log("WSTimer         ", wstimer, (uintptr_t)wstimer - base);
	Log("WSSizeFloats    ", WSSize.addr, (uintptr_t)WSSize.addr - base);
	Log("WSSizeFinder    ", WSSize.ptr, (uintptr_t)WSSize.ptr - base);
	pir.debuglog.FormattedMessage("Rotate          :%p|%p|orig %f|slow %f", Rotate.ptr, Rotate.addr, pir.fOriginalROTATE, pir.fSlowerROTATE);
	pir.debuglog.FormattedMessage("Zoom            :%p|%p|orig %f|slow %f", Zoom.ptr, Zoom.addr, pir.fOriginalZOOM, pir.fSlowerZOOM);
	pir.debuglog.FormattedMessage("--------------------------------------------------------------------------------");
}


// return the currently selected workshop ref with some safety checks
static TESObjectREFR* GetCurrentWSRef(bool bOnlySelectReferences = true)
{
	if (!CurrentWSRef.ptr || !CurrentWSRef.addr || !IsPlayerInWorkshopMode())
		return nullptr;

	uintptr_t refaddr = GimmeMultiPointer(CurrentWSRef.addr, CurrentWSRef_Offsets, CurrentWSRef_OffsetsSize);
	if (!refaddr)
		return nullptr;

	TESObjectREFR* ref = reinterpret_cast<TESObjectREFR*>(refaddr);

	__try
	{
		// Native member lookup safely catches bad pointers / unmapped memory pages
		if (!ref->formID || ref->formID == 0)
			return nullptr;

		if (bOnlySelectReferences && ref->formType != 0x40)
			return nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}

	return ref;
}


// debug use only - detailed log of TESObjectREFR
static void LogWSRef()
{
	TESObjectREFR* ref = GetCurrentWSRef(0);

	if (!ref) {

		return;
	}

	//
	// Identity
	//
	_MESSAGE("Ref Ptr:      %p", ref);
	_MESSAGE("FormID:       %08X", ref->formID);
	_MESSAGE("FormType:     %02X", ref->GetFormType());
	_MESSAGE("Flags:        %08X", ref->flags);

	if (ref->baseForm)
		_MESSAGE("BaseForm: %p (%08X)", ref->baseForm, ref->baseForm->formID);
	else
		_MESSAGE("BaseForm: NULL");

	//
	// Name (safe virtual)
	//
	const char* name = nullptr;
	__try { name = CALL_MEMBER_FN(ref, GetReferenceName)(); }
	__except (EXCEPTION_EXECUTE_HANDLER) { name = nullptr; }

	_MESSAGE("Name:         %s", name ? name : "(none)");


	UInt8 formtype = 0;
	__try { formtype = *(UInt8*)((UInt8*)ref + 0x1A); }
	__except (EXCEPTION_EXECUTE_HANDLER) { formtype = 0xFF; }

	_MESSAGE("Ref+0x1A:     0x%02X", formtype);

	//
	// Transform
	//
	_MESSAGE("Position:     X=%.4f Y=%.4f Z=%.4f",
		ref->pos.x, ref->pos.y, ref->pos.z);

	_MESSAGE("Rot(rad):     X=%.4f Y=%.4f Z=%.4f",
		ref->rot.x, ref->rot.y, ref->rot.z);

	_MESSAGE("Rot(deg):     X=%.2f Y=%.2f Z=%.2f",
		ref->rot.x * 57.2957795f,
		ref->rot.y * 57.2957795f,
		ref->rot.z * 57.2957795f);

	//
	// Cell / world
	//
	_MESSAGE("ParentCell:   %p", ref->parentCell);

	TESWorldSpace* ws = nullptr;
	__try { ws = CALL_MEMBER_FN(ref, GetWorldspace)(); }
	__except (EXCEPTION_EXECUTE_HANDLER) { ws = nullptr; }

	_MESSAGE("Worldspace:   %p", ws);

	//
	// HandleRefObject
	//
	_MESSAGE("HandleRefCount:%u", ref->handleRefObject.QRefCount());

	//
	// Loaded 3D
	//
	_MESSAGE("LoadedData:     %p", ref->unkF0);
	if (ref->unkF0) {
		_MESSAGE("   RootNode:    %p", ref->unkF0->rootNode);
		_MESSAGE("   Flags:       %016llX", ref->unkF0->flags);
	}

	//
	// Inventory
	//
	_MESSAGE("InventoryList:  %p", ref->inventoryList);

	//
	// ExtraData
	//
	_MESSAGE("ExtraDataList:  %p", ref->extraDataList);

	if (ref->extraDataList) {
		BSExtraData* data = nullptr;
		__try { data = ref->extraDataList->m_data; }
		__except (EXCEPTION_EXECUTE_HANDLER) { data = nullptr; }

		UInt32 i = 0;
		while (data && i < 512) {
			UInt8 type = 0xFF;
			__try { type = data->type; }
			__except (EXCEPTION_EXECUTE_HANDLER) {}

			_MESSAGE("   Extra[%03u]: type=0x%02X ptr=%p", i, type, data);

			__try { data = data->next; }
			__except (EXCEPTION_EXECUTE_HANDLER) { break; }
			++i;
		}
	}

	//
	// Raw internal fields (IN ORDER)
	//
	_MESSAGE("Internal Fields:");
	_MESSAGE("   unk60:   %p", ref->unk60);
	_MESSAGE("   unk68:   %p", ref->unk68);
	_MESSAGE("   unk70:   %08X", ref->unk70);
	_MESSAGE("   unk74:   %08X", ref->unk74);
	_MESSAGE("   unk78:   %08X", ref->unk78);
	_MESSAGE("   unk7C:   %08X", ref->unk7C);
	_MESSAGE("   unk80:   %016llX", ref->unk80);
	_MESSAGE("   unk88:   %016llX", ref->unk88);
	_MESSAGE("   unk90:   %016llX", ref->unk90);
	_MESSAGE("   unk98:   %016llX", ref->unk98);
	_MESSAGE("   unkA0:   %016llX", ref->unkA0);
	_MESSAGE("   unkA8:   %016llX", ref->unkA8);
	_MESSAGE("   unkB0:   %016llX", ref->unkB0);
	_MESSAGE("   unkE8:   %p", ref->unkE8);
	_MESSAGE("   unk104:  %08X", ref->unk104);
	_MESSAGE("   unk108:  %08X", ref->unk108);
	_MESSAGE("=============== END DEBUG ===============");
}

const char* sLockObjectSound = "UIQuestInactive";
// lock the current WS ref in place by changing the motion type to keyframed
static void LockUnlockWSRef(bool unlock = false, bool sound = false)
{
	if (!g_gameVM || !*g_gameVM) return;
	VirtualMachine* vm = (*g_gameVM)->m_virtualMachine;
	TESObjectREFR* ref = GetCurrentWSRef();
	UInt32 motion = 2; // Motion_Keyframed

	if (unlock) {
		motion = 1; // Motion_Dynamic
	}

	if (vm && ref) {
		SetMotionType_native(vm, NULL, ref, motion, false);
		if (sound) {
			PlaySound_UI_func(sLockObjectSound);
		}
	}
}

// Toggles the current WS ref between Dynamic (1) and Keyframed (2)
static void ToggleLockWSRef(bool sound = false)
{
	// Check global VM pointer safely
	if (!g_gameVM || !*g_gameVM) return;

	VirtualMachine* vm = (*g_gameVM)->m_virtualMachine;
	TESObjectREFR* ref = GetCurrentWSRef();

	// Early exit if the virtual machine or reference is invalid
	if (!vm || !ref) return;

	// Flip the global state: if it was 2, make it 1. If it was 1, make it 2.
	pir.uLockUnlockLastMotionType = (pir.uLockUnlockLastMotionType == 2) ? 1 : 2;

	// Apply the new toggled state from the global
	SetMotionType_native(vm, 0, ref, pir.uLockUnlockLastMotionType, false);

	// Play sound if requested
	if (sound) {
		PlaySound_UI_func(sLockObjectSound);
	}
}

// djb2 hash: Iterative AND constexpr. To switch with strings
static constexpr unsigned int ConsoleSwitch(const char* s)
{
	unsigned int hash = 5381u;
	while (s && *s)
	{
		hash = ((hash << 5) + hash) ^ static_cast<unsigned char>(*s++); // (hash * 33) ^ c
	}
	return hash;
}

// print to console (copied from f4se + modified to use pattern)
static void PIR_ConsolePrintOld(const char* fmt, ...)
{
	if (TheFO4Console.ptr && TheFO4Console.addr && pir.PrintConsoleMessages)
	{
		ConsoleManager* mgr = (ConsoleManager*)TheFO4Console.addr;
		if (mgr) {
			va_list args;
			va_start(args, fmt);
			char buf[4096];
			int len = vsprintf_s(buf, sizeof(buf), fmt, args);
			if (len > 0)
			{
				// add newline and terminator, truncate if not enough room
				if (len > (sizeof(buf) - 2))
					len = sizeof(buf) - 2;

				buf[len] = '\n';
				buf[len + 1] = 0;

				CALL_MEMBER_FN(mgr, Print)(buf);
			}
			va_end(args);
		}
	}
}

// print to console (copied from f4se + modified to use pattern)
static void PIR_ConsolePrint(const char* fmt, ...)
{
	if (!TheFO4Console.ptr || !TheFO4Console.addr || !pir.PrintConsoleMessages)
		return;

	ConsoleManager* mgr = (ConsoleManager*)TheFO4Console.addr;
	if (!mgr)
		return;

	va_list args;
	va_start(args, fmt);

	char buf[4096];

	// Leave exactly 1 byte of room so we can overwrite the null terminator with \n
	int len = _vsnprintf_s(buf, sizeof(buf) - 1, _TRUNCATE, fmt, args);
	va_end(args);

	if (len >= 0)
	{
		// Normal case: overwrite the auto-placed \0 with \n
		buf[len] = '\n';
		buf[len + 1] = '\0';
	}
	else
	{
		// Truncated case: overwrite the last character placed by _vsnprintf_s
		buf[sizeof(buf) - 2] = '\n';
		buf[sizeof(buf) - 1] = '\0';
	}

	CALL_MEMBER_FN(mgr, Print)(buf);
}

//toggle printing console messages
static bool Toggle_ConsolePrint()
{
	if (pir.PrintConsoleMessages == true) {
		PIR_ConsolePrint("Disabling all console messages from Place in Red.");
		pir.PrintConsoleMessages = false;
		return true;
	}
	else {
		pir.PrintConsoleMessages = true;
		PIR_ConsolePrint("Enabled PIR console print.");
		return true;
	}
	return false;
}

// f4se message interface handler
static void MessageInterfaceHandler(F4SEMessagingInterface::Message* msg)
{
	switch (msg->type) {

	case F4SEMessagingInterface::kMessage_GameDataReady:
		pir.bF4SEGameDataIsReady = true;
		break;

	default: 
		break;
	}
}

// Set the scale of the current workshop reference (highlighted or grabbed)
static bool SetCurrentRefScale(float newScale)
{
	TESObjectREFR* ref = GetCurrentWSRef();
	if (ref) {
		SetScale_func(ref, newScale);
		return true;
	}
	return false;
}

// Move reference to itself and optionally repeat the operation to reduce jitter.
static void MoveRefToSelf(int repeat = 0)
{
	TESObjectREFR* ref = GetCurrentWSRef();
	if (!ref) return;

	TESObjectCELL* parentCell = ref->parentCell;
	TESWorldSpace* worldspace = CALL_MEMBER_FN(ref, GetWorldspace)();

	NiPoint3 staticPos = ref->pos;
	NiPoint3 staticRot = ref->rot;

	for (int i = 0; i <= repeat; i++)
	{
		// Safely resolve the invalid handle, falling back to 0 based on your memory dump
		UInt32 nullHandle = InvalidRefHandle.addr ? *(reinterpret_cast<UInt32*>(InvalidRefHandle.addr)) : 0;
		MoveRefrToPosition(ref, &nullHandle, parentCell, worldspace, &staticPos, &staticRot);
	}
}

// Modify the scale of the current workshop reference by a percent.
static bool ModCurrentRefScaleOld(float fMultiplyAmount)
{
	TESObjectREFR* ref = GetCurrentWSRef();
	if (ref) {
		float oldscale = GetScale_func(ref);
		float newScale = oldscale * (fMultiplyAmount);
		if (newScale > 9.9999f) { newScale = 9.9999f; }
		if (newScale < 0.0001f) { newScale = 0.0001f; }
		SetScale_func(ref, newScale);

		// fix jitter only if player isnt grabbing the item
		if (IsCurrentWSRefGrabbed() == false) {
			MoveRefToSelf(1);
		}

		return true;
	}
	return false;
}

// Modify the scale of the current workshop reference by a percent, with improved jitter handling.
static bool ModCurrentRefScale(float fMultiplyAmount)
{
	TESObjectREFR* ref = GetCurrentWSRef();
	if (!ref) return false;

	float oldscale = GetScale_func(ref);
	float newScale = oldscale * fMultiplyAmount;

	if (newScale > 9.9999f) newScale = 9.9999f;
	if (newScale < 0.0001f) newScale = 0.0001f;

	SetScale_func(ref, newScale);

	if (!IsCurrentWSRefGrabbed()){
		NiNode* rootNode = ref->GetObjectRootNode();		
		MoveRefToSelf(0);

		if (rootNode) {
			NiAVObject* avNode = reinterpret_cast<NiAVObject*>(rootNode);
			avNode->flags |= NiAVObject::kFlagForceUpdate;
			avNode->flags |= NiAVObject::kFlagScenegraphChange;
		}
		MoveRefToSelf(0);

	}

	return true;
}

static void ResetCurrentWSRefRotation()
{
	TESObjectREFR* ref = GetCurrentWSRef();
	if (!ref) return;

	NiPoint3 resetRot = ref->rot;
	resetRot.x = 0;
	resetRot.y = 0;
	resetRot.z = 0;

	TESObjectCELL* cell = ref->parentCell;
	TESWorldSpace* worldSpace = (cell) ? cell->worldSpace : nullptr;

	// FIX: Pass valid handle pointer to prevent engine access violation CTD
	//UInt32 nullHandle = *g_invalidRefHandle;
	UInt32 nullHandle = InvalidRefHandle.addr ? *(reinterpret_cast<UInt32*>(InvalidRefHandle.addr)) : 0;
	MoveRefrToPosition(
		ref,
		&nullHandle,
		cell,
		worldSpace,
		&ref->pos,
		&resetRot
	);
}


// Helper to wrap angles into the [-PI, PI] range
inline float NormalizeAngle(float angle) {
	// fmodf is faster than while loops for large degree offsets
	float wrapped = fmodf(angle + (float)PI_20_DIGITS, (float)(PI_20_DIGITS * 2.0f));
	if (wrapped < 0) wrapped += (float)(PI_20_DIGITS * 2.0f);
	return wrapped - (float)PI_20_DIGITS;
}

// Rotate the current workshop reference by specified degree amounts on each axis
static void RotateCurrentWSRefByDegrees(float dXDeg, float dYDeg, float dZDeg)
{
	TESObjectREFR* ref = GetCurrentWSRef();
	if (!ref) return;

	constexpr float DegToRad = (float)PI_20_DIGITS / 180.0f;

	NiPoint3 newRot = ref->rot;
	newRot.x = NormalizeAngle(newRot.x + (dXDeg * DegToRad)); // Pitch
	newRot.y = NormalizeAngle(newRot.y + (dYDeg * DegToRad)); // Roll
	newRot.z = NormalizeAngle(newRot.z + (dZDeg * DegToRad)); // Yaw

	TESObjectCELL* cell = ref->parentCell;
	TESWorldSpace* worldSpace = (cell) ? cell->worldSpace : nullptr;

	// FIX: Pass valid handle pointer to prevent engine access violation CTD
	//UInt32 nullHandle = *g_invalidRefHandle;
	UInt32 nullHandle = InvalidRefHandle.addr ? *(reinterpret_cast<UInt32*>(InvalidRefHandle.addr)) : 0;
	MoveRefrToPosition(
		ref,
		&nullHandle,
		cell,
		worldSpace,
		&ref->pos,
		&newRot
	);
}

// Single-axis helpers for readability and console bindings
static inline void RotateCurrentWSRefByDegreesZ(float deg)
{
	// yaw = Z axis
	RotateCurrentWSRefByDegrees(0.0f, 0.0f, deg);
}

static inline void RotateCurrentWSRefByDegreesX(float deg)
{
	// pitch = X axis
	RotateCurrentWSRefByDegrees(deg, 0.0f, 0.0f);
}

static inline void RotateCurrentWSRefByDegreesY(float deg)
{
	// roll = Y axis
	RotateCurrentWSRefByDegrees(0.0f, deg, 0.0f);
}

// dump console and obscript commands to plugin log file
static bool DumpCmds()
{
	if (!FirstConsole.cmd || !FirstObScript.cmd) {
		return false;
	}

	pirlog("---------------------------------------------------------");
	pirlog("Type|opcode|rel32|address|short|long|params|needsparent|helptext");

	auto DumpTable = [&](const char* type, ObScriptCommand* start, uint32_t base, uint32_t max)
		{
			constexpr uint32_t HARD_MAX = 4096;
			ObScriptCommand* iter = start;

			for (uint32_t i = 0; i < HARD_MAX; ++i, ++iter)
			{
				if (!iter) break;
				if (iter->opcode < base || iter->opcode >= base + max) break;

				uint64_t func = (uint64_t)iter->execute;
				if (!func || !iter->shortName || !iter->longName)
					continue;

				uint64_t rel = (func >= pir.FO4BaseAddr)
					? (func - pir.FO4BaseAddr)
					: 0;

				char helpBuf[2048];
				StripNewLinesAndPipesToBuffer(iter->helpText, helpBuf, sizeof(helpBuf));

				pirlog(
					"%s|%06X|Fallout4.exe+0x%08llX|%p|%s|%s|%X|%X|%s",
					type,
					iter->opcode,
					rel,
					(void*)func,
					iter->shortName,
					iter->longName,
					iter->numParams,
					iter->needsParent,
					helpBuf
				);
			}
		};

	DumpTable("Console", FirstConsole.cmd, kObScript_ConsoleOpBase, kObScript_NumConsoleCommands);
	DumpTable("ObScript", FirstObScript.cmd, kObScript_ScriptOpBase, kObScript_NumObScriptCommands);

	return true;
}

//toggle object outlines on next object
static bool Toggle_Outlines()
{
	if (outlines && pir.OUTLINES_ENABLED) {
		SafeWrite8((uintptr_t)outlines + 0x06, 0x00); //objects
		SafeWrite8((uintptr_t)outlines + 0x0D, 0xEB); //npcs
		pir.OUTLINES_ENABLED = false;
		PIR_ConsolePrint("Object outlines disabled");
		return true;
	}
	if (outlines && !pir.OUTLINES_ENABLED) {
		SafeWrite8((uintptr_t)outlines + 0x06, 0x01); //objects
		SafeWrite8((uintptr_t)outlines + 0x0D, 0x76); //npcs
		pir.OUTLINES_ENABLED = true;
		PIR_ConsolePrint("Object outlines enabled");
		return true;
	}
	return false;
}

//toggle slower object rotation and zoom speed
static bool Toggle_SlowZoomAndRotate()
{
	// its on, turn it off
	if (Zoom.ptr && Rotate.ptr && Zoom.addr && Rotate.addr && pir.SLOW_ENABLED) {
		SafeWriteBuf(Zoom.addr, &pir.fOriginalZOOM, sizeof(Float32));
		SafeWriteBuf(Rotate.addr, &pir.fOriginalROTATE, sizeof(Float32));
		pir.SLOW_ENABLED = false;
		PIR_ConsolePrint("Slow zoom/rotate - disabled");
		return true;
	}
	// its off, turn it on
	if (Zoom.ptr && Rotate.ptr && Zoom.addr && Rotate.addr && !pir.SLOW_ENABLED) {
		SafeWriteBuf(Zoom.addr, &pir.fSlowerZOOM, sizeof(Float32));
		SafeWriteBuf(Rotate.addr, &pir.fSlowerROTATE, sizeof(Float32));
		pir.SLOW_ENABLED = true;
		PIR_ConsolePrint("Slow zoom/rotate - enabled");
		return true;
	}
	return false;
}

//toggle infinite workshop size
static bool Toggle_WorkshopSize()
{
	if (WSSize.ptr && pir.WORKSHOPSIZE_ENABLED) {
		SafeWriteBuf((uintptr_t)WSSize.ptr, DRAWS_OLD, sizeof(DRAWS_OLD));
		SafeWriteBuf((uintptr_t)WSSize.ptr + 0x0A, TRIS_OLD, sizeof(TRIS_OLD));
		pir.WORKSHOPSIZE_ENABLED = false;
		PIR_ConsolePrint("Unlimited workshop size disabled");
		return true;
	}

	if (WSSize.ptr && pir.WORKSHOPSIZE_ENABLED == false) {
		// Write nop 6 so its never increased
		SafeWriteBuf((uintptr_t)WSSize.ptr, NOP6, sizeof(NOP6));
		SafeWriteBuf((uintptr_t)WSSize.ptr + 0x0A, NOP6, sizeof(NOP6));

		// set current ws draws and triangles to zero
		//SafeWrite64(WSSize.addr, 0); works but not really good practice
		SafeWrite32(WSSize.addr, 0);
		SafeWrite32(WSSize.addr + 0x04, 0);

		pir.WORKSHOPSIZE_ENABLED = true;
		PIR_ConsolePrint("Unlimited workshop size enabled");
		return true;
	}
	return false;
}

//toggle groundsnap
static bool Toggle_GroundSnap()
{
	if (gsnap && pir.GROUNDSNAP_ENABLED) {

		SafeWrite8((uintptr_t)gsnap + 0x01, 0x85);
		pir.GROUNDSNAP_ENABLED = false;
		PIR_ConsolePrint("Ground snap disabled");
		return true;
	}
	if (gsnap && !pir.GROUNDSNAP_ENABLED) {
		SafeWrite8((uintptr_t)gsnap + 0x01, 0x86);
		pir.GROUNDSNAP_ENABLED = true;
		PIR_ConsolePrint("Ground snap enabled (game default)");
		return true;

	}
	return false;
}

//toggle objectsnap
static bool Toggle_ObjectSnap()
{
	// its on - toggle it off
	if (osnap && pir.OBJECTSNAP_ENABLED) {
		SafeWriteBuf((uintptr_t)osnap, OSNAP_NEW, sizeof(OSNAP_NEW));
		pir.OBJECTSNAP_ENABLED = false;
		PIR_ConsolePrint("Object snap disabled");
		return true;
	}
	// its off - toggle it on
	if (osnap && !pir.OBJECTSNAP_ENABLED) {
		SafeWriteBuf((uintptr_t)osnap, OSNAP_OLD, sizeof(OSNAP_OLD));
		pir.OBJECTSNAP_ENABLED = true;
		PIR_ConsolePrint("Object snap enabled (game default)");
		return true;
	}
	return false;
}

//toggle allowing achievements with mods
static bool Toggle_Achievements()
{
	// its on - toggle it off
	if (achievements && pir.ACHIEVEMENTS_ENABLED) {
		SafeWriteBuf((uintptr_t)achievements, ACHIEVE_OLD, sizeof(ACHIEVE_OLD));
		pir.ACHIEVEMENTS_ENABLED = false;
		PIR_ConsolePrint("Achievements with mods disabled (game default)");
		return true;
	}
	// its off - toggle it on
	if (achievements && !pir.ACHIEVEMENTS_ENABLED) {
		SafeWriteBuf((uintptr_t)achievements, ACHIEVE_NEW, sizeof(ACHIEVE_NEW));
		pir.ACHIEVEMENTS_ENABLED = true;
		PIR_ConsolePrint("Achievements with mods enabled!");
		return true;
	}
	return false;
}

//toggle survival console
static bool Toggle_SurvivalConsole()
{
	// set to game default
	if (survivalconsole && pir.bAllowConsoleInSurvival) {
		SafeWrite8((uintptr_t)survivalconsole + 0x08, 0x75); //jne
		pir.bAllowConsoleInSurvival = false;
		PIR_ConsolePrint("Console in survival disabled (game default)");
		return true;
	}
	// patch and allow console in survival
	if (survivalconsole && !pir.bAllowConsoleInSurvival) {
		SafeWrite8((uintptr_t)survivalconsole + 0x08, 0xEB); //jmp
		pir.bAllowConsoleInSurvival = true;
		PIR_ConsolePrint("Console in survival enabled!");
		return true;
	}
	return false;
}

// toggle consolenameref
static bool Toggle_ConsoleNameRef()
{
	if (!cnref_GetRefName_pattern || !cnref_original_call_pattern) {
		return false;
	}

	// toggle off
	if (pir.ConsoleNameRef_ENABLED)
	{
		SafeWriteBuf(uintptr_t(cnref_original_call_pattern), CNAMEREF_OLD, CNAMEREF_OLD_SIZE);
		pir.ConsoleNameRef_ENABLED = false;
		PIR_ConsolePrint("ConsoleRefName toggled off.");
		return true;
	}

	// toggle on
	if (!pir.ConsoleNameRef_ENABLED)
	{
		SafeWriteCall(uintptr_t(cnref_original_call_pattern), cnref_GetRefName_addr); //patch call
		SafeWrite8(uintptr_t(cnref_original_call_pattern) + 0x05, 0x90); // for a clean patch
		pir.ConsoleNameRef_ENABLED = true;
		PIR_ConsolePrint("ConsoleRefName toggled on.");
		return true;
	}

	return false;
}

// toggle moving the workbench by modifying vtable lookup bit for workbench type
static bool Toggle_WorkbenchMove()
{
	if (WorkbenchSelection.ptr && WorkbenchSelection.addr) {

		UInt8 AllowSelect1F = 0xFF; // 0xFF sentinel to indicate an error if we can't read the memory

		// for workbench (containers) the type is 0x1A, so the calculation is 0x1F - 0x1A = 0x05 -- i.e. the fifth bit in the vtable
		if (!ReadMemory(uintptr_t(WorkbenchSelection.addr + 0x05), &AllowSelect1F, sizeof(UInt8))) {
			pirlog("Toggle failed trying to ReadMemory on the vtable bit.");
			return false;
		}

		// game default - disable selecting workbench
		if (AllowSelect1F == 0x00) {
			SafeWrite8((uintptr_t)WorkbenchSelection.addr + 0x05, 0x01);
			PIR_ConsolePrint("Workbench move disabled (game default).");
			return true;
		}

		// allows selecting and storing workbench
		if (AllowSelect1F == 0x01) {
			SafeWrite8((uintptr_t)WorkbenchSelection.addr + 0x05, 0x00);
			PIR_ConsolePrint("Workbench move allowed! Don't accidentally store it or your save will be corrupted! Turn this OFF when you're done!");
			return true;
		}

		pirlog("Toggle failed. The WorkbenchSelection vtable bit is an invalid value (not 1 or 0).");
		return false;
	}
	return false;
}

// Toggles Place in Red allowing placing objects in red, moving yellow objects, and disabling the out of bounds workshop timer
static bool Toggle_PlaceInRed()
{
	// 1. Safety Check: Guard against null/invalid pointers before any memory writes.
	if (!A || !B || !C || !D || !E ||
		!F || !G || !H || !J || !R ||
		!Y || !wstimer || !WSMode.addr)
	{
		PIR_ConsolePrint("Toggle failed! One or more of the required pointer addresses is null.");
		pirlog("Toggle failed! One or more of the required pointer addresses is null.");
		return false;
	}

	// 2. Helper lambdas (C-style cast + proper const handling for F4SE)
	auto write8 = [](auto baseAddr, std::uintptr_t offset, std::uint8_t val) {
		SafeWrite8((std::uintptr_t)baseAddr + offset, val);
		};

	auto writeBuf = [](auto baseAddr, const auto& buffer) {
		SafeWriteBuf((std::uintptr_t)baseAddr,
			const_cast<void*>(static_cast<const void*>(&buffer[0])),
			sizeof(buffer));
		};

	auto writeBufOffset = [](auto baseAddr, std::uintptr_t offset, const auto& buffer) {
		SafeWriteBuf((std::uintptr_t)baseAddr + offset,
			const_cast<void*>(static_cast<const void*>(&buffer[0])),
			sizeof(buffer));
		};

	// 3. Determine target state and apply patches
	if (pir.PLACEINRED_ENABLED)
	{
		// --- DISABLE Place in Red ---
		write8(A, 0x06, 0x01);
		write8(A, 0x0C, 0x02);
		write8(B, 0x01, 0x01);

		writeBuf(C, C_OLD);
		writeBufOffset(C, 0x11, CC_OLD);
		write8(C, 0x1D, 0x01);

		writeBuf(D, D_OLD);
		write8(E, 0x00, 0x76);
		writeBuf(F, F_OLD);
		write8(G, 0x01, 0x95);
		write8(H, 0x00, 0x74);

		writeBuf(J, J_OLD);
		write8(R, 0x0C, 0x01); // red
		writeBuf(Y, Y_OLD);
		writeBuf(wstimer, WSTIMER_OLD);

		pir.PLACEINRED_ENABLED = false;
		PIR_ConsolePrint("Place in Red disabled.");

		return true;
	}
	else
	{
		// --- ENABLE Place in Red ---
		write8(A, 0x06, 0x00);
		write8(A, 0x0C, 0x01);
		write8(B, 0x01, 0x00);

		writeBuf(C, C_NEW); // movzx eax,byte ptr [Fallout4.exe+2E74998]
		writeBufOffset(C, 0x11, CC_NEW);
		write8(C, 0x1D, 0x00);

		writeBuf(D, D_NEW);
		write8(E, 0x00, 0xEB);
		writeBuf(F, NOP6);
		write8(G, 0x01, 0x98);  // works but look at again later
		write8(H, 0x00, 0xEB);

		writeBuf(J, J_NEW); // water or other restrictions
		write8(R, 0x0C, 0x00);  // red to green
		writeBuf(Y, NOP3);  // move yellow
		writeBuf(wstimer, WSTIMER_NEW); // timer

		// set the correct bytes on enable
		writeBufOffset(WSMode.addr, 0x03, TWO_ZEROS); // 0000
		writeBufOffset(WSMode.addr, 0x09, TWO_ONES);  // 0101

		pir.PLACEINRED_ENABLED = true;
		PIR_ConsolePrint("Place In Red enabled.");

		return true;
	}

	// Fallback return to satisfy the compiler if neither block executes
	return false;
}

// play sound by filename. must be under data\sounds
//static void PIR_PlayFileSound(const char* wav)
//{
//	if (PlaySound_File_func) {
//		PlaySound_File_func(wav);
//	}
//}

// play sound using form name
static void PIR_PlayUISound(const char* sound)
{
	if (PlaySound_UI_func) {
		PlaySound_UI_func(sound);
	}
}

static constexpr char console_help_msg[] =
"PlaceInRed (pir) - Command Reference\n"
"==============================================================\n"
"[ Toggles ]\n"
"  pir toggle            Toggle Place in Red\n"
"  pir osnap             Toggle object snapping\n"
"  pir gsnap             Toggle ground snapping\n"
"  pir slow              Toggle slower rotate/zoom speed\n"
"  pir workshopsize      Toggle unlimited workshop build size\n"
"  pir outlines          Toggle object outlines\n"
"  pir achievements      Toggle achievements while using mods\n"
"[ Scaling ]\n"
"  pir scaleup<N>        Scale selected object up by N percent\n"
"  pir scaledown<N>      Scale selected object down by N percent\n"
"                       N = 1, 2, 5, 10, 25, 50, 75, 100\n"
"                       Example: pir scaleup10\n"
"[ Rotation ]\n"
"  pir x<N>              Rotate +N degrees on the X axis\n"
"  pir x-<N>             Rotate -N degrees on the X axis\n"
"  pir y<N>              Rotate +N degrees on the Y axis\n"
"  pir y-<N>             Rotate -N degrees on the Y axis\n"
"  pir z<N>              Rotate +N degrees on the Z axis\n"
"  pir z-<N>             Rotate -N degrees on the Z axis\n"
"                       N = 0.1, 0.5, 1, 2, 5, 10, 15, 30, 45\n"
"                       Example: pir z45\n"
"[ Custom Rotation ]\n"
"  pir xc                Rotate +custom degrees on the X axis\n"
"  pir x-c               Rotate -custom degrees on the X axis\n"
"  pir yc                Rotate +custom degrees on the Y axis\n"
"  pir y-c               Rotate -custom degrees on the Y axis\n"
"  pir zc                Rotate +custom degrees on the Z axis\n"
"  pir z-c               Rotate -custom degrees on the Z axis\n"
"                       Custom degrees are set in placeinred.ini\n"
"[ Object Physics ]\n"
"  pir lock              Lock object and disable physics\n"
"  pir lockq             Lock object silently, with no sound FX\n"
"  pir unlock            Unlock object and enable physics\n"
"[ Miscellaneous ]\n"
"  pir wb                Toggle allowing the workbench to be moved\n"
"  pir cnref             Show ref name in console when clicked\n"
"==============================================================\n";

// Called every time the console command runs
static bool ExecuteConsole(void* paramInfo, void* scriptData, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, double* result, void* opcodeOffsetPtr)
{
	if (!ParseConsoleArg_native || !ParseConsoleArg.ptr || (ParseConsoleArg.r32 == 0)) {
		pirlog("Failed to execute the console command!");
		return false;
	}

	std::array<char, 1024> param1{};
	std::array<char, 1024> param2{};
		
	bool consoleresult = ParseConsoleArg_native(
		paramInfo,
		scriptData,
		opcodeOffsetPtr,
		thisObj,
		containingObj,
		scriptObj,
		locals,
		param1.data(),
		param2.data()
	);

	// Force termination even if native writes garbage / no terminator
	param1.back() = '\0';
	param2.back() = '\0';

	if (consoleresult && param1[0] != '\0')
	{
		// Fast ASCII case-folding to ensure commands like "X10" and "x10" both work
		for (char& c : param1) {
			if (c == '\0') break;
			if (c >= 'A' && c <= 'Z') c |= 0x20;
		}

		switch (ConsoleSwitch(param1.data()))
		{
			// debug and tests
		case ConsoleSwitch("dumpcmds"):     DumpCmds(); break;
		case ConsoleSwitch("logref"):       LogWSRef(); break;
		case ConsoleSwitch("print"):        Toggle_ConsolePrint(); break;
		//case ConsoleSwitch("sound"):        if (param2[0]) PIR_PlayFileSound(param2.data()); break;
		case ConsoleSwitch("uisound"):      if (param2[0]) PIR_PlayUISound(param2.data()); break;



			// toggles
		case ConsoleSwitch("1"):
		case ConsoleSwitch("toggle"):       Toggle_PlaceInRed(); break;
		case ConsoleSwitch("2"):
		case ConsoleSwitch("osnap"):        Toggle_ObjectSnap(); break;
		case ConsoleSwitch("3"):
		case ConsoleSwitch("gsnap"):        Toggle_GroundSnap(); break;
		case ConsoleSwitch("4"):
		case ConsoleSwitch("slow"):         Toggle_SlowZoomAndRotate(); break;
		case ConsoleSwitch("5"):
		case ConsoleSwitch("workshopsize"): Toggle_WorkshopSize(); break;
		case ConsoleSwitch("6"):
		case ConsoleSwitch("outlines"):     Toggle_Outlines(); break;
		case ConsoleSwitch("7"):
		case ConsoleSwitch("achievements"): Toggle_Achievements(); break;
		case ConsoleSwitch("8"):
		case ConsoleSwitch("wb"):           Toggle_WorkbenchMove(); break;

		// scale constants
		case ConsoleSwitch("scale1"):       SetCurrentRefScale(1.0000f); break;
		case ConsoleSwitch("scale10"):      SetCurrentRefScale(9.9999f); break;

		// reset angle
		case ConsoleSwitch("ra"):           ResetCurrentWSRefRotation(); break;

		// modify x angle
		case ConsoleSwitch("xc"):    RotateCurrentWSRefByDegreesX(pir.fRotateDegreesCustomX); break;
		case ConsoleSwitch("x-c"):   RotateCurrentWSRefByDegreesX(-pir.fRotateDegreesCustomX); break;
		case ConsoleSwitch("x0.1"):  RotateCurrentWSRefByDegreesX(0.1f); break;
		case ConsoleSwitch("x0.5"):  RotateCurrentWSRefByDegreesX(0.5f); break;
		case ConsoleSwitch("x1"):    RotateCurrentWSRefByDegreesX(1.0f); break;
		case ConsoleSwitch("x2"):    RotateCurrentWSRefByDegreesX(2.0f); break;
		case ConsoleSwitch("x5"):    RotateCurrentWSRefByDegreesX(5.0f); break;
		case ConsoleSwitch("x10"):   RotateCurrentWSRefByDegreesX(10.0f); break;
		case ConsoleSwitch("x15"):   RotateCurrentWSRefByDegreesX(15.0f); break;
		case ConsoleSwitch("x30"):   RotateCurrentWSRefByDegreesX(30.0f); break;
		case ConsoleSwitch("x45"):   RotateCurrentWSRefByDegreesX(45.0f); break;
		case ConsoleSwitch("x-0.1"): RotateCurrentWSRefByDegreesX(-0.1f); break;
		case ConsoleSwitch("x-0.5"): RotateCurrentWSRefByDegreesX(-0.5f); break;
		case ConsoleSwitch("x-1"):   RotateCurrentWSRefByDegreesX(-1.0f); break;
		case ConsoleSwitch("x-5"):   RotateCurrentWSRefByDegreesX(-5.0f); break;
		case ConsoleSwitch("x-10"):  RotateCurrentWSRefByDegreesX(-10.0f); break;
		case ConsoleSwitch("x-15"):  RotateCurrentWSRefByDegreesX(-15.0f); break;
		case ConsoleSwitch("x-30"):  RotateCurrentWSRefByDegreesX(-30.0f); break;
		case ConsoleSwitch("x-45"):  RotateCurrentWSRefByDegreesX(-45.0f); break;

		// modify y angle
		case ConsoleSwitch("yc"):    RotateCurrentWSRefByDegreesY(pir.fRotateDegreesCustomY); break;
		case ConsoleSwitch("y-c"):   RotateCurrentWSRefByDegreesY(-pir.fRotateDegreesCustomY); break;
		case ConsoleSwitch("y0.1"):  RotateCurrentWSRefByDegreesY(0.1f); break;
		case ConsoleSwitch("y0.5"):  RotateCurrentWSRefByDegreesY(0.5f); break;
		case ConsoleSwitch("y1"):    RotateCurrentWSRefByDegreesY(1.0f); break;
		case ConsoleSwitch("y2"):    RotateCurrentWSRefByDegreesY(2.0f); break;
		case ConsoleSwitch("y5"):    RotateCurrentWSRefByDegreesY(5.0f); break;
		case ConsoleSwitch("y10"):   RotateCurrentWSRefByDegreesY(10.0f); break;
		case ConsoleSwitch("y15"):   RotateCurrentWSRefByDegreesY(15.0f); break;
		case ConsoleSwitch("y30"):   RotateCurrentWSRefByDegreesY(30.0f); break;
		case ConsoleSwitch("y45"):   RotateCurrentWSRefByDegreesY(45.0f); break;
		case ConsoleSwitch("y-0.1"): RotateCurrentWSRefByDegreesY(-0.1f); break;
		case ConsoleSwitch("y-0.5"): RotateCurrentWSRefByDegreesY(-0.5f); break;
		case ConsoleSwitch("y-1"):   RotateCurrentWSRefByDegreesY(-1.0f); break;
		case ConsoleSwitch("y-5"):   RotateCurrentWSRefByDegreesY(-5.0f); break;
		case ConsoleSwitch("y-10"):  RotateCurrentWSRefByDegreesY(-10.0f); break;
		case ConsoleSwitch("y-15"):  RotateCurrentWSRefByDegreesY(-15.0f); break;
		case ConsoleSwitch("y-30"):  RotateCurrentWSRefByDegreesY(-30.0f); break;
		case ConsoleSwitch("y-45"):  RotateCurrentWSRefByDegreesY(-45.0f); break;

		// modify z angle
		case ConsoleSwitch("zc"):    RotateCurrentWSRefByDegreesZ(pir.fRotateDegreesCustomZ); break;
		case ConsoleSwitch("z-c"):   RotateCurrentWSRefByDegreesZ(-pir.fRotateDegreesCustomZ); break;
		case ConsoleSwitch("z0.1"):  RotateCurrentWSRefByDegreesZ(0.1f); break;
		case ConsoleSwitch("z0.5"):  RotateCurrentWSRefByDegreesZ(0.5f); break;
		case ConsoleSwitch("z1"):    RotateCurrentWSRefByDegreesZ(1.0f); break;
		case ConsoleSwitch("z2"):    RotateCurrentWSRefByDegreesZ(2.0f); break;
		case ConsoleSwitch("z5"):    RotateCurrentWSRefByDegreesZ(5.0f); break;
		case ConsoleSwitch("z10"):   RotateCurrentWSRefByDegreesZ(10.0f); break;
		case ConsoleSwitch("z15"):   RotateCurrentWSRefByDegreesZ(15.0f); break;
		case ConsoleSwitch("z30"):   RotateCurrentWSRefByDegreesZ(30.0f); break;
		case ConsoleSwitch("z45"):   RotateCurrentWSRefByDegreesZ(45.0f); break;
		case ConsoleSwitch("z-0.1"): RotateCurrentWSRefByDegreesZ(-0.1f); break;
		case ConsoleSwitch("z-0.5"): RotateCurrentWSRefByDegreesZ(-0.5f); break;
		case ConsoleSwitch("z-1"):   RotateCurrentWSRefByDegreesZ(-1.0f); break;
		case ConsoleSwitch("z-5"):   RotateCurrentWSRefByDegreesZ(-5.0f); break;
		case ConsoleSwitch("z-10"):  RotateCurrentWSRefByDegreesZ(-10.0f); break;
		case ConsoleSwitch("z-15"):  RotateCurrentWSRefByDegreesZ(-15.0f); break;
		case ConsoleSwitch("z-30"):  RotateCurrentWSRefByDegreesZ(-30.0f); break;
		case ConsoleSwitch("z-45"):  RotateCurrentWSRefByDegreesZ(-45.0f); break;

		// scale up
		case ConsoleSwitch("scaleup1"):   ModCurrentRefScale(1.0100f); break;
		case ConsoleSwitch("scaleup2"):   ModCurrentRefScale(1.0200f); break;
		case ConsoleSwitch("scaleup5"):   ModCurrentRefScale(1.0500f); break;
		case ConsoleSwitch("scaleup10"):  ModCurrentRefScale(1.1000f); break;
		case ConsoleSwitch("scaleup25"):  ModCurrentRefScale(1.2500f); break;
		case ConsoleSwitch("scaleup50"):  ModCurrentRefScale(1.5000f); break;
		case ConsoleSwitch("scaleup100"): ModCurrentRefScale(2.0000f); break;

		// scale down
		case ConsoleSwitch("scaledown1"):  ModCurrentRefScale(0.9900f); break;
		case ConsoleSwitch("scaledown2"):  ModCurrentRefScale(0.9800f); break;
		case ConsoleSwitch("scaledown5"):  ModCurrentRefScale(0.9500f); break;
		case ConsoleSwitch("scaledown10"): ModCurrentRefScale(0.9000f); break;
		case ConsoleSwitch("scaledown25"): ModCurrentRefScale(0.7500f); break;
		case ConsoleSwitch("scaledown50"): ModCurrentRefScale(0.5000f); break;
		case ConsoleSwitch("scaledown75"): ModCurrentRefScale(0.2500f); break;

		// locks and unlocks
		case ConsoleSwitch("lock"):
		case ConsoleSwitch("l"):       LockUnlockWSRef(false, true); break;
		case ConsoleSwitch("lockq"):   LockUnlockWSRef(false, false); break;
		case ConsoleSwitch("unlock"):
		case ConsoleSwitch("u"):       LockUnlockWSRef(true, false); break;
		case ConsoleSwitch("lockunlock"):
		case ConsoleSwitch("lu"):      ToggleLockWSRef(1); break;

		// lockunlock no sound fx
		case ConsoleSwitch("lockunlockq"):
		case ConsoleSwitch("luq"):     ToggleLockWSRef(0); break;

		// console name ref toggle
		case ConsoleSwitch("cnref"):   Toggle_ConsoleNameRef(); break;

		// survival console toggle
		case ConsoleSwitch("sc"):      Toggle_SurvivalConsole(); break;

		// show help
		case ConsoleSwitch("?"):
		case ConsoleSwitch("help"):         PIR_ConsolePrint(console_help_msg); break;

		// default
		default: PIR_ConsolePrint(console_help_msg); break;
		}

		return true;
	}

	pirlog("Failed to execute the console command!");
	return false;
}


// Explicitly define our parameters instead of stealing them.
// F4SE ObScriptParam struct: { const char* typeStr, UInt32 typeID, UInt32 isOptional }
// typeID 0 = String.
static ObScriptParam s_place_in_red_console_params[2] = {
	{ "String", 0, 0 }, // first string param, required
	{ "String", 0, 1 }  // second string param, optional
};

// Attempt to create the console command by hijacking an existing one
static bool PatchConsole(const char* hijacked_cmd_fullname)
{
	pirlog("Attempting to hijack console command: %s", hijacked_cmd_fullname);

	if (FirstConsole.cmd == nullptr) {
		pirlog("Failed because FirstConsole.cmd is a nullptr.");
		return false;
	}

	ObScriptCommand* s_hijackedCommand = nullptr;

	// Scan the command table for the target to hijack
	for (ObScriptCommand* iter = FirstConsole.cmd;
		iter->opcode < (kObScript_NumConsoleCommands + kObScript_ConsoleOpBase);
		++iter)
	{
		if (iter->longName && !strcmp(iter->longName, hijacked_cmd_fullname))
		{
			s_hijackedCommand = iter;
			break;
		}
	}

	if (s_hijackedCommand)
	{
		// Clone the command structure
		ObScriptCommand cmd = *s_hijackedCommand;

		// Configure the new command
		cmd.longName = "placeinred";
		cmd.shortName = "pir";
		cmd.helpText = "pir (placeinred) - type pir help for example usage";
		cmd.needsParent = 0;
		cmd.numParams = 2;
		cmd.execute = ExecuteConsole;

		// Assign our safely defined parameters
		cmd.params = s_place_in_red_console_params;
		cmd.flags = 0;

		// Patch the command table
		SafeWriteBuf((uintptr_t)s_hijackedCommand, &cmd, sizeof(cmd));

		pirlog("Successfully hijacked '%s' -> 'pir' command", hijacked_cmd_fullname);
		return true;
	}

	pirlog("Failed to find command to hijack: %s", hijacked_cmd_fullname);
	return false;
}

static std::unordered_map<std::string, std::string> GetIniMapOld(const std::string& filepath)
{
	std::unordered_map<std::string, std::string> iniData;
	std::ifstream file(filepath);

	if (!file.is_open()) {
		return iniData;           // or throw / log
	}

	std::string line;

	// Trim helper (in-place, efficient)
	auto Trim = [](std::string& s) {
		s.erase(0, s.find_first_not_of(" \t\r\n"));
		if (!s.empty()) {
			s.erase(s.find_last_not_of(" \t\r\n") + 1);
		}
		};

	while (std::getline(file, line)) {
		Trim(line);

		// Skip empty, comments, sections
		if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') {
			continue;
		}

		// Remove inline comment (if any)
		size_t comment = line.find_first_of(";#");
		if (comment != std::string::npos) {
			line.erase(comment);
			Trim(line);               // clean up after erasing
		}

		size_t eq = line.find('=');
		if (eq != std::string::npos) {
			std::string key = line.substr(0, eq);
			std::string val = line.substr(eq + 1);

			Trim(key);
			Trim(val);

			if (!key.empty()) {
				iniData[std::move(key)] = std::move(val);
			}
		}
	}
	return iniData;
}

static void ReadINIOld()
{
	const char* str_hardcoded = "hardcoded (not found in INI)";
	const char* found_in_ini = "ini";

	pirlog("Reading PlaceInRed.ini");

	// Read disk ONCE into the map
	auto iniMap = GetIniMapOld(GetPluginINIPath());

	// Helper to get string from our map instead of disk
	auto GetValue = [&](const char* key) -> std::string {
		auto it = iniMap.find(key);
		return (it != iniMap.end()) ? it->second : "";
		};

	// 1. Lambda for standard bool toggles (Memory Patching)
	auto ApplyToggle = [&](const char* key, bool& currentFlag, auto toggleFunc, bool defaultEnabled)
		{
			std::string val = GetValue(key);
			bool wantEnabled = GetBoolFromINIString(val, defaultEnabled);

			if (wantEnabled != currentFlag)
			{
				toggleFunc();
				pirlog("%s=%d (ini, toggled)", key, wantEnabled);
			}
			else
			{
				pirlog("%s=%d (ini, unchanged)", key, wantEnabled);
			}
		};

	// 2. Lambda for float settings
	auto ApplyFloat = [&](const char* key, float& target, float minVal, float maxVal, float defVal)
		{
			std::string str = GetValue(key);
			if (!str.empty())
			{
				float f = FloatFromString(str, minVal, maxVal, 0.0f);
				target = (f > 0.0f) ? f : defVal;
			}
			pirlog("%s=%.4f (%s)", key, target, str.empty() ? str_hardcoded : found_in_ini);
		};

	// ------------------- Boolean Toggles -------------------
	ApplyToggle("PLACEINRED_ENABLED", pir.PLACEINRED_ENABLED, Toggle_PlaceInRed, false);
	ApplyToggle("OBJECTSNAP_ENABLED", pir.OBJECTSNAP_ENABLED, Toggle_ObjectSnap, true);
	ApplyToggle("GROUNDSNAP_ENABLED", pir.GROUNDSNAP_ENABLED, Toggle_GroundSnap, true);
	ApplyToggle("WORKSHOPSIZE_ENABLED", pir.WORKSHOPSIZE_ENABLED, Toggle_WorkshopSize, false);
	ApplyToggle("OUTLINES_ENABLED", pir.OUTLINES_ENABLED, Toggle_Outlines, true);
	ApplyToggle("ACHIEVEMENTS_ENABLED", pir.ACHIEVEMENTS_ENABLED, Toggle_Achievements, false);
	ApplyToggle("ConsoleNameRef_ENABLED", pir.ConsoleNameRef_ENABLED, Toggle_ConsoleNameRef, false);
	ApplyToggle("bAllowConsoleInSurvival", pir.bAllowConsoleInSurvival, Toggle_SurvivalConsole, false);

	// ------------------- Pure Variables --------------------
	// PrintConsoleMessages (Direct assignment, no toggle hook required)
	std::string printVal = GetValue("PrintConsoleMessages");
	pir.PrintConsoleMessages = GetBoolFromINIString(printVal, true);
	pirlog("PrintConsoleMessages=%d (%s)", pir.PrintConsoleMessages, printVal.empty() ? str_hardcoded : found_in_ini);

	// ------------------- Slow Mode -------------------------
	ApplyFloat("fSlowerROTATE", pir.fSlowerROTATE, 0.01f, 50.0f, 0.5000f);
	ApplyFloat("fSlowerZOOM", pir.fSlowerZOOM, 0.01f, 50.0f, 1.0000f);
	ApplyToggle("SLOW_ENABLED", pir.SLOW_ENABLED, Toggle_SlowZoomAndRotate, false);

	// ------------------- Custom Rotation -------------------
	ApplyFloat("fRotateDegreesCustomX", pir.fRotateDegreesCustomX, 0.001f, 360.0f, 3.6000f);
	ApplyFloat("fRotateDegreesCustomY", pir.fRotateDegreesCustomY, 0.001f, 360.0f, 3.6000f);
	ApplyFloat("fRotateDegreesCustomZ", pir.fRotateDegreesCustomZ, 0.001f, 360.0f, 3.6000f);

	pirlog("Finished parsing INI from memory.");
}

// 2. Your complete, compiling INI parser
static void ToLowerCase(std::string& str) {
	std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
}

static std::unordered_map<std::string, std::string> GetIniMap(const std::string& filepath)
{
	std::unordered_map<std::string, std::string> iniData;
	std::ifstream file(filepath);

	if (!file.is_open()) {
		pirlog("Warning: Could not open %s", filepath.c_str());
		return iniData;
	}

	std::string line;

	auto Trim = [](std::string& s) {
		s.erase(0, s.find_first_not_of(" \t\r\n"));
		if (!s.empty()) {
			s.erase(s.find_last_not_of(" \t\r\n") + 1);
		}
		};

	while (std::getline(file, line)) {
		Trim(line);

		if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') {
			continue;
		}

		size_t comment = line.find_first_of(";#");
		if (comment != std::string::npos) {
			line.erase(comment);
			Trim(line);
		}

		size_t eq = line.find('=');
		if (eq != std::string::npos) {
			std::string key = line.substr(0, eq);
			std::string val = line.substr(eq + 1);

			Trim(key);
			Trim(val);
			ToLowerCase(key); // Normalize key

			if (!key.empty()) {
				iniData[std::move(key)] = std::move(val);
			}
		}
	}
	return iniData;
}

static void ReadINI()
{
	pirlog("--- Reading PlaceInRed.ini ---");

	auto iniMap = GetIniMap(GetPluginINIPath());

	auto GetValue = [&](std::string key, std::string& outVal) -> bool {
		ToLowerCase(key);
		auto it = iniMap.find(key);
		if (it != iniMap.end()) {
			outVal = it->second;
			return true;
		}
		return false;
		};

	// 1. Lambda for standard bool toggles (Fixed function pointer return type)
	auto ApplyToggle = [&](const char* key, bool& currentFlag, bool (*toggleFunc)(), bool defaultEnabled)
		{
			std::string val;
			bool found = GetValue(key, val);
			bool wantEnabled = GetBoolFromINIString(val, defaultEnabled);

			// Changed fallback to (DEF) to maintain strict 5-character alignment 
			const char* source = found ? "(INI)" : "(DEF)";
			const char* status = "";

			if (wantEnabled != currentFlag) {
				toggleFunc();
				status = "[Toggled]";
			}
			else {
				status = "[Unchanged]";
			}

			// Surgical spacing: Key(24), Value(7), Source(5)
			pirlog(" %-24s= %-7s%-5s %s", key, wantEnabled ? "true" : "false", source, status);
		};

	// 2. Lambda for direct bool assignments (No hooks)
	auto ApplyBool = [&](const char* key, bool& target, bool defaultEnabled)
		{
			std::string val;
			bool found = GetValue(key, val);
			target = GetBoolFromINIString(val, defaultEnabled);
			const char* source = found ? "(INI)" : "(DEF)";

			pirlog(" %-24s= %-7s%-5s [Set]", key, target ? "true" : "false", source);
		};

	// 3. Lambda for float settings
	auto ApplyFloat = [&](const char* key, float& target, float minVal, float maxVal, float defVal)
		{
			std::string val;
			bool found = GetValue(key, val);
			const char* source = found ? "(INI)" : "(DEF)";

			if (found && !val.empty()) {
				target = FloatFromString(val, minVal, maxVal, defVal);
			}
			else {
				target = defVal;
			}

			pirlog(" %-24s= %-7.4f%-5s [Set]", key, target, source);
		};

	// ------------------- Boolean Toggles -------------------
	ApplyToggle("PLACEINRED_ENABLED", pir.PLACEINRED_ENABLED, Toggle_PlaceInRed, false);
	ApplyToggle("OBJECTSNAP_ENABLED", pir.OBJECTSNAP_ENABLED, Toggle_ObjectSnap, true);
	ApplyToggle("GROUNDSNAP_ENABLED", pir.GROUNDSNAP_ENABLED, Toggle_GroundSnap, true);
	ApplyToggle("WORKSHOPSIZE_ENABLED", pir.WORKSHOPSIZE_ENABLED, Toggle_WorkshopSize, false);
	ApplyToggle("OUTLINES_ENABLED", pir.OUTLINES_ENABLED, Toggle_Outlines, true);
	ApplyToggle("ACHIEVEMENTS_ENABLED", pir.ACHIEVEMENTS_ENABLED, Toggle_Achievements, false);
	ApplyToggle("ConsoleNameRef_ENABLED", pir.ConsoleNameRef_ENABLED, Toggle_ConsoleNameRef, false);
	ApplyToggle("bAllowConsoleInSurvival", pir.bAllowConsoleInSurvival, Toggle_SurvivalConsole, false);

	// ------------------- Pure Variables --------------------
	ApplyBool("PrintConsoleMessages", pir.PrintConsoleMessages, true);

	// ------------------- Slow Mode -------------------------
	ApplyFloat("fSlowerROTATE", pir.fSlowerROTATE, 0.01f, 50.0f, 0.5000f);
	ApplyFloat("fSlowerZOOM", pir.fSlowerZOOM, 0.01f, 50.0f, 1.0000f);
	ApplyToggle("SLOW_ENABLED", pir.SLOW_ENABLED, Toggle_SlowZoomAndRotate, false);

	// ------------------- Custom Rotation -------------------
	ApplyFloat("fRotateDegreesCustomX", pir.fRotateDegreesCustomX, 0.001f, 360.0f, 3.6000f);
	ApplyFloat("fRotateDegreesCustomY", pir.fRotateDegreesCustomY, 0.001f, 360.0f, 3.6000f);
	ApplyFloat("fRotateDegreesCustomZ", pir.fRotateDegreesCustomZ, 0.001f, 360.0f, 3.6000f);

	pirlog("--- Finished parsing INI ---");
}

//init f4se stuff and return false if anything fails
static bool InitF4SE(const F4SEInterface* f4se)
{
	// get a plugin handle
	pir.pluginHandle = f4se->GetPluginHandle();
	if (!pir.pluginHandle) {
		pirlog("Couldn't get a plugin handle!");
		return false;
	}
	pirlog("Got a plugin handle.");

	// set messaging interface
	pir.g_messaging = (F4SEMessagingInterface*)f4se->QueryInterface(kInterface_Messaging);
	if (!pir.g_messaging) {
		pirlog("Failed to set messaging interface!");
		return false;
	}

	pirlog("F4SE interfaces are set.");
	return true;

}

static void InitPIR()
{
	// launch async pattern searches
	std::vector<std::future<void>> vec_futures;

	pirlog("Multithreaded search for memory patterns.");
	vec_futures.emplace_back(FindPatternAsyncV2(SetMotionType.ptr, pat_SetMotionType));
	vec_futures.emplace_back(FindPatternAsyncV2(R, pat_pirR));
	vec_futures.emplace_back(FindPatternAsyncV2(Zoom.ptr, pat_Zoom));
	vec_futures.emplace_back(FindPatternAsyncV2(Rotate.ptr, pat_Rotate));
	vec_futures.emplace_back(FindPatternAsyncV2(CurrentWSRef.ptr, pat_CurrentWSRef));
	vec_futures.emplace_back(FindPatternAsyncV2(achievements, pat_achievements));
	vec_futures.emplace_back(FindPatternAsyncV2(survivalconsole, pat_survivalconsole));
	vec_futures.emplace_back(FindPatternAsyncV2(cnref_original_call_pattern, pat_cnref_original));
	vec_futures.emplace_back(FindPatternAsyncV2(cnref_GetRefName_pattern, pat_cnref_GetRefName));
	vec_futures.emplace_back(FindPatternAsyncV2(A, pat_A));
	vec_futures.emplace_back(FindPatternAsyncV2(B, pat_B));
	vec_futures.emplace_back(FindPatternAsyncV2(C, pat_C));
	vec_futures.emplace_back(FindPatternAsyncV2(D, pat_D));
	vec_futures.emplace_back(FindPatternAsyncV2(E, pat_E));
	vec_futures.emplace_back(FindPatternAsyncV2(F, pat_F));
	vec_futures.emplace_back(FindPatternAsyncV2(G, pat_G));
	vec_futures.emplace_back(FindPatternAsyncV2(H, pat_H));
	vec_futures.emplace_back(FindPatternAsyncV2(J, pat_J));
	vec_futures.emplace_back(FindPatternAsyncV2(CORRECT, pat_CORRECT));
	vec_futures.emplace_back(FindPatternAsyncV2(gsnap, pat_gsnap));
	vec_futures.emplace_back(FindPatternAsyncV2(osnap, pat_osnap));
	vec_futures.emplace_back(FindPatternAsyncV2(outlines, pat_outlines));
	vec_futures.emplace_back(FindPatternAsyncV2(wstimer, pat_wstimer));
	vec_futures.emplace_back(FindPatternAsyncV2(Y, pat_Y));
	vec_futures.emplace_back(FindPatternAsyncV2(FirstConsole.ptr, pat_FirstConsole));
	vec_futures.emplace_back(FindPatternAsyncV2(FirstObScript.ptr, pat_FirstObScript));
	vec_futures.emplace_back(FindPatternAsyncV2(ParseConsoleArg.ptr, pat_ParseConsoleArg));
	vec_futures.emplace_back(FindPatternAsyncV2(GetScale_pattern, pat_GetScale));
	vec_futures.emplace_back(FindPatternAsyncV2(SetScale_pattern, pat_SetScale));
	//vec_futures.emplace_back(FindPatternAsyncV2(PlaySound_File_pattern, pat_PlaySound_File));
	vec_futures.emplace_back(FindPatternAsyncV2(PlaySound_UI_pattern, pat_PlaySound_UI));
	vec_futures.emplace_back(FindPatternAsyncV2(WSMode.ptr, pat_WSMode));
	vec_futures.emplace_back(FindPatternAsyncV2(WSSize.ptr, pat_WSSize));
	vec_futures.emplace_back(FindPatternAsyncV2(TheFO4Console.ptr, pat_gConsole));
	vec_futures.emplace_back(FindPatternAsyncV2(WorkbenchSelection.ptr, pat_WorkbenchSelection));
	vec_futures.emplace_back(FindPatternAsyncV2(InvalidRefHandle.ptr, pat_InvalidRefHandle));

	// Wait for all async tasks to complete
	for (auto& future : vec_futures) {
		future.get();
	}

	// Safely store old bytes so disabling toggles restores the EXACT game memory (prevents update crashes)
	if (C) {
		ReadMemory((uintptr_t)C, &C_OLD, sizeof(C_OLD));
		ReadMemory((uintptr_t)C + 0x11, &CC_OLD, sizeof(CC_OLD));
	}
	if (D) { ReadMemory((uintptr_t)D, &D_OLD, sizeof(D_OLD)); }
	if (F) { ReadMemory((uintptr_t)F, &F_OLD, sizeof(F_OLD)); }
	if (J) { ReadMemory((uintptr_t)J, &J_OLD, sizeof(J_OLD)); }
	if (Y) { ReadMemory((uintptr_t)Y, &Y_OLD, sizeof(Y_OLD)); }
	if (wstimer) { ReadMemory((uintptr_t)wstimer, &WSTIMER_OLD, sizeof(WSTIMER_OLD)); }
	if (osnap) { ReadMemory((uintptr_t)osnap, &OSNAP_OLD, sizeof(OSNAP_OLD)); }
	if (achievements) { ReadMemory((uintptr_t)achievements, &ACHIEVE_OLD, sizeof(ACHIEVE_OLD)); }
	if (cnref_original_call_pattern) { ReadMemory((uintptr_t)cnref_original_call_pattern, &CNAMEREF_OLD, sizeof(CNAMEREF_OLD)); }


	// wssize
	if (WSSize.ptr) {
		ReadMemory((uintptr_t)WSSize.ptr + 0x00, &DRAWS_OLD, sizeof(DRAWS_OLD));
		ReadMemory((uintptr_t)WSSize.ptr + 0x0A, &TRIS_OLD, sizeof(TRIS_OLD));
		WSSize.r32 = GetRel32FromPattern((uintptr_t)WSSize.ptr, 0x02, 0x06, 0x00);
		WSSize.addr = WSSize.r32 ? (pir.FO4BaseAddr + (uintptr_t)WSSize.r32) : 0;
	}

	// zoom and rotate
	if (Zoom.ptr && Rotate.ptr) {
		Zoom.r32 = GetRel32FromPattern((uintptr_t)Zoom.ptr, 0x04, 0x08, 0x00);
		Rotate.r32 = GetRel32FromPattern((uintptr_t)Rotate.ptr, 0x04, 0x08, 0x00);

		Zoom.addr = Zoom.r32 ? (pir.FO4BaseAddr + (uintptr_t)Zoom.r32) : 0;
		Rotate.addr = Rotate.r32 ? (pir.FO4BaseAddr + (uintptr_t)Rotate.r32) : 0;

		if (Rotate.addr) ReadMemory(Rotate.addr, &pir.fOriginalROTATE, sizeof(Float32));
		if (Zoom.addr) ReadMemory(Zoom.addr, &pir.fOriginalZOOM, sizeof(Float32));
	}

	// consolenameref
	if (cnref_original_call_pattern && cnref_GetRefName_pattern) {
		cnref_GetRefName_r32 = GetRel32FromPattern((uintptr_t)cnref_GetRefName_pattern, 0x01, 0x05, 0x00);
		cnref_GetRefName_addr = cnref_GetRefName_r32 ? (pir.FO4BaseAddr + (uintptr_t)cnref_GetRefName_r32) : 0;
	}

	// wsmode
	if (WSMode.ptr) {
		WSMode.r32 = GetRel32FromPattern((uintptr_t)WSMode.ptr, 0x02, 0x07, 0x00);
		WSMode.addr = WSMode.r32 ? (pir.FO4BaseAddr + (uintptr_t)WSMode.r32) : 0;
	}

	// moveworkbench
	if (WorkbenchSelection.ptr) {
		ReadMemory((uintptr_t)WorkbenchSelection.ptr + 0x04, &WorkbenchSelection.r32, sizeof(uint32_t));
		WorkbenchSelection.addr = WorkbenchSelection.r32 ? (pir.FO4BaseAddr + (uintptr_t)WorkbenchSelection.r32) : 0;
	}

	// InvalidRefPointer
	if (InvalidRefHandle.ptr) {
		InvalidRefHandle.r32 = GetRel32FromPattern((uintptr_t)InvalidRefHandle.ptr, 0x02, 0x06, 0x00);

		if (InvalidRefHandle.r32 != 0) {
			InvalidRefHandle.addr = pir.FO4BaseAddr + (uintptr_t)InvalidRefHandle.r32;
		}
		else {
			InvalidRefHandle.addr = 0;
		}
	}

	// setscale
	if (SetScale_pattern) {
		SetScale_s32 = GetRel32FromPattern((uintptr_t)SetScale_pattern, 0x01, 0x05, 0x00);
		if (SetScale_s32) {
			SetScale_func = RelocAddr<_SetScale_Native>(SetScale_s32);
		}
	}

	// getscale
	if (GetScale_pattern) {
		GetScale_r32 = GetRel32FromPattern((uintptr_t)GetScale_pattern, 0x08, 0x0C, 0x00);
		if (GetScale_r32) {
			GetScale_func = RelocAddr<_GetScale_Native>(GetScale_r32);
		}
	}

	// g_console
	if (TheFO4Console.ptr) {
		TheFO4Console.r32 = GetRel32FromPattern((uintptr_t)TheFO4Console.ptr, 0x03, 0x07, 0x00);
		TheFO4Console.addr = TheFO4Console.r32 ? (pir.FO4BaseAddr + (uintptr_t)TheFO4Console.r32) : 0;
	}

	// CurrentWSRef
	if (CurrentWSRef.ptr) {
		CurrentWSRef.r32 = GetRel32FromPattern((uintptr_t)CurrentWSRef.ptr, 0x03, 0x07, 0x00);
		CurrentWSRef.addr = CurrentWSRef.r32 ? (pir.FO4BaseAddr + (uintptr_t)CurrentWSRef.r32) : 0;
	}

	// GetConsoleArg
	if (ParseConsoleArg.ptr) {
		ParseConsoleArg.r32 = uintptr_t(ParseConsoleArg.ptr) - pir.FO4BaseAddr;
		if (ParseConsoleArg.r32) {
			ParseConsoleArg_native = RelocAddr<_ParseConsoleArg_Native>(ParseConsoleArg.r32);
		}
	}

	// first console
	if (FirstConsole.ptr) {
		FirstConsole.r32 = GetRel32FromPattern((uintptr_t)FirstConsole.ptr, 0x03, 0x07, -0x08);
		if (FirstConsole.r32) {
			FirstConsole.cmd = RelocPtr<ObScriptCommand>(FirstConsole.r32);
		}
	}

	// first obscript
	if (FirstObScript.ptr) {
		FirstObScript.r32 = GetRel32FromPattern((uintptr_t)FirstObScript.ptr, 0x03, 0x07, -0x08);
		if (FirstObScript.r32) {
			FirstObScript.cmd = RelocPtr<ObScriptCommand>(FirstObScript.r32);
		}
	}
	
	// setmotiontype
	if (SetMotionType.ptr) {
		SetMotionType.r32 = uintptr_t(SetMotionType.ptr) - pir.FO4BaseAddr;
		if (SetMotionType.r32) {
			SetMotionType_native = RelocAddr<_SetMotionType_Native>(SetMotionType.r32);
		}
	}
	
	// playuisound
	if (PlaySound_UI_pattern) {
		PlaySound_UI_r32 = uintptr_t(PlaySound_UI_pattern) - pir.FO4BaseAddr;
		if (PlaySound_UI_r32) {
			PlaySound_UI_func = RelocAddr<_PlayUISound_Native>(PlaySound_UI_r32);
		}
	}
	
	// playfilesound
	//if (PlaySound_File_pattern) {
	//	PlaySound_File_r32 = uintptr_t(PlaySound_File_pattern) - pir.FO4BaseAddr;
	//	if (PlaySound_File_r32) {
	//		PlaySound_File_func = RelocAddr<_PlayFileSound_Native>(PlaySound_File_r32);
	//	}
	//}
}


extern "C" {

	// F4SEPlugin_Load
	__declspec(dllexport) bool F4SEPlugin_Load(const F4SEInterface* f4se)
	{
		// start log
		pir.debuglog.OpenRelative(0x0005, pir.plugin_log_file);
		pirlog("Plugin loaded (v%i)", pluginVersion);
		
		// init f4se
		if (!InitF4SE(f4se))
		{
			return false;
		}

		// init place in red
		InitPIR();

		// did we find all the patterns?
		if (!FoundPatterns())
		{
			pirlog("plugin load failed! Couldn't find required patterns in memory!");
			LogPatterns();
			return false;
		}

		// must happen after pattern check passes
		if (pir.g_messaging->RegisterListener(pir.pluginHandle, "F4SE", MessageInterfaceHandler) == false) {
			pirlog("Failed to register message listener!");
			return false;
		}

		if (!PatchConsole("GameComment"))
		{
			pirlog("Failed to create console command! Plugin will run with defaults.");
		}

		// toggle defaults
		ReadINI();

		// plugin loaded
		pirlog("Plugin initialized!");
		LogPatterns();

		return true;
	}

	// F4SEPluginVersionData
	__declspec(dllexport) F4SEPluginVersionData F4SEPlugin_Version = {
		F4SEPluginVersionData::kVersion,
		pluginVersion,
		"PlaceInRed",
		"RandyConstan",
		0, // addressIndependence
		0, // structureIndependence
		{ RUNTIME_VERSION_1_11_221, 0 }, // compatibleVersions[16]
		0, // seVersionRequired
		0, // reservedNonBreaking
		0, // reservedBreaking
		{ 0 } // reserved[512] padding block
	};
}
