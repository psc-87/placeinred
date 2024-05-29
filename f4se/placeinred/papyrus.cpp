#include "papyrus.h"

namespace papyrusPlaceInRed
{ 
	void TestFunction1(StaticFunctionTag* base)
	{
		pluginLog.FormattedMessage("[papyrusPlaceInRed::TestFunction1] called.");
		return;
	}
	void TestFunction2(StaticFunctionTag* base)
	{
		pluginLog.FormattedMessage("[papyrusPlaceInRed::TestFunction2] called.");
		return;
	}
}


bool papyrusPlaceInRed::RegisterFuncs(VirtualMachine* vm)
{
	if(vm){

		// register the functions
		vm->RegisterFunction( new NativeFunction0 <StaticFunctionTag, void> ("TestFunction1", "PlaceInRed", papyrusPlaceInRed::TestFunction1, vm) );
		vm->RegisterFunction( new NativeFunction0 <StaticFunctionTag, void> ("TestFunction2", "PlaceInRed", papyrusPlaceInRed::TestFunction2, vm) );

		// set function flags
		vm->SetFunctionFlags("PlaceInRed", "TestFunction1", IFunction::kFunctionFlag_NoWait);
		vm->SetFunctionFlags("PlaceInRed", "TestFunction2", IFunction::kFunctionFlag_NoWait);


		pluginLog.FormattedMessage("[papyrusPlaceInRed::RegisterFuncs] registered papyrus functions.");
		return true;
	}
	pluginLog.FormattedMessage("[papyrusPlaceInRed::RegisterFuncs] failed (no vm)! Plugin load will fail.");
	return false;
}

