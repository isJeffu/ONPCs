#pragma once

// 0.24.0 (handoff 21) baby stages: carried -> away -> child. Papyrus (ONPCR_BeeingFemaleBridge) owns the stages and the save data
// (StorageUtil); the DLL only holds the "where is the child" reasons from OStimNPCsReforged_ChildAway.json and the Couples page notes
// Papyrus pushes once a game hour (not saved - the next hourly pass rebuilds them).
namespace ONPCR::Babies
{
	void LoadReasons();  // kDataLoaded

	[[nodiscard]] int         ReasonCount();
	[[nodiscard]] std::string ReasonPlace(int a_index);  // "" when out of range
	[[nodiscard]] int         PickReason(RE::Actor* a_mother, RE::Actor* a_father);  // random among the places that fit the parents' race and home; -1 without reasons
	// 0.25.0: the same with each parent's M.A.R.A.S social class (-1 = unknown / no M.A.R.A.S - places with a "class" filter never fit)
	[[nodiscard]] int         PickReason(RE::Actor* a_mother, RE::Actor* a_father, int a_classMother, int a_classFather);
	// 0.25.1: saves keep a place by its id, so a reordered or once-missing JSON can't move an away child to another place
	[[nodiscard]] std::string ReasonId(int a_index);                  // "" when out of range
	[[nodiscard]] std::string PlaceById(const std::string& a_id);     // "" when the JSON has no such place
	[[nodiscard]] int         DialogueRank(const std::string& a_id);  // ONPCR_ChildAway rank the answers expect for this id (slot + 1), 0 = no line

	void                      ClearNotes();
	void                      AddNote(RE::FormID a_baseA, RE::FormID a_baseB, const std::string& a_text);
	[[nodiscard]] std::string Note(RE::FormID a_baseA, RE::FormID a_baseB);  // "baby Ada (carried); Ada away: ..." or ""
}
