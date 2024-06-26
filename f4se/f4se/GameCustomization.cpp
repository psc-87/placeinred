#include "f4se/GameCustomization.h"
#include "f4se/GameAPI.h"
#include "f4se/GameObjects.h"

// 
RelocPtr <CharacterCreation*> g_characterCreation(0x02FF8180);	// array
// 
RelocPtr <UInt32> g_characterIndex(0x02FF8158);

// 
RelocPtr <BSFaceGenManager*> g_faceGenManager(0x02E663F8);

// 
RelocPtr <TESNPC*> g_customizationDummy1(0x02F6ED18);	// Either Nora or Nate's dummy actors
// 
RelocPtr <TESNPC*> g_customizationDummy2(0x02F6ECE0);	// Either Nora or Nate's dummy actors

RelocAddr<_CreateCharacterTintEntry> CreateCharacterTintEntry(0x003AB9C0);
RelocAddr<_CopyCharacterTints> CopyCharacterTints(0x003AA670);
RelocAddr<_ClearCharacterTints> ClearCharacterTints(0x003AEF90);

RelocAddr <_FillTintTemplates> FillTintTemplates(0x003AA820); // For manipulating the tint lists, their signatures aren't quite right yet
RelocAddr <_MergeTintTextures> MergeTintTextures(0x006985F0);
RelocAddr <_CreateMergeTintTextures> CreateMergeTintTextures(0x006983F0);

// NG: seems to be handled differently? (see 1405FD2E0)
// RelocPtr <tHashSet<CharacterCreation::MorphIntensity, TESNPC*>> g_morphIntensityMap(0x03715370 - 0x08);

// These are for creating new instances

// ??_7Mask@Template@BGSCharacterTint@@6B@
RelocAddr <uintptr_t> s_BGSCharacterTint_Template_MaskVtbl(0x02294398);
// ??_7Palette@Template@BGSCharacterTint@@6B@
RelocAddr <uintptr_t> s_BGSCharacterTint_Template_PaletteVtbl(0x022943C8);
// ??_7TextureSet@Template@BGSCharacterTint@@6B@
RelocAddr <uintptr_t> s_BGSCharacterTint_Template_TextureSetVtbl(0x022943F8);

bool BGSCharacterTint::Entry::IsEqual(Entry * rhs)
{
	return GetType() == rhs->GetType() && tintIndex == rhs->tintIndex && percent == rhs->percent;
}

bool BGSCharacterTint::PaletteEntry::IsEqual(Entry * rhs)
{
	return __super::IsEqual(rhs) && color.bgra == ((PaletteEntry*)rhs)->color.bgra && colorID == ((PaletteEntry*)rhs)->colorID;
}

void BGSCharacterTint::Entry::Copy(Entry * rhs)
{
	if(GetType() == rhs->GetType())
	{
		percent = rhs->percent;
	}
}

void BGSCharacterTint::PaletteEntry::Copy(Entry * rhs)
{
	PaletteEntry * pRHS = (PaletteEntry*)rhs;
	if(GetType() == pRHS->GetType())
	{
		percent = pRHS->percent;
		color = pRHS->color;
		colorID = pRHS->colorID;
	}
}

BGSCharacterTint::Template::Entry * BGSCharacterTint::Template::Entry::Create(UInt32 size, UInt64 vtbl)
{
	void* memory = Heap_Allocate(size);
	memset(memory, 0, size);
	((UInt64*)memory)[0] = vtbl;
	BGSCharacterTint::Template::Entry* xData = (BGSCharacterTint::Template::Entry*)memory;
	return xData;
}

BGSCharacterTint::Template::Mask * BGSCharacterTint::Template::Mask::Create()
{
	return (BGSCharacterTint::Template::Mask *)BGSCharacterTint::Template::Entry::Create(sizeof(BGSCharacterTint::Template::Mask), s_BGSCharacterTint_Template_MaskVtbl.GetUIntPtr());
}

BGSCharacterTint::Template::Palette * BGSCharacterTint::Template::Palette::Create()
{
	return (BGSCharacterTint::Template::Palette *)BGSCharacterTint::Template::Entry::Create(sizeof(BGSCharacterTint::Template::Palette), s_BGSCharacterTint_Template_PaletteVtbl.GetUIntPtr());
}

BGSCharacterTint::Template::TextureSet * BGSCharacterTint::Template::TextureSet::Create()
{
	return (BGSCharacterTint::Template::TextureSet *)BGSCharacterTint::Template::Entry::Create(sizeof(BGSCharacterTint::Template::TextureSet), s_BGSCharacterTint_Template_TextureSetVtbl.GetUIntPtr());
}

BGSCharacterTint::Template::Palette::ColorData * BGSCharacterTint::Template::Palette::GetColorDataByID(UInt16 colorID)
{
	for(UInt32 i = 0; i < colors.count; i++) {
		if(colors[i].colorID == colorID) {
			return &colors[i];
		}
	}

	return nullptr;
}

BGSCharacterTint::Template::Entry * CharacterCreation::CharGenData::GetTemplateBySlot(UInt32 slotType)
{
	if(!tintData)
		return nullptr;

	for(UInt32 i = 0; i < tintData->count; i++)
	{
		CharacterCreation::TintData * data;
		tintData->GetNthItem(i, data);

		BGSCharacterTint::Template::Entry* entry = data->GetTemplateBySlot(slotType);
		if(entry)
			return entry;
	}

	return nullptr;
}

BGSCharacterTint::Template::Entry * CharacterCreation::CharGenData::GetTemplateByIndex(UInt16 index)
{
	if(!tintData)
		return nullptr;

	for(UInt32 i = 0; i < tintData->count; i++)
	{
		CharacterCreation::TintData * data;
		tintData->GetNthItem(i, data);

		BGSCharacterTint::Template::Entry* entry = data->GetTemplateByIndex(index);
		if(entry)
			return entry;
	}

	return nullptr;
}

BGSCharacterTint::Template::Entry * CharacterCreation::TintData::GetTemplateByIndex(UInt16 index)
{
	for(UInt32 k = 0; k < entry.count; k++)
	{
		BGSCharacterTint::Template::Entry* templateEntry;
		entry.GetNthItem(k, templateEntry);

		if(templateEntry->templateIndex == index)
			return templateEntry;
	}

	return nullptr;
}

BGSCharacterTint::Template::Entry * CharacterCreation::TintData::GetTemplateBySlot(UInt32 slotType)
{
	for(UInt32 k = 0; k < entry.count; k++)
	{
		BGSCharacterTint::Template::Entry* templateEntry;
		entry.GetNthItem(k, templateEntry);

		if(templateEntry->slot == slotType)
			return templateEntry;
	}

	return nullptr;
}

void CharacterCreation::MorphIntensity::Dump(void)
{
	_MESSAGE("\t\tFormID: %08X", npc->formID);
	_MESSAGE("\t\tintensity: %f", morphIntensity);
}
