#pragma once

namespace ONPCR::Liita
{
	// Love Is In The Air (Truman, Nexus 155836) runs its own NPC weddings. Runtime-only compatibility (0.13.3) - its files are never
	// changed or shipped; every call does nothing when LIITA.esp is not loaded.
	//   - its DLL marries NPCs from PotentialMarriageFaction that are not in its form list LIITA_ExcludedNpcs: Sync() keeps our
	//     committed NPCs (dating / engaged / married - refs and bases) in that list and takes them out again when they split up
	//   - NPCs it married (faction LIITA_Faction) are taken: out of our pool and new pairings (IsMarried); a pair of ours that one
	//     of them was still in ends quietly in Sync() (it happened before this fix, or while the list was not in step yet)
	//   - one wedding at a time at the Temple of Mara with Maramal (global LIITA_GlobalWIP): WeddingBusy() puts ours off
	[[nodiscard]] bool Present();
	[[nodiscard]] bool IsMarried(RE::Actor* a_actor);
	[[nodiscard]] bool WeddingBusy();
	void               Sync();  // Papyrus thread (called from SyncStageFaction every ~10 s); never holds the ladder lock while calling it

	// the forms WE put into LIITA_ExcludedNpcs (co-save record LADL, written by the ladder)
	[[nodiscard]] std::vector<RE::FormID> Added();
	void                                  SetAdded(std::vector<RE::FormID> a_ids);
}
