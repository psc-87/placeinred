#include "papyrus.h"


namespace papyrusPIR
{ 
	const char* logprefix = { "papyrusPIR" };

	void funky1(StaticFunctionTag* s)
	{
		return;
	}
}


bool papyrusPIR::RegisterFuncs(VirtualMachine* vm)
{
	SET_CURRENT_FUNCTION_STRING

	if(vm){

		vm->RegisterFunction(new NativeFunction0<StaticFunctionTag, void>("funky1", "PlaceInRed", papyrusPIR::funky1, vm));
		vm->SetFunctionFlags("PlaceInRed", "funky1", IFunction::kFunctionFlag_NoWait);

		pirlog.FormattedMessage("[%S::%S] registered papyrus functions.");
		return true;
	}

	pirlog.FormattedMessage("[%S::%S] no vm!", logprefix, thisfunc);
	return false;
}

