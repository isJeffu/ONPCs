#pragma once

namespace ONPCR::Simulation
{
	// Off-screen simulation (phase 2 slice 2): once per missed in-game day, every eligible unique NPC may meet one
	// partner from their community (editor location) or hold; successes feed the ladder and a queue of Lover's Ledger
	// writes that Papyrus flushes. Also applies drift and marks widowed pairs.
	void Tick();  // called from the scan loop (Papyrus ONPCR_Ladder.Tick)
	[[nodiscard]] std::string RunDayNow();  // testing: one extra day now, with a summary line

	[[nodiscard]] std::size_t  PoolSize();
	[[nodiscard]] std::int32_t LastSimulatedDay();
	void                       EnsurePool();  // build the NPC pool if this session has none yet (post-load)
	void                       RequestRebuild();  // rebuild the pool at the next EnsurePool / Tick (a follower was allowed or removed)
	[[nodiscard]] bool         IsSceneExcluded(RE::Actor* a_actor);  // 0.22.2: generic-named (PoolExclusions.json names / startsWith / contains) or a guard - no scenes near the player either
	void                       LoadExclusions();  // read OStimNPCsReforged_PoolExclusions.json (kDataLoaded)
	// actor refs of pool NPCs whose M.A.R.A.S archetype is still unknown (marks them requested)
	[[nodiscard]] std::vector<RE::FormID> ArchetypeRequests(std::size_t a_max);
	// pool NPCs whose name contains a_needle (case-insensitive): base, ref, display name - for the Create a couple picker
	struct Person
	{
		RE::FormID  base;
		RE::FormID  ref;
		std::string name;
	};
	[[nodiscard]] std::vector<Person> FindPeople(const std::string& a_needle, std::size_t a_max);
}