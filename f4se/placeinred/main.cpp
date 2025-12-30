#include "main.h"

static PlaceInRed placeinred;
UInt32 pluginVersion = 14;

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
			s_configPath = runtimePath + placeinred.pluginINI;
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
static Float32 FloatFromString(std::string fString, Float32 min = 0.001, Float32 max = 999.999, Float32 error = 0)
{
	Float32 theFloat = 0;
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

	return (SInt32)(((instr + end) + rel - placeinred.FO4BaseAddr) + shift);
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
		&& placeinred.SetScale_pattern && placeinred.GetScale_pattern && CurrentWSRef.ptr
		&& WSMode.ptr && gConsole.ptr && placeinred.A && placeinred.B && placeinred.C
		&& placeinred.D && placeinred.E && placeinred.F && placeinred.G && placeinred.H
		&& placeinred.J && placeinred.Y && placeinred.R && placeinred.CORRECT
		&& placeinred.wstimer && placeinred.gsnap && placeinred.osnap && placeinred.outlines
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

//log all the memory patterns to the log file
static void LogPatterns()
{
	placeinred.log.FormattedMessage("--------------------------------------------------------------------------------");
	placeinred.log.FormattedMessage("Base          :%p|Fallout4.exe+0x00000000", (uintptr_t)placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("achievements  :%p|Fallout4.exe+0x%08X", placeinred.achievements, (uintptr_t)placeinred.achievements - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("A             :%p|Fallout4.exe+0x%08X", placeinred.A, (uintptr_t)placeinred.A - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("B             :%p|Fallout4.exe+0x%08X", placeinred.B, (uintptr_t)placeinred.B - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("C             :%p|Fallout4.exe+0x%08X (OLD: %02X%02X%02X%02X%02X%02X%02X)", placeinred.C, (uintptr_t)placeinred.C - placeinred.FO4BaseAddr, placeinred.C_OLD[0], placeinred.C_OLD[1], placeinred.C_OLD[2], placeinred.C_OLD[3], placeinred.C_OLD[4], placeinred.C_OLD[5], placeinred.C_OLD[6]);
	placeinred.log.FormattedMessage("D             :%p|Fallout4.exe+0x%08X (OLD: %02X%02X%02X%02X%02X%02X%02X)", placeinred.D, (uintptr_t)placeinred.D - placeinred.FO4BaseAddr, placeinred.D_OLD[0], placeinred.D_OLD[1], placeinred.D_OLD[2], placeinred.D_OLD[3], placeinred.D_OLD[4], placeinred.D_OLD[5], placeinred.D_OLD[6]);
	placeinred.log.FormattedMessage("E             :%p|Fallout4.exe+0x%08X", placeinred.E, (uintptr_t)placeinred.E - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("F             :%p|Fallout4.exe+0x%08X", placeinred.F, (uintptr_t)placeinred.F - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("G             :%p|Fallout4.exe+0x%08X", placeinred.G, (uintptr_t)placeinred.G - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("H             :%p|Fallout4.exe+0x%08X", placeinred.H, (uintptr_t)placeinred.H - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("J             :%p|Fallout4.exe+0x%08X", placeinred.J, (uintptr_t)placeinred.J - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("Y             :%p|Fallout4.exe+0x%08X", placeinred.Y, (uintptr_t)placeinred.Y - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("R             :%p|Fallout4.exe+0x%08X", placeinred.R, (uintptr_t)placeinred.R - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("RC            :%p|Fallout4.exe+0x%08X", placeinred.RC, (uintptr_t)placeinred.RC - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("CORRECT       :%p|Fallout4.exe+0x%08X", placeinred.CORRECT, (uintptr_t)placeinred.CORRECT - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("CurrentWSRef  :%p|Fallout4.exe+0x%08X", CurrentWSRef.ptr, CurrentWSRef.r32);
	placeinred.log.FormattedMessage("FirstConsole  :%p|Fallout4.exe+0x%08X", FirstConsole.ptr, FirstConsole.r32);
	placeinred.log.FormattedMessage("FirstObScript :%p|Fallout4.exe+0x%08X", FirstObScript.ptr, FirstObScript.r32);
	placeinred.log.FormattedMessage("GetConsoleArg :%p|Fallout4.exe+0x%08X", ParseConsoleArg.ptr, ParseConsoleArg.r32);
	placeinred.log.FormattedMessage("GetScale      :%p|Fallout4.exe+0x%08X", placeinred.GetScale_pattern, placeinred.GetScale_r32);
	placeinred.log.FormattedMessage("GConsole      :%p|Fallout4.exe+0x%08X", gConsole.ptr, gConsole.r32);
	placeinred.log.FormattedMessage("gsnap         :%p|Fallout4.exe+0x%08X", placeinred.gsnap, (uintptr_t)placeinred.gsnap - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("osnap         :%p|Fallout4.exe+0x%08X", placeinred.osnap, (uintptr_t)placeinred.osnap - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("outlines      :%p|Fallout4.exe+0x%08X", placeinred.outlines, (uintptr_t)placeinred.outlines - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("SetScale      :%p|Fallout4.exe+0x%08X", placeinred.SetScale_pattern, placeinred.SetScale_s32);
	placeinred.log.FormattedMessage("PlayFileSound :%p|Fallout4.exe+0x%08X", placeinred.PlaySound_File_pattern, placeinred.PlaySound_File_r32);
	placeinred.log.FormattedMessage("PlayUISound   :%p|Fallout4.exe+0x%08X", placeinred.PlaySound_UI_pattern, placeinred.PlaySound_UI_r32);
	placeinred.log.FormattedMessage("SetMotionType :%p|Fallout4.exe+0x%08X", SetMotionType.ptr, SetMotionType.r32);
	placeinred.log.FormattedMessage("WBSelect      :%p|Fallout4.exe+0x%08X", WorkbenchSelection.ptr, WorkbenchSelection.r32);
	placeinred.log.FormattedMessage("WSFloats      :%p|Fallout4.exe+0x%08X", WSSize.addr, WSSize.addr - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("WSMode        :%p|Fallout4.exe+0x%08X", WSMode.ptr, WSMode.r32);
	placeinred.log.FormattedMessage("WSSize        :%p|Fallout4.exe+0x%08X", WSSize.ptr, (uintptr_t)WSSize.ptr - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("WSTimer       :%p|Fallout4.exe+0x%08X", placeinred.wstimer, (uintptr_t)placeinred.wstimer - placeinred.FO4BaseAddr);
	placeinred.log.FormattedMessage("Rotate        :%p|%p|orig %f|slow %f", Rotate.ptr, Rotate.addr, placeinred.fOriginalROTATE, placeinred.fSlowerROTATE);
	placeinred.log.FormattedMessage("Zoom          :%p|%p|orig %f|slow %f", Zoom.ptr, Zoom.addr, placeinred.fOriginalZOOM, placeinred.fSlowerZOOM);
	placeinred.log.FormattedMessage("--------------------------------------------------------------------------------");
}



// Determine if player is in workshop mode
static bool IsPlayerInWorkshopMode()
		{
			if (WSMode.ptr) {
				UInt8 WSMODE = 0x00;
				ReadMemory(uintptr_t(WSMode.addr), &WSMODE, sizeof(UInt8));
				if (WSMODE == 0x01) {
					return true;
				}
			}
			return false;
		}

// Is the player 'grabbing' the current workshop ref or just highlighting it
static bool IsCurrentWSRefGrabbed()
		{
			if (WSMode.ptr) {
				UInt8 WSMODE_GRABBED = 0x00;
				ReadMemory(uintptr_t(WSMode.addr) + 0xB, &WSMODE_GRABBED, sizeof(UInt8));
				if (WSMODE_GRABBED == 0x01) {
					return true;
				}
			}
			return false;
		}

// force correct bytes (0000 and 0101) to check locations
static void SetCorrectBytes()
{
	if (WSMode.ptr) {
		SafeWriteBuf(uintptr_t(WSMode.addr) + 0x03, placeinred.TWO_ZEROS, sizeof(placeinred.TWO_ZEROS)); //0000
		SafeWriteBuf(uintptr_t(WSMode.addr) + 0x09, placeinred.TWO_ONES, sizeof(placeinred.TWO_ONES)); //0101
	}
}

// return the currently selected workshop ref with some safety checks
static TESObjectREFR* GetCurrentWSRef(bool bOnlySelectReferences=1)
		{
			if (CurrentWSRef.ptr && CurrentWSRef.addr && IsPlayerInWorkshopMode()) {

				//uintptr_t* refptr = GimmeMultiPointer(CurrentWSRef.addr, placeinred.CurrentWSRef_Offsets, placeinred.CurrentWSRef_OffsetsSize);
				//TESObjectREFR* ref = (TESObjectREFR*)(refptr);
				uintptr_t refaddr = GimmeMultiPointer(CurrentWSRef.addr, placeinred.CurrentWSRef_Offsets, placeinred.CurrentWSRef_OffsetsSize);
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

			_MESSAGE("========== TESObjectREFR DEBUG ==========");

			if (!ref) {
				_MESSAGE("Ref: NULL");
				_MESSAGE("=============== END DEBUG ===============");
				return;
			}

			//
			// Identity
			//
			_MESSAGE("Ref Ptr:        %p", ref);
			_MESSAGE("FormID:         %08X", ref->formID);
			_MESSAGE("FormType:       %02X", ref->GetFormType());
			_MESSAGE("Flags:          %08X", ref->flags);

			if (ref->baseForm)
				_MESSAGE("BaseForm:       %p (%08X)", ref->baseForm, ref->baseForm->formID);
			else
				_MESSAGE("BaseForm:       NULL");

			//
			// Name (safe virtual)
			//
			const char* name = nullptr;
			__try { name = CALL_MEMBER_FN(ref, GetReferenceName)(); }
			__except (EXCEPTION_EXECUTE_HANDLER) { name = nullptr; }

			_MESSAGE("Name:           %s", name ? name : "(none)");


			UInt8 formtype = 0;
			__try { formtype = *(UInt8*)((UInt8*)ref + 0x1A); }
			__except (EXCEPTION_EXECUTE_HANDLER) { formtype = 0xFF; }

			_MESSAGE("Ref+0x1A:     0x%02X", formtype);

			//
			// Transform
			//
			_MESSAGE("Position:       X=%.4f Y=%.4f Z=%.4f",
				ref->pos.x, ref->pos.y, ref->pos.z);

			_MESSAGE("Rotation(rad):  X=%.4f Y=%.4f Z=%.4f",
				ref->rot.x, ref->rot.y, ref->rot.z);

			_MESSAGE("Rotation(deg):  X=%.2f Y=%.2f Z=%.2f",
				ref->rot.x * 57.2957795f,
				ref->rot.y * 57.2957795f,
				ref->rot.z * 57.2957795f);

			//
			// Cell / world
			//
			_MESSAGE("ParentCell:     %p", ref->parentCell);

			TESWorldSpace* ws = nullptr;
			__try { ws = CALL_MEMBER_FN(ref, GetWorldspace)(); }
			__except (EXCEPTION_EXECUTE_HANDLER) { ws = nullptr; }

			_MESSAGE("Worldspace:     %p", ws);

			//
			// HandleRefObject
			//
			_MESSAGE("HandleRefCount: %u", ref->handleRefObject.QRefCount());

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
					placeinred.PlaySound_UI_func(placeinred.locksound);
				}
			}
		}

// dump cell refids and position to the log file
static void DumpCellRefs()
		{
			TESObjectREFR* ref = GetCurrentWSRef();
			if (ref) {
				TESObjectCELL* currentcell = ref->parentCell;
				TESObjectREFR* tempref;
				for (int i = 0; i < currentcell->objectList.count; i++)
				{
					currentcell->objectList.GetNthItem(i, tempref);
					UInt32 fid = tempref->formID;
					pirlog("%04X %f %f %f %f %f %f", fid, tempref->pos.x, tempref->pos.y, tempref->pos.z, tempref->rot.x, tempref->rot.y, tempref->rot.z);
				}
			}
		}

// To switch with strings
static constexpr unsigned int ConsoleSwitch(const char* s, int off = 0)
		{
			return !s[off] ? 5381 : (ConsoleSwitch(s, off + 1) * 33) ^ s[off];
		}

// print to console (copied from f4se + modified to use pattern)
static void ConsolePrint(const char* fmt, ...)
		{
			if (gConsole.ptr && gConsole.addr && placeinred.PrintConsoleMessages)
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
			if (placeinred.PrintConsoleMessages == true) {
				placeinred.PrintConsoleMessages = false;
				return true;
			}
			else {
				placeinred.PrintConsoleMessages = true;
				ConsolePrint("Enabled PIR console print.");
				return true;
			}
			return false;
		}

// f4se message interface handler
static void MessageInterfaceHandler(F4SEMessagingInterface::Message* msg)
		{
			switch (msg->type) {

			case F4SEMessagingInterface::kMessage_GameDataReady:
				placeinred.bF4SEGameDataIsReady = true;
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
				placeinred.SetScale_func(ref, newScale);
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
				float oldscale = placeinred.GetScale_func(ref);
				float newScale = oldscale * (fMultiplyAmount);
				if (newScale > 9.9999f) { newScale = 9.9999f; }
				if (newScale < 0.0001f) { newScale = 0.0001f; }
				placeinred.SetScale_func(ref, newScale);

				// fix jitter only if player isnt grabbing the item
				if (IsCurrentWSRefGrabbed() == false) {
					MoveRefToSelf(1);
				}

				return true;
			}
			return false;
		}

static void RotateRefByDegrees(float dXDeg, float dYDeg, float dZDeg)
		{
			TESObjectREFR* ref = GetCurrentWSRef();
			if (!ref)
				return;

			NiPoint3 pos = ref->pos;
			NiPoint3 rot = ref->rot;

			constexpr float DegToRad = (float)(MATH_PI / 180.0f);

			rot.x += dXDeg * DegToRad; // pitch
			rot.y += dYDeg * DegToRad; // roll
			rot.z += dZDeg * DegToRad; // yaw

			auto NormalizeRad = [](float a) -> float
				{
					static const float TwoPi = (float)(MATH_PI * 2.0f);
					while (a > MATH_PI) a -= TwoPi;
					while (a < -MATH_PI) a += TwoPi;
					return a;
				};

			rot.x = NormalizeRad(rot.x);
			rot.y = NormalizeRad(rot.y);
			rot.z = NormalizeRad(rot.z);

			TESObjectCELL* cell = ref->parentCell;
			TESWorldSpace* ws = cell ? cell->worldSpace : nullptr;

			UInt32* targetHandle = nullptr;

			MoveRefrToPosition(
				ref,
				targetHandle,
				cell,
				ws,
				&pos,
				&rot
			);
		}

// Single-axis helpers for readability and console bindings
static inline void RotateRefByDegreesZ(float deg)
		{
			// yaw = Z axis
			RotateRefByDegrees(0.0f, 0.0f, deg);
		}

static inline void RotateRefByDegreesX(float deg)
		{
			// pitch = X axis
			RotateRefByDegrees(deg, 0.0f, 0.0f);
		}

static inline void RotateRefByDegreesY(float deg)
		{
			// roll = Y axis
			RotateRefByDegrees(0.0f, deg, 0.0f);
		}

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

						uint64_t rel = (func >= placeinred.FO4BaseAddr)
							? (func - placeinred.FO4BaseAddr)
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
			if (placeinred.outlines && placeinred.OUTLINES_ENABLED) {
				SafeWrite8((uintptr_t)placeinred.outlines + 0x06, 0x00); //objects
				SafeWrite8((uintptr_t)placeinred.outlines + 0x0D, 0xEB); //npcs
				placeinred.OUTLINES_ENABLED = false;
				ConsolePrint("Object outlines disabled");
				return true;
			}
			if (placeinred.outlines && !placeinred.OUTLINES_ENABLED) {
				SafeWrite8((uintptr_t)placeinred.outlines + 0x06, 0x01); //objects
				SafeWrite8((uintptr_t)placeinred.outlines + 0x0D, 0x76); //npcs
				placeinred.OUTLINES_ENABLED = true;
				ConsolePrint("Object outlines enabled");
				return true;
			}
			return false;
		}

//toggle slower object rotation and zoom speed
static bool Toggle_SlowZoomAndRotate()
		{
			// its on, turn it off
			if (Zoom.ptr && Rotate.ptr && Zoom.addr && Rotate.addr && placeinred.SLOW_ENABLED) {
				SafeWriteBuf(Zoom.addr, &placeinred.fOriginalZOOM, sizeof(Float32));
				SafeWriteBuf(Rotate.addr, &placeinred.fOriginalROTATE, sizeof(Float32));
				placeinred.SLOW_ENABLED = false;
				ConsolePrint("Slow zoom/rotate - disabled");
				return true;
			}
			// its off, turn it on
			if (Zoom.ptr && Rotate.ptr && Zoom.addr && Rotate.addr && !placeinred.SLOW_ENABLED) {
				SafeWriteBuf(Zoom.addr, &placeinred.fSlowerZOOM, sizeof(Float32));
				SafeWriteBuf(Rotate.addr, &placeinred.fSlowerROTATE, sizeof(Float32));
				placeinred.SLOW_ENABLED = true;
				ConsolePrint("Slow zoom/rotate - enabled");
				return true;
			}
			return false;
		}

//toggle infinite workshop size
static bool Toggle_WorkshopSize()
		{
			if (WSSize.ptr && placeinred.WORKSHOPSIZE_ENABLED) {
				SafeWriteBuf((uintptr_t)WSSize.ptr, placeinred.DRAWS_OLD, sizeof(placeinred.DRAWS_OLD));
				SafeWriteBuf((uintptr_t)WSSize.ptr + 0x0A, placeinred.TRIS_OLD, sizeof(placeinred.TRIS_OLD));
				placeinred.WORKSHOPSIZE_ENABLED = false;
				ConsolePrint("Unlimited workshop size disabled");
				return true;
			}

			if (WSSize.ptr && placeinred.WORKSHOPSIZE_ENABLED == false) {
				// Write nop 6 so its never increased
				SafeWriteBuf((uintptr_t)WSSize.ptr, placeinred.NOP6, sizeof(placeinred.NOP6));
				SafeWriteBuf((uintptr_t)WSSize.ptr + 0x0A, placeinred.NOP6, sizeof(placeinred.NOP6));

				// set current ws draws and triangles to zero
				SafeWrite64(WSSize.addr, 0);
				placeinred.WORKSHOPSIZE_ENABLED = true;
				ConsolePrint("Unlimited workshop size enabled");
				return true;
			}
			return false;
		}

//toggle groundsnap
static bool Toggle_GroundSnap()
		{
			if (placeinred.gsnap && placeinred.GROUNDSNAP_ENABLED) {

				SafeWrite8((uintptr_t)placeinred.gsnap + 0x01, 0x85);
				placeinred.GROUNDSNAP_ENABLED = false;
				ConsolePrint("Ground snap disabled");
				return true;
			}
			if (placeinred.gsnap && !placeinred.GROUNDSNAP_ENABLED) {
				SafeWrite8((uintptr_t)placeinred.gsnap + 0x01, 0x86);
				placeinred.GROUNDSNAP_ENABLED = true;
				return true;

			}
			return false;
		}

//toggle objectsnap
static bool Toggle_ObjectSnap()
		{
			// its on - toggle it off
			if (placeinred.osnap && placeinred.OBJECTSNAP_ENABLED) {
				SafeWriteBuf((uintptr_t)placeinred.osnap, placeinred.OSNAP_NEW, sizeof(placeinred.OSNAP_NEW));
				placeinred.OBJECTSNAP_ENABLED = false;
				ConsolePrint("Object snap disabled");
				return true;
			}
			// its off - toggle it on
			if (placeinred.osnap && !placeinred.OBJECTSNAP_ENABLED) {
				SafeWriteBuf((uintptr_t)placeinred.osnap, placeinred.OSNAP_OLD, sizeof(placeinred.OSNAP_OLD));
				placeinred.OBJECTSNAP_ENABLED = true;
				ConsolePrint("Object snap enabled");
				return true;
			}
			return false;
		}

//toggle allowing achievements with mods
static bool Toggle_Achievements()
		{
			// its on - toggle it off
			if (placeinred.achievements && placeinred.ACHIEVEMENTS_ENABLED) {
				SafeWriteBuf((uintptr_t)placeinred.achievements, placeinred.ACHIEVE_OLD, sizeof(placeinred.ACHIEVE_OLD));
				placeinred.ACHIEVEMENTS_ENABLED = false;
				ConsolePrint("Achievements with mods disabled (game default)");
				return true;
			}
			// its off - toggle it on
			if (placeinred.achievements && !placeinred.ACHIEVEMENTS_ENABLED) {
				SafeWriteBuf((uintptr_t)placeinred.achievements, placeinred.ACHIEVE_NEW, sizeof(placeinred.ACHIEVE_NEW));
				placeinred.ACHIEVEMENTS_ENABLED = true;
				ConsolePrint("Achievements with mods enabled!");
				return true;
			}
			return false;
		}

// toggle consolenameref
static bool Toggle_ConsoleNameRef()
		{
			if (placeinred.cnref_GetRefName_pattern == false || placeinred.cnref_original_call_pattern == false) {
				return false;
			}

			// toggle off
			if (placeinred.ConsoleNameRef_ENABLED)
			{
				SafeWriteBuf(uintptr_t(placeinred.cnref_original_call_pattern), placeinred.CNameRef_OLD, placeinred.CNameRef_OLD_Size);
				placeinred.ConsoleNameRef_ENABLED = false;
				ConsolePrint("ConsoleRefName toggled off.");
				return true;
			}

			// toggle on
			if (!placeinred.ConsoleNameRef_ENABLED)
			{
				SafeWriteCall(uintptr_t(placeinred.cnref_original_call_pattern), placeinred.cnref_GetRefName_addr); //patch call
				SafeWrite8(uintptr_t(placeinred.cnref_original_call_pattern) + 0x05, 0x90); // for a clean patch
				placeinred.ConsoleNameRef_ENABLED = true;
				ConsolePrint("ConsoleRefName toggled on.");
				return true;
			}

			return false;
		}

// toggle moving the workbench by modifying vtable lookup bit for workbench type
static bool ToggleWorkbenchMove()
		{
			if (WorkbenchSelection.ptr && WorkbenchSelection.addr) {

				// game checks ref type in vtable at entry +5
				// Workbench is type 1F. (0x1F - 0x1A = 0x05)
				UInt8 AllowSelect1F = 0x00; //00 in the vtable means we can select it
				ReadMemory(uintptr_t(WorkbenchSelection.addr + 0x05), &AllowSelect1F, sizeof(UInt8)); //whats the current value
				
				// game default - disable selecting workbench
				if (AllowSelect1F == 0x00) {
					SafeWrite8((uintptr_t)WorkbenchSelection.addr + 0x05, 0x01);
					ConsolePrint("Workbench move disabled (game default)");
					return true;
				}

				// allows selecting and stroring workbench (todo: select only)
				if (AllowSelect1F == 0x01) {
					SafeWrite8((uintptr_t)WorkbenchSelection.addr + 0x05, 0x00); // allow selecting and storing workbench
					ConsolePrint("Workbench move allowed! Dont accidentally store it!");
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
			if (placeinred.PLACEINRED_ENABLED) {
				SafeWrite8((uintptr_t)placeinred.A + 0x06, 0x01);
				SafeWrite8((uintptr_t)placeinred.A + 0x0C, 0x02);
				SafeWrite8((uintptr_t)placeinred.B + 0x01, 0x01);
				SafeWriteBuf((uintptr_t)placeinred.C, placeinred.C_OLD, sizeof(placeinred.C_OLD));
				SafeWriteBuf((uintptr_t)placeinred.C + 0x11, placeinred.CC_OLD, sizeof(placeinred.CC_OLD));
				SafeWrite8((uintptr_t)placeinred.C + 0x1D, 0x01);
				SafeWriteBuf((uintptr_t)placeinred.D, placeinred.D_OLD, sizeof(placeinred.D_OLD));
				SafeWrite8((uintptr_t)placeinred.E + 0x00, 0x76);
				SafeWriteBuf((uintptr_t)placeinred.F, placeinred.F_OLD, sizeof(placeinred.F_OLD));
				SafeWrite8((uintptr_t)placeinred.G + 0x01, 0x95);
				SafeWrite8((uintptr_t)placeinred.H + 0x00, 0x74);
				SafeWriteBuf((uintptr_t)placeinred.J, placeinred.J_OLD, sizeof(placeinred.J_OLD));
				SafeWrite8((uintptr_t)placeinred.R + 0xC, 0x01); //red
				SafeWriteBuf((uintptr_t)placeinred.Y, placeinred.Y_OLD, sizeof(placeinred.Y_OLD)); 
				SafeWriteBuf((uintptr_t)placeinred.wstimer, placeinred.TIMER_OLD, sizeof(placeinred.TIMER_OLD)); 
				placeinred.PLACEINRED_ENABLED = false;
				ConsolePrint("Place in Red disabled.");
				return true;
			}

			// toggle on
			if (!placeinred.PLACEINRED_ENABLED) {
				SafeWrite8((uintptr_t)placeinred.A + 0x06, 0x00);
				SafeWrite8((uintptr_t)placeinred.A + 0x0C, 0x01);
				SafeWrite8((uintptr_t)placeinred.B + 0x01, 0x00);
				SafeWriteBuf((uintptr_t)placeinred.C, placeinred.C_NEW, sizeof(placeinred.C_NEW)); // movzx eax,byte ptr [Fallout4.exe+2E74998]
				SafeWriteBuf((uintptr_t)placeinred.C + 0x11, placeinred.CC_NEW, sizeof(placeinred.CC_NEW));
				SafeWrite8((uintptr_t)placeinred.C + 0x1D, 0x00);
				SafeWriteBuf((uintptr_t)placeinred.D, placeinred.D_NEW, sizeof(placeinred.D_NEW));
				SafeWrite8((uintptr_t)placeinred.E + 0x00, 0xEB);
				SafeWriteBuf((uintptr_t)placeinred.F, placeinred.NOP6, sizeof(placeinred.NOP6)); 
				SafeWrite8((uintptr_t)placeinred.G + 0x01, 0x98); // works but look at again later
				SafeWrite8((uintptr_t)placeinred.H + 0x00, 0xEB);
				SafeWriteBuf((uintptr_t)placeinred.J, placeinred.J_NEW, sizeof(placeinred.J_NEW)); //water or other restrictions
				SafeWrite8((uintptr_t)placeinred.R + 0x0C, 0x00); // red to green
				SafeWriteBuf((uintptr_t)placeinred.Y, placeinred.NOP3, sizeof(placeinred.NOP3)); // move yellow
				SafeWriteBuf((uintptr_t)placeinred.wstimer, placeinred.TIMER_NEW, sizeof(placeinred.TIMER_NEW)); // timer
				SetCorrectBytes();
				placeinred.PLACEINRED_ENABLED = true;
				ConsolePrint("Place In Red enabled.");
				return true;
			}

			return false;
		}

// play sound by filename. must be under data\sounds
static void PIR_PlayFileSound(const char* wav)
		{
			if (placeinred.PlaySound_File_func) {
				placeinred.PlaySound_File_func(wav);
			}
		}

// play sound using form name
static void PIR_PlayUISound(const char* sound)
		{
			if (placeinred.PlaySound_UI_func) {
				placeinred.PlaySound_UI_func(sound);
			}
		}

//called every time the console command runs
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
				case ConsoleSwitch("dumprefs"):     DumpCellRefs();         break;
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

					// modify x angle
				case ConsoleSwitch("xc"):    RotateRefByDegreesX(placeinred.fRotateDegreesCustomX);   break;
				case ConsoleSwitch("x-c"):   RotateRefByDegreesX(-placeinred.fRotateDegreesCustomX);  break;
				case ConsoleSwitch("x0.1"):  RotateRefByDegreesX(0.1f);   break;
				case ConsoleSwitch("x0.5"):  RotateRefByDegreesX(0.5f);   break;
				case ConsoleSwitch("x1"):    RotateRefByDegreesX(1.0f);   break;
				case ConsoleSwitch("x2"):    RotateRefByDegreesX(2.0f);   break;
				case ConsoleSwitch("x5"):    RotateRefByDegreesX(5.0f);   break;
				case ConsoleSwitch("x10"):   RotateRefByDegreesX(10.0f);  break;
				case ConsoleSwitch("x15"):   RotateRefByDegreesX(15.0f);  break;
				case ConsoleSwitch("x30"):   RotateRefByDegreesX(30.0f);  break;
				case ConsoleSwitch("x45"):   RotateRefByDegreesX(45.0f);  break;
				case ConsoleSwitch("x-0.1"): RotateRefByDegreesX(-0.1f);  break;
				case ConsoleSwitch("x-0.5"): RotateRefByDegreesX(-0.5f);  break;
				case ConsoleSwitch("x-1"):   RotateRefByDegreesX(-1.0f);  break;
				case ConsoleSwitch("x-5"):   RotateRefByDegreesX(-5.0f);  break;
				case ConsoleSwitch("x-10"):  RotateRefByDegreesX(-10.0f); break;
				case ConsoleSwitch("x-15"):  RotateRefByDegreesX(-15.0f); break;
				case ConsoleSwitch("x-30"):  RotateRefByDegreesX(-30.0f); break;
				case ConsoleSwitch("x-45"):  RotateRefByDegreesX(-45.0f); break;

					// modify y angle
				case ConsoleSwitch("yc"):    RotateRefByDegreesY(placeinred.fRotateDegreesCustomY);   break;
				case ConsoleSwitch("y-c"):   RotateRefByDegreesY(-placeinred.fRotateDegreesCustomY);  break;
				case ConsoleSwitch("y0.1"):  RotateRefByDegreesY(0.1f);   break;
				case ConsoleSwitch("y0.5"):  RotateRefByDegreesY(0.5f);   break;
				case ConsoleSwitch("y1"):    RotateRefByDegreesY(1.0f);   break;
				case ConsoleSwitch("y2"):    RotateRefByDegreesY(2.0f);   break;
				case ConsoleSwitch("y5"):    RotateRefByDegreesY(5.0f);   break;
				case ConsoleSwitch("y10"):   RotateRefByDegreesY(10.0f);  break;
				case ConsoleSwitch("y15"):   RotateRefByDegreesY(15.0f);  break;
				case ConsoleSwitch("y30"):   RotateRefByDegreesY(30.0f);  break;
				case ConsoleSwitch("y45"):   RotateRefByDegreesY(45.0f);  break;
				case ConsoleSwitch("y-0.1"): RotateRefByDegreesY(-0.1f);  break;
				case ConsoleSwitch("y-0.5"): RotateRefByDegreesY(-0.5f);  break;
				case ConsoleSwitch("y-1"):   RotateRefByDegreesY(-1.0f);  break;
				case ConsoleSwitch("y-5"):   RotateRefByDegreesY(-5.0f);  break;
				case ConsoleSwitch("y-10"):  RotateRefByDegreesY(-10.0f); break;
				case ConsoleSwitch("y-15"):  RotateRefByDegreesY(-15.0f); break;
				case ConsoleSwitch("y-30"):  RotateRefByDegreesY(-30.0f); break;
				case ConsoleSwitch("y-45"):  RotateRefByDegreesY(-45.0f); break;

					// modify z angle
				case ConsoleSwitch("zc"):    RotateRefByDegreesZ(placeinred.fRotateDegreesCustomZ);   break;
				case ConsoleSwitch("z-c"):   RotateRefByDegreesZ(-placeinred.fRotateDegreesCustomZ);  break;
				case ConsoleSwitch("z0.1"):  RotateRefByDegreesZ(0.1f);   break;
				case ConsoleSwitch("z0.5"):  RotateRefByDegreesZ(0.5f);   break;
				case ConsoleSwitch("z1"):    RotateRefByDegreesZ(1.0f);   break;
				case ConsoleSwitch("z2"):    RotateRefByDegreesZ(2.0f);   break;
				case ConsoleSwitch("z5"):    RotateRefByDegreesZ(5.0f);   break;
				case ConsoleSwitch("z10"):   RotateRefByDegreesZ(10.0f);  break;
				case ConsoleSwitch("z15"):   RotateRefByDegreesZ(15.0f);  break;
				case ConsoleSwitch("z30"):   RotateRefByDegreesZ(30.0f);  break;
				case ConsoleSwitch("z45"):   RotateRefByDegreesZ(45.0f);  break;
				case ConsoleSwitch("z-0.1"): RotateRefByDegreesZ(-0.1f);  break;
				case ConsoleSwitch("z-0.5"): RotateRefByDegreesZ(-0.5f);  break;
				case ConsoleSwitch("z-1"):   RotateRefByDegreesZ(-1.0f);  break;
				case ConsoleSwitch("z-5"):   RotateRefByDegreesZ(-5.0f);  break;
				case ConsoleSwitch("z-10"):  RotateRefByDegreesZ(-10.0f); break;
				case ConsoleSwitch("z-15"):  RotateRefByDegreesZ(-15.0f); break;
				case ConsoleSwitch("z-30"):  RotateRefByDegreesZ(-30.0f); break;
				case ConsoleSwitch("z-45"):  RotateRefByDegreesZ(-45.0f); break;

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
				case ConsoleSwitch("?"):       ConsolePrint(placeinred.ConsoleHelpMSG); break;
				case ConsoleSwitch("help"):    ConsolePrint(placeinred.ConsoleHelpMSG); break;

				default:
					ConsolePrint(placeinred.ConsoleHelpMSG);
					break;
				}

				return true;
			}

			pirlog("Failed to execute the console command!");
			return false;
		}

//attempt to create the console command
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
						placeinred.log.FormattedMessage(" %s=%d (toggled)", key, wantEnabled);
					}
					else
					{
						placeinred.log.FormattedMessage(" %s=%d (%s)", key, wantEnabled, val.empty() ? "hardcoded" : "unchanged");
					}
				};

			// ------------------- Boolean toggles -------------------
			ApplyToggle("PLACEINRED_ENABLED", placeinred.PLACEINRED_ENABLED, Toggle_PlaceInRed, false);
			ApplyToggle("OBJECTSNAP_ENABLED", placeinred.OBJECTSNAP_ENABLED, Toggle_ObjectSnap, true);
			ApplyToggle("GROUNDSNAP_ENABLED", placeinred.GROUNDSNAP_ENABLED, Toggle_GroundSnap, true);
			ApplyToggle("WORKSHOPSIZE_ENABLED", placeinred.WORKSHOPSIZE_ENABLED, Toggle_WorkshopSize, false);
			ApplyToggle("OUTLINES_ENABLED", placeinred.OUTLINES_ENABLED, Toggle_Outlines, true);
			ApplyToggle("ACHIEVEMENTS_ENABLED", placeinred.ACHIEVEMENTS_ENABLED, Toggle_Achievements, false);
			ApplyToggle("ConsoleNameRef_ENABLED", placeinred.ConsoleNameRef_ENABLED, Toggle_ConsoleNameRef, false);

			// ------------------- PrintConsoleMessages -------------------
			{
				std::string val = GetPluginINISettingAsString(section, "PrintConsoleMessages");
				bool wantEnabled = GetBoolFromINIString(val, true);

				if (wantEnabled != placeinred.PrintConsoleMessages)
				{
					placeinred.PrintConsoleMessages = wantEnabled;
					placeinred.log.FormattedMessage(" PrintConsoleMessages=%d (toggled)", wantEnabled);
				}
				else
				{
					placeinred.log.FormattedMessage(" PrintConsoleMessages=%d (%s)", wantEnabled, val.empty() ? "hardcoded" : "unchanged");
				}
			}

			// ------------------- Slow mode parameters -------------------
			{
				std::string rotStr = GetPluginINISettingAsString(section, "fSlowerROTATE");
				if (!rotStr.empty())
				{
					float f = FloatFromString(rotStr, 0.01f, 50.0f, 0.0f);
					placeinred.fSlowerROTATE = (f > 0.0f) ? f : 0.5000f;
				}
				placeinred.log.FormattedMessage(" fSlowerROTATE=%.4f (%s)", placeinred.fSlowerROTATE, rotStr.empty() ? "hardcoded" : "ini");

				std::string zoomStr = GetPluginINISettingAsString(section, "fSlowerZOOM");
				if (!zoomStr.empty())
				{
					float f = FloatFromString(zoomStr, 0.01f, 50.0f, 0.0f);
					placeinred.fSlowerZOOM = (f > 0.0f) ? f : 1.0000f;
				}
				placeinred.log.FormattedMessage(" fSlowerZOOM=%.4f (%s)", placeinred.fSlowerZOOM, zoomStr.empty() ? "hardcoded" : "ini");

				std::string val = GetPluginINISettingAsString(section, "SLOW_ENABLED");
				bool wantSlow = GetBoolFromINIString(val, false);

				if (wantSlow != placeinred.SLOW_ENABLED)
					Toggle_SlowZoomAndRotate();

				placeinred.log.FormattedMessage(" SLOW_ENABLED=%d (%s)", wantSlow, val.empty() ? "hardcoded" : (wantSlow != placeinred.SLOW_ENABLED ? "toggled" : "unchanged"));
			}

			// ------------------- Custom rotate degrees -------------------
			{
				std::string xStr = GetPluginINISettingAsString(section, "fRotateDegreesCustomX");
				if (!xStr.empty())
				{
					float f = FloatFromString(xStr, 0.001f, 360.000f, 3.6000f);
					placeinred.fRotateDegreesCustomX = (f > 0.0f) ? f : 3.6000f;
				}
				placeinred.log.FormattedMessage(" fRotateDegreesCustomX=%.4f (%s)", placeinred.fRotateDegreesCustomX, xStr.empty() ? "hardcoded" : "ini");

				std::string yStr = GetPluginINISettingAsString(section, "fRotateDegreesCustomY");
				if (!yStr.empty())
				{
					float f = FloatFromString(yStr, 0.001f, 360.000f, 3.6000f);
					placeinred.fRotateDegreesCustomY = (f > 0.0f) ? f : 3.6000f;
				}
				placeinred.log.FormattedMessage(" fRotateDegreesCustomY=%.4f (%s)", placeinred.fRotateDegreesCustomY, yStr.empty() ? "hardcoded" : "ini");

				std::string zStr = GetPluginINISettingAsString(section, "fRotateDegreesCustomZ");
				if (!zStr.empty())
				{
					float f = FloatFromString(zStr, 0.001f, 360.000f, 3.6000f);
					placeinred.fRotateDegreesCustomZ = (f > 0.0f) ? f : 3.6000f;
				}
				placeinred.log.FormattedMessage(" fRotateDegreesCustomZ=%.4f (%s)", placeinred.fRotateDegreesCustomZ, zStr.empty() ? "hardcoded" : "ini");
			}

			pirlog("Finished reading INI.");
		}

//init f4se stuff and return false if anything fails
static bool InitF4SE(const F4SEInterface* f4se)
		{
			// get a plugin handle
			placeinred.pirPluginHandle = f4se->GetPluginHandle();
			if (!placeinred.pirPluginHandle) {
				pirlog("Couldn't get a plugin handle!");
				return false;
			}
			pirlog("Got a plugin handle.");

			// set messaging interface
			placeinred.g_messaging = (F4SEMessagingInterface*)f4se->QueryInterface(kInterface_Messaging);
			if (!placeinred.g_messaging) {
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
			if (placeinred.g_messaging->RegisterListener(placeinred.pirPluginHandle, "F4SE", MessageInterfaceHandler) == false) {
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

			vec_futures.emplace_back(FindPatternAsync(placeinred.R, "89 05 ? ? ? ? C6 05 ? ? ? ? 01 48 83 C4 68 C3"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.RC, "E8 ? ? ? ? 83 3D ? ? ? ? 00 0F 87 ? ? ? ? 48 8B 03 48 8B CB FF 90 ? ? ? ? 48"));
			vec_futures.emplace_back(FindPatternAsync(Zoom.ptr, "F3 0F 10 0D ? ? ? ? 0F 29 74 24 20 F3 0F 10 35"));
			vec_futures.emplace_back(FindPatternAsync(Rotate.ptr, "F3 0F 10 05 ? ? ? ? ? ? ? ? ? ? ? ? 84 C9 75 07 0F 57 05"));
			vec_futures.emplace_back(FindPatternAsync(CurrentWSRef.ptr, "48 8B 1D ? ? ? ? 4C 8D 24 C3 49 3B DC 0F 84 ? ? ? ? 66"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.achievements, "48 83 EC 28 C6 44 24 ? 00 84 D2 74 1C 48"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.cnref_original_call_pattern, "FF 90 D0 01 00 00 48 89 74 24 40 4C 8D 05 ? ? ? ? 4C"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.cnref_GetRefName_pattern, "E8 ? ? ? ? 4C 8B 05 ? ? ? ? 48 8D 4C 24 40 4C 8B C8 BA 00 01 00 00 E8 ? ? ? ? 83"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.A, "C6 05 ? ? ? ? 01 84 C0 75 A9 B1 02"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.B, "B2 01 88 15 ? ? ? ? EB 04 84 D2 74 07"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.C, "0F B6 05 ? ? ? ? 44 0F B6 A5 ? ? ? ? 84 C0 75"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.D, "0F B6 05 ? ? ? ? 3A 05 ? ? ? ? 48 8B B4 24 ? ? ? ? C6 05"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.E, "76 0C C6 05 ? ? ? ? 06 E9 ? ? ? ? 40 84 ED 0F 84"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.F, "88 05 ? ? ? ? E8 ? ? ? ? 84 C0 75 ? 80 3D ? ? ? ? 02"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.G, "0F 95 05 ? ? ? ? E8 ? ? ? ? 40 38 35 ? ? ? ? 48"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.H, "74 11 40 84 F6 74 0C 48 8D ? ? 49 8B ? E8"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.J, "74 35 48 8B B5 ? ? ? ? 48 8B CE E8 ? ? ? ? 84 C0 75"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.CORRECT, "C6 05 ? ? ? ? 01 40 84 F6 74 09 80 3D ? ? ? ? 00 75 ? 80 3D"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.gsnap, "0F 86 ? ? ? ? 41 8B 4E 34 49 B8"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.osnap, "F3 0F 10 35 ? ? ? ? 0F 28 C6 48 ? ? ? ? ? ? 33 C0"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.outlines, "C6 05 ? ? ? ? 01 88 15 ? ? ? ? 76 13 48 8B 05"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.wstimer, "0F 85 AB 00 00 00 F3 0F 10 05 ? ? ? ? 41 0F 2E"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.Y, "8B 58 14 48 8D 4C 24 30 8B D3 45 33 C0 E8"));
			vec_futures.emplace_back(FindPatternAsync(FirstConsole.ptr, "48 8D 1D ? ? ? ? 48 8D 35 ? ? ? ? 66 90 48 8B 53 F8"));
			vec_futures.emplace_back(FindPatternAsync(FirstObScript.ptr, "48 8D 1D ? ? ? ? 4C 8D 35 ? ? ? ? 0F 1F 40 00 0F 1F 84 00 00 00 00 00"));
			vec_futures.emplace_back(FindPatternAsync(ParseConsoleArg.ptr, "4C 89 4C 24 20 48 89 4C 24 08 53 55 56 57 41 54 41 55 41 56 41 57"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.GetScale_pattern, "66 89 BB 08 01 00 00 E8 ? ? ? ? 48 8B 0D ? ? ? ? 0F 28 F0 48"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.SetScale_pattern, "E8 ? ? ? ? 40 84 F6 75 07 81 63 10 FF FF DF FF 33 ED"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.PlaySound_File_pattern, "48 8B C4 48 89 58 08 57 48 81 EC 50 01 00 00 8B FA C7 40 18 FF FF FF FF 48"));
			vec_futures.emplace_back(FindPatternAsync(placeinred.PlaySound_UI_pattern, "48 89 5C 24 08 57 48 83 EC 50 48 8B D9 E8 ? ? ? ? 48 85 C0 74 6A"));
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
			if (placeinred.C) { ReadMemory((uintptr_t(placeinred.C)), &placeinred.C_OLD, 0x07); }
			if (placeinred.D) { ReadMemory((uintptr_t(placeinred.D)), &placeinred.D_OLD, 0x07); }
			if (placeinred.F) { ReadMemory((uintptr_t(placeinred.F)), &placeinred.F_OLD, 0x06); }
			if (placeinred.RC) { ReadMemory((uintptr_t(placeinred.RC)), &placeinred.RC_OLD, 0x05); }
			if (placeinred.osnap) { ReadMemory((uintptr_t(placeinred.osnap)), &placeinred.OSNAP_OLD, 0x08); }

			//wssize
			if (WSSize.ptr) {
				ReadMemory((uintptr_t(WSSize.ptr) + 0x00), &placeinred.DRAWS_OLD, 0x06); //draws
				ReadMemory((uintptr_t(WSSize.ptr) + 0x0A), &placeinred.TRIS_OLD, 0x06); //triangles
				WSSize.r32 = GetRel32FromPattern((uintptr_t)WSSize.ptr, 0x02, 0x06, 0x00); // rel32 of draws
				WSSize.addr = placeinred.FO4BaseAddr + (uintptr_t)WSSize.r32;
			}

			//zoom and rotate
			if (Zoom.ptr && Rotate.ptr) {
				Zoom.r32 = GetRel32FromPattern((uintptr_t)Zoom.ptr, 0x04, 0x08, 0x00);
				Rotate.r32 = GetRel32FromPattern((uintptr_t)Rotate.ptr, 0x04, 0x08, 0x00);
				Zoom.addr = placeinred.FO4BaseAddr + (uintptr_t)Zoom.r32;
				Rotate.addr = placeinred.FO4BaseAddr + (uintptr_t)Rotate.r32;
				ReadMemory(Rotate.addr, &placeinred.fOriginalROTATE, sizeof(Float32));
				ReadMemory(Zoom.addr, &placeinred.fOriginalZOOM, sizeof(Float32));
			}

			//consolenameref
			if (placeinred.cnref_original_call_pattern && placeinred.cnref_GetRefName_pattern) {
				placeinred.cnref_GetRefName_r32 = GetRel32FromPattern((uintptr_t)placeinred.cnref_GetRefName_pattern, 0x01, 0x05, 0x00); //the good function
				placeinred.cnref_GetRefName_addr = placeinred.FO4BaseAddr + (uintptr_t)placeinred.cnref_GetRefName_r32; // good function full address
			}

			//wsmode
			if (WSMode.ptr) {
				WSMode.r32 = GetRel32FromPattern((uintptr_t)WSMode.ptr, 0x02, 0x07, 0x00);
				WSMode.addr = placeinred.FO4BaseAddr + WSMode.r32;
			}

			//moveworkbench
			if (WorkbenchSelection.ptr) {
				//this particular r32 is relative to the base address not the pattern location
				ReadMemory((uintptr_t)WorkbenchSelection.ptr + 0x04, &WorkbenchSelection.r32, sizeof(uint32_t));
				WorkbenchSelection.addr = placeinred.FO4BaseAddr + (uintptr_t)WorkbenchSelection.r32;

			}

			//setscale
			if (placeinred.SetScale_pattern) {
				placeinred.SetScale_s32 = GetRel32FromPattern((uintptr_t)placeinred.SetScale_pattern, 0x01, 0x05, 0x00);
				RelocAddr <_SetScale_Native> GimmeSetScale(placeinred.SetScale_s32);
				placeinred.SetScale_func = GimmeSetScale;
			}

			//getscale
			if (placeinred.GetScale_pattern) {
				placeinred.GetScale_r32 = GetRel32FromPattern((uintptr_t)placeinred.GetScale_pattern, 0x08, 0x0C, 0x00);
				RelocAddr <_GetScale_Native> GimmeGetScale(placeinred.GetScale_r32);
				placeinred.GetScale_func = GimmeGetScale;
			}

			//g_console
			if (gConsole.ptr) {
				gConsole.r32 = GetRel32FromPattern((uintptr_t)gConsole.ptr, 0x03, 0x07, 0x00);
				gConsole.addr = placeinred.FO4BaseAddr + (uintptr_t)gConsole.r32;
			}

			//g_datahandler
			if (gDataHandler.ptr) {
				gDataHandler.r32 = GetRel32FromPattern((uintptr_t)gDataHandler.ptr, 0x03, 0x08, 0x00);
				gDataHandler.addr = placeinred.FO4BaseAddr + (uintptr_t)gDataHandler.r32;
			}

			//CurrentWSRef
			if (CurrentWSRef.ptr) {
				CurrentWSRef.r32 = GetRel32FromPattern((uintptr_t)CurrentWSRef.ptr, 0x03, 0x07, 0x00);
				CurrentWSRef.addr = placeinred.FO4BaseAddr + (uintptr_t)CurrentWSRef.r32;
			}

			//GetConsoleArg
			if (ParseConsoleArg.ptr) {
				ParseConsoleArg.r32 = uintptr_t(ParseConsoleArg.ptr) - placeinred.FO4BaseAddr;
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
				SetMotionType.r32 = uintptr_t(SetMotionType.ptr) - placeinred.FO4BaseAddr;
				RelocAddr <_SetMotionType_Native> GimmeSetMotionType(SetMotionType.r32);
				SetMotionType_native = GimmeSetMotionType;
			}

			// playuisound
			if (placeinred.PlaySound_UI_pattern) {
				placeinred.PlaySound_UI_r32 = uintptr_t(placeinred.PlaySound_UI_pattern) - placeinred.FO4BaseAddr;
				RelocAddr <_PlayUISound_Native> _PlayUISound_Native(placeinred.PlaySound_UI_r32);
				placeinred.PlaySound_UI_func = _PlayUISound_Native;
			}

			// playfilesound
			if (placeinred.PlaySound_File_pattern) {
				placeinred.PlaySound_File_r32 = uintptr_t(placeinred.PlaySound_File_pattern) - placeinred.FO4BaseAddr;
				RelocAddr <_PlayFileSound_Native> PlayFileSound_Native(placeinred.PlaySound_File_r32);
				placeinred.PlaySound_File_func = PlayFileSound_Native;
			}
		}

	


extern "C" {

	// F4SEPlugin_Load
	__declspec(dllexport) bool F4SEPlugin_Load(const F4SEInterface* f4se)
	{
		// start log
		placeinred.log.OpenRelative(CSIDL_MYDOCUMENTS, placeinred.pluginLogFile);
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
			pirlog("plugin load failed after %llums! Couldn't find required patterns in memory!", placeinred.end_tickcount);
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
		placeinred.end_tickcount = GetTickCount64() - placeinred.start_tickcount;
		pirlog("finished in %llums.", placeinred.end_tickcount);
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
