#include "main.h"

// SetScale
static _SetScale SetScale = nullptr;
static uintptr_t* SetScaleFinder = nullptr;
static SInt32 SetScaleRel32 = 0;

// GetScale
static _GetScale GetScale = nullptr;
static uintptr_t* GetScaleFinder = nullptr;
static SInt32 GetScaleRel32 = 0;

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

static uintptr_t bsfadenode_offsets[] = { 0x0, 0x0, 0x10 }; //bsfadenode
static size_t bsfadenode_count = sizeof(bsfadenode_offsets) / sizeof(bsfadenode_offsets[0]);
static uintptr_t ref_offsets[] = { 0x0, 0x0, 0x10, 0x110 }; //TESObjectREFR
static size_t ref_count = sizeof(ref_offsets) / sizeof(ref_offsets[0]);
static uintptr_t bhknicoll_offsets[] = { 0x0, 0x0, 0x10, 0x100 }; //bhkNiCollisionObject
static size_t bhknicoll_count = sizeof(bhknicoll_offsets) / sizeof(bhknicoll_offsets[0]);

// workshop mode finder
static uintptr_t* WorkshopModeFinder = nullptr;
static SInt32 WorkshopModeFinderRel32 = 0;
static uintptr_t WorkshopModeBoolAddress;

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
static uintptr_t* WSTIMER = nullptr;
static uintptr_t* WSTIMER2 = nullptr;
static uintptr_t* GROUNDSNAP = nullptr;
static uintptr_t* OBJECTSNAP = nullptr;
static uintptr_t* WORKSHOPSIZE = nullptr;
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
static SInt32 WORKSHOPSIZE_REL32 = 0;
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


// copied from f4se and modified for use with a pattern
static void PIR_ConsolePrint(const char* fmt, ...)
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

// handles f4se messages from message interface
static void MessageInterfaceHandler(F4SEMessagingInterface::Message* msg) {
	switch (msg->type) {
		case F4SEMessagingInterface::kMessage_PostLoad: pluginLog.FormattedMessage("[MessageInterfaceHandler] kMessage_PostLoad"); break;
		case F4SEMessagingInterface::kMessage_PostPostLoad: pluginLog.FormattedMessage("[MessageInterfaceHandler] kMessage_PostPostLoad"); break;
		case F4SEMessagingInterface::kMessage_PreLoadGame: pluginLog.FormattedMessage("[MessageInterfaceHandler] kMessage_PreLoadGame"); break;
		case F4SEMessagingInterface::kMessage_PostLoadGame: pluginLog.FormattedMessage("[MessageInterfaceHandler] kMessage_PostLoadGame"); break;
		case F4SEMessagingInterface::kMessage_PreSaveGame: pluginLog.FormattedMessage("[MessageInterfaceHandler] kMessage_PreSaveGame"); break;
		case F4SEMessagingInterface::kMessage_PostSaveGame: pluginLog.FormattedMessage("[MessageInterfaceHandler] kMessage_PostSaveGame"); break;
		case F4SEMessagingInterface::kMessage_DeleteGame: pluginLog.FormattedMessage("[MessageInterfaceHandler] kMessage_DeleteGame"); break;
		case F4SEMessagingInterface::kMessage_InputLoaded: pluginLog.FormattedMessage("[MessageInterfaceHandler] kMessage_InputLoaded"); break;
		case F4SEMessagingInterface::kMessage_NewGame: pluginLog.FormattedMessage("[MessageInterfaceHandler] kMessage_NewGame"); break;
		case F4SEMessagingInterface::kMessage_GameLoaded: pluginLog.FormattedMessage("[MessageInterfaceHandler] kMessage_GameLoaded"); break;
		case F4SEMessagingInterface::kMessage_GameDataReady: pluginLog.FormattedMessage("[MessageInterfaceHandler] kMessage_GameDataReady"); break;

		default: pluginLog.FormattedMessage("[MessageInterfaceHandler] kMessage_UNKNOWN type: %0X", msg->type); break;
	}
}

// Determine if player is in workshop mode
static bool PIR_InWorkshopMode() {
	if (WorkshopModeFinder) {
		UInt8 WSMODE = 0x00;
		ReadMemory(uintptr_t(WorkshopModeBoolAddress), &WSMODE, sizeof(bool));
		if (WSMODE == 0x01) {
			return true;
		}
	}
	return false;
}

// Set the scale of the current workshop reference
static bool PIR_SetCurrentRefScale(float newScale) {

	if (CurrentRefFinder && CurrentRefBase && SetScaleFinder && GetScaleFinder && PIR_InWorkshopMode()) {
		uintptr_t* refptr = GetMultiLevelPointer(CurrentRefBase, ref_offsets, ref_count);
		TESObjectREFR* ref = reinterpret_cast<TESObjectREFR*>(refptr);
		if (ref) {
			SetScale(ref, newScale);
			PIR_ConsolePrint("Scale modified.");
			return true;
		}
	}
	return false;
}

// Mod the scale of the current workshop reference by a percent
static bool PIR_ModCurrentRefScale(float fMultiplyAmount) {

	if (CurrentRefFinder && CurrentRefBase && SetScaleFinder && GetScaleFinder && PIR_InWorkshopMode()) {
		uintptr_t* refptr = GetMultiLevelPointer(CurrentRefBase, ref_offsets, ref_count);
		TESObjectREFR* ref = reinterpret_cast<TESObjectREFR*>(refptr);
		if (ref) {
			float oldscale = GetScale(ref);
			float newScale = oldscale * (fMultiplyAmount);
			if (newScale > 9.999999f) { newScale = 9.999999f; }
			if (newScale < 0.000001f) { newScale = 0.000001f; }
			SetScale(ref, newScale);
			PIR_ConsolePrint("Scale modified.");
			return true;
		}
	}
	return false;
}

// Dump all console and obscript commands to the log file
static bool PIR_DumpCmds2Log()
{

	if (FirstConsole == nullptr || FirstObScript == nullptr) {
		return false;
	}

	for (ObScriptCommand* iter = FirstConsole; iter->opcode < (kObScript_NumConsoleCommands + kObScript_ConsoleOpBase); ++iter) {
		if (iter) {

			uintptr_t rel32 = (uintptr_t)&(iter->execute) - RelocationManager::s_baseAddr;

			pluginLog.FormattedMessage("Console|%08X|Fallout4.exe+0x%08X|%s|%s|%X|%X|%s", iter->opcode, rel32, iter->shortName, iter->longName, iter->numParams, iter->needsParent, iter->helpText);
		}
	}

	for (ObScriptCommand* iter = FirstObScript; iter->opcode < (kObScript_NumObScriptCommands + kObScript_ScriptOpBase); ++iter) {
		if (iter) {

			uintptr_t rel32 = (uintptr_t) & (iter->execute) - RelocationManager::s_baseAddr;

			pluginLog.FormattedMessage("ObScript|%08X|Fallout4.exe+0x%08X|%s|%s|%X|%X|%s", iter->opcode, rel32, iter->shortName, iter->longName, iter->numParams, iter->needsParent, iter->helpText);
		}
	}

	return true;
}

static bool Toggle_Outlines()
{
	if (OUTLINES && OUTLINES_ENABLED) {
		SafeWrite8((uintptr_t)OUTLINES + 0x06, 0x00); //objects
		SafeWrite8((uintptr_t)OUTLINES + 0x0D, 0xEB); //npcs
		OUTLINES_ENABLED = false;
		PIR_ConsolePrint("Object outlines - disabled");
		return true;
	}
	if (OUTLINES && !OUTLINES_ENABLED) {
		SafeWrite8((uintptr_t)OUTLINES + 0x06, 0x01); //objects
		SafeWrite8((uintptr_t)OUTLINES + 0x0D, 0x76); //npcs
		OUTLINES_ENABLED = true;
		PIR_ConsolePrint("Object outlines - enabled");
		return true;
	}
	return false;
}

static bool Toggle_SlowZoomAndRotate()
{
	// its on, turn it off
	if (ZOOM && ROTATE && ZOOM_REL32 != 0 && ROTATE_REL32 !=0 && SLOW_ENABLED) {
		SafeWriteBuf(uintptr_t(ZOOM) + (static_cast<uintptr_t>(ZOOM_REL32) + 8), fZOOM_DEFAULT, sizeof(fZOOM_DEFAULT));
		SafeWriteBuf(uintptr_t(ROTATE) + (static_cast<uintptr_t>(ROTATE_REL32) + 8), fROTATE_DEFAULT, sizeof(fROTATE_DEFAULT));
		SLOW_ENABLED = false;
		PIR_ConsolePrint("Slow zoom and rotate - disabled");
		return true;
	}
	// its off, turn it on
	if (ZOOM && ROTATE && ZOOM_REL32 != 0 && ROTATE_REL32 != 0 && !SLOW_ENABLED) {
		SafeWriteBuf(uintptr_t(ZOOM) + (static_cast<uintptr_t>(ZOOM_REL32) + 8), fZOOM_SLOWED, sizeof(fZOOM_SLOWED));
		SafeWriteBuf(uintptr_t(ROTATE) + (static_cast<uintptr_t>(ROTATE_REL32) + 8), fROTATE_SLOWED, sizeof(fROTATE_SLOWED));
		SLOW_ENABLED = true;
		PIR_ConsolePrint("Slow zoom and rotate - enabled");
		return true;
	}
	return false;
}

static bool Toggle_WorkshopSize()
{
	if (WORKSHOPSIZE && WORKSHOPSIZE_ENABLED) {
		SafeWriteBuf((uintptr_t)WORKSHOPSIZE, WORKSHOPSIZE_DRAWS_OLDCODE, sizeof(WORKSHOPSIZE_DRAWS_OLDCODE));
		SafeWriteBuf((uintptr_t)WORKSHOPSIZE + 0x0A, WORKSHOPSIZE_TRIANGLES_OLDCODE, sizeof(WORKSHOPSIZE_TRIANGLES_OLDCODE));
		WORKSHOPSIZE_ENABLED = false;
		PIR_ConsolePrint("Unlimited workshop size - disabled");
		return true;
	}

	if (WORKSHOPSIZE && WORKSHOPSIZE_ENABLED == false) {
		
		SafeWriteBuf((uintptr_t)WORKSHOPSIZE, WORKSHOPSIZE_DRAWS_NEWCODE, sizeof(WORKSHOPSIZE_DRAWS_NEWCODE));
		SafeWriteBuf((uintptr_t)WORKSHOPSIZE + 0x0A, WORKSHOPSIZE_TRIANGLES_NEWCODE, sizeof(WORKSHOPSIZE_TRIANGLES_NEWCODE));

		// set draws and triangles to zero
		SafeWrite64(uintptr_t(WORKSHOPSIZE) + (static_cast<uintptr_t>(WORKSHOPSIZE_REL32) + 6), 0x0000000000000000);
		WORKSHOPSIZE_ENABLED = true;
		PIR_ConsolePrint("Unlimited workshop size - enabled");
		return true;
	}
	return false;
}

static bool Toggle_GroundSnap()
{
	if (GROUNDSNAP && GROUNDSNAP_ENABLED) {

		SafeWrite8((uintptr_t)GROUNDSNAP + 0x01, 0x85);
		GROUNDSNAP_ENABLED = false;
		PIR_ConsolePrint("Ground snapping - disabled");
		return true;
	}
	if (GROUNDSNAP && !GROUNDSNAP_ENABLED) {
		SafeWrite8((uintptr_t)GROUNDSNAP + 0x01, 0x86);
		GROUNDSNAP_ENABLED = true;
		PIR_ConsolePrint("Ground snapping - enabled");
		return true;

	}
	return false;
}

static bool Toggle_ObjectSnap()
{
	if (OBJECTSNAP && OBJECTSNAP_ENABLED) {
		SafeWrite64((uintptr_t)OBJECTSNAP, OBJECTSNAP_NEWCODE);
		OBJECTSNAP_ENABLED = false;
		PIR_ConsolePrint("Object snapping - disabled");
		return true;
	}
	if (OBJECTSNAP && !OBJECTSNAP_ENABLED) {
		SafeWriteBuf((uintptr_t)OBJECTSNAP, OBJECTSNAP_OLDCODE, sizeof(OBJECTSNAP_OLDCODE));
		OBJECTSNAP_ENABLED = true;
		PIR_ConsolePrint("Object snapping - enabled");
		return true;
	}
	return false;
}

static bool Toggle_Achievements()
{
	// its on - toggle it off
	if (ACHIEVEMENTS && ACHIEVEMENTS_ENABLED) {
		SafeWriteBuf((uintptr_t)ACHIEVEMENTS, ACHIEVEMENTS_OLDCODE, sizeof(ACHIEVEMENTS_OLDCODE));
		ACHIEVEMENTS_ENABLED = false;
		PIR_ConsolePrint("Allow achievements with mods - disabled");
		return true;
	}
	// its off - toggle it on
	if (ACHIEVEMENTS && !ACHIEVEMENTS_ENABLED) {
		SafeWriteBuf((uintptr_t)ACHIEVEMENTS, ACHIEVEMENTS_NEWCODE, sizeof(ACHIEVEMENTS_NEWCODE));
		ACHIEVEMENTS_ENABLED = true;
		PIR_ConsolePrint("Allow achievements with mods - enabled");
		return true;
	}
	return false;
}

static bool Toggle_PlaceInRed()
{
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
		PIR_ConsolePrint("Place In Red disabled.");
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
		PIR_ConsolePrint("Place In Red enabled.");
		return true;
	}

	return false;
}

static bool pirdebug(int option = 0) {
	

	//1 function to debug stuff. option relates to:
	// pir d1 d2 d3 d4 d5 d6
	//dump commands to log file
	if (option == 0) {
		pluginLog.FormattedMessage("[pirdebug] hello world!");
		PIR_ConsolePrint("[pirdebug] hello world!");
		return true;
	}

	//test refs
	if (option == 1) {
		if (CurrentRefFinder && CurrentRefBase) {
			uintptr_t* refptr = GetMultiLevelPointer(CurrentRefBase, ref_offsets, ref_count);
			uintptr_t* test = GetMultiLevelPointer(CurrentRefBase, bsfadenode_offsets, bsfadenode_count);
			TESObjectREFR* ref = reinterpret_cast<TESObjectREFR*>(refptr);
			BSHandleRefObject* testref = reinterpret_cast<BSHandleRefObject*>(test);

			if (ref) {
				TESObjectCELL* cell = ref->parentCell;
				TESWorldSpace* worldspace = ref->parentCell->worldSpace;
				bhkWorld* havokworld = CALL_MEMBER_FN(cell, GetHavokWorld)();
				UInt8 formtype = ref->GetFormType();
				const char* edid = ref->GetEditorID();
				const char* fullname = ref->GetFullName();
				UInt32 formid = ref->formID;
				UInt32 flags = ref->flags;
				UInt32 cellfullname = ref->parentCell->formID;
				UInt64 rootnodeflags = ref->GetObjectRootNode()->flags;
				UInt64 rootnodechildre = ref->GetObjectRootNode()->m_children.m_size;

				if (testref->m_uiRefCount) { pluginLog.FormattedMessage("test ref m ui ref count %i", testref->m_uiRefCount); }

				float Px = ref->pos.x;
				float Py = ref->pos.y;
				float Pz = ref->pos.z;
				float Rx = ref->rot.z;
				float Ry = ref->rot.z;
				float Rz = ref->rot.z;
				
				pluginLog.FormattedMessage("FormType:%X\nEditorID:%s\nFullName%s\nformID:%08X\nflags:%08X\ncellFormID:%08X\nRoot Node Flags:%016X\nrootnodechildrensize:%i\nPx:%f Py:%f Pz:%f Rx:%f Ry:%f Rz:%f", formtype, edid, fullname, formid, flags, cellfullname, Px, Py, Pz, Rx, Ry, Rz);
				

				return true;
				
			}
		}
	}

	//return true just in case i forgot
	return true;
}

static bool PIR_ExecuteConsoleCommand(void* paramInfo, void* scriptData, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, double* result, void* opcodeOffsetPtr)
{
	if (GetConsoleArg && ConsoleArgFinder && (ConsoleArgRel32 != 0)) {

		char consolearg[4096];
		bool consoleresult = GetConsoleArg(paramInfo, scriptData, opcodeOffsetPtr, thisObj, containingObj, scriptObj, locals, &consolearg);

		if (consoleresult && consolearg[0]) {
			switch (PIR_Switch(consolearg)) {
				// debug and tests
				case PIR_Switch("dump"): PIR_DumpCmds2Log(); break;
				case PIR_Switch("d0"): pirdebug(0); break;
				case PIR_Switch("d1"): pirdebug(1); break;
				case PIR_Switch("d2"): pirdebug(2); break;
				case PIR_Switch("d3"): pirdebug(3); break;
				case PIR_Switch("d4"): pirdebug(4); break;
				case PIR_Switch("d5"): pirdebug(5); break;
				case PIR_Switch("d6"): pirdebug(6); break;
				case PIR_Switch("d7"): pirdebug(7); break;
				case PIR_Switch("d8"): pirdebug(8); break;
				case PIR_Switch("d9"): pirdebug(9); break;

				case PIR_Switch("1"): Toggle_PlaceInRed(); break;
				case PIR_Switch("toggle"): Toggle_PlaceInRed(); break;

				case PIR_Switch("2"): Toggle_ObjectSnap(); break;
				case PIR_Switch("osnap"): Toggle_ObjectSnap(); break;

				case PIR_Switch("3"): Toggle_GroundSnap(); break;
				case PIR_Switch("gsnap"): Toggle_GroundSnap(); break;

				case PIR_Switch("4"): Toggle_SlowZoomAndRotate(); break;
				case PIR_Switch("slow"): Toggle_SlowZoomAndRotate(); break;

				case PIR_Switch("5"): Toggle_WorkshopSize(); break;
				case PIR_Switch("workshopsize"): Toggle_WorkshopSize(); break;

				case PIR_Switch("6"): Toggle_Outlines(); break;
				case PIR_Switch("outlines"): Toggle_Outlines(); break;

				case PIR_Switch("7"): Toggle_Achievements(); break;
				case PIR_Switch("achievements"): Toggle_Achievements(); break;

				case PIR_Switch("scaleup1"): PIR_ModCurrentRefScale(1.01f); break;
				case PIR_Switch("scaleup5"): PIR_ModCurrentRefScale(1.05f); break;
				case PIR_Switch("scaleup10"): PIR_ModCurrentRefScale(1.10f); break;
				case PIR_Switch("scaleup25"): PIR_ModCurrentRefScale(1.25f); break;
				case PIR_Switch("scaleup50"): PIR_ModCurrentRefScale(1.5f); break;
				case PIR_Switch("scaleup100"): PIR_ModCurrentRefScale(2.0f); break;
				case PIR_Switch("scaledown1"): PIR_ModCurrentRefScale(0.99f); break;
				case PIR_Switch("scaledown5"): PIR_ModCurrentRefScale(0.95f); break;
				case PIR_Switch("scaledown10"): PIR_ModCurrentRefScale(0.90f); break;
				case PIR_Switch("scaledown25"): PIR_ModCurrentRefScale(0.75f); break;
				case PIR_Switch("scaledown50"): PIR_ModCurrentRefScale(0.50f); break;
				case PIR_Switch("scaledown75"): PIR_ModCurrentRefScale(0.25f); break;

				default: PIR_ConsolePrint("PlaceInRed (pir) usage:\n pir toggle (pir 1) toggle place in red\n pir osnap (pir 2) toggle object snapping\n pir gsnap (pir 3) toggle ground snapping\n pir slow (pir 4) slow object rotation and zoom speed\n pir workshopsize (pir 5) unlimited workshop build size\n pir outlines (pir 6) toggle object outlines\n pir achievements (pir 7) toggle achievement feature\n pir scaleup1   (also: 1, 5, 10, 25, 50, 100) scale up percent\n pir scaledown1   (also: 1, 5, 10, 25, 50, 75) scale down percent\n");  break;
			}
			return true;
		} else {
			pluginLog.FormattedMessage("[PIR_ExecuteConsoleCommand] Failed to execute the console command. Try restarting the game or check for conflicting mods.");
			return false;
		}
	} else {
		pluginLog.FormattedMessage("[PIR_ExecuteConsoleCommand] Failed to execute the console command. Try restarting the game or check for conflicting mods.");
		return false;
	}
}

static bool PIR_CreateConsoleCommand()
{
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
		cmd.execute = PIR_ExecuteConsoleCommand;
		cmd.params = s_hijackedCommandParams;
		cmd.flags = 0;
		cmd.eval;

		SafeWriteBuf((uintptr_t)s_hijackedCommand, &cmd, sizeof(cmd));
		return true;

	}
	return false;
}

static bool PIR_FoundRequiredMemoryPatterns()
{
	if (ConsoleArgFinder && FirstConsoleFinder && FirstObScriptFinder && SetScaleFinder && GetScaleFinder && CurrentRefFinder
		&& WorkshopModeFinder && GConsoleFinder && CHANGE_A && CHANGE_B && CHANGE_C && CHANGE_D && CHANGE_E && CHANGE_F && CHANGE_G && CHANGE_H && CHANGE_I
		&& YELLOW && RED && WSTIMER2 && GROUNDSNAP && OBJECTSNAP && OUTLINES && WORKSHOPSIZE && ZOOM && ROTATE	)
	{
		return true;
	} else {
		return false;
	}
}

static void PIR_LogMemoryPatterns()
{	
	pluginLog.FormattedMessage("------------------------------------------------------------------------------");
	pluginLog.FormattedMessage("Base: %p", RelocationManager::s_baseAddr);
	pluginLog.FormattedMessage("ConsoleArgFinder: %p | rel32: 0x%08X", ConsoleArgFinder, ConsoleArgRel32);
	pluginLog.FormattedMessage("FirstConsoleFinder: %p | rel32: 0x%08X", FirstConsoleFinder, FirstConsoleRel32);
	pluginLog.FormattedMessage("FirstObScriptFinder: %p | rel32: 0x%08X", FirstObScriptFinder, FirstObScriptRel32);
	pluginLog.FormattedMessage("GConsoleFinder: %p | rel32: 0x%08X | %p", GConsoleFinder, GConsoleRel32, GConsoleStatic);
	pluginLog.FormattedMessage("SetScaleFinder: %p | rel32: 0x%08X", SetScaleFinder, SetScaleRel32);
	pluginLog.FormattedMessage("GetScaleFinder: %p | rel32: 0x%08X", GetScaleFinder, GetScaleRel32);
	pluginLog.FormattedMessage("CurrentRefFinder: %p | rel32: 0x%08X | %p", CurrentRefFinder, CurrentRefBaseRel32, CurrentRefBase);
	pluginLog.FormattedMessage("WorkshopModeFinder: %p | rel32: 0x%08X | %p", WorkshopModeFinder, WorkshopModeFinderRel32, WorkshopModeBoolAddress);
	pluginLog.FormattedMessage("CHANGE_A: %p", CHANGE_A);
	pluginLog.FormattedMessage("CHANGE_B: %p", CHANGE_B);
	pluginLog.FormattedMessage("CHANGE_C: %p | original bytes: %02X%02X%02X%02X%02X%02X%02X", CHANGE_C, CHANGE_C_OLDCODE[0], CHANGE_C_OLDCODE[1], CHANGE_C_OLDCODE[2], CHANGE_C_OLDCODE[3], CHANGE_C_OLDCODE[4], CHANGE_C_OLDCODE[5], CHANGE_C_OLDCODE[6]);
	pluginLog.FormattedMessage("CHANGE_D: %p | original bytes: %02X%02X%02X%02X%02X%02X%02X", CHANGE_D, CHANGE_D_OLDCODE[0], CHANGE_D_OLDCODE[1], CHANGE_D_OLDCODE[2], CHANGE_D_OLDCODE[3], CHANGE_D_OLDCODE[4], CHANGE_D_OLDCODE[5], CHANGE_D_OLDCODE[6]);
	pluginLog.FormattedMessage("CHANGE_E: %p", CHANGE_E);
	pluginLog.FormattedMessage("CHANGE_F: %p", CHANGE_F);
	pluginLog.FormattedMessage("CHANGE_G: %p", CHANGE_G);
	pluginLog.FormattedMessage("CHANGE_H: %p", CHANGE_H);
	pluginLog.FormattedMessage("CHANGE_I: %p", CHANGE_I);
	pluginLog.FormattedMessage("RED: %p", RED);
	pluginLog.FormattedMessage("YELLOW: %p", YELLOW);
	pluginLog.FormattedMessage("WSTIMER2: %p | original bytes: %02X%02X%02X%02X%02X%02X%02X%02X", WSTIMER2, WSTIMER2_OLDCODE[0], WSTIMER2_OLDCODE[1], WSTIMER2_OLDCODE[2], WSTIMER2_OLDCODE[3], WSTIMER2_OLDCODE[4], WSTIMER2_OLDCODE[5], WSTIMER2_OLDCODE[6], WSTIMER2_OLDCODE[7]);
	pluginLog.FormattedMessage("GROUNDSNAP: %p", GROUNDSNAP);
	pluginLog.FormattedMessage("OBJECTSNAP: %p | original bytes: %02X%02X%02X%02X%02X%02X%02X%02X", OBJECTSNAP, OBJECTSNAP_OLDCODE[0], OBJECTSNAP_OLDCODE[1], OBJECTSNAP_OLDCODE[2], OBJECTSNAP_OLDCODE[3], OBJECTSNAP_OLDCODE[4], OBJECTSNAP_OLDCODE[5], OBJECTSNAP_OLDCODE[6], OBJECTSNAP_OLDCODE[7]);
	pluginLog.FormattedMessage("OUTLINES: %p", OUTLINES);
	pluginLog.FormattedMessage("ZOOM: %p", ZOOM);
	pluginLog.FormattedMessage("ZOOM_FLOAT: %p", uintptr_t(ZOOM) + (static_cast<uintptr_t>(ZOOM_REL32) + 8));
	pluginLog.FormattedMessage("ROTATE: %p", ROTATE);
	pluginLog.FormattedMessage("ROTATE_FLOAT: %p", uintptr_t(ROTATE) + (static_cast<uintptr_t>(ROTATE_REL32) + 8));
	pluginLog.FormattedMessage("ACHIEVEMENTS: %p", ACHIEVEMENTS);
	pluginLog.FormattedMessage("WORKSHOPSIZE_DRAWS_OLDCODE: %p | original bytes: %02X%02X%02X%02X%02X%02X", WORKSHOPSIZE, WORKSHOPSIZE_DRAWS_OLDCODE[0], WORKSHOPSIZE_DRAWS_OLDCODE[1], WORKSHOPSIZE_DRAWS_OLDCODE[2], WORKSHOPSIZE_DRAWS_OLDCODE[3], WORKSHOPSIZE_DRAWS_OLDCODE[4], WORKSHOPSIZE_DRAWS_OLDCODE[5]);
	pluginLog.FormattedMessage("WORKSHOPSIZE_TRIANGLES_OLDCODE: %p | original bytes: %02X%02X%02X%02X%02X%02X", WORKSHOPSIZE + 0x0A, WORKSHOPSIZE_TRIANGLES_OLDCODE[0], WORKSHOPSIZE_TRIANGLES_OLDCODE[1], WORKSHOPSIZE_TRIANGLES_OLDCODE[2], WORKSHOPSIZE_TRIANGLES_OLDCODE[3], WORKSHOPSIZE_TRIANGLES_OLDCODE[4], WORKSHOPSIZE_TRIANGLES_OLDCODE[5]);
	pluginLog.FormattedMessage("WORKSHOPSIZE_CURRENT: %p", uintptr_t(WORKSHOPSIZE) + (static_cast<uintptr_t>(WORKSHOPSIZE_REL32) + 6));
	pluginLog.FormattedMessage("------------------------------------------------------------------------------");
}

static void PIR_Init()
{
	// search for all the memory patterns
	ConsoleArgFinder			= Utility::pattern("4C 89 4C 24 20 48 89 4C 24 08 53 55 56 57 41 54 41 55 41 56 41 57").count(1).get(0).get<uintptr_t>();
	FirstConsoleFinder			= Utility::pattern("48 8D 1D ? ? ? ? 48 8D 35 ? ? ? ? 66 90 48 8B 53 F8").count(1).get(0).get<uintptr_t>();
	FirstObScriptFinder			= Utility::pattern("48 8D 1D ? ? ? ? 4C 8D 35 ? ? ? ? 0F 1F 40 00 0F 1F 84 00 00 00 00 00").count(1).get(0).get<uintptr_t>();
	GConsoleFinder				= Utility::pattern("48 8D 05 ? ? ? ? 48 89 2D ? ? ? ? 48 89 05 ? ? ? ? 89 2D ? ? ? ? 40 88 2D ? ? ? ? 48").count(1).get(0).get<uintptr_t>(); // for console print
	SetScaleFinder				= Utility::pattern("E8 ? ? ? ? 40 84 F6 75 07 81 63 10 FF FF DF FF 33 ED").count(1).get(0).get<uintptr_t>();
	GetScaleFinder				= Utility::pattern("66 89 BB 08 01 00 00 E8 ? ? ? ? 48 8B 0D ? ? ? ? 0F 28 F0 48").count(1).get(0).get<uintptr_t>();
	CurrentRefFinder			= Utility::pattern("48 8B 1D ? ? ? ? 4C 8D 24 C3 49 3B DC 0F 84 ? ? ? ? 66").count(1).get(0).get<uintptr_t>();
	WorkshopModeFinder			= Utility::pattern("80 3D ? ? ? ? 00 74 0E C6 07 02 48 8B 5C 24 30 48 83 C4 20 5F C3").count(1).get(0).get<uintptr_t>(); //is player in ws mode
	CHANGE_A					= Utility::pattern("C6 05 ? ? ? ? 01 84 C0 75 A9 B1 02").count(1).get(0).get<uintptr_t>();
	CHANGE_B					= Utility::pattern("B2 01 88 15 ? ? ? ? EB 04 84 D2 74 07").count(1).get(0).get<uintptr_t>();
	CHANGE_C					= Utility::pattern("0F B6 05 ? ? ? ? 44 0F B6 A5 ? ? ? ? 84 C0 75").count(1).get(0).get<uintptr_t>();
	CHANGE_D					= Utility::pattern("0F B6 05 ? ? ? ? 3A 05 ? ? ? ? 48 8B B4 24 ? ? ? ? C6 05").count(1).get(0).get<uintptr_t>();
	CHANGE_E					= Utility::pattern("76 0C C6 05 ? ? ? ? 06 E9 ? ? ? ? 40 84 ED 0F 84").count(1).get(0).get<uintptr_t>();
	CHANGE_F					= Utility::pattern("88 05 ? ? ? ? E8 ? ? ? ? 84 C0 75 ? 80 3D ? ? ? ? 02").count(1).get(0).get<uintptr_t>();
	CHANGE_G					= Utility::pattern("0F 95 05 ? ? ? ? E8 ? ? ? ? 40 38 35 ? ? ? ? 48").count(1).get(0).get<uintptr_t>();
	CHANGE_H					= Utility::pattern("74 11 40 84 F6 74 0C 48 8D ? ? 49 8B ? E8").count(1).get(0).get<uintptr_t>();
	CHANGE_I					= Utility::pattern("74 35 48 8B B5 ? ? ? ? 48 8B CE E8 ? ? ? ? 84 C0 75").count(1).get(0).get<uintptr_t>(); // ignore water restrictions
	YELLOW						= Utility::pattern("8B 58 14 48 8D 4C 24 30 8B D3 45 33 C0 E8").count(1).get(0).get<uintptr_t>(); // allow moving yellow objects
	RED							= Utility::pattern("89 05 ? ? ? ? C6 05 ? ? ? ? 01 48 83 C4 68 C3").count(1).get(0).get<uintptr_t>(); //keep objects green
	WSTIMER2					= Utility::pattern("F3 0F 11 05 ? ? ? ? 77 65 48 8B 0D ? ? ? ? 41 B1 01 45 0F B6 C1 33 D2 E8").count(1).get(0).get<uintptr_t>(); // New ws timer check is buried in here
	OBJECTSNAP					= Utility::pattern("F3 0F 10 35 ? ? ? ? 0F 28 C6 48 ? ? ? ? ? ? 33 C0").count(1).get(0).get<uintptr_t>();
	GROUNDSNAP					= Utility::pattern("0F 86 ? ? ? ? 41 8B 4E 34 49 B8").count(1).get(0).get<uintptr_t>();
	WORKSHOPSIZE				= Utility::pattern("01 05 ? ? ? ? 8B 44 24 58 01 05 ? ? ? ? 85 D2 0F 84").count(1).get(0).get<uintptr_t>(); // where the game adds to the ws size
	OUTLINES					= Utility::pattern("C6 05 ? ? ? ? 01 88 15 ? ? ? ? 76 13 48 8B 05").count(1).get(0).get<uintptr_t>(); // object outlines not instant
	ZOOM						= Utility::pattern("F3 0F 10 0D ? ? ? ? 0F 29 74 24 20 F3 0F 10 35").count(1).get(0).get<uintptr_t>();
	ROTATE						= Utility::pattern("F3 0F 10 05 ? ? ? ? ? ? ? ? ? ? ? ? 84 C9 75 07 0F 57 05").count(1).get(0).get<uintptr_t>(); //better compatibility with high physics fps
	ACHIEVEMENTS				= Utility::pattern("48 83 EC 28 C6 44 24 ? 00 84 D2 74 1C 48").count(1).get(0).get<uintptr_t>();

	// storing the old bytes and setting pointers
	if (CHANGE_C) { ReadMemory((uintptr_t(CHANGE_C)), &CHANGE_C_OLDCODE, 0x07); }
	if (CHANGE_D) { ReadMemory((uintptr_t(CHANGE_D)), &CHANGE_D_OLDCODE, 0x07); }
	if (CHANGE_F) { ReadMemory((uintptr_t(CHANGE_F)), &CHANGE_F_OLDCODE, 0x06); }
	if (OBJECTSNAP) { ReadMemory((uintptr_t(OBJECTSNAP)), &OBJECTSNAP_OLDCODE, 0x08); }
	if (WSTIMER2) { ReadMemory(uintptr_t(WSTIMER2), &WSTIMER2_OLDCODE, 0x08); }
	if (ZOOM) { ReadMemory((uintptr_t(ZOOM) + 0x04), &ZOOM_REL32, sizeof(SInt32)); }
	if (ROTATE) { ReadMemory((uintptr_t(ROTATE) + 0x04), &ROTATE_REL32, sizeof(SInt32)); }

	if (WORKSHOPSIZE) {
		ReadMemory((uintptr_t(WORKSHOPSIZE)), &WORKSHOPSIZE_DRAWS_OLDCODE, 0x06);
		ReadMemory((uintptr_t(WORKSHOPSIZE) + 0x0A), &WORKSHOPSIZE_TRIANGLES_OLDCODE, 0x06);
		ReadMemory((uintptr_t(WORKSHOPSIZE) + 0x02), &WORKSHOPSIZE_REL32, sizeof(SInt32));
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

	if (CurrentRefFinder) {
		CurrentRefBaseRel32 = GetRel32FromPattern(CurrentRefFinder, 0x03, 0x07, 0x00);
		CurrentRefBase = RelocationManager::s_baseAddr + static_cast<uintptr_t>(CurrentRefBaseRel32);
	}

	if (ConsoleArgFinder) {
		ConsoleArgRel32 = uintptr_t(ConsoleArgFinder) - RelocationManager::s_baseAddr;
		RelocAddr <_GetConsoleArg> GetDatArg(ConsoleArgRel32);
		GetConsoleArg = GetDatArg;
	}

	if (FirstConsoleFinder){
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


__declspec(dllexport) bool F4SEPlugin_Load(const F4SEInterface* f4seinterface)
{
	// start log
	pluginLog.OpenRelative(CSIDL_MYDOCUMENTS, pluginLogFile);
	pluginLog.FormattedMessage("[F4SEPlugin_Load] Plugin loaded!");

	// get a plugin handle
	pluginHandle = f4seinterface->GetPluginHandle();
	if (!pluginHandle) { 
		pluginLog.FormattedMessage("[F4SEPlugin_Load] Plugin load failed! Couldn't get a plugin handle!"); 
		return false; 
	}

	pluginLog.FormattedMessage("[F4SEPlugin_Load] Got a plugin handle!");
	pluginLog.FormattedMessage("[F4SEPlugin_Load] Locating memory patterns...");

	// init and log memory patterns
	PIR_Init();
	PIR_LogMemoryPatterns();

	// check memory patterns
	if (!PIR_FoundRequiredMemoryPatterns())
	{
		pluginLog.FormattedMessage("[F4SEPlugin_Load] Plugin load failed! Couldnt find required memory patterns. Check for conflicting mods.");
		return false;
	}

	// papyrus interface
	g_papyrus = (F4SEPapyrusInterface*)f4seinterface->QueryInterface(kInterface_Papyrus);
	if (!g_papyrus) {
		pluginLog.FormattedMessage("[F4SEPlugin_Load] Plugin load failed! Failed to set papyrus interface.");
		return false;
	}
	
	// messaging interface
	g_messaging = (F4SEMessagingInterface*)f4seinterface->QueryInterface(kInterface_Messaging);
	if (!g_messaging) {
		pluginLog.FormattedMessage("[F4SEPlugin_Load] Plugin load failed! Failed to set messaging interface.");
		return false;
	}

	// object interface
	g_object = (F4SEObjectInterface*)f4seinterface->QueryInterface(kInterface_Object);
	if (!g_object) {
		pluginLog.FormattedMessage("[F4SEPlugin_Load] Plugin load failed! Failed to set object interface.");
		return false;
	}
		
	// register papyrus functions
	pluginLog.FormattedMessage("[F4SEPlugin_Load] Registering papyrus functions");
	g_papyrus->Register(papyrusPlaceInRed::RegisterFuncs);

	// register message listener handler
	pluginLog.FormattedMessage("[F4SEPlugin_Load] Registering message listeners.");
	g_messaging->RegisterListener(pluginHandle, "F4SE", MessageInterfaceHandler);

	// attempt to create the console command
	pluginLog.FormattedMessage("[F4SEPlugin_Load] Creating console command.");
	if (!PIR_CreateConsoleCommand()){
		pluginLog.FormattedMessage("[F4SEPlugin_Load] Failed to create console command! Plugin will run with hard coded default options.");
	}

	// toggle defaults
	pluginLog.FormattedMessage("[F4SEPlugin_Load] Toggling on defaults.");
	Toggle_PlaceInRed();
	Toggle_Achievements();

	// plugin loaded
	pluginLog.FormattedMessage("[F4SEPlugin_Load] Plugin load finished!");
	return true;
}

__declspec(dllexport) F4SEPluginVersionData F4SEPlugin_Version = {
	F4SEPluginVersionData::kVersion,
	pluginVersion,
	"Place In Red",
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
