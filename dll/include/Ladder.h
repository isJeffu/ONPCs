#pragma once

namespace ONPCR::Ladder
{
	// NPC-to-NPC relationship ladder (phase 2). Pairs are keyed on the two NPCs' BASE form IDs
	// (stable across cell loads, like Lover's Ledger) and saved in the SKSE co-save.
	enum class Stage : std::uint8_t
	{
		None = 0,       // no relationship (or it ended quietly)
		Casual = 1,     // met at least once, not exclusive
		Candidate = 2,  // courting - exclusive from here on
		Engaged = 3,
		Married = 4,
		Jilted = 5,     // engaged pair broken by straying
		Divorced = 6,   // married pair broken by straying / a long cold spell
		Widowed = 7,    // partner died
		// 0.19.0 side relationships: never "committed", never exclusive, never shown as a couple in news or letters
		Affair = 8,     // secret pair, at least one of them taken elsewhere; every meeting is cheating on the main partner
		Crush = 9,      // one-sided: the active side (Pair.activeA) wants the other
		Thrall = 10     // a vampire (the active side) and an enthralled victim - not love, never progresses
	};

	[[nodiscard]] constexpr bool IsSideStage(Stage a_s) noexcept { return a_s == Stage::Affair || a_s == Stage::Crush || a_s == Stage::Thrall; }
	[[nodiscard]] constexpr bool IsEndedStage(Stage a_s) noexcept { return a_s == Stage::None || a_s == Stage::Jilted || a_s == Stage::Divorced || a_s == Stage::Widowed; }

	// 0.19.0 how a PERSON does love: Normal; FreeSpirit = several lovers at once, never past seeing each other, never "taken" (a
	// married lover of theirs still cheats on their own spouse); Never = no love life at all (the Greybeards)
	enum class Style : std::uint8_t
	{
		Normal = 0,
		FreeSpirit = 1,
		Never = 2
	};

	enum class VanillaRelation : std::uint8_t
	{
		None = 0,
		Courting = 1,  // game-record courting pair: joins at Candidate, protected (never below Candidate)
		Married = 2    // game-record married pair: left alone entirely
	};

	struct Pair
	{
		RE::FormID    a{ 0 };  // lower base form ID
		RE::FormID    b{ 0 };  // higher base form ID
		Stage         stage{ Stage::None };
		std::uint32_t realEncounters{ 0 };
		std::uint32_t offscreenEncounters{ 0 };
		float         closeness{ 0.0f };  // 0..100
		float         firstDay{ 0.0f };   // game days when the pair was created
		float         lastDay{ 0.0f };    // last encounter
		float         stageDay{ 0.0f };   // when the current stage began
		std::uint8_t  straysByA{ 0 };     // strays A committed against this pair
		std::uint8_t  straysByB{ 0 };
		bool          createdByMod{ true };
		bool          protectedVanilla{ false };
		bool          cold{ false };      // married pair whose closeness ran out
		float         coldSinceDay{ 0.0f };
		std::uint8_t  children{ 0 };      // children born to this pair (pregnancy engine feedback)
		RE::FormID    home{ 0 };          // married couples: the location (editor location of one partner) they share; 0 = none
		float         weddingDay{ 0.0f };  // RealWeddings: game day + hour of the ceremony (noon; ceremony window to 17:00); valid only while engaged and later than stageDay
		bool          weddingNotice{ false };  // the invitation (news 7) went out - two days before the wedding
		std::uint8_t  altarRoll{ 0 };          // 0.14.0: 0 not rolled yet, 1 both come, 2 A stays away, 3 B stays away (stood up at the altar)
		bool          invitedPlayer{ false };  // 0.14.0: the player got an invitation letter - the wedding waits for them (up to 3 more days)
		std::uint8_t  waitedDays{ 0 };         // days the wedding was put off waiting for the invited player
		bool          imported{ false };       // 0.17.0: taken over from the game's own relationship records (not formed by this mod)
		// 0.19.0 couples redesign (co-save v8)
		bool          frozen{ false };         // quest-locked: never steps up or down, never cools, no strikes, no breakup - until its quest releases it
		bool          secret{ false };         // no news or letters until it comes out (an engagement, gossip after a while, a quest)
		bool          familyBlock{ false };    // may date, may not get engaged until the family block lifts
		bool          exposed{ false };        // Affair: it has come out
		bool          activeA{ true };         // Crush / Thrall: true = a is the one who pines / the vampire
		bool          byPlayer{ false };       // the player set or changed this pair on the Couples page - the couple table never overrides it
		std::uint8_t  quest{ 0 };              // game-couple table: which quest owns / releases it (0 none)
		std::uint8_t  endedFrom{ 0 };          // a pair that ended: the stage it ended from (heartbreak length)
		std::uint8_t  endedBy{ 0 };            // who ended it: 0 neither (drifted), 1 a, 2 b - that one gets over it faster

		[[nodiscard]] bool          IsCommitted() const noexcept { return stage == Stage::Candidate || stage == Stage::Engaged || stage == Stage::Married; }
		[[nodiscard]] std::uint32_t Encounters() const noexcept { return realEncounters + offscreenEncounters; }
	};

	// one simulated encounter that Papyrus still has to write into Lover's Ledger
	struct LedgerWrite
	{
		RE::FormID refA{ 0 };  // ACTOR reference IDs (persistent for unique NPCs)
		RE::FormID refB{ 0 };
		std::int32_t day{ 0 };
	};

	[[nodiscard]] const char* StageName(Stage a_stage);
	[[nodiscard]] std::string StageTitle(Stage a_stage);  // "Candidate" - capitalised for messages and the menu
	[[nodiscard]] std::string PairTitle(const Pair& a_pair);  // 0.19.3: StageTitle, but an ended pair says how it ended ("Broke up - X ended it")
	[[nodiscard]] std::string StagePhrase(const std::string& a_a, const std::string& a_b, Stage a_stage);  // "A and B are now dating"
	// on-screen "[Reforged] ..." line when the DebugNotifications setting is on (main-thread task; safe from any thread)
	void DebugNotify(std::string a_msg);

	// News page: one line per ladder event (stage changes, strays, drift, deaths, pregnancies), last 50 kept in the co-save
	struct ChronicleEntry
	{
		float       day{ 0.0f };  // game days passed when it happened
		std::string text;
	};
	void Chronicle(std::string a_text, float a_day = -1.0f);  // a_day = the (simulated) game day, -1 = now; records the event and passes it to DebugNotify; safe while the ladder mutex is held
	[[nodiscard]] std::vector<ChronicleEntry> ChronicleSnapshot();  // newest first

	void Init();  // registers the co-save callbacks; call once from SKSEPluginLoad

	// A scene between two NPCs. Real scenes come from Papyrus (actors); the simulation calls the base-ID form.
	bool RecordEncounter(RE::Actor* a_x, RE::Actor* a_y, bool a_offscreen, VanillaRelation a_vanilla);
	void AllowPlayerChosen(RE::Actor* a_x, RE::Actor* a_y);  // 0.20.3: the player paired them (match-maker) - the same-voice block does not apply
	bool RecordEncounterByBase(RE::FormID a_x, RE::FormID a_y, bool a_offscreen, VanillaRelation a_vanilla, RE::FormID a_community, bool a_eager = false);

	// Called from the scan loop: catches up missed days (simulation + drift) and promotes engaged -> married.
	void Tick();

	// drift and day-based promotions for one simulated day (the simulation calls this once per day)
	void AdvanceDay(float a_day, const std::function<bool(RE::FormID)>& a_driftPaused);

	[[nodiscard]] Stage       GetStage(RE::Actor* a_x, RE::Actor* a_y);
	[[nodiscard]] bool        IsCommittedTo(RE::Actor* a_x, RE::Actor* a_y);
	[[nodiscard]] bool        IsCommittedToOther(RE::Actor* a_x, RE::Actor* a_y);
	[[nodiscard]] Stage       GetPartnerStage(RE::Actor* a_x);
	[[nodiscard]] RE::TESNPC* GetPartnerBase(RE::Actor* a_x);

	// base-ID queries for the simulation
	[[nodiscard]] Pair*       CommittedPairOfUnlocked(RE::FormID a_npc);  // caller holds Mutex()
	[[nodiscard]] std::mutex& Mutex();
	[[nodiscard]] std::uint32_t EncountersBetween(RE::FormID a_x, RE::FormID a_y);
	[[nodiscard]] Stage       StageBetween(RE::FormID a_x, RE::FormID a_y);
	[[nodiscard]] RE::FormID  CommittedPartnerOf(RE::FormID a_npc, Stage* a_stageOut = nullptr, std::uint8_t* a_strikesOut = nullptr, bool* a_coldOut = nullptr);
	void                      MarkWidowed(RE::FormID a_deadNpc, float a_day);
	// pregnancy engine feedback: expecting pauses drift; a child makes the couple much harder to split
	void                      SetPregnant(RE::FormID a_woman, bool a_pregnant);
	[[nodiscard]] bool        IsPregnant(RE::FormID a_npc);
	void                      AddChild(RE::FormID a_mother, RE::FormID a_father);
	[[nodiscard]] bool        IsMourning(RE::FormID a_npc, float a_now);  // widowed recently (MourningDays x pace x temperament)
	// 0.19.0: getting over a breakup with someone still alive (Heartbreak setting); free spirits never are
	[[nodiscard]] bool        IsHeartbroken(RE::FormID a_npc, float a_now);

	// ---- 0.19.0 couples redesign ----
	[[nodiscard]] Style GetStyle(RE::FormID a_base);  // the player's choice, else the built-in list, else Normal
	void                SetStyle(RE::FormID a_base, Style a_style);  // the player's choice (Couples page); saved
	void                MarkByPlayer(RE::FormID a_a, RE::FormID a_b);  // the player touched this pair - the couple table leaves it alone
	// the built-in game-couple table (stages, quest locks, affairs, crushes, free-spirit lovers): applied on import, and once on an
	// older save (only to pairs the player has not touched). Returns how many pairs it changed or added.
	std::size_t         ApplyCoupleTable();
	void                LoadProtectedCouples();   // 0.21.0: read OStimNPCsReforged_ProtectedCouples.json (kDataLoaded)
	void                LoadPlayerPartnerFactions();  // 0.21.2: OStimNPCsReforged_Couples.json "playerPartnerFactions" (kDataLoaded)
	bool                InPlayerPartnerFaction(RE::Actor* a_actor);  // 0.21.2: a follower mod's own romance faction marks the player's partner
	std::size_t         ApplyProtectedCouples();  // 0.21.0: listed game couples -> frozen + protected (not byPlayer pairs)
	std::string         ProtectedWhy(RE::FormID a_a, RE::FormID a_b);  // "" when not listed
	void                SetKeepTogether(RE::FormID a_a, RE::FormID a_b, bool a_keep);  // Couples page tick
	[[nodiscard]] bool  CoupleTableDue();  // this save has not had the current table yet
	// once per game hour from the scan: quests that release or move a locked couple (A Lovely Letter, The Book of Love ...)
	void                WatchQuests();
	void                WatchRelationships();  // 0.20.0: a mod that changes two NPCs' relationship rank changes their couple (every 6 game hours)
	// kDataLoaded: their own romance lines (vanilla / mod dialogue) only play while the two are still together
	void                InstallRomanceConditions();

	bool SetStage(RE::Actor* a_x, RE::Actor* a_y, Stage a_stage);  // debug / force
	bool SetStage(RE::FormID a_a, RE::FormID a_b, Stage a_stage);
	bool EndPair(RE::FormID a_a, RE::FormID a_b);
	bool EndForOtherMarriage(RE::FormID a_a, RE::FormID a_b);
	// 0.14.0 real weddings: who stays away from the wedding (base ID, 0 = both come / not decided yet); the player was invited by letter;
	// someone objected during the ceremony (new date, closeness down)
	[[nodiscard]] RE::FormID AltarRunner(RE::FormID a_a, RE::FormID a_b);
	bool                     SetWeddingInvite(RE::FormID a_a, RE::FormID a_b);
	bool                     CallOffWedding(RE::FormID a_a, RE::FormID a_b);  // 0.13.3: one of them married someone else (Love Is In The Air) - casual / dating ends, engaged = jilted
	bool PostponeWedding(RE::FormID a_a, RE::FormID a_b);  // new date (next free day) - a partner was busy with a quest
	std::size_t StartFresh();  // forget every couple and all news in this save (Advanced page, confirmed); returns how many pairs
	// Create a couple (F3 picker): any stage, full closeness so it does not drift apart at once; a_keep = protected (never drifts
	// or breaks up, like a game-record couple). Goes through SetStage, so promotions run news / letters / ring as usual.
	bool CreateCouple(RE::FormID a_a, RE::FormID a_b, Stage a_stage, bool a_keep);
	void RememberRef(RE::FormID a_base, RE::FormID a_ref);  // a picked NPC outside the simulation pool
	// Clears out pairs that are over: stage none (drifted apart / ended quietly) with nothing for a_minIdleDays.
	// a_includeEnded also takes jilted and divorced pairs, and widowed ones once even the longest mourning is over.
	// Casual, candidate, engaged, married and protected game couples are never touched. a_dryRun only counts.
	std::size_t ForgetStale(float a_minIdleDays, bool a_includeEnded, bool a_dryRun = false);

	// simulated encounters waiting to be written to Lover's Ledger by Papyrus
	void                     QueueLedgerWrite(RE::FormID a_refA, RE::FormID a_refB, std::int32_t a_day);
	std::vector<LedgerWrite> PopLedgerWrites();

	// ---- archetypes (M.A.R.A.S, optional; -1 = unknown / neutral, -2 = requested) ----
	struct Archetype
	{
		std::int8_t socialClass{ -1 };  // Outcast 0, Poverty, Working, Middle, Wealthy, Religious, Nobles, Rulers 7
		std::int8_t skillType{ -1 };    // Warrior 0, Mage, Rogue, Craftsman, Ranger, Orator 5
		std::int8_t temperament{ -1 };  // Proud 0, Humble, Jealous, Romantic, Independent 4
	};
	void                    SetArchetype(RE::FormID a_base, int a_class, int a_skill, int a_temperament);
	[[nodiscard]] Archetype GetArchetype(RE::FormID a_base);
	[[nodiscard]] bool      ArchetypeKnownOrRequested(RE::FormID a_base);
	void                    MarkArchetypeRequested(RE::FormID a_base);

	// ---- news for the player (courier letters) ----
	struct News
	{
		std::int32_t type;  // 1 engaged, 2 married, 3 breakup, 4 started courting, 5 widowed (a = survivor, b = the dead), 6 stray (a strayed on b), 7 wedding set (in two days, Temple of Mara)
		RE::FormID   a;     // base IDs
		RE::FormID   b;
	};
	std::vector<News> PopNews();

	// the simulation knows each base NPC's reference; the ladder keeps its own copy (base -> ref) so a lookup never needs the
	// simulation's pool lock - the simulation calls into the ladder while holding that lock
	void SetPoolRefs(std::unordered_map<RE::FormID, RE::FormID> a_baseToRef);
	// actor reference for a base NPC: the simulation pool first, then NPCs seen in real scenes this session; 0 = unknown
	[[nodiscard]] RE::FormID RefOfBase(RE::FormID a_base);
	[[nodiscard]] std::vector<std::pair<RE::FormID, RE::FormID>> MarriedPairRefs();
	[[nodiscard]] std::vector<std::pair<RE::FormID, RE::FormID>> CommittedPairRefs();  // candidate / engaged / married, as actor refs
	[[nodiscard]] std::vector<std::pair<RE::FormID, RE::FormID>> GameMarriedPairRefs();  // 0.17.0: married pairs taken over from the game (their own rings stay)
	[[nodiscard]] bool IsGameMarried(RE::FormID a_base);  // this NPC is in a married pair taken over from the game

	// ---- 0.17.0 import of the game's couples ----
	struct ImportResult
	{
		bool          ran{ false };  // false = couples are switched off, nothing happened
		std::uint32_t married{ 0 };  // game Spouse pairs taken in as married
		std::uint32_t courting{ 0 }; // Courting / Lover-rank pairs taken in as dating
		std::uint32_t protectedByQuest{ 0 };  // of those, how many a quest owns (never moved by the ladder)
		std::uint32_t withChildren{ 0 };      // couples that have children (never divorce)
		std::uint32_t skipped{ 0 };           // dead, missing, child, already paired, married through Love Is In The Air ...
		std::uint32_t waiting{ 0 };           // 0.19.1: one of them is not in memory (cell not loaded) - taken in once both are seen
	};
	// Walks every NPC's game relationship records once: Spouse -> married, Courting / Lover rank -> dating. Couples a quest owns are
	// protected (the quest decides); the rest live on the ladder. Children (any age, Parent/Child records) are counted. Safe to press again.
	// a_auto = the automatic run on first load (also sets the co-save flag; the button always runs).
	ImportResult ImportGameCouples(bool a_auto);
	[[nodiscard]] bool GameImportDone();
	// 0.19.1: a placed NPC whose cell is not loaded is not in memory (Shel, Bagrak: not persistent), so the import cannot see them.
	// Those couples wait in a saved list and are taken in the first time both are seen. Called from the simulation tick; on a save
	// that ran its import before 0.19.1 it first fills the list once.
	void ResolvePendingImports();
	// optional, from other mods (Lover's Ledger scenes, via Papyrus): a pair becomes dating. Returns true when a new pair or an upgrade happened.
	bool ImportOtherPair(RE::FormID a_baseA, RE::FormID a_baseB);
	void FinishOtherImport(std::int32_t a_count, const char* a_source);

	[[nodiscard]] std::vector<Pair> Snapshot();
	[[nodiscard]] std::string       NpcName(RE::FormID a_base);
	[[nodiscard]] float             DaysNow();
	[[nodiscard]] std::int32_t      LastSimDay();
	void                            SetLastSimDay(std::int32_t a_day);
	// per-community pace cap: may this community start one more couple this month?
	[[nodiscard]] bool              CommunityMayPair(RE::FormID a_community, std::int32_t a_day);
	void                            NoteCommunityPairing(RE::FormID a_community, std::int32_t a_day);

	// custom (mod-added) followers belong to the player: kept out of the simulation, real-scene ladder and couple chats unless
	// the player allows them on the Couples page (playable races only). Vanilla followers are not affected.
	[[nodiscard]] bool                    IsCustomFollower(RE::Actor* a_actor);
	[[nodiscard]] bool                    IsPersonRace(const RE::TESRace* a_race);  // NPC race, not a creature (vampires count)
	[[nodiscard]] bool                    IsHumanoid(RE::Actor* a_actor);  // an adult of a person race
	[[nodiscard]] bool                    IsReservedFollower(RE::Actor* a_actor);  // custom follower, not allowed
	[[nodiscard]] bool                    FollowerAllowed(RE::FormID a_base);
	void                                  AllowFollower(RE::FormID a_base, bool a_allow);
	[[nodiscard]] std::vector<RE::FormID> AllowedFollowers();
	void                                  NoteFollowerSeen(RE::Actor* a_actor);  // remembered once seen as a player teammate
	[[nodiscard]] bool                    IsModNpc(RE::Actor* a_actor);  // defined by a mod, not the base game or DLC
	void                                  MarkFollower(RE::FormID a_base, bool a_mark);  // the player marks someone the scan missed

	// children: 0 automatic, 1 always, 2 never (per person, the player's choice on the Followers page). Automatic = yes, except
	// people known to be unable (automatons like Ashe). A pair can have children when both can and - with BeastChildrenOwnKind
	// on - they are of the same kind (men and mer / Khajiit / Argonian); someone set to "always" overrides the race rule.
	[[nodiscard]] int                     ChildrenChoice(RE::FormID a_base);
	void                                  SetChildrenChoice(RE::FormID a_base, int a_choice);
	[[nodiscard]] bool                    CanHaveChildrenAlone(RE::Actor* a_actor);
	// what the pregnancy mod says (Beeing Female per person / race / global "disable pregnancy"), pushed from Papyrus: -1 unknown
	void                                  SetEngineChildren(RE::FormID a_base, int a_verdict);
	[[nodiscard]] int                     EngineChildren(RE::FormID a_base);
	[[nodiscard]] bool                    KnownNoChildren(RE::FormID a_base);  // automatons and the like (built-in list)
	[[nodiscard]] bool                    CanHaveChildren(RE::Actor* a_a, RE::Actor* a_b);

	// NPCs the player has met - talked to, or been near (saved). With OnlyMetCouples on, new couples only form between met NPCs.
	[[nodiscard]] bool        PlayerMet(RE::FormID a_base);
	void                      NoteMet(RE::FormID a_base);
	[[nodiscard]] std::size_t MetCount();
	[[nodiscard]] bool        TalkedToPlayer(RE::Actor* a_actor);

	// wedding register: "A and B - Fredas, 19th of Last Seed, in the afternoon (in 2 days)", soonest first
	[[nodiscard]] std::vector<std::string> WeddingSchedule();
	[[nodiscard]] std::string              GameDate(int a_daysFromToday);
}