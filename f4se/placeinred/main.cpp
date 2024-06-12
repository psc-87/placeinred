#include "main.h"

// SetScale
static _SetScale SetScale = nullptr;
static uintptr_t* SetScaleFinder = nullptr;
static SInt32 SetScaleRel32 = 0;

// GetScale
static _GetScale GetScale = nullptr;
static uintptr_t* GetScaleFinder = nullptr;
static SInt32 GetScaleRel32 = 0;

// ConsoleNameFix (credit registrator2000)
static uintptr_t* ConsoleRefFuncFinder = nullptr; //pattern to find the function that really gives us the ref name
static SInt32 ConsoleRefFuncRel32 = 0; // rel32
static uintptr_t ConsoleRefFuncAddress; //the address for the function that really gives us the ref name
static uintptr_t* ConsoleRefCallFinder = nullptr; //function called when references are clicked

// Console command creation and parsing. Credit to reg2k
static _GetConsoleArg GetConsoleArg; // to view console command arguments from user after execution
static uintptr_t* ConsoleArgFinder = nullptr; // memory pattern to find the real function
static SInt32 ConsoleArgRel32 = 0; // rel32 set later on
static const char* s_CommandToBorrow = "GameComment"; // the command we will replace (full name)
static ObScriptCommand* s_hijackedCommand = nullptr; 
static ObScriptParam* s_hijackedCommandParams = nullptr;

// First Console Command
static uintptr_t* FirstConsoleFinder = nullptr; // pattern where the first console command is referenced
static ObScriptCommand* FirstConsole = nullptr; // obscriptcommand first console command
static SInt32 FirstConsoleRel32 = 0; // rel32 set later on

// First ObScript Command
static uintptr_t* FirstObScriptFinder = nullptr; // pattern where the first Obscript command is referenced
static ObScriptCommand* FirstObScript = nullptr; // obscriptcommand first console command
static SInt32 FirstObScriptRel32 = 0; // rel32 set later on

//gconsole console print
static uintptr_t* GConsoleFinder = nullptr; //pattern to find g_console
static SInt32 GConsoleRel32 = 0; // rel32 set later on
static uintptr_t GConsoleStatic; // g_console

// Currently grabbed or highlighted workshop reference
// multi level pointer. always contains the highlighted or grabbed ref
static uintptr_t* CurrentRefFinder = nullptr; // pattern to help us find it
static uintptr_t CurrentRefBase; // base address
static SInt32 CurrentRefBaseRel32 = 0; // rel32 of base address

// useful offsets
static UInt64 bsfadenode_offsets[] = { 0x0, 0x0, 0x10 }; //bsfadenode
static UInt64 ref_offsets[] = { 0x0, 0x0, 0x10, 0x110 }; //TESObjectREFR
static UInt64 collision_offsets[] = { 0x0, 0x0, 0x10, 0x100 }; //bhkNiCollisionObject
static size_t collision_count = sizeof(collision_offsets) / sizeof(collision_offsets[0]);
static size_t ref_count = sizeof(ref_offsets) / sizeof(ref_offsets[0]);
static size_t bsfadenode_count = sizeof(bsfadenode_offsets) / sizeof(bsfadenode_offsets[0]);

// workshop mode finder
static uintptr_t* WorkshopModeFinder = nullptr;
static SInt32 WorkshopModeFinderRel32 = 0;
static uintptr_t WorkshopModeBoolAddress;

//g_gamedata
//auto lightMods = (*g_dataHandler)->modList.lightMods;
static uintptr_t* GDataHandlerFinder = nullptr;
static SInt32 GDataHandlerRel32 = 0;
static uintptr_t GDataHandlerStatic; // g_dataHandler RelocPtr <DataHandler*> g_dataHandler(0x02E64E68);

// Pointers to memory patterns
static uintptr_t* CHANGE_A = nullptr;
static uintptr_t* CHANGE_B = nullptr;
static uintptr_t* CHANGE_C = nullptr;
static uintptr_t* CHANGE_D = nullptr;
static uintptr_t* CHANGE_E = nullptr;
static uintptr_t* CHANGE_F = nullptr;
static uintptr_t* CHANGE_G = nullptr;
static uintptr_t* CHANGE_H = nullptr;
static uintptr_t* CHANGE_I = nullptr;
static uintptr_t* RED = nullptr;
static uintptr_t* YELLOW = nullptr;
static uintptr_t* WSTIMER2 = nullptr;
static uintptr_t* GROUNDSNAP = nullptr;
static uintptr_t* OBJECTSNAP = nullptr;
static uintptr_t* WSSIZE = nullptr;
static uintptr_t* OUTLINES = nullptr;
static uintptr_t* ACHIEVEMENTS = nullptr;
static uintptr_t* ZOOM = nullptr;
static uintptr_t* ROTATE = nullptr;

// For proper toggling 
static UInt8 CHANGE_C_OLDCODE[7];
static UInt8 CHANGE_C_NEWCODE[7] = { 0x31, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90 }; //xor al,al;nop x5
static UInt8 CHANGE_D_OLDCODE[7];
static UInt8 CHANGE_D_NEWCODE[7] = { 0x31, 0xC0, 0xB0, 0x01, 0x90, 0x90, 0x90 }; //xor al,al;mov al,01;nop x3
static UInt8 CHANGE_F_OLDCODE[2] = { 0x88, 0x05 };
static UInt8 CHANGE_F_NEWCODE[2] = { 0xEB, 0x04 };
static UInt8 CHANGE_I_OLDCODE[2] = { 0x74, 0x35 };
static UInt8 CHANGE_I_NEWCODE[2] = { 0xEB, 0x30 };
static UInt8 YELLOW_NEWCODE[3] = { 0x90, 0x90, 0x90 }; //nop x3
static UInt8 YELLOW_OLDCODE[3] = { 0x8B, 0x58, 0x14 };
static UInt8 WSTIMER2_OLDCODE[8];
static UInt8 WSTIMER2_NEWCODE[8] = { 0x0F, 0x57, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90 }; //xorps xmm0,xmm0; nop x5

// Allows achievements with mods and prevents game adding [MODS] in save file name
static UInt8 ACHIEVEMENTS_NEWCODE[3] = { 0x30, 0xC0, 0xC3 }; // xor al, al; ret
static UInt8 ACHIEVEMENTS_OLDCODE[4] = { 0x48, 0x83, 0xEC, 0x28 }; // sub rsp,28

// Object snap
static UInt8 OBJECTSNAP_OLDCODE[8];
static UInt64 OBJECTSNAP_NEWCODE = 0x9090909090F6570F; // xorps xmm6, xmm6; nop x5

// Workshop size
static SInt32 WSSIZE_REL32 = 0;
static UInt8 WORKSHOPSIZE_DRAWS_OLDCODE[6];
static UInt8 WORKSHOPSIZE_DRAWS_NEWCODE[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
static UInt8 WORKSHOPSIZE_TRIANGLES_OLDCODE[6];
static UInt8 WORKSHOPSIZE_TRIANGLES_NEWCODE[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

// zoom and rotate
static SInt32 ZOOM_REL32 = 0;
static SInt32 ROTATE_REL32 = 0;
static UInt8 fZOOM_DEFAULT[4] = { 0x00, 0x00, 0x20, 0x41 }; // 10.0f
static UInt8 fZOOM_SLOWED[4] = { 0x00, 0x00, 0x80, 0x3F }; // 1.0f
static UInt8 fROTATE_DEFAULT[4] = { 0x00, 0x00, 0xA0, 0x40 }; // 5.0f
static UInt8 fROTATE_SLOWED[4] = { 0x00, 0x00, 0x00, 0x3F }; // 0.5f

// On and off switches for toggling. These are the baked in defaults
static bool PLACEINRED_ENABLED = false; //false, toggled on during F4SEPlugin_Load
static bool ACHIEVEMENTS_ENABLED = false; //false, toggled on during F4SEPlugin_Load
static bool OBJECTSNAP_ENABLED = true; //true, game default
static bool GROUNDSNAP_ENABLED = true; // true, game default
static bool SLOW_ENABLED = false; // false, game default
static bool WORKSHOPSIZE_ENABLED = false; // false, game default
static bool OUTLINES_ENABLED = true; // true, game default


extern "C" {

namespace pir {

	const char* logprefix = {"pir"};

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
			}
			else {
				relish32 = (((uintptr_t(pattern) + rel32end) + relish32) - RelocationManager::s_baseAddr) + (specialmodify);
				return relish32;
			}
		}
		return 0;
	}

	// read the address at an address+offset
	static uintptr_t GimmeSinglePointer(uintptr_t address, UInt64 offset) {
		uintptr_t result = 0;
		if (ReadMemory(address + offset, &result, sizeof(uintptr_t))) {
			return result;
		}
		else {
			return 0;
		}
	}

	//return a pointer to a base address with offets
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

	// Determine if player is in workshop mode
	static bool InWorkshopMode() {
		PIR_LOG_PREP
			if (WorkshopModeFinder) {
				UInt8 WSMODE = 0x00;
				ReadMemory(uintptr_t(WorkshopModeBoolAddress), &WSMODE, sizeof(bool));
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
		if (CurrentRefFinder && CurrentRefBase && pir::InWorkshopMode()) {

			uintptr_t* refptr = GimmeMultiPointer(CurrentRefBase, ref_offsets, ref_count);
			TESObjectREFR* ref = (TESObjectREFR*)(refptr);

			if (ref)
			{
				if (!ref->formID) { return nullptr;	}
				if (ref->formID <= 0) { return nullptr;	}

				//optional but checks by default
				if (refonly){
					if (ref->formType != 0x40) { 
						return nullptr; 
					} 
				}
				//return the ref if we get here
				return ref;

			}
		}
		return nullptr;
	}

	// dump cell refids and position to the log file
	static void DumpCellRefs()
	{
		PIR_LOG_PREP
		if (CurrentRefFinder && CurrentRefBase) {
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

	}

	// To switch with strings
	static constexpr unsigned int ConsoleSwitch(const char* s, int off = 0) {
		return !s[off] ? 5381 : (ConsoleSwitch(s, off + 1) * 33) ^ s[off];
	}

	// copied from f4se and modified for use with the pattern
	static void ConsolePrint(const char* fmt, ...)
	{
		
		if (GConsoleFinder && GConsoleStatic)
		{
			ConsoleManager* mgr = (ConsoleManager*)GConsoleStatic;

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

	// f4se message interface handler
	static void MessageInterfaceHandler(F4SEMessagingInterface::Message* msg) {
		PIR_LOG_PREP
		switch (msg->type) {
		//case F4SEMessagingInterface::kMessage_PostLoad: pirlog.FormattedMessage("[MessageInterfaceHandler] kMessage_PostLoad"); break;
		//case F4SEMessagingInterface::kMessage_PostPostLoad: pirlog.FormattedMessage("[MessageInterfaceHandler] kMessage_PostPostLoad"); break;
		//case F4SEMessagingInterface::kMessage_PreLoadGame: pirlog.FormattedMessage("[MessageInterfaceHandler] kMessage_PreLoadGame"); break;
		//case F4SEMessagingInterface::kMessage_PostLoadGame: pirlog.FormattedMessage("[MessageInterfaceHandler] kMessage_PostLoadGame"); break;
		//case F4SEMessagingInterface::kMessage_PreSaveGame: pirlog.FormattedMessage("[MessageInterfaceHandler] kMessage_PreSaveGame"); break;
		//case F4SEMessagingInterface::kMessage_PostSaveGame: pirlog.FormattedMessage("[MessageInterfaceHandler] kMessage_PostSaveGame"); break;
		//case F4SEMessagingInterface::kMessage_DeleteGame: pirlog.FormattedMessage("[MessageInterfaceHandler] kMessage_DeleteGame"); break;
		//case F4SEMessagingInterface::kMessage_InputLoaded: pirlog.FormattedMessage("[MessageInterfaceHandler] kMessage_InputLoaded"); break;
		//case F4SEMessagingInterface::kMessage_NewGame: pirlog.FormattedMessage("[MessageInterfaceHandler] kMessage_NewGame"); break;
		//case F4SEMessagingInterface::kMessage_GameLoaded: pirlog.FormattedMessage("[MessageInterfaceHandler] kMessage_GameLoaded"); break;
		case F4SEMessagingInterface::kMessage_GameDataReady: pirlog.FormattedMessage("[%s::%s] kMessage_GameDataReady", logprefix, thisfunc); break;
		//default: pirlog.FormattedMessage("[MessageInterfaceHandler] kMessage_UNKNOWN type: %0X", msg->type); break;
		default: break;
		}
	}

	// Set the scale of the current workshop reference (highlighted or grabbed)
	static bool SetCurrentRefScale(float newScale)
	{
		PIR_LOG_PREP
		TESObjectREFR* ref = GetCurrentWSRef();
		if (ref) {
			SetScale(ref, newScale);
			return true;
		}
		return false;
	}

	//Move reference to itself
	static void MoveRefToSelf(float modx = 0, float mody = 0, float modz = 0, int repeat=0)
	{
		PIR_LOG_PREP
		TESObjectREFR* ref = GetCurrentWSRef();
		if (ref) {
			UInt32 nullHandle = *g_invalidRefHandle;
			TESObjectCELL* parentCell = ref->parentCell;
			TESWorldSpace* worldspace = CALL_MEMBER_FN(ref, GetWorldspace)();

			NiPoint3 newPos;
			newPos.x = ref->pos.x + modx;
			newPos.y = ref->pos.y + mody;
			newPos.z = ref->pos.z + modz;

			NiPoint3 newRot;
			newRot.x = ref->rot.x + 0;
			newRot.y = ref->rot.y + 0;
			newRot.z = ref->rot.z + 0;
			
			for (int i=0; i<=repeat; i++)
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
			float oldscale = GetScale(ref);
			float newScale = oldscale * (fMultiplyAmount);
			if (newScale > 9.9999f) { newScale = 9.9999f; }
			if (newScale < 0.0001f) { newScale = 0.0001f; } //kFloatEpsilon
			SetScale(ref, newScale);
			pir::MoveRefToSelf(0, 0, 0, 1); //repeat once to fix jitter
			return true;
		}
		return false;
	}

	//dump all console and obscript commands to the log file
	static bool DumpCmds()
	{
		PIR_LOG_PREP

		if (FirstConsole == nullptr || FirstObScript == nullptr) {
			return false;
		}

		for (ObScriptCommand* iter = FirstConsole; iter->opcode < (kObScript_NumConsoleCommands + kObScript_ConsoleOpBase); ++iter) {
			if (iter) {
				uintptr_t exeaddr = (uintptr_t) & (iter->execute);
				uint64_t* funcptr = (uint64_t*)(exeaddr);
				pirlog.FormattedMessage("Console|%08X|%p|%s|%s|%X|%X|%s", iter->opcode, *funcptr, iter->shortName, iter->longName, iter->numParams, iter->needsParent, iter->helpText);
			}
		}

		for (ObScriptCommand* iter = FirstObScript; iter->opcode < (kObScript_NumObScriptCommands + kObScript_ScriptOpBase); ++iter) {
			if (iter) {
				uintptr_t exeaddr = (uintptr_t) & (iter->execute);
				uint64_t* funcptr = (uint64_t*)(exeaddr);
				pirlog.FormattedMessage("ObScript|%08X|%p|%s|%s|%X|%X|%s", iter->opcode, *funcptr, iter->shortName, iter->longName, iter->numParams, iter->needsParent, iter->helpText);
			}
		}

		return true;
	}

	//toggle object outlines on next object
	static bool Toggle_Outlines()
	{
		PIR_LOG_PREP
		if (OUTLINES && OUTLINES_ENABLED) {
			SafeWrite8((uintptr_t)OUTLINES + 0x06, 0x00); //objects
			SafeWrite8((uintptr_t)OUTLINES + 0x0D, 0xEB); //npcs
			OUTLINES_ENABLED = false;
			pir::ConsolePrint("Object outlines disabled");
			return true;
		}
		if (OUTLINES && !OUTLINES_ENABLED) {
			SafeWrite8((uintptr_t)OUTLINES + 0x06, 0x01); //objects
			SafeWrite8((uintptr_t)OUTLINES + 0x0D, 0x76); //npcs
			OUTLINES_ENABLED = true;
			pir::ConsolePrint("Object outlines enabled");
			return true;
		}
		return false;
	}

	//toggle slower object rotation and zoom speed
	static bool Toggle_SlowZoomAndRotate()
	{
		PIR_LOG_PREP
		// its on, turn it off
		if (ZOOM && ROTATE && ZOOM_REL32 != 0 && ROTATE_REL32 != 0 && SLOW_ENABLED) {
			SafeWriteBuf(uintptr_t(ZOOM) + (static_cast<uintptr_t>(ZOOM_REL32) + 8), fZOOM_DEFAULT, sizeof(fZOOM_DEFAULT));
			SafeWriteBuf(uintptr_t(ROTATE) + (static_cast<uintptr_t>(ROTATE_REL32) + 8), fROTATE_DEFAULT, sizeof(fROTATE_DEFAULT));
			SLOW_ENABLED = false;
			pir::ConsolePrint("Slow zoom and rotate disabled");
			return true;
		}
		// its off, turn it on
		if (ZOOM && ROTATE && ZOOM_REL32 != 0 && ROTATE_REL32 != 0 && !SLOW_ENABLED) {
			SafeWriteBuf(uintptr_t(ZOOM) + (static_cast<uintptr_t>(ZOOM_REL32) + 8), fZOOM_SLOWED, sizeof(fZOOM_SLOWED));
			SafeWriteBuf(uintptr_t(ROTATE) + (static_cast<uintptr_t>(ROTATE_REL32) + 8), fROTATE_SLOWED, sizeof(fROTATE_SLOWED));
			SLOW_ENABLED = true;
			pir::ConsolePrint("Slow zoom and rotate enabled");
			return true;
		}
		return false;
	}

	//toggle infinite workshop size
	static bool Toggle_WorkshopSize()
	{
		PIR_LOG_PREP
		if (WSSIZE && WORKSHOPSIZE_ENABLED) {
			SafeWriteBuf((uintptr_t)WSSIZE, WORKSHOPSIZE_DRAWS_OLDCODE, sizeof(WORKSHOPSIZE_DRAWS_OLDCODE));
			SafeWriteBuf((uintptr_t)WSSIZE + 0x0A, WORKSHOPSIZE_TRIANGLES_OLDCODE, sizeof(WORKSHOPSIZE_TRIANGLES_OLDCODE));
			WORKSHOPSIZE_ENABLED = false;
			pir::ConsolePrint("Unlimited workshop size disabled");
			return true;
		}

		if (WSSIZE && WORKSHOPSIZE_ENABLED == false) {

			SafeWriteBuf((uintptr_t)WSSIZE, WORKSHOPSIZE_DRAWS_NEWCODE, sizeof(WORKSHOPSIZE_DRAWS_NEWCODE));
			SafeWriteBuf((uintptr_t)WSSIZE + 0x0A, WORKSHOPSIZE_TRIANGLES_NEWCODE, sizeof(WORKSHOPSIZE_TRIANGLES_NEWCODE));

			// set draws and triangles to zero
			SafeWrite64(uintptr_t(WSSIZE) + (static_cast<uintptr_t>(WSSIZE_REL32) + 6), 0x0000000000000000);
			WORKSHOPSIZE_ENABLED = true;
			pir::ConsolePrint("Unlimited workshop size enabled");
			return true;
		}
		return false;
	}

	//toggle groundsnap (instant)
	static bool Toggle_GroundSnap()
	{
		PIR_LOG_PREP
		if (GROUNDSNAP && GROUNDSNAP_ENABLED) {

			SafeWrite8((uintptr_t)GROUNDSNAP + 0x01, 0x85);
			GROUNDSNAP_ENABLED = false;
			pir::ConsolePrint("Ground snap disabled");
			return true;
		}
		if (GROUNDSNAP && !GROUNDSNAP_ENABLED) {
			SafeWrite8((uintptr_t)GROUNDSNAP + 0x01, 0x86);
			GROUNDSNAP_ENABLED = true;
			pir::ConsolePrint("Ground snap enabled");
			return true;

		}
		return false;
	}

	//toggle objectsnap (instant)
	static bool Toggle_ObjectSnap()
	{
		PIR_LOG_PREP
		if (OBJECTSNAP && OBJECTSNAP_ENABLED) {
			SafeWrite64((uintptr_t)OBJECTSNAP, OBJECTSNAP_NEWCODE);
			OBJECTSNAP_ENABLED = false;
			pir::ConsolePrint("Object snap disabled");
			return true;
		}
		if (OBJECTSNAP && !OBJECTSNAP_ENABLED) {
			SafeWriteBuf((uintptr_t)OBJECTSNAP, OBJECTSNAP_OLDCODE, sizeof(OBJECTSNAP_OLDCODE));
			OBJECTSNAP_ENABLED = true;
			pir::ConsolePrint("Object snap enabled");
			return true;
		}
		return false;
	}

	//toggle allowing achievements with mods
	static bool Toggle_Achievements()
	{
		PIR_LOG_PREP
		// its on - toggle it off
		if (ACHIEVEMENTS && ACHIEVEMENTS_ENABLED) {
			SafeWriteBuf((uintptr_t)ACHIEVEMENTS, ACHIEVEMENTS_OLDCODE, sizeof(ACHIEVEMENTS_OLDCODE));
			ACHIEVEMENTS_ENABLED = false;
			pir::ConsolePrint("Achievements with mods disabled (game default)");
			return true;
		}
		// its off - toggle it on
		if (ACHIEVEMENTS && !ACHIEVEMENTS_ENABLED) {
			SafeWriteBuf((uintptr_t)ACHIEVEMENTS, ACHIEVEMENTS_NEWCODE, sizeof(ACHIEVEMENTS_NEWCODE));
			ACHIEVEMENTS_ENABLED = true;
			pir::ConsolePrint("Achievements with mods enabled!");
			return true;
		}
		return false;
	}

	//toggle placing objects in red
	static bool Toggle_PlaceInRed()
	{
		PIR_LOG_PREP
		if (PLACEINRED_ENABLED) {
			SafeWrite8((uintptr_t)CHANGE_A + 0x06, 0x01);
			SafeWrite8((uintptr_t)CHANGE_A + 0x0C, 0x02);
			SafeWrite8((uintptr_t)CHANGE_B + 0x01, 0x01);
			SafeWriteBuf((uintptr_t)CHANGE_C, CHANGE_C_OLDCODE, sizeof(CHANGE_C_OLDCODE));
			SafeWriteBuf((uintptr_t)CHANGE_D, CHANGE_D_OLDCODE, sizeof(CHANGE_D_OLDCODE));
			SafeWrite8((uintptr_t)CHANGE_E + 0x00, 0x76);
			SafeWriteBuf((uintptr_t)CHANGE_F, CHANGE_F_OLDCODE, sizeof(CHANGE_F_OLDCODE));
			SafeWrite8((uintptr_t)CHANGE_G + 0x01, 0x95);
			SafeWrite8((uintptr_t)CHANGE_H + 0x00, 0x74);
			SafeWriteBuf((uintptr_t)CHANGE_I, CHANGE_I_OLDCODE, sizeof(CHANGE_I_OLDCODE));
			SafeWrite8((uintptr_t)RED + 0xC, 0x01);
			SafeWriteBuf((uintptr_t)YELLOW, YELLOW_OLDCODE, sizeof(YELLOW_OLDCODE));
			SafeWriteBuf((uintptr_t)WSTIMER2, WSTIMER2_OLDCODE, sizeof(WSTIMER2_OLDCODE));
			PLACEINRED_ENABLED = false;
			pir::ConsolePrint("Place In Red disabled.");
			return true;
		}

		if (!PLACEINRED_ENABLED) {
			SafeWrite8((uintptr_t)CHANGE_A + 0x06, 0x00);
			SafeWrite8((uintptr_t)CHANGE_A + 0x0C, 0x01);
			SafeWrite8((uintptr_t)CHANGE_B + 0x01, 0x00);
			SafeWriteBuf((uintptr_t)CHANGE_C, CHANGE_C_NEWCODE, sizeof(CHANGE_C_NEWCODE));
			SafeWriteBuf((uintptr_t)CHANGE_D, CHANGE_D_NEWCODE, sizeof(CHANGE_D_NEWCODE));
			SafeWrite8((uintptr_t)CHANGE_E + 0x00, 0xEB);
			SafeWriteBuf((uintptr_t)CHANGE_F, CHANGE_F_NEWCODE, sizeof(CHANGE_F_NEWCODE));
			SafeWrite8((uintptr_t)CHANGE_G + 0x01, 0x98); // works but look at again later
			SafeWrite8((uintptr_t)CHANGE_H + 0x00, 0xEB);   
			SafeWriteBuf((uintptr_t)CHANGE_I, CHANGE_I_NEWCODE, sizeof(CHANGE_I_NEWCODE));
			SafeWrite8((uintptr_t)RED + 0xC, 0x00);
			SafeWriteBuf((uintptr_t)YELLOW, YELLOW_NEWCODE, sizeof(YELLOW_NEWCODE));
			SafeWriteBuf((uintptr_t)WSTIMER2, WSTIMER2_NEWCODE, sizeof(WSTIMER2_NEWCODE));
			PLACEINRED_ENABLED = true;
			pir::ConsolePrint("Place In Red enabled.");
			return true;
		}

		return false;
	}

	//Log details about the selected workshop ref
	static void LogWSRef()
	{
		PIR_LOG_PREP
		TESObjectREFR* ref = GetCurrentWSRef(0); // 0 to remove formtype restriction
		if (ref) {
			pirlog.FormattedMessage("-------------------------------------------------------------------------------------");

			//TESObjectCELL*		cell =				ref->parentCell;
			//TESWorldSpace*		worldspace =		ref->parentCell->worldSpace;
			//bhkWorld*				havokworld =		CALL_MEMBER_FN(cell, GetHavokWorld)();
			//const char*			edid =				ref->GetEditorID();
			//const char*			fullname =			ref->GetFullName();

			UInt8					formtype =			ref->GetFormType();

			UInt32					formid =			ref->formID;
			UInt32					refflags =			ref->flags;
			UInt32					cellformid =		ref->parentCell->formID;
			UInt64					rootflags =			ref->GetObjectRootNode()->flags;
			const char*				rootname =			ref->GetObjectRootNode()->m_name.c_str();
			UInt16					rootchildren =		ref->GetObjectRootNode()->m_children.m_size;
			float					Px = ref->pos.x;
			float					Py = ref->pos.y;
			float					Pz = ref->pos.z;
			float					Rx = ref->rot.z;
			float					Ry = ref->rot.z;
			float					Rz = ref->rot.z;

			pirlog.FormattedMessage("Pos             : %f %f %f", Px, Py, Pz);
			pirlog.FormattedMessage("Rot             : %f %f %f", Rx, Ry, Rz);
			pirlog.FormattedMessage("parentcell      : %04X", cellformid);
			pirlog.FormattedMessage("root->m_name    : %s", rootname);
			pirlog.FormattedMessage("root->m_children: %02X", rootchildren);
			pirlog.FormattedMessage("ref->formtype   : %01X (%d)", formtype, formtype);
			pirlog.FormattedMessage("ref->formID     : %04X", formid);
			//pirlog.FormattedMessage("ref->editorID : %s", edid);
			//pirlog.FormattedMessage("ref->fullname : %s", fullname);
			pirlog.FormattedMessage("ref->flags      : %04X", refflags);
			
			pirlog.FormattedMessage("Unknowns: %08X %08X %08X %08X %08X %08X %08X", ref->unk104, ref->unk108, ref->unk18, ref->unk1B, ref->unk60, ref->unk68, ref->unk70);
			pirlog.FormattedMessage("Unknowns: %08X %08X %08X %08X %08X %08X %08X", ref->unk74, ref->unk78, ref->unk7C, ref->unk80, ref->unk88, ref->unk90, ref->unk98);
			pirlog.FormattedMessage("Unknowns: %08X %08X %08X %08X %08X %08X %08X %08X", ref->unkA0, ref->unkA8, ref->unkB0, ref->unkCC, ref->unkDC, ref->unkE8, ref->unkF0, ref->unk08);
			pirlog.FormattedMessage("-------------------------------------------------------------------------------------");

		}
	}

	//called every time the console command runs
	static bool ExecuteConsoleCommand(void* paramInfo, void* scriptData, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, double* result, void* opcodeOffsetPtr)
	{
		PIR_LOG_PREP
		if (GetConsoleArg && ConsoleArgFinder && (ConsoleArgRel32 != 0)) {

			char consolearg[4096];
			bool consoleresult = GetConsoleArg(paramInfo, scriptData, opcodeOffsetPtr, thisObj, containingObj, scriptObj, locals, &consolearg);

			if (consoleresult && consolearg[0]) {
				switch (ConsoleSwitch(consolearg)) {
					// debug and tests
					case pir::ConsoleSwitch("dumprefs"):        pir::DumpCellRefs();                 break;
					case pir::ConsoleSwitch("dump"):            pir::DumpCmds();                     break;
					case pir::ConsoleSwitch("logref"):          pir::LogWSRef();                     break;
					case pir::ConsoleSwitch("moveself"):        pir::MoveRefToSelf(0,0,0,0);         break;
					case pir::ConsoleSwitch("moveselftwice"):   pir::MoveRefToSelf(0,0,0,1);         break;

					//toggles
					case pir::ConsoleSwitch("1"):               pir::Toggle_PlaceInRed();            break;
					case pir::ConsoleSwitch("toggle"):          pir::Toggle_PlaceInRed();            break;
					case pir::ConsoleSwitch("2"):               pir::Toggle_ObjectSnap();            break;
					case pir::ConsoleSwitch("osnap"):           pir::Toggle_ObjectSnap();            break;
					case pir::ConsoleSwitch("3"):               pir::Toggle_GroundSnap();            break;
					case pir::ConsoleSwitch("gsnap"):           pir::Toggle_GroundSnap();            break;
					case pir::ConsoleSwitch("4"):               pir::Toggle_SlowZoomAndRotate();     break;
					case pir::ConsoleSwitch("slow"):            pir::Toggle_SlowZoomAndRotate();     break;
					case pir::ConsoleSwitch("5"):               pir::Toggle_WorkshopSize();          break;
					case pir::ConsoleSwitch("workshopsize"):    pir::Toggle_WorkshopSize();          break;
					case pir::ConsoleSwitch("6"):               pir::Toggle_Outlines();              break;
					case pir::ConsoleSwitch("outlines"):        pir::Toggle_Outlines();              break;
					case pir::ConsoleSwitch("7"):               pir::Toggle_Achievements();          break;
					case pir::ConsoleSwitch("achievements"):    pir::Toggle_Achievements();          break;
																								     
					//scale up and fix jitter													     
					case pir::ConsoleSwitch("scaleup1"):		pir::ModCurrentRefScale(1.0100f);   break;
					case pir::ConsoleSwitch("scaleup5"):		pir::ModCurrentRefScale(1.0500f);   break;
					case pir::ConsoleSwitch("scaleup10"):		pir::ModCurrentRefScale(1.1000f);   break;
					case pir::ConsoleSwitch("scaleup25"):		pir::ModCurrentRefScale(1.2500f);   break;
					case pir::ConsoleSwitch("scaleup50"):		pir::ModCurrentRefScale(1.5000f);   break;
					case pir::ConsoleSwitch("scaleup100"):		pir::ModCurrentRefScale(2.0000f);   break;
																								     
					//scale down and fix jitter													     
					case pir::ConsoleSwitch("scaledown1"):		pir::ModCurrentRefScale(0.9900f);   break;
					case pir::ConsoleSwitch("scaledown5"):		pir::ModCurrentRefScale(0.9500f);   break;
					case pir::ConsoleSwitch("scaledown10"):		pir::ModCurrentRefScale(0.9000f);   break;
					case pir::ConsoleSwitch("scaledown25"):		pir::ModCurrentRefScale(0.7500f);   break;
					case pir::ConsoleSwitch("scaledown50"):		pir::ModCurrentRefScale(0.5000f);   break;
					case pir::ConsoleSwitch("scaledown75"):		pir::ModCurrentRefScale(0.2500f);   break;

					default: pir::ConsolePrint(pirunknowncommandmsg);  break;
				}
				return true;
			}

		}
	pirlog.FormattedMessage("[%s::%s] Failed to execute the console command!", logprefix, thisfunc);
	return false;
	}

	//attempt to create the console command
	static bool CreateConsoleCommand()
	{
		PIR_LOG_PREP
		if (FirstConsole == nullptr) {
			return false;
		}

		for (ObScriptCommand* iter = FirstConsole; iter->opcode < (kObScript_NumConsoleCommands + kObScript_ConsoleOpBase); ++iter) {
			if (!strcmp(iter->longName, s_CommandToBorrow)) {
				s_hijackedCommand = iter;
				s_hijackedCommandParams = iter->params;
				break;
			}
		}

		if (s_hijackedCommand && s_hijackedCommandParams) {
			ObScriptCommand cmd = *s_hijackedCommand;
			cmd.longName = "placeinred";
			cmd.shortName = "pir";
			cmd.helpText = "pir [option] example: pir toggle, pir 1, pir 2, pir 3";
			cmd.needsParent = 0;
			cmd.numParams = 1;
			cmd.execute = pir::ExecuteConsoleCommand;
			cmd.params = s_hijackedCommandParams;
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
		if (ConsoleArgFinder && FirstConsoleFinder && FirstObScriptFinder && SetScaleFinder && GetScaleFinder && CurrentRefFinder
			&& WorkshopModeFinder && GConsoleFinder && GDataHandlerFinder && CHANGE_A && CHANGE_B && CHANGE_C && CHANGE_D && CHANGE_E && CHANGE_F && CHANGE_G && CHANGE_H && CHANGE_I
			&& YELLOW && RED && WSTIMER2 && GROUNDSNAP && OBJECTSNAP && OUTLINES && WSSIZE && ZOOM && ROTATE)
		{
			return true;
		}
		else {
			return false;
		}
	}

	//log all the memory patterns to the log file
	static void LogMemoryPatterns()
	{
		pirlog.FormattedMessage("----------------------------------------------------------------");
		pirlog.FormattedMessage("Base                 : %p", RelocationManager::s_baseAddr);
		pirlog.FormattedMessage("ConsoleArgFinder     : %p | rel32: 0x%08X", ConsoleArgFinder, ConsoleArgRel32);
		pirlog.FormattedMessage("FirstConsoleFinder   : %p | rel32: 0x%08X", FirstConsoleFinder, FirstConsoleRel32);
		pirlog.FormattedMessage("FirstObScriptFinder  : %p | rel32: 0x%08X", FirstObScriptFinder, FirstObScriptRel32);
		pirlog.FormattedMessage("GConsoleFinder       : %p | rel32: 0x%08X | %p", GConsoleFinder, GConsoleRel32, GConsoleStatic);
		pirlog.FormattedMessage("GetScaleFinder       : %p | rel32: 0x%08X", GetScaleFinder, GetScaleRel32);
		pirlog.FormattedMessage("SetScaleFinder       : %p | rel32: 0x%08X", SetScaleFinder, SetScaleRel32);
		pirlog.FormattedMessage("CurrentRefFinder     : %p | rel32: 0x%08X | %p", CurrentRefFinder, CurrentRefBaseRel32, CurrentRefBase);
		pirlog.FormattedMessage("ConsoleRefCallFinder : %p", ConsoleRefCallFinder);
		pirlog.FormattedMessage("ConsoleRefFuncFinder : %p", ConsoleRefFuncFinder);
		pirlog.FormattedMessage("ConsoleRefFuncAddress: %p | rel32: 0x%08X", ConsoleRefFuncAddress, static_cast<uintptr_t>(ConsoleRefFuncRel32));
		pirlog.FormattedMessage("GDataHandlerFinder   : %p | rel32: 0x%08X | %p", GDataHandlerFinder, GDataHandlerRel32, GDataHandlerStatic);
		pirlog.FormattedMessage("WorkshopModeFinder   : %p | rel32: 0x%08X | %p", WorkshopModeFinder, WorkshopModeFinderRel32, WorkshopModeBoolAddress);
		pirlog.FormattedMessage("CHANGE_A             : %p", CHANGE_A);
		pirlog.FormattedMessage("CHANGE_B             : %p", CHANGE_B);
		pirlog.FormattedMessage("CHANGE_C             : %p | original bytes: %02X%02X%02X%02X%02X%02X%02X", CHANGE_C, CHANGE_C_OLDCODE[0], CHANGE_C_OLDCODE[1], CHANGE_C_OLDCODE[2], CHANGE_C_OLDCODE[3], CHANGE_C_OLDCODE[4], CHANGE_C_OLDCODE[5], CHANGE_C_OLDCODE[6]);
		pirlog.FormattedMessage("CHANGE_D             : %p | original bytes: %02X%02X%02X%02X%02X%02X%02X", CHANGE_D, CHANGE_D_OLDCODE[0], CHANGE_D_OLDCODE[1], CHANGE_D_OLDCODE[2], CHANGE_D_OLDCODE[3], CHANGE_D_OLDCODE[4], CHANGE_D_OLDCODE[5], CHANGE_D_OLDCODE[6]);
		pirlog.FormattedMessage("CHANGE_E             : %p", CHANGE_E);
		pirlog.FormattedMessage("CHANGE_F             : %p", CHANGE_F);
		pirlog.FormattedMessage("CHANGE_G             : %p", CHANGE_G);
		pirlog.FormattedMessage("CHANGE_H             : %p", CHANGE_H);
		pirlog.FormattedMessage("CHANGE_I             : %p", CHANGE_I);
		pirlog.FormattedMessage("RED                  : %p", RED);
		pirlog.FormattedMessage("YELLOW               : %p", YELLOW);
		pirlog.FormattedMessage("WSTIMER2             : %p | original bytes: %02X%02X%02X%02X%02X%02X%02X%02X", WSTIMER2, WSTIMER2_OLDCODE[0], WSTIMER2_OLDCODE[1], WSTIMER2_OLDCODE[2], WSTIMER2_OLDCODE[3], WSTIMER2_OLDCODE[4], WSTIMER2_OLDCODE[5], WSTIMER2_OLDCODE[6], WSTIMER2_OLDCODE[7]);
		pirlog.FormattedMessage("GROUNDSNAP           : %p", GROUNDSNAP);
		pirlog.FormattedMessage("OBJECTSNAP           : %p | original bytes: %02X%02X%02X%02X%02X%02X%02X%02X", OBJECTSNAP, OBJECTSNAP_OLDCODE[0], OBJECTSNAP_OLDCODE[1], OBJECTSNAP_OLDCODE[2], OBJECTSNAP_OLDCODE[3], OBJECTSNAP_OLDCODE[4], OBJECTSNAP_OLDCODE[5], OBJECTSNAP_OLDCODE[6], OBJECTSNAP_OLDCODE[7]);
		pirlog.FormattedMessage("OUTLINES             : %p", OUTLINES);
		pirlog.FormattedMessage("ZOOM                 : %p", ZOOM);
		pirlog.FormattedMessage("ZOOM_FLOAT           : %p", uintptr_t(ZOOM) + (static_cast<uintptr_t>(ZOOM_REL32) + 8));
		pirlog.FormattedMessage("ROTATE               : %p", ROTATE);
		pirlog.FormattedMessage("ROTATE_FLOAT         : %p", uintptr_t(ROTATE) + (static_cast<uintptr_t>(ROTATE_REL32) + 8));
		pirlog.FormattedMessage("ACHIEVEMENTS         : %p", ACHIEVEMENTS);
		pirlog.FormattedMessage("WS_DRAWS_OLDCODE     : %p | original bytes: %02X%02X%02X%02X%02X%02X", WSSIZE, WORKSHOPSIZE_DRAWS_OLDCODE[0], WORKSHOPSIZE_DRAWS_OLDCODE[1], WORKSHOPSIZE_DRAWS_OLDCODE[2], WORKSHOPSIZE_DRAWS_OLDCODE[3], WORKSHOPSIZE_DRAWS_OLDCODE[4], WORKSHOPSIZE_DRAWS_OLDCODE[5]);
		pirlog.FormattedMessage("WS_TRIANGLES_OLDCODE : %p | original bytes: %02X%02X%02X%02X%02X%02X", WSSIZE + 0x0A, WORKSHOPSIZE_TRIANGLES_OLDCODE[0], WORKSHOPSIZE_TRIANGLES_OLDCODE[1], WORKSHOPSIZE_TRIANGLES_OLDCODE[2], WORKSHOPSIZE_TRIANGLES_OLDCODE[3], WORKSHOPSIZE_TRIANGLES_OLDCODE[4], WORKSHOPSIZE_TRIANGLES_OLDCODE[5]);
		pirlog.FormattedMessage("WSSIZE               : %p", uintptr_t(WSSIZE) + (static_cast<uintptr_t>(WSSIZE_REL32) + 6));
	}

	// first plugin function to run. search for patterns, save memory, relocate, etc.
	static void Init()
	{
		PIR_LOG_PREP
		// search for all the memory patterns
		ConsoleArgFinder = Utility::pattern("4C 89 4C 24 20 48 89 4C 24 08 53 55 56 57 41 54 41 55 41 56 41 57").count(1).get(0).get<uintptr_t>();
		FirstConsoleFinder = Utility::pattern("48 8D 1D ? ? ? ? 48 8D 35 ? ? ? ? 66 90 48 8B 53 F8").count(1).get(0).get<uintptr_t>();
		FirstObScriptFinder = Utility::pattern("48 8D 1D ? ? ? ? 4C 8D 35 ? ? ? ? 0F 1F 40 00 0F 1F 84 00 00 00 00 00").count(1).get(0).get<uintptr_t>();
		GConsoleFinder = Utility::pattern("48 8D 05 ? ? ? ? 48 89 2D ? ? ? ? 48 89 05 ? ? ? ? 89 2D ? ? ? ? 40 88 2D ? ? ? ? 48").count(1).get(0).get<uintptr_t>(); // for console print
		SetScaleFinder = Utility::pattern("E8 ? ? ? ? 40 84 F6 75 07 81 63 10 FF FF DF FF 33 ED").count(1).get(0).get<uintptr_t>();
		GetScaleFinder = Utility::pattern("66 89 BB 08 01 00 00 E8 ? ? ? ? 48 8B 0D ? ? ? ? 0F 28 F0 48").count(1).get(0).get<uintptr_t>();
		CurrentRefFinder = Utility::pattern("48 8B 1D ? ? ? ? 4C 8D 24 C3 49 3B DC 0F 84 ? ? ? ? 66").count(1).get(0).get<uintptr_t>();
		WorkshopModeFinder = Utility::pattern("80 3D ? ? ? ? 00 74 0E C6 07 02 48 8B 5C 24 30 48 83 C4 20 5F C3").count(1).get(0).get<uintptr_t>(); //is player in ws mode
		CHANGE_A = Utility::pattern("C6 05 ? ? ? ? 01 84 C0 75 A9 B1 02").count(1).get(0).get<uintptr_t>();
		CHANGE_B = Utility::pattern("B2 01 88 15 ? ? ? ? EB 04 84 D2 74 07").count(1).get(0).get<uintptr_t>();
		CHANGE_C = Utility::pattern("0F B6 05 ? ? ? ? 44 0F B6 A5 ? ? ? ? 84 C0 75").count(1).get(0).get<uintptr_t>();
		CHANGE_D = Utility::pattern("0F B6 05 ? ? ? ? 3A 05 ? ? ? ? 48 8B B4 24 ? ? ? ? C6 05").count(1).get(0).get<uintptr_t>();
		CHANGE_E = Utility::pattern("76 0C C6 05 ? ? ? ? 06 E9 ? ? ? ? 40 84 ED 0F 84").count(1).get(0).get<uintptr_t>();
		CHANGE_F = Utility::pattern("88 05 ? ? ? ? E8 ? ? ? ? 84 C0 75 ? 80 3D ? ? ? ? 02").count(1).get(0).get<uintptr_t>();
		CHANGE_G = Utility::pattern("0F 95 05 ? ? ? ? E8 ? ? ? ? 40 38 35 ? ? ? ? 48").count(1).get(0).get<uintptr_t>();
		CHANGE_H = Utility::pattern("74 11 40 84 F6 74 0C 48 8D ? ? 49 8B ? E8").count(1).get(0).get<uintptr_t>();
		CHANGE_I = Utility::pattern("74 35 48 8B B5 ? ? ? ? 48 8B CE E8 ? ? ? ? 84 C0 75").count(1).get(0).get<uintptr_t>(); // ignore water restrictions
		YELLOW = Utility::pattern("8B 58 14 48 8D 4C 24 30 8B D3 45 33 C0 E8").count(1).get(0).get<uintptr_t>(); // allow moving yellow objects
		RED = Utility::pattern("89 05 ? ? ? ? C6 05 ? ? ? ? 01 48 83 C4 68 C3").count(1).get(0).get<uintptr_t>(); //keep objects green
		WSTIMER2 = Utility::pattern("F3 0F 11 05 ? ? ? ? 77 65 48 8B 0D ? ? ? ? 41 B1 01 45 0F B6 C1 33 D2 E8").count(1).get(0).get<uintptr_t>(); // New ws timer check is buried in here
		OBJECTSNAP = Utility::pattern("F3 0F 10 35 ? ? ? ? 0F 28 C6 48 ? ? ? ? ? ? 33 C0").count(1).get(0).get<uintptr_t>();
		GROUNDSNAP = Utility::pattern("0F 86 ? ? ? ? 41 8B 4E 34 49 B8").count(1).get(0).get<uintptr_t>();
		WSSIZE = Utility::pattern("01 05 ? ? ? ? 8B 44 24 58 01 05 ? ? ? ? 85 D2 0F 84").count(1).get(0).get<uintptr_t>(); // where the game adds to the ws size
		OUTLINES = Utility::pattern("C6 05 ? ? ? ? 01 88 15 ? ? ? ? 76 13 48 8B 05").count(1).get(0).get<uintptr_t>(); // object outlines not instant
		ZOOM = Utility::pattern("F3 0F 10 0D ? ? ? ? 0F 29 74 24 20 F3 0F 10 35").count(1).get(0).get<uintptr_t>();
		ROTATE = Utility::pattern("F3 0F 10 05 ? ? ? ? ? ? ? ? ? ? ? ? 84 C9 75 07 0F 57 05").count(1).get(0).get<uintptr_t>(); //better compatibility with high physics fps
		ACHIEVEMENTS = Utility::pattern("48 83 EC 28 C6 44 24 ? 00 84 D2 74 1C 48").count(1).get(0).get<uintptr_t>();
		ConsoleRefFuncFinder = Utility::pattern("E8 ? ? ? ? 4C 8B 05 ? ? ? ? 48 8D 4C 24 40 4C 8B C8 BA 00 01 00 00 E8 ? ? ? ? 83").count(1).get(0).get<uintptr_t>(); //consolenamefix credit registrator2000
		ConsoleRefCallFinder = Utility::pattern("FF 90 D0 01 00 00 48 89 74 24 40 4C 8D 05 ? ? ? ? 4C").count(1).get(0).get<uintptr_t>(); //consolenamefix credit registrator2000
		GDataHandlerFinder = Utility::pattern("48 83 3D ? ? ? ? 00 4D 8B F1 41 0F B6 F0 48 8B FA 48 8B D9 0F 84 ? ? ? ? 80 3D ? ? ? ? 00 48").count(1).get(0).get<uintptr_t>();
	
		// storing the old bytes and setting pointers
		if (CHANGE_C) { ReadMemory((uintptr_t(CHANGE_C)), &CHANGE_C_OLDCODE, 0x07); }
		if (CHANGE_D) { ReadMemory((uintptr_t(CHANGE_D)), &CHANGE_D_OLDCODE, 0x07); }
		if (CHANGE_F) { ReadMemory((uintptr_t(CHANGE_F)), &CHANGE_F_OLDCODE, 0x06); }
		if (OBJECTSNAP) { ReadMemory((uintptr_t(OBJECTSNAP)), &OBJECTSNAP_OLDCODE, 0x08); }
		if (WSTIMER2) { ReadMemory(uintptr_t(WSTIMER2), &WSTIMER2_OLDCODE, 0x08); }
		if (ZOOM) { ReadMemory((uintptr_t(ZOOM) + 0x04), &ZOOM_REL32, sizeof(SInt32)); }
		if (ROTATE) { ReadMemory((uintptr_t(ROTATE) + 0x04), &ROTATE_REL32, sizeof(SInt32)); }

		if (WSSIZE) {
			ReadMemory((uintptr_t(WSSIZE)), &WORKSHOPSIZE_DRAWS_OLDCODE, 0x06);
			ReadMemory((uintptr_t(WSSIZE) + 0x0A), &WORKSHOPSIZE_TRIANGLES_OLDCODE, 0x06);
			ReadMemory((uintptr_t(WSSIZE) + 0x02), &WSSIZE_REL32, sizeof(SInt32));
		}

		//console name fix credit registrator2000
		if (ConsoleRefCallFinder && ConsoleRefFuncFinder) {
			ConsoleRefFuncRel32 = GetRel32FromPattern(ConsoleRefFuncFinder, 0x01, 0x05, 0x00); //rel32 of the good function
			ConsoleRefFuncAddress = RelocationManager::s_baseAddr + (uintptr_t)ConsoleRefFuncRel32; // calculate the full address
			SafeWriteCall(uintptr_t(ConsoleRefCallFinder), ConsoleRefFuncAddress); // now we can patch the call to use the good function
			SafeWrite8(uintptr_t(ConsoleRefCallFinder) + 0x05, 0x90); // x1 nop for a clean patch
		}


		if (WorkshopModeFinder) {
			WorkshopModeFinderRel32 = GetRel32FromPattern(WorkshopModeFinder, 0x02, 0x07, 0x00);
			WorkshopModeBoolAddress = RelocationManager::s_baseAddr + static_cast<uintptr_t>(WorkshopModeFinderRel32);
		}

		if (SetScaleFinder) {
			SetScaleRel32 = GetRel32FromPattern(SetScaleFinder, 0x01, 0x05, 0x00);
			RelocAddr <_SetScale> GimmeSetScale(SetScaleRel32);
			SetScale = GimmeSetScale;
		}

		if (GetScaleFinder) {
			GetScaleRel32 = GetRel32FromPattern(GetScaleFinder, 0x08, 0x0C, 0x00);
			RelocAddr <_GetScale> GimmeGetScale(GetScaleRel32);
			GetScale = GimmeGetScale;
		}

		if (GConsoleFinder) {
			GConsoleRel32 = GetRel32FromPattern(GConsoleFinder, 0x03, 0x07, 0x00);
			GConsoleStatic = RelocationManager::s_baseAddr + static_cast<uintptr_t>(GConsoleRel32);
		}

		if (GDataHandlerFinder) {
			GDataHandlerRel32 = GetRel32FromPattern(GDataHandlerFinder, 0x03, 0x08, 0x00);
			GDataHandlerStatic = RelocationManager::s_baseAddr + static_cast<uintptr_t>(GDataHandlerRel32);
		}

		if (CurrentRefFinder) {
			CurrentRefBaseRel32 = GetRel32FromPattern(CurrentRefFinder, 0x03, 0x07, 0x00);
			CurrentRefBase = RelocationManager::s_baseAddr + static_cast<uintptr_t>(CurrentRefBaseRel32);
		}

		if (ConsoleArgFinder) {
			ConsoleArgRel32 = uintptr_t(ConsoleArgFinder) - RelocationManager::s_baseAddr;
			RelocAddr <_GetConsoleArg> GetDatArg(ConsoleArgRel32);
			GetConsoleArg = GetDatArg;
		}

		if (FirstConsoleFinder) {
			FirstConsoleRel32 = GetRel32FromPattern(FirstConsoleFinder, 0x03, 0x07, -0x08);
			RelocPtr <ObScriptCommand> _FirstConsoleCommand(FirstConsoleRel32);
			FirstConsole = _FirstConsoleCommand;
		}

		if (FirstObScriptFinder) {
			FirstObScriptRel32 = GetRel32FromPattern(FirstObScriptFinder, 0x03, 0x07, -0x08);
			RelocPtr <ObScriptCommand> _FirstObScriptCommand(FirstObScriptRel32);
			FirstObScript = _FirstObScriptCommand;
		}

	}

}




__declspec(dllexport) bool F4SEPlugin_Load(const F4SEInterface* f4seinterface)
{
	PIR_LOG_PREP
	
	// start log
	pirlog.OpenRelative(CSIDL_MYDOCUMENTS, pluginLogFile);
	pirlog.FormattedMessage("[%s] Plugin loaded!", thisfunc);

	// get a plugin handle
	pirPluginHandle = f4seinterface->GetPluginHandle();
	if (!pirPluginHandle) {
		pirlog.FormattedMessage("[%s] Plugin load failed! Couldn't get a plugin handle!", thisfunc);
		return false; 
	}

	pirlog.FormattedMessage("[%s] Got a plugin handle!", thisfunc);
	pirlog.FormattedMessage("[%s] Locating memory patterns...", thisfunc);

	// init and log memory patterns
	pir::Init();
	pir::LogMemoryPatterns();

	// check memory patterns
	if (!pir::FoundRequiredMemoryPatterns())
	{
		pirlog.FormattedMessage("[%s] Plugin load failed! Couldnt find required memory patterns. Check for conflicting mods.", thisfunc);
		return false;
	}

	// papyrus interface
	g_papyrus = (F4SEPapyrusInterface*)f4seinterface->QueryInterface(kInterface_Papyrus);
	if (!g_papyrus) {
		pirlog.FormattedMessage("[%s] Plugin load failed! Failed to set papyrus interface.", thisfunc);
		return false;
	}
	
	// messaging interface
	g_messaging = (F4SEMessagingInterface*)f4seinterface->QueryInterface(kInterface_Messaging);
	if (!g_messaging) {
		pirlog.FormattedMessage("[%s] Plugin load failed! Failed to set messaging interface.", thisfunc);
		return false;
	}

	// object interface
	g_object = (F4SEObjectInterface*)f4seinterface->QueryInterface(kInterface_Object);
	if (!g_object) {
		pirlog.FormattedMessage("[%s] Plugin load failed! Failed to set object interface.", thisfunc);
		return false;
	}
		
	// register papyrus functions
	pirlog.FormattedMessage("[%s] Registering papyrus functions...", thisfunc);
	g_papyrus->Register(papyrusPIR::RegisterFuncs);

	// register message listener handler
	pirlog.FormattedMessage("[%s] Registering message listeners...", thisfunc);
	g_messaging->RegisterListener(pirPluginHandle, "F4SE", pir::MessageInterfaceHandler);

	// attempt to create the console command
	pirlog.FormattedMessage("[%s] Creating console command...", thisfunc);
	if (!pir::CreateConsoleCommand()){
		pirlog.FormattedMessage("[%s] Failed to create console command! Plugin will run with hard coded default options.", thisfunc);
	}

	// toggle defaults
	pirlog.FormattedMessage("[%s] Toggling on defaults...", thisfunc);
	pir::Toggle_PlaceInRed();
	pir::Toggle_Achievements();

	// plugin loaded
	pirlog.FormattedMessage("[%s] Plugin load finished!", thisfunc);
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

__declspec(dllexport) bool F4SEPlugin_Query(const F4SEInterface* f4seinterface, PluginInfo* plugininfo) {
	// this used anymore?
	plugininfo->infoVersion = PluginInfo::kInfoVersion;
	plugininfo->name = pluginName;
	plugininfo->version = pluginVersion;
	return true;
}


}



