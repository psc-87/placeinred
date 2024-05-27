#pragma once
#include "f4se.h"

extern IDebugLog pluginLog;

namespace papyrusPlaceInRed
{
	void TestFunction1(StaticFunctionTag* base);
	bool RegisterFuncs(VirtualMachine* vm);
}