#pragma once

namespace ONPCR::Amulet
{
	// Amulet of Mara handling (phase 2 slice 3). Config: Data\SKSE\Plugins\OStimNPCsReforged_Amulets.json
	// { "amulets": ["Skyrim.esm|0xC891B", ...], "giverSpells": ["NPCs_Wear_Amulets_Of_Mara.esp|0x808"],
	//   "ignoreFactions": ["Skyrim.esm|0xA2526"], "ring": "" }
	void LoadConfig();  // after kDataLoaded

	// is this NPC wearing an Amulet of Mara (and not clergy)?
	[[nodiscard]] bool IsLooking(RE::Actor* a_actor);

	// NPC-NPC marriage: amulet off, giver spells off, out of PotentialMarriageFaction; divorce/widowed: back in the faction.
	// Both run on the game thread through the task interface; actors are resolved from their reference IDs.
	void OnMarried(RE::FormID a_refA, RE::FormID a_refB);
	void OnUnmarried(RE::FormID a_refA, RE::FormID a_refB);

	// re-assert the married state for every married ladder pair (SPID may re-add the giver spell on load)
	// married NPC loaded near the player without the ring worn: put it on again (game thread)
	void EnsureMarried(RE::FormID a_ref, bool a_gameCouple = false);

	// 0.17.0: marriages taken over from the game: amulets and giver spells off, out of PotentialMarriageFaction, and a ring only when none is worn
	void OnMarriedGame(RE::FormID a_refA, RE::FormID a_refB);
	void ReassertAllGame(const std::vector<std::pair<RE::FormID, RE::FormID>>& a_marriedRefs);

	void ReassertAll(const std::vector<std::pair<RE::FormID, RE::FormID>>& a_marriedRefs);

	// own distribution (only when no amulet-giver mod is configured/loaded)
	[[nodiscard]] bool GiverModPresent();
	[[nodiscard]] bool CanWearOwnAmulet(RE::Actor* a_actor);   // neck slot free or a non-unique necklace, not clergy
	void GiveAmulet(RE::FormID a_ref);                          // game thread; remembered so it can be taken back
	void TakeBackAmulet(RE::FormID a_ref);                      // only if this mod gave it
	[[nodiscard]] bool GaveAmulet(RE::FormID a_ref);
	[[nodiscard]] std::vector<RE::FormID> GivenRefs();          // for the co-save
	void SetGivenRefs(std::vector<RE::FormID> a_refs);
}