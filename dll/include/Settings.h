#pragma once

namespace ONPCR::Settings
{
	// Every setting of OStim NPCs Reforged. Lives in Data\SKSE\Plugins\ONPCs.ini,
	// is edited on the SKSE Menu Framework page and read by ONpcMain.psc through ONPCR_Settings.
	struct Data
	{
		// [General]
		bool disabled{ false };                       // master switch: no automatic scenes, no polling
		bool allowActiveFollowers{ true };            // followers have scenes between themselves
		bool allowPlayersPartners{ false };           // the player's lover / spouse may be picked
		bool activeFollowersEngageOtherNPCs{ false }; // followers may pair with other NPCs
		bool allowElderRace{ false };                 // elders may be picked
		bool allowCommonEnemies{ true };              // bandits etc. have scenes in their camps
		bool stopWhenFound{ true };                   // NPC scenes end (or never start) when the player is detected
		bool travelToLocation{ true };                // NPCs walk to the furniture first
		bool enemiesTravelToLocation{ false };        // enemies walk to the furniture first
		bool sceneAmmoOff{ true };                    // 0.20.2: arrows/bolts off in every OStim scene, back on after

		// [Furniture]
		bool enableFurniture{ true };                 // use furniture at all
		bool furnitureOnlyBeds{ true };               // only beds count as furniture
		int  scenesStartIn{ 2 };                      // 0 anywhere, 1 any furniture, 2 beds only
		bool enemiesIgnoreScenesStartInRules{ false };

		// [Locations]
		bool followersNoScenesDungeons{ true };
		bool noScenesInTowns{ true };
		bool noScenesInGuilds{ true };
		bool noScenesInInns{ false };

		// [Limits]
		int minRelation{ 3 };                         // minimum relationship rank between two NPCs (-4..4)
		int maxScenes{ 2 };                           // scenes running at once (1..4)
		int maxFollowerScenesPerNight{ 2 };           // 1..10
		int maxEnemyScenesPerNight{ 3 };              // 1..20
		int minNight{ 20 };                           // hour the night starts (13..24)
		int maxNight{ 4 };                            // hour the night ends (1..11)
		int scanFreq{ 15 };                           // seconds between scans (10..120)

		// [Weights]
		int weightMF{ 100 };
		int weightFF{ 0 };
		int weightMM{ 0 };

		// [Relationships] (Reforged)
		bool  infidelity{ true };          // off = committed NPCs never stray (scenes, off-screen) and nothing counts as cheating
		bool  gameMarriagesCanEnd{ false }; // 0.21.0: imported game marriages follow the normal cheating / cold-divorce rules (protected couples never)
		bool  beastChildrenOwnKind{ true };  // Khajiit and Argonians only have children with their own kind
		float strayChance{ 0.3f };         // 0.18.0: % per scan (scans run every few seconds, so keep it small)
		float strayRepeatMult{ 2.0f };
		float historyWeight{ 1.0f };
		float strangerBaseScore{ 1.0f };
		bool  useRelationsFinderFamily{ true };
		bool  useLoversLedger{ true };
		float pairRadius{ 4096.0f };  // 0.26.0 base port: how far from the player known couples are looked for (one building indoors)
		int   startState{ 3 };             // 0.26.0: arousal state one partner needs to start a scene: 0 Indifferent 1 Content 2 Interested 3 Yearning 4 Aching
		int   partnerState{ 2 };           // ... and the state the other partner needs (receptive)
		bool  groupScenes{ true };         // 0.26.0 phase 3: an NPC with two or more living partners nearby (orc chiefs and their wives) can have a group scene
		int   maxGroupSize{ 4 };           // ... at most this many people (3..5; OStim's installed packs decide which scenes fit)
		float coupleCooldownDays{ 0.04f };  // ... a short break (game days, ~1 h) after a scene for the couple and for each of them; then their state decides

		// [Ladder] (Reforged phase 2)
		bool  ladderEnabled{ true };
		bool  rankFollowsLadder{ true };   // dating = confidant, engaged = ally, married = lover (only raises); breakup -> acquaintance
		bool  watchRelationships{ true };  // 0.20.0: follow other mods' relationship rank changes (breakups, new lovers, marriages)
		bool  onlyMetCouples{ true };      // new couples only form between NPCs the player has met (talked to or been near)
		float sparkChance{ 25.0f };        // % chance a meeting between two unattached people starts anything (x archetypes)
		float directDatingShare{ 25.0f };  // % of sparks that skip "seeing each other" and go straight to dating (x archetypes)
		float promoteChance{ 20.0f };      // % chance per meeting, once the thresholds are met, that a couple moves up a stage
		int   candidateEncounters{ 3 };    // encounters before casual -> candidate
		int   candidateMinDays{ 7 };       // and at least this many days since they met
		int   engagedEncounters{ 6 };      // encounters before candidate -> engaged
		int   engagedMinDays{ 30 };        // and at least this many days as candidates
		int   marriedAfterDays{ 14 };      // engaged -> married after this many days
		bool  realWeddings{ false };       // engaged couples marry at the Temple of Mara two days after the time is up
		int   weddingGapDays{ 3 };         // weddings take turns: at most one every this many days
		int   weddingGuests{ 6 };          // guests besides the couple's children (children always come): close family first, then friends
		int   altarNoShowChance{ 3 };      // 0.14.1: base % chance one partner never comes to the wedding (+10 per stray, +10 if not close; max 40); 0 = never
		int   ledgerImportEncounters{ 3 };  // 0.17.0: 'Import from other mods' - Lover's Ledger NPC pairs with at least this many scenes become dating
		int   weddingWaitDays{ 3 };        // 0.14.1: an invited player who did not come: the wedding is put off a day, up to this many times; 0 = it goes ahead
		bool  seatedGuests{ true };        // 0.14.1: wedding guests sit on the temple benches during the ceremony
		bool  objectionStopsWedding{ true };  // 0.14.1: attacking the bride, groom or priest during the ceremony stops the wedding
		bool  friendChats{ true };         // 0.16.0: married / engaged NPCs now and then talk to a friend nearby about their partner or wedding
		int   strayStrikes{ 2 };           // strays before a mod-made couple breaks up
		float closenessGain{ 25.0f };      // closeness added per encounter (0..100)
		float strayPenalty{ 40.0f };       // closeness lost per stray
		float worldPace{ 1.0f };           // multiplies every day threshold (2 = slow, 0.5 = fast)

		// [Simulation] (Reforged phase 2 slice 2)
		bool  simulationEnabled{ true };
		float dailyChance{ 0.25f };               // % per pair per simulated day, before multipliers (~one encounter per NPC every 80 days)
		float sameHoldFactor{ 0.5f };            // other places in the same hold
		int   catchUpCapDays{ 30 };              // most missed days simulated per catch-up
		float driftPerDay{ 1.5f };               // closeness lost per day without an encounter
		int   coldDaysBeforeDivorce{ 60 };       // cold + strayed married pairs divorce after this
		int   maxNewCouplesPerCommunityPerMonth{ 2 };
		int   shortlist{ 5 };                    // likely partners considered per NPC per day
		bool  writeLoversLedger{ true };         // simulated encounters are written to Lover's Ledger

		// [Archetypes] [Amulet] [Letters] (slice 3)
		bool  useMarasArchetypes{ true };   // read class / skill / temperament from M.A.R.A.S when it is installed
		int   playerPartnerRomances{ 0 };   // the player's fiances/spouses on the NPC ladder: 0 never, 1 only M.A.R.A.S open-relationship spouses, 2 always
		bool  amuletEnabled{ true };        // Amulet of Mara wearers count as looking for marriage
		float lookingMultiplier{ 2.0f };    // interest multiplier when either single NPC wears one
		bool  manageMarriedAmulets{ true }; // NPC-NPC marriage removes the amulet + giver spell + marriage faction
		bool  ownAmulets{ true };           // no amulet-giver mod present: this mod hands out a few Amulets of Mara itself
		float ownAmuletShare{ 15.0f };      // % of eligible singles per community that may wear one
		bool  gossip{ true };               // notification when a couple close to the player strays
		bool  lettersEnabled{ true };       // courier letters about couples close to the player
		int   letterMinRank{ 3 };           // vanilla relationship rank to the player needed (3 = ally)
		int   candidateLetterStage{ 2 };
		int   notifications{ 1 };               // 0 none, 1 friends + your candidates, 2 every couple
		int   mourningDays{ 90 };
		// 0.19.0 couples redesign
		int   affairAfter{ 2 };             // cheating with the same person this many times turns into an affair (once can be a mistake)
		int   familyBlockDays{ 60 };        // a family-blocked couple (not quest-locked) may get engaged after this many days of dating
		bool  matchmakers{ true };          // finishing Spread the Love makes sparks likelier in Riften for 30 days
		bool  heartbreak{ true };           // after a breakup nobody starts something new at once (time by how far they got)
		bool  skipPeriodScenes{ true };     // 0.18.0: Beeing Female - no scenes during her period; tampon back on after a scene
		bool  pregnancyRules{ true };       // 0.21.3: vampires (until cured), automatons and androids never have children; Khajiit / Argonians only with their own kind - also for Beeing Female's own scenes (ONpcMain.PregnancyRules)
		bool  hatchNpcBabies{ true };       // 0.21.4: Beeing Female baby items of two NPCs turn into children on its own clock (ONPCR_BeeingFemaleBridge)
		bool  babyStages{ true };           // 0.24.0: carried -> away -> child for every Beeing Female baby (ONPCR_BeeingFemaleBridge.BabyStages); off = hatchNpcBabies
		int   babyCarriedDays{ 365 };       // baby stages: days the baby item stays with its mother
		int   childAppearsDays{ 1642 };     // baby stages: days after birth when the child comes home (4.5 years)
		bool  pregnancyEvents{ true };      // simulated encounters are handed to the pregnancy engine (FertilityModeAddSperm event)           // a widowed NPC takes no new partner for this long (x pace, x temperament)    // a M.A.R.A.S candidate of the player writes about an NPC romance: 0 never, 1 courting, 2 engaged, 3 married

		// [Voice] (slice 5)
		bool  voiceLines{ true };           // couples near the player speak vanilla lines to each other; jealous partners react
		float voiceCooldownHours{ 3.0f };   // game hours before the same couple speaks again
		bool  familyChats{ true };          // 0.23.0: parents, children, siblings and grandparents near the player exchange an everyday line now and then

		// [Home] (sleep-overs)
		bool  sleepOvers{ false };          // married partners who live apart spend the night at the shared home (21:00-07:00)
		bool  dateNights{ false };          // dating and engaged couples meet in the evening (18:00-22:00) at one partner's
		int   dateNightChance{ 30 };       // % chance per couple per evening
		bool  households{ false };          // 0.22.0: married couples who live apart in one town move in together (19:00-07:00 at the shared home)
		bool  householdChildren{ true };    // their Beeing Female children live there too (outside in town by day)
		bool  nightsOut{ true };            // households: some evenings (DateNightChance) the couple spends 19:00-22:00 at the town inn

		// [Advanced]
		bool furnitureGuardDebug{ false };            // verbose furniture-guard lines in the Papyrus log
		bool debugNotifications{ false };             // on-screen line for every simulation / ladder event
	};

	enum class Kind
	{
		Bool,
		Int,
		Float
	};

	// One line of the settings table: INI section + key (the key doubles as the Papyrus key).
	struct Entry
	{
		const char*   section;
		const char*   key;
		Kind          kind;
		const char*   comment;
		bool Data::*  b;
		int Data::*   i;
		float Data::* f;
	};

	[[nodiscard]] std::span<const Entry> Entries();

	// 0.26.0 alpha: the add-on modules (Notes and Reports\Reforged - module split plan). A module's features only run while its
	// plugin is loaded (looked up once at kDataLoaded); without it they are off whatever the INI says. The INI itself keeps
	// the player's values, so installing the module later brings them back. Until the split, every module lives in the
	// Letters plugin.
	enum class Module
	{
		Relationships,
		DailyLife,
		Weddings,
		Families,
		Conversations
	};
	void               DetectModules();
	[[nodiscard]] bool Installed(Module a_module);

	[[nodiscard]] Data        Get();  // what the mod runs with: the INI values, a missing module's features off
	[[nodiscard]] Data        Raw();  // the INI values as the player set them (menu drafts, saving, presets)
	[[nodiscard]] Data        Defaults();
	void                      Set(const Data& a_data);   // replaces the live values and saves the INI
	void                      Load();                    // INI -> live values (missing file = defaults)
	bool                      Save();                    // live values -> INI
	[[nodiscard]] float       GetFloat(std::string_view a_key, float a_default);
	[[nodiscard]] bool        GetBool(std::string_view a_key, bool a_default);
	[[nodiscard]] int         GetInt(std::string_view a_key, int a_default);
	[[nodiscard]] std::string IniPath();
	// Testing mode: fast rates + every feature under test on; the player's own settings are backed up and restored on Stop
	[[nodiscard]] bool TestingMode();
	void               StartTesting();
	void               StopTesting();

	// 0.18.0 pace presets: Slow / Default / Fast set the couple-pace fields only; anything else hand-changed = Custom.
	enum class Pace
	{
		Slow,
		Default,
		Fast
	};
	void                      ApplyPace(Data& a_data, Pace a_pace);
	[[nodiscard]] const char* PaceName(const Data& a_data);  // "Slow", "Default", "Fast" or "Custom"
	// the player's own saved presets (every setting), one INI each in the folder Data/SKSE/Plugins/ONPCs Presets
	[[nodiscard]] std::vector<std::string> SavedPresets();
	bool                                   SavePreset(const std::string& a_name);
	bool                                   LoadPreset(const std::string& a_name);
	bool                                   DeletePreset(const std::string& a_name);
}