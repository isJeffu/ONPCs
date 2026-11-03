// 0.19.0 couples redesign: the game-couple table, per-person styles, quest watchers and the romance-line switch. Included at the
// end of Ladder.cpp (after Ladder_Import.inc), inside namespace ONPCR::Ladder, so it can use the ladder's private helpers.
// Research and every decision behind the table: Notes and Reports\Reforged - couples redesign plan 2026-10-16.md

	namespace
	{
		struct FormRef
		{
			RE::FormID  local;
			const char* file;
		};

		RE::FormID Resolve(const FormRef& a_ref)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupFormID(a_ref.local, a_ref.file) : 0;
		}

		constexpr const char* kSkyrim = "Skyrim.esm";
		constexpr const char* kDragonborn = "Dragonborn.esm";
		constexpr const char* kBruma = "BSHeartland.esm";
		// 0.19.2 mod-aware couples: mods in the user's list that change a couple (scan: Notes and Reports\Reforged - mod-aware couples scan 2026-10-17.md)
		constexpr const char* kWyrmstooth = "Wyrmstooth.esp";
		constexpr const char* kFdeFaralda = "FDE Faralda.esp";
		constexpr const char* kFdeMjoll = "FDE Mjoll.esp";

		bool PluginLoaded(const char* a_name)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return a_name && dh && (dh->LookupLoadedModByName(a_name) || dh->LookupLoadedLightModByName(a_name));
		}

		// quests that own or move a couple (bit = 1 << key in questDone)
		enum QuestKey : std::uint8_t
		{
			kQNone = 0,
			kQBoundUntilDeath = 1,   // DB05 - Vittoria + Asgeir's wedding
			kQSealingTheDeal = 2,    // FreeformRiften06 - Talen-Jei's ring for Keerava
			kQLovelyLetter = 3,      // FreeformRiverwood01 - Sven or Faendal for Camilla
			kQBookOfLove = 4,        // T02 - Fastred (Bassianus or Klimmek), Calcelmo + Faleen
			kQLoversRequital = 5,    // FreeformRiften02 - Sibbi, Svana and Svidi
			kQUglyLove = 6,          // CYRBrumaMS05 - Dumrag, Atul, Simund, Afanna
			kQCaughtRedHanded = 7,   // FreeformRiften11 - Haelga's lovers exposed
			kQSpreadTheLove = 8,     // FreeformRiften05 - the Temple of Mara pamphlets (matchmakers)
			kQBookOfLoveCalcelmo = 9, // second outcome bit of T02 (Calcelmo's part is its own stage)
			// 10-14 were 0.19.2's built-in mod quest outcomes - since 0.19.3 those are data ("questOutcomes" in OStimNPCsReforged_Couples.json)
		};

		struct QuestInfo
		{
			QuestKey key;
			FormRef  form;
		};
		constexpr QuestInfo kQuests[] = {
			{ kQBoundUntilDeath, { 0x01EA54, kSkyrim } }, { kQSealingTheDeal, { 0x053318, kSkyrim } }, { kQLovelyLetter, { 0x074A83, kSkyrim } },
			{ kQBookOfLove, { 0x0211D5, kSkyrim } }, { kQLoversRequital, { 0x053306, kSkyrim } }, { kQUglyLove, { 0x08AE84, kBruma } },
			{ kQCaughtRedHanded, { 0x041F91, kSkyrim } }, { kQSpreadTheLove, { 0x053309, kSkyrim } },
		};

		// a stage of a quest has been done (the engine's own GetStageDone condition, run on the player)
		bool StageDone(RE::TESQuest* a_quest, std::uint16_t a_stage)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!a_quest || !player) {
				return false;
			}
			RE::TESConditionItem item;
			item.next = nullptr;
			item.data.object = RE::CONDITIONITEMOBJECT::kSelf;
			item.data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetStageDone;
			item.data.functionData.params[0] = a_quest;
			item.data.functionData.params[1] = reinterpret_cast<void*>(static_cast<std::uintptr_t>(a_stage));
			item.data.comparisonValue.f = 1.0f;
			item.data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kEqualTo;
			item.data.flags.isOR = false;
			item.data.flags.global = false;
			RE::ConditionCheckParams params(player, player);
			return item(params);
		}

		RE::TESQuest* QuestOf(QuestKey a_key)
		{
			for (const auto& q : kQuests) {
				if (q.key == a_key) {
					return RE::TESForm::LookupByID<RE::TESQuest>(Resolve(q.form));
				}
			}
			return nullptr;
		}

		// one game couple. For Crush and Thrall, a is the active one (who pines / the vampire). Stage::Casual for a pair where one
		// of them is taken elsewhere becomes an Affair when applied (Haelga + Bolli).
		// 0.19.2: needs = the rule only counts while that plugin is loaded, unless = only while it is NOT loaded (a mod in the list
		// that rewrites a couple). Stage::None with exFrom set = they broke up before the game starts (a = the one who ended it).
		struct CoupleRule
		{
			FormRef     a;
			FormRef     b;
			Stage       stage;
			bool        frozen;
			bool        secret;
			bool        family;
			QuestKey    quest;
			const char* needs = nullptr;
			const char* unless = nullptr;
			Stage       exFrom = Stage::None;
		};

		bool RuleApplies(const CoupleRule& a_rule)
		{
			return (!a_rule.needs || PluginLoaded(a_rule.needs)) && (!a_rule.unless || !PluginLoaded(a_rule.unless));
		}

		// bump when the table changes - saves that had an older table get the new one once (pairs the player touched are skipped)
		// v2 (0.19.1): the same rules, now also for people far away (not persistent, cell not loaded) - a v1 save only gets the missing pairs
		// v3 (0.19.2): mod-aware rules (needs / unless), Wyrmstooth's two marriages; and the quest locks of existing table pairs are
		// put back (the v2 upgrade re-ran the import's lock reset over the table's pairs)
		// v4 (0.21.0): protected couples (OStimNPCsReforged_ProtectedCouples.json) - applied by ApplyProtectedCouples on every load
		constexpr std::uint32_t kTableVersion = 4;

		const std::vector<CoupleRule>& Rules()
		{
			static const std::vector<CoupleRule> rules = {
				// ---- quest-locked (frozen: no step up or down, no cooling, no breakup until the quest moves them) ----
				{ { 0x01327A, kSkyrim }, { 0x01334B, kSkyrim }, Stage::Engaged, true, false, false, kQBoundUntilDeath },   // Vittoria Vici + Asgeir Snow-Shod
				{ { 0x013365, kSkyrim }, { 0x013373, kSkyrim }, Stage::Candidate, true, false, false, kQSealingTheDeal },  // Keerava + Talen-Jei
				{ { 0x01347F, kSkyrim }, { 0x01347B, kSkyrim }, Stage::Crush, true, false, false, kQLovelyLetter },        // Sven -> Camilla
				{ { 0x013480, kSkyrim }, { 0x01347B, kSkyrim }, Stage::Crush, true, false, false, kQLovelyLetter },        // Faendal -> Camilla
				{ { 0x0136BF, kSkyrim }, { 0x0136C1, kSkyrim }, Stage::Candidate, true, false, true, kQBookOfLove },        // Fastred + Bassianus (her parents object)
				{ { 0x0136C2, kSkyrim }, { 0x0136BF, kSkyrim }, Stage::Crush, true, false, false, kQBookOfLove },          // Klimmek -> Fastred
				{ { 0x01338E, kSkyrim }, { 0x013397, kSkyrim }, Stage::Crush, true, false, false, kQBookOfLove },          // Calcelmo -> Faleen
				{ { 0x01337B, kSkyrim }, { 0x01337C, kSkyrim }, Stage::Candidate, true, false, false, kQLoversRequital },   // Sibbi + Svana (he is in jail)
				{ { 0x003A1E, kBruma }, { 0x08AEC5, kBruma }, Stage::Married, true, false, false, kQUglyLove },             // Dumrag + Atul
				{ { 0x08AEC5, kBruma }, { 0x08AE79, kBruma }, Stage::Affair, true, false, false, kQUglyLove },              // Atul + Simund
				{ { 0x08AE7B, kBruma }, { 0x08AE79, kBruma }, Stage::Crush, true, false, false, kQUglyLove },               // Afanna -> Simund
				{ { 0x013B9B, kSkyrim }, { 0x013BB1, kSkyrim }, Stage::Candidate, true, true, true, kQNone },               // Olfina + Jon (feuding clans; FDE Olfina may end it - questOutcomes)
				// ---- free couples (normal ladder) ----
				{ { 0x018294, kDragonborn }, { 0x0182AC, kDragonborn }, Stage::Candidate, false, true, true, kQNone },  // Dreyla Alor + Captain Veleth (Fethis forbids it)
				{ { 0x013265, kSkyrim }, { 0x013274, kSkyrim }, Stage::Candidate, false, true, false, kQNone },          // Bryling + Falk Firebeard (secret)
				{ { 0x01A6B7, kSkyrim }, { 0x01A6B9, kSkyrim }, Stage::Candidate, false, false, false, kQNone },         // Brina Merilis + Horik Halfhand
				{ { 0x01412A, kSkyrim }, { 0x014149, kSkyrim }, Stage::Candidate, false, false, false, kQNone },         // Elda Early-Dawn + Brunwulf Free-Winter
				{ { 0x013349, kSkyrim }, { 0x0371D7, kSkyrim }, Stage::Candidate, false, false, false, kQNone },         // Anuriel + Unmid Snow-Shod
				{ { 0x013656, kSkyrim }, { 0x013652, kSkyrim }, Stage::Candidate, false, false, false, kQNone },         // Tekla + Solaf
				{ { 0x01328C, kSkyrim }, { 0x01325F, kSkyrim }, Stage::Candidate, false, false, false, kQNone },         // Jala + Ahtar
				{ { 0x0B8827, kSkyrim }, { 0x013380, kSkyrim }, Stage::Candidate, false, false, false, kQNone },         // Tonilia + Vekel the Man
				{ { 0x02C925, kSkyrim }, { 0x02C926, kSkyrim }, Stage::Candidate, false, false, false, kQNone },         // Severio Pelagia + Nimriel
				{ { 0x0D25F9, kBruma }, { 0x0D25F8, kBruma }, Stage::Engaged, false, false, false, kQNone },             // Dar'Seel + Umog ("my fiancee")
				{ { 0x013661, kSkyrim }, { 0x01412B, kSkyrim }, Stage::Casual, false, false, false, kQNone },            // Kjeld the Younger + Susanna the Wicked
				{ { 0x01328F, kSkyrim }, { 0x013297, kSkyrim }, Stage::Casual, false, false, false, kQNone },            // Jorn + Lisette
				{ { 0x001112, kBruma }, { 0x002F20, kBruma }, Stage::Casual, false, false, false, kQNone },              // Hulgard + Eddvia Jucani
				{ { 0x002D6F, kBruma }, { 0x02DAFD, kBruma }, Stage::Casual, false, false, false, kQNone },              // Harald Burdsson + Ananril
				{ { 0x01A670, kSkyrim }, { 0x013BAB, kSkyrim }, Stage::Casual, false, false, false, kQNone },            // Mikael + Ysolda
				// free spirits' lovers (an Affair where the other one is taken)
				{ { 0x01335F, kSkyrim }, { 0x01334F, kSkyrim }, Stage::Casual, false, false, false, kQCaughtRedHanded }, // Haelga + Brand-Shei
				{ { 0x01335F, kSkyrim }, { 0x013370, kSkyrim }, Stage::Casual, false, false, false, kQCaughtRedHanded }, // Haelga + Indaryn
				{ { 0x01335F, kSkyrim }, { 0x013351, kSkyrim }, Stage::Casual, false, false, false, kQCaughtRedHanded }, // Haelga + Hofgrir Horse-Crusher
				{ { 0x01335F, kSkyrim }, { 0x01334E, kSkyrim }, Stage::Casual, false, false, false, kQCaughtRedHanded }, // Haelga + Bolli (married to Nivenor -> affair)
				{ { 0x01335F, kSkyrim }, { 0x013362, kSkyrim }, Stage::Casual, false, false, false, kQNone },            // Haelga + Hemming Black-Briar
				{ { 0x01C23E, kSkyrim }, { 0x01C197, kSkyrim }, Stage::Casual, false, false, false, kQNone, nullptr, kFdeFaralda },  // Sergius + Faralda
				{ { 0x01C23E, kSkyrim }, { 0x01C19B, kSkyrim }, Stage::Casual, false, false, false, kQNone, nullptr, kFdeFaralda },  // Sergius + Nirya
				// FDE Faralda: "After I broke things off with Sergius, he started a new flame with that harlot" - Faralda ended it long
				// ago, Sergius chose Nirya (and is no free spirit there, see kStyles)
				{ { 0x01C197, kSkyrim }, { 0x01C23E, kSkyrim }, Stage::None, false, false, false, kQNone, kFdeFaralda, nullptr, Stage::Casual },  // Faralda -x- Sergius
				{ { 0x01C23E, kSkyrim }, { 0x01C19B, kSkyrim }, Stage::Candidate, false, false, false, kQNone, kFdeFaralda },                     // Sergius + Nirya, dating
				// Wyrmstooth: husband and wife by their own lines ("my husband, Balmir"), but the game record is filed as boss/employee
				{ { 0x272C15, kWyrmstooth }, { 0x272C17, kWyrmstooth }, Stage::Married, false, false, false, kQNone },  // Balmir + Yngfa
				{ { 0x00A049, kWyrmstooth }, { 0x274F39, kWyrmstooth }, Stage::Married, false, false, false, kQNone },  // Ja'Shavi-Dar + Shomara
				// affairs
				{ { 0x019A20, kSkyrim }, { 0x019A22, kSkyrim }, Stage::Affair, false, false, false, kQNone },            // Olda (Horgeir's wife) + Lodvar, his brother
				{ { 0x0133BA, kSkyrim }, { 0x0133A5, kSkyrim }, Stage::Affair, false, false, false, kQNone },            // Thonar Silver-Blood + Lisbet
				// crushes
				{ { 0x014129, kSkyrim }, { 0x014134, kSkyrim }, Stage::Crush, false, false, false, kQNone },             // Viola Giordano -> Captain Lonely-Gale
				{ { 0x0132A7, kSkyrim }, { 0x0132AE, kSkyrim }, Stage::Crush, false, false, false, kQNone },             // Sorex Vinius -> Vivienne Onis
				{ { 0x013346, kSkyrim }, { 0x01336B, kSkyrim }, Stage::Crush, false, false, false, kQNone, nullptr, kFdeMjoll },  // Aerin -> Mjoll the Lioness
				// 0.19.3: with FDE Mjoll his crush is held until her quest decides it (questOutcomes: he wins her, or Fertility Adventures ends it)
				{ { 0x013346, kSkyrim }, { 0x01336B, kSkyrim }, Stage::Crush, true, false, false, kQNone, kFdeMjoll },            // Aerin -> Mjoll, held
				{ { 0x01A670, kSkyrim }, { 0x013B99, kSkyrim }, Stage::Crush, false, false, false, kQNone },             // Mikael -> Carlotta Valentia (she never returns it)
				// thrall
				{ { 0x0135E6, kSkyrim }, { 0x0135F0, kSkyrim }, Stage::Thrall, false, false, false, kQNone },            // Alva (vampire) -> Hroggar
			};
			return rules;
		}

		struct StyleRule
		{
			FormRef     form;
			Style       style;
			const char* unless = nullptr;  // 0.19.2: not while this plugin is loaded
		};
		constexpr StyleRule kStyles[] = {
			{ { 0x01335F, kSkyrim }, Style::FreeSpirit },               // Haelga
			{ { 0x01C23E, kSkyrim }, Style::FreeSpirit, kFdeFaralda },  // Sergius Turrianus (FDE Faralda: he settled on Nirya)
			{ { 0x01A670, kSkyrim }, Style::FreeSpirit },  // Mikael
			{ { 0x02C6C7, kSkyrim }, Style::Never },       // Arngeir
			{ { 0x02C6CE, kSkyrim }, Style::Never },       // Borri
			{ { 0x02C6CC, kSkyrim }, Style::Never },       // Einarth
			{ { 0x02C6CA, kSkyrim }, Style::Never },       // Wulfgar
		};

		std::unordered_map<RE::FormID, Style> jsonStyles;  // OStimNPCsReforged_Couples.json "styles" (other mods' NPCs), filled once per session
		// 0.19.3: OStimNPCsReforged_Couples.json "questOutcomes" - when a quest reaches a stage (or completes), a couple changes
		struct QuestOutcome
		{
			std::string  id;                       // saved once applied (record LADZ) - never applied twice
			RE::FormID   quest{ 0 };
			std::int32_t stage{ -1 };              // -1 = when the quest completes
			RE::FormID   a{ 0 };
			RE::FormID   b{ 0 };
			Stage        together{ Stage::None };  // seeing / dating / engaged / married; None = the pair ends
			bool         crushOnly{ false };       // "crushEnds": only a crush ends (a couple they became since is left alone)
			std::uint8_t endedBy{ 0 };             // 1 = a ended it, 2 = b
		};
		std::vector<QuestOutcome> jsonOutcomes;
		void ApplyJsonCouples();                            // below: the JSON's couples and styles, once per session

		Style StyleOfUnlocked(RE::FormID a_base)
		{
			if (const auto it = styleChoice.find(a_base); it != styleChoice.end()) {
				return it->second;
			}
			if (const auto it = jsonStyles.find(a_base); it != jsonStyles.end()) {
				return it->second;
			}
			static const std::unordered_map<RE::FormID, Style> builtIn = [] {
				std::unordered_map<RE::FormID, Style> m;
				for (const auto& s : kStyles) {
					if (s.unless && PluginLoaded(s.unless)) {
						continue;
					}
					if (const auto id = Resolve(s.form)) {
						m[id] = s.style;
					}
				}
				return m;
			}();
			const auto it = builtIn.find(a_base);
			return it != builtIn.end() ? it->second : Style::Normal;
		}

		// unique, alive, placed AND in memory (base -> ref). 0.19.1: a placed NPC that is not persistent is only in memory while their
		// cell is loaded (Dreyla Alor, Asgeir) - a_dead (optional) collects the unique NPCs in memory that are dead, so a hand-picked
		// list can tell "dead" from "far away"
		std::unordered_map<RE::FormID, RE::FormID> PlacedPeople(std::unordered_set<RE::FormID>* a_dead = nullptr)
		{
			std::unordered_map<RE::FormID, RE::FormID> out;
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			const RE::BSReadLockGuard l{ lock };
			if (!map) {
				return out;
			}
			for (auto& entry : *map) {
				auto* actor = entry.second ? entry.second->As<RE::Actor>() : nullptr;
				auto* base = actor ? actor->GetActorBase() : nullptr;
				if (base && base->IsUnique() && !actor->IsDeleted()) {
					if (!actor->IsDead()) {
						out.try_emplace(base->GetFormID(), actor->GetFormID());
					} else if (a_dead) {
						a_dead->insert(base->GetFormID());
					}
				}
			}
			if (a_dead) {
				for (const auto& [b, r] : out) {
					a_dead->erase(b);  // a second, living reference wins
				}
			}
			return out;
		}

		// 0.19.1: a hand-picked person (the table, the Couples json) may be a couple while far away - only a known death rules them out
		bool UsablePerson(RE::FormID a_base, const std::unordered_map<RE::FormID, RE::FormID>& a_placed, const std::unordered_set<RE::FormID>& a_dead)
		{
			return a_base && !a_dead.contains(a_base) && (a_placed.contains(a_base) || RE::TESForm::LookupByID<RE::TESNPC>(a_base));
		}

		void RememberRefs(RE::FormID a_x, RE::FormID a_y, const std::unordered_map<RE::FormID, RE::FormID>& a_placed)
		{
			for (const auto id : { a_x, a_y }) {
				if (const auto it = a_placed.find(id); it != a_placed.end()) {
					knownRefs[id] = it->second;
				}
			}
		}

		// guard held. Applies one rule: creates the pair if needed, sets stage and flags. Returns true when anything changed.
		bool ApplyRule(const CoupleRule& a_rule, RE::FormID a_x, RE::FormID a_y, float a_now)
		{
			const Key key = MakeKey(a_x, a_y);
			Pair*     p = Find(key);
			if (p && p->byPlayer) {
				return false;  // the player set this one by hand - leave it
			}
			bool created = false;
			if (!p) {
				Pair fresh;
				fresh.a = key.a;
				fresh.b = key.b;
				fresh.firstDay = a_now;
				fresh.lastDay = a_now;
				fresh.stageDay = a_now;
				fresh.createdByMod = false;
				fresh.imported = true;
				pairs.push_back(fresh);
				p = &pairs.back();
				created = true;
			}
			if (a_rule.stage == Stage::None && a_rule.exFrom != Stage::None) {
				// 0.19.2: an old break-up (a mod's backstory) - exes, heartbreak long over; a = the one who ended it
				const std::uint8_t by = key.a == a_x ? 1 : 2;
				if (!created && p->stage == Stage::None && p->endedFrom == static_cast<std::uint8_t>(a_rule.exFrom) && p->endedBy == by) {
					return false;
				}
				p->stage = Stage::None;
				p->stageDay = a_now - 365.0f;
				p->endedFrom = static_cast<std::uint8_t>(a_rule.exFrom);
				p->endedBy = by;
				p->closeness = 0.0f;
				p->frozen = false;
				p->protectedVanilla = false;
				p->secret = false;
				p->familyBlock = false;
				p->quest = a_rule.quest;
				SKSE::log::info("Couple table: {} + {} -> exes ({} ended it long ago){}", NpcName(a_x), NpcName(a_y), NpcName(a_x),
					a_rule.needs ? std::format(" - {}", a_rule.needs) : std::string());
				return true;
			}
			Stage stage = a_rule.stage;
			if (stage == Stage::Casual) {
				Pair* ox = CommittedPairOfUnlocked(a_x);
				Pair* oy = CommittedPairOfUnlocked(a_y);
				if ((ox && ox != p) || (oy && oy != p)) {
					stage = Stage::Affair;  // the free spirit's lover is taken: it runs behind their partner's back
				}
			}
			const bool activeA = key.a == a_x;
			const bool same = !created && p->stage == stage && p->frozen == a_rule.frozen && p->secret == a_rule.secret &&
			                  p->familyBlock == a_rule.family && p->quest == a_rule.quest && (!(stage == Stage::Crush || stage == Stage::Thrall) || p->activeA == activeA);
			if (same) {
				return false;
			}
			if (p->stage != stage) {
				p->stage = stage;
				p->stageDay = a_now;
				p->endedFrom = 0;
				p->endedBy = 0;
			}
			p->frozen = a_rule.frozen;
			p->protectedVanilla = a_rule.frozen;
			p->secret = a_rule.secret;
			p->familyBlock = a_rule.family;
			p->quest = a_rule.quest;
			p->activeA = activeA;
			p->closeness = std::max(p->closeness, stage == Stage::Married ? 100.0f : (stage == Stage::Crush || stage == Stage::Thrall) ? 50.0f : 75.0f);
			if (stage == Stage::Married && !p->home) {
				p->home = ChooseHome(*p);  // 0.21.1: a marriage the table, a Couples.json line or the watcher makes gets its shared home like any other
			}
			SKSE::log::info("Couple table: {} + {} -> {}{}{}{}{}", NpcName(a_x), NpcName(a_y), StageName(stage), a_rule.frozen ? ", quest-locked" : "",
				a_rule.secret ? ", secret" : "", a_rule.family ? ", family disapproves" : "", a_rule.needs ? std::format(" - {}", a_rule.needs) : std::string());
			return true;
		}

		// 0.19.2: the plugins the built-in table names in needs / unless that are loaded right now (sorted, lower case)
		std::vector<std::string> LoadedGatePlugins()
		{
			std::vector<std::string> out;
			for (const auto& r : Rules()) {
				for (const char* g : { r.needs, r.unless }) {
					if (g && PluginLoaded(g)) {
						std::string s(g);
						std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						out.push_back(std::move(s));
					}
				}
			}
			std::sort(out.begin(), out.end());
			out.erase(std::unique(out.begin(), out.end()), out.end());
			return out;
		}

		bool SameGate(const char* a_rule, const std::string& a_lower)
		{
			return a_rule && _stricmp(a_rule, a_lower.c_str()) == 0;
		}
	}

	bool CoupleTableDue()
	{
		const auto gates = LoadedGatePlugins();
		std::scoped_lock lock(guard);
		return gameImportDone && (tableApplied < kTableVersion || gates != modRulesOn);  // 0.19.2: or a mod with rules was added / removed
	}

	std::size_t ApplyCoupleTable()
	{
		if (!Settings::Get().ladderEnabled) {
			return 0;
		}
		std::unordered_set<RE::FormID> dead;
		const auto  placed = PlacedPeople(&dead);  // outside the ladder lock (it takes the form map lock)
		const auto  lists = LoadCoupleLists();
		const auto  gates = LoadedGatePlugins();
		const float now = DaysNow();
		std::size_t changed = 0;
		std::scoped_lock lock(guard);
		auto decided = [&](const CoupleRule& r) { return r.quest != kQNone && (questDone & (1u << static_cast<std::uint32_t>(r.quest))) != 0; };
		const std::uint32_t before = tableApplied;
		// 1. "protected" now means quest-locked only: imported couples that were locked just because one of them is in some quest
		// (Brand-Shei's Distant Memories), or because the game had a courting record, go back to the normal ladder - unless the
		// built-in or the JSON quest-couple list owns them (those stay locked as before). 0.19.2: only on the first table run - run
		// again (v2 upgrade) it overwrote the table's own locks (Camilla's suitors unlocked, Brina + Horik locked)
		if (before == 0) {
			for (auto& p : pairs) {
				if (!p.imported || p.byPlayer) {
					continue;
				}
				const bool listed = InList(lists.quest, NpcName(p.a), NpcName(p.b)) && !InList(lists.free, NpcName(p.a), NpcName(p.b));
				if (listed != p.frozen || listed != p.protectedVanilla) {
					p.frozen = listed;
					p.protectedVanilla = listed;
					++changed;
				}
			}
		}
		// 2. the table. A save that had an older table already has every pair it could make (and they may have moved on since) -
		// an upgrade only adds the missing pairs (v2: people who were far away, v3: Wyrmstooth's marriages)
		if (before < kTableVersion) {
			const bool onlyMissing = before >= 1;
			for (const auto& r : Rules()) {
				if (decided(r) || !RuleApplies(r)) {
					continue;  // its quest already decided this couple - never put it back; or a mod in the list rewrites it
				}
				const RE::FormID x = Resolve(r.a), y = Resolve(r.b);
				if (!UsablePerson(x, placed, dead) || !UsablePerson(y, placed, dead)) {
					continue;  // a plugin missing, or one of them dead
				}
				if (onlyMissing) {
					// 0.19.2: put the table's quest lock back on a pair it made (see 1.) - stage and everything else stay as they are
					if (Pair* p = Find(MakeKey(x, y))) {
						// 0.19.3: only while the pair is still at the table's stage - one a quest moved on (FDE Olfina's break-up) stays free
						if (!p->byPlayer && p->imported && p->stage == r.stage && (p->frozen != r.frozen || (r.frozen && p->quest != r.quest))) {
							p->frozen = r.frozen;
							p->protectedVanilla = r.frozen;
							if (r.frozen) {
								p->quest = r.quest;
							}
							SKSE::log::info("Couple table: {} + {} {}", NpcName(x), NpcName(y), r.frozen ? "quest-locked again" : "back on the normal ladder");
							++changed;
						}
						continue;
					}
				}
				RememberRefs(x, y, placed);
				if (ApplyRule(r, x, y, now)) {
					++changed;
				}
			}
		}
		// 3. 0.19.2 mod-aware rules on a save that already had a table: a mod added since -> its rules win over the plain game's for
		// those pairs (never a pair the player set by hand); a mod removed since -> the plain game's rules come back for them
		if (before >= 1) {
			for (const auto& g : gates) {
				if (std::find(modRulesOn.begin(), modRulesOn.end(), g) != modRulesOn.end()) {
					continue;
				}
				SKSE::log::info("Couple table: {} is in the load order - its couple rules apply now", g);
				for (const auto& r : Rules()) {
					const RE::FormID x = Resolve(r.a), y = Resolve(r.b);
					if (SameGate(r.needs, g) && RuleApplies(r) && !decided(r) && UsablePerson(x, placed, dead) && UsablePerson(y, placed, dead)) {
						RememberRefs(x, y, placed);
						changed += ApplyRule(r, x, y, now) ? 1 : 0;
					}
				}
			}
			for (const auto& g : modRulesOn) {
				if (std::find(gates.begin(), gates.end(), g) != gates.end()) {
					continue;
				}
				SKSE::log::info("Couple table: {} left the load order - the game's own couple rules apply again", g);
				for (const auto& r : Rules()) {
					const RE::FormID x = Resolve(r.a), y = Resolve(r.b);
					if (SameGate(r.unless, g) && RuleApplies(r) && !decided(r) && UsablePerson(x, placed, dead) && UsablePerson(y, placed, dead)) {
						RememberRefs(x, y, placed);
						changed += ApplyRule(r, x, y, now) ? 1 : 0;
					}
				}
			}
		}
		modRulesOn = gates;
		tableApplied = kTableVersion;
		SKSE::log::info("Couple table v{} (was v{}): {} pair(s) changed or added; mod rules on: {}", kTableVersion, before, changed,
			gates.empty() ? std::string("none") : [&] { std::string s; for (const auto& g : gates) { s += (s.empty() ? "" : ", ") + g; } return s; }());
		if (changed) {
			Chronicle(std::format("Couples updated from the game's own stories: {} changed or added", changed), now);
		}
		return changed;
	}

	Style GetStyle(RE::FormID a_base)
	{
		std::scoped_lock lock(guard);
		return StyleOfUnlocked(a_base);
	}

	void SetStyle(RE::FormID a_base, Style a_style)
	{
		std::scoped_lock lock(guard);
		styleChoice[a_base] = a_style;
		SKSE::log::info("Ladder: {} is now {}", NpcName(a_base), a_style == Style::FreeSpirit ? "a free spirit" : a_style == Style::Never ? "never in love" : "normal");
	}

	void MarkByPlayer(RE::FormID a_a, RE::FormID a_b)
	{
		std::scoped_lock lock(guard);
		if (Pair* p = Find(MakeKey(a_a, a_b))) {
			p->byPlayer = true;
			p->frozen = false;  // the player took over: the quest lock no longer applies
		}
	}

	// ---- quest outcomes ----
	namespace
	{
		RE::Actor* ActorOf(RE::FormID a_base)
		{
			return RE::TESForm::LookupByID<RE::Actor>(RefOf(a_base));
		}

		// the actor an alias of a quest holds right now (by alias name), nullptr if empty
		RE::Actor* AliasActor(RE::TESQuest* a_quest, std::string_view a_name)
		{
			if (!a_quest) {
				return nullptr;
			}
			for (auto* alias : a_quest->aliases) {
				if (!alias || _stricmp(alias->aliasName.c_str(), a_name.data()) != 0) {
					continue;
				}
				if (auto* ra = skyrim_cast<RE::BGSRefAlias*>(alias)) {
					return ra->GetActorReference();
				}
			}
			return nullptr;
		}

		// guard held: unlock a pair the quest released (it lives on the normal ladder from now on)
		void Release(Pair& a_p)
		{
			a_p.frozen = false;
			a_p.protectedVanilla = false;
			a_p.quest = kQNone;
		}

		// guard held: a winner and a loser of a suitor quest - the winner dates the one they courted, the loser is jilted
		void Decide(Pair* a_win, Pair* a_lose, float a_now)
		{
			if (a_win) {
				Release(*a_win);
				Promote(*a_win, Stage::Candidate, a_now);
			}
			if (a_lose) {
				Release(*a_lose);
				a_lose->stage = Stage::Jilted;
				a_lose->endedFrom = static_cast<std::uint8_t>(Stage::Candidate);  // three weeks of heartbreak
				a_lose->endedBy = 0;
				a_lose->stageDay = a_now;
				a_lose->closeness = 0.0f;
				Chronicle(std::format("{} has lost {} to someone else", NpcName(a_lose->activeA ? a_lose->a : a_lose->b), NpcName(a_lose->activeA ? a_lose->b : a_lose->a)), a_now);
			}
		}

		Pair* FindRule(QuestKey a_quest, RE::FormID a_x, RE::FormID a_y)
		{
			if (!a_x || !a_y) {
				return nullptr;
			}
			Pair* p = Find(MakeKey(a_x, a_y));
			return p && p->quest == a_quest && !p->byPlayer ? p : nullptr;
		}

		using Placed = std::unordered_map<RE::FormID, RE::FormID>;

		// 0.19.3 (guard held): a mod puts two people together. A pair the player set by hand, or one already further along, is left
		// alone; so is anyone married or quest-locked to someone else. Any other relationship that would clash ends first (dating /
		// engaged elsewhere; for dating or more also seeing someone) - nobody ends up dating two people. Returns true when it acted.
		bool BringTogether(RE::FormID a_x, RE::FormID a_y, Stage a_stage, const std::string& a_why, const Placed& a_placed,
			const std::unordered_set<RE::FormID>& a_dead, float a_now)
		{
			if (!UsablePerson(a_x, a_placed, a_dead) || !UsablePerson(a_y, a_placed, a_dead)) {
				return false;
			}
			Pair* p = Find(MakeKey(a_x, a_y));
			if (p && (p->byPlayer || !(IsEndedStage(p->stage) || p->stage == Stage::Crush || p->stage < a_stage))) {
				return false;
			}
			auto involves = [&](const Pair& o) { return &o != p && (o.a == a_x || o.b == a_x || o.a == a_y || o.b == a_y); };
			auto clashes = [&](const Pair& o) {
				return o.stage == Stage::Candidate || o.stage == Stage::Engaged || (a_stage >= Stage::Candidate && o.stage == Stage::Casual);
			};
			for (const auto& o : pairs) {
				if (involves(o) && (o.stage == Stage::Married || (clashes(o) && (o.frozen || o.byPlayer)))) {
					SKSE::log::info("Mod change: {} + {} not brought together ({}) - {} + {} is married, quest-locked or set by hand", NpcName(a_x),
						NpcName(a_y), a_why, NpcName(o.a), NpcName(o.b));
					return false;
				}
			}
			for (auto& o : pairs) {
				if (involves(o) && clashes(o)) {
					const RE::FormID leaver = (o.a == a_x || o.a == a_y) ? o.a : o.b;
					Breakup(o, a_now, leaver == o.a ? 1 : 2);
					SKSE::log::info("Mod change: {} + {} ended - {} is with someone new ({})", NpcName(o.a), NpcName(o.b), NpcName(leaver), a_why);
				}
			}
			if (p) {
				Release(*p);
				Promote(*p, a_stage, a_now);
			} else {
				RememberRefs(a_x, a_y, a_placed);
				const CoupleRule rule{ {}, {}, a_stage, false, false, false, kQNone };
				ApplyRule(rule, a_x, a_y, a_now);
				Chronicle(StagePhrase(NpcName(a_x), NpcName(a_y), a_stage), a_now);
			}
			SKSE::log::info("Mod change: {} + {} -> {} ({})", NpcName(a_x), NpcName(a_y), StageName(a_stage), a_why);
			return true;
		}

		// 0.19.3 (guard held): a mod ends a pair; a_by = the one who ended it (0 = neither). Returns true when it acted.
		bool EndPairByMod(RE::FormID a_x, RE::FormID a_y, RE::FormID a_by, bool a_onlyCrush, const std::string& a_why, float a_now)
		{
			Pair* p = a_x && a_y ? Find(MakeKey(a_x, a_y)) : nullptr;
			if (!p || p->byPlayer || IsEndedStage(p->stage) || (a_onlyCrush && p->stage != Stage::Crush)) {
				return false;
			}
			Release(*p);
			Breakup(*p, a_now, a_by == 0 ? 0 : a_by == p->a ? 1 : 2);
			SKSE::log::info("Mod change: {} + {} ended ({})", NpcName(a_x), NpcName(a_y), a_why);
			return true;
		}
	}

	void WatchQuests()
	{
		if (!Settings::Get().ladderEnabled) {
			return;
		}
		ApplyJsonCouples();  // once per session: couples and styles other mods' NPCs get from OStimNPCsReforged_Couples.json
		const float now = DaysNow();
		auto        bit = [](QuestKey k) { return 1u << static_cast<std::uint32_t>(k); };
		// 0.19.3: other mods' quest outcomes come from OStimNPCsReforged_Couples.json "questOutcomes" (data, no rebuild). Which ones
		// are due is worked out before the ladder lock (quest checks, and finding people takes the form map lock)
		std::vector<const QuestOutcome*> due;
		{
			std::vector<std::string> doneIds;
			{
				std::scoped_lock l(guard);
				doneIds = outcomesDone;
			}
			for (const auto& o : jsonOutcomes) {
				if (std::find(doneIds.begin(), doneIds.end(), o.id) != doneIds.end()) {
					continue;
				}
				auto* q = RE::TESForm::LookupByID<RE::TESQuest>(o.quest);
				if (q && (o.stage < 0 ? q->IsCompleted() : StageDone(q, static_cast<std::uint16_t>(o.stage)))) {
					due.push_back(&o);
				}
			}
		}
		std::unordered_set<RE::FormID>             dead;
		std::unordered_map<RE::FormID, RE::FormID> placed;
		if (std::any_of(due.begin(), due.end(), [](const QuestOutcome* o) { return o->together != Stage::None; })) {
			placed = PlacedPeople(&dead);
		}
		std::scoped_lock lock(guard);
		auto done = [&](QuestKey k) { return (questDone & bit(k)) != 0; };
		auto mark = [&](QuestKey k) { questDone |= bit(k); };

		// Sealing the Deal: Talen-Jei has the amethysts - they are engaged (still quest-locked: the game never marries them)
		if (!done(kQSealingTheDeal)) {
			if (auto* q = QuestOf(kQSealingTheDeal); q && q->IsCompleted()) {
				if (Pair* p = FindRule(kQSealingTheDeal, Resolve({ 0x013365, kSkyrim }), Resolve({ 0x013373, kSkyrim })); p && p->stage == Stage::Candidate) {
					Promote(*p, Stage::Engaged, now);
				}
				mark(kQSealingTheDeal);
			}
		}
		// A Lovely Letter: Camilla turns against the loser (rank -1); the quest runs its stages in order and keeps the last one
		if (!done(kQLovelyLetter)) {
			if (auto* q = QuestOf(kQLovelyLetter)) {
				const auto stage = q->GetCurrentStageID();  // 210 / 240 / 310 = Faendal won, 220 / 230 / 320 = Sven won
				const bool svenLost = stage == 210 || stage == 240 || stage == 310;
				const bool faendalLost = stage == 220 || stage == 230 || stage == 320;
				if (svenLost != faendalLost) {
					const RE::FormID camilla = Resolve({ 0x01347B, kSkyrim });
					Pair*            sven = FindRule(kQLovelyLetter, Resolve({ 0x01347F, kSkyrim }), camilla);
					Pair*            faendal = FindRule(kQLovelyLetter, Resolve({ 0x013480, kSkyrim }), camilla);
					Decide(svenLost ? faendal : sven, svenLost ? sven : faendal, now);
					mark(kQLovelyLetter);
				}
			}
		}
		// The Book of Love (stages run in order): Fastred's part ends at stage 30 - the boy she chose is the FastredsBoy alias; Calcelmo's at 70
		if (auto* q = QuestOf(kQBookOfLove)) {
			if (!done(kQBookOfLove) && (q->GetCurrentStageID() >= 30 || q->IsCompleted())) {
				const RE::FormID fastred = Resolve({ 0x0136BF, kSkyrim });
				const RE::FormID bassianus = Resolve({ 0x0136C1, kSkyrim });
				const RE::FormID klimmek = Resolve({ 0x0136C2, kSkyrim });
				auto*            boy = AliasActor(q, "FastredsBoy");
				const RE::FormID chosen = boy && boy->GetActorBase() ? boy->GetActorBase()->GetFormID() : 0;
				Pair*            withB = FindRule(kQBookOfLove, fastred, bassianus);
				Pair*            withK = FindRule(kQBookOfLove, klimmek, fastred);
				if (chosen == bassianus) {
					Decide(withB, withK, now);
					if (withB) {
						withB->familyBlock = false;  // they left for Riften together - her parents no longer stop them
					}
				} else if (chosen == klimmek) {
					if (withK) {
						withK->activeA = true;
					}
					Decide(withK, withB, now);
				} else if (withB) {
					Release(*withB);  // could not tell who she chose - both live on the normal ladder
					if (withK) {
						Release(*withK);
					}
					SKSE::log::warn("Book of Love: Fastred's choice not found (alias empty) - her couples are unlocked unchanged");
				}
				mark(kQBookOfLove);
			}
			if (!done(kQBookOfLoveCalcelmo) && (q->GetCurrentStageID() >= 70 || q->IsCompleted())) {
				if (Pair* p = FindRule(kQBookOfLove, Resolve({ 0x01338E, kSkyrim }), Resolve({ 0x013397, kSkyrim }))) {
					Decide(p, nullptr, now);  // Calcelmo and Faleen confess their love
				}
				mark(kQBookOfLoveCalcelmo);
			}
		}
		// The Lover's Requital ends: Sibbi and Svana are no longer the quest's
		if (!done(kQLoversRequital)) {
			if (auto* q = QuestOf(kQLoversRequital); q && q->IsCompleted()) {
				if (Pair* p = FindRule(kQLoversRequital, Resolve({ 0x01337B, kSkyrim }), Resolve({ 0x01337C, kSkyrim }))) {
					Release(*p);
				}
				mark(kQLoversRequital);
			}
		}
		// Ugly Love ends: whoever is still alive lives on the ladder; Simund dead = the affair is over (death widows the marriage itself)
		if (!done(kQUglyLove)) {
			if (auto* q = QuestOf(kQUglyLove); q && q->IsCompleted()) {
				const RE::FormID simund = Resolve({ 0x08AE79, kBruma });
				auto*            simundRef = ActorOf(simund);
				for (auto& p : pairs) {
					if (p.quest != kQUglyLove || p.byPlayer) {
						continue;
					}
					Release(p);
					if ((p.a == simund || p.b == simund) && (!simundRef || simundRef->IsDead()) && IsSideStage(p.stage)) {
						p.stage = Stage::None;
						p.stageDay = now;
					}
				}
				mark(kQUglyLove);
			}
		}
		// Caught Red Handed: Haelga's lovers are exposed - every affair of hers comes out, and their partners hear of it
		if (!done(kQCaughtRedHanded)) {
			if (auto* q = QuestOf(kQCaughtRedHanded); q && q->IsCompleted()) {
				for (auto& p : pairs) {
					if (p.quest != kQCaughtRedHanded) {
						continue;
					}
					p.quest = kQNone;
					if (p.stage == Stage::Affair && !p.exposed) {
						p.exposed = true;
						for (const RE::FormID lover : { p.a, p.b }) {
							if (Pair* main = CommittedPairOfUnlocked(lover); main && !main->frozen) {
								main->closeness = std::max(0.0f, main->closeness - 30.0f);
								if (news.size() < 200) {
									news.push_back({ 6, lover, main->a == lover ? main->b : main->a });
								}
							}
						}
						Chronicle(std::format("Word is out: {} and {} have been seeing each other", NpcName(p.a), NpcName(p.b)), now);
					}
				}
				mark(kQCaughtRedHanded);
			}
		}
		// Spread the Love: the Temple of Mara's message goes round Riften - sparks there are likelier for a month
		if (!done(kQSpreadTheLove)) {
			if (auto* q = QuestOf(kQSpreadTheLove); q && q->IsCompleted()) {
				spreadLoveDay = now;
				Chronicle("The Temple of Mara's pamphlets are going round Riften - love is in the air there this month", now);
				mark(kQSpreadTheLove);
			}
		}
		// Bound Until Death: once it is over (Vittoria killed at her wedding) Asgeir's pair is no longer the quest's
		if (!done(kQBoundUntilDeath)) {
			if (auto* q = QuestOf(kQBoundUntilDeath); q && q->IsCompleted()) {
				for (auto& p : pairs) {
					if (p.quest == kQBoundUntilDeath) {
						Release(p);
					}
				}
				mark(kQBoundUntilDeath);
			}
		}

		// ---- 0.19.3 other mods' quest outcomes (OStimNPCsReforged_Couples.json "questOutcomes"; each one only while its plugin is loaded) ----
		// a mod quest puts two people together (guard held). A pair the player set by hand, or one already further along, is left
		// alone; so is anyone married or quest-locked to someone else. Any other relationship that would clash ends first (dating /
		// engaged elsewhere; for dating or more also seeing someone) - nobody ends up dating two people.
		for (const auto* o : due) {
			if (o->together != Stage::None) {
				BringTogether(o->a, o->b, o->together, o->id, placed, dead, now);
			} else {
				EndPairByMod(o->a, o->b, o->endedBy == 1 ? o->a : o->endedBy == 2 ? o->b : 0, o->crushOnly, o->id, now);
			}
			outcomesDone.push_back(o->id);
		}
	}

	// ---- 0.20.0 relationship watcher: any mod that changes two NPCs' relationship rank changes their couple ----
	// Mods' scripts move couples with SetRelationshipRank (FDE Olfina drops Olfina's rank to Jon, FDE Mjoll makes Aerin Mjoll's lover)
	// and a newly installed mod brings its own relationship records. Every 6 game hours every relationship record (the plugins' and
	// the ones scripts create at runtime) is compared with the last look (saved in the co-save, record LADV):
	//   - partners Reforged still has together fall to acquaintance or below -> they split (a marriage is only logged: game
	//     marriages never divorce);
	//   - two people reach lover rank -> they are dating (an affair when one of them is taken);
	//   - a spouse record appears -> they are married.
	// Reforged's own rank changes always FOLLOW its ladder (raised once a couple moved up, dropped once it ended), so they never
	// disagree with it and are never mistaken for a mod's. Ignored: the player, the player's spouse, children, "never in love"
	// people and pairs set by hand. The first look of a save is only a baseline.
	namespace
	{
		constexpr std::uint16_t kSeenSpouse = 0x100;
		constexpr std::uint16_t kSeenCourting = 0x200;
		float                   lastRankScan{ -1.0f };  // per session

		std::uint64_t PairKey64(RE::FormID a_x, RE::FormID a_y)
		{
			const auto lo = std::min(a_x, a_y), hi = std::max(a_x, a_y);
			return (static_cast<std::uint64_t>(lo) << 32) | hi;
		}

		// the game's rank (-4 archnemesis .. 4 lover) from a record's level (0 lover .. 8 archnemesis)
		int RankOf(std::uint16_t a_seen)
		{
			return 4 - static_cast<int>(a_seen & 0xFF);
		}

		bool WatchablePerson(const RE::TESNPC* a_npc)
		{
			return a_npc && a_npc->GetFormID() != 0x7 && a_npc->IsUnique() && !(a_npc->race && a_npc->race->IsChildRace());
		}

		std::unordered_map<std::uint64_t, std::uint16_t> ScanRelationships()
		{
			std::unordered_map<std::uint64_t, std::uint16_t> out;
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			const RE::BSReadLockGuard l{ lock };
			if (!map) {
				return out;
			}
			for (auto& entry : *map) {
				auto* rel = entry.second && entry.second->GetFormType() == RE::FormType::Relationship ? static_cast<RE::BGSRelationship*>(entry.second) : nullptr;
				if (!rel || rel->IsDeleted() || !WatchablePerson(rel->npc1) || !WatchablePerson(rel->npc2) || rel->npc1 == rel->npc2) {
					continue;
				}
				const bool          spouse = rel->assocType && rel->assocType->GetFormID() == kSpouseAssoc;
				const bool          courting = rel->assocType && rel->assocType->GetFormID() == kCourtingAssoc;
				const std::uint16_t seen = static_cast<std::uint16_t>(static_cast<std::uint8_t>(rel->level.get()) | (spouse ? kSeenSpouse : 0) | (courting ? kSeenCourting : 0));
				auto [it, fresh] = out.try_emplace(PairKey64(rel->npc1->GetFormID(), rel->npc2->GetFormID()), seen);
				if (!fresh && RankOf(seen) > RankOf(it->second)) {
					it->second = static_cast<std::uint16_t>(seen | (it->second & (kSeenSpouse | kSeenCourting)));  // two records for one pair: the warmer one counts
				} else if (!fresh) {
					it->second |= (seen & (kSeenSpouse | kSeenCourting));
				}
			}
			return out;
		}

		bool IsTogether(Stage a_s)
		{
			return a_s == Stage::Casual || a_s == Stage::Candidate || a_s == Stage::Engaged || a_s == Stage::Married || a_s == Stage::Affair;
		}

		bool PlayersSpouse(RE::FormID a_base)
		{
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(RefOf(a_base));
			auto* married = RE::TESForm::LookupByID<RE::TESFaction>(0x0C6472);  // PlayerMarriedFaction
			return actor && married && actor->IsInFaction(married);
		}
	}

	void WatchRelationships()
	{
		const auto s = Settings::Get();
		if (!s.ladderEnabled || !s.watchRelationships) {
			return;
		}
		const float now = DaysNow();
		{
			std::scoped_lock lock(guard);
			if (lastRankScan >= 0.0f && now >= lastRankScan && now - lastRankScan < 0.25f) {
				return;
			}
			lastRankScan = now;
		}
		auto current = ScanRelationships();  // outside the ladder lock (it takes the form map lock)
		enum class Change { Split, Lovers, Married };
		std::vector<std::tuple<RE::FormID, RE::FormID, Change>> changes;
		{
			std::scoped_lock lock(guard);
			if (!rankSnapshotTaken) {
				SKSE::log::info("Relationship watcher: first look in this save - {} relationship(s) noted, changes from now on are followed", current.size());
				rankSnapshot = std::move(current);
				rankSnapshotTaken = true;
				return;
			}
			for (const auto& [key, seen] : current) {
				const auto      it = rankSnapshot.find(key);
				const int       before = it != rankSnapshot.end() ? RankOf(it->second) : 0;  // no record yet = acquaintances
				const bool      wasSpouse = it != rankSnapshot.end() && (it->second & kSeenSpouse) != 0;
				const bool      wasCourting = it != rankSnapshot.end() && (it->second & kSeenCourting) != 0;
				const int       rank = RankOf(seen);
				const RE::FormID x = static_cast<RE::FormID>(key >> 32), y = static_cast<RE::FormID>(key & 0xFFFFFFFF);
				if ((seen & kSeenSpouse) && !wasSpouse) {
					changes.emplace_back(x, y, Change::Married);
				} else if ((rank == 4 && before < 4) || ((seen & kSeenCourting) && !wasCourting)) {
					changes.emplace_back(x, y, Change::Lovers);
				} else if (rank <= 0 && before >= 1) {
					changes.emplace_back(x, y, Change::Split);
				}
			}
			rankSnapshot = std::move(current);  // a record that disappeared (a mod removed) is simply forgotten
		}
		if (changes.empty()) {
			return;
		}
		std::unordered_set<RE::FormID> dead;
		const auto                     placed = PlacedPeople(&dead);
		std::scoped_lock               lock(guard);
		for (const auto& [x, y, change] : changes) {
			if (StyleOfUnlocked(x) == Style::Never || StyleOfUnlocked(y) == Style::Never || PlayersSpouse(x) || PlayersSpouse(y)) {
				continue;
			}
			Pair* p = Find(MakeKey(x, y));
			if (p && p->byPlayer) {
				continue;
			}
			const std::string why = std::format("a mod changed their relationship rank");
			switch (change) {
			case Change::Split:
				if (!p || !IsTogether(p->stage)) {
					break;  // nothing to end - or Reforged ended it itself and dropped the rank after
				}
				if (p->stage == Stage::Married) {
					SKSE::log::info("Relationship watcher: {} + {} fell out (rank dropped) - married, so they stay married", NpcName(x), NpcName(y));
					break;
				}
				EndPairByMod(x, y, 0, false, "a mod dropped their relationship rank", now);
				break;
			case Change::Lovers:
				if (p && IsTogether(p->stage)) {
					break;  // already a couple (Reforged raises ranks itself once they marry)
				}
				if (CommittedPairOfUnlocked(x) || CommittedPairOfUnlocked(y)) {
					// one of them is taken: a lover on the side
					if (UsablePerson(x, placed, dead) && UsablePerson(y, placed, dead)) {
						RememberRefs(x, y, placed);
						const CoupleRule rule{ {}, {}, Stage::Casual, false, false, false, kQNone };  // Casual + taken = Affair
						if (ApplyRule(rule, x, y, now)) {
							SKSE::log::info("Relationship watcher: {} + {} -> affair (lover rank, one of them is taken)", NpcName(x), NpcName(y));
						}
					}
					break;
				}
				BringTogether(x, y, Stage::Candidate, why + " to lover", placed, dead, now);
				break;
			case Change::Married:
				if (p && p->stage == Stage::Married) {
					break;
				}
				BringTogether(x, y, Stage::Married, why + " - a spouse record", placed, dead, now);
				break;
			}
		}
	}

	// ---- their own romance scenes (vanilla / mod dialogue) only while they are together ----
	// Data\SKSE\Plugins\OStimNPCsReforged_RomanceLines.json:
	//   { "scenes": [ { "quest": "046B79:Skyrim.esm", "a": "013349:Skyrim.esm", "b": "0371D7:Skyrim.esm", "minRank": 2 }, ... ] }
	// Each listed scene quest gets one condition in memory (no plugin record is changed): a's rank to b >= minRank, added to the
	// quest's start (story manager) conditions AND its dialogue conditions, so the scene neither starts nor plays a line after a
	// breakup. Reforged drops the rank to acquaintance on every breakup; a couple that gets back together gets the scene back.
	// Quests, not lines: dialogue lines load lazily (not in memory at load), quests always are. Only quests whose cast is the two
	// of them are listed. Free spirits' scenes are not listed (they flirt with whoever they like).
	namespace
	{
		RE::FormID ParseForm(const std::string& a_text)
		{
			const auto colon = a_text.find(':');
			if (colon == std::string::npos) {
				return 0;
			}
			try {
				const auto local = static_cast<RE::FormID>(std::stoul(a_text.substr(0, colon), nullptr, 16));
				auto*      dh = RE::TESDataHandler::GetSingleton();
				return dh ? dh->LookupFormID(local, a_text.substr(colon + 1)) : 0;
			} catch (...) {
				return 0;
			}
		}

		// a's GetRelationshipRank(b) >= minRank, run on a's placed reference (whoever the event's subject or the speaker is)
		RE::TESConditionItem* MakeRankItem(RE::TESObjectREFR* a_ref, RE::TESObjectREFR* a_other, float a_minRank)
		{
			auto* item = new RE::TESConditionItem();
			item->next = nullptr;
			item->data.object = RE::CONDITIONITEMOBJECT::kRef;
			item->data.runOnRef = a_ref->CreateRefHandle();
			item->data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetRelationshipRank;
			item->data.functionData.params[0] = a_other;
			item->data.functionData.params[1] = nullptr;
			item->data.comparisonValue.f = a_minRank;
			item->data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kGreaterThanOrEqualTo;
			item->data.flags.isOR = false;
			item->data.flags.global = false;
			return item;
		}

		struct GatedScene
		{
			RE::TESConditionItem* start;
			RE::TESConditionItem* dialogue;
			RE::FormID            refA;
		};
		std::vector<GatedScene> gatedScenes;  // the items stay in the quests for the session; their reference handle is refreshed per load
	}

	void InstallRomanceConditions()
	{
		static bool installed = false;  // the conditions live in the game's quest records for the whole session - add them once
		if (installed) {
			for (const auto& g : gatedScenes) {  // a new load can rebuild references: point the conditions at the current one
				if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(g.refA)) {
					g.start->data.runOnRef = ref->CreateRefHandle();
					g.dialogue->data.runOnRef = ref->CreateRefHandle();
				}
			}
			return;
		}
		installed = true;
		std::error_code ec;
		const auto      path = std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OStimNPCsReforged_RomanceLines.json";
		std::ifstream   in(path);
		if (!in) {
			SKSE::log::info("Romance lines: {} not found - none gated", path.string());
			return;
		}
		nlohmann::json j;
		try {
			in >> j;
		} catch (const std::exception& e) {
			SKSE::log::error("Romance lines: {} is not valid JSON ({})", path.string(), e.what());
			return;
		}
		const auto placed = PlacedPeople();
		int        gated = 0, skipped = 0;
		for (const auto& e : j.value("scenes", nlohmann::json::array())) {
			if (!e.is_object()) {
				continue;
			}
			auto*            quest = RE::TESForm::LookupByID<RE::TESQuest>(ParseForm(e.value("quest", std::string())));
			const RE::FormID a = ParseForm(e.value("a", std::string()));
			const RE::FormID b = ParseForm(e.value("b", std::string()));
			const float      minRank = e.value("minRank", 2.0f);
			auto*            refA = placed.contains(a) ? RE::TESForm::LookupByID<RE::TESObjectREFR>(placed.at(a)) : nullptr;
			auto*            refB = placed.contains(b) ? RE::TESForm::LookupByID<RE::TESObjectREFR>(placed.at(b)) : nullptr;
			if (!quest || !refA || !refB) {
				++skipped;
				continue;
			}
			// in front of the quest's own conditions (all items are AND-ed, so order does not matter)
			auto* start = MakeRankItem(refA, refB, minRank);
			start->next = quest->storyManagerConditions.head;
			quest->storyManagerConditions.head = start;
			auto* dialogue = MakeRankItem(refA, refB, minRank);
			dialogue->next = quest->objConditions.head;
			quest->objConditions.head = dialogue;
			gatedScenes.push_back({ start, dialogue, refA->GetFormID() });
			++gated;
		}
		if (!j.value("lines", nlohmann::json::array()).empty()) {
			SKSE::log::warn("Romance lines: the old \"lines\" list is ignored since 0.19.1 - list the scene quests under \"scenes\"");
		}
		SKSE::log::info("Romance lines: {} scene(s) now need their couple together, {} skipped (plugin missing or person not in the world)", gated, skipped);
	}

	// ---- couples and styles from a JSON file (other mods' NPCs, no rebuild) ----
	// Data\SKSE\Plugins\OStimNPCsReforged_Couples.json:
	//   { "couples": [ { "a": "000D62:SomeMod.esp", "b": "000D63:SomeMod.esp", "stage": "dating",
	//                    "questLocked": false, "secret": false, "familyDisapproves": false } ],
	//     "styles":  [ { "npc": "000D62:SomeMod.esp", "style": "freeSpirit" } ] }
	// stage: seeing / dating / engaged / married / affair / crush (a pines for b) / thrall (a is the vampire) / ex (0.19.2: they
	// broke up long ago, a ended it).
	// A couple is only ADDED when the two have no pair yet - an existing pair (the game's, Reforged's or one you set on the
	// Couples page) is never changed by the file. Styles: normal / freeSpirit / never; your choice on the Couples page wins.
	// 0.19.2: any couple or style may carry "requires": "Some Mod.esp" (only while that plugin is loaded) and/or
	// "unless": "Other Mod.esp" (not while that one is loaded) - for a mod that changes someone else's NPCs.
	namespace
	{
		void ApplyJsonCouples()
		{
			static bool done = false;
			if (done) {
				return;
			}
			done = true;
			std::error_code ec;
			const auto      path = std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OStimNPCsReforged_Couples.json";
			std::ifstream   in(path);
			if (!in) {
				return;
			}
			nlohmann::json j;
			try {
				in >> j;
			} catch (const std::exception& e) {
				SKSE::log::error("Couples json: {} is not valid JSON ({})", path.string(), e.what());
				return;
			}
			auto stageOf = [](std::string s) -> std::optional<Stage> {
				std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (s == "seeing" || s == "casual" || s == "seeing each other") return Stage::Casual;
				if (s == "dating" || s == "candidate") return Stage::Candidate;
				if (s == "engaged") return Stage::Engaged;
				if (s == "married") return Stage::Married;
				if (s == "affair") return Stage::Affair;
				if (s == "crush") return Stage::Crush;
				if (s == "thrall") return Stage::Thrall;
				if (s == "ex") return Stage::None;
				return std::nullopt;
			};
			auto gated = [](const nlohmann::json& e) {
				const std::string req = e.value("requires", std::string());
				const std::string unl = e.value("unless", std::string());
				return (!req.empty() && !PluginLoaded(req.c_str())) || (!unl.empty() && PluginLoaded(unl.c_str()));
			};
			// 0.19.3 quest outcomes - read once per session, checked on every quest watch (WatchQuests)
			int outcomesRead = 0, outcomesOff = 0;
			for (const auto& e : j.value("questOutcomes", nlohmann::json::array())) {
				if (!e.is_object()) {
					continue;
				}
				QuestOutcome o;
				const std::string questText = e.value("quest", std::string());
				o.quest = ParseForm(questText);
				o.a = ParseForm(e.value("a", std::string()));
				o.b = ParseForm(e.value("b", std::string()));
				o.stage = e.contains("stageDone") && e["stageDone"].is_number_integer() ? e["stageDone"].get<std::int32_t>() : -1;
				std::string result = e.value("result", std::string());
				std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				const std::string ender = e.value("endedBy", std::string());
				o.endedBy = ender == "a" ? 1 : ender == "b" ? 2 : 0;
				o.crushOnly = result == "crushends";
				if (result != "breakup" && !o.crushOnly) {
					const auto st = stageOf(result);
					if (!st || IsEndedStage(*st) || IsSideStage(*st)) {
						SKSE::log::warn("Couples json: quest outcome with unknown result '{}' ignored", result);
						continue;
					}
					o.together = *st;
				}
				o.id = e.value("id", std::format("{}|{}|{}|{}", questText, o.stage, e.value("a", std::string()), e.value("b", std::string())));
				if (gated(e) || !o.quest || !o.a || !o.b || o.a == o.b) {
					++outcomesOff;  // its mod is not in the load order (or excluded by "unless")
					continue;
				}
				jsonOutcomes.push_back(std::move(o));
				++outcomesRead;
			}
			if (outcomesRead || outcomesOff) {
				SKSE::log::info("Couples json: {} quest outcome(s) watched, {} not (plugin not loaded / excluded)", outcomesRead, outcomesOff);
			}
			std::unordered_set<RE::FormID> dead;
			const auto  placed = PlacedPeople(&dead);
			const float now = DaysNow();
			int         added = 0, kept = 0, skipped = 0, styles = 0;
			std::scoped_lock lock(guard);
			for (const auto& e : j.value("styles", nlohmann::json::array())) {
				if (!e.is_object() || gated(e)) {
					continue;
				}
				const RE::FormID id = ParseForm(e.value("npc", std::string()));
				std::string      st = e.value("style", std::string());
				std::transform(st.begin(), st.end(), st.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (id && (st == "freespirit" || st == "free spirit" || st == "never" || st == "normal")) {
					jsonStyles[id] = st == "never" ? Style::Never : st == "normal" ? Style::Normal : Style::FreeSpirit;
					++styles;
				}
			}
			for (const auto& e : j.value("couples", nlohmann::json::array())) {
				if (!e.is_object()) {
					continue;
				}
				if (gated(e)) {
					++skipped;
					continue;
				}
				const RE::FormID x = ParseForm(e.value("a", std::string()));
				const RE::FormID y = ParseForm(e.value("b", std::string()));
				const auto       stage = stageOf(e.value("stage", std::string("dating")));
				if (x == y || !stage || !UsablePerson(x, placed, dead) || !UsablePerson(y, placed, dead)) {
					++skipped;
					continue;
				}
				if (Find(MakeKey(x, y))) {
					++kept;  // already a pair - the file never changes an existing one
					continue;
				}
				RememberRefs(x, y, placed);
				const CoupleRule rule{ {}, {}, *stage, e.value("questLocked", false), e.value("secret", false), e.value("familyDisapproves", false), kQNone,
					nullptr, nullptr, *stage == Stage::None ? Stage::Candidate : Stage::None };
				if (ApplyRule(rule, x, y, now)) {
					++added;
				}
			}
			SKSE::log::info("Couples json: {} couple(s) added, {} already paired, {} skipped (plugin missing / dead / bad stage), {} style(s)", added, kept,
				skipped, styles);
		}
	}

	// ---- 0.21.0: protected couples ----
	// Data\SKSE\Plugins\OStimNPCsReforged_ProtectedCouples.json: { "protected": [ { "a": "01BDB4:Skyrim.esm", "b": "01BDB0:Skyrim.esm",
	// "why": "Dark Brotherhood" }, ... ] }. Game couples whose marriage is quest material: frozen + protectedVanilla (never split, no
	// strikes, no cold divorce, no promotion), also when "Game marriages can end" is on. A pair the player set by hand (byPlayer, which
	// includes unticking "Keep together") is left alone.
	namespace
	{
		std::map<std::pair<RE::FormID, RE::FormID>, std::string> protectedWhy;  // filled at kDataLoaded, read-only after
	}

	void LoadProtectedCouples()
	{
		std::error_code ec;
		const auto path = (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OStimNPCsReforged_ProtectedCouples.json").string();
		protectedWhy.clear();
		std::ifstream in(path);
		if (!in) {
			SKSE::log::warn("Protected couples: {} missing - none protected", path);
			return;
		}
		nlohmann::json j;
		try {
			in >> j;
		} catch (const std::exception& e) {
			SKSE::log::error("Protected couples: {} is not valid JSON ({}) - none protected", path, e.what());
			return;
		}
		std::size_t skipped = 0;
		for (const auto& e : j.value("protected", nlohmann::json::array())) {
			if (!e.is_object()) {
				continue;
			}
			const RE::FormID a = ParseForm(e.value("a", std::string()));
			const RE::FormID b = ParseForm(e.value("b", std::string()));
			if (!a || !b || a == b) {
				++skipped;  // a plugin not loaded
				continue;
			}
			const Key k = MakeKey(a, b);
			protectedWhy[{ k.a, k.b }] = e.value("why", std::string("quest"));
		}
		SKSE::log::info("Protected couples: {} loaded, {} skipped (plugin not loaded)", protectedWhy.size(), skipped);
	}

	std::size_t ApplyProtectedCouples()
	{
		std::scoped_lock lock(guard);
		std::size_t applied = 0;  // cheap: runs every scan, so pairs a late import adds are covered too
		for (auto& p : pairs) {
			if (p.byPlayer || !protectedWhy.contains({ p.a, p.b })) {
				continue;
			}
			if (!p.frozen || !p.protectedVanilla) {
				p.frozen = true;
				p.protectedVanilla = true;
				++applied;
			}
		}
		if (applied || !protectedPassDone) {
			SKSE::log::info("Protected couples: {} loaded, {} applied", protectedWhy.size(), applied);
			protectedPassDone = true;  // log the first pass of each load once, later ones only when they change something
		}
		return applied;
	}

	std::string ProtectedWhy(RE::FormID a_a, RE::FormID a_b)
	{
		const Key k = MakeKey(a_a, a_b);
		const auto it = protectedWhy.find({ k.a, k.b });
		return it != protectedWhy.end() ? it->second : std::string();
	}

	void SetKeepTogether(RE::FormID a_a, RE::FormID a_b, bool a_keep)
	{
		std::scoped_lock lock(guard);
		Pair* p = Find(MakeKey(a_a, a_b));
		if (!p) {
			return;
		}
		p->protectedVanilla = a_keep;
		if (!a_keep && protectedWhy.contains({ p->a, p->b })) {
			p->frozen = false;  // freed from the protected list: the normal rules apply again
		} else if (a_keep && protectedWhy.contains({ p->a, p->b })) {
			p->frozen = true;
		}
		p->byPlayer = true;  // later passes leave the player's choice alone
		SKSE::log::info("Ladder: {} + {} keep together {}", NpcName(p->a), NpcName(p->b), a_keep ? "on" : "off");
	}

	// ---- 0.21.2 player-partner factions (OStimNPCsReforged_Couples.json "playerPartnerFactions") ----
	// A follower mod's own romance (FDE Karliah's aaKarliahRomanceFaction) shows in no relationship rank or marriage, so Reforged
	// would pair the player's love with an NPC. Listed factions count like a spouse: kept out of NPC couples and NPC scenes.
	// Read once at kDataLoaded; the list never changes afterwards, so the checks need no lock.
	namespace
	{
		struct PartnerFaction
		{
			RE::TESFaction* faction{ nullptr };
			std::int8_t     minRank{ 0 };
		};
		std::vector<PartnerFaction> partnerFactions;
	}

	void LoadPlayerPartnerFactions()
	{
		partnerFactions.clear();
		std::error_code ec;
		const auto      path = std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OStimNPCsReforged_Couples.json";
		std::ifstream   in(path);
		if (!in) {
			return;
		}
		nlohmann::json j;
		try {
			in >> j;
		} catch (const std::exception& e) {
			SKSE::log::error("Couples json: {} is not valid JSON ({})", path.string(), e.what());
			return;
		}
		int off = 0;
		for (const auto& e : j.value("playerPartnerFactions", nlohmann::json::array())) {
			if (!e.is_object()) {
				continue;
			}
			const std::string req = e.value("requires", std::string());
			const std::string unl = e.value("unless", std::string());
			if ((!req.empty() && !PluginLoaded(req.c_str())) || (!unl.empty() && PluginLoaded(unl.c_str()))) {
				++off;
				continue;
			}
			auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(ParseForm(e.value("faction", std::string())));
			if (!faction) {
				++off;  // its plugin is not in the load order
				continue;
			}
			const int rank = e.contains("rank") && e["rank"].is_number_integer() ? e["rank"].get<int>() : 0;
			partnerFactions.push_back({ faction, static_cast<std::int8_t>(std::clamp(rank, 0, 127)) });
		}
		if (!partnerFactions.empty() || off) {
			SKSE::log::info("Couples json: {} player-partner faction(s) watched, {} not (plugin not loaded / excluded)", partnerFactions.size(), off);
		}
	}

	bool InPlayerPartnerFaction(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		for (const auto& f : partnerFactions) {
			if (a_actor->GetFactionRank(f.faction, false) >= f.minRank) {
				return true;
			}
		}
		return false;
	}
