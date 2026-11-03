#pragma once

namespace ONPCR::Papyrus
{
	// Registers the ONPCR_Settings natives (GetFloat / GetBool / GetInt / Reload / GetVersion) with the Papyrus VM.
	void Register();

	// Fires the "ONPCR_SettingsChanged" mod event on the main thread so ONpcMain re-reads the values.
	// a_key names the changed setting ("*" = everything, e.g. after a reset).
	void NotifyChanged(const char* a_key);

	// Couples page "Voice test": fires "ONPCR_VoiceTest" - ONpcMain pairs the two nearest NPCs and plays every voice moment
	void SendImportOthers(int a_minScenes);
	void SendVoiceTest();
	void SendFertilityCheck(const std::vector<RE::FormID>& a_refs);  // ask Papyrus what the pregnancy mod says about these people

	// 0.22.0: do the two live in the same town (editor locations walked up to their town)? Couples page household note
	bool SameSettlement(RE::Actor* a_x, RE::Actor* a_y);
}
