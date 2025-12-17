#include "main.h"

static _SETTINGS             settings;
static _PATCHES              Patches;
static _POINTERS             Pointers;
static _PlaySounds           PlaySounds;
static _ScaleFuncs           ScaleFuncs;
//static _AngleFunctions       AngleFunctions;
static _CNameRef             CNameRef;
static SimpleFinder          FirstConsole;
static SimpleFinder          FirstObScript;
static SimpleFinder          WSMode;
static SimpleFinder          WSSize;
static SimpleFinder          WorkbenchSelection;
static SimpleFinder          gConsole;
static SimpleFinder          gDataHandler;
static SimpleFinder          CurrentWSRef;
static SimpleFinder          Zoom;
static SimpleFinder          Rotate;
static SimpleFinder          SetMotionType;
static SimpleFinder          GetConsoleArg;
static _SetMotionType_Native SetMotionType_Native = nullptr;
static _GetConsoleArg_Native GetConsoleArg_Native = nullptr;

namespace pir {

	// return the ini path as a std string
	static const std::string& GetPluginINIPath()
	{
		static std::string s_configPath;

		if (s_configPath.empty())
		{
			std::string	runtimePath = GetRuntimeDirectory();
			if (!runtimePath.empty())
			{
				s_configPath = runtimePath + pluginINI;
			}
		}
		return s_configPath;
	}

	// return an ini setting as a std string
	static std::string GetPIRConfigOption(const char* section, const char* key)
	{
		std::string	result;
		const std::string& configPath = GetPluginINIPath();
		if (!configPath.empty())
		{
			char resultBuf[2048]{};
			resultBuf[0] = 0;
			UInt32 resultLen = GetPrivateProfileString(section, key, NULL, resultBuf, sizeof(resultBuf), configPath.c_str());
			result = resultBuf;
		}
		return result;
	}

	// grok generated - bool parser
	static bool StringToBool(const std::string& s, bool defaultValue = false)
	{
		std::string lower;
		lower.reserve(s.size());
		for (char c : s) lower += std::tolower(c);

		// Trim whitespace
		const auto start = lower.find_first_not_of(" \t\r\n");
		if (start == std::string::npos) return defaultValue;
		const auto end = lower.find_last_not_of(" \t\r\n");
		const std::string trimmed = lower.substr(start, end - start + 1);

		if (trimmed == "1" || trimmed == "true" || trimmed == "yes" || trimmed == "on")
			return true;
		if (trimmed == "0" || trimmed == "false" || trimmed == "no" || trimmed == "off")
			return false;

		return defaultValue;
	}

	// grok generated - GetPrivateProfileString with a tiny helper wrapper
	static std::string GetINIString(const char* section, const char* key, const char* defaultValue = "")
	{
		const std::string& iniPath = pir::GetPluginINIPath();
		if (iniPath.empty()) return defaultValue;

		char buffer[256] = {};
		GetPrivateProfileString(section, key, defaultValue, buffer, sizeof(buffer), iniPath.c_str());
		return buffer;
	}


}

extern "C" {
	namespace pir {

		static const char* ConsoleHelpMSG =
		{
		  "PlaceInRed (pir) usage:\n"
		  "pir toggle       (pir 1) toggle place in R\n"
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

		// return the same char array with '\r' '\n' and '|' removed
		static char* StripNewLinesAndPipes(const char* str) {
			size_t len = strlen(str);
			char* newStr = new char[len + 1]; // Allocate memory for the new string
		
			const char* src = str;
			char* dst = newStr;
		
			while (*src) {
				if ((*src != '\n') && (*src != 0x7C) && (*src != '\r'))
				{
					*dst++ = *src;
				}
				src++;
			}
			*dst = '\0'; // Null-terminate the result string
		
			return newStr;
		}

		// Simple function to read memory (credit reg2k).
		static bool ReadMemory(uintptr_t addr, void* data, size_t len)
		{
			UInt32 oldProtect;
			// Change memory protection to allow read/write/execute access
			if (VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &oldProtect)) {
				// Copy memory to buffer
				memcpy(data, (void*)addr, len);

				// Restore the original memory protection
				if (VirtualProtect((void*)addr, len, oldProtect, &oldProtect)) {
					return true;
				}
			}
			return false;
		}

		// return rel32 from a pattern match
		// pattern: pointer to pattern match
		// start: bytes to reach rel32 from pattern
		// end: bytes to reach the end
		// shift: shift the final address by this many bytes
		static SInt32 GetRel32FromPattern(uintptr_t* pattern, UInt64 start, UInt64 end, UInt64 shift = 0x0)
		{

			if (pattern) {
				SInt32 relish32 = 0;
				if (!ReadMemory(uintptr_t(pattern) + start, &relish32, sizeof(SInt32))) {
					return 0;
				}
				else {
					relish32 = (((uintptr_t(pattern) + end) + relish32) - RelocationManager::s_baseAddr) + (shift);
					return relish32;
				}
			}
			return 0;
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

		// Returns a pointer to the address obtained from a base+offsets
		static uintptr_t* GimmeMultiPointer(uintptr_t baseAddress, UInt32* offsets, UInt32 numOffsets)
		{
			if (!baseAddress || baseAddress == 0) {
				return nullptr;
			}
			uintptr_t address = baseAddress;

			for (UInt32 i = 0; i < numOffsets; i++) {
				address = GetSinglePointer(address, offsets[i]);
				if (!address || address == 0) {
					return nullptr;
				}
			}
			return reinterpret_cast<uintptr_t*>(address);
		}

		// Determine if player is in workshop mode
		static bool InWorkshopMode()
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
		static bool IsWSRefGrabbed()
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
				SafeWriteBuf(uintptr_t(WSMode.addr) + 0x03, Patches.TWO_ZEROS, sizeof(Patches.TWO_ZEROS)); //0000
				SafeWriteBuf(uintptr_t(WSMode.addr) + 0x09, Patches.TWO_ONES, sizeof(Patches.TWO_ONES)); //0101
			}
		}

		// return the currently selected workshop ref with some safety checks
		static TESObjectREFR* GetCurrentWSRef(bool bOnlySelectReferences=1)
		{
			PIR_LOG_PREP
			if (CurrentWSRef.ptr && CurrentWSRef.addr && pir::InWorkshopMode()) {

				uintptr_t* refptr = GimmeMultiPointer(CurrentWSRef.addr, Patches.CurrentWSRef_Offsets, Patches.CurrentWSRef_OffsetsSize);
				TESObjectREFR* ref = (TESObjectREFR*)(refptr);

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

		static void LogWSRef()
		{
			PIR_LOG_PREP
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

			//
			// Pickup / interaction byte
			//
			UInt8 pickup = 0;
			__try { pickup = *(UInt8*)((UInt8*)ref + 0x1A); }
			__except (EXCEPTION_EXECUTE_HANDLER) { pickup = 0xFF; }

			_MESSAGE("PickupType:     0x%02X (ref+0x1A)", pickup);

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

			//
			// LinkedRef (default only)
			//
			TESObjectREFR* linked = nullptr;
			if (GetLinkedRef_Native) {
				__try { linked = GetLinkedRef_Native(ref, nullptr); }
				__except (EXCEPTION_EXECUTE_HANDLER) { linked = nullptr; }
			}
			_MESSAGE("LinkedRef:      %p", linked);

			//
			// VTABLE POINTER + SLOTS
			//
			void** vtbl = nullptr;
			__try { vtbl = *(void***)ref; }
			__except (EXCEPTION_EXECUTE_HANDLER) { vtbl = nullptr; }

			_MESSAGE("VTable Ptr:     %p", vtbl);

			if (vtbl) {
				for (UInt32 i = 0x48; i <= 0xC3; i++) {
					void* fn = nullptr;
					__try { fn = vtbl[i]; }
					__except (EXCEPTION_EXECUTE_HANDLER) { fn = nullptr; }

					_MESSAGE("   VT[%02X]: %p", i, fn);
				}
			}

			_MESSAGE("=============== END DEBUG ===============");
		}


		static void LogWSRef2()
		{
			PIR_LOG_PREP
			TESObjectREFR* ref = GetCurrentWSRef(0);

			_MESSAGE("========== TESObjectREFR DEBUG ==========");

			if (!ref) {
				_MESSAGE("Ref: NULL");
				_MESSAGE("=============== END DEBUG ===============");
				return;
			}

			//
			// Basic identity
			//
			_MESSAGE("Ref:        %p", ref);
			_MESSAGE("FormID:     %08X", ref->formID);
			_MESSAGE("BaseForm:   %p (%08X)", ref->baseForm, ref->baseForm ? ref->baseForm->formID : 0);
			_MESSAGE("Flags:      %04X", ref->flags);

			//
			// Name
			//
			const char* name = CALL_MEMBER_FN(ref, GetReferenceName)();
			_MESSAGE("Name:       %s", name ? name : "(none)");

			//
			// Pickup type (ref + 0x1A)
			//
			UInt8 pickup = *((UInt8*)ref + 0x1A);
			_MESSAGE("PickupType (ref+0x1A): %02X", pickup);

			//
			// Position / Rotation
			//
			_MESSAGE("Pos:        X=%.4f Y=%.4f Z=%.4f", ref->pos.x, ref->pos.y, ref->pos.z);
			_MESSAGE("Rot(rad):   X=%.4f Y=%.4f Z=%.4f", ref->rot.x, ref->rot.y, ref->rot.z);
			_MESSAGE("Rot(deg):   X=%.2f Y=%.2f Z=%.2f",
				ref->rot.x * 57.2957795f,
				ref->rot.y * 57.2957795f,
				ref->rot.z * 57.2957795f);

			//
			// Parent cell / worldspace
			//
			_MESSAGE("ParentCell: %p", ref->parentCell);
			TESWorldSpace* ws = CALL_MEMBER_FN(ref, GetWorldspace)();
			_MESSAGE("Worldspace: %p", ws);

			//
			// Handle ref count
			//
			_MESSAGE("HandleRefObject:");
			_MESSAGE("   RefCount: %u", ref->handleRefObject.QRefCount());

			//
			// ExtraDataList
			//
			_MESSAGE("ExtraDataList: %p", ref->extraDataList);

			if (ref->extraDataList) {
				ExtraDataList* xl = ref->extraDataList;

				_MESSAGE("   ExtraData entries:");
				for (BSExtraData* data = xl->m_data; data; data = data->next) {
					_MESSAGE("      Extra type: %02X  ptr=%p", data->type, data);
				}
			}

			//
			// Loaded 3D data
			//
			_MESSAGE("LoadedData (ref->unkF0): %p", ref->unkF0);
			if (ref->unkF0) {
				_MESSAGE("   RootNode: %p", ref->unkF0->rootNode);
				_MESSAGE("   Flags:    %016llX", ref->unkF0->flags);
			}

			//
			// Inventory
			//
			_MESSAGE("InventoryList: %p", ref->inventoryList);

			//
			// Unk offsets
			//
			_MESSAGE("unk60:   %p", ref->unk60);
			_MESSAGE("unk68:   %p", ref->unk68);
			_MESSAGE("unk70:   %08X", ref->unk70);
			_MESSAGE("unk74:   %08X", ref->unk74);
			_MESSAGE("unk78:   %08X", ref->unk78);
			_MESSAGE("unk7C:   %08X", ref->unk7C);
			_MESSAGE("unk80:   %016llX", ref->unk80);
			_MESSAGE("unk88:   %016llX", ref->unk88);
			_MESSAGE("unk90:   %016llX", ref->unk90);
			_MESSAGE("unk98:   %016llX", ref->unk98);
			_MESSAGE("unkA0:   %016llX", ref->unkA0);
			_MESSAGE("unkA8:   %016llX", ref->unkA8);
			_MESSAGE("unkB0:   %016llX", ref->unkB0);
			_MESSAGE("unkE8:   %p", ref->unkE8);

			//
			// LinkedRef
			//
			_MESSAGE("LinkedRefs:");
			BGSKeyword* kw = nullptr;
			TESObjectREFR* linked = GetLinkedRef_Native(ref, kw);
			_MESSAGE("   DefaultLinkedRef: %p", linked);

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
				SetMotionType_Native(vm, NULL, ref, motion, acti);
				if (sound == 1) {
					PlaySounds.UI_func("UIQuestInactive");
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
					pirlog.FormattedMessage("%04X %f %f %f %f %f %f", fid, tempref->pos.x, tempref->pos.y, tempref->pos.z, tempref->rot.x, tempref->rot.y, tempref->rot.z);
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
			if (gConsole.ptr && gConsole.addr && settings.PrintConsoleMessages)
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
			if (settings.PrintConsoleMessages == true) {
				settings.PrintConsoleMessages = false;
				return true;
			}
			else {
				settings.PrintConsoleMessages = true;
				pir::ConsolePrint("Enabled PIR console print.");
				return true;
			}
			return false;
		}

		// f4se message interface handler
		static void MessageInterfaceHandler(F4SEMessagingInterface::Message* msg)
		{
			switch (msg->type) {

			case F4SEMessagingInterface::kMessage_GameDataReady:
				settings.GameDataIsReady = true;
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
				ScaleFuncs.SetScale(ref, newScale);
				return true;
			}
			return false;
		}

		//Move reference to itself
		static void MoveRefToSelf(float modx = 0, float mody = 0, float modz = 0, int repeat_count = 0)
		{
			PIR_LOG_PREP
				TESObjectREFR* ref = GetCurrentWSRef();
			if (ref) {
				UInt32 nullHandle = *g_invalidRefHandle;
				TESObjectCELL* parentCell = ref->parentCell;
				TESWorldSpace* worldspace = CALL_MEMBER_FN(ref, GetWorldspace)();

				// new position
				NiPoint3 newPos;
				newPos.x = ref->pos.x + modx;
				newPos.y = ref->pos.y + mody;
				newPos.z = ref->pos.z + modz;
				// new rotation
				NiPoint3 newRot;
				newRot.x = ref->rot.x;
				newRot.y = ref->rot.y;
				newRot.z = ref->rot.z;

				for (int i = 0; i <= repeat_count; i++)
				{
					MoveRefrToPosition(ref, &nullHandle, parentCell, worldspace, &newPos, &newRot);
				}

			}
		}

		// Modify the scale of the current workshop reference by a percent.
		static bool ModCurrentRefScale(float fMultiplyAmount)
		{
			PIR_LOG_PREP
				TESObjectREFR* ref = GetCurrentWSRef();
			if (ref) {
				float oldscale = ScaleFuncs.GetScale(ref);
				float newScale = oldscale * (fMultiplyAmount);
				if (newScale > 9.9999f) { newScale = 9.9999f; }
				if (newScale < 0.0001f) { newScale = 0.0001f; }
				ScaleFuncs.SetScale(ref, newScale);

				// fix jitter only if player isnt grabbing the item
				if (IsWSRefGrabbed() == false) {
					pir::MoveRefToSelf(0, 0, 0, 1);
				}

				return true;
			}
			return false;
		}

		/*
	RotateRefByDegrees
	------------------
	Applies incremental Euler rotations to a TESObjectREFR in worldspace.

	Fallout 4 Rotation Notes:
	  • ref->rot stores Euler angles in RADIANS.
	  • Axis mapping (Creation Engine):
			rot.x = pitch (rotation around X axis)
			rot.y = roll  (rotation around Y axis)
			rot.z = yaw   (rotation around Z axis)
	  • Engine composes these in Z-Y-X Tait-Bryan order:
			R = Rz(yaw) * Ry(roll) * Rx(pitch)
		(This defines how rotations accumulate.)
	  • Input deltas are provided in DEGREES, converted to radians here.
	  • Angles are normalized to the range [-pi, pi].
	  • MoveRefrToPosition applies both position AND rotation.

	Parameters:
		dXDeg - pitch delta (degrees, X axis)
		dYDeg - roll  delta (degrees, Y axis)
		dZDeg - yaw   delta (degrees, Z axis)
*/

		static void RotateRefByDegrees(float dXDeg, float dYDeg, float dZDeg)
		{
			TESObjectREFR* ref = GetCurrentWSRef(1);
			if (!ref)
				return;

			// Current worldspace transform
			NiPoint3 pos = ref->pos;   // World position (X/Y/Z)
			NiPoint3 rot = ref->rot;   // Euler rotation in radians (X=pitch, Y=roll, Z=yaw)

			// Convert deltas from degrees to radians
			const float degToRad = (float)(MATH_PI / 180.0);

			// Apply rotation deltas (FO4: X=pitch, Y=roll, Z=yaw)
			rot.x += dXDeg * degToRad;   // pitch (X axis)
			rot.y += dYDeg * degToRad;   // roll  (Y axis)
			rot.z += dZDeg * degToRad;   // yaw   (Z axis)

			// Normalize each component to [-pi, pi]
			auto normalize = [](float a) -> float
				{
					const float twoPi = (float)(MATH_PI * 2.0);
					while (a > MATH_PI) a -= twoPi;
					while (a < -MATH_PI) a += twoPi;
					return a;
				};

			rot.x = normalize(rot.x);
			rot.y = normalize(rot.y);
			rot.z = normalize(rot.z);

			// Resolve worldspace via F4SE
			TESWorldSpace* world = CALL_MEMBER_FN(ref, GetWorldspace)();

			// Apply updated transform
			UInt32* targetHandle = nullptr;
			MoveRefrToPosition(ref, targetHandle, ref->parentCell, world, &pos, &rot);
		}


		// Single-axis helpers for readability and console bindings
		static inline void YawRef(float deg)
		{
			// yaw = Z axis
			RotateRefByDegrees(0.0f, 0.0f, deg);
		}

		static inline void PitchRef(float deg)
		{
			// pitch = X axis
			RotateRefByDegrees(deg, 0.0f, 0.0f);
		}

		static inline void RollRef(float deg)
		{
			// roll = Y axis
			RotateRefByDegrees(0.0f, deg, 0.0f);
		}


		//dump all console and obscript commands to the log file
		static bool DumpCmds()
		{
			PIR_LOG_PREP
			if (FirstConsole.cmd == nullptr || FirstObScript.cmd == nullptr) {
				return false;
			}

			pirlog.FormattedMessage("---------------------------------------------------------");
			pirlog.FormattedMessage("Type|opcode|rel32|address|short|long|params|needsparent|helptext");

			for (ObScriptCommand* iter = FirstConsole.cmd; iter->opcode < (kObScript_NumConsoleCommands + kObScript_ConsoleOpBase); ++iter) {
				if (iter) {
					uintptr_t exeaddr = (uintptr_t) & (iter->execute);
					uint64_t* funcptr = (uint64_t*)(exeaddr);
					uint64_t relly = *funcptr - RelocationManager::s_baseAddr;
					if (*funcptr == 0) {
						relly = 0;
					}
					const char* cleanhelp = StripNewLinesAndPipes(iter->helpText);
					pirlog.FormattedMessage("Console|%06X|Fallout4.exe+0x%08X|%p|%s|%s|%X|%X|%s", iter->opcode, relly, *funcptr, iter->shortName, iter->longName, iter->numParams, iter->needsParent, cleanhelp);
				}
			}

			for (ObScriptCommand* iter = FirstObScript.cmd; iter->opcode < (kObScript_NumObScriptCommands + kObScript_ScriptOpBase); ++iter) {
				if (iter) {
					uintptr_t exeaddr = (uintptr_t) & (iter->execute);
					uint64_t* funcptr = (uint64_t*)(exeaddr);
					uint64_t relly = *funcptr - RelocationManager::s_baseAddr;
					if (*funcptr == 0) {
						relly = 0;
					}
					const char* cleanhelp = StripNewLinesAndPipes(iter->helpText);
					pirlog.FormattedMessage("ObScript|%06X|Fallout4.exe+0x%08X|%p|%s|%s|%X|%X|%s", iter->opcode, relly, *funcptr, iter->shortName, iter->longName, iter->numParams, iter->needsParent, cleanhelp);
				}
			}

			return true;
		}

		//toggle object outlines on next object
		static bool Toggle_Outlines()
		{
			if (Pointers.outlines && settings.OUTLINES_ENABLED) {
				SafeWrite8((uintptr_t)Pointers.outlines + 0x06, 0x00); //objects
				SafeWrite8((uintptr_t)Pointers.outlines + 0x0D, 0xEB); //npcs
				settings.OUTLINES_ENABLED = false;
				pir::ConsolePrint("Object outlines disabled");
				return true;
			}
			if (Pointers.outlines && !settings.OUTLINES_ENABLED) {
				SafeWrite8((uintptr_t)Pointers.outlines + 0x06, 0x01); //objects
				SafeWrite8((uintptr_t)Pointers.outlines + 0x0D, 0x76); //npcs
				settings.OUTLINES_ENABLED = true;
				pir::ConsolePrint("Object outlines enabled");
				return true;
			}
			return false;
		}

		//toggle slower object rotation and zoom speed
		static bool Toggle_SlowZoomAndRotate()
		{
			// its on, turn it off
			if (Zoom.ptr && Rotate.ptr && Zoom.addr && Rotate.addr && settings.SLOW_ENABLED) {
				SafeWriteBuf(Zoom.addr, &settings.fOriginalZOOM, sizeof(Float32));
				SafeWriteBuf(Rotate.addr, &settings.fOriginalROTATE, sizeof(Float32));
				settings.SLOW_ENABLED = false;
				pir::ConsolePrint("Slow zoom/rotate - disabled");
				return true;
			}
			// its off, turn it on
			if (Zoom.ptr && Rotate.ptr && Zoom.addr && Rotate.addr && !settings.SLOW_ENABLED) {
				SafeWriteBuf(Zoom.addr, &settings.fSlowerZOOM, sizeof(Float32));
				SafeWriteBuf(Rotate.addr, &settings.fSlowerROTATE, sizeof(Float32));
				settings.SLOW_ENABLED = true;
				pir::ConsolePrint("Slow zoom/rotate - enabled");
				return true;
			}
			return false;
		}

		//toggle infinite workshop size
		static bool Toggle_WorkshopSize()
		{
			if (WSSize.ptr && settings.WORKSHOPSIZE_ENABLED) {
				SafeWriteBuf((uintptr_t)WSSize.ptr, Patches.DRAWS_OLD, sizeof(Patches.DRAWS_OLD));
				SafeWriteBuf((uintptr_t)WSSize.ptr + 0x0A, Patches.TRIS_OLD, sizeof(Patches.TRIS_OLD));
				settings.WORKSHOPSIZE_ENABLED = false;
				pir::ConsolePrint("Unlimited workshop size disabled");
				return true;
			}

			if (WSSize.ptr && settings.WORKSHOPSIZE_ENABLED == false) {
				// Write nop 6 so its never increased
				SafeWriteBuf((uintptr_t)WSSize.ptr, Patches.NOP6, sizeof(Patches.NOP6));
				SafeWriteBuf((uintptr_t)WSSize.ptr + 0x0A, Patches.NOP6, sizeof(Patches.NOP6));

				// set current ws draws and triangles to zero
				SafeWrite64(WSSize.addr, 0);
				settings.WORKSHOPSIZE_ENABLED = true;
				pir::ConsolePrint("Unlimited workshop size enabled");
				return true;
			}
			return false;
		}

		//toggle groundsnap
		static bool Toggle_GroundSnap()
		{
			if (Pointers.gsnap && settings.GROUNDSNAP_ENABLED) {

				SafeWrite8((uintptr_t)Pointers.gsnap + 0x01, 0x85);
				settings.GROUNDSNAP_ENABLED = false;
				pir::ConsolePrint("Ground snap disabled");
				return true;
			}
			if (Pointers.gsnap && !settings.GROUNDSNAP_ENABLED) {
				SafeWrite8((uintptr_t)Pointers.gsnap + 0x01, 0x86);
				settings.GROUNDSNAP_ENABLED = true;
				pir::ConsolePrint("Ground snap enabled");
				return true;

			}
			return false;
		}

		//toggle objectsnap
		static bool Toggle_ObjectSnap()
		{
			// its on - toggle it off
			if (Pointers.osnap && settings.OBJECTSNAP_ENABLED) {
				SafeWriteBuf((uintptr_t)Pointers.osnap, Patches.OSNAP_NEW, sizeof(Patches.OSNAP_NEW));
				settings.OBJECTSNAP_ENABLED = false;
				pir::ConsolePrint("Object snap disabled");
				return true;
			}
			// its off - toggle it on
			if (Pointers.osnap && !settings.OBJECTSNAP_ENABLED) {
				SafeWriteBuf((uintptr_t)Pointers.osnap, Patches.OSNAP_OLD, sizeof(Patches.OSNAP_OLD));
				settings.OBJECTSNAP_ENABLED = true;
				pir::ConsolePrint("Object snap enabled");
				return true;
			}
			return false;
		}

		//toggle allowing achievements with mods
		static bool Toggle_Achievements()
		{
			// its on - toggle it off
			if (Pointers.achievements && settings.ACHIEVEMENTS_ENABLED) {
				SafeWriteBuf((uintptr_t)Pointers.achievements, Patches.ACHIEVE_OLD, sizeof(Patches.ACHIEVE_OLD));
				settings.ACHIEVEMENTS_ENABLED = false;
				pir::ConsolePrint("Achievements with mods disabled (game default)");
				return true;
			}
			// its off - toggle it on
			if (Pointers.achievements && !settings.ACHIEVEMENTS_ENABLED) {
				SafeWriteBuf((uintptr_t)Pointers.achievements, Patches.ACHIEVE_NEW, sizeof(Patches.ACHIEVE_NEW));
				settings.ACHIEVEMENTS_ENABLED = true;
				pir::ConsolePrint("Achievements with mods enabled!");
				return true;
			}
			return false;
		}

		// toggle consolenameref
		static bool Toggle_CNameRef()
		{
			if (CNameRef.goodfinder == false || CNameRef.call == false) {
				return false;
			}

			// toggle off
			if (settings.ConsoleNameRef_ENABLED)
			{
				SafeWriteBuf(uintptr_t(CNameRef.call), Patches.CNameRef_OLD, Patches.CNameRef_OLD_Size);
				settings.ConsoleNameRef_ENABLED = false;
				pir::ConsolePrint("ConsoleRefName toggled off.");
				return true;
			}

			// toggle on
			if (!settings.ConsoleNameRef_ENABLED)
			{
				SafeWriteCall(uintptr_t(CNameRef.call), CNameRef.goodfunc); //patch call
				SafeWrite8(uintptr_t(CNameRef.call) + 0x05, 0x90); // for a clean patch
				settings.ConsoleNameRef_ENABLED = true;
				pir::ConsolePrint("ConsoleRefName toggled on.");
				return true;
			}

			return false;
		}

		static bool ToggleWorkbenchMove()
		{
			if (WorkbenchSelection.ptr && WorkbenchSelection.addr) {

				UInt8 AllowSelect1F = 0x00; //00 in the vtable means we can select it
				ReadMemory(uintptr_t(WorkbenchSelection.addr + 0x05), &AllowSelect1F, sizeof(UInt8)); //whats the current value
				
				// game default - disable selecting workbench
				if (AllowSelect1F == 0x00) {
					SafeWrite8((uintptr_t)WorkbenchSelection.addr + 0x05, 0x01);
					pir::ConsolePrint("Workbench move disabled (game default)");
					return true;
				}

				// allow moving workbench
				if (AllowSelect1F == 0x01) {
					SafeWrite8((uintptr_t)WorkbenchSelection.addr + 0x05, 0x00); // allow selecting and storing workbench
					pir::ConsolePrint("Workbench move allowed!");
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
			if (settings.PLACEINRED_ENABLED) {
				SafeWrite8((uintptr_t)Pointers.A + 0x06, 0x01);
				SafeWrite8((uintptr_t)Pointers.A + 0x0C, 0x02);
				SafeWrite8((uintptr_t)Pointers.B + 0x01, 0x01);
				SafeWriteBuf((uintptr_t)Pointers.C, Patches.C_OLD, sizeof(Patches.C_OLD));
				SafeWriteBuf((uintptr_t)Pointers.C + 0x11, Patches.CC_OLD, sizeof(Patches.CC_OLD));
				SafeWrite8((uintptr_t)Pointers.C + 0x1D, 0x01);
				SafeWriteBuf((uintptr_t)Pointers.D, Patches.D_OLD, sizeof(Patches.D_OLD));
				SafeWrite8((uintptr_t)Pointers.E + 0x00, 0x76);
				SafeWriteBuf((uintptr_t)Pointers.F, Patches.F_OLD, sizeof(Patches.F_OLD));
				SafeWrite8((uintptr_t)Pointers.G + 0x01, 0x95);
				SafeWrite8((uintptr_t)Pointers.H + 0x00, 0x74);
				SafeWriteBuf((uintptr_t)Pointers.J, Patches.J_OLD, sizeof(Patches.J_OLD));
				SafeWrite8((uintptr_t)Pointers.R + 0xC, 0x01); //red
				SafeWriteBuf((uintptr_t)Pointers.Y, Patches.Y_OLD, sizeof(Patches.Y_OLD)); 
				SafeWriteBuf((uintptr_t)Pointers.wstimer, Patches.TIMER_OLD, sizeof(Patches.TIMER_OLD)); 
				settings.PLACEINRED_ENABLED = false;
				pir::ConsolePrint("Place in Red disabled.");
				return true;
			}

			// toggle on
			if (!settings.PLACEINRED_ENABLED) {
				SafeWrite8((uintptr_t)Pointers.A + 0x06, 0x00);
				SafeWrite8((uintptr_t)Pointers.A + 0x0C, 0x01);
				SafeWrite8((uintptr_t)Pointers.B + 0x01, 0x00);
				SafeWriteBuf((uintptr_t)Pointers.C, Patches.C_NEW, sizeof(Patches.C_NEW)); // movzx eax,byte ptr [Fallout4.exe+2E74998]
				SafeWriteBuf((uintptr_t)Pointers.C + 0x11, Patches.CC_NEW, sizeof(Patches.CC_NEW));
				SafeWrite8((uintptr_t)Pointers.C + 0x1D, 0x00);
				SafeWriteBuf((uintptr_t)Pointers.D, Patches.D_NEW, sizeof(Patches.D_NEW));
				SafeWrite8((uintptr_t)Pointers.E + 0x00, 0xEB);
				SafeWriteBuf((uintptr_t)Pointers.F, Patches.NOP6, sizeof(Patches.NOP6)); 
				SafeWrite8((uintptr_t)Pointers.G + 0x01, 0x98); // works but look at again later
				SafeWrite8((uintptr_t)Pointers.H + 0x00, 0xEB);
				SafeWriteBuf((uintptr_t)Pointers.J, Patches.J_NEW, sizeof(Patches.J_NEW)); //water or other restrictions
				SafeWrite8((uintptr_t)Pointers.R + 0x0C, 0x00); // red to green
				SafeWriteBuf((uintptr_t)Pointers.Y, Patches.NOP3, sizeof(Patches.NOP3)); // move yellow
				SafeWriteBuf((uintptr_t)Pointers.wstimer, Patches.TIMER_NEW, sizeof(Patches.TIMER_NEW)); // timer
				SetCorrectBytes();
				settings.PLACEINRED_ENABLED = true;
				pir::ConsolePrint("Place In Red enabled.");
				return true;
			}

			return false;
		}

		// play sound by filename. must be under data\sounds
		static void PlayFileSound(const char* wav)
		{
			if (PlaySounds.File_func) {
				PlaySounds.File_func(wav);
			}
		}

		// play UI sound using form name
		static void PlayUISound(const char* sound)
		{
			if (PlaySounds.UI_func) {
				PlaySounds.UI_func(sound);
			}
		}

		//called every time the console command runs
		static bool ExecuteConsole(void* paramInfo, void* scriptData, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, double* result, void* opcodeOffsetPtr)
		{
			PIR_LOG_PREP
			if (GetConsoleArg_Native && GetConsoleArg.ptr && (GetConsoleArg.r32 != 0)) {
				char param1[4096];
				char param2[4096];
				bool consoleresult = GetConsoleArg_Native(paramInfo, scriptData, opcodeOffsetPtr, thisObj, containingObj, scriptObj, locals, &param1, &param2);

				if (consoleresult && param1[0]) {
					switch (ConsoleSwitch(param1)) {
						// debug and tests
						case pir::ConsoleSwitch("dumprefs"):     pir::DumpCellRefs();         break;
						case pir::ConsoleSwitch("dumpcmds"):     pir::DumpCmds();             break;
						case pir::ConsoleSwitch("logref"):       pir::LogWSRef();             break;
						case pir::ConsoleSwitch("print"):        pir::Toggle_ConsolePrint();  break;
						case pir::ConsoleSwitch("sound"):        pir::PlayFileSound(param2);  break;
						case pir::ConsoleSwitch("uisound"):      pir::PlayUISound(param2);    break;

						//toggles
						case pir::ConsoleSwitch("1"):             pir::Toggle_PlaceInRed();         break;
						case pir::ConsoleSwitch("toggle"):        pir::Toggle_PlaceInRed();         break;
						case pir::ConsoleSwitch("2"):             pir::Toggle_ObjectSnap();         break;
						case pir::ConsoleSwitch("osnap"):         pir::Toggle_ObjectSnap();         break;
						case pir::ConsoleSwitch("3"):             pir::Toggle_GroundSnap();         break;
						case pir::ConsoleSwitch("gsnap"):         pir::Toggle_GroundSnap();         break;
						case pir::ConsoleSwitch("4"):             pir::Toggle_SlowZoomAndRotate();  break;
						case pir::ConsoleSwitch("slow"):          pir::Toggle_SlowZoomAndRotate();  break;
						case pir::ConsoleSwitch("5"):             pir::Toggle_WorkshopSize();       break;
						case pir::ConsoleSwitch("workshopsize"):  pir::Toggle_WorkshopSize();       break;
						case pir::ConsoleSwitch("6"):             pir::Toggle_Outlines();           break;
						case pir::ConsoleSwitch("outlines"):      pir::Toggle_Outlines();           break;
						case pir::ConsoleSwitch("7"):             pir::Toggle_Achievements();       break;
						case pir::ConsoleSwitch("achievements"):  pir::Toggle_Achievements();       break;
						case pir::ConsoleSwitch("wb"):            pir::ToggleWorkbenchMove();       break;

						//scale constants
						case pir::ConsoleSwitch("scale1"):       pir::SetCurrentRefScale(1.0000f); break;
						case pir::ConsoleSwitch("scale10"):      pir::SetCurrentRefScale(9.9999f); break;

						// yaw (Z)
						case pir::ConsoleSwitch("y0.1"):  pir::YawRef(0.1f);  break;
						case pir::ConsoleSwitch("y0.5"):  pir::YawRef(0.5f);  break;
						case pir::ConsoleSwitch("y1"):    pir::YawRef(1.0f);  break;
						case pir::ConsoleSwitch("y2"):    pir::YawRef(2.0f);  break;
						case pir::ConsoleSwitch("y5"):    pir::YawRef(5.0f);  break;
						case pir::ConsoleSwitch("y10"):   pir::YawRef(10.0f); break;
						case pir::ConsoleSwitch("y15"):   pir::YawRef(15.0f); break;
						case pir::ConsoleSwitch("y30"):   pir::YawRef(30.0f); break;

						// yaw negative
						case pir::ConsoleSwitch("y-0.1"): pir::YawRef(-0.1f); break;
						case pir::ConsoleSwitch("y-0.5"): pir::YawRef(-0.5f); break;
						case pir::ConsoleSwitch("y-1"):   pir::YawRef(-1.0f); break;
						case pir::ConsoleSwitch("y-5"):   pir::YawRef(-5.0f); break;
						case pir::ConsoleSwitch("y-10"):  pir::YawRef(-10.0f); break;

						// roll (Y)
						case pir::ConsoleSwitch("r0.1"):  pir::RollRef(0.1f);  break;
						case pir::ConsoleSwitch("r0.5"):  pir::RollRef(0.5f);  break;
						case pir::ConsoleSwitch("r1"):    pir::RollRef(1.0f);  break;
						case pir::ConsoleSwitch("r2"):    pir::RollRef(2.0f);  break;
						case pir::ConsoleSwitch("r5"):    pir::RollRef(5.0f);  break;
						case pir::ConsoleSwitch("r10"):   pir::RollRef(10.0f); break;
						case pir::ConsoleSwitch("r15"):   pir::RollRef(15.0f); break;
						case pir::ConsoleSwitch("r30"):   pir::RollRef(30.0f); break;

						// roll negative
						case pir::ConsoleSwitch("r-0.1"): pir::RollRef(-0.1f); break;
						case pir::ConsoleSwitch("r-0.5"): pir::RollRef(-0.5f); break;
						case pir::ConsoleSwitch("r-1"):   pir::RollRef(-1.0f); break;
						case pir::ConsoleSwitch("r-5"):   pir::RollRef(-5.0f); break;
						case pir::ConsoleSwitch("r-10"):  pir::RollRef(-10.0f); break;

						// pitch (X)
						case pir::ConsoleSwitch("p0.1"):  pir::PitchRef(0.1f);  break;
						case pir::ConsoleSwitch("p0.5"):  pir::PitchRef(0.5f);  break;
						case pir::ConsoleSwitch("p1"):    pir::PitchRef(1.0f);  break;
						case pir::ConsoleSwitch("p2"):    pir::PitchRef(2.0f);  break;
						case pir::ConsoleSwitch("p5"):    pir::PitchRef(5.0f);  break;
						case pir::ConsoleSwitch("p10"):   pir::PitchRef(10.0f); break;
						case pir::ConsoleSwitch("p15"):   pir::PitchRef(15.0f); break;
						case pir::ConsoleSwitch("p30"):   pir::PitchRef(30.0f); break;

						// pitch negative
						case pir::ConsoleSwitch("p-0.1"): pir::PitchRef(-0.1f); break;
						case pir::ConsoleSwitch("p-0.5"): pir::PitchRef(-0.5f); break;
						case pir::ConsoleSwitch("p-1"):   pir::PitchRef(-1.0f); break;
						case pir::ConsoleSwitch("p-5"):   pir::PitchRef(-5.0f); break;
						case pir::ConsoleSwitch("p-10"):  pir::PitchRef(-10.0f); break;

						//scale up							  				     
						case pir::ConsoleSwitch("scaleup1"):	 pir::ModCurrentRefScale(1.0100f); break;
						case pir::ConsoleSwitch("scaleup2"):	 pir::ModCurrentRefScale(1.0200f); break;
						case pir::ConsoleSwitch("scaleup5"):	 pir::ModCurrentRefScale(1.0500f); break;
						case pir::ConsoleSwitch("scaleup10"):	 pir::ModCurrentRefScale(1.1000f); break;
						case pir::ConsoleSwitch("scaleup25"):	 pir::ModCurrentRefScale(1.2500f); break;
						case pir::ConsoleSwitch("scaleup50"):	 pir::ModCurrentRefScale(1.5000f); break;
						case pir::ConsoleSwitch("scaleup100"):	 pir::ModCurrentRefScale(2.0000f); break;

						//scale down			   			  				        			 
						case pir::ConsoleSwitch("scaledown1"):	 pir::ModCurrentRefScale(0.9900f); break;
						case pir::ConsoleSwitch("scaledown2"):	 pir::ModCurrentRefScale(0.9800f); break;
						case pir::ConsoleSwitch("scaledown5"):	 pir::ModCurrentRefScale(0.9500f); break;
						case pir::ConsoleSwitch("scaledown10"):  pir::ModCurrentRefScale(0.9000f); break;
						case pir::ConsoleSwitch("scaledown25"):  pir::ModCurrentRefScale(0.7500f); break;
						case pir::ConsoleSwitch("scaledown50"):  pir::ModCurrentRefScale(0.5000f); break;
						case pir::ConsoleSwitch("scaledown75"):	 pir::ModCurrentRefScale(0.2500f); break;

						// lock and unlock
						case pir::ConsoleSwitch("lock"):         pir::LockUnlockWSRef(0, 1); break;
						case pir::ConsoleSwitch("l"):            pir::LockUnlockWSRef(0, 1); break;
						case pir::ConsoleSwitch("lockq"):        pir::LockUnlockWSRef(0, 0); break;
						case pir::ConsoleSwitch("unlock"):       pir::LockUnlockWSRef(1, 0); break;
						case pir::ConsoleSwitch("u"):            pir::LockUnlockWSRef(1, 0); break;


						// console name ref toggle
						case pir::ConsoleSwitch("cnref"):        pir::Toggle_CNameRef(); break;


						// show help
						case pir::ConsoleSwitch("?"):            pir::ConsolePrint(ConsoleHelpMSG); break;
						case pir::ConsoleSwitch("help"):         pir::ConsolePrint(ConsoleHelpMSG); break;

						default: pir::ConsolePrint(ConsoleHelpMSG);  break;
					}

					return true;
				}

			}
			pirlog.FormattedMessage("[%s] Failed to execute the console command!", thisfunc);
			return false;
		}

		//attempt to create the console command
		static bool CreateConsole(const char* hijacked_cmd_fullname)
		{
			PIR_LOG_PREP
				pirlog.FormattedMessage("[%s] Creating console command.", thisfunc);

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
				cmd.execute = pir::ExecuteConsole;
				cmd.params = allParams;
				cmd.flags = 0;
				cmd.eval;

				SafeWriteBuf((uintptr_t)s_hijackedCommand, &cmd, sizeof(cmd));
				return true;
			}
			return false;
		}

		//did we find all the required memory patterns?
		static bool FoundPatterns()
		{
			PIR_LOG_PREP
				if (
					GetConsoleArg.ptr && FirstConsole.ptr && FirstObScript.ptr
					&& ScaleFuncs.setpattern && ScaleFuncs.getpattern && CurrentWSRef.ptr
					&& WSMode.ptr && gConsole.ptr && Pointers.A && Pointers.B && Pointers.C
					&& Pointers.D && Pointers.E && Pointers.F && Pointers.G && Pointers.H
					&& Pointers.J && Pointers.Y && Pointers.R && Pointers.CORRECT
					&& Pointers.wstimer && Pointers.gsnap && Pointers.osnap && Pointers.outlines
					&& WSSize.ptr && Zoom.ptr && Rotate.ptr && SetMotionType.ptr && WorkbenchSelection.ptr
					)
				{
					/*
					 allow plugin to load even if these arent found:

					 Pointers.achievements - not a showstopper
					 ConsoleRefCallFinder - copy of another mod never required
					 GDataHandlerFinder - not using yet
					 AngleFunctions.modAngleX - probably wont ever use
					 AngleFunctions.modAngleY - probably wont ever use
					 AngleFunctions.modAngleZ - probably wont ever use

					*/

					return true;
				}
				else {
					pirlog.FormattedMessage("[%s] Couldnt find required memory patterns! Check for conflicting mods.", thisfunc);
					return false;
				}
		}

		//log all the memory patterns to the log file
		static void LogPatterns()
		{
			pirlog.FormattedMessage("----------------------------------------------------------------------------");
			pirlog.FormattedMessage("Base          :%p|Fallout4.exe+0x00000000", RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("achievements  :%p|Fallout4.exe+0x%08X", Pointers.achievements, (uintptr_t)Pointers.achievements - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("A             :%p|Fallout4.exe+0x%08X", Pointers.A, (uintptr_t)Pointers.A - RelocationManager::s_baseAddr );
			pirlog.FormattedMessage("B             :%p|Fallout4.exe+0x%08X", Pointers.B, (uintptr_t)Pointers.B - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("C             :%p|Fallout4.exe+0x%08X (OLD: %02X%02X%02X%02X%02X%02X%02X)", Pointers.C, (uintptr_t)Pointers.C - RelocationManager::s_baseAddr, Patches.C_OLD[0], Patches.C_OLD[1], Patches.C_OLD[2], Patches.C_OLD[3], Patches.C_OLD[4], Patches.C_OLD[5], Patches.C_OLD[6]);
			pirlog.FormattedMessage("D             :%p|Fallout4.exe+0x%08X (OLD: %02X%02X%02X%02X%02X%02X%02X)", Pointers.D, (uintptr_t)Pointers.D - RelocationManager::s_baseAddr, Patches.D_OLD[0], Patches.D_OLD[1], Patches.D_OLD[2], Patches.D_OLD[3], Patches.D_OLD[4], Patches.D_OLD[5], Patches.D_OLD[6]);
			pirlog.FormattedMessage("E             :%p|Fallout4.exe+0x%08X", Pointers.E, (uintptr_t)Pointers.E - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("F             :%p|Fallout4.exe+0x%08X", Pointers.F, (uintptr_t)Pointers.F - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("G             :%p|Fallout4.exe+0x%08X", Pointers.G, (uintptr_t)Pointers.G - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("H             :%p|Fallout4.exe+0x%08X", Pointers.H, (uintptr_t)Pointers.H - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("J             :%p|Fallout4.exe+0x%08X", Pointers.J, (uintptr_t)Pointers.J - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("Y             :%p|Fallout4.exe+0x%08X", Pointers.Y, (uintptr_t)Pointers.Y - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("R             :%p|Fallout4.exe+0x%08X", Pointers.R, (uintptr_t)Pointers.R - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("RC            :%p|Fallout4.exe+0x%08X", Pointers.RC, (uintptr_t)Pointers.RC - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("CORRECT       :%p|Fallout4.exe+0x%08X", Pointers.CORRECT, (uintptr_t)Pointers.CORRECT - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("wstimer       :%p|Fallout4.exe+0x%08X", Pointers.wstimer, (uintptr_t)Pointers.wstimer - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("gsnap         :%p|Fallout4.exe+0x%08X", Pointers.gsnap, (uintptr_t)Pointers.gsnap - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("osnap         :%p|Fallout4.exe+0x%08X", Pointers.osnap, (uintptr_t)Pointers.osnap - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("outlines      :%p|Fallout4.exe+0x%08X", Pointers.outlines, (uintptr_t)Pointers.outlines - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("FirstConsole  :%p|Fallout4.exe+0x%08X", FirstConsole.ptr, FirstConsole.r32);
			pirlog.FormattedMessage("FirstObScript :%p|Fallout4.exe+0x%08X", FirstObScript.ptr, FirstObScript.r32);
			pirlog.FormattedMessage("GetConsoleArg :%p|Fallout4.exe+0x%08X", GetConsoleArg.ptr, GetConsoleArg.r32);
			pirlog.FormattedMessage("GetScale      :%p|Fallout4.exe+0x%08X", ScaleFuncs.getpattern, ScaleFuncs.g32);
			pirlog.FormattedMessage("SetScale      :%p|Fallout4.exe+0x%08X", ScaleFuncs.setpattern, ScaleFuncs.s32);
			//pirlog.FormattedMessage("ModAngleX     :%p|Fallout4.exe+0x%08X", AngleFunctions.modAngleXAddr, AngleFunctions.modAngleXRel32);
			//pirlog.FormattedMessage("ModAngleY     :%p|Fallout4.exe+0x%08X", AngleFunctions.modAngleYAddr, AngleFunctions.modAngleYRel32);
			//pirlog.FormattedMessage("ModAngleZ     :%p|Fallout4.exe+0x%08X", AngleFunctions.modAngleZAddr, AngleFunctions.modAngleZRel32);
			pirlog.FormattedMessage("SetMotionType :%p|Fallout4.exe+0x%08X", SetMotionType.ptr, SetMotionType.r32);
			pirlog.FormattedMessage("PlayFileSound :%p|Fallout4.exe+0x%08X", PlaySounds.Filepattern, PlaySounds.File_r32);
			pirlog.FormattedMessage("PlayUISound   :%p|Fallout4.exe+0x%08X", PlaySounds.UIpattern, PlaySounds.UI_r32);
			pirlog.FormattedMessage("WSMode        :%p|Fallout4.exe+0x%08X", WSMode.ptr, WSMode.r32);
			pirlog.FormattedMessage("CurrentWSRef  :%p|Fallout4.exe+0x%08X", CurrentWSRef.ptr, CurrentWSRef.r32);
			pirlog.FormattedMessage("GConsole      :%p|Fallout4.exe+0x%08X", gConsole.ptr, gConsole.r32);
			pirlog.FormattedMessage("WBSelect      :%p|Fallout4.exe+0x%08X", WorkbenchSelection.ptr, WorkbenchSelection.r32);
			pirlog.FormattedMessage("WSSize        :%p|Fallout4.exe+0x%08X", WSSize.ptr, (uintptr_t)WSSize.ptr - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("WSFloats      :%p|Fallout4.exe+0x%08X", WSSize.addr, WSSize.addr - RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("Rotate        :%p|%p|orig %f|slow %f", Rotate.ptr, Rotate.addr, settings.fOriginalROTATE, settings.fSlowerROTATE);
			pirlog.FormattedMessage("Zoom          :%p|%p|orig %f|slow %f", Zoom.ptr, Zoom.addr, settings.fOriginalZOOM, settings.fSlowerZOOM);
			pirlog.FormattedMessage("----------------------------------------------------------------------------");
		}

		//grok generated - ini reader v2
		static void ReadINI()
		{
			PIR_LOG_PREP
			pirlog.FormattedMessage("[%s] Reading PlaceInRed.ini settings...", thisfunc);

			const char* section = "Main";

			// Helper lambda for the majority of toggle settings
			auto ApplyToggle = [&](const char* key, bool& currentFlag, auto toggleFunc, bool defaultEnabled)
				{
					std::string val = GetINIString(section, key);
					bool wantEnabled = StringToBool(val, defaultEnabled);

					if (wantEnabled != currentFlag)
					{
						toggleFunc();   // this flips the flag and applies the patch
						pirlog.FormattedMessage("[INI] %s = %d (toggled)", key, wantEnabled);
					}
					else if (!val.empty())
					{
						pirlog.FormattedMessage("[INI] %s = %d (no change)", key, wantEnabled);
					}
				};

			// ------------------- Boolean toggles -------------------
			ApplyToggle("PLACEINRED_ENABLED", settings.PLACEINRED_ENABLED, pir::Toggle_PlaceInRed, false);
			ApplyToggle("OBJECTSNAP_ENABLED", settings.OBJECTSNAP_ENABLED, pir::Toggle_ObjectSnap, true);
			ApplyToggle("GROUNDSNAP_ENABLED", settings.GROUNDSNAP_ENABLED, pir::Toggle_GroundSnap, true);
			ApplyToggle("WORKSHOPSIZE_ENABLED", settings.WORKSHOPSIZE_ENABLED, pir::Toggle_WorkshopSize, false);
			ApplyToggle("OUTLINES_ENABLED", settings.OUTLINES_ENABLED, pir::Toggle_Outlines, true);
			ApplyToggle("ACHIEVEMENTS_ENABLED", settings.ACHIEVEMENTS_ENABLED, pir::Toggle_Achievements, false);
			ApplyToggle("ConsoleNameRef_ENABLED", settings.ConsoleNameRef_ENABLED, pir::Toggle_CNameRef, false);

			// ------------------- PrintConsoleMessages -------------------
			{
				std::string val = GetINIString(section, "PrintConsoleMessages", "1");
				bool want = StringToBool(val, true);
				if (want != settings.PrintConsoleMessages)
				{
					settings.PrintConsoleMessages = want;
					pirlog.FormattedMessage("[INI] PrintConsoleMessages = %d", want);
				}
			}

			// ------------------- Custom slow speeds (must be read BEFORE the toggle) -------------------
			{
				std::string rotStr = GetINIString(section, "fSlowerROTATE");
				if (!rotStr.empty())
				{
					float f = FloatFromString(rotStr, 0.01f, 50.0f, 0.0f);
					if (f > 0.0f)
						settings.fSlowerROTATE = f;
					else
					{
						pirlog.FormattedMessage("[INI] fSlowerROTATE invalid, using default 0.5");
						settings.fSlowerROTATE = 0.5f;
					}
				}

				std::string zoomStr = GetINIString(section, "fSlowerZOOM");
				if (!zoomStr.empty())
				{
					float f = FloatFromString(zoomStr, 0.01f, 50.0f, 0.0f);
					if (f > 0.0f)
						settings.fSlowerZOOM = f;
					else
					{
						pirlog.FormattedMessage("[INI] fSlowerZOOM invalid, using default 1.0");
						settings.fSlowerZOOM = 1.0f;
					}
				}
			}

			// ------------------- Slow zoom/rotate toggle (applies the speeds we just read) -------------------
			{
				std::string val = GetINIString(section, "SLOW_ENABLED");
				bool wantSlow = StringToBool(val, false);
				if (wantSlow != settings.SLOW_ENABLED)
				{
					pir::Toggle_SlowZoomAndRotate();
					pirlog.FormattedMessage("[INI] SLOW_ENABLED = %d (applied custom speeds)", wantSlow);
				}
			}

			pirlog.FormattedMessage("[%s] Finished reading INI.", thisfunc);
		}

		//init f4se stuff and return false if anything fails
		static bool InitF4SE(const F4SEInterface* f4se)
		{
			PIR_LOG_PREP

			// get a plugin handle
			pirPluginHandle = f4se->GetPluginHandle();
			if (!pirPluginHandle) {
				pirlog.FormattedMessage("[%s] Couldn't get a plugin handle!", thisfunc);
				return false;
			}
			pirlog.FormattedMessage("[%s] Got a plugin handle.", thisfunc);

			// set messaging interface
			g_messaging = (F4SEMessagingInterface*)f4se->QueryInterface(kInterface_Messaging);
			if (!g_messaging) {
				pirlog.FormattedMessage("[%s] Failed to set messaging interface!", thisfunc);
				return false;
			}

			/* object interface
			g_object = (F4SEObjectInterface*)f4se->QueryInterface(kInterface_Object);
			if (!g_object) {
				pirlog.FormattedMessage("[%s] Failed to set object interface!", thisfunc);
				return false;
			}
			*/

			// message interface
			if (g_messaging->RegisterListener(pirPluginHandle, "F4SE", pir::MessageInterfaceHandler) == false) {
				pirlog.FormattedMessage("[%s] Failed to register message listener!", thisfunc);
				return false;
			}

			pirlog.FormattedMessage("[%s] F4SE interfaces are set.", thisfunc);
			return true;

		}

		//search for required memory patterns. store old bytes. relocate functions. etc.
		static void InitPIR()
		{
			CNameRef.call = Utility::pattern("FF 90 D0 01 00 00 48 89 74 24 40 4C 8D 05 ? ? ? ? 4C").count(1).get(0).get<uintptr_t>();
			CNameRef.goodfinder = Utility::pattern("E8 ? ? ? ? 4C 8B 05 ? ? ? ? 48 8D 4C 24 40 4C 8B C8 BA 00 01 00 00 E8 ? ? ? ? 83").count(1).get(0).get<uintptr_t>();
			CurrentWSRef.ptr = Utility::pattern("48 8B 1D ? ? ? ? 4C 8D 24 C3 49 3B DC 0F 84 ? ? ? ? 66").count(1).get(0).get<uintptr_t>(); //has address leading to current WS ref
			Pointers.A = Utility::pattern("C6 05 ? ? ? ? 01 84 C0 75 A9 B1 02").count(1).get(0).get<uintptr_t>();
			Pointers.B = Utility::pattern("B2 01 88 15 ? ? ? ? EB 04 84 D2 74 07").count(1).get(0).get<uintptr_t>();
			Pointers.C = Utility::pattern("0F B6 05 ? ? ? ? 44 0F B6 A5 ? ? ? ? 84 C0 75").count(1).get(0).get<uintptr_t>();
			Pointers.D = Utility::pattern("0F B6 05 ? ? ? ? 3A 05 ? ? ? ? 48 8B B4 24 ? ? ? ? C6 05").count(1).get(0).get<uintptr_t>();
			Pointers.E = Utility::pattern("76 0C C6 05 ? ? ? ? 06 E9 ? ? ? ? 40 84 ED 0F 84").count(1).get(0).get<uintptr_t>();
			Pointers.F = Utility::pattern("88 05 ? ? ? ? E8 ? ? ? ? 84 C0 75 ? 80 3D ? ? ? ? 02").count(1).get(0).get<uintptr_t>();
			Pointers.G = Utility::pattern("0F 95 05 ? ? ? ? E8 ? ? ? ? 40 38 35 ? ? ? ? 48").count(1).get(0).get<uintptr_t>();
			Pointers.H = Utility::pattern("74 11 40 84 F6 74 0C 48 8D ? ? 49 8B ? E8").count(1).get(0).get<uintptr_t>();
			Pointers.J = Utility::pattern("74 35 48 8B B5 ? ? ? ? 48 8B CE E8 ? ? ? ? 84 C0 75").count(1).get(0).get<uintptr_t>(); // ignore water restrictions
			Pointers.CORRECT = Utility::pattern("C6 05 ? ? ? ? 01 40 84 F6 74 09 80 3D ? ? ? ? 00 75 ? 80 3D").count(1).get(0).get<uintptr_t>();
			Pointers.achievements = Utility::pattern("48 83 EC 28 C6 44 24 ? 00 84 D2 74 1C 48").count(1).get(0).get<uintptr_t>();
			Pointers.gsnap = Utility::pattern("0F 86 ? ? ? ? 41 8B 4E 34 49 B8").count(1).get(0).get<uintptr_t>();
			Pointers.osnap = Utility::pattern("F3 0F 10 35 ? ? ? ? 0F 28 C6 48 ? ? ? ? ? ? 33 C0").count(1).get(0).get<uintptr_t>();
			Pointers.outlines = Utility::pattern("C6 05 ? ? ? ? 01 88 15 ? ? ? ? 76 13 48 8B 05").count(1).get(0).get<uintptr_t>();
			Pointers.R = Utility::pattern("89 05 ? ? ? ? C6 05 ? ? ? ? 01 48 83 C4 68 C3").count(1).get(0).get<uintptr_t>(); //keep objects green
			Pointers.RC = Utility::pattern("E8 ? ? ? ? 83 3D ? ? ? ? 00 0F 87 ? ? ? ? 48 8B 03 48 8B CB FF 90 ? ? ? ? 48").count(1).get(0).get<uintptr_t>(); // prevent function from being called after R is patched
			Pointers.wstimer = Utility::pattern("0F 85 AB 00 00 00 F3 0F 10 05 ? ? ? ? 41 0F 2E").count(1).get(0).get<uintptr_t>(); // Shortened to match xbox version
			Pointers.Y = Utility::pattern("8B 58 14 48 8D 4C 24 30 8B D3 45 33 C0 E8").count(1).get(0).get<uintptr_t>(); // allow moving Y objects
			FirstConsole.ptr = Utility::pattern("48 8D 1D ? ? ? ? 48 8D 35 ? ? ? ? 66 90 48 8B 53 F8").count(1).get(0).get<uintptr_t>();
			FirstObScript.ptr = Utility::pattern("48 8D 1D ? ? ? ? 4C 8D 35 ? ? ? ? 0F 1F 40 00 0F 1F 84 00 00 00 00 00").count(1).get(0).get<uintptr_t>();
			GetConsoleArg.ptr = Utility::pattern("4C 89 4C 24 20 48 89 4C 24 08 53 55 56 57 41 54 41 55 41 56 41 57").count(1).get(0).get<uintptr_t>();
			Rotate.ptr = Utility::pattern("F3 0F 10 05 ? ? ? ? ? ? ? ? ? ? ? ? 84 C9 75 07 0F 57 05").count(1).get(0).get<uintptr_t>(); //better compatibility with high physics fps
			ScaleFuncs.getpattern = Utility::pattern("66 89 BB 08 01 00 00 E8 ? ? ? ? 48 8B 0D ? ? ? ? 0F 28 F0 48").count(1).get(0).get<uintptr_t>(); //getscale
			ScaleFuncs.setpattern = Utility::pattern("E8 ? ? ? ? 40 84 F6 75 07 81 63 10 FF FF DF FF 33 ED").count(1).get(0).get<uintptr_t>(); //setscale
			PlaySounds.Filepattern = Utility::pattern("48 8B C4 48 89 58 08 57 48 81 EC 50 01 00 00 8B FA C7 40 18 FF FF FF FF 48").count(1).get(0).get<uintptr_t>();
			PlaySounds.UIpattern = Utility::pattern("48 89 5C 24 08 57 48 83 EC 50 48 8B D9 E8 ? ? ? ? 48 85 C0 74 6A").count(1).get(0).get<uintptr_t>();
			SetMotionType.ptr = Utility::pattern("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 50 45 32 E4 41 8D 41 FF").count(1).get(0).get<uintptr_t>();
			WSMode.ptr = Utility::pattern("80 3D ? ? ? ? 00 74 0E C6 07 02 48 8B 5C 24 30 48 83 C4 20 5F C3").count(1).get(0).get<uintptr_t>(); //is player in ws mode
			WSSize.ptr = Utility::pattern("01 05 ? ? ? ? 8B 44 24 58 01 05 ? ? ? ? 85 D2 0F 84").count(1).get(0).get<uintptr_t>(); // where the game adds to the ws size
			Zoom.ptr = Utility::pattern("F3 0F 10 0D ? ? ? ? 0F 29 74 24 20 F3 0F 10 35").count(1).get(0).get<uintptr_t>();
			gConsole.ptr = Utility::pattern("48 8D 05 ? ? ? ? 48 89 2D ? ? ? ? 48 89 05 ? ? ? ? 89 2D ? ? ? ? 40 88 2D ? ? ? ? 48").count(1).get(0).get<uintptr_t>(); // for console print
			gDataHandler.ptr = Utility::pattern("48 83 3D ? ? ? ? 00 4D 8B F1 41 0F B6 F0 48 8B FA 48 8B D9 0F 84 ? ? ? ? 80 3D ? ? ? ? 00 48").count(1).get(0).get<uintptr_t>();
			WorkbenchSelection.ptr = Utility::pattern("0F B6 84 02 ? ? ? ? 8B 8C 82 ? ? ? ? 48 03 CA FF E1 B0 01 48 83 C4 20 5B C3").count(1).get(0).get<uintptr_t>();
			//AngleFunctions.modAngleXAddr = Utility::pattern("40 53 48 83 EC 30 0F 29 74 24 20 48 8B D9 0F 28 F1 0F 2E B1 C0 00 00 00").count(1).get(0).get<uintptr_t>();
			//AngleFunctions.modAngleYAddr = Utility::pattern("40 53 48 83 EC 30 0F 29 74 24 20 48 8B D9 0F 28 F1 0F 2E B1 C4 00 00 00").count(1).get(0).get<uintptr_t>();
			//AngleFunctions.modAngleZAddr = Utility::pattern("40 53 48 83 EC 30 0F 29 74 24 20 48 8B D9 0F 28 F1 0F 2E B1 C8 00 00 00").count(1).get(0).get<uintptr_t>();

			// store old bytes
			if (Pointers.C) { ReadMemory((uintptr_t(Pointers.C)), &Patches.C_OLD, 0x07); }
			if (Pointers.D) { ReadMemory((uintptr_t(Pointers.D)), &Patches.D_OLD, 0x07); }
			if (Pointers.F) { ReadMemory((uintptr_t(Pointers.F)), &Patches.F_OLD, 0x06); }
			if (Pointers.RC) { ReadMemory((uintptr_t(Pointers.RC)), &Patches.RC_OLD, 0x05); }
			if (Pointers.osnap) { ReadMemory((uintptr_t(Pointers.osnap)), &Patches.OSNAP_OLD, 0x08); }

			//wssize
			if (WSSize.ptr) {
				ReadMemory((uintptr_t(WSSize.ptr) + 0x00), &Patches.DRAWS_OLD, 0x06); //draws
				ReadMemory((uintptr_t(WSSize.ptr) + 0x0A), &Patches.TRIS_OLD, 0x06); //triangles
				WSSize.r32 = GetRel32FromPattern(WSSize.ptr, 0x02, 0x06, 0x00); // rel32 of draws
				WSSize.addr = RelocationManager::s_baseAddr + (uintptr_t)WSSize.r32;
			}

			//zoom and rotate
			if (Zoom.ptr && Rotate.ptr) {
				Zoom.r32 = GetRel32FromPattern(Zoom.ptr, 0x04, 0x08, 0x00);
				Rotate.r32 = GetRel32FromPattern(Rotate.ptr, 0x04, 0x08, 0x00);
				Zoom.addr = RelocationManager::s_baseAddr + (uintptr_t)Zoom.r32;
				Rotate.addr = RelocationManager::s_baseAddr + (uintptr_t)Rotate.r32;
				ReadMemory(Rotate.addr, &settings.fOriginalROTATE, sizeof(Float32));
				ReadMemory(Zoom.addr, &settings.fOriginalZOOM, sizeof(Float32));
			}

			//consolenameref
			if (CNameRef.call && CNameRef.goodfinder) {
				CNameRef.goodfinder_r32 = GetRel32FromPattern(CNameRef.goodfinder, 0x01, 0x05, 0x00); //the good function
				CNameRef.goodfunc = RelocationManager::s_baseAddr + (uintptr_t)CNameRef.goodfinder_r32; // good function full address
			}

			//wsmode
			if (WSMode.ptr) {
				WSMode.r32 = GetRel32FromPattern(WSMode.ptr, 0x02, 0x07, 0x00);
				WSMode.addr = RelocationManager::s_baseAddr + WSMode.r32;
			}

			//moveworkbench
			if (WorkbenchSelection.ptr) {

				//moveworkbench.r32 = GetRel32FromPattern(moveworkbench.ptr, 0x04, 0x08, 0x00);
				//^ doesnt work because this particular one is relative to the base address not the pattern location

				ReadMemory((uintptr_t)WorkbenchSelection.ptr + 0x04, &WorkbenchSelection.r32, sizeof(uint32_t));
				WorkbenchSelection.addr = RelocationManager::s_baseAddr + (uintptr_t)WorkbenchSelection.r32;

			}

			//setscale
			if (ScaleFuncs.setpattern) {
				ScaleFuncs.s32 = GetRel32FromPattern(ScaleFuncs.setpattern, 0x01, 0x05, 0x00);
				RelocAddr <_SetScale_Native> GimmeSetScale(ScaleFuncs.s32);
				ScaleFuncs.SetScale = GimmeSetScale;
			}

			/*modangle
			if (AngleFunctions.modAngleXAddr && AngleFunctions.modAngleYAddr && AngleFunctions.modAngleZAddr) {

				//modangle x
				AngleFunctions.modAngleXRel32 = uintptr_t(AngleFunctions.modAngleXAddr) - RelocationManager::s_baseAddr;
				RelocAddr <_ModAngleX> GimmeModAngleX(AngleFunctions.modAngleXRel32);
				AngleFunctions.modAngleX = GimmeModAngleX;

				//modangle y
				AngleFunctions.modAngleYRel32 = uintptr_t(AngleFunctions.modAngleYAddr) - RelocationManager::s_baseAddr;
				RelocAddr <_ModAngleY> GimmeModAngleY(AngleFunctions.modAngleYRel32);
				AngleFunctions.modAngleY = GimmeModAngleY;

				//modangle z
				AngleFunctions.modAngleZRel32 = uintptr_t(AngleFunctions.modAngleZAddr) - RelocationManager::s_baseAddr;
				RelocAddr <_ModAngleZ> GimmeModAngleZ(AngleFunctions.modAngleZRel32);
				AngleFunctions.modAngleZ = GimmeModAngleZ;
			}
			*/

			//getscale
			if (ScaleFuncs.getpattern) {
				ScaleFuncs.g32 = GetRel32FromPattern(ScaleFuncs.getpattern, 0x08, 0x0C, 0x00);
				RelocAddr <_GetScale_Native> GimmeGetScale(ScaleFuncs.g32);
				ScaleFuncs.GetScale = GimmeGetScale;
			}

			//g_console
			if (gConsole.ptr) {
				gConsole.r32 = GetRel32FromPattern(gConsole.ptr, 0x03, 0x07, 0x00);
				gConsole.addr = RelocationManager::s_baseAddr + (uintptr_t)gConsole.r32;
			}

			//g_datahandler
			if (gDataHandler.ptr) {
				gDataHandler.r32 = GetRel32FromPattern(gDataHandler.ptr, 0x03, 0x08, 0x00);
				gDataHandler.addr = RelocationManager::s_baseAddr + (uintptr_t)gDataHandler.r32;
			}

			//CurrentWSRef
			if (CurrentWSRef.ptr) {
				CurrentWSRef.r32 = GetRel32FromPattern(CurrentWSRef.ptr, 0x03, 0x07, 0x00);
				CurrentWSRef.addr = RelocationManager::s_baseAddr + (uintptr_t)CurrentWSRef.r32;
			}

			//GetConsoleArg
			if (GetConsoleArg.ptr) {
				GetConsoleArg.r32 = uintptr_t(GetConsoleArg.ptr) - RelocationManager::s_baseAddr;
				RelocAddr <_GetConsoleArg_Native> GetDatArg(GetConsoleArg.r32);
				GetConsoleArg_Native = GetDatArg;
			}

			//first console
			if (FirstConsole.ptr) {
				FirstConsole.r32 = GetRel32FromPattern(FirstConsole.ptr, 0x03, 0x07, -0x08);
				RelocPtr <ObScriptCommand> _FirstConsole(FirstConsole.r32);
				FirstConsole.cmd = _FirstConsole;
			}

			//first obscript
			if (FirstObScript.ptr) {
				FirstObScript.r32 = GetRel32FromPattern(FirstObScript.ptr, 0x03, 0x07, -0x08);
				RelocPtr <ObScriptCommand> _FirstObScript(FirstObScript.r32);
				FirstObScript.cmd = _FirstObScript;
			}

			// setmotiontype
			if (SetMotionType.ptr) {
				SetMotionType.r32 = uintptr_t(SetMotionType.ptr) - RelocationManager::s_baseAddr;
				RelocAddr <_SetMotionType_Native> GimmeSetMotionType(SetMotionType.r32);
				SetMotionType_Native = GimmeSetMotionType;
			}

			// playuisound
			if (PlaySounds.UIpattern) {
				PlaySounds.UI_r32 = uintptr_t(PlaySounds.UIpattern) - RelocationManager::s_baseAddr;
				RelocAddr <_PlayUISound_Native> _PlayUISound_Native(PlaySounds.UI_r32);
				PlaySounds.UI_func = _PlayUISound_Native;
			}

			// playfilesound
			if (PlaySounds.Filepattern) {
				PlaySounds.File_r32 = uintptr_t(PlaySounds.Filepattern) - RelocationManager::s_baseAddr;
				RelocAddr <_PlayFileSound_Native> PlayFileSound_Native(PlaySounds.File_r32);
				PlaySounds.File_func = PlayFileSound_Native;
			}
		}



	}


	__declspec(dllexport) bool F4SEPlugin_Load(const F4SEInterface* f4se)
	{
		// start log
		PIR_LOG_PREP
			pirlog.OpenRelative(CSIDL_MYDOCUMENTS, pluginLogFile);
		pirlog.FormattedMessage("[%s] Plugin loaded. (version %i)", thisfunc, pluginVersion);

		if (!pir::InitF4SE(f4se)) {
			return false;
		}

		pir::InitPIR();

		if (!pir::FoundPatterns()) {
			pir::LogPatterns();
			return false;
		}

		if (!pir::CreateConsole("GameComment"))
		{
			pirlog.FormattedMessage("[%s] Failed to create console command! Plugin will run with defaults.", thisfunc);
		}

		// toggle defaults
		pir::ReadINI();

		// plugin loaded
		pirlog.FormattedMessage("[%s] Plugin load finished!", thisfunc);
		pir::LogPatterns();
		return true;
	}


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