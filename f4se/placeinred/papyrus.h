#pragma once
#include "f4se.h"
#include "main.h"

namespace papyrusPlaceInRed
{
	void TestFunction1(StaticFunctionTag* base);
	void TestFunction2(StaticFunctionTag* base);
	bool RegisterFuncs(VirtualMachine* vm);
	
}