#include "papyrus.h"

RelocAddr <_SetMotionType_Native> SetMotionType_Native(0x010D7B50);


namespace papyrusPlaceInRed
{ 
	void TestFunction1(VirtualMachine* vm, UInt32 stackId, TESObjectREFR* refr, SInt32 motiontype, bool akAllowActivate)
	{
		pirlog.FormattedMessage("[papyrusPlaceInRed::TestStaticFunction1] called.");
		return;
	}
}


bool papyrusPlaceInRed::RegisterFuncs(VirtualMachine* vm)
{
	if(vm){
		pirlog.FormattedMessage("[papyrusPlaceInRed::RegisterFuncs] registered papyrus functions.");
		return true;
	}
	pirlog.FormattedMessage("[papyrusPlaceInRed::RegisterFuncs] no vm!");
	return false;
}

