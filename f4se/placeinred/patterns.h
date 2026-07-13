#pragma once

#include <vector>
#include <string_view>
#include <charconv>
#include <cstdint>
#include <cstdarg>

// F4SE API
#include "common/ITypes.h"
#include "f4se/ObScript.h"


struct SimpleFinder
{
    uintptr_t* ptr = nullptr; // Pointer to a pattern match
    SInt32           r32 = 0;       // Rel32 offset
    uintptr_t        addr = 0;       // Final resolved addresss
    ObScriptCommand* cmd = nullptr; // For console commands
};

static SimpleFinder CurrentWSRef; // the current workshop reference highlighted or grabbed
static SimpleFinder FirstConsole; // first console command
static SimpleFinder FirstObScript; // first objectscript command
static SimpleFinder WorkbenchSelection; // function determines workshop object selection based on 
static SimpleFinder InvalidRefHandle;
static SimpleFinder gConsole; // gconsole
static SimpleFinder ParseConsoleArg;
static SimpleFinder SetMotionType;
static SimpleFinder Rotate;
static SimpleFinder Zoom;
static SimpleFinder WSMode;
static SimpleFinder WSSize;

/**
 * @brief Parses and compiles an IDA-style hex byte signature into parallel byte/mask vectors.
 * * Optimized for high-performance pattern scanning. Pre-compiles the string signature at runtime
 * into contiguous arrays suitable for cache-friendly linear scanning or SIMD vectorization.
 * * Supported formats:
 * - Exact bytes: "48 8B 05" or "488B05"
 * - Wildcards:   "??" or "?" (spaces optional)
 * - Example:     const ParsedPattern Pattern_A{ "48 89 5C ? ? ? ? C8 ? ? ? ?" };
 */
struct ParsedPattern
{
    std::vector<uint8_t> bytes;     ///< Target byte sequence. Wildcard positions hold 0x00.
    std::vector<uint8_t> mask;      ///< Bitmask: 0xFF = exact match required, 0x00 = wildcard/ignore.

    // Anchor optimizations: Used to rapidly skip mismatched memory regions via std::memchr or SIMD
    // before performing a full byte-by-byte comparison loop.
    size_t               anchorPos = 0; ///< Index of the first non-wildcard byte in the pattern.
    uint8_t              anchorVal = 0; ///< Value of the anchor byte.

    /**
     * @brief Constructs a parsed pattern from a hex string signature.
     * @param sig String view of the IDA-style signature (e.g., "E8 ? ? ? ? 48 8D 0D").
     */
    explicit ParsedPattern(std::string_view sig)
    {
        // A hex signature takes at most 2 chars per byte (plus spaces).
        // Pre-allocating half the string length avoids reallocations during parsing.
        bytes.reserve(sig.size() / 2);
        mask.reserve(sig.size() / 2);

        for (size_t i = 0; i < sig.size(); ++i)
        {
            // Skip formatting spaces
            if (sig[i] == ' ') continue;

            // Handle wildcards ('?' or '??')
            if (sig[i] == '?')
            {
                bytes.push_back(0x00);
                mask.push_back(0x00);

                // If double-question-mark format ("??"), consume the second character
                if (i + 1 < sig.size() && sig[i + 1] == '?') ++i;
                continue;
            }

            // Prevent out-of-bounds reading: a valid hex byte requires at least 2 characters remaining
            if (i + 1 >= sig.size()) break;

            // Parse two hex characters directly into a byte.
            // std::from_chars is zero-allocation, locale-independent, and significantly faster than strtol/stringstream.
            uint8_t val = 0;
            auto [ptr, ec] = std::from_chars(sig.data() + i, sig.data() + i + 2, val, 16);
            if (ec == std::errc())
            {
                bytes.push_back(val);
                mask.push_back(0xFF);
                ++i; // Advance past the second character of the hex pair
            }
        }

        if (bytes.empty()) return;

        // Locate the primary anchor byte.
        // Finding the first static (non-wildcard) byte allows pattern scanners to use hardware-accelerated
        // searches (like std::memchr / AVX2) to jump directly to candidate memory locations.
        for (size_t j = 0; j < bytes.size(); ++j)
        {
            if (mask[j] == 0xFF)
            {
                anchorPos = j;
                anchorVal = bytes[j];
                break;
            }
        }
    }
};

/* interesting bytes starting at bWSMode

example               01      00        ??   00  00        ??     00      ?? ??    01  01       01
label                 bwsmode holdingE       zerochecks           exitws           onechecks    something grabbed
Fallout4.exe+2E749??  94      95        96   97  98        99     9A      9B 9C    9D  9E       0x9F
bwsmode offset        +0      +1        +2   +3  +4        +5     +6      +7 +8    +9  +A       +B
*/
constexpr uintptr_t WSMODE_OFFSET_PLAYERINWSMODE = 0x0;
constexpr uintptr_t WSMODE_OFFSET_PLAYERGRABBINGOBJECT = 0xB;
constexpr UInt8     WSMODE_TRUE = 0x01;

// -------------------------------------------------------------------------------------
// Memory Scanning Pointers to pattern matches (filled at runtime)
// -------------------------------------------------------------------------------------
static uintptr_t* A = nullptr;
static uintptr_t* B = nullptr;
static uintptr_t* C = nullptr;
static uintptr_t* D = nullptr;
static uintptr_t* E = nullptr;
static uintptr_t* F = nullptr;
static uintptr_t* G = nullptr;
static uintptr_t* H = nullptr;
static uintptr_t* J = nullptr;
static uintptr_t* R = nullptr;
static uintptr_t* Y = nullptr;
static uintptr_t* CORRECT = nullptr;
static uintptr_t* wstimer = nullptr;
static uintptr_t* gsnap = nullptr;
static uintptr_t* osnap = nullptr;
static uintptr_t* outlines = nullptr;
static uintptr_t* achievements = nullptr;
static uintptr_t* survivalconsole = nullptr;


// Patch Bytes - Standard NOPs
static UInt8  NOP3[3] = { 0x0F, 0x1F, 0x00 };
//static UInt8  NOP4[4] = { 0x0F, 0x1F, 0x40, 0x00 };
//static UInt8  NOP5[5] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };
static UInt8  NOP6[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };
//static UInt8  NOP7[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };
//static UInt8  NOP8[8] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };

// Patch Bytes- Game Specific Patches
static UInt8  C_OLD[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };       // init NOP7 -> movzx eax, byte ptr [...]
static UInt8  C_NEW[7] = { 0x31, 0xC0, 0x0F, 0x1F, 0x44, 0x00, 0x00 };       // xor al,al; nop x5
static UInt8  CC_OLD[2] = { 0x75, 0x11 };                                     // JNE 0x11
static UInt8  CC_NEW[2] = { 0xEB, 0x1C };                                     // JMP 0x1C
static UInt8  D_OLD[7] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };       // init NOP7 -> movzx eax, byte ptr [...]
static UInt8  D_NEW[7] = { 0x31, 0xC0, 0xB0, 0x01, 0x90, 0x90, 0x90 };       // xor al,al; mov al,01; nop x3
static UInt8  F_OLD[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };             // init NOP6 -> mov [...], al
static UInt8  J_OLD[2] = { 0x74, 0x35 };                                     // JE 0x35
static UInt8  J_NEW[2] = { 0xEB, 0x30 };                                     // JMP 0x30
static UInt8  R_OLD[5] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };                   // init NOP5 -> call (relative)
static UInt8  Y_OLD[3] = { 0x8B, 0x58, 0x14 };                               // mov rbx, [rax+14]
static UInt8  WSTIMER_OLD[6] = { 0x0F, 0x85, 0xAB, 0x00, 0x00, 0x00 };             // JNE
static UInt8  WSTIMER_NEW[6] = { 0xE9, 0xAC, 0x00, 0x00, 0x00, 0x90 };             // JMP + NOP
static UInt8  ACHIEVE_OLD[4] = { 0x48, 0x83, 0xEC, 0x28 };                         // sub rsp, 28
static UInt8  ACHIEVE_NEW[3] = { 0x30, 0xC0, 0xC3 };                               // xor al, al; ret
static UInt8  OSNAP_OLD[8] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 }; // init NOP8 -> movss xmm, [...]
static UInt8  OSNAP_NEW[8] = { 0x0F, 0x57, 0xF6, 0x0F, 0x1F, 0x44, 0x00, 0x00 }; // xorps xmm6, xmm6; NOP5
static UInt8  DRAWS_OLD[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };             // init NOP6 -> add [...], ...
static UInt8  TRIS_OLD[6] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };             // init NOP6 -> add [...], ...
static UInt8  CNAMEREF_OLD[6] = { 0xFF, 0x90, 0xD0, 0x01, 0x00, 0x00 };            // call qword [rax+1D0]
static size_t CNAMEREF_OLD_SIZE = sizeof(CNAMEREF_OLD) / sizeof(CNAMEREF_OLD[0]);  // proper way to calculate size of array
static UInt32 CurrentWSRef_Offsets[4] = { 0x0, 0x0, 0x10, 0x110 }; // Offsets to get the current workshop reference from the CurrentWSRef pointer
static size_t CurrentWSRef_OffsetsSize = sizeof(CurrentWSRef_Offsets) / sizeof(CurrentWSRef_Offsets[0]); // proper way to calculate size of array
static UInt8  TWO_ZEROS[2] = { 0x00, 0x00 }; // written to bWSMode+0x03 when place in red is enabled only
static UInt8  TWO_ONES[2] = { 0x01, 0x01 }; // written to bWSMode+0x09 when place in red is enabled only

// patterns for scanning
static const ParsedPattern pat_SetMotionType{ "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 50 45 32 E4 41 8D 41 FF" };
static const ParsedPattern pat_pirR{ "89 05 ? ? ? ? C6 05 ? ? ? ? 01 48 83 C4 68 C3" };
static const ParsedPattern pat_Zoom{ "F3 0F 10 0D ? ? ? ? 0F 29 74 24 20 F3 0F 10 35" };
static const ParsedPattern pat_Rotate{ "F3 0F 10 05 ? ? ? ? ? ? ? ? ? ? ? ? 84 C9 75 07 0F 57 05" };
static const ParsedPattern pat_CurrentWSRef{ "48 8B 1D ? ? ? ? 4C 8D 24 C3 49 3B DC 0F 84 ? ? ? ? 66" };
static const ParsedPattern pat_achievements{ "48 83 EC 28 C6 44 24 ? 00 84 D2 74 1C 48" };
static const ParsedPattern pat_survivalconsole{ "E8 ? ? ? ? 83 F8 06 75 0D 80 3D ? ? ? ? 00 0F 84 8A 00 00 00 0F 2E" };
static const ParsedPattern pat_cnref_original{ "FF 90 D0 01 00 00 48 89 74 24 40 4C 8D 05 ? ? ? ? 4C" };
static const ParsedPattern pat_cnref_GetRefName{ "E8 ? ? ? ? 4C 8B 05 ? ? ? ? 48 8D 4C 24 40 4C 8B C8 BA 00 01 00 00 E8 ? ? ? ? 83" };
static const ParsedPattern pat_A{ "C6 05 ? ? ? ? 01 84 C0 75 A9 B1 02" };
static const ParsedPattern pat_B{ "B2 01 88 15 ? ? ? ? EB 04 84 D2 74 07" };
static const ParsedPattern pat_C{ "0F B6 05 ? ? ? ? 44 0F B6 A5 ? ? ? ? 84 C0 75" };
static const ParsedPattern pat_D{ "0F B6 05 ? ? ? ? 3A 05 ? ? ? ? 48 8B B4 24 ? ? ? ? C6 05" };
static const ParsedPattern pat_E{ "76 0C C6 05 ? ? ? ? 06 E9 ? ? ? ? 40 84 ED 0F 84" };
static const ParsedPattern pat_F{ "88 05 ? ? ? ? E8 ? ? ? ? 84 C0 75 ? 80 3D ? ? ? ? 02" };
static const ParsedPattern pat_G{ "0F 95 05 ? ? ? ? E8 ? ? ? ? 40 38 35 ? ? ? ? 48" };
static const ParsedPattern pat_H{ "74 11 40 84 F6 74 0C 48 8D ? ? 49 8B ? E8" };
static const ParsedPattern pat_J{ "74 35 48 8B B5 ? ? ? ? 48 8B CE E8 ? ? ? ? 84 C0 75" };
static const ParsedPattern pat_CORRECT{ "C6 05 ? ? ? ? 01 40 84 F6 74 09 80 3D ? ? ? ? 00 75 ? 80 3D" };
static const ParsedPattern pat_gsnap{ "0F 86 ? ? ? ? 41 8B 4E 34 49 B8" };
static const ParsedPattern pat_osnap{ "F3 0F 10 35 ? ? ? ? 0F 28 C6 48 ? ? ? ? ? ? 33 C0" };
static const ParsedPattern pat_outlines{ "C6 05 ? ? ? ? 01 88 15 ? ? ? ? 76 13 48 8B 05" };
static const ParsedPattern pat_wstimer{ "0F 85 AB 00 00 00 F3 0F 10 05 ? ? ? ? 41 0F 2E" };
static const ParsedPattern pat_Y{ "8B 58 14 48 8D 4C 24 30 8B D3 45 33 C0 E8" };
static const ParsedPattern pat_FirstConsole{ "48 8D 1D ? ? ? ? 48 8D 35 ? ? ? ? 66 90 48 8B 53 F8" };
static const ParsedPattern pat_FirstObScript{ "48 8D 1D ? ? ? ? 4C 8D 35 ? ? ? ? 0F 1F 40 00 0F 1F 84 00 00 00 00 00" };
static const ParsedPattern pat_ParseConsoleArg{ "4C 89 4C 24 20 48 89 4C 24 08 53 55 56 57 41 54 41 55 41 56 41 57" };
static const ParsedPattern pat_GetScale{ "66 89 BB 08 01 00 00 E8 ? ? ? ? 48 8B 0D ? ? ? ? 0F 28 F0 48" };
static const ParsedPattern pat_SetScale{ "E8 ? ? ? ? 40 84 F6 75 07 81 63 10 FF FF DF FF 33 ED" };
static const ParsedPattern pat_PlaySound_File{ "48 8B C4 48 89 58 08 57 48 81 EC 50 01 00 00 8B FA C7 40 18 FF FF FF FF 48" };
static const ParsedPattern pat_PlaySound_UI{ "48 89 5C 24 08 57 48 83 EC 50 48 8B D9 E8 ? ? ? ? 48 85 C0 74 6A" };
static const ParsedPattern pat_WSMode{ "80 3D ? ? ? ? 00 74 0E C6 07 02 48 8B 5C 24 30 48 83 C4 20 5F C3" };
static const ParsedPattern pat_WSSize{ "01 05 ? ? ? ? 8B 44 24 58 01 05 ? ? ? ? 85 D2 0F 84" };
static const ParsedPattern pat_gConsole{ "48 8D 05 ? ? ? ? 48 89 2D ? ? ? ? 48 89 05 ? ? ? ? 89 2D ? ? ? ? 40 88 2D ? ? ? ? 48" };
static const ParsedPattern pat_gDataHandler{ "48 83 3D ? ? ? ? 00 4D 8B F1 41 0F B6 F0 48 8B FA 48 8B D9 0F 84 ? ? ? ? 80 3D ? ? ? ? 00 48" };
static const ParsedPattern pat_WorkbenchSelection{ "0F B6 84 02 ? ? ? ? 8B 8C 82 ? ? ? ? 48 03 CA FF E1 B0 01 48 83 C4 20 5B C3" };
static const ParsedPattern pat_InvalidRefHandle{ "3B 05 ? ? ? ? 89 03 0F85 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 8B 57 28 33 C0 8B CA" };