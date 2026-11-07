#pragma once

namespace ONPCR::Base
{
	// 0.26.0 base port, phase 1 (Notes and Reports\Reforged base - DLL port plan 2026-11-03.md): the NPC pair search moves from
	// Papyrus (ONpcMain.Scan) into the DLL. Papyrus asks ONPCR_Base.FindPairs for known couples near the player - spouses and
	// courting couples from the game's live relationship records, pairs with a past scene in Lover's Ledger - never strangers,
	// never family; it then runs its own per-actor checks, the furniture search and the scene as before.
	void Register();  // ONPCR_Base natives
	void OnDataLoaded();  // 0.26.0 phase 2: connect to OStim's thread API (scene start / end events)
	void Reset();       // a save is about to load: forget the caches of the last one
	void OnPostLoad();  // a save finished loading: map unique NPCs to their references (main thread)

	// 0.1.3: the breaks (each NPC's, each couple's, last fulfilling scene) in ONPCs' co-save. Called from Ladder's co-save
	// callbacks: Save BEFORE Ladder takes its own lock; LoadRecord = true when the record was the base's (read or skipped)
	void Save(SKSE::SerializationInterface* a_intfc);
	bool LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version);
}
