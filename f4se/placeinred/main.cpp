#include "main.h"


UInt32 pluginVersion = 14;
static PlaceInRed pir;

constexpr uintptr_t WS_MODE_OFFSET_IN_WORKSHOP = 0x0;   // UInt8: 1 = in workshop mode
constexpr uintptr_t WS_MODE_OFFSET_GRABBED = 0xB;   // UInt8: 1 = object grabbed
constexpr UInt8     WS_MODE_TRUE = 0x01;

static SimpleFinder FirstConsole;
static SimpleFinder FirstObScript;
static SimpleFinder WSMode;
static SimpleFinder WSSize;
static SimpleFinder WorkbenchSelection;
static SimpleFinder gConsole;
static SimpleFinder gDataHandler;
static SimpleFinder CurrentWSRef;
static SimpleFinder Zoom;
static SimpleFinder Rotate;
static SimpleFinder SetMotionType;
static SimpleFinder ParseConsoleArg;

// Construct Utility::pattern synchronously so the async task captures an owned copy
// and avoids undefined behavior from dangling pattern storage.
// Asynchronously find a single pattern match.
// If the pattern is not found or the scan faults, ptr_address is set to 0.
template <typename T, size_t N>
std::future<void> FindPatternAsync(T& ptr_address, const char(&pattern)[N])
{
	Utility::pattern pat(pattern); // owned copy, lifetime-safe

	return std::async(std::launch::async,
		[&ptr_address, pat]() mutable
		{
			// Default: not found
			ptr_address = 0;

			// The scan itself can fault (end-of-code reads, SSE loads),
			// so guard only the scan step.
			__try
			{
				pat.count(1);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return;
			}

			// Safe: returns nullptr if no matches
			auto match = pat.get(0).get<uintptr_t>();
			if (match)
				ptr_address = (T)match;
		}
	);
}

// return the ini path as a std string
static const std::string& GetPluginINIPath()
{
	static std::string s_configPath;

	if (s_configPath.empty())
	{
		std::string	runtimePath = GetRuntimeDirectory();
		if (!runtimePath.empty())
		{
			s_configPath = runtimePath + pir.plugin_ini_path;
		}
	}
	return s_configPath;
}

// get a plugin ini setting as a string value
static std::string GetPluginINISettingAsString(const char* section, const char* key, const char* defaultValue = "")
{
	const std::string& iniPath = GetPluginINIPath();
	if (iniPath.empty()) return defaultValue;

	char buffer[256] = {};
	GetPrivateProfileString(section, key, defaultValue, buffer, sizeof(buffer), iniPath.c_str());
	return buffer;
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
	if (theFloat > min && theFloat < max) {
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

// get a rel32 from a pattern match
static SInt32 GetRel32FromPattern(uintptr_t instr, UInt64 start, UInt64 end, UInt64 shift = 0)
{
	SInt32 rel = 0;
	if (!ReadMemory(instr + start, &rel, sizeof(rel)))
		return 0;

	return (SInt32)(((instr + end) + rel - pir.FO4BaseAddr) + shift);
}

// read the address at an address+offset
static uintptr_t GetSinglePointer(uintptr_t address, UInt32 offset)
{
	uintptr_t result = 0;
	if (ReadMemory(address + offset, &result, sizeof(uintptr_t))) {
		return result;
	}
	else {
		return 0;
	}
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

//did we find all the required memory patterns?
static bool FoundPatterns()
{
	if (
		ParseConsoleArg.ptr && FirstConsole.ptr && FirstObScript.ptr
		&& pir.SetScale_pattern && pir.GetScale_pattern && CurrentWSRef.ptr
		&& WSMode.ptr && gConsole.ptr && pir.A && pir.B && pir.C
		&& pir.D && pir.E && pir.F && pir.G && pir.H
		&& pir.J && pir.Y && pir.R && pir.CORRECT
		&& pir.wstimer && pir.gsnap && pir.osnap && pir.outlines
		&& WSSize.ptr && Zoom.ptr && Rotate.ptr && SetMotionType.ptr && WorkbenchSelection.ptr
		)
	{
		/*
		 allow plugin to load even if these arent found:

		 placeinred.achievements - not a showstopper
		 ConsoleRefCallFinder - copy of another mod never required
		 GDataHandlerFinder - not using yet

		*/

		return true;
	}
	else {
		pirlog("Couldnt find required memory patterns! Check for conflicting mods.");
		return false;
	}
}

//static uintptr_t FindFunctionStart(uintptr_t startAddress, int nTheNumberOfCC = 3, size_t maxScanDistance = 0x10000) {
//	if (startAddress == 0 || nTheNumberOfCC <= 0) return 0;
//
//	uint8_t* currentPtr = reinterpret_cast<uint8_t*>(startAddress);
//	int consecutiveCC = 0;
//
//	// Scan backwards
//	for (size_t i = 0; i < maxScanDistance; ++i) {
//		// Decrement pointer first to check the byte before the current instruction
//		currentPtr--;
//
//		// Check if the current byte is an INT 3 (0xCC)
//		if (*currentPtr == 0xCC) {
//			consecutiveCC++;
//		}
//		else {
//			// Reset counter if we hit a non-CC byte
//			consecutiveCC = 0;
//		}
//
//		// If we found our N consecutive CCs
//		if (consecutiveCC == nTheNumberOfCC) {
//			// The function start is the address immediately after the CC block
//			// Result = Address of last CC + 1
//			return reinterpret_cast<uintptr_t>(currentPtr + nTheNumberOfCC);
//		}
//	}
//
//	return 0; // Return 0 if the pattern wasn't found within the safety distance
//}
//
//static void LogPatternsWithNearbyFunctionStart()
//{
//	// Constant for the number of CC interrupts to look for
//	const int N = 3;
//
//	// Helper to find and format the address to avoid code bloat
//	auto GetStart = [&](uintptr_t addr) -> uintptr_t {
//		return (addr == 0) ? 0 : FindFunctionStart(addr, N);
//		};
//
//	pir.debuglog.FormattedMessage("--------------------------------------------------------------------------------");
//	pir.debuglog.FormattedMessage("Base          :%p|Fallout4.exe+0x00000000", (uintptr_t)pir.FO4BaseAddr);
//
//	// Format: [Instruction Addr] | [Func Start Addr] | Fallout4.exe+Offset
//	pir.debuglog.FormattedMessage("achievements  :%p|%p|Fallout4.exe+0x%08X", pir.achievements, GetStart((uintptr_t)pir.achievements), (uintptr_t)pir.achievements - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("A             :%p|%p|Fallout4.exe+0x%08X", pir.A, GetStart((uintptr_t)pir.A), (uintptr_t)pir.A - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("B             :%p|%p|Fallout4.exe+0x%08X", pir.B, GetStart((uintptr_t)pir.B), (uintptr_t)pir.B - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("C             :%p|%p|Fallout4.exe+0x%08X (OLD: %02X%02X%02X%02X%02X%02X%02X)", pir.C, GetStart((uintptr_t)pir.C), (uintptr_t)pir.C - pir.FO4BaseAddr, pir.C_OLD[0], pir.C_OLD[1], pir.C_OLD[2], pir.C_OLD[3], pir.C_OLD[4], pir.C_OLD[5], pir.C_OLD[6]);
//	pir.debuglog.FormattedMessage("D             :%p|%p|Fallout4.exe+0x%08X (OLD: %02X%02X%02X%02X%02X%02X%02X)", pir.D, GetStart((uintptr_t)pir.D), (uintptr_t)pir.D - pir.FO4BaseAddr, pir.D_OLD[0], pir.D_OLD[1], pir.D_OLD[2], pir.D_OLD[3], pir.D_OLD[4], pir.D_OLD[5], pir.D_OLD[6]);
//	pir.debuglog.FormattedMessage("E             :%p|%p|Fallout4.exe+0x%08X", pir.E, GetStart((uintptr_t)pir.E), (uintptr_t)pir.E - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("F             :%p|%p|Fallout4.exe+0x%08X", pir.F, GetStart((uintptr_t)pir.F), (uintptr_t)pir.F - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("G             :%p|%p|Fallout4.exe+0x%08X", pir.G, GetStart((uintptr_t)pir.G), (uintptr_t)pir.G - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("H             :%p|%p|Fallout4.exe+0x%08X", pir.H, GetStart((uintptr_t)pir.H), (uintptr_t)pir.H - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("J             :%p|%p|Fallout4.exe+0x%08X", pir.J, GetStart((uintptr_t)pir.J), (uintptr_t)pir.J - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("Y             :%p|%p|Fallout4.exe+0x%08X", pir.Y, GetStart((uintptr_t)pir.Y), (uintptr_t)pir.Y - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("R             :%p|%p|Fallout4.exe+0x%08X", pir.R, GetStart((uintptr_t)pir.R), (uintptr_t)pir.R - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("RC            :%p|%p|Fallout4.exe+0x%08X", pir.RC, GetStart((uintptr_t)pir.RC), (uintptr_t)pir.RC - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("CORRECT       :%p|%p|Fallout4.exe+0x%08X", pir.CORRECT, GetStart((uintptr_t)pir.CORRECT), (uintptr_t)pir.CORRECT - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("CurrentWSRef  :%p|%p|Fallout4.exe+0x%08X", CurrentWSRef.ptr, GetStart((uintptr_t)CurrentWSRef.ptr), CurrentWSRef.r32);
//	pir.debuglog.FormattedMessage("FirstConsole  :%p|%p|Fallout4.exe+0x%08X", FirstConsole.ptr, GetStart((uintptr_t)FirstConsole.ptr), FirstConsole.r32);
//	pir.debuglog.FormattedMessage("FirstObScript :%p|%p|Fallout4.exe+0x%08X", FirstObScript.ptr, GetStart((uintptr_t)FirstObScript.ptr), FirstObScript.r32);
//	pir.debuglog.FormattedMessage("GetConsoleArg :%p|%p|Fallout4.exe+0x%08X", ParseConsoleArg.ptr, GetStart((uintptr_t)ParseConsoleArg.ptr), ParseConsoleArg.r32);
//	pir.debuglog.FormattedMessage("GetScale      :%p|%p|Fallout4.exe+0x%08X", pir.GetScale_pattern, GetStart((uintptr_t)pir.GetScale_pattern), pir.GetScale_r32);
//	pir.debuglog.FormattedMessage("GConsole      :%p|%p|Fallout4.exe+0x%08X", gConsole.ptr, GetStart((uintptr_t)gConsole.ptr), gConsole.r32);
//	pir.debuglog.FormattedMessage("gsnap         :%p|%p|Fallout4.exe+0x%08X", pir.gsnap, GetStart((uintptr_t)pir.gsnap), (uintptr_t)pir.gsnap - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("osnap         :%p|%p|Fallout4.exe+0x%08X", pir.osnap, GetStart((uintptr_t)pir.osnap), (uintptr_t)pir.osnap - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("outlines      :%p|%p|Fallout4.exe+0x%08X", pir.outlines, GetStart((uintptr_t)pir.outlines), (uintptr_t)pir.outlines - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("SetScale      :%p|%p|Fallout4.exe+0x%08X", pir.SetScale_pattern, GetStart((uintptr_t)pir.SetScale_pattern), pir.SetScale_s32);
//	pir.debuglog.FormattedMessage("PlayFileSound :%p|%p|Fallout4.exe+0x%08X", pir.PlaySound_File_pattern, GetStart((uintptr_t)pir.PlaySound_File_pattern), pir.PlaySound_File_r32);
//	pir.debuglog.FormattedMessage("PlayUISound   :%p|%p|Fallout4.exe+0x%08X", pir.PlaySound_UI_pattern, GetStart((uintptr_t)pir.PlaySound_UI_pattern), pir.PlaySound_UI_r32);
//	pir.debuglog.FormattedMessage("SetMotionType :%p|%p|Fallout4.exe+0x%08X", SetMotionType.ptr, GetStart((uintptr_t)SetMotionType.ptr), SetMotionType.r32);
//	pir.debuglog.FormattedMessage("WBSelect      :%p|%p|Fallout4.exe+0x%08X", WorkbenchSelection.ptr, GetStart((uintptr_t)WorkbenchSelection.ptr), WorkbenchSelection.r32);
//	pir.debuglog.FormattedMessage("WSSizeFinder  :%p|%p|Fallout4.exe+0x%08X", WSSize.ptr, GetStart((uintptr_t)WSSize.ptr), (uintptr_t)WSSize.ptr - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("WSSizeFloats  :%p|%p|Fallout4.exe+0x%08X", WSSize.addr, GetStart((uintptr_t)WSSize.addr), WSSize.addr - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("bWSMode       :%p|%p|Fallout4.exe+0x%08X", WSMode.ptr, GetStart((uintptr_t)WSMode.ptr), WSMode.r32);
//	pir.debuglog.FormattedMessage("WSTimer       :%p|%p|Fallout4.exe+0x%08X", pir.wstimer, GetStart((uintptr_t)pir.wstimer), (uintptr_t)pir.wstimer - pir.FO4BaseAddr);
//	pir.debuglog.FormattedMessage("Rotate        :%p|%p|%p|orig %f|slow %f", Rotate.ptr, Rotate.addr, GetStart((uintptr_t)Rotate.ptr), pir.fOriginalROTATE, pir.fSlowerROTATE);
//	pir.debuglog.FormattedMessage("Zoom          :%p|%p|%p|orig %f|slow %f", Zoom.ptr, Zoom.addr, GetStart((uintptr_t)Zoom.ptr), pir.fOriginalZOOM, pir.fSlowerZOOM);
//	pir.debuglog.FormattedMessage("--------------------------------------------------------------------------------");
//}

// log all the memory patterns to the log file
static void LogPatterns()
{
	pir.debuglog.FormattedMessage("--------------------------------------------------------------------------------");
	pir.debuglog.FormattedMessage("Base          :%p|Fallout4.exe+0x00000000", (uintptr_t)pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("achievements  :%p|Fallout4.exe+0x%08X", pir.achievements, (uintptr_t)pir.achievements - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("A             :%p|Fallout4.exe+0x%08X", pir.A, (uintptr_t)pir.A - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("B             :%p|Fallout4.exe+0x%08X", pir.B, (uintptr_t)pir.B - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("C             :%p|Fallout4.exe+0x%08X (OLD: %02X%02X%02X%02X%02X%02X%02X)", pir.C, (uintptr_t)pir.C - pir.FO4BaseAddr, pir.C_OLD[0], pir.C_OLD[1], pir.C_OLD[2], pir.C_OLD[3], pir.C_OLD[4], pir.C_OLD[5], pir.C_OLD[6]);
	pir.debuglog.FormattedMessage("D             :%p|Fallout4.exe+0x%08X (OLD: %02X%02X%02X%02X%02X%02X%02X)", pir.D, (uintptr_t)pir.D - pir.FO4BaseAddr, pir.D_OLD[0], pir.D_OLD[1], pir.D_OLD[2], pir.D_OLD[3], pir.D_OLD[4], pir.D_OLD[5], pir.D_OLD[6]);
	pir.debuglog.FormattedMessage("E             :%p|Fallout4.exe+0x%08X", pir.E, (uintptr_t)pir.E - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("F             :%p|Fallout4.exe+0x%08X", pir.F, (uintptr_t)pir.F - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("G             :%p|Fallout4.exe+0x%08X", pir.G, (uintptr_t)pir.G - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("H             :%p|Fallout4.exe+0x%08X", pir.H, (uintptr_t)pir.H - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("J             :%p|Fallout4.exe+0x%08X", pir.J, (uintptr_t)pir.J - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("Y             :%p|Fallout4.exe+0x%08X", pir.Y, (uintptr_t)pir.Y - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("R             :%p|Fallout4.exe+0x%08X", pir.R, (uintptr_t)pir.R - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("RC            :%p|Fallout4.exe+0x%08X", pir.RC, (uintptr_t)pir.RC - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("CORRECT       :%p|Fallout4.exe+0x%08X", pir.CORRECT, (uintptr_t)pir.CORRECT - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("CurrentWSRef  :%p|Fallout4.exe+0x%08X", CurrentWSRef.ptr, CurrentWSRef.r32);
	pir.debuglog.FormattedMessage("FirstConsole  :%p|Fallout4.exe+0x%08X", FirstConsole.ptr, FirstConsole.r32);
	pir.debuglog.FormattedMessage("FirstObScript :%p|Fallout4.exe+0x%08X", FirstObScript.ptr, FirstObScript.r32);
	pir.debuglog.FormattedMessage("GetConsoleArg :%p|Fallout4.exe+0x%08X", ParseConsoleArg.ptr, ParseConsoleArg.r32);
	pir.debuglog.FormattedMessage("GetScale      :%p|Fallout4.exe+0x%08X", pir.GetScale_pattern, pir.GetScale_r32);
	pir.debuglog.FormattedMessage("GConsole      :%p|Fallout4.exe+0x%08X", gConsole.ptr, gConsole.r32);
	pir.debuglog.FormattedMessage("gsnap         :%p|Fallout4.exe+0x%08X", pir.gsnap, (uintptr_t)pir.gsnap - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("osnap         :%p|Fallout4.exe+0x%08X", pir.osnap, (uintptr_t)pir.osnap - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("outlines      :%p|Fallout4.exe+0x%08X", pir.outlines, (uintptr_t)pir.outlines - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("PlayFileSound :%p|Fallout4.exe+0x%08X", pir.PlaySound_File_pattern, pir.PlaySound_File_r32);
	pir.debuglog.FormattedMessage("PlayUISound   :%p|Fallout4.exe+0x%08X", pir.PlaySound_UI_pattern, pir.PlaySound_UI_r32);
	pir.debuglog.FormattedMessage("SetMotionType :%p|Fallout4.exe+0x%08X", SetMotionType.ptr, SetMotionType.r32);
	pir.debuglog.FormattedMessage("SetScale      :%p|Fallout4.exe+0x%08X", pir.SetScale_pattern, pir.SetScale_s32);
	pir.debuglog.FormattedMessage("WBSelect      :%p|Fallout4.exe+0x%08X", WorkbenchSelection.ptr, WorkbenchSelection.r32);
	pir.debuglog.FormattedMessage("bWSMode       :%p|Fallout4.exe+0x%08X", WSMode.ptr, WSMode.r32);
	pir.debuglog.FormattedMessage("WSTimer       :%p|Fallout4.exe+0x%08X", pir.wstimer, (uintptr_t)pir.wstimer - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("WSSizeFloats  :%p|Fallout4.exe+0x%08X", WSSize.addr, WSSize.addr - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("WSSizeFinder  :%p|Fallout4.exe+0x%08X", WSSize.ptr, (uintptr_t)WSSize.ptr - pir.FO4BaseAddr);
	pir.debuglog.FormattedMessage("Rotate        :%p|%p|orig %f|slow %f", Rotate.ptr, Rotate.addr, pir.fOriginalROTATE, pir.fSlowerROTATE);
	pir.debuglog.FormattedMessage("Zoom          :%p|%p|orig %f|slow %f", Zoom.ptr, Zoom.addr, pir.fOriginalZOOM, pir.fSlowerZOOM);
	pir.debuglog.FormattedMessage("--------------------------------------------------------------------------------");
}


// Read a UInt8 flag from WSMode safely
static bool ReadWSModeFlag(uintptr_t offset)
{
	if (!WSMode.ptr)
		return false;

	UInt8 value = 0;
	if (!ReadMemory(uintptr_t(WSMode.addr) + offset, &value, sizeof(value)))
		return false;

	return value == WS_MODE_TRUE;
}

// Determine if player is in workshop mode
static bool IsPlayerInWorkshopMode()
{
	return ReadWSModeFlag(WS_MODE_OFFSET_IN_WORKSHOP);
}

// Is the player grabbing the current workshop ref
static bool IsCurrentWSRefGrabbed()
{
	return ReadWSModeFlag(WS_MODE_OFFSET_GRABBED);
}

// return the currently selected workshop ref with some safety checks
static TESObjectREFR* GetCurrentWSRef(bool bOnlySelectReferences=1)
{
	if (CurrentWSRef.ptr && CurrentWSRef.addr && IsPlayerInWorkshopMode()) {

		uintptr_t refaddr = GimmeMultiPointer(CurrentWSRef.addr, pir.CurrentWSRef_Offsets, pir.CurrentWSRef_OffsetsSize);
		TESObjectREFR* ref = (TESObjectREFR*)refaddr;

		if (ref)
		{
			if (!ref->formID) { return nullptr; }
			if (ref->formID <= 0) { return nullptr; }

			//optional but checks by default
			if (bOnlySelectReferences) {
				if (ref->formType != 0x40) {
					return nullptr;
				}
			}
			return ref;
		}
	}
	return nullptr;
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

// lock the current WS ref in place by changing the motion type to keyframed
static void LockUnlockWSRef(bool unlock = 0, bool sound = 0)
{
	VirtualMachine* vm = (*g_gameVM)->m_virtualMachine;
	TESObjectREFR* ref = GetCurrentWSRef();
	UInt32 motion = 00000002; //Motion_Keyframed aka locked in place
	bool acti = false; //akAllowActivate

	if (unlock == 1) {
		motion = 00000001; //Motion_Dynamic unlock and release to havok
	}

	if (vm && ref) {
		SetMotionType_native(vm, NULL, ref, motion, acti);
		if (sound == 1) {
			pir.PlaySound_UI_func(pir.sLockObjectSound);
		}
	}
}

// To switch with strings
static constexpr unsigned int ConsoleSwitch(const char* s, int off = 0)
{
	return !s[off] ? 5381 : (ConsoleSwitch(s, off + 1) * 33) ^ s[off];
}

// print to console (copied from f4se + modified to use pattern)
static void PIR_ConsolePrint(const char* fmt, ...)
{
	if (gConsole.ptr && gConsole.addr && pir.PrintConsoleMessages)
	{
		ConsoleManager* mgr = (ConsoleManager*)gConsole.addr;
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
		pir.SetScale_func(ref, newScale);
		return true;
	}
	return false;
}

//Move reference to itself
static void MoveRefToSelf(int repeat = 0)
{
	TESObjectREFR* ref = GetCurrentWSRef();
	if (ref) {
		UInt32 nullHandle = *g_invalidRefHandle;
		TESObjectCELL* parentCell = ref->parentCell;
		TESWorldSpace* worldspace = CALL_MEMBER_FN(ref, GetWorldspace)();

		// new position
		NiPoint3 newPos;
		newPos.x = ref->pos.x;
		newPos.y = ref->pos.y;
		newPos.z = ref->pos.z;
		// new rotation
		NiPoint3 newRot;
		newRot.x = ref->rot.x;
		newRot.y = ref->rot.y;
		newRot.z = ref->rot.z;

		for (int i = 0; i <= repeat; i++)
		{
			MoveRefrToPosition(ref, &nullHandle, parentCell, worldspace, &ref->pos, &ref->rot);
		}

	}
}

// Modify the scale of the current workshop reference by a percent.
static bool ModCurrentRefScale(float fMultiplyAmount)
{
	TESObjectREFR* ref = GetCurrentWSRef();
	if (ref) {
		float oldscale = pir.GetScale_func(ref);
		float newScale = oldscale * (fMultiplyAmount);
		if (newScale > 9.9999f) { newScale = 9.9999f; }
		if (newScale < 0.0001f) { newScale = 0.0001f; }
		pir.SetScale_func(ref, newScale);

		// fix jitter only if player isnt grabbing the item
		if (IsCurrentWSRefGrabbed() == false) {
			MoveRefToSelf(1);
		}

		return true;
	}
	return false;
}

static void ResetCurrentWSRefRotation()
{
	TESObjectREFR* ref = GetCurrentWSRef();
	if (!ref) return;

	NiPoint3 resetRot = ref->rot;
	resetRot.x = 0;
	resetRot.y = 0;
	resetRot.z = 0;

	// 2. Safely resolve cell and worldspace pointers
	TESObjectCELL* cell = ref->parentCell;
	TESWorldSpace* worldSpace = (cell) ? cell->worldSpace : nullptr;

	// 3. Apply the transformation
	// We pass the existing position and the modified rotation
	MoveRefrToPosition(
		ref,
		nullptr, // targetHandle
		cell,
		worldSpace,
		&ref->pos,
		&resetRot
	);

}


// Helper to wrap angles into the [-PI, PI] range
inline float NormalizeAngle(float angle) {
	// fmodf is faster than while loops for large degree offsets
	float wrapped = fmodf(angle + (float)MATH_PI, (float)(MATH_PI * 2.0f));
	if (wrapped < 0) wrapped += (float)(MATH_PI * 2.0f);
	return wrapped - (float)MATH_PI;
}

// Rotate the current workshop reference by specified degree amounts on each axis
static void RotateCurrentWSRefByDegrees(float dXDeg, float dYDeg, float dZDeg)
{
	TESObjectREFR* ref = GetCurrentWSRef();
	if (!ref) return;

	// 1. Convert degrees to radians and apply increments
	constexpr float DegToRad = (float)MATH_PI / 180.0f;

	NiPoint3 newRot = ref->rot;
	newRot.x = NormalizeAngle(newRot.x + (dXDeg * DegToRad)); // Pitch
	newRot.y = NormalizeAngle(newRot.y + (dYDeg * DegToRad)); // Roll
	newRot.z = NormalizeAngle(newRot.z + (dZDeg * DegToRad)); // Yaw

	// 2. Safely resolve cell and worldspace pointers
	TESObjectCELL* cell = ref->parentCell;
	TESWorldSpace* worldSpace = (cell) ? cell->worldSpace : nullptr;

	// 3. Apply the transformation
	// We pass the existing position and the modified rotation
	MoveRefrToPosition(
		ref,
		nullptr, // targetHandle
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
	if (pir.outlines && pir.OUTLINES_ENABLED) {
		SafeWrite8((uintptr_t)pir.outlines + 0x06, 0x00); //objects
		SafeWrite8((uintptr_t)pir.outlines + 0x0D, 0xEB); //npcs
		pir.OUTLINES_ENABLED = false;
		PIR_ConsolePrint("Object outlines disabled");
		return true;
	}
	if (pir.outlines && !pir.OUTLINES_ENABLED) {
		SafeWrite8((uintptr_t)pir.outlines + 0x06, 0x01); //objects
		SafeWrite8((uintptr_t)pir.outlines + 0x0D, 0x76); //npcs
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
		SafeWriteBuf((uintptr_t)WSSize.ptr, pir.DRAWS_OLD, sizeof(pir.DRAWS_OLD));
		SafeWriteBuf((uintptr_t)WSSize.ptr + 0x0A, pir.TRIS_OLD, sizeof(pir.TRIS_OLD));
		pir.WORKSHOPSIZE_ENABLED = false;
		PIR_ConsolePrint("Unlimited workshop size disabled");
		return true;
	}

	if (WSSize.ptr && pir.WORKSHOPSIZE_ENABLED == false) {
		// Write nop 6 so its never increased
		SafeWriteBuf((uintptr_t)WSSize.ptr, pir.NOP6, sizeof(pir.NOP6));
		SafeWriteBuf((uintptr_t)WSSize.ptr + 0x0A, pir.NOP6, sizeof(pir.NOP6));

		// set current ws draws and triangles to zero
		SafeWrite64(WSSize.addr, 0);
		pir.WORKSHOPSIZE_ENABLED = true;
		PIR_ConsolePrint("Unlimited workshop size enabled");
		return true;
	}
	return false;
}

//toggle groundsnap
static bool Toggle_GroundSnap()
{
	if (pir.gsnap && pir.GROUNDSNAP_ENABLED) {

		SafeWrite8((uintptr_t)pir.gsnap + 0x01, 0x85);
		pir.GROUNDSNAP_ENABLED = false;
		PIR_ConsolePrint("Ground snap disabled");
		return true;
	}
	if (pir.gsnap && !pir.GROUNDSNAP_ENABLED) {
		SafeWrite8((uintptr_t)pir.gsnap + 0x01, 0x86);
		pir.GROUNDSNAP_ENABLED = true;
		return true;

	}
	return false;
}

//toggle objectsnap
static bool Toggle_ObjectSnap()
{
	// its on - toggle it off
	if (pir.osnap && pir.OBJECTSNAP_ENABLED) {
		SafeWriteBuf((uintptr_t)pir.osnap, pir.OSNAP_NEW, sizeof(pir.OSNAP_NEW));
		pir.OBJECTSNAP_ENABLED = false;
		PIR_ConsolePrint("Object snap disabled");
		return true;
	}
	// its off - toggle it on
	if (pir.osnap && !pir.OBJECTSNAP_ENABLED) {
		SafeWriteBuf((uintptr_t)pir.osnap, pir.OSNAP_OLD, sizeof(pir.OSNAP_OLD));
		pir.OBJECTSNAP_ENABLED = true;
		PIR_ConsolePrint("Object snap enabled");
		return true;
	}
	return false;
}

//toggle allowing achievements with mods
static bool Toggle_Achievements()
{
	// its on - toggle it off
	if (pir.achievements && pir.ACHIEVEMENTS_ENABLED) {
		SafeWriteBuf((uintptr_t)pir.achievements, pir.ACHIEVE_OLD, sizeof(pir.ACHIEVE_OLD));
		pir.ACHIEVEMENTS_ENABLED = false;
		PIR_ConsolePrint("Achievements with mods disabled (game default)");
		return true;
	}
	// its off - toggle it on
	if (pir.achievements && !pir.ACHIEVEMENTS_ENABLED) {
		SafeWriteBuf((uintptr_t)pir.achievements, pir.ACHIEVE_NEW, sizeof(pir.ACHIEVE_NEW));
		pir.ACHIEVEMENTS_ENABLED = true;
		PIR_ConsolePrint("Achievements with mods enabled!");
		return true;
	}
	return false;
}

// toggle consolenameref
static bool Toggle_ConsoleNameRef()
{
	if (pir.cnref_GetRefName_pattern == false || pir.cnref_original_call_pattern == false) {
		return false;
	}

	// toggle off
	if (pir.ConsoleNameRef_ENABLED)
	{
		SafeWriteBuf(uintptr_t(pir.cnref_original_call_pattern), pir.CNameRef_OLD, pir.CNameRef_OLD_Size);
		pir.ConsoleNameRef_ENABLED = false;
		PIR_ConsolePrint("ConsoleRefName toggled off.");
		return true;
	}

	// toggle on
	if (!pir.ConsoleNameRef_ENABLED)
	{
		SafeWriteCall(uintptr_t(pir.cnref_original_call_pattern), pir.cnref_GetRefName_addr); //patch call
		SafeWrite8(uintptr_t(pir.cnref_original_call_pattern) + 0x05, 0x90); // for a clean patch
		pir.ConsoleNameRef_ENABLED = true;
		PIR_ConsolePrint("ConsoleRefName toggled on.");
		return true;
	}

	return false;
}

// toggle moving the workbench by modifying vtable lookup bit for workbench type
static bool ToggleWorkbenchMove()
{
	if (WorkbenchSelection.ptr && WorkbenchSelection.addr) {

		// game checks ref type and does vtable lookup at +5
		// Workbench is type 0x1F. (0x1F - 0x1A = 0x05)
		//  Fallout4.exe+30B86A - 0FB6 C0 - movzx eax,al
		//  Fallout4.exe + 30B86D - 83 C0 E6 - add eax, -1A { 230 }
		//  Fallout4.exe + 30B870 - 83 F8 75 - cmp eax, 75 { 117 }
		//  Fallout4.exe + 30B873 - 77 23 - ja Fallout4.exe + 30B898
		//  Fallout4.exe + 30B875 - 48 8D 15 8447CFFF - lea rdx, [Base] { (9460301) }
		//  Fallout4.exe + 30B87C - 0FB6 84 02 A8B83000 - movzx eax, byte ptr[rdx + rax + 0030B8A8]
		//  Fallout4.exe + 30B884 - 8B 8C 82 A0B83000 - mov ecx, [rdx + rax * 4 + 0030B8A0]
		//  Fallout4.exe + 30B88E - FF E1 - jmp rcx

		UInt8 AllowSelect1F = 0x00; //00 in the vtable means we can select it
		ReadMemory(uintptr_t(WorkbenchSelection.addr + 0x05), &AllowSelect1F, sizeof(UInt8)); //whats the current value
		
		// game default - disable selecting workbench
		if (AllowSelect1F == 0x00) {
			SafeWrite8((uintptr_t)WorkbenchSelection.addr + 0x05, 0x01);
			PIR_ConsolePrint("Workbench move disabled (game default)");
			return true;
		}

		// allows selecting and stroring workbench (todo: select only)
		if (AllowSelect1F == 0x01) {
			SafeWrite8((uintptr_t)WorkbenchSelection.addr + 0x05, 0x00); // allow selecting and storing workbench
			PIR_ConsolePrint("Workbench move allowed! Dont accidentally store it!");
			return true;
		}

		// should always be 0 or 1
		return false;

	}
	return false;
}

//toggle placing objects in red
static bool Toggle_PlaceInRed()
{
	// toggle off
	if (pir.PLACEINRED_ENABLED) {
		SafeWrite8((uintptr_t)pir.A + 0x06, 0x01);
		SafeWrite8((uintptr_t)pir.A + 0x0C, 0x02);
		SafeWrite8((uintptr_t)pir.B + 0x01, 0x01);
		SafeWriteBuf((uintptr_t)pir.C, pir.C_OLD, sizeof(pir.C_OLD));
		SafeWriteBuf((uintptr_t)pir.C + 0x11, pir.CC_OLD, sizeof(pir.CC_OLD));
		SafeWrite8((uintptr_t)pir.C + 0x1D, 0x01);
		SafeWriteBuf((uintptr_t)pir.D, pir.D_OLD, sizeof(pir.D_OLD));
		SafeWrite8((uintptr_t)pir.E + 0x00, 0x76);
		SafeWriteBuf((uintptr_t)pir.F, pir.F_OLD, sizeof(pir.F_OLD));
		SafeWrite8((uintptr_t)pir.G + 0x01, 0x95);
		SafeWrite8((uintptr_t)pir.H + 0x00, 0x74);
		SafeWriteBuf((uintptr_t)pir.J, pir.J_OLD, sizeof(pir.J_OLD));
		SafeWrite8((uintptr_t)pir.R + 0xC, 0x01); //red
		SafeWriteBuf((uintptr_t)pir.Y, pir.Y_OLD, sizeof(pir.Y_OLD)); 
		SafeWriteBuf((uintptr_t)pir.wstimer, pir.TIMER_OLD, sizeof(pir.TIMER_OLD)); 
		pir.PLACEINRED_ENABLED = false;
		PIR_ConsolePrint("Place in Red disabled.");
		return true;
	}

	// toggle on
	if (!pir.PLACEINRED_ENABLED) {
		SafeWrite8((uintptr_t)pir.A + 0x06, 0x00);
		SafeWrite8((uintptr_t)pir.A + 0x0C, 0x01);
		SafeWrite8((uintptr_t)pir.B + 0x01, 0x00);
		SafeWriteBuf((uintptr_t)pir.C, pir.C_NEW, sizeof(pir.C_NEW)); // movzx eax,byte ptr [Fallout4.exe+2E74998]
		SafeWriteBuf((uintptr_t)pir.C + 0x11, pir.CC_NEW, sizeof(pir.CC_NEW));
		SafeWrite8((uintptr_t)pir.C + 0x1D, 0x00);
		SafeWriteBuf((uintptr_t)pir.D, pir.D_NEW, sizeof(pir.D_NEW));
		SafeWrite8((uintptr_t)pir.E + 0x00, 0xEB);
		SafeWriteBuf((uintptr_t)pir.F, pir.NOP6, sizeof(pir.NOP6)); 
		SafeWrite8((uintptr_t)pir.G + 0x01, 0x98); // works but look at again later
		SafeWrite8((uintptr_t)pir.H + 0x00, 0xEB);
		SafeWriteBuf((uintptr_t)pir.J, pir.J_NEW, sizeof(pir.J_NEW)); //water or other restrictions
		SafeWrite8((uintptr_t)pir.R + 0x0C, 0x00); // red to green
		SafeWriteBuf((uintptr_t)pir.Y, pir.NOP3, sizeof(pir.NOP3)); // move yellow
		SafeWriteBuf((uintptr_t)pir.wstimer, pir.TIMER_NEW, sizeof(pir.TIMER_NEW)); // timer
		
		// set the correct bytes on enable
		SafeWriteBuf(uintptr_t(WSMode.addr) + 0x03, pir.TWO_ZEROS, sizeof(pir.TWO_ZEROS)); //0000
		SafeWriteBuf(uintptr_t(WSMode.addr) + 0x09, pir.TWO_ONES, sizeof(pir.TWO_ONES)); //0101

		pir.PLACEINRED_ENABLED = true;
		PIR_ConsolePrint("Place In Red enabled.");
		return true;
	}

	return false;
}

// play sound by filename. must be under data\sounds
static void PIR_PlayFileSound(const char* wav)
{
	if (pir.PlaySound_File_func) {
		pir.PlaySound_File_func(wav);
	}
}

// play sound using form name
static void PIR_PlayUISound(const char* sound)
{
	if (pir.PlaySound_UI_func) {
		pir.PlaySound_UI_func(sound);
	}
}

// called every time the console command runs
static bool ExecuteConsole(void* paramInfo, void* scriptData, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, double* result, void* opcodeOffsetPtr)
{
	if (!ParseConsoleArg_native || !ParseConsoleArg.ptr || (ParseConsoleArg.r32 == 0)) {
		pirlog("Failed to execute the console command!");
		return false;
	}
	
	std::array<char, 4096> param1{}; // zeroed
	std::array<char, 4096> param2{}; // zeroed

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

	if (consoleresult && param1[0])
	{
		switch (ConsoleSwitch(param1.data()))
		{
		// debug and tests
		case ConsoleSwitch("dumpcmds"):     DumpCmds();             break;
		case ConsoleSwitch("logref"):       LogWSRef();             break;
		case ConsoleSwitch("print"):        Toggle_ConsolePrint();  break;
		
		case ConsoleSwitch("sound"):        PIR_PlayFileSound(param2.data());  break;
		case ConsoleSwitch("uisound"):      PIR_PlayUISound(param2.data());    break;

			//toggles
		case ConsoleSwitch("1"):            Toggle_PlaceInRed();         break;
		case ConsoleSwitch("toggle"):       Toggle_PlaceInRed();         break;
		case ConsoleSwitch("2"):            Toggle_ObjectSnap();         break;
		case ConsoleSwitch("osnap"):        Toggle_ObjectSnap();         break;
		case ConsoleSwitch("3"):            Toggle_GroundSnap();         break;
		case ConsoleSwitch("gsnap"):        Toggle_GroundSnap();         break;
		case ConsoleSwitch("4"):            Toggle_SlowZoomAndRotate();  break;
		case ConsoleSwitch("slow"):         Toggle_SlowZoomAndRotate();  break;
		case ConsoleSwitch("5"):            Toggle_WorkshopSize();       break;
		case ConsoleSwitch("workshopsize"): Toggle_WorkshopSize();       break;
		case ConsoleSwitch("6"):            Toggle_Outlines();           break;
		case ConsoleSwitch("outlines"):     Toggle_Outlines();           break;
		case ConsoleSwitch("7"):            Toggle_Achievements();       break;
		case ConsoleSwitch("achievements"): Toggle_Achievements();       break;
		case ConsoleSwitch("8"):            ToggleWorkbenchMove();       break;
		case ConsoleSwitch("wb"):           ToggleWorkbenchMove();       break;

			//scale constants
		case ConsoleSwitch("scale1"):       SetCurrentRefScale(1.0000f);  break;
		case ConsoleSwitch("scale10"):      SetCurrentRefScale(9.9999f);  break;

		// reset angle
		case ConsoleSwitch("ra"):     ResetCurrentWSRefRotation();                       break;

		// modify x angle
		case ConsoleSwitch("xc"):    RotateCurrentWSRefByDegreesX(pir.fRotateDegreesCustomX);   break;
		case ConsoleSwitch("x-c"):   RotateCurrentWSRefByDegreesX(-pir.fRotateDegreesCustomX);  break;
		case ConsoleSwitch("x0.1"):  RotateCurrentWSRefByDegreesX(0.1f);                        break;
		case ConsoleSwitch("x0.5"):  RotateCurrentWSRefByDegreesX(0.5f);                        break;
		case ConsoleSwitch("x1"):    RotateCurrentWSRefByDegreesX(1.0f);                        break;
		case ConsoleSwitch("x2"):    RotateCurrentWSRefByDegreesX(2.0f);                        break;
		case ConsoleSwitch("x5"):    RotateCurrentWSRefByDegreesX(5.0f);                        break;
		case ConsoleSwitch("x10"):   RotateCurrentWSRefByDegreesX(10.0f);                       break;
		case ConsoleSwitch("x15"):   RotateCurrentWSRefByDegreesX(15.0f);                       break;
		case ConsoleSwitch("x30"):   RotateCurrentWSRefByDegreesX(30.0f);                       break;
		case ConsoleSwitch("x45"):   RotateCurrentWSRefByDegreesX(45.0f);                       break;
		case ConsoleSwitch("x-0.1"): RotateCurrentWSRefByDegreesX(-0.1f);                       break;
		case ConsoleSwitch("x-0.5"): RotateCurrentWSRefByDegreesX(-0.5f);                       break;
		case ConsoleSwitch("x-1"):   RotateCurrentWSRefByDegreesX(-1.0f);                       break;
		case ConsoleSwitch("x-5"):   RotateCurrentWSRefByDegreesX(-5.0f);                       break;
		case ConsoleSwitch("x-10"):  RotateCurrentWSRefByDegreesX(-10.0f);                      break;
		case ConsoleSwitch("x-15"):  RotateCurrentWSRefByDegreesX(-15.0f);                      break;
		case ConsoleSwitch("x-30"):  RotateCurrentWSRefByDegreesX(-30.0f);                      break;
		case ConsoleSwitch("x-45"):  RotateCurrentWSRefByDegreesX(-45.0f);                      break;

			// modify y angle
		case ConsoleSwitch("yc"):    RotateCurrentWSRefByDegreesY(pir.fRotateDegreesCustomY);   break;
		case ConsoleSwitch("y-c"):   RotateCurrentWSRefByDegreesY(-pir.fRotateDegreesCustomY);  break;
		case ConsoleSwitch("y0.1"):  RotateCurrentWSRefByDegreesY(0.1f);                        break;
		case ConsoleSwitch("y0.5"):  RotateCurrentWSRefByDegreesY(0.5f);                        break;
		case ConsoleSwitch("y1"):    RotateCurrentWSRefByDegreesY(1.0f);                        break;
		case ConsoleSwitch("y2"):    RotateCurrentWSRefByDegreesY(2.0f);                        break;
		case ConsoleSwitch("y5"):    RotateCurrentWSRefByDegreesY(5.0f);                        break;
		case ConsoleSwitch("y10"):   RotateCurrentWSRefByDegreesY(10.0f);                       break;
		case ConsoleSwitch("y15"):   RotateCurrentWSRefByDegreesY(15.0f);                       break;
		case ConsoleSwitch("y30"):   RotateCurrentWSRefByDegreesY(30.0f);                       break;
		case ConsoleSwitch("y45"):   RotateCurrentWSRefByDegreesY(45.0f);                       break;
		case ConsoleSwitch("y-0.1"): RotateCurrentWSRefByDegreesY(-0.1f);                       break;
		case ConsoleSwitch("y-0.5"): RotateCurrentWSRefByDegreesY(-0.5f);                       break;
		case ConsoleSwitch("y-1"):   RotateCurrentWSRefByDegreesY(-1.0f);                       break;
		case ConsoleSwitch("y-5"):   RotateCurrentWSRefByDegreesY(-5.0f);                       break;
		case ConsoleSwitch("y-10"):  RotateCurrentWSRefByDegreesY(-10.0f);                      break;
		case ConsoleSwitch("y-15"):  RotateCurrentWSRefByDegreesY(-15.0f);                      break;
		case ConsoleSwitch("y-30"):  RotateCurrentWSRefByDegreesY(-30.0f);                      break;
		case ConsoleSwitch("y-45"):  RotateCurrentWSRefByDegreesY(-45.0f);                      break;

			// modify z angle
		case ConsoleSwitch("zc"):    RotateCurrentWSRefByDegreesZ(pir.fRotateDegreesCustomZ);   break;
		case ConsoleSwitch("z-c"):   RotateCurrentWSRefByDegreesZ(-pir.fRotateDegreesCustomZ);  break;
		case ConsoleSwitch("z0.1"):  RotateCurrentWSRefByDegreesZ(0.1f);                        break;
		case ConsoleSwitch("z0.5"):  RotateCurrentWSRefByDegreesZ(0.5f);                        break;
		case ConsoleSwitch("z1"):    RotateCurrentWSRefByDegreesZ(1.0f);                        break;
		case ConsoleSwitch("z2"):    RotateCurrentWSRefByDegreesZ(2.0f);                        break;
		case ConsoleSwitch("z5"):    RotateCurrentWSRefByDegreesZ(5.0f);                        break;
		case ConsoleSwitch("z10"):   RotateCurrentWSRefByDegreesZ(10.0f);                       break;
		case ConsoleSwitch("z15"):   RotateCurrentWSRefByDegreesZ(15.0f);                       break;
		case ConsoleSwitch("z30"):   RotateCurrentWSRefByDegreesZ(30.0f);                       break;
		case ConsoleSwitch("z45"):   RotateCurrentWSRefByDegreesZ(45.0f);                       break;
		case ConsoleSwitch("z-0.1"): RotateCurrentWSRefByDegreesZ(-0.1f);                       break;
		case ConsoleSwitch("z-0.5"): RotateCurrentWSRefByDegreesZ(-0.5f);                       break;
		case ConsoleSwitch("z-1"):   RotateCurrentWSRefByDegreesZ(-1.0f);                       break;
		case ConsoleSwitch("z-5"):   RotateCurrentWSRefByDegreesZ(-5.0f);                       break;
		case ConsoleSwitch("z-10"):  RotateCurrentWSRefByDegreesZ(-10.0f);                      break;
		case ConsoleSwitch("z-15"):  RotateCurrentWSRefByDegreesZ(-15.0f);                      break;
		case ConsoleSwitch("z-30"):  RotateCurrentWSRefByDegreesZ(-30.0f);                      break;
		case ConsoleSwitch("z-45"):  RotateCurrentWSRefByDegreesZ(-45.0f);                      break;

			//scale up
		case ConsoleSwitch("scaleup1"):    ModCurrentRefScale(1.0100f); break;
		case ConsoleSwitch("scaleup2"):    ModCurrentRefScale(1.0200f); break;
		case ConsoleSwitch("scaleup5"):    ModCurrentRefScale(1.0500f); break;
		case ConsoleSwitch("scaleup10"):   ModCurrentRefScale(1.1000f); break;
		case ConsoleSwitch("scaleup25"):   ModCurrentRefScale(1.2500f); break;
		case ConsoleSwitch("scaleup50"):   ModCurrentRefScale(1.5000f); break;
		case ConsoleSwitch("scaleup100"):  ModCurrentRefScale(2.0000f); break;

			//scale down
		case ConsoleSwitch("scaledown1"):  ModCurrentRefScale(0.9900f); break;
		case ConsoleSwitch("scaledown2"):  ModCurrentRefScale(0.9800f); break;
		case ConsoleSwitch("scaledown5"):  ModCurrentRefScale(0.9500f); break;
		case ConsoleSwitch("scaledown10"): ModCurrentRefScale(0.9000f); break;
		case ConsoleSwitch("scaledown25"): ModCurrentRefScale(0.7500f); break;
		case ConsoleSwitch("scaledown50"): ModCurrentRefScale(0.5000f); break;
		case ConsoleSwitch("scaledown75"): ModCurrentRefScale(0.2500f); break;

			// lock and unlock
		case ConsoleSwitch("lock"):    LockUnlockWSRef(0, 1); break;
		case ConsoleSwitch("l"):       LockUnlockWSRef(0, 1); break;
		case ConsoleSwitch("lockq"):   LockUnlockWSRef(0, 0); break;
		case ConsoleSwitch("unlock"):  LockUnlockWSRef(1, 0); break;
		case ConsoleSwitch("u"):       LockUnlockWSRef(1, 0); break;

			// console name ref toggle
		case ConsoleSwitch("cnref"):   Toggle_ConsoleNameRef(); break;

			// show help
		case ConsoleSwitch("?"):       PIR_ConsolePrint(pir.ConsoleHelpMSG); break;
		case ConsoleSwitch("help"):    PIR_ConsolePrint(pir.ConsoleHelpMSG); break;

		default:
			PIR_ConsolePrint(pir.ConsoleHelpMSG);
			break;
		}

		return true;
	}

	pirlog("Failed to execute the console command!");
	return false;
}

// attempt to create the console command by hijacking an existing one
static bool PatchConsole(const char* hijacked_cmd_fullname)
{
	pirlog("Creating console command.");

	if (FirstConsole.cmd == nullptr) {
		return false;
	}

	const char* s_CommandToBorrow = hijacked_cmd_fullname;
	ObScriptCommand* s_hijackedCommand = nullptr;
	ObScriptParam* s_hijackedCommandParams = nullptr;

	for (ObScriptCommand* iter = FirstConsole.cmd; iter->opcode < (kObScript_NumConsoleCommands + kObScript_ConsoleOpBase); ++iter) {
		if (!strcmp(iter->longName, s_CommandToBorrow)) {
			s_hijackedCommand = iter;
			s_hijackedCommandParams = iter->params;
			break;
		}
	}

	if (s_hijackedCommand && s_hijackedCommandParams) {
		ObScriptCommand cmd = *s_hijackedCommand;
		ObScriptParam p1 = *s_hijackedCommandParams;
		ObScriptParam p2 = *s_hijackedCommandParams; // add optional param
		p2.isOptional = 1;
		ObScriptParam* allParams = new ObScriptParam[2]; // combine them
		allParams[0] = p1;
		allParams[1] = p2;

		cmd.longName = "placeinred";
		cmd.shortName = "pir";
		cmd.helpText = "pir [option] example: pir toggle, pir 1, pir 2, pir 3";
		cmd.needsParent = 0;
		cmd.numParams = 2;
		cmd.execute = ExecuteConsole;
		cmd.params = allParams;
		cmd.flags = 0;
		cmd.eval;

		SafeWriteBuf((uintptr_t)s_hijackedCommand, &cmd, sizeof(cmd));
		return true;
	}
	return false;
}

// read ini settings and toggle if needed
static void ReadINI()
{
	pirlog("Reading PlaceInRed.ini");
	const char* section = "Main";

	// Helper lambda for toggle settings
	auto ApplyToggle = [&](const char* key, bool& currentFlag, auto toggleFunc, bool defaultEnabled)
		{
			std::string val = GetPluginINISettingAsString(section, key);
			bool wantEnabled = GetBoolFromINIString(val, defaultEnabled);

			if (wantEnabled != currentFlag)
			{
				toggleFunc(); // flips the flag and applies patch
				pir.debuglog.FormattedMessage(" %s=%d (toggled)", key, wantEnabled);
			}
			else
			{
				pir.debuglog.FormattedMessage(" %s=%d (%s)", key, wantEnabled, val.empty() ? "hardcoded" : "unchanged");
			}
		};

	// ------------------- Boolean toggles -------------------
	ApplyToggle("PLACEINRED_ENABLED", pir.PLACEINRED_ENABLED, Toggle_PlaceInRed, false);
	ApplyToggle("OBJECTSNAP_ENABLED", pir.OBJECTSNAP_ENABLED, Toggle_ObjectSnap, true);
	ApplyToggle("GROUNDSNAP_ENABLED", pir.GROUNDSNAP_ENABLED, Toggle_GroundSnap, true);
	ApplyToggle("WORKSHOPSIZE_ENABLED", pir.WORKSHOPSIZE_ENABLED, Toggle_WorkshopSize, false);
	ApplyToggle("OUTLINES_ENABLED", pir.OUTLINES_ENABLED, Toggle_Outlines, true);
	ApplyToggle("ACHIEVEMENTS_ENABLED", pir.ACHIEVEMENTS_ENABLED, Toggle_Achievements, false);
	ApplyToggle("ConsoleNameRef_ENABLED", pir.ConsoleNameRef_ENABLED, Toggle_ConsoleNameRef, false);

	// ------------------- PrintConsoleMessages -------------------
	{
		std::string val = GetPluginINISettingAsString(section, "PrintConsoleMessages");
		bool wantEnabled = GetBoolFromINIString(val, true);

		if (wantEnabled != pir.PrintConsoleMessages)
		{
			pir.PrintConsoleMessages = wantEnabled;
			pir.debuglog.FormattedMessage(" PrintConsoleMessages=%d (toggled)", wantEnabled);
		}
		else
		{
			pir.debuglog.FormattedMessage(" PrintConsoleMessages=%d (%s)", wantEnabled, val.empty() ? "hardcoded" : "unchanged");
		}
	}

	// ------------------- Slow mode parameters -------------------
	{
		std::string rotStr = GetPluginINISettingAsString(section, "fSlowerROTATE");
		if (!rotStr.empty())
		{
			float f = FloatFromString(rotStr, 0.01f, 50.0f, 0.0f);
			pir.fSlowerROTATE = (f > 0.0f) ? f : 0.5000f;
		}
		pir.debuglog.FormattedMessage(" fSlowerROTATE=%.4f (%s)", pir.fSlowerROTATE, rotStr.empty() ? "hardcoded" : "ini");

		std::string zoomStr = GetPluginINISettingAsString(section, "fSlowerZOOM");
		if (!zoomStr.empty())
		{
			float f = FloatFromString(zoomStr, 0.01f, 50.0f, 0.0f);
			pir.fSlowerZOOM = (f > 0.0f) ? f : 1.0000f;
		}
		pir.debuglog.FormattedMessage(" fSlowerZOOM=%.4f (%s)", pir.fSlowerZOOM, zoomStr.empty() ? "hardcoded" : "ini");

		std::string val = GetPluginINISettingAsString(section, "SLOW_ENABLED");
		bool wantSlow = GetBoolFromINIString(val, false);

		if (wantSlow != pir.SLOW_ENABLED)
			Toggle_SlowZoomAndRotate();

		pir.debuglog.FormattedMessage(" SLOW_ENABLED=%d (%s)", wantSlow, val.empty() ? "hardcoded" : (wantSlow != pir.SLOW_ENABLED ? "toggled" : "unchanged"));
	}

	// ------------------- Custom rotate degrees -------------------
	{
		std::string xStr = GetPluginINISettingAsString(section, "fRotateDegreesCustomX");
		if (!xStr.empty())
		{
			float f = FloatFromString(xStr, 0.001f, 360.000f, 3.6000f);
			pir.fRotateDegreesCustomX = (f > 0.0f) ? f : 3.6000f;
		}
		pir.debuglog.FormattedMessage(" fRotateDegreesCustomX=%.4f (%s)", pir.fRotateDegreesCustomX, xStr.empty() ? "hardcoded" : "ini");

		std::string yStr = GetPluginINISettingAsString(section, "fRotateDegreesCustomY");
		if (!yStr.empty())
		{
			float f = FloatFromString(yStr, 0.001f, 360.000f, 3.6000f);
			pir.fRotateDegreesCustomY = (f > 0.0f) ? f : 3.6000f;
		}
		pir.debuglog.FormattedMessage(" fRotateDegreesCustomY=%.4f (%s)", pir.fRotateDegreesCustomY, yStr.empty() ? "hardcoded" : "ini");

		std::string zStr = GetPluginINISettingAsString(section, "fRotateDegreesCustomZ");
		if (!zStr.empty())
		{
			float f = FloatFromString(zStr, 0.001f, 360.000f, 3.6000f);
			pir.fRotateDegreesCustomZ = (f > 0.0f) ? f : 3.6000f;
		}
		pir.debuglog.FormattedMessage(" fRotateDegreesCustomZ=%.4f (%s)", pir.fRotateDegreesCustomZ, zStr.empty() ? "hardcoded" : "ini");
	}

	pirlog("Finished reading INI.");
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

	/* object interface
	placeinred.g_object = (F4SEObjectInterface*)f4se->QueryInterface(kInterface_Object);
	if (!g_object) {
		placeinred.pirlog.FormattedMessage("[%s] Failed to set object interface!", thisfunc);
		return false;
	}
	*/

	// message interface
	if (pir.g_messaging->RegisterListener(pir.pluginHandle, "F4SE", MessageInterfaceHandler) == false) {
		pirlog("Failed to register message listener!");
		return false;
	}

	pirlog("F4SE interfaces are set.");
	return true;

}

static void InitPIR()
{
	// launch async pattern searches
	pirlog("Multithreaded search for memory patterns.");
	std::vector<std::future<void>> vec_futures;

	vec_futures.emplace_back(FindPatternAsync(pir.R, "89 05 ? ? ? ? C6 05 ? ? ? ? 01 48 83 C4 68 C3"));
	vec_futures.emplace_back(FindPatternAsync(pir.RC, "E8 ? ? ? ? 83 3D ? ? ? ? 00 0F 87 ? ? ? ? 48 8B 03 48 8B CB FF 90 ? ? ? ? 48"));
	vec_futures.emplace_back(FindPatternAsync(Zoom.ptr, "F3 0F 10 0D ? ? ? ? 0F 29 74 24 20 F3 0F 10 35"));
	vec_futures.emplace_back(FindPatternAsync(Rotate.ptr, "F3 0F 10 05 ? ? ? ? ? ? ? ? ? ? ? ? 84 C9 75 07 0F 57 05"));
	vec_futures.emplace_back(FindPatternAsync(CurrentWSRef.ptr, "48 8B 1D ? ? ? ? 4C 8D 24 C3 49 3B DC 0F 84 ? ? ? ? 66"));
	vec_futures.emplace_back(FindPatternAsync(pir.achievements, "48 83 EC 28 C6 44 24 ? 00 84 D2 74 1C 48"));
	vec_futures.emplace_back(FindPatternAsync(pir.cnref_original_call_pattern, "FF 90 D0 01 00 00 48 89 74 24 40 4C 8D 05 ? ? ? ? 4C"));
	vec_futures.emplace_back(FindPatternAsync(pir.cnref_GetRefName_pattern, "E8 ? ? ? ? 4C 8B 05 ? ? ? ? 48 8D 4C 24 40 4C 8B C8 BA 00 01 00 00 E8 ? ? ? ? 83"));
	vec_futures.emplace_back(FindPatternAsync(pir.A, "C6 05 ? ? ? ? 01 84 C0 75 A9 B1 02"));
	vec_futures.emplace_back(FindPatternAsync(pir.B, "B2 01 88 15 ? ? ? ? EB 04 84 D2 74 07"));
	vec_futures.emplace_back(FindPatternAsync(pir.C, "0F B6 05 ? ? ? ? 44 0F B6 A5 ? ? ? ? 84 C0 75"));
	vec_futures.emplace_back(FindPatternAsync(pir.D, "0F B6 05 ? ? ? ? 3A 05 ? ? ? ? 48 8B B4 24 ? ? ? ? C6 05"));
	vec_futures.emplace_back(FindPatternAsync(pir.E, "76 0C C6 05 ? ? ? ? 06 E9 ? ? ? ? 40 84 ED 0F 84"));
	vec_futures.emplace_back(FindPatternAsync(pir.F, "88 05 ? ? ? ? E8 ? ? ? ? 84 C0 75 ? 80 3D ? ? ? ? 02"));
	vec_futures.emplace_back(FindPatternAsync(pir.G, "0F 95 05 ? ? ? ? E8 ? ? ? ? 40 38 35 ? ? ? ? 48"));
	vec_futures.emplace_back(FindPatternAsync(pir.H, "74 11 40 84 F6 74 0C 48 8D ? ? 49 8B ? E8"));
	vec_futures.emplace_back(FindPatternAsync(pir.J, "74 35 48 8B B5 ? ? ? ? 48 8B CE E8 ? ? ? ? 84 C0 75"));
	vec_futures.emplace_back(FindPatternAsync(pir.CORRECT, "C6 05 ? ? ? ? 01 40 84 F6 74 09 80 3D ? ? ? ? 00 75 ? 80 3D"));
	vec_futures.emplace_back(FindPatternAsync(pir.gsnap, "0F 86 ? ? ? ? 41 8B 4E 34 49 B8"));
	vec_futures.emplace_back(FindPatternAsync(pir.osnap, "F3 0F 10 35 ? ? ? ? 0F 28 C6 48 ? ? ? ? ? ? 33 C0"));
	vec_futures.emplace_back(FindPatternAsync(pir.outlines, "C6 05 ? ? ? ? 01 88 15 ? ? ? ? 76 13 48 8B 05"));
	vec_futures.emplace_back(FindPatternAsync(pir.wstimer, "0F 85 AB 00 00 00 F3 0F 10 05 ? ? ? ? 41 0F 2E"));
	vec_futures.emplace_back(FindPatternAsync(pir.Y, "8B 58 14 48 8D 4C 24 30 8B D3 45 33 C0 E8"));
	vec_futures.emplace_back(FindPatternAsync(FirstConsole.ptr, "48 8D 1D ? ? ? ? 48 8D 35 ? ? ? ? 66 90 48 8B 53 F8"));
	vec_futures.emplace_back(FindPatternAsync(FirstObScript.ptr, "48 8D 1D ? ? ? ? 4C 8D 35 ? ? ? ? 0F 1F 40 00 0F 1F 84 00 00 00 00 00"));
	vec_futures.emplace_back(FindPatternAsync(ParseConsoleArg.ptr, "4C 89 4C 24 20 48 89 4C 24 08 53 55 56 57 41 54 41 55 41 56 41 57"));
	vec_futures.emplace_back(FindPatternAsync(pir.GetScale_pattern, "66 89 BB 08 01 00 00 E8 ? ? ? ? 48 8B 0D ? ? ? ? 0F 28 F0 48"));
	vec_futures.emplace_back(FindPatternAsync(pir.SetScale_pattern, "E8 ? ? ? ? 40 84 F6 75 07 81 63 10 FF FF DF FF 33 ED"));
	vec_futures.emplace_back(FindPatternAsync(pir.PlaySound_File_pattern, "48 8B C4 48 89 58 08 57 48 81 EC 50 01 00 00 8B FA C7 40 18 FF FF FF FF 48"));
	vec_futures.emplace_back(FindPatternAsync(pir.PlaySound_UI_pattern, "48 89 5C 24 08 57 48 83 EC 50 48 8B D9 E8 ? ? ? ? 48 85 C0 74 6A"));
	vec_futures.emplace_back(FindPatternAsync(SetMotionType.ptr, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 50 45 32 E4 41 8D 41 FF"));
	vec_futures.emplace_back(FindPatternAsync(WSMode.ptr, "80 3D ? ? ? ? 00 74 0E C6 07 02 48 8B 5C 24 30 48 83 C4 20 5F C3"));
	vec_futures.emplace_back(FindPatternAsync(WSSize.ptr, "01 05 ? ? ? ? 8B 44 24 58 01 05 ? ? ? ? 85 D2 0F 84"));
	vec_futures.emplace_back(FindPatternAsync(gConsole.ptr, "48 8D 05 ? ? ? ? 48 89 2D ? ? ? ? 48 89 05 ? ? ? ? 89 2D ? ? ? ? 40 88 2D ? ? ? ? 48"));
	vec_futures.emplace_back(FindPatternAsync(gDataHandler.ptr, "48 83 3D ? ? ? ? 00 4D 8B F1 41 0F B6 F0 48 8B FA 48 8B D9 0F 84 ? ? ? ? 80 3D ? ? ? ? 00 48"));
	vec_futures.emplace_back(FindPatternAsync(WorkbenchSelection.ptr, "0F B6 84 02 ? ? ? ? 8B 8C 82 ? ? ? ? 48 03 CA FF E1 B0 01 48 83 C4 20 5B C3"));

	// Wait for all async tasks to complete
	for (auto& future : vec_futures) {
		future.get();
	}

	// store old bytes
	if (pir.C) { ReadMemory((uintptr_t(pir.C)), &pir.C_OLD, 0x07); }
	if (pir.D) { ReadMemory((uintptr_t(pir.D)), &pir.D_OLD, 0x07); }
	if (pir.F) { ReadMemory((uintptr_t(pir.F)), &pir.F_OLD, 0x06); }
	if (pir.RC) { ReadMemory((uintptr_t(pir.RC)), &pir.RC_OLD, 0x05); }
	if (pir.osnap) { ReadMemory((uintptr_t(pir.osnap)), &pir.OSNAP_OLD, 0x08); }

	//wssize
	if (WSSize.ptr) {
		ReadMemory((uintptr_t(WSSize.ptr) + 0x00), &pir.DRAWS_OLD, 0x06); //draws
		ReadMemory((uintptr_t(WSSize.ptr) + 0x0A), &pir.TRIS_OLD, 0x06); //triangles
		WSSize.r32 = GetRel32FromPattern((uintptr_t)WSSize.ptr, 0x02, 0x06, 0x00); // rel32 of draws
		WSSize.addr = pir.FO4BaseAddr + (uintptr_t)WSSize.r32;
	}

	//zoom and rotate
	if (Zoom.ptr && Rotate.ptr) {
		Zoom.r32 = GetRel32FromPattern((uintptr_t)Zoom.ptr, 0x04, 0x08, 0x00);
		Rotate.r32 = GetRel32FromPattern((uintptr_t)Rotate.ptr, 0x04, 0x08, 0x00);
		Zoom.addr = pir.FO4BaseAddr + (uintptr_t)Zoom.r32;
		Rotate.addr = pir.FO4BaseAddr + (uintptr_t)Rotate.r32;
		ReadMemory(Rotate.addr, &pir.fOriginalROTATE, sizeof(Float32));
		ReadMemory(Zoom.addr, &pir.fOriginalZOOM, sizeof(Float32));
	}

	//consolenameref
	if (pir.cnref_original_call_pattern && pir.cnref_GetRefName_pattern) {
		pir.cnref_GetRefName_r32 = GetRel32FromPattern((uintptr_t)pir.cnref_GetRefName_pattern, 0x01, 0x05, 0x00); //the good function
		pir.cnref_GetRefName_addr = pir.FO4BaseAddr + (uintptr_t)pir.cnref_GetRefName_r32; // good function full address
	}

	//wsmode
	if (WSMode.ptr) {
		WSMode.r32 = GetRel32FromPattern((uintptr_t)WSMode.ptr, 0x02, 0x07, 0x00);
		WSMode.addr = pir.FO4BaseAddr + WSMode.r32;
	}

	//moveworkbench
	if (WorkbenchSelection.ptr) {
		//this particular r32 is relative to the base address not the pattern location
		ReadMemory((uintptr_t)WorkbenchSelection.ptr + 0x04, &WorkbenchSelection.r32, sizeof(uint32_t));
		WorkbenchSelection.addr = pir.FO4BaseAddr + (uintptr_t)WorkbenchSelection.r32;

	}

	//setscale
	if (pir.SetScale_pattern) {
		pir.SetScale_s32 = GetRel32FromPattern((uintptr_t)pir.SetScale_pattern, 0x01, 0x05, 0x00);
		RelocAddr <_SetScale_Native> GimmeSetScale(pir.SetScale_s32);
		pir.SetScale_func = GimmeSetScale;
	}

	//getscale
	if (pir.GetScale_pattern) {
		pir.GetScale_r32 = GetRel32FromPattern((uintptr_t)pir.GetScale_pattern, 0x08, 0x0C, 0x00);
		RelocAddr <_GetScale_Native> GimmeGetScale(pir.GetScale_r32);
		pir.GetScale_func = GimmeGetScale;
	}

	//g_console
	if (gConsole.ptr) {
		gConsole.r32 = GetRel32FromPattern((uintptr_t)gConsole.ptr, 0x03, 0x07, 0x00);
		gConsole.addr = pir.FO4BaseAddr + (uintptr_t)gConsole.r32;
	}

	//g_datahandler
	if (gDataHandler.ptr) {
		gDataHandler.r32 = GetRel32FromPattern((uintptr_t)gDataHandler.ptr, 0x03, 0x08, 0x00);
		gDataHandler.addr = pir.FO4BaseAddr + (uintptr_t)gDataHandler.r32;
	}

	//CurrentWSRef
	if (CurrentWSRef.ptr) {
		CurrentWSRef.r32 = GetRel32FromPattern((uintptr_t)CurrentWSRef.ptr, 0x03, 0x07, 0x00);
		CurrentWSRef.addr = pir.FO4BaseAddr + (uintptr_t)CurrentWSRef.r32;
	}

	//GetConsoleArg
	if (ParseConsoleArg.ptr) {
		ParseConsoleArg.r32 = uintptr_t(ParseConsoleArg.ptr) - pir.FO4BaseAddr;
		RelocAddr <_ParseConsoleArg_Native> GetDatArg(ParseConsoleArg.r32);
		ParseConsoleArg_native = GetDatArg;
	}

	//first console
	if (FirstConsole.ptr) {
		FirstConsole.r32 = GetRel32FromPattern((uintptr_t)FirstConsole.ptr, 0x03, 0x07, -0x08);
		RelocPtr <ObScriptCommand> _FirstConsole(FirstConsole.r32);
		FirstConsole.cmd = _FirstConsole;
	}

	//first obscript
	if (FirstObScript.ptr) {
		FirstObScript.r32 = GetRel32FromPattern((uintptr_t)FirstObScript.ptr, 0x03, 0x07, -0x08);
		RelocPtr <ObScriptCommand> _FirstObScript(FirstObScript.r32);
		FirstObScript.cmd = _FirstObScript;
	}

	// setmotiontype
	if (SetMotionType.ptr) {
		SetMotionType.r32 = uintptr_t(SetMotionType.ptr) - pir.FO4BaseAddr;
		RelocAddr <_SetMotionType_Native> GimmeSetMotionType(SetMotionType.r32);
		SetMotionType_native = GimmeSetMotionType;
	}

	// playuisound
	if (pir.PlaySound_UI_pattern) {
		pir.PlaySound_UI_r32 = uintptr_t(pir.PlaySound_UI_pattern) - pir.FO4BaseAddr;
		RelocAddr <_PlayUISound_Native> _PlayUISound_Native(pir.PlaySound_UI_r32);
		pir.PlaySound_UI_func = _PlayUISound_Native;
	}

	// playfilesound
	if (pir.PlaySound_File_pattern) {
		pir.PlaySound_File_r32 = uintptr_t(pir.PlaySound_File_pattern) - pir.FO4BaseAddr;
		RelocAddr <_PlayFileSound_Native> PlayFileSound_Native(pir.PlaySound_File_r32);
		pir.PlaySound_File_func = PlayFileSound_Native;
	}
}

	


extern "C" {

	// F4SEPlugin_Load
	__declspec(dllexport) bool F4SEPlugin_Load(const F4SEInterface* f4se)
	{
		// start log
		pir.debuglog.OpenRelative(CSIDL_MYDOCUMENTS, pir.plugin_log_file);
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
			pirlog("plugin load failed after %llums! Couldn't find required patterns in memory!", pir.end_tickcount);
			LogPatterns();
			return false;
		}

		if (!PatchConsole("GameComment"))
		{
			pirlog("Failed to create console command! Plugin will run with defaults.");
		}

		// toggle defaults
		ReadINI();

		// plugin loaded
		pir.end_tickcount = GetTickCount64() - pir.start_tickcount;
		pirlog("finished in %llums.", pir.end_tickcount);
		LogPatterns();

		return true;
	}

	// F4SEPluginVersionData
	__declspec(dllexport) F4SEPluginVersionData F4SEPlugin_Version = {
		F4SEPluginVersionData::kVersion,
		pluginVersion,
		"PlaceInRed",
		"RandyConstan",
		0,
		0,
		{	RUNTIME_VERSION_1_11_191, 1
		},
		0,
	};
}
