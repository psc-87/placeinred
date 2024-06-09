#pragma once
#include "f4se.h"
#include "main.h"

struct StaticFunctionTag;
class VirtualMachine;

namespace papyrusPIR
{
	bool RegisterFuncs(VirtualMachine* vm);
}