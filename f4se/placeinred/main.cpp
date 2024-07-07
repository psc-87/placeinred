#include "main.h"

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
			char resultBuf[2048];
			resultBuf[0] = 0;
			UInt32 resultLen = GetPrivateProfileString(section, key, NULL, resultBuf, sizeof(resultBuf), configPath.c_str());
			result = resultBuf;
		}
		return result;
	}
}

extern "C" {
	namespace pir {

		static _SETTINGS    settings;
		static _PATCHES     Patches;
		static _POINTERS    Pointers;
		static _PlaySounds  PlaySounds;
		static _ScaleFuncs  ScaleFuncs;
		static _CNameRef    CNameRef;
		static SimpleFinder FirstConsole;
		static SimpleFinder FirstObScript;
		static SimpleFinder WSMode;
		static SimpleFinder WSSize;
		static SimpleFinder gConsole;
		static SimpleFinder gDataHandler;
		static SimpleFinder CurrentWSRef;
		static SimpleFinder Zoom;
		static SimpleFinder Rotate;
		static SimpleFinder SetMotionType;
		static SimpleFinder GetConsoleArg;
		static _SetMotionType_Native SetMotionType_Native = nullptr;
		static _GetConsoleArg_Native GetConsoleArg_Native = nullptr;

		static const char* ConsoleHelpMSG =
		{
		  "PlaceInRed (pir) usage:\n"
		  "pir toggle       (pir 1) toggle place in red\n"
		  "pir osnap        (pir 2) toggle object snapping\n"
		  "pir gsnap        (pir 3) toggle ground snapping\n"
		  "pir slow         (pir 4) toggle slower rotation/zoom speed\n"
		  "pir workshopsize (pir 5) toggle unlimited workshop build size\n"
		  "pir outlines     (pir 6) toggle object outlines\n"
		  "pir achievements (pir 7) toggle achievement with mods\n"
		  "pir scaleup1     (or 2,5,10,25,50,and 100) scale up percent\n"
		  "pir scaledown1   (or 2,5,10,25,50,and 75) scale down percent\n"
		  "pir lock         (pir l) lock object in place (motiontype keyframed)\n"
		  "pir unlock       (pir u) unlock object (motiontype dynamic)"
		  "pir cnref        toggle consolenameref"
		  "pir print        toggle place in red printing console messages"
		};



		// string to float
		static Float32 FloatFromString(std::string fString, Float32 min = 0.001, Float32 max = 999.999, Float32 error=0)
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
			} else {
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
		static SInt32 GetRel32FromPattern(uintptr_t* pattern, UInt64 start, UInt64 end, UInt64 shift = 0x0)
		{
			// pattern: pointer to pattern match
			// start: bytes to reach rel32 from pattern
			// end: bytes to reach the end
			// shift: shift the final address by this many bytes
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
			if (WSMode.func) {
				UInt8 WSMODE = 0x00;
				ReadMemory(uintptr_t(WSMode.addr), &WSMODE, sizeof(bool));
				if (WSMODE == 0x01) {
					return true;
				}
			}
			return false;
		}

		// return the currently selected workshop ref with some safety checks
		static TESObjectREFR* GetCurrentWSRef(bool refonly=1)
		{
			PIR_LOG_PREP
			if (CurrentWSRef.func && CurrentWSRef.addr && pir::InWorkshopMode()) {

				uintptr_t* refptr = GimmeMultiPointer(CurrentWSRef.addr, Patches.CurrentWSRef_Offsets, Patches.CurrentWSRef_OffsetsSize);
				TESObjectREFR* ref = (TESObjectREFR*)(refptr);

				if (ref)
				{
					if (!ref->formID) { return nullptr; }
					if (ref->formID <= 0) { return nullptr; }

					//optional but checks by default
					if (refonly) {
						if (ref->formType != 0x40) {
							return nullptr;
						}
					}
					return ref;
				}
			}
			return nullptr;
		}

		// lock the current WS ref in place by changing the motion type to keyframed
		static void LockOrUnlockCurrentWSRef(bool unlock = 0, bool sound = 0)
		{
			

			VirtualMachine* vm = (*g_gameVM)->m_virtualMachine;
			TESObjectREFR* ref = GetCurrentWSRef();
			UInt32 motion = 00000002; //Motion_Keyframed
			bool acti = false; //akAllowACtivate

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

		// copied from f4se and modified for use with the pattern
		static void ConsolePrint(const char* fmt, ...)
		{
			if (gConsole.func && gConsole.addr && settings.PrintConsoleMessages)
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
		static bool Toggle_PrintConsoleMessages()
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

			    default: break;
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
		static void MoveRefToSelf(float modx = 0, float mody = 0, float modz = 0, int repeat = 0)
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

				for (int i = 0; i <= repeat; i++)
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
				pir::MoveRefToSelf(0, 0, 0, 1); //repeat once to fix jitter
				return true;
			}
			return false;
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
			if (Zoom.func && Rotate.func && Zoom.addr && Rotate.addr && settings.SLOW_ENABLED) {
				SafeWriteBuf(Zoom.addr, &settings.fOriginalZOOM, sizeof(Float32));
				SafeWriteBuf(Rotate.addr, &settings.fOriginalROTATE, sizeof(Float32));
				settings.SLOW_ENABLED = false;
				pir::ConsolePrint("Slow zoom and rotate disabled");
				return true;
			}
			// its off, turn it on
			if (Zoom.func && Rotate.func && Zoom.addr && Rotate.addr && !settings.SLOW_ENABLED) {
				SafeWriteBuf(Zoom.addr, &settings.fSlowerZOOM, sizeof(Float32));
				SafeWriteBuf(Rotate.addr, &settings.fSlowerROTATE, sizeof(Float32));
				settings.SLOW_ENABLED = true;
				pir::ConsolePrint("Slow zoom and rotate enabled");
				return true;
			}
			return false;
		}

		//toggle infinite workshop size
		static bool Toggle_WorkshopSize()
		{
			if (WSSize.func && settings.WORKSHOPSIZE_ENABLED) {
				SafeWriteBuf((uintptr_t)WSSize.func, Patches.DRAWS_OLD, sizeof(Patches.DRAWS_OLD));
				SafeWriteBuf((uintptr_t)WSSize.func + 0x0A, Patches.TRIS_OLD, sizeof(Patches.TRIS_OLD));
				settings.WORKSHOPSIZE_ENABLED = false;
				pir::ConsolePrint("Unlimited workshop size disabled");
				return true;
			}

			if (WSSize.func && settings.WORKSHOPSIZE_ENABLED == false) {
				// Write nop 6 so its never increased
				SafeWriteBuf((uintptr_t)WSSize.func, Patches.NOP6, sizeof(Patches.NOP6));
				SafeWriteBuf((uintptr_t)WSSize.func + 0x0A, Patches.NOP6, sizeof(Patches.NOP6));

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
			if (Pointers.osnap && settings.OBJECTSNAP_ENABLED) {
				SafeWriteBuf((uintptr_t)Pointers.osnap, Patches.OSNAP_NEW, sizeof(Patches.OSNAP_NEW));
				settings.OBJECTSNAP_ENABLED = false;
				pir::ConsolePrint("Object snap disabled");
				return true;
			}
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
				SafeWriteBuf((uintptr_t)Pointers.achievements, Patches.achievements_old, sizeof(Patches.achievements_old));
				settings.ACHIEVEMENTS_ENABLED = false;
				pir::ConsolePrint("Achievements with mods disabled (game default)");
				return true;
			}
			// its off - toggle it on
			if (Pointers.achievements && !settings.ACHIEVEMENTS_ENABLED) {
				SafeWriteBuf((uintptr_t)Pointers.achievements, Patches.achievements_new, sizeof(Patches.achievements_new));
				settings.ACHIEVEMENTS_ENABLED = true;
				pir::ConsolePrint("Achievements with mods enabled!");
				return true;
			}
			return false;
		}

		// toggle consolenameref
		static bool Toggle_ConsoleRefName()
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

		//toggle placing objects in red
		static bool Toggle_PlaceInRed()
		{
			if (settings.PLACEINRED_ENABLED) {
				SafeWrite8((uintptr_t)Pointers.A + 0x06, 0x01);
				SafeWrite8((uintptr_t)Pointers.A + 0x0C, 0x02);
				SafeWrite8((uintptr_t)Pointers.B + 0x01, 0x01);
				SafeWriteBuf((uintptr_t)Pointers.C, Patches.C_OLD, sizeof(Patches.C_OLD));
				SafeWriteBuf((uintptr_t)Pointers.C + 0x11, Patches.K_OLD, sizeof(Patches.K_OLD)); // testing
				SafeWrite8((uintptr_t)Pointers.C + 0x1D, 0x01);
				SafeWriteBuf((uintptr_t)Pointers.D, Patches.D_OLD, sizeof(Patches.D_OLD));
				SafeWrite8((uintptr_t)Pointers.E + 0x00, 0x76);
				SafeWriteBuf((uintptr_t)Pointers.F, Patches.F_OLD, sizeof(Patches.F_OLD));
				SafeWrite8((uintptr_t)Pointers.G + 0x01, 0x95);
				SafeWrite8((uintptr_t)Pointers.H + 0x00, 0x74);
				SafeWriteBuf((uintptr_t)Pointers.J, Patches.J_OLD, sizeof(Patches.J_OLD));
				SafeWrite8((uintptr_t)Pointers.red + 0xC, 0x01);
				SafeWriteBuf((uintptr_t)Pointers.yellow, Patches.Y_OLD, sizeof(Patches.Y_OLD));
				SafeWriteBuf((uintptr_t)Pointers.wstimer, Patches.TIMER_OLD, sizeof(Patches.TIMER_OLD));
				settings.PLACEINRED_ENABLED = false;
				pir::ConsolePrint("Place in Red disabled.");
				return true;
			}

			if (!settings.PLACEINRED_ENABLED) {
				SafeWrite8((uintptr_t)Pointers.A + 0x06, 0x00);
				SafeWrite8((uintptr_t)Pointers.A + 0x0C, 0x01);
				SafeWrite8((uintptr_t)Pointers.B + 0x01, 0x00);
				SafeWriteBuf((uintptr_t)Pointers.C, Patches.C_NEW, sizeof(Patches.C_NEW));
				SafeWriteBuf((uintptr_t)Pointers.C + 0x11, Patches.K_NEW, sizeof(Patches.K_NEW)); // testing
				SafeWrite8((uintptr_t)Pointers.C + 0x1D, 0x00);
				SafeWriteBuf((uintptr_t)Pointers.D, Patches.D_NEW, sizeof(Patches.D_NEW));
				SafeWrite8((uintptr_t)Pointers.E + 0x00, 0xEB);
				SafeWriteBuf((uintptr_t)Pointers.F, Patches.NOP6, sizeof(Patches.NOP6));
				SafeWrite8((uintptr_t)Pointers.G + 0x01, 0x98); // works but look at again later
				SafeWrite8((uintptr_t)Pointers.H + 0x00, 0xEB);
				SafeWriteBuf((uintptr_t)Pointers.J, Patches.J_NEW, sizeof(Patches.J_NEW));
				SafeWrite8((uintptr_t)Pointers.red + 0x0C, 0x00);
				SafeWriteBuf((uintptr_t)Pointers.yellow, Patches.NOP3, sizeof(Patches.NOP3));
				SafeWriteBuf((uintptr_t)Pointers.wstimer, Patches.TIMER_NEW, sizeof(Patches.TIMER_NEW));
				settings.PLACEINRED_ENABLED = true;
				pir::ConsolePrint("Place In Red enabled.");
				return true;
			}

			return false;
		}

		//Log details about the selected workshop ref
		static void LogWSRef()
		{
			PIR_LOG_PREP
				TESObjectREFR* ref = GetCurrentWSRef(0);
			if (ref) {
				pirlog.FormattedMessage("-------------------------------------------------------------------------------------");
				UInt8	formtype = ref->GetFormType();
				UInt32	formid = ref->formID;
				UInt32	refflags = ref->flags;
				UInt32	cellformid = ref->parentCell->formID;
				UInt64	rootflags = ref->GetObjectRootNode()->flags;
				const char* rootname = ref->GetObjectRootNode()->m_name.c_str();
				UInt16	rootchildren = ref->GetObjectRootNode()->m_children.m_size;
				float Px = ref->pos.x;
				float Py = ref->pos.y;
				float Pz = ref->pos.z;
				float Rx = ref->rot.x;
				float Ry = ref->rot.y;
				float Rz = ref->rot.z;
				pirlog.FormattedMessage("Pos             : %f %f %f", Px, Py, Pz);
				pirlog.FormattedMessage("Rot             : %f %f %f", Rx, Ry, Rz);
				pirlog.FormattedMessage("parentcell      : %04X", cellformid);
				pirlog.FormattedMessage("root->m_name    : %s", rootname);
				pirlog.FormattedMessage("root->m_children: %02X", rootchildren);
				pirlog.FormattedMessage("ref->formtype   : %01X (%d)", formtype, formtype);
				pirlog.FormattedMessage("ref->formID     : %04X", formid);
				pirlog.FormattedMessage("ref->flags      : %04X", refflags);
				pirlog.FormattedMessage("-------------------------------------------------------------------------------------");

				NiBound	rootnibound = ref->GetObjectRootNode()->m_worldBound;

				float nbradius = rootnibound.m_fRadius;
				int nbradiusint = rootnibound.m_iRadiusAsInt;
				NiPoint3 nbcenter = rootnibound.m_kCenter;

				pirlog.FormattedMessage("%f %i %f %f %f", nbradius, nbradiusint, nbcenter.x, nbcenter.y, nbcenter.z);

			}
		}

		static void PlayFileSound(const char* wav)
		{
			if (PlaySounds.File_func) {
				PlaySounds.File_func(wav);
			}
		}

		static void PlayUISound(const char* sound)
		{
			if (PlaySounds.UI_func) {
				PlaySounds.UI_func(sound);
			}
		}

		//called every time the console command runs
		static bool ExecuteConsoleCommand(void* paramInfo, void* scriptData, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, double* result, void* opcodeOffsetPtr)
		{
			PIR_LOG_PREP
				if (GetConsoleArg_Native && GetConsoleArg.func && (GetConsoleArg.r32 != 0)) {

					char param1[4096];
					char param2[4096];
					bool consoleresult = GetConsoleArg_Native(paramInfo, scriptData, opcodeOffsetPtr, thisObj, containingObj, scriptObj, locals, &param1, &param2);

					if (consoleresult && param1[0]) {
						switch (ConsoleSwitch(param1)) {
						// debug and tests
						case pir::ConsoleSwitch("dumprefs"):     pir::DumpCellRefs();                 break;
						case pir::ConsoleSwitch("dumpcmds"):     pir::DumpCmds();                     break;
						case pir::ConsoleSwitch("logref"):       pir::LogWSRef();                     break;
						case pir::ConsoleSwitch("print"):        pir::Toggle_PrintConsoleMessages();  break;
						case pir::ConsoleSwitch("sound"):        pir::PlayFileSound(param2);          break;
						case pir::ConsoleSwitch("uisound"):      pir::PlayUISound(param2);            break;

						//toggles
						case pir::ConsoleSwitch("1"):            pir::Toggle_PlaceInRed();         break;
						case pir::ConsoleSwitch("toggle"):       pir::Toggle_PlaceInRed();         break;
						case pir::ConsoleSwitch("2"):            pir::Toggle_ObjectSnap();         break;
						case pir::ConsoleSwitch("osnap"):        pir::Toggle_ObjectSnap();         break;
						case pir::ConsoleSwitch("3"):            pir::Toggle_GroundSnap();         break;
						case pir::ConsoleSwitch("gsnap"):        pir::Toggle_GroundSnap();         break;
						case pir::ConsoleSwitch("4"):            pir::Toggle_SlowZoomAndRotate();  break;
						case pir::ConsoleSwitch("slow"):         pir::Toggle_SlowZoomAndRotate();  break;
						case pir::ConsoleSwitch("5"):            pir::Toggle_WorkshopSize();       break;
						case pir::ConsoleSwitch("workshopsize"): pir::Toggle_WorkshopSize();       break;
						case pir::ConsoleSwitch("6"):            pir::Toggle_Outlines();           break;
						case pir::ConsoleSwitch("outlines"):     pir::Toggle_Outlines();           break;
						case pir::ConsoleSwitch("7"):            pir::Toggle_Achievements();       break;
						case pir::ConsoleSwitch("achievements"): pir::Toggle_Achievements();       break;

						//scale constants
						case pir::ConsoleSwitch("scale1"):       pir::SetCurrentRefScale(1.0000f); break;
						case pir::ConsoleSwitch("scale10"):      pir::SetCurrentRefScale(9.9999f); break;

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
						case pir::ConsoleSwitch("lock"):         pir::LockOrUnlockCurrentWSRef(0, 1); break;
						case pir::ConsoleSwitch("l"):            pir::LockOrUnlockCurrentWSRef(0, 1); break;
						case pir::ConsoleSwitch("lockq"):        pir::LockOrUnlockCurrentWSRef(0, 0); break;
						case pir::ConsoleSwitch("unlock"):       pir::LockOrUnlockCurrentWSRef(1, 0); break;
						case pir::ConsoleSwitch("u"):            pir::LockOrUnlockCurrentWSRef(1, 0); break;
						

						// console name ref toggle
						case pir::ConsoleSwitch("cnref"):        pir::Toggle_ConsoleRefName(); break;
						
											
						// show help
						case pir::ConsoleSwitch("?"):            pir::ConsolePrint(ConsoleHelpMSG); break;
						case pir::ConsoleSwitch("help"):         pir::ConsolePrint(ConsoleHelpMSG); break;

						// scale

						default: pir::ConsolePrint(ConsoleHelpMSG);  break;
						}

						return true;
					}

				}
			pirlog.FormattedMessage("[%s] Failed to execute the console command!", thisfunc);
			return false;
		}

		//attempt to create the console command
		static bool CreateConsoleCMD(const char* hijacked_cmd_fullname)
		{
			PIR_LOG_PREP
			pirlog.FormattedMessage("[%s] Patching command '%s'.", thisfunc, hijacked_cmd_fullname);

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
				cmd.execute = pir::ExecuteConsoleCommand;
				cmd.params = allParams;
				cmd.flags = 0;
				cmd.eval;

				SafeWriteBuf((uintptr_t)s_hijackedCommand, &cmd, sizeof(cmd));
				return true;
			}
			return false;
		}

		//did we find all the required memory patterns?
		static bool FoundRequiredMemoryPatterns()
		{
			PIR_LOG_PREP
			if (GetConsoleArg.func && FirstConsole.func && FirstObScript.func && ScaleFuncs.setpattern && ScaleFuncs.getpattern && CurrentWSRef.func
				&& WSMode.func && gConsole.func && Pointers.A && Pointers.B && Pointers.C && Pointers.D && Pointers.E && Pointers.F && Pointers.G && Pointers.H && Pointers.J
				&& Pointers.yellow && Pointers.red && Pointers.wstimer && Pointers.gsnap && Pointers.osnap && Pointers.outlines && WSSize.func && Zoom.func && Rotate.func && SetMotionType.func)
			{
				/*
				 allow plugin to load even if these arent found:

				 Pointers.achievements - not a showstopper
				 ConsoleRefCallFinder - copy of another mod never required
				 GDataHandlerFinder - not using yet
				*/

				return true;
			} else {
				pirlog.FormattedMessage("[%s] Couldnt find required memory patterns! Check for conflicting mods.", thisfunc);
				return false;
			}
		}

		//log all the memory patterns to the log file
		static void LogMemoryPatterns()
		{
			pirlog.FormattedMessage("-------------------------------------------------------------------------------");
			pirlog.FormattedMessage("Base           :%p", RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("achievements   :%p", Pointers.achievements);
			pirlog.FormattedMessage("A              :%p", Pointers.A);
			pirlog.FormattedMessage("B              :%p", Pointers.B);
			pirlog.FormattedMessage("C              :%p|OLD:%02X%02X%02X%02X%02X%02X%02X", Pointers.C, Patches.C_OLD[0], Patches.C_OLD[1], Patches.C_OLD[2], Patches.C_OLD[3], Patches.C_OLD[4], Patches.C_OLD[5], Patches.C_OLD[6]);
			pirlog.FormattedMessage("D              :%p|OLD:%02X%02X%02X%02X%02X%02X%02X", Pointers.D, Patches.D_OLD[0], Patches.D_OLD[1], Patches.D_OLD[2], Patches.D_OLD[3], Patches.D_OLD[4], Patches.D_OLD[5], Patches.D_OLD[6]);
			pirlog.FormattedMessage("E              :%p", Pointers.E);
			pirlog.FormattedMessage("F              :%p", Pointers.F);
			pirlog.FormattedMessage("G              :%p", Pointers.G);
			pirlog.FormattedMessage("H              :%p", Pointers.H);
			pirlog.FormattedMessage("J              :%p", Pointers.J);
			pirlog.FormattedMessage("yellow         :%p", Pointers.yellow);
			pirlog.FormattedMessage("red            :%p", Pointers.red);
			pirlog.FormattedMessage("redcall        :%p", Pointers.redcall);
			pirlog.FormattedMessage("wstimer        :%p", Pointers.wstimer);
			pirlog.FormattedMessage("gsnap          :%p", Pointers.gsnap);
			pirlog.FormattedMessage("osnap          :%p", Pointers.osnap);
			pirlog.FormattedMessage("outlines       :%p", Pointers.outlines);
			pirlog.FormattedMessage("WSMode         :%p|0x%08X|%p", WSMode.func, WSMode.r32, WSMode.addr);
			pirlog.FormattedMessage("FirstConsole   :%p|0x%08X", FirstConsole.func, FirstConsole.r32);
			pirlog.FormattedMessage("FirstObScript  :%p|0x%08X", FirstObScript.func, FirstObScript.r32);
			pirlog.FormattedMessage("GConsole       :%p|0x%08X|%p", gConsole.func, gConsole.r32, gConsole.addr);
			pirlog.FormattedMessage("GetConsoleArg  :%p|0x%08X|%p", GetConsoleArg.func, GetConsoleArg.r32, GetConsoleArg_Native);
			pirlog.FormattedMessage("CurrentWSRef   :%p|0x%08X|%p", CurrentWSRef.func, CurrentWSRef.r32, CurrentWSRef.addr);
			pirlog.FormattedMessage("GetScale       :%p|0x%08X", ScaleFuncs.getpattern, ScaleFuncs.g32);
			pirlog.FormattedMessage("SetScale       :%p|0x%08X", ScaleFuncs.setpattern, ScaleFuncs.s32);
			pirlog.FormattedMessage("SetMotionType  :%p|0x%08X", SetMotionType.func, SetMotionType.r32);
			pirlog.FormattedMessage("PlayUISound    :%p|0x%08X", PlaySounds.Filepattern, PlaySounds.File_r32);
			pirlog.FormattedMessage("PlayFileSound  :%p|0x%08X", PlaySounds.UIpattern, PlaySounds.UI_r32);
			pirlog.FormattedMessage("WSSize|Floats  :%p|%p", WSSize.func, WSSize.addr);
			pirlog.FormattedMessage("Rotate         :%p|%p|orig %f|slow %f", Rotate.func, Rotate.addr, settings.fOriginalROTATE, settings.fSlowerROTATE);
			pirlog.FormattedMessage("Zoom           :%p|%p|orig %f|slow %f", Zoom.func, Zoom.addr, settings.fOriginalZOOM, settings.fSlowerZOOM);
			pirlog.FormattedMessage("-------------------------------------------------------------------------------");
		}

		//read the ini and toggle default settings
		static void ReadINIDefaults()
		{
			PIR_LOG_PREP
			pirlog.FormattedMessage("[%s] Reading and setting ini defaults.", thisfunc);

			// store the setting as a string
			std::string SETTING01 = GetPIRConfigOption("Main", "PLACEINRED_ENABLED");
			std::string SETTING02 = GetPIRConfigOption("Main", "OBJECTSNAP_ENABLED");
			std::string SETTING03 = GetPIRConfigOption("Main", "GROUNDSNAP_ENABLED");
			std::string SETTING04 = GetPIRConfigOption("Main", "SLOW_ENABLED");
			std::string SETTING05 = GetPIRConfigOption("Main", "WORKSHOPSIZE_ENABLED");
			std::string SETTING06 = GetPIRConfigOption("Main", "OUTLINES_ENABLED");
			std::string SETTING07 = GetPIRConfigOption("Main", "ACHIEVEMENTS_ENABLED");
			std::string SETTING08 = GetPIRConfigOption("Main", "ConsoleNameRef_ENABLED");
			std::string SETTING09 = GetPIRConfigOption("Main", "PrintConsoleMessages");
			std::string SETTING10 = GetPIRConfigOption("Main", "fSlowerROTATE");
			std::string SETTING11 = GetPIRConfigOption("Main", "fSlowerZOOM");

			//[Main] PLACEINRED_ENABLED
			if (SETTING01 == "1") { pir::Toggle_PlaceInRed(); }
			//[Main] OBJECTSNAP_ENABLED
			if (SETTING02 == "0") { pir::Toggle_ObjectSnap(); }
			//[Main] GROUNDSNAP_ENABLED
			if (SETTING03 == "0") { pir::Toggle_GroundSnap(); }
			//[Main] WORKSHOPSIZE_ENABLED
			if (SETTING05 == "1") { pir::Toggle_WorkshopSize(); }
			//[Main] OUTLINES_ENABLED
			if (SETTING06 == "0") { pir::Toggle_Outlines(); }
			//[Main] ACHIEVEMENTS_ENABLED
			if (SETTING07 == "1") { pir::Toggle_Achievements();	}
			//[Main] ConsoleNameRef_ENABLED
			if (SETTING08 == "1") { pir::Toggle_ConsoleRefName(); }
			//[Main] PrintConsoleMessages
			if (SETTING09 == "0") { settings.PrintConsoleMessages = 0; }
			//[Main] fSlowerROTATE
			if (!SETTING10.empty()) {
				Float32 rTemp = FloatFromString(SETTING10);
				if (rTemp == 0) {
					settings.fSlowerROTATE = 0.5; //bad ini force plugin default
					pirlog.FormattedMessage("[INI] fSlowerROTATE: invalid. Using 0.5");
				}
				else {
					settings.fSlowerROTATE = rTemp;
				}
			}
			//[Main] fSlowerZOOM
			if (!SETTING11.empty()) {
				Float32 zTemp = FloatFromString(SETTING11);
				if (zTemp == 0) {
					settings.fSlowerZOOM = 1.0; // bad ini force plugin default
					pirlog.FormattedMessage("[INI] fSlowerZOOM: invalid. Using 1.0");
				}
				else {
					settings.fSlowerZOOM = zTemp;
				}
			}
			// toggle this one AFTER reading the ini setting
			if (SETTING04 == "1") { pir::Toggle_SlowZoomAndRotate(); }
			
		}
		
		//init f4se stuff and return false if anything fails
		static bool InitF4SE(const F4SEInterface* f4se)
		{
			PIR_LOG_PREP
			// get a plugin handle
			pirlog.FormattedMessage("[%s] Getting plugin handle.", thisfunc);
			pirPluginHandle = f4se->GetPluginHandle();
			if (!pirPluginHandle) {
				pirlog.FormattedMessage("[%s] Couldn't get a plugin handle!", thisfunc);
				return false;
			}
			

			// messaging interface
			pirlog.FormattedMessage("[%s] Setting messaging interface.", thisfunc);
			g_messaging = (F4SEMessagingInterface*)f4se->QueryInterface(kInterface_Messaging);
			if (!g_messaging) {
				pirlog.FormattedMessage("[%s] Failed to set messaging interface!", thisfunc);
				return false;
			}

			// object interface
			pirlog.FormattedMessage("[%s] Setting object interface.", thisfunc);
			g_object = (F4SEObjectInterface*)f4se->QueryInterface(kInterface_Object);
			if (!g_object) {
				pirlog.FormattedMessage("[%s] Failed to set object interface!", thisfunc);
				return false;
			}

			// message interface
			pirlog.FormattedMessage("[%s] Registering message listener.", thisfunc);
			if (g_messaging->RegisterListener(pirPluginHandle, "F4SE", pir::MessageInterfaceHandler) == false) {
				pirlog.FormattedMessage("[%s] Failed to register message listener!", thisfunc);
				return false;
			}

			return true;

		}
		
		//search for required memory patterns. store old bytes. relocate functions. etc.
		static void InitPIR()
		{
			CNameRef.call = Utility::pattern("FF 90 D0 01 00 00 48 89 74 24 40 4C 8D 05 ? ? ? ? 4C").count(1).get(0).get<uintptr_t>();
			CNameRef.goodfinder = Utility::pattern("E8 ? ? ? ? 4C 8B 05 ? ? ? ? 48 8D 4C 24 40 4C 8B C8 BA 00 01 00 00 E8 ? ? ? ? 83").count(1).get(0).get<uintptr_t>();
			CurrentWSRef.func = Utility::pattern("48 8B 1D ? ? ? ? 4C 8D 24 C3 49 3B DC 0F 84 ? ? ? ? 66").count(1).get(0).get<uintptr_t>(); //has address leading to current WS ref
			Pointers.A = Utility::pattern("C6 05 ? ? ? ? 01 84 C0 75 A9 B1 02").count(1).get(0).get<uintptr_t>();
			Pointers.B = Utility::pattern("B2 01 88 15 ? ? ? ? EB 04 84 D2 74 07").count(1).get(0).get<uintptr_t>();
			Pointers.C = Utility::pattern("0F B6 05 ? ? ? ? 44 0F B6 A5 ? ? ? ? 84 C0 75").count(1).get(0).get<uintptr_t>();
			Pointers.D = Utility::pattern("0F B6 05 ? ? ? ? 3A 05 ? ? ? ? 48 8B B4 24 ? ? ? ? C6 05").count(1).get(0).get<uintptr_t>();
			Pointers.E = Utility::pattern("76 0C C6 05 ? ? ? ? 06 E9 ? ? ? ? 40 84 ED 0F 84").count(1).get(0).get<uintptr_t>();
			Pointers.F = Utility::pattern("88 05 ? ? ? ? E8 ? ? ? ? 84 C0 75 ? 80 3D ? ? ? ? 02").count(1).get(0).get<uintptr_t>();
			Pointers.G = Utility::pattern("0F 95 05 ? ? ? ? E8 ? ? ? ? 40 38 35 ? ? ? ? 48").count(1).get(0).get<uintptr_t>();
			Pointers.H = Utility::pattern("74 11 40 84 F6 74 0C 48 8D ? ? 49 8B ? E8").count(1).get(0).get<uintptr_t>();
			Pointers.J = Utility::pattern("74 35 48 8B B5 ? ? ? ? 48 8B CE E8 ? ? ? ? 84 C0 75").count(1).get(0).get<uintptr_t>(); // ignore water restrictions
			Pointers.achievements = Utility::pattern("48 83 EC 28 C6 44 24 ? 00 84 D2 74 1C 48").count(1).get(0).get<uintptr_t>();
			Pointers.gsnap = Utility::pattern("0F 86 ? ? ? ? 41 8B 4E 34 49 B8").count(1).get(0).get<uintptr_t>();
			Pointers.osnap = Utility::pattern("F3 0F 10 35 ? ? ? ? 0F 28 C6 48 ? ? ? ? ? ? 33 C0").count(1).get(0).get<uintptr_t>();
			Pointers.outlines = Utility::pattern("C6 05 ? ? ? ? 01 88 15 ? ? ? ? 76 13 48 8B 05").count(1).get(0).get<uintptr_t>(); // object outlines not instant
			Pointers.red = Utility::pattern("89 05 ? ? ? ? C6 05 ? ? ? ? 01 48 83 C4 68 C3").count(1).get(0).get<uintptr_t>(); //keep objects green
			Pointers.redcall = Utility::pattern("E8 ? ? ? ? 83 3D ? ? ? ? 00 0F 87 ? ? ? ? 48 8B 03 48 8B CB FF 90 ? ? ? ? 48").count(1).get(0).get<uintptr_t>(); // prevent function from being called after red is patched
			Pointers.wstimer = Utility::pattern("0F 85 AB 00 00 00 F3 0F 10 05 ? ? ? ? 41 0F 2E C3 75 66 F3 0F 10 05 ? ? ? ? F3 0F 11 05 ? ? ? ? C6").count(1).get(0).get<uintptr_t>(); // New ws timer check is buried in here
			Pointers.yellow = Utility::pattern("8B 58 14 48 8D 4C 24 30 8B D3 45 33 C0 E8").count(1).get(0).get<uintptr_t>(); // allow moving yellow objects
			FirstConsole.func = Utility::pattern("48 8D 1D ? ? ? ? 48 8D 35 ? ? ? ? 66 90 48 8B 53 F8").count(1).get(0).get<uintptr_t>();
			FirstObScript.func = Utility::pattern("48 8D 1D ? ? ? ? 4C 8D 35 ? ? ? ? 0F 1F 40 00 0F 1F 84 00 00 00 00 00").count(1).get(0).get<uintptr_t>();
			GetConsoleArg.func = Utility::pattern("4C 89 4C 24 20 48 89 4C 24 08 53 55 56 57 41 54 41 55 41 56 41 57").count(1).get(0).get<uintptr_t>();
			Rotate.func = Utility::pattern("F3 0F 10 05 ? ? ? ? ? ? ? ? ? ? ? ? 84 C9 75 07 0F 57 05").count(1).get(0).get<uintptr_t>(); //better compatibility with high physics fps
			ScaleFuncs.getpattern = Utility::pattern("66 89 BB 08 01 00 00 E8 ? ? ? ? 48 8B 0D ? ? ? ? 0F 28 F0 48").count(1).get(0).get<uintptr_t>(); //getscale
			ScaleFuncs.setpattern = Utility::pattern("E8 ? ? ? ? 40 84 F6 75 07 81 63 10 FF FF DF FF 33 ED").count(1).get(0).get<uintptr_t>(); //setscale
			PlaySounds.Filepattern = Utility::pattern("48 8B C4 48 89 58 08 57 48 81 EC 50 01 00 00 8B FA C7 40 18 FF FF FF FF 48").count(1).get(0).get<uintptr_t>();
			PlaySounds.UIpattern = Utility::pattern("48 89 5C 24 08 57 48 83 EC 50 48 8B D9 E8 ? ? ? ? 48 85 C0 74 6A").count(1).get(0).get<uintptr_t>();
			SetMotionType.func = Utility::pattern("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 50 45 32 E4 41 8D 41 FF").count(1).get(0).get<uintptr_t>();
			WSMode.func = Utility::pattern("80 3D ? ? ? ? 00 74 0E C6 07 02 48 8B 5C 24 30 48 83 C4 20 5F C3").count(1).get(0).get<uintptr_t>(); //is player in ws mode
			WSSize.func = Utility::pattern("01 05 ? ? ? ? 8B 44 24 58 01 05 ? ? ? ? 85 D2 0F 84").count(1).get(0).get<uintptr_t>(); // where the game adds to the ws size
			Zoom.func = Utility::pattern("F3 0F 10 0D ? ? ? ? 0F 29 74 24 20 F3 0F 10 35").count(1).get(0).get<uintptr_t>();
			gConsole.func = Utility::pattern("48 8D 05 ? ? ? ? 48 89 2D ? ? ? ? 48 89 05 ? ? ? ? 89 2D ? ? ? ? 40 88 2D ? ? ? ? 48").count(1).get(0).get<uintptr_t>(); // for console print
			gDataHandler.func = Utility::pattern("48 83 3D ? ? ? ? 00 4D 8B F1 41 0F B6 F0 48 8B FA 48 8B D9 0F 84 ? ? ? ? 80 3D ? ? ? ? 00 48").count(1).get(0).get<uintptr_t>();

			// store old bytes
			if (Pointers.C){ReadMemory((uintptr_t(Pointers.C)), &Patches.C_OLD, 0x07); }
			if (Pointers.D){ReadMemory((uintptr_t(Pointers.D)), &Patches.D_OLD, 0x07); }
			if (Pointers.F){ReadMemory((uintptr_t(Pointers.F)), &Patches.F_OLD, 0x06); }
			if (Pointers.redcall) {ReadMemory((uintptr_t(Pointers.redcall)), &Patches.redcall_OLD, 0x05); }
			if (Pointers.osnap) {ReadMemory((uintptr_t(Pointers.osnap)), &Patches.OSNAP_OLD, 0x08); }

			//wssize
			if (WSSize.func) {
				ReadMemory((uintptr_t(WSSize.func) + 0x00), &Patches.DRAWS_OLD, 0x06); //draws
				ReadMemory((uintptr_t(WSSize.func) + 0x0A), &Patches.TRIS_OLD, 0x06); //triangles
				WSSize.r32 = GetRel32FromPattern(WSSize.func, 0x02, 0x06, 0x00); // rel32 of draws
				WSSize.addr = RelocationManager::s_baseAddr + (uintptr_t)WSSize.r32;
			}

			//zoom and rotate
			if (Zoom.func && Rotate.func) {
				Zoom.r32 = GetRel32FromPattern(Zoom.func, 0x04, 0x08, 0x00);
				Rotate.r32 = GetRel32FromPattern(Rotate.func, 0x04, 0x08, 0x00);
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
			if (WSMode.func) {
				WSMode.r32 = GetRel32FromPattern(WSMode.func, 0x02, 0x07, 0x00);
				WSMode.addr = RelocationManager::s_baseAddr + WSMode.r32;
			}

			//setscale
			if (ScaleFuncs.setpattern) {
				ScaleFuncs.s32 = GetRel32FromPattern(ScaleFuncs.setpattern, 0x01, 0x05, 0x00);
				RelocAddr <_SetScale_Native> GimmeSetScale(ScaleFuncs.s32);
				ScaleFuncs.SetScale = GimmeSetScale;
			}

			//getscale
			if (ScaleFuncs.getpattern) {
				ScaleFuncs.g32 = GetRel32FromPattern(ScaleFuncs.getpattern, 0x08, 0x0C, 0x00);
				RelocAddr <_GetScale_Native> GimmeGetScale(ScaleFuncs.g32);
				ScaleFuncs.GetScale = GimmeGetScale;
			}

			//g_console
			if (gConsole.func) {
				gConsole.r32 = GetRel32FromPattern(gConsole.func, 0x03, 0x07, 0x00);
				gConsole.addr = RelocationManager::s_baseAddr + (uintptr_t)gConsole.r32;
			}

			//g_datahandler
			if (gDataHandler.func) {
				gDataHandler.r32 = GetRel32FromPattern(gDataHandler.func, 0x03, 0x08, 0x00);
				gDataHandler.addr = RelocationManager::s_baseAddr + (uintptr_t)gDataHandler.r32;
			}

			//CurrentWSRef
			if (CurrentWSRef.func) {
				CurrentWSRef.r32 = GetRel32FromPattern(CurrentWSRef.func, 0x03, 0x07, 0x00);
				CurrentWSRef.addr = RelocationManager::s_baseAddr + (uintptr_t)CurrentWSRef.r32;
			}

			//GetConsoleArg
			if (GetConsoleArg.func) {
				GetConsoleArg.r32 = uintptr_t(GetConsoleArg.func) - RelocationManager::s_baseAddr;
				RelocAddr <_GetConsoleArg_Native> GetDatArg(GetConsoleArg.r32);
				GetConsoleArg_Native = GetDatArg;
			}

			//first console
			if (FirstConsole.func) {
				FirstConsole.r32 = GetRel32FromPattern(FirstConsole.func, 0x03, 0x07, -0x08);
				RelocPtr <ObScriptCommand> _FirstConsole(FirstConsole.r32);
				FirstConsole.cmd = _FirstConsole;
			}

			//first obscript
			if (FirstObScript.func) {
				FirstObScript.r32 = GetRel32FromPattern(FirstObScript.func, 0x03, 0x07, -0x08);
				RelocPtr <ObScriptCommand> _FirstObScript(FirstObScript.r32);
				FirstObScript.cmd = _FirstObScript;
			}

			// setmotiontype
			if (SetMotionType.func) {
				SetMotionType.r32 = uintptr_t(SetMotionType.func) - RelocationManager::s_baseAddr;
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
		pirlog.FormattedMessage("[%s] Starting up!", thisfunc);

		if (!pir::InitF4SE(f4se)){
			pirlog.FormattedMessage("[%s] F4SE init failed! The plugin will not load.", thisfunc);
			return false;
		}

		pir::InitPIR();

		if (!pir::FoundRequiredMemoryPatterns()){
			pir::LogMemoryPatterns();
			return false;
		}
		
		if (!pir::CreateConsoleCMD("GameComment"))
		{
			pirlog.FormattedMessage("[%s] Failed to create console command! Plugin will run with defaults.", thisfunc);
		}

		// toggle defaults
		pir::ReadINIDefaults();

		// plugin loaded
		pirlog.FormattedMessage("[%s] Plugin load finished!", thisfunc);
		pir::LogMemoryPatterns();
				
		return true;
	}


	__declspec(dllexport) F4SEPluginVersionData F4SEPlugin_Version = {
		F4SEPluginVersionData::kVersion,
		pluginVersion,
		"PlaceInRed",
		"RandyConstan",
		0,
		0,
		{	RUNTIME_VERSION_1_10_980, 1,
			RUNTIME_VERSION_1_10_984, 1
		},
		0,
	};







}//end of extern c

