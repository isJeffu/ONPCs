#include "Settings.h"

#include <SimpleIni.h>

namespace ONPCR::Settings
{
	namespace
	{
		Data              live;
		std::shared_mutex guard;

		constexpr Entry B(const char* s, const char* k, bool Data::* m, const char* c) { return Entry{ s, k, Kind::Bool, c, m, nullptr, nullptr }; }
		constexpr Entry I(const char* s, const char* k, int Data::* m, const char* c) { return Entry{ s, k, Kind::Int, c, nullptr, m, nullptr }; }
		constexpr Entry F(const char* s, const char* k, float Data::* m, const char* c) { return Entry{ s, k, Kind::Float, c, nullptr, nullptr, m }; }

		const std::array kEntries{
			B("General", "Disabled", &Data::disabled, "; 1 = no automatic scenes and no cell polling (match-maker spells still work)"),
			B("General", "AllowActiveFollowers", &Data::allowActiveFollowers, "; followers have scenes between themselves"),
			B("General", "AllowPlayersPartners", &Data::allowPlayersPartners, "; the player's lover or spouse may be picked for NPC scenes"),
			B("General", "ActiveFollowersEngageOtherNPCs", &Data::activeFollowersEngageOtherNPCs, "; followers may pair with other NPCs"),
			B("General", "AllowElderRace", &Data::allowElderRace, "; elders may be picked"),
			B("General", "AllowCommonEnemies", &Data::allowCommonEnemies, "; bandits, warlocks etc. have scenes in their camps"),
			B("General", "StopWhenFound", &Data::stopWhenFound, "; NPC scenes end, or never start, when the player is detected"),
			B("General", "TravelToLocation", &Data::travelToLocation, "; NPCs walk to the furniture first"),
			B("General", "EnemiesTravelToLocation", &Data::enemiesTravelToLocation, "; enemies walk to the furniture first"),
			B("General", "SceneAmmoOff", &Data::sceneAmmoOff, "; arrows and bolts come off in every OStim scene and go back on after"),
			B("Privacy", "Privacy", &Data::privacy, "; couples want privacy: nobody awake may see them (or they stop); someone within earshot = they keep quiet; their own private bed first"),
			F("Privacy", "Earshot", &Data::earshot, "; how far away someone hears them, through walls too (units, 600 = about 8.5 m); 0 = only being seen counts"),

			B("Furniture", "EnableFurniture", &Data::enableFurniture, "; use furniture at all"),
			B("Furniture", "FurnitureOnlyBeds", &Data::furnitureOnlyBeds, "; only beds count as furniture"),
			I("Furniture", "ScenesStartIn", &Data::scenesStartIn, "; 0 = anywhere, 1 = any furniture, 2 = beds only"),
			B("Furniture", "EnemiesIgnoreScenesStartInRules", &Data::enemiesIgnoreScenesStartInRules, "; enemies ignore the ScenesStartIn rule"),

			B("Locations", "FollowersNoScenesDungeons", &Data::followersNoScenesDungeons, "; followers never start scenes in dungeons"),
			B("Locations", "NoScenesInTowns", &Data::noScenesInTowns, "; no scenes in city and town locations"),
			B("Locations", "NoScenesInGuilds", &Data::noScenesInGuilds, "; no scenes in guild halls"),
			B("Locations", "NoScenesInInns", &Data::noScenesInInns, "; no scenes in inns"),

			I("Limits", "MinRelation", &Data::minRelation, "; minimum relationship rank between two NPCs, -4..4"),
			I("Limits", "MaxScenes", &Data::maxScenes, "; scenes running at the same time, 1..4"),
			I("Limits", "MaxFollowerScenesPerNight", &Data::maxFollowerScenesPerNight, "; 1..10"),
			I("Limits", "MaxEnemyScenesPerNight", &Data::maxEnemyScenesPerNight, "; 1..20"),
			I("Limits", "MinNight", &Data::minNight, "; hour the night starts, 24-hour clock (20 = 8 PM)"),
			I("Limits", "MaxNight", &Data::maxNight, "; hour the night ends (4 = 4 AM)"),
			I("Limits", "ScanFreq", &Data::scanFreq, "; seconds between scans, 10..120"),

			I("Weights", "WeightMF", &Data::weightMF, "; male-female pair weight 0..100"),
			I("Weights", "WeightFF", &Data::weightFF, "; female-female pair weight 0..100"),
			I("Weights", "WeightMM", &Data::weightMM, "; male-male pair weight 0..100"),

			B("Relationships", "Infidelity", &Data::infidelity, "; off = committed NPCs never stray (not in scenes, not off-screen) and nothing counts as cheating; on = the stray chance below applies"),
			B("Relationships", "BeastChildrenOwnKind", &Data::beastChildrenOwnKind, "; on = Khajiit and Argonians only have children with their own kind (lore); off = any two people can"),
			F("Relationships", "StrayChance", &Data::strayChance, "; % chance per scan that a married/courting NPC strays (0 = faithful)"),
			F("Relationships", "StrayRepeatMult", &Data::strayRepeatMult, "; multiplier when Lover's Ledger shows the NPC has strayed before"),
			F("Relationships", "HistoryWeight", &Data::historyWeight, "; weight of the Lover's Ledger pair score when choosing a partner"),
			F("Relationships", "StrangerBaseScore", &Data::strangerBaseScore, "; base score every allowed stranger pair gets (0 = only pairs with history)"),
			B("Relationships", "UseRelationsFinderFamily", &Data::useRelationsFinderFamily, "; exclude family of any degree, blood and in-law (Relations Finder)"),
			B("Relationships", "UseLoversLedger", &Data::useLoversLedger, "; prefer pairs with history (Lover's Ledger)"),
			I("Relationships", "StartState", &Data::startState, "; 0.26.0: arousal starts a scene - the state one partner needs: 0 Indifferent, 1 Content, 2 Interested, 3 Yearning, 4 Aching (from OSL Aroused, a SexLab Aroused-type mod, or Reforged's own desire when none is installed)"),
			I("Relationships", "PartnerState", &Data::partnerState, "; 0.26.0: ...and the state the other partner needs to say yes (2 = Interested)"),
			B("Relationships", "GroupScenes", &Data::groupScenes, "; 0.26.0: an NPC with two or more living partners (an orc chief and his wives) can have a scene with them together, when they are near and in the mood; the others walk over"),
			I("Relationships", "MaxGroupSize", &Data::maxGroupSize, "; 0.26.0: most people in a group scene, 3..5 (your OStim animation packs decide which scenes fit)"),
			F("Relationships", "CoupleCooldownDays", &Data::coupleCooldownDays, "; 0.26.0: a short break after a scene (game days, 0.04 = about an hour) for the couple and for each of them - after that their arousal state decides (a fulfilled couple waits, a frustrated or eager one may go again)"),
			F("Relationships", "PairRadius", &Data::pairRadius, "; 0.26.0: known couples (spouses, courting, past lovers) are looked for within this distance of you (4096 = a town district; indoors: the building you are in)"),

			B("Ladder", "LadderEnabled", &Data::ladderEnabled, "; NPC-to-NPC relationship ladder on/off"),
			B("Ladder", "RankFollowsLadder", &Data::rankFollowsLadder, "; the game's relationship rank follows the couple: dating = confidant, engaged = ally, married = lover (never lowers a higher rank); a breakup drops it to acquaintance"),
			B("Ladder", "WatchRelationships", &Data::watchRelationships, "; follow other mods: partners whose relationship rank a mod drops to acquaintance split up, two people a mod makes lovers start dating, a new spouse record marries them"),
			B("Ladder", "GameMarriagesCanEnd", &Data::gameMarriagesCanEnd, "; 0.21.0: marriages the game already had (imported) can end like ours: enough cheating breaks them, a cold marriage with cheating and no children divorces. Protected couples (OStimNPCsReforged_ProtectedCouples.json, or Keep together ticked) never"),
			B("Ladder", "OnlyMetCouples", &Data::onlyMetCouples, "; new couples only form between NPCs you have met (talked to, or been near); couples that already exist carry on. Off = anyone in Skyrim can get together off-screen"),
			F("Ladder", "SparkChance", &Data::sparkChance, "; % chance a meeting between two unattached people starts anything at all (archetypes tilt it: romantics more, the independent less, a big class gap less)"),
			F("Ladder", "DirectDatingShare", &Data::directDatingShare, "; % of new couples that skip seeing each other and start dating straight away (romantics and the jealous more, the independent less)"),
			F("Ladder", "PromoteChance", &Data::promoteChance, "; % chance per meeting, once the encounter and day thresholds are met, that a couple moves up a stage"),
			I("Ladder", "CandidateEncounters", &Data::candidateEncounters, "; encounters before casual -> candidate"),
			I("Ladder", "CandidateMinDays", &Data::candidateMinDays, "; and at least this many days since they met"),
			I("Ladder", "EngagedEncounters", &Data::engagedEncounters, "; encounters before candidate -> engaged"),
			I("Ladder", "EngagedMinDays", &Data::engagedMinDays, "; and at least this many days as candidates"),
			I("Ladder", "MarriedAfterDays", &Data::marriedAfterDays, "; engaged -> married after this many days"),
			B("Ladder", "RealWeddings", &Data::realWeddings, "; engaged couples marry at the Temple of Mara in Riften two days after the engagement time is up (in the afternoon, noon to 17:00); you get word of it, and if you are there Maramal holds the ceremony"),
			I("Ladder", "WeddingGapDays", &Data::weddingGapDays, "; weddings take turns: at most one couple marries every this many days (a couple whose time is up waits for the next free day)"),
			I("Ladder", "WeddingGuests", &Data::weddingGuests, "; real weddings: guests besides the children of either partner (children always come): close family first (parents, siblings, grandparents), then friends; 0..12"),
			I("Ladder", "AltarNoShowChance", &Data::altarNoShowChance, "; real weddings: base % chance one partner never comes and the other is left at the altar (+10 per stray, +10 if they are not close; at most 40); 0 = never"),
			I("Ladder", "LedgerImportEncounters", &Data::ledgerImportEncounters, "; Couples page > Import from other mods: Lover's Ledger NPC pairs with at least this many scenes together become dating (1..20)"),
			I("Ladder", "WeddingWaitDays", &Data::weddingWaitDays, "; real weddings: you were invited and did not come - the wedding is put off a day, up to this many times; 0 = it goes ahead without you"),
			B("Ladder", "SeatedGuests", &Data::seatedGuests, "; real weddings: guests sit on the temple benches during the ceremony"),
			B("Ladder", "FriendChats", &Data::friendChats, "; married or engaged NPCs near you now and then tell a friend or relative about their partner or their wedding (voice lines must be on)"),
			B("Ladder", "ObjectionStopsWedding", &Data::objectionStopsWedding, "; real weddings: attacking the bride, the groom or the priest during the ceremony stops the wedding (a new date follows)"),
			I("Ladder", "StrayStrikes", &Data::strayStrikes, "; strays before a mod-made couple breaks up"),
			F("Ladder", "ClosenessGain", &Data::closenessGain, "; closeness added per encounter (0..100)"),
			F("Ladder", "StrayPenalty", &Data::strayPenalty, "; closeness lost per stray"),
			F("Ladder", "WorldPace", &Data::worldPace, "; multiplies every day threshold (2 = slow, 0.5 = fast)"),

			B("Simulation", "SimulationEnabled", &Data::simulationEnabled, "; off-screen encounters while the player is elsewhere"),
			F("Simulation", "DailyChance", &Data::dailyChance, "; % per pair per simulated day, before multipliers"),
			F("Simulation", "SameHoldFactor", &Data::sameHoldFactor, "; chance factor for other places in the same hold (0 = own community only)"),
			I("Simulation", "CatchUpCapDays", &Data::catchUpCapDays, "; most missed days simulated per catch-up"),
			F("Simulation", "DriftPerDay", &Data::driftPerDay, "; closeness lost per day without an encounter"),
			I("Simulation", "ColdDaysBeforeDivorce", &Data::coldDaysBeforeDivorce, "; cold + strayed married pairs (made by this mod) divorce after this many days"),
			I("Simulation", "MaxNewCouplesPerCommunityPerMonth", &Data::maxNewCouplesPerCommunityPerMonth, "; pace cap for new courting couples per community per 30 days (0 = none)"),
			I("Simulation", "Shortlist", &Data::shortlist, "; likely partners considered per NPC per day"),
			B("Simulation", "WriteLoversLedger", &Data::writeLoversLedger, "; simulated encounters are written to Lover's Ledger"),

			B("Archetypes", "UseMarasArchetypes", &Data::useMarasArchetypes, "; read social class / skill / temperament from M.A.R.A.S when it is installed"),
			I("Archetypes", "PlayerPartnerRomances", &Data::playerPartnerRomances, "; the player's fiances/spouses may have NPC romances off-screen: 0 never, 1 only spouses in M.A.R.A.S's open-relationship faction, 2 always"),
			B("Amulet", "AmuletEnabled", &Data::amuletEnabled, "; Amulet of Mara wearers count as looking for marriage"),
			F("Amulet", "LookingMultiplier", &Data::lookingMultiplier, "; interest multiplier when either single NPC wears one"),
			B("Amulet", "ManageMarriedAmulets", &Data::manageMarriedAmulets, "; NPC-NPC marriage removes the amulet, the giver spell and the marriage faction"),
			B("Amulet", "OwnAmulets", &Data::ownAmulets, "; no amulet-giver mod present: this mod hands out a few Amulets of Mara itself (and takes them back on pairing)"),
			F("Amulet", "OwnAmuletShare", &Data::ownAmuletShare, "; % of eligible singles that may wear one"),
			B("Letters", "Gossip", &Data::gossip, "; notification when a couple close to the player strays"),
			B("Letters", "LettersEnabled", &Data::lettersEnabled, "; courier letters about couples close to the player"),
			I("Letters", "LetterMinRank", &Data::letterMinRank, "; vanilla relationship rank to the player needed for a letter (3 = ally, 4 = lover)"),
			I("Letters", "CandidateLetterStage", &Data::candidateLetterStage, "; an NPC the player courts in M.A.R.A.S writes about an NPC romance: 0 never, 1 when they start courting, 2 when engaged, 3 when married"),
			I("Letters", "Notifications", &Data::notifications, "; on-screen messages about NPC couples: 0 none (letters only), 1 friends of yours and NPCs you are courting, 2 every couple"),

			I("Ladder", "MourningDays", &Data::mourningDays, "; a widowed NPC takes no new partner for this many days (x world pace; romantics longer, the independent shorter)"),
			I("Ladder", "AffairAfter", &Data::affairAfter, "; 0.19.0: cheating with the same person this many times turns into an affair (1..10)"),
			I("Ladder", "FamilyBlockDays", &Data::familyBlockDays, "; 0.19.0: a couple whose family disapproves may get engaged after this many days of dating (quest-locked couples wait for their quest)"),
			B("Ladder", "Matchmakers", &Data::matchmakers, "; 0.19.0: finishing Spread the Love (the Temple of Mara pamphlets) makes sparks likelier in Riften for 30 days"),
			B("Ladder", "Heartbreak", &Data::heartbreak, "; 0.19.0: after a breakup nobody starts something new at once: seeing each other ~1 week, dating ~3 weeks, jilted ~2 months, divorced ~3 months (x world pace)"),

			B("Pregnancy", "SkipPeriodScenes", &Data::skipPeriodScenes, "; Beeing Female: no scenes for a woman in her period; after any scene of this mod she puts her tampon back on"),
			B("Pregnancy", "PregnancyEvents", &Data::pregnancyEvents, "; simulated encounters are handed to the installed pregnancy engine (Beeing Female NG, Fertility Mode, FM Reloaded) through the FertilityModeAddSperm mod event"),
			B("Pregnancy", "HatchNpcBabies", &Data::hatchNpcBabies, "; 0.21.4: Beeing Female (baby item births) only turns a baby item into a child when the player is the father; on = babies of two NPCs grow into children on the same clock (Beeing Female's Mature time), mothers near you"),
			B("Pregnancy", "BabyStages", &Data::babyStages, "; 0.24.0: Beeing Female baby items (every baby, yours too) are carried for BabyCarriedDays, then live away from home (with kin, a temple, a wet nurse...) until ChildAppearsDays after birth, then come home as a child beside a parent; parents tell you where the child is. Off = the 0.21.4 behaviour (HatchNpcBabies)"),
			I("Pregnancy", "BabyCarriedDays", &Data::babyCarriedDays, "; baby stages: game days the baby item stays with its mother (1..3650; 365 = a year)"),
			I("Pregnancy", "ChildAppearsDays", &Data::childAppearsDays, "; baby stages: game days after birth when the child comes home (must be after BabyCarriedDays; 1642 = 4.5 years)"),
			B("Pregnancy", "PregnancyRules", &Data::pregnancyRules, "; 0.21.3: no children for vampires (until cured), automatons and androids from follower mods; Khajiit and Argonians only with their own kind - applied to this mod's couples AND to Beeing Female's own scenes (the player's too) through Beeing Female's per-person switches"),

			B("Advanced", "FurnitureGuardDebug", &Data::furnitureGuardDebug, "; verbose furniture-guard lines in the Papyrus log"),
			B("Voice", "VoiceLines", &Data::voiceLines, "; couples near you greet each other, say goodbye and react to engagements and breakups with vanilla voice lines; a partner who sees a stray reacts"),
			F("Voice", "VoiceCooldownHours", &Data::voiceCooldownHours, "; game hours before the same couple speaks again"),
			B("Voice", "FamilyChats", &Data::familyChats, "; 0.23.0: parents, children, siblings and grandparents near you now and then say an everyday line to each other (vanilla lines and voices, never anything romantic; voice lines must be on)"),
			B("Home", "SleepOvers", &Data::sleepOvers, "; married partners who live apart spend the night (21:00-07:00) around the shared home and may use a free bed there; never followers or guards"),
			B("Home", "DateNights", &Data::dateNights, "; dating and engaged couples who live in the same town meet in the evening (18:00-22:00): one goes to the other and spends the evening near them; never followers or guards; quests always win"),
			I("Home", "DateNightChance", &Data::dateNightChance, "; % chance each evening that a couple has a date night, 0..100"),
			B("Home", "Households", &Data::households, "; married couples who live apart in the same town: the spouse moves in for the evenings and nights (19:00-07:00), joins the house (its beds) and keeps their day job; a widow keeps the house; never followers or guards; quests always win"),
			B("Home", "HouseholdChildren", &Data::householdChildren, "; with Households: the couple's Beeing Female children live in the home too - out in town by day, home from 19:00"),
			B("Home", "NightsOut", &Data::nightsOut, "; with Households: some evenings (DateNightChance) the couple goes to the town inn together from 19:00 to 22:00"),
			B("Advanced", "DebugNotifications", &Data::debugNotifications, "; on-screen message for every simulation and ladder event (pool size, each off-screen encounter, strays, drift, promotions) - testing only"),
		};

		std::string IniKey(const Entry& e)
		{
			const char prefix = e.kind == Kind::Bool ? 'b' : (e.kind == Kind::Int ? 'i' : 'f');
			return std::string(1, prefix) + e.key;
		}

		bool KeyIs(std::string_view a_key, std::string_view a_name)
		{
			return std::equal(a_key.begin(), a_key.end(), a_name.begin(), a_name.end(),
				[](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
		}

		const Entry* Find(std::string_view a_key)
		{
			for (const auto& e : kEntries) {
				if (KeyIs(a_key, e.key)) {
					return &e;
				}
			}
			return nullptr;
		}
	}

	std::span<const Entry> Entries()
	{
		return kEntries;
	}

	std::string IniPath()
	{
		std::error_code ec;
		const auto      root = std::filesystem::current_path(ec);
		return (root / "Data" / "SKSE" / "Plugins" / "ONPCs.ini").string();
	}

	Data Defaults()
	{
		return Data{};
	}

	namespace
	{
		// which plugin carries each module (all in the Letters plugin until the split)
		constexpr std::array<std::string_view, 5> kModulePlugin{
			"OStimNPCsReforged - Letters.esp",  // Relationships
			"OStimNPCsReforged - Letters.esp",  // DailyLife
			"OStimNPCsReforged - Letters.esp",  // Weddings
			"OStimNPCsReforged - Letters.esp",  // Families
			"OStimNPCsReforged - Letters.esp",  // Conversations
		};
		constexpr std::array<std::string_view, 5> kModuleName{ "Relationships", "Daily Life", "Weddings & Letters", "Families", "Conversations" };
		std::array<std::atomic_bool, 5>          installed{};

		Data Gate(Data d)
		{
			if (!Installed(Module::Relationships)) {
				d.ladderEnabled = false;
				d.rankFollowsLadder = false;
				d.watchRelationships = false;
				d.simulationEnabled = false;
				d.writeLoversLedger = false;
				d.useMarasArchetypes = false;
				d.gossip = false;
				d.matchmakers = false;
				d.heartbreak = false;
				d.infidelity = false;
				d.gameMarriagesCanEnd = false;
			}
			if (!Installed(Module::DailyLife)) {
				d.sleepOvers = false;
				d.dateNights = false;
				d.households = false;
				d.nightsOut = false;
			}
			if (!Installed(Module::Weddings)) {
				d.realWeddings = false;
				d.lettersEnabled = false;
				d.amuletEnabled = false;
				d.ownAmulets = false;
				d.manageMarriedAmulets = false;
			}
			if (!Installed(Module::Families)) {
				d.pregnancyRules = false;  // user 3 Nov: the pregnancy rules belong to Families
				d.beastChildrenOwnKind = false;
				d.hatchNpcBabies = false;
				d.babyStages = false;
				d.pregnancyEvents = false;
				d.householdChildren = false;
			}
			if (!Installed(Module::Conversations)) {
				d.voiceLines = false;
				d.familyChats = false;
				d.friendChats = false;
			}
			return d;
		}
	}

	void DetectModules()
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		for (std::size_t i = 0; i < kModulePlugin.size(); ++i) {
			const bool here = data && data->LookupModByName(kModulePlugin[i]);
			installed[i] = here;
			SKSE::log::info("Module {}: {}", kModuleName[i], here ? "installed" : "not installed (its features stay off)");
		}
	}

	bool Installed(Module a_module)
	{
		return installed[static_cast<std::size_t>(a_module)];
	}

	Data Get()
	{
		std::shared_lock lock(guard);
		return Gate(live);
	}

	Data Raw()
	{
		std::shared_lock lock(guard);
		return live;
	}

	void Set(const Data& a_data)
	{
		{
			std::unique_lock lock(guard);
			live = a_data;
		}
		Save();
	}

	Data LoadFrom(const std::string& a_path, const Data& a_base);
	bool SaveTo(const Data& a_data, const std::string& a_path);

	void Load()
	{
		// 0.27.0 rename to ONPCs: the first time, the settings come from the old OStimNPCsReforged.ini (read only, never
		// changed or deleted); the next save writes ONPCs.ini
		std::string path = IniPath();
		std::error_code ec;
		if (!std::filesystem::exists(path, ec)) {
			const auto old = std::filesystem::path(path).parent_path() / "OStimNPCsReforged.ini";
			if (std::filesystem::exists(old, ec)) {
				SKSE::log::info("No ONPCs.ini yet - carrying the settings over from {}", old.string());
				path = old.string();
			}
		}
		Data d = LoadFrom(path, Defaults());
		std::unique_lock lock(guard);
		live = d;
	}

	Data LoadFrom(const std::string& a_path, const Data& a_base)
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		Data       d = a_base;
		const auto& path = a_path;
		if (ini.LoadFile(path.c_str()) == SI_OK) {
			for (const auto& e : kEntries) {
				const auto key = IniKey(e);
				switch (e.kind) {
				case Kind::Bool:
					d.*(e.b) = ini.GetBoolValue(e.section, key.c_str(), d.*(e.b));
					break;
				case Kind::Int:
					d.*(e.i) = static_cast<int>(ini.GetLongValue(e.section, key.c_str(), d.*(e.i)));
					break;
				case Kind::Float:
					d.*(e.f) = static_cast<float>(ini.GetDoubleValue(e.section, key.c_str(), d.*(e.f)));
					break;
				}
			}
			d.affairAfter = std::clamp(d.affairAfter, 1, 10);
			d.familyBlockDays = std::clamp(d.familyBlockDays, 0, 365);
			if (d.weddingGapDays < 1) {
				d.weddingGapDays = 1;  // two weddings on one day at one temple cannot happen
			}
			d.babyCarriedDays = std::clamp(d.babyCarriedDays, 1, 3650);
			if (d.childAppearsDays <= d.babyCarriedDays) {
				SKSE::log::warn("Settings: iChildAppearsDays {} is not after iBabyCarriedDays {} - using {}", d.childAppearsDays, d.babyCarriedDays, d.babyCarriedDays + 1);
				d.childAppearsDays = d.babyCarriedDays + 1;  // the child cannot come home before the baby has gone away
			}
			d.childAppearsDays = std::min(d.childAppearsDays, 7300);
			SKSE::log::info("Settings loaded from {}", path);
		} else {
			SKSE::log::info("No INI at {} - using defaults", path);
		}
		return d;
	}

	bool Save()
	{
		return SaveTo(Raw(), IniPath());
	}

	bool SaveTo(const Data& a_data, const std::string& a_path)
	{
		const Data& d = a_data;
		CSimpleIniA ini;
		ini.SetUnicode();
		const auto& path = a_path;
		ini.LoadFile(path.c_str());  // keep anything else the file already has
		for (const auto& e : kEntries) {
			const auto key = IniKey(e);
			switch (e.kind) {
			case Kind::Bool:
				ini.SetBoolValue(e.section, key.c_str(), d.*(e.b), e.comment);
				break;
			case Kind::Int:
				ini.SetLongValue(e.section, key.c_str(), d.*(e.i), e.comment);
				break;
			case Kind::Float:
				ini.SetDoubleValue(e.section, key.c_str(), d.*(e.f), e.comment);
				break;
			}
		}
		const bool ok = ini.SaveFile(path.c_str()) == SI_OK;
		if (!ok) {
			SKSE::log::error("Could not save {}", path);
		}
		return ok;
	}

	float GetFloat(std::string_view a_key, float a_default)
	{
		const auto* e = Find(a_key);
		if (!e) {
			return a_default;
		}
		const Data d = Get();
		switch (e->kind) {
		case Kind::Float: return d.*(e->f);
		case Kind::Int: return static_cast<float>(d.*(e->i));
		case Kind::Bool: return d.*(e->b) ? 1.0f : 0.0f;
		}
		return a_default;
	}

	bool GetBool(std::string_view a_key, bool a_default)
	{
		const auto* e = Find(a_key);
		if (!e) {
			return a_default;
		}
		const Data d = Get();
		switch (e->kind) {
		case Kind::Bool: return d.*(e->b);
		case Kind::Int: return d.*(e->i) != 0;
		case Kind::Float: return d.*(e->f) != 0.0f;
		}
		return a_default;
	}

	int GetInt(std::string_view a_key, int a_default)
	{
		const auto* e = Find(a_key);
		if (!e) {
			return a_default;
		}
		const Data d = Get();
		switch (e->kind) {
		case Kind::Int: return d.*(e->i);
		case Kind::Float: return static_cast<int>(std::lround(d.*(e->f)));
		case Kind::Bool: return d.*(e->b) ? 1 : 0;
		}
		return a_default;
	}
}
namespace ONPCR::Settings
{
	// ---- Testing mode: fast rates + everything under test switched on; the player's own settings are kept in a backup file
	// and put back when testing mode is turned off. The backup file's existence IS the "testing mode on" flag (survives
	// saves, reloads and restarts).
	std::string TestingBackupPath()
	{
		std::error_code ec;
		const auto      dir = std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins";
		// a testing run started before the rename keeps its backup under the old name: use it until it is restored
		if (const auto old = dir / "OStimNPCsReforged_BeforeTesting.ini"; !std::filesystem::exists(dir / "ONPCs_BeforeTesting.ini", ec) && std::filesystem::exists(old, ec)) {
			return old.string();
		}
		return (dir / "ONPCs_BeforeTesting.ini").string();
	}

	bool TestingMode()
	{
		std::error_code ec;
		return std::filesystem::exists(TestingBackupPath(), ec);
	}

	void StartTesting()
	{
		if (TestingMode()) {
			return;
		}
		const Data mine = Raw();
		if (!SaveTo(mine, TestingBackupPath())) {
			SKSE::log::error("Testing mode: could not write the backup - not switching");
			return;
		}
		Data t = mine;
		// couples form and move up quickly
		t.ladderEnabled = true;
		t.sparkChance = 100.0f;
		t.directDatingShare = 50.0f;
		t.promoteChance = 100.0f;
		t.candidateEncounters = 1;
		t.candidateMinDays = 0;
		t.engagedEncounters = 2;
		t.engagedMinDays = 0;
		t.marriedAfterDays = 1;
		t.weddingGapDays = 1;
		// the off-screen world moves fast
		t.simulationEnabled = true;
		t.dailyChance = 5.0f;
		t.maxNewCouplesPerCommunityPerMonth = 0;
		// everything under test on
		t.voiceLines = true;
		t.voiceCooldownHours = 0.5f;
		t.sleepOvers = true;
		t.dateNights = true;
		t.dateNightChance = 100;
		t.households = true;
		t.realWeddings = true;
		t.lettersEnabled = true;
		t.notifications = 2;
		t.debugNotifications = true;
		Set(t);
		SKSE::log::info("Testing mode ON (backup {})", TestingBackupPath());
	}

	void StopTesting()
	{
		if (!TestingMode()) {
			return;
		}
		const Data mine = LoadFrom(TestingBackupPath(), Defaults());
		Set(mine);
		std::error_code ec;
		std::filesystem::remove(TestingBackupPath(), ec);
		SKSE::log::info("Testing mode OFF - own settings restored");
	}

	// ---- 0.18.0 pace presets
	namespace
	{
		struct PaceValues
		{
			bool  onlyMet;
			float spark;
			float promote;
			int   candidateDays;
			int   engagedDays;
			int   marriedDays;
			int   weddingGap;
			int   dateNight;
			float stray;
		};
		constexpr PaceValues kSlow{ true, 15.0f, 10.0f, 14, 60, 21, 5, 20, 0.1f };
		constexpr PaceValues kDefault{ true, 25.0f, 20.0f, 7, 30, 14, 3, 30, 0.3f };
		constexpr PaceValues kFast{ false, 50.0f, 40.0f, 3, 10, 7, 1, 50, 1.0f };

		const PaceValues& ValuesFor(Pace a_pace)
		{
			switch (a_pace) {
			case Pace::Slow: return kSlow;
			case Pace::Fast: return kFast;
			default: return kDefault;
			}
		}

		bool Same(float a, float b) { return std::fabs(a - b) < 0.05f; }

		bool Matches(const Data& d, const PaceValues& v)
		{
			return d.onlyMetCouples == v.onlyMet && Same(d.sparkChance, v.spark) && Same(d.promoteChance, v.promote) &&
			       d.candidateMinDays == v.candidateDays && d.engagedMinDays == v.engagedDays && d.marriedAfterDays == v.marriedDays &&
			       d.weddingGapDays == v.weddingGap && d.dateNightChance == v.dateNight && Same(d.strayChance, v.stray);
		}

		std::filesystem::path PresetDir()
		{
			std::error_code ec;
			return std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "ONPCs Presets";
		}

		// letters, digits, space, - and _ only, at most 40 characters
		std::string CleanName(const std::string& a_name)
		{
			std::string out;
			for (const char c : a_name) {
				if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') {
					out += c;
				}
			}
			while (!out.empty() && out.back() == ' ') {
				out.pop_back();
			}
			while (!out.empty() && out.front() == ' ') {
				out.erase(out.begin());
			}
			return out.substr(0, 40);
		}
	}

	void ApplyPace(Data& a_data, Pace a_pace)
	{
		const auto& v = ValuesFor(a_pace);
		a_data.onlyMetCouples = v.onlyMet;
		a_data.sparkChance = v.spark;
		a_data.promoteChance = v.promote;
		a_data.candidateMinDays = v.candidateDays;
		a_data.engagedMinDays = v.engagedDays;
		a_data.marriedAfterDays = v.marriedDays;
		a_data.weddingGapDays = v.weddingGap;
		a_data.dateNightChance = v.dateNight;
		a_data.strayChance = v.stray;
	}

	const char* PaceName(const Data& a_data)
	{
		if (Matches(a_data, kSlow)) {
			return "Slow";
		}
		if (Matches(a_data, kDefault)) {
			return "Default";
		}
		if (Matches(a_data, kFast)) {
			return "Fast";
		}
		return "Custom";
	}

	std::vector<std::string> SavedPresets()
	{
		std::vector<std::string> out;
		std::error_code          ec;
		for (const auto& e : std::filesystem::directory_iterator(PresetDir(), ec)) {
			if (e.is_regular_file(ec) && e.path().extension() == ".ini") {
				out.push_back(e.path().stem().string());
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	bool SavePreset(const std::string& a_name)
	{
		const auto name = CleanName(a_name);
		if (name.empty()) {
			return false;
		}
		std::error_code ec;
		std::filesystem::create_directories(PresetDir(), ec);
		const bool ok = SaveTo(Raw(), (PresetDir() / (name + ".ini")).string());
		SKSE::log::info("Preset '{}' saved: {}", name, ok);
		return ok;
	}

	bool LoadPreset(const std::string& a_name)
	{
		const auto      path = PresetDir() / (CleanName(a_name) + ".ini");
		std::error_code ec;
		if (!std::filesystem::exists(path, ec)) {
			return false;
		}
		Set(LoadFrom(path.string(), Raw()));
		SKSE::log::info("Preset '{}' loaded", a_name);
		return true;
	}

	bool DeletePreset(const std::string& a_name)
	{
		std::error_code ec;
		return std::filesystem::remove(PresetDir() / (CleanName(a_name) + ".ini"), ec);
	}
}
