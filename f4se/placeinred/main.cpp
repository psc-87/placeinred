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
static _GetConsoleArg GetConsoleArg; // to parse arguments from user after execution
static uintptr_t* ConsoleArgFinder = nullptr; // memory pattern to find the real function
static SInt32 ConsoleArgRel32 = 0; // rel32 set later on

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

// workshop mode finder
static uintptr_t* WSModeFinder = nullptr;
static SInt32 WSModeFinderRel32 = 0;
static uintptr_t WSModeBoolAddress;

// SetMotionType
static uintptr_t* SetMotionTypeFinder = nullptr;
static SInt32 SetMotionTypeRel32 = 0;
static _SetMotionType_Native SetMotionType_Native;

// g_gamedata via pattern
static uintptr_t* GDataHandlerFinder = nullptr;
static SInt32 GDataHandlerRel32 = 0;
static uintptr_t GDataHandlerStatic;

// Currently grabbed or highlighted workshop reference
static uintptr_t* CurrentWSRefFinder = nullptr; // pattern to help us find it
static uintptr_t CurrentRefBase; // base address
static SInt32 CurrentRefBaseRel32 = 0; // rel32 of base address

// useful offsets
static UInt64 ref_offsets[] = { 0x0, 0x0, 0x10, 0x110 }; //current workshop TESObjectREFR
static size_t ref_count = sizeof(ref_offsets) / sizeof(ref_offsets[0]);

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
static uintptr_t* GROUNDSNAP = nullptr;
static uintptr_t* OBJECTSNAP = nullptr;
static uintptr_t* WSSIZE = nullptr;
static uintptr_t* OUTLINES = nullptr;
static uintptr_t* AchievementsFinder = nullptr;

// For proper toggling
static UInt8 NOP1[1] = { 0x90 }; // 1 byte nop
static UInt8 NOP2[2] = { 0x66, 0x90 }; // 2 byte nop
static UInt8 NOP3[3] = { 0x0F, 0x1F, 0x00 }; // 3 byte nop
static UInt8 NOP4[4] = { 0x0F, 0x1F, 0x40, 0x00 }; // 4 byte nop
static UInt8 NOP5[5] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // 5 byte nop
static UInt8 NOP6[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // 6 byte nop
static UInt8 NOP7[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 }; // 7 byte nop
static UInt8 NOP8[8] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 }; // 8 byte nop

static UInt8 CHANGE_C_OLD[7];
static UInt8 CHANGE_C_NEW[7] = { 0x31, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90 }; //xor al,al;nop x5
static UInt8 CHANGE_D_OLD[7];
static UInt8 CHANGE_D_NEW[7] = { 0x31, 0xC0, 0xB0, 0x01, 0x90, 0x90, 0x90 }; //xor al,al;mov al,01;nop x3
static UInt8 CHANGE_F_OLD[6];
//static UInt8 CHANGE_F_NEW[2] = { 0xEB, 0x04 };
static UInt8 CHANGE_I_OLD[2] = { 0x74, 0x35 };
static UInt8 CHANGE_I_NEW[2] = { 0xEB, 0x30 };
static UInt8 YELLOW_OLD[3] = { 0x8B, 0x58, 0x14 };
static UInt8 YELLOW_NEW[3] = { 0x0F, 0x1F, 0x00 }; //3 byte nop
static UInt8 WSTIMER_OLD[6] = { 0x0F, 0x85, 0xAB, 0x00, 0x00, 0x00 }; //original is jne
static UInt8 WSTIMER_NEW[6] = { 0xE9, 0xAC, 0x00, 0x00, 0x00, 0x90 }; //jmp instead

static UInt8 Achieve_NEW[3] = { 0x30, 0xC0, 0xC3 }; // xor al, al; ret
static UInt8 Achieve_OLD[4] = { 0x48, 0x83, 0xEC, 0x28 }; // sub rsp,28

static UInt8 OSNAP_OLD[8];
static UInt64 OSNAP_NEW = 0x9090909090F6570F; // xorps xmm6, xmm6; nop x5

static SInt32 WSSIZE_REL32 = 0;
static UInt8 WSDRAWS_OLD[6];
static UInt8 WSTRIS_OLD[6];


static UInt8 CONSOLEREF_OLD[6] = { 0xFF, 0x90, 0xD0, 0x01, 0x00, 0x00 }; //call qword ptr [rax+000001D0]

// zoom and rotate
static uintptr_t* ZoomFinder = nullptr;
static uintptr_t* RotateFinder = nullptr;
static uintptr_t ZoomAddress;
static uintptr_t RotateAddress;
static SInt32 ZoomRel32 = 0;
static SInt32 RotateRel32 = 0;
static Float32 fOriginalZOOM = 10.0; //game default. updated later to fItemHoldDistantSpeed:Workshop
static Float32 fOriginalROTATE = 5.0; //game default. updated later to fItemRotationSpeed:Workshop
static Float32 fSlowerZOOM = 1.0; //plugin default. updated later to plugin ini value
static Float32 fSlowerROTATE = 0.5; //plugin default. updated later to plugin ini value

extern "C"
{
	namespace pir {

		const char* logprefix = { "pir" }; // label used when we log something in this namespace

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
		static SInt32 GetRel32FromPattern(uintptr_t* pattern, UInt64 rel32start, UInt64 end, UInt64 specialmodify = 0x0)
		{
			// pattern: pointer to pattern match
			// rel32start:to reach start of rel32 from pattern
			// end: to reach end of instructions
			// specifymodify: bytes to shift the final result by, default 0 no change
			if (pattern) {
				SInt32 relish32 = 0;
				if (!ReadMemory(uintptr_t(pattern) + rel32start, &relish32, sizeof(SInt32))) {
					return 0;
				}
				else {
					relish32 = (((uintptr_t(pattern) + end) + relish32) - RelocationManager::s_baseAddr) + (specialmodify);
					return relish32;
				}
			}
			return 0;
		}

		// read the address at an address+offset
		static uintptr_t GimmeSinglePointer(uintptr_t address, UInt64 offset)
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
		static uintptr_t* GimmeMultiPointer(uintptr_t baseAddress, UInt64* offsets, UInt64 numOffsets)
		{
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
		static bool InWorkshopMode()
		{
			if (WSModeFinder) {
				UInt8 WSMODE = 0x00;
				ReadMemory(uintptr_t(WSModeBoolAddress), &WSMODE, sizeof(bool));
				if (WSMODE == 0x01) {
					return true;
				}
			}
			return false;
		}

		// return the currently selected workshop ref with some safety checks
		static TESObjectREFR* GetCurrentWSRef(bool refonly = 1)
		{
			PIR_LOG_PREP
				if (CurrentWSRefFinder && CurrentRefBase && pir::InWorkshopMode()) {

					uintptr_t* refptr = GimmeMultiPointer(CurrentRefBase, ref_offsets, ref_count);
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
						//return the ref if we get here
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
				motion = 00000001; //Motion_Dynamic
			}

			if (vm && ref) {
				SetMotionType_Native(vm, NULL, ref, motion, acti);
				if (sound == 1) {
					PlayUISound("UIQuestInactive");
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
			if (GConsoleFinder && GConsoleStatic && PIRSettings.PrintConsoleMessages)
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
		static void MessageInterfaceHandler(F4SEMessagingInterface::Message* msg)
		{
			switch (msg->type) {

			    case F4SEMessagingInterface::kMessage_GameDataReady: 
					PIRSettings.GameDataIsReady = true; 
					break;

			    default: break;
			}
		}

		// Set the scale of the current workshop reference (highlighted or grabbed)
		static bool SetCurrentRefScale(float newScale)
		{
			TESObjectREFR* ref = GetCurrentWSRef();
			if (ref) {
				SetScale(ref, newScale);
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
				float oldscale = GetScale(ref);
				float newScale = oldscale * (fMultiplyAmount);
				if (newScale > 9.9999f) { newScale = 9.9999f; }
				if (newScale < 0.0001f) { newScale = 0.0001f; }
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

			pirlog.FormattedMessage("---------------------------------------------------------");
			pirlog.FormattedMessage("Type|opcode|rel32|address|short|long|params|needsparent|helptext");

			for (ObScriptCommand* iter = FirstConsole; iter->opcode < (kObScript_NumConsoleCommands + kObScript_ConsoleOpBase); ++iter) {
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

			for (ObScriptCommand* iter = FirstObScript; iter->opcode < (kObScript_NumObScriptCommands + kObScript_ScriptOpBase); ++iter) {
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
			if (OUTLINES && PIRSettings.OUTLINES_ENABLED) {
				SafeWrite8((uintptr_t)OUTLINES + 0x06, 0x00); //objects
				SafeWrite8((uintptr_t)OUTLINES + 0x0D, 0xEB); //npcs
				PIRSettings.OUTLINES_ENABLED = false;
				pir::ConsolePrint("Object outlines disabled");
				return true;
			}
			if (OUTLINES && !PIRSettings.OUTLINES_ENABLED) {
				SafeWrite8((uintptr_t)OUTLINES + 0x06, 0x01); //objects
				SafeWrite8((uintptr_t)OUTLINES + 0x0D, 0x76); //npcs
				PIRSettings.OUTLINES_ENABLED = true;
				pir::ConsolePrint("Object outlines enabled");
				return true;
			}
			return false;
		}

		//toggle slower object rotation and zoom speed
		static bool Toggle_SlowZoomAndRotate()
		{
			// its on, turn it off
			if (ZoomFinder && RotateFinder && ZoomAddress && RotateAddress && PIRSettings.SLOW_ENABLED) {
				SafeWriteBuf(ZoomAddress, &fOriginalZOOM, sizeof(Float32));
				SafeWriteBuf(RotateAddress, &fOriginalROTATE, sizeof(Float32));
				PIRSettings.SLOW_ENABLED = false;
				pir::ConsolePrint("Slow zoom and rotate disabled");
				return true;
			}
			// its off, turn it on
			if (ZoomFinder && RotateFinder && ZoomAddress && RotateAddress && !PIRSettings.SLOW_ENABLED) {
				SafeWriteBuf(ZoomAddress, &fSlowerZOOM, sizeof(Float32));
				SafeWriteBuf(RotateAddress, &fSlowerROTATE, sizeof(Float32));
				PIRSettings.SLOW_ENABLED = true;
				pir::ConsolePrint("Slow zoom and rotate enabled");
				return true;
			}
			return false;
		}

		//toggle infinite workshop size
		static bool Toggle_WorkshopSize()
		{
			if (WSSIZE && PIRSettings.WORKSHOPSIZE_ENABLED) {
				SafeWriteBuf((uintptr_t)WSSIZE, WSDRAWS_OLD, sizeof(WSDRAWS_OLD));
				SafeWriteBuf((uintptr_t)WSSIZE + 0x0A, WSTRIS_OLD, sizeof(WSTRIS_OLD));
				PIRSettings.WORKSHOPSIZE_ENABLED = false;
				pir::ConsolePrint("Unlimited workshop size disabled");
				return true;
			}

			if (WSSIZE && PIRSettings.WORKSHOPSIZE_ENABLED == false) {
				// Write nop 6 so its never increased
				SafeWriteBuf((uintptr_t)WSSIZE, NOP6, sizeof(NOP6));
				SafeWriteBuf((uintptr_t)WSSIZE + 0x0A, NOP6, sizeof(NOP6));

				// set current ws draws and triangles to zero
				SafeWrite64(uintptr_t(WSSIZE) + (uintptr_t)WSSIZE_REL32 + 0x06, 0x0000000000000000);
				PIRSettings.WORKSHOPSIZE_ENABLED = true;
				pir::ConsolePrint("Unlimited workshop size enabled");
				return true;
			}
			return false;
		}

		//toggle groundsnap
		static bool Toggle_GroundSnap()
		{
			if (GROUNDSNAP && PIRSettings.GROUNDSNAP_ENABLED) {

				SafeWrite8((uintptr_t)GROUNDSNAP + 0x01, 0x85);
				PIRSettings.GROUNDSNAP_ENABLED = false;
				pir::ConsolePrint("Ground snap disabled");
				return true;
			}
			if (GROUNDSNAP && !PIRSettings.GROUNDSNAP_ENABLED) {
				SafeWrite8((uintptr_t)GROUNDSNAP + 0x01, 0x86);
				PIRSettings.GROUNDSNAP_ENABLED = true;
				pir::ConsolePrint("Ground snap enabled");
				return true;

			}
			return false;
		}

		//toggle objectsnap
		static bool Toggle_ObjectSnap()
		{
			if (OBJECTSNAP && PIRSettings.OBJECTSNAP_ENABLED) {
				SafeWrite64((uintptr_t)OBJECTSNAP, OSNAP_NEW);
				PIRSettings.OBJECTSNAP_ENABLED = false;
				pir::ConsolePrint("Object snap disabled");
				return true;
			}
			if (OBJECTSNAP && !PIRSettings.OBJECTSNAP_ENABLED) {
				SafeWriteBuf((uintptr_t)OBJECTSNAP, OSNAP_OLD, sizeof(OSNAP_OLD));
				PIRSettings.OBJECTSNAP_ENABLED = true;
				pir::ConsolePrint("Object snap enabled");
				return true;
			}
			return false;
		}

		//toggle allowing achievements with mods
		static bool Toggle_Achievements()
		{
			// its on - toggle it off
			if (AchievementsFinder && PIRSettings.ACHIEVEMENTS_ENABLED) {
				SafeWriteBuf((uintptr_t)AchievementsFinder, Achieve_OLD, sizeof(Achieve_OLD));
				PIRSettings.ACHIEVEMENTS_ENABLED = false;
				pir::ConsolePrint("Achievements with mods disabled (game default)");
				return true;
			}
			// its off - toggle it on
			if (AchievementsFinder && !PIRSettings.ACHIEVEMENTS_ENABLED) {
				SafeWriteBuf((uintptr_t)AchievementsFinder, Achieve_NEW, sizeof(Achieve_NEW));
				PIRSettings.ACHIEVEMENTS_ENABLED = true;
				pir::ConsolePrint("Achievements with mods enabled!");
				return true;
			}
			return false;
		}

		//toggle allowing achievements with mods
		static bool Toggle_ConsoleRefName()
		{
			// its on - toggle it off
			if (ConsoleRefFuncFinder && ConsoleRefCallFinder && PIRSettings.ConsoleNameRef_ENABLED)
			{
				SafeWriteBuf(uintptr_t(ConsoleRefCallFinder), CONSOLEREF_OLD, sizeof(CONSOLEREF_OLD));
				PIRSettings.ConsoleNameRef_ENABLED = false;
				pir::ConsolePrint("ConsoleRefName toggled off!");
				return true;
			}

			// its off - toggle it on
			if (ConsoleRefFuncFinder && ConsoleRefCallFinder && !PIRSettings.ConsoleNameRef_ENABLED)
			{
				SafeWriteCall(uintptr_t(ConsoleRefCallFinder), ConsoleRefFuncAddress); //patch call
				SafeWrite8(uintptr_t(ConsoleRefCallFinder) + 0x05, 0x90); //for a clean patch
				PIRSettings.ConsoleNameRef_ENABLED = true;
				pir::ConsolePrint("ConsoleRefName toggled on!");
				return true;
			}
			return false;
		}

		//toggle placing objects in red
		static bool Toggle_PlaceInRed()
		{
			if (PIRSettings.PLACEINRED_ENABLED) {
				SafeWrite8((uintptr_t)CHANGE_A + 0x06, 0x01);
				SafeWrite8((uintptr_t)CHANGE_A + 0x0C, 0x02);
				SafeWrite8((uintptr_t)CHANGE_B + 0x01, 0x01);
				SafeWriteBuf((uintptr_t)CHANGE_C, CHANGE_C_OLD, sizeof(CHANGE_C_OLD));
				SafeWrite8((uintptr_t)CHANGE_C + 0x1D, 0x01); // added june 2024 to test potential improvement
				SafeWriteBuf((uintptr_t)CHANGE_D, CHANGE_D_OLD, sizeof(CHANGE_D_OLD));
				SafeWrite8((uintptr_t)CHANGE_E + 0x00, 0x76);
				SafeWriteBuf((uintptr_t)CHANGE_F, CHANGE_F_OLD, sizeof(CHANGE_F_OLD));
				SafeWrite8((uintptr_t)CHANGE_G + 0x01, 0x95);
				SafeWrite8((uintptr_t)CHANGE_H + 0x00, 0x74);
				SafeWriteBuf((uintptr_t)CHANGE_I, CHANGE_I_OLD, sizeof(CHANGE_I_OLD));
				SafeWrite8((uintptr_t)RED + 0xC, 0x01);
				SafeWriteBuf((uintptr_t)YELLOW, YELLOW_OLD, sizeof(YELLOW_OLD));
				SafeWriteBuf((uintptr_t)WSTIMER, WSTIMER_OLD, sizeof(WSTIMER_OLD));
				PIRSettings.PLACEINRED_ENABLED = false;
				pir::ConsolePrint("Place In Red disabled.");
				return true;
			}

			if (!PIRSettings.PLACEINRED_ENABLED) {
				SafeWrite8((uintptr_t)CHANGE_A + 0x06, 0x00);
				SafeWrite8((uintptr_t)CHANGE_A + 0x0C, 0x01);
				SafeWrite8((uintptr_t)CHANGE_B + 0x01, 0x00);
				SafeWriteBuf((uintptr_t)CHANGE_C, CHANGE_C_NEW, sizeof(CHANGE_C_NEW));
				SafeWrite8((uintptr_t)CHANGE_C + 0x1D, 0x00); // added june 2024 to test potential improvement
				SafeWriteBuf((uintptr_t)CHANGE_D, CHANGE_D_NEW, sizeof(CHANGE_D_NEW));
				SafeWrite8((uintptr_t)CHANGE_E + 0x00, 0xEB);
				SafeWriteBuf((uintptr_t)CHANGE_F, NOP6, sizeof(NOP6));
				SafeWrite8((uintptr_t)CHANGE_G + 0x01, 0x98); // works but look at again later
				SafeWrite8((uintptr_t)CHANGE_H + 0x00, 0xEB);
				SafeWriteBuf((uintptr_t)CHANGE_I, CHANGE_I_NEW, sizeof(CHANGE_I_NEW));
				SafeWrite8((uintptr_t)RED + 0x0C, 0x00);
				SafeWriteBuf((uintptr_t)YELLOW, YELLOW_NEW, sizeof(YELLOW_NEW));
				SafeWriteBuf((uintptr_t)WSTIMER, WSTIMER_NEW, sizeof(WSTIMER_NEW));
				PIRSettings.PLACEINRED_ENABLED = true;
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

		//called every time the console command runs
		static bool ExecuteConsoleCommand(void* paramInfo, void* scriptData, TESObjectREFR* thisObj, void* containingObj, void* scriptObj, void* locals, double* result, void* opcodeOffsetPtr)
		{
			PIR_LOG_PREP
				if (GetConsoleArg && ConsoleArgFinder && (ConsoleArgRel32 != 0)) {

					char param1[4096];
					char param2[4096];
					bool consoleresult = GetConsoleArg(paramInfo, scriptData, opcodeOffsetPtr, thisObj, containingObj, scriptObj, locals, &param1, &param2);

					if (consoleresult && param1[0]) {
						switch (ConsoleSwitch(param1)) {
							// debug and tests
						case pir::ConsoleSwitch("dumpcellrefs"):  pir::DumpCellRefs();             break;
						case pir::ConsoleSwitch("dumpcmds"):      pir::DumpCmds();                 break;
						case pir::ConsoleSwitch("logref"):        pir::LogWSRef();                 break;
						case pir::ConsoleSwitch("moveself"):      pir::MoveRefToSelf(0, 0, 0, 0);  break;
						case pir::ConsoleSwitch("moveselftwice"): pir::MoveRefToSelf(0, 0, 0, 1);  break;
						case pir::ConsoleSwitch("print"):         pir::ConsolePrint(param2);       break;

							//toggles
						case pir::ConsoleSwitch("1"):             pir::Toggle_PlaceInRed();        break;
						case pir::ConsoleSwitch("toggle"):        pir::Toggle_PlaceInRed();        break;
						case pir::ConsoleSwitch("2"):             pir::Toggle_ObjectSnap();        break;
						case pir::ConsoleSwitch("osnap"):         pir::Toggle_ObjectSnap();        break;
						case pir::ConsoleSwitch("3"):             pir::Toggle_GroundSnap();        break;
						case pir::ConsoleSwitch("gsnap"):         pir::Toggle_GroundSnap();        break;
						case pir::ConsoleSwitch("4"):             pir::Toggle_SlowZoomAndRotate(); break;
						case pir::ConsoleSwitch("slow"):          pir::Toggle_SlowZoomAndRotate(); break;
						case pir::ConsoleSwitch("5"):             pir::Toggle_WorkshopSize();      break;
						case pir::ConsoleSwitch("workshopsize"):  pir::Toggle_WorkshopSize();      break;
						case pir::ConsoleSwitch("6"):             pir::Toggle_Outlines();          break;
						case pir::ConsoleSwitch("outlines"):      pir::Toggle_Outlines();          break;
						case pir::ConsoleSwitch("7"):             pir::Toggle_Achievements();      break;
						case pir::ConsoleSwitch("achievements"):  pir::Toggle_Achievements();      break;

							//scale constants
						case pir::ConsoleSwitch("scale1"):	      pir::SetCurrentRefScale(1.0000f); break;
						case pir::ConsoleSwitch("scale10"):	      pir::SetCurrentRefScale(9.9999f); break;

							//scale up								  				     
						case pir::ConsoleSwitch("scaleup1"):	  pir::ModCurrentRefScale(1.0100f); break;
						case pir::ConsoleSwitch("scaleup2"):	  pir::ModCurrentRefScale(1.0200f); break;
						case pir::ConsoleSwitch("scaleup5"):	  pir::ModCurrentRefScale(1.0500f); break;
						case pir::ConsoleSwitch("scaleup10"):	  pir::ModCurrentRefScale(1.1000f); break;
						case pir::ConsoleSwitch("scaleup25"):	  pir::ModCurrentRefScale(1.2500f); break;
						case pir::ConsoleSwitch("scaleup50"):	  pir::ModCurrentRefScale(1.5000f); break;
						case pir::ConsoleSwitch("scaleup100"):	  pir::ModCurrentRefScale(2.0000f); break;

							//scale down			   				  				        			 
						case pir::ConsoleSwitch("scaledown1"):	  pir::ModCurrentRefScale(0.9900f); break;
						case pir::ConsoleSwitch("scaledown2"):	  pir::ModCurrentRefScale(0.9800f); break;
						case pir::ConsoleSwitch("scaledown5"):	  pir::ModCurrentRefScale(0.9500f); break;
						case pir::ConsoleSwitch("scaledown10"):   pir::ModCurrentRefScale(0.9000f); break;
						case pir::ConsoleSwitch("scaledown25"):   pir::ModCurrentRefScale(0.7500f); break;
						case pir::ConsoleSwitch("scaledown50"):   pir::ModCurrentRefScale(0.5000f); break;
						case pir::ConsoleSwitch("scaledown75"):	  pir::ModCurrentRefScale(0.2500f); break;

							// lock and unlock
						case pir::ConsoleSwitch("lock"):   pir::LockOrUnlockCurrentWSRef(0, 1); break;
						case pir::ConsoleSwitch("l"):      pir::LockOrUnlockCurrentWSRef(0, 1); break;
						case pir::ConsoleSwitch("lockq"):  pir::LockOrUnlockCurrentWSRef(0, 0); break; // no sound
						case pir::ConsoleSwitch("unlock"): pir::LockOrUnlockCurrentWSRef(1, 0); break;
						case pir::ConsoleSwitch("u"):      pir::LockOrUnlockCurrentWSRef(1, 0); break;

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
			if (FirstConsole == nullptr) {
				return false;
			}

			static const char* s_CommandToBorrow = "GameComment"; // the command we will replace (full name)
			static ObScriptCommand* s_hijackedCommand = nullptr;
			static ObScriptParam* s_hijackedCommandParams = nullptr;

			for (ObScriptCommand* iter = FirstConsole; iter->opcode < (kObScript_NumConsoleCommands + kObScript_ConsoleOpBase); ++iter) {
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
			if (ConsoleArgFinder && FirstConsoleFinder && FirstObScriptFinder && SetScaleFinder && GetScaleFinder && CurrentWSRefFinder
				&& WSModeFinder && GConsoleFinder && CHANGE_A && CHANGE_B && CHANGE_C && CHANGE_D && CHANGE_E && CHANGE_F && CHANGE_G && CHANGE_H && CHANGE_I
				&& YELLOW && RED && WSTIMER && GROUNDSNAP && OBJECTSNAP && OUTLINES && WSSIZE && ZoomFinder && RotateFinder && SetMotionTypeFinder)
			{
				// allow plugin to load even if these arent found
				// AchievementsFinder, ConsoleRefCallFinder
				// GDataHandlerFinder not using yet

				return true;
			}
			else {
				return false;
			}
		}

		//log all the memory patterns to the log file
		static void LogMemoryPatterns()
		{
			pirlog.FormattedMessage("--------------------------------------------------------------------");
			pirlog.FormattedMessage("Base                  : %p", RelocationManager::s_baseAddr);
			pirlog.FormattedMessage("AchievementsFinder    : %p", AchievementsFinder);
			pirlog.FormattedMessage("ZoomFinder            : %p", ZoomFinder);
			pirlog.FormattedMessage("RotateFinder          : %p", RotateFinder);
			pirlog.FormattedMessage("ZoomAddress           : %p | orig %f | slow %f", ZoomAddress, fOriginalZOOM, fSlowerZOOM);
			pirlog.FormattedMessage("RotateAddress         : %p | orig %f | slow %f", RotateAddress, fOriginalROTATE, fSlowerROTATE);
			pirlog.FormattedMessage("CHANGE_A              : %p", CHANGE_A);
			pirlog.FormattedMessage("CHANGE_B              : %p", CHANGE_B);
			pirlog.FormattedMessage("CHANGE_C              : %p | original bytes: %02X%02X%02X%02X%02X%02X%02X", CHANGE_C, CHANGE_C_OLD[0], CHANGE_C_OLD[1], CHANGE_C_OLD[2], CHANGE_C_OLD[3], CHANGE_C_OLD[4], CHANGE_C_OLD[5], CHANGE_C_OLD[6]);
			pirlog.FormattedMessage("CHANGE_D              : %p | original bytes: %02X%02X%02X%02X%02X%02X%02X", CHANGE_D, CHANGE_D_OLD[0], CHANGE_D_OLD[1], CHANGE_D_OLD[2], CHANGE_D_OLD[3], CHANGE_D_OLD[4], CHANGE_D_OLD[5], CHANGE_D_OLD[6]);
			pirlog.FormattedMessage("CHANGE_E              : %p", CHANGE_E);
			pirlog.FormattedMessage("CHANGE_F              : %p", CHANGE_F);
			pirlog.FormattedMessage("CHANGE_G              : %p", CHANGE_G);
			pirlog.FormattedMessage("CHANGE_H              : %p", CHANGE_H);
			pirlog.FormattedMessage("CHANGE_I              : %p", CHANGE_I);
			pirlog.FormattedMessage("GROUNDSNAP            : %p", GROUNDSNAP);
			pirlog.FormattedMessage("OUTLINES              : %p", OUTLINES);
			pirlog.FormattedMessage("YELLOW                : %p", YELLOW);
			pirlog.FormattedMessage("RED                   : %p", RED);
			pirlog.FormattedMessage("WSTIMER               : %p", WSTIMER);
			pirlog.FormattedMessage("WSSIZE                : %p", uintptr_t(WSSIZE) + (static_cast<uintptr_t>(WSSIZE_REL32) + 6));
			pirlog.FormattedMessage("OBJECTSNAP            : %p | original bytes: %02X%02X%02X%02X%02X%02X%02X%02X", OBJECTSNAP, OSNAP_OLD[0], OSNAP_OLD[1], OSNAP_OLD[2], OSNAP_OLD[3], OSNAP_OLD[4], OSNAP_OLD[5], OSNAP_OLD[6], OSNAP_OLD[7]);
			pirlog.FormattedMessage("WSDRAWS_OLD           : %p | original bytes: %02X%02X%02X%02X%02X%02X", WSSIZE, WSDRAWS_OLD[0], WSDRAWS_OLD[1], WSDRAWS_OLD[2], WSDRAWS_OLD[3], WSDRAWS_OLD[4], WSDRAWS_OLD[5]);
			pirlog.FormattedMessage("WSTRIS_OLD            : %p | original bytes: %02X%02X%02X%02X%02X%02X", WSSIZE + 0x0A, WSTRIS_OLD[0], WSTRIS_OLD[1], WSTRIS_OLD[2], WSTRIS_OLD[3], WSTRIS_OLD[4], WSTRIS_OLD[5]);
			pirlog.FormattedMessage("ConsoleRefCallFinder  : %p", ConsoleRefCallFinder);
			pirlog.FormattedMessage("ConsoleRefFuncFinder  : %p", ConsoleRefFuncFinder);
			pirlog.FormattedMessage("ConsoleRefFuncAddress : %p | rel32: 0x%08X", ConsoleRefFuncAddress, static_cast<uintptr_t>(ConsoleRefFuncRel32));
			pirlog.FormattedMessage("WSModeFinder          : %p | rel32: 0x%08X | %p", WSModeFinder, WSModeFinderRel32, WSModeBoolAddress);
			pirlog.FormattedMessage("FirstConsoleFinder    : %p | rel32: 0x%08X", FirstConsoleFinder, FirstConsoleRel32);
			pirlog.FormattedMessage("FirstObScriptFinder   : %p | rel32: 0x%08X", FirstObScriptFinder, FirstObScriptRel32);
			pirlog.FormattedMessage("GConsoleFinder        : %p | rel32: 0x%08X | %p", GConsoleFinder, GConsoleRel32, GConsoleStatic);
			pirlog.FormattedMessage("GDataHandlerFinder    : %p | rel32: 0x%08X | %p", GDataHandlerFinder, GDataHandlerRel32, GDataHandlerStatic);
			pirlog.FormattedMessage("ConsoleArgFinder      : %p | rel32: 0x%08X", ConsoleArgFinder, ConsoleArgRel32);
			pirlog.FormattedMessage("CurrentWSRefFinder    : %p | rel32: 0x%08X | %p", CurrentWSRefFinder, CurrentRefBaseRel32, CurrentRefBase);
			pirlog.FormattedMessage("SetScaleFinder        : %p | rel32: 0x%08X", SetScaleFinder, SetScaleRel32);
			pirlog.FormattedMessage("GetScaleFinder        : %p | rel32: 0x%08X", GetScaleFinder, GetScaleRel32);
			pirlog.FormattedMessage("SetMotionTypeFinder   : %p", SetMotionTypeFinder);
			pirlog.FormattedMessage("--------------------------------------------------------------------");
		}

		//read the ini and set default settings
		static void ReadINIDefaults()
		{
			PIR_LOG_PREP

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
			if (SETTING07 == "1") {
				pir::Toggle_Achievements();
				//[Main] ConsoleNameRef_ENABLED
				if (SETTING08 == "1") { pir::Toggle_ConsoleRefName(); }
				//[Main] PrintConsoleMessages
				if (SETTING09 == "0") { PIRSettings.PrintConsoleMessages = 0; }

				//[Main] fSlowerROTATE
				if (!SETTING10.empty()) {
					Float32 rTemp = FloatFromString(SETTING10);
					if (rTemp == 0) {
						PIRSettings.fSlowerROTATE = 0.5; //bad ini force plugin default
						pirlog.FormattedMessage("[INI] fSlowerROTATE: invalid. Using 0.5");
					}
					else {
						PIRSettings.fSlowerROTATE = rTemp;
					}
				}

				//[Main] fSlowerZOOM
				if (!SETTING11.empty()) {
					Float32 zTemp = FloatFromString(SETTING11);
					if (zTemp == 0) {
						PIRSettings.fSlowerZOOM = 1.0; // bad ini force plugin default
						pirlog.FormattedMessage("[INI] fSlowerZOOM: invalid. Using 1.0");
					}
					else {
						PIRSettings.fSlowerZOOM = zTemp;
					}
				}
				// toggle this one AFTER reading the ini setting
				if (SETTING04 == "1") { pir::Toggle_SlowZoomAndRotate(); }

			}

		}
		
		//init f4se stuff and return false if anything fails
		static bool Init_F4SE(const F4SEInterface* f4se)
		{
			PIR_LOG_PREP
			// get a plugin handle
			pirPluginHandle = f4se->GetPluginHandle();
			if (!pirPluginHandle) {
				pirlog.FormattedMessage("[%s] Plugin load failed! Couldn't get a plugin handle!", thisfunc);
				return false;
			}
			pirlog.FormattedMessage("[%s] Got a plugin handle.", thisfunc);

			// messaging interface
			g_messaging = (F4SEMessagingInterface*)f4se->QueryInterface(kInterface_Messaging);
			if (!g_messaging) {
				pirlog.FormattedMessage("[%s] Plugin load failed! Failed to set messaging interface.", thisfunc);
				return false;
			}

			// object interface
			g_object = (F4SEObjectInterface*)f4se->QueryInterface(kInterface_Object);
			if (!g_object) {
				pirlog.FormattedMessage("[%s] Plugin load failed! Failed to set object interface.", thisfunc);
				return false;
			}

			// register message listener handler
			if (g_messaging->RegisterListener(pirPluginHandle, "F4SE", pir::MessageInterfaceHandler) == false) {
				return false;
			}

			pirlog.FormattedMessage("[%s] F4SE interfaces are set.", thisfunc);
			return true;

		}
		
		static void Init_PlaceInRed()
		{
			// search for memory patterns
			ConsoleArgFinder = Utility::pattern("4C 89 4C 24 20 48 89 4C 24 08 53 55 56 57 41 54 41 55 41 56 41 57").count(1).get(0).get<uintptr_t>();
			SetMotionTypeFinder = Utility::pattern("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 50 45 32 E4 41 8D 41 FF").count(1).get(0).get<uintptr_t>();
			FirstConsoleFinder = Utility::pattern("48 8D 1D ? ? ? ? 48 8D 35 ? ? ? ? 66 90 48 8B 53 F8").count(1).get(0).get<uintptr_t>();
			FirstObScriptFinder = Utility::pattern("48 8D 1D ? ? ? ? 4C 8D 35 ? ? ? ? 0F 1F 40 00 0F 1F 84 00 00 00 00 00").count(1).get(0).get<uintptr_t>();
			GConsoleFinder = Utility::pattern("48 8D 05 ? ? ? ? 48 89 2D ? ? ? ? 48 89 05 ? ? ? ? 89 2D ? ? ? ? 40 88 2D ? ? ? ? 48").count(1).get(0).get<uintptr_t>(); // for console print
			SetScaleFinder = Utility::pattern("E8 ? ? ? ? 40 84 F6 75 07 81 63 10 FF FF DF FF 33 ED").count(1).get(0).get<uintptr_t>(); //setscale
			GetScaleFinder = Utility::pattern("66 89 BB 08 01 00 00 E8 ? ? ? ? 48 8B 0D ? ? ? ? 0F 28 F0 48").count(1).get(0).get<uintptr_t>(); //getscale
			CurrentWSRefFinder = Utility::pattern("48 8B 1D ? ? ? ? 4C 8D 24 C3 49 3B DC 0F 84 ? ? ? ? 66").count(1).get(0).get<uintptr_t>(); //has address leading to current WS ref
			WSModeFinder = Utility::pattern("80 3D ? ? ? ? 00 74 0E C6 07 02 48 8B 5C 24 30 48 83 C4 20 5F C3").count(1).get(0).get<uintptr_t>(); //is player in ws mode
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
			WSTIMER = Utility::pattern("0F 85 AB 00 00 00 F3 0F 10 05 ? ? ? ? 41 0F 2E C3 75 66 F3 0F 10 05 ? ? ? ? F3 0F 11 05 ? ? ? ? C6").count(1).get(0).get<uintptr_t>(); // New ws timer check is buried in here
			OBJECTSNAP = Utility::pattern("F3 0F 10 35 ? ? ? ? 0F 28 C6 48 ? ? ? ? ? ? 33 C0").count(1).get(0).get<uintptr_t>();
			GROUNDSNAP = Utility::pattern("0F 86 ? ? ? ? 41 8B 4E 34 49 B8").count(1).get(0).get<uintptr_t>();
			WSSIZE = Utility::pattern("01 05 ? ? ? ? 8B 44 24 58 01 05 ? ? ? ? 85 D2 0F 84").count(1).get(0).get<uintptr_t>(); // where the game adds to the ws size
			OUTLINES = Utility::pattern("C6 05 ? ? ? ? 01 88 15 ? ? ? ? 76 13 48 8B 05").count(1).get(0).get<uintptr_t>(); // object outlines not instant
			ZoomFinder = Utility::pattern("F3 0F 10 0D ? ? ? ? 0F 29 74 24 20 F3 0F 10 35").count(1).get(0).get<uintptr_t>();
			RotateFinder = Utility::pattern("F3 0F 10 05 ? ? ? ? ? ? ? ? ? ? ? ? 84 C9 75 07 0F 57 05").count(1).get(0).get<uintptr_t>(); //better compatibility with high physics fps
			AchievementsFinder = Utility::pattern("48 83 EC 28 C6 44 24 ? 00 84 D2 74 1C 48").count(1).get(0).get<uintptr_t>();
			ConsoleRefFuncFinder = Utility::pattern("E8 ? ? ? ? 4C 8B 05 ? ? ? ? 48 8D 4C 24 40 4C 8B C8 BA 00 01 00 00 E8 ? ? ? ? 83").count(1).get(0).get<uintptr_t>(); //consolenamefix credit registrator2000
			ConsoleRefCallFinder = Utility::pattern("FF 90 D0 01 00 00 48 89 74 24 40 4C 8D 05 ? ? ? ? 4C").count(1).get(0).get<uintptr_t>(); //consolenamefix credit registrator2000
			GDataHandlerFinder = Utility::pattern("48 83 3D ? ? ? ? 00 4D 8B F1 41 0F B6 F0 48 8B FA 48 8B D9 0F 84 ? ? ? ? 80 3D ? ? ? ? 00 48").count(1).get(0).get<uintptr_t>();

			// store old bytes
			if (CHANGE_C) { ReadMemory((uintptr_t(CHANGE_C)), &CHANGE_C_OLD, 0x07); }
			if (CHANGE_D) { ReadMemory((uintptr_t(CHANGE_D)), &CHANGE_D_OLD, 0x07); }
			if (CHANGE_F) { ReadMemory((uintptr_t(CHANGE_F)), &CHANGE_F_OLD, 0x06); }
			if (OBJECTSNAP) { ReadMemory((uintptr_t(OBJECTSNAP)), &OSNAP_OLD, 0x08); }

			if (WSSIZE) {
				ReadMemory((uintptr_t(WSSIZE) + 0x00), &WSDRAWS_OLD, 0x06);
				ReadMemory((uintptr_t(WSSIZE) + 0x0A), &WSTRIS_OLD, 0x06);
				ReadMemory((uintptr_t(WSSIZE) + 0x02), &WSSIZE_REL32, sizeof(SInt32));
			}

			if (ZoomFinder && RotateFinder) {
				ZoomRel32 = GetRel32FromPattern(ZoomFinder, 0x04, 0x08, 0x00);
				RotateRel32 = GetRel32FromPattern(RotateFinder, 0x04, 0x08, 0x00);
				ZoomAddress = RelocationManager::s_baseAddr + (uintptr_t)ZoomRel32;
				RotateAddress = RelocationManager::s_baseAddr + (uintptr_t)RotateRel32;
				ReadMemory(RotateAddress, &fOriginalROTATE, sizeof(Float32));
				ReadMemory(ZoomAddress, &fOriginalZOOM, sizeof(Float32));
			}

			if (ConsoleRefCallFinder && ConsoleRefFuncFinder) {
				ConsoleRefFuncRel32 = GetRel32FromPattern(ConsoleRefFuncFinder, 0x01, 0x05, 0x00); //rel32 of the good function
				ConsoleRefFuncAddress = RelocationManager::s_baseAddr + (uintptr_t)ConsoleRefFuncRel32; // calculate the full address
			}

			if (WSModeFinder) {
				WSModeFinderRel32 = GetRel32FromPattern(WSModeFinder, 0x02, 0x07, 0x00);
				WSModeBoolAddress = RelocationManager::s_baseAddr + WSModeFinderRel32;
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
				GConsoleStatic = RelocationManager::s_baseAddr + (uintptr_t)GConsoleRel32;
			}

			if (GDataHandlerFinder) {
				GDataHandlerRel32 = GetRel32FromPattern(GDataHandlerFinder, 0x03, 0x08, 0x00);
				GDataHandlerStatic = RelocationManager::s_baseAddr + (uintptr_t)GDataHandlerRel32;
			}

			if (CurrentWSRefFinder) {
				CurrentRefBaseRel32 = GetRel32FromPattern(CurrentWSRefFinder, 0x03, 0x07, 0x00);
				CurrentRefBase = RelocationManager::s_baseAddr + (uintptr_t)CurrentRefBaseRel32;
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

			if (SetMotionTypeFinder) {
				SetMotionTypeRel32 = uintptr_t(SetMotionTypeFinder) - RelocationManager::s_baseAddr;
				RelocAddr <_SetMotionType_Native> GimmeSetMotionType(SetMotionTypeRel32);
				SetMotionType_Native = GimmeSetMotionType;
			}
		}
	}


	__declspec(dllexport) bool F4SEPlugin_Load(const F4SEInterface* f4se)
	{
		PIR_LOG_PREP

		// start log
		pirlog.OpenRelative(CSIDL_MYDOCUMENTS, pluginLogFile);
		pirlog.FormattedMessage("[%s] Plugin loaded.", thisfunc);

		if (!pir::Init_F4SE(f4se)){
			return false;
		}

		pir::Init_PlaceInRed();

		// check memory patterns
		if (!pir::FoundRequiredMemoryPatterns())
		{
			pir::LogMemoryPatterns();
			pirlog.FormattedMessage("[%s] Plugin load failed! Couldnt find required memory patterns. Check for conflicting mods.", thisfunc);
			return false;
		}



		// attempt to create the console command
		pirlog.FormattedMessage("[%s] Creating console command.", thisfunc);
		if (!pir::CreateConsoleCommand()) {
			pirlog.FormattedMessage("[%s] Failed to create console command! Another mod may be conflicting.", thisfunc);
		}

		// toggle defaults
		pirlog.FormattedMessage("[%s] Setting options from PlaceInRed.ini", thisfunc);
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


}