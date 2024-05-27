#include "papyrus.h"


namespace papyrusPlaceInRed
{ 
	void papyrusPlaceInRed::TestFunction1(StaticFunctionTag* base)
	{
		pluginLog.FormattedMessage("[papyrusPlaceInRed::TestFunction1] it works!");
		return;
	}
}


bool papyrusPlaceInRed::RegisterFuncs(VirtualMachine* vm)
{
	
	vm->RegisterFunction(
		new NativeFunction0 <StaticFunctionTag, void>("TestFunction1", "PlaceInRed", papyrusPlaceInRed::TestFunction1, vm));

	vm->SetFunctionFlags("PlaceInRed", "TestFunction1", IFunction::kFunctionFlag_NoWait);

	pluginLog.FormattedMessage("[papyrusPlaceInRed::RegisterFuncs] registered papyrus functions.");
	return true;
}

