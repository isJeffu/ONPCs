// 0.17.0: import of the game's own couples. Included at the end of Ladder.cpp, inside namespace ONPCR::Ladder, so it can use the
// ladder's private helpers (guard, pairs, Find, ChooseHome ...). Not a separate .cpp on purpose.

	namespace
	{
		constexpr RE::FormID kSpouseAssoc = 0x0142CA;
		constexpr RE::FormID kCourtingAssoc = 0x01EE23;
		constexpr RE::FormID kParentChildAssoc = 0x0142C6;  // npc1 = parent, npc2 = child, any age

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		// "Thonar" matches "Thonar Silver-Blood"; a full name matches itself
		bool NameIs(const std::string& a_name, const std::string& a_token)
		{
			const std::string n = Lower(a_name), t = Lower(a_token);
			return n == t || (n.size() > t.size() && n.compare(0, t.size(), t) == 0 && n[t.size()] == ' ');
		}

		using NamePair = std::pair<std::string, std::string>;

		// Couples that quests depend on (research 2026-10-14, "game couples and their quests"): the quest decides, so the ladder never moves
		// or breaks them. Names are first names or full names; both partners must match.
		const std::vector<NamePair>& BuiltInQuestCouples()
		{
			static const std::vector<NamePair> list = {
				{ "Astrid", "Arnbjorn" }, { "Thonar", "Betrid" }, { "Eltrys", "Rhiada" }, { "Endon", "Kerah" },
				{ "Susanna", "Kjeld" }, { "Iddra", "Kjeld" }, { "Arivanya", "Ulundil" }, { "Vulwulf", "Nura" },
				{ "Rorlund", "Freir" }, { "Alvor", "Sigrid" }, { "Gerdur", "Hod" }, { "Eorlund", "Fralia" },
				{ "Alfhild", "Idolaf" }, { "Bergritte", "Olfrid" }, { "Maramal", "Dinya" }, { "Adril", "Cindiri" },
				{ "Crescius", "Aphia" }, { "Garyn", "Milore" }, { "Deor", "Yrsa" }, { "Oslaf", "Finna" },
				{ "Tharstan", "Fanari" }, { "Dumrag", "Atul" }, { "Harran", "Lassinia" }, { "Aebond", "Pelena" },
				{ "Felki", "Ralgar" }, { "Gergus", "Cindaia" }, { "Afer", "Rona" }, { "Stantus", "Hjotra" },
				{ "Vittoria", "Asgeir" }, { "Olfina", "Jon" }, { "Svana", "Sibbi" }, { "Vendil", "Mirri" },
				{ "Keerava", "Talen-Jei" }, { "Brina", "Horik" }, { "Alva", "Hroggar" }, { "Fastred", "Bassianus" },
				{ "Elda", "Brunwulf" },
			};
			return list;
		}

		// optional Data\SKSE\Plugins\OStimNPCsReforged_QuestCouples.json:
		//   { "questCouples": [["Name", "Name"], ...],   couples a mod's quest owns - protected
		//     "freeCouples":  [["Name", "Name"], ...] }  couples left to the ladder even when the list or the quest scan says protected
		struct CoupleLists
		{
			std::vector<NamePair> quest;
			std::vector<NamePair> free;
		};

		CoupleLists LoadCoupleLists()
		{
			CoupleLists out;
			out.quest = BuiltInQuestCouples();
			std::error_code ec;
			const auto      path = std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OStimNPCsReforged_QuestCouples.json";
			std::ifstream   in(path);
			if (!in) {
				return out;
			}
			try {
				nlohmann::json j;
				in >> j;
				auto read = [&](const char* a_key, std::vector<NamePair>& a_to) {
					for (const auto& e : j.value(a_key, nlohmann::json::array())) {
						if (e.is_array() && e.size() == 2 && e[0].is_string() && e[1].is_string()) {
							a_to.emplace_back(e[0].get<std::string>(), e[1].get<std::string>());
						}
					}
				};
				read("questCouples", out.quest);
				read("freeCouples", out.free);
			} catch (const std::exception& e) {
				SKSE::log::error("Ladder: OStimNPCsReforged_QuestCouples.json is not valid ({}) - using the built-in list only", e.what());
			}
			return out;
		}

		bool InList(const std::vector<NamePair>& a_list, const std::string& a_x, const std::string& a_y)
		{
			for (const auto& [p, q] : a_list) {
				if ((NameIs(a_x, p) && NameIs(a_y, q)) || (NameIs(a_x, q) && NameIs(a_y, p))) {
					return true;
				}
			}
			return false;
		}

		// NPC base -> name of a journal quest that fills one of its aliases with exactly that person (unique-actor fill or a forced reference).
		// Quests with no journal type are the everyday dialogue / radiant ones and do not count.
		[[maybe_unused]] std::unordered_map<RE::FormID, std::string> QuestOwnedNpcs()
		{
			std::unordered_map<RE::FormID, std::string> out;
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			const RE::BSReadLockGuard l{ lock };
			if (!map) {
				return out;
			}
			for (auto& entry : *map) {
				auto* form = entry.second;
				if (!form || form->GetFormType() != RE::FormType::Quest) {
					continue;
				}
				auto* quest = form->As<RE::TESQuest>();
				if (!quest || quest->IsDeleted() || quest->GetType() == RE::QUEST_DATA::Type::kNone) {
					continue;
				}
				for (auto* alias : quest->aliases) {
					auto* ra = alias ? skyrim_cast<RE::BGSRefAlias*>(alias) : nullptr;
					if (!ra) {
						continue;
					}
					RE::FormID npcId = 0;
					if (ra->fillType == RE::BGSBaseAlias::FILL_TYPE::kUniqueActor && ra->fillData.uniqueActor.uniqueActor) {
						npcId = ra->fillData.uniqueActor.uniqueActor->GetFormID();
					} else if (ra->fillType == RE::BGSBaseAlias::FILL_TYPE::kForced) {
						if (auto ref = ra->fillData.forced.forcedRef.get(); ref) {
							if (auto* npc = ref->GetBaseObject() ? ref->GetBaseObject()->As<RE::TESNPC>() : nullptr) {
								npcId = npc->GetFormID();
							}
						}
					}
					if (npcId) {
						const char* name = quest->GetName();
						out.try_emplace(npcId, name && *name ? std::string(name) : std::format("quest {:08X}", quest->GetFormID()));
					}
				}
			}
			return out;
		}

		bool ImportablePerson(RE::TESNPC* a_npc)
		{
			if (!a_npc || a_npc->IsDeleted() || a_npc->GetFormID() == 0x7 || !a_npc->IsUnique() || !a_npc->GetName() || !*a_npc->GetName()) {
				return false;
			}
			if (!a_npc->race || a_npc->race->IsChildRace()) {
				return false;
			}
			return IsPersonRace(a_npc->race);
		}
	}

	bool GameImportDone()
	{
		std::scoped_lock lock(guard);
		return gameImportDone;
	}

	bool IsGameMarried(RE::FormID a_base)
	{
		std::scoped_lock lock(guard);
		for (const auto& p : pairs) {
			if (p.imported && p.stage == Stage::Married && (p.a == a_base || p.b == a_base)) {
				return true;
			}
		}
		return false;
	}

	std::vector<std::pair<RE::FormID, RE::FormID>> GameMarriedPairRefs()
	{
		std::scoped_lock lock(guard);
		std::vector<std::pair<RE::FormID, RE::FormID>> out;
		for (const auto& p : pairs) {
			if (p.stage == Stage::Married && p.imported) {
				const RE::FormID ra = RefOf(p.a);
				const RE::FormID rb = RefOf(p.b);
				if (ra && rb) {
					out.emplace_back(ra, rb);
				}
			}
		}
		return out;
	}

	namespace
	{
		struct ActInfo
		{
			RE::FormID ref;
			bool       dead;
		};
		struct Cand
		{
			RE::FormID a{ 0 }, b{ 0 };
			int        kind{ 0 };  // 1 courting / lover rank, 2 spouse
		};

		// One pass over every form: who is in memory (unique NPC -> placed actor, alive or dead), every candidate couple (Spouse ->
		// married, Courting / Lover rank -> dating) and every parent -> child record. 0.19.1: split out so the waiting list can reuse it.
		void GatherGameCouples(std::unordered_map<RE::FormID, ActInfo>& a_infos, std::map<std::pair<RE::FormID, RE::FormID>, Cand>& a_cands,
			std::unordered_map<RE::FormID, std::unordered_set<RE::FormID>>& a_kids)
		{
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			const RE::BSReadLockGuard l{ lock };
			if (!map) {
				return;
			}
			for (auto& entry : *map) {
				auto* form = entry.second;
				if (!form) {
					continue;
				}
				if (form->GetFormType() == RE::FormType::ActorCharacter) {
					auto* actor = form->As<RE::Actor>();
					auto* base = actor ? actor->GetActorBase() : nullptr;
					if (actor && base && !actor->IsDeleted() && base->IsUnique()) {
						a_infos.try_emplace(base->GetFormID(), ActInfo{ actor->GetFormID(), actor->IsDead() });
					}
					continue;
				}
				if (form->GetFormType() != RE::FormType::NPC) {
					continue;
				}
				auto* npc = form->As<RE::TESNPC>();
				if (!npc || !npc->relationships) {
					continue;
				}
				for (auto* rel : *npc->relationships) {
					if (!rel || !rel->npc1 || !rel->npc2) {
						continue;
					}
					const RE::FormID id1 = rel->npc1->GetFormID(), id2 = rel->npc2->GetFormID();
					const RE::FormID assoc = rel->assocType ? rel->assocType->GetFormID() : 0;
					if (assoc == kParentChildAssoc) {
						a_kids[id1].insert(id2);
						continue;
					}
					int kind = 0;
					if (assoc == kSpouseAssoc) {
						kind = 2;
					} else if (assoc == kCourtingAssoc || (assoc == 0 && rel->level.get() == RE::BGSRelationship::RELATIONSHIP_LEVEL::kLover)) {
						kind = 1;
					}
					if (kind == 0 || id1 == id2) {
						continue;
					}
					auto& c = a_cands[{ std::min(id1, id2), std::max(id1, id2) }];
					c.a = std::min(id1, id2);
					c.b = std::max(id1, id2);
					c.kind = std::max(c.kind, kind);
				}
			}
		}

		// married first, so a courting record against someone already married is skipped
		std::vector<Cand> MarriedFirst(const std::map<std::pair<RE::FormID, RE::FormID>, Cand>& a_cands)
		{
			std::vector<Cand> order;
			order.reserve(a_cands.size());
			for (const auto& [k, c] : a_cands) {
				order.push_back(c);
			}
			std::stable_sort(order.begin(), order.end(), [](const Cand& x, const Cand& y) { return x.kind > y.kind; });
			return order;
		}

		// guard held. 0.19.1: the waiting list - one entry per couple
		void AddPendingUnlocked(RE::FormID a_a, RE::FormID a_b, int a_kind)
		{
			const RE::FormID lo = std::min(a_a, a_b), hi = std::max(a_a, a_b);  // 0.21.1: always stored lower id first
			for (const auto& w : pendingImports) {
				if (w.a == lo && w.b == hi) {
					return;
				}
			}
			pendingImports.push_back({ lo, hi, a_kind });
		}

		// guard held. One game couple -> a ladder pair; both placed actors are in memory and alive. Returns true when a pair was made.
		bool ImportCandidateUnlocked(RE::FormID a_a, RE::FormID a_b, int a_kind, RE::Actor* a_actA, RE::Actor* a_actB, int a_kids, const CoupleLists& a_lists,
			float a_now, ImportResult& a_res, std::vector<std::pair<RE::FormID, RE::FormID>>& a_newMarriedRefs)
		{
			auto* na = RE::TESForm::LookupByID<RE::TESNPC>(a_a);
			auto* nb = RE::TESForm::LookupByID<RE::TESNPC>(a_b);
			if (!na || !nb || Liita::IsMarried(a_actA) || Liita::IsMarried(a_actB) || IsReservedFollower(a_actA) || IsReservedFollower(a_actB)) {
				++a_res.skipped;
				return false;
			}
			if (Pair* existing = Find(MakeKey(a_a, a_b))) {
				if (existing->children < a_kids) {
					existing->children = static_cast<std::uint8_t>(a_kids);  // an earlier pair of ours: at least count their children
				}
				++a_res.skipped;
				return false;
			}
			// 0.19.0: an orc chief keeps every wife the game gives him (strongholds only - nowhere else has more than one spouse)
			auto isOrc = [](RE::TESNPC* a_npc) { return a_npc->race && a_npc->race->GetFullName() && std::string_view(a_npc->race->GetFullName()) == "Orc"; };
			const bool chiefsWife = a_kind == 2 && isOrc(na) && isOrc(nb);
			if (!chiefsWife && (CommittedPairOfUnlocked(a_a) || CommittedPairOfUnlocked(a_b))) {
				++a_res.skipped;  // one of them is already in a committed pair (ours, or the marriage imported a moment ago)
				return false;
			}

			// does a quest own this couple? 0.19.0: only the quest-couple lists (built in + JSON) lock a couple; being in some quest,
			// or a game courting record, no longer does - the couple table (Ladder_Couples.inc) sets the rest
			const std::string nameA = na->GetName(), nameB = nb->GetName();
			std::string       why;
			if (!InList(a_lists.free, nameA, nameB) && InList(a_lists.quest, nameA, nameB)) {
				why = "known quest couple";
			}

			Pair      fresh;
			const Key key = MakeKey(a_a, a_b);  // 0.21.1: lower id first, whatever order the waiting list handed over (Find relies on it)
			fresh.a = key.a;
			fresh.b = key.b;
			fresh.stage = a_kind == 2 ? Stage::Married : Stage::Candidate;
			fresh.firstDay = a_now;
			fresh.lastDay = a_now;
			fresh.stageDay = a_now;
			fresh.closeness = a_kind == 2 ? 100.0f : 75.0f;
			fresh.createdByMod = false;
			fresh.protectedVanilla = !why.empty();
			fresh.frozen = !why.empty();
			fresh.imported = true;
			fresh.children = static_cast<std::uint8_t>(a_kids);
			knownRefs[a_a] = a_actA->GetFormID();
			knownRefs[a_b] = a_actB->GetFormID();
			pairs.push_back(fresh);
			Pair& p = pairs.back();
			if (a_kind == 2) {
				p.home = ChooseHome(p);  // their existing shared home (editor location); the ranking only decides when they differ
				a_newMarriedRefs.emplace_back(a_actA->GetFormID(), a_actB->GetFormID());
				++a_res.married;
			} else {
				++a_res.courting;
			}
			if (p.protectedVanilla) {
				++a_res.protectedByQuest;
			}
			if (a_kids > 0) {
				++a_res.withChildren;
			}
			SKSE::log::info("Ladder import: {} + {} -> {}{}{}{}", nameA, nameB, StageName(p.stage), p.protectedVanilla ? " (protected: " + why + ")" : std::string(),
				a_kids ? std::format(", {} child(ren)", a_kids) : std::string(), p.home ? ", home set" : "");
			return true;
		}

		int KidCount(const std::unordered_map<RE::FormID, std::unordered_set<RE::FormID>>& a_kids, RE::FormID a_a, RE::FormID a_b)
		{
			std::unordered_set<RE::FormID> children;
			for (const auto id : { a_a, a_b }) {
				if (const auto it = a_kids.find(id); it != a_kids.end()) {
					children.insert(it->second.begin(), it->second.end());
				}
			}
			return static_cast<int>(std::min<std::size_t>(children.size(), 250));
		}

		// 0.19.1: a save that imported before the waiting list existed - put the couples it could not see on the list, once
		void SeedPendingImports()
		{
			std::unordered_map<RE::FormID, ActInfo>                        infos;
			std::map<std::pair<RE::FormID, RE::FormID>, Cand>              cands;
			std::unordered_map<RE::FormID, std::unordered_set<RE::FormID>> kids;
			GatherGameCouples(infos, cands, kids);
			std::scoped_lock lock(guard);
			if (pendingSeeded) {
				return;
			}
			pendingSeeded = true;
			int added = 0;
			for (const auto& c : MarriedFirst(cands)) {
				auto*      na = RE::TESForm::LookupByID<RE::TESNPC>(c.a);
				auto*      nb = RE::TESForm::LookupByID<RE::TESNPC>(c.b);
				const auto ia = infos.find(c.a), ib = infos.find(c.b);
				const bool inA = ia != infos.end(), inB = ib != infos.end();
				if ((inA && inB) || !ImportablePerson(na) || !ImportablePerson(nb) || (inA && ia->second.dead) || (inB && ib->second.dead) || Find(MakeKey(c.a, c.b))) {
					continue;  // the import saw them (made or skipped them then), or they cannot be a couple
				}
				AddPendingUnlocked(c.a, c.b, c.kind);
				++added;
			}
			SKSE::log::info("Ladder import: {} game couple(s) were not in memory at the import - they are taken in once both are seen", added);
		}
	}

	ImportResult ImportGameCouples(bool a_auto)
	{
		ImportResult res;
		if (!Settings::Get().ladderEnabled) {
			return res;
		}

		std::unordered_map<RE::FormID, ActInfo>                        infos;
		std::map<std::pair<RE::FormID, RE::FormID>, Cand>              cands;
		std::unordered_map<RE::FormID, std::unordered_set<RE::FormID>> kids;
		GatherGameCouples(infos, cands, kids);
		const auto lists = LoadCoupleLists();
		const auto order = MarriedFirst(cands);

		const float now = DaysNow();
		std::vector<std::pair<RE::FormID, RE::FormID>> newMarriedRefs;
		{
			std::scoped_lock lock(guard);
			pendingSeeded = true;
			for (const auto& c : order) {
				auto*      na = RE::TESForm::LookupByID<RE::TESNPC>(c.a);
				auto*      nb = RE::TESForm::LookupByID<RE::TESNPC>(c.b);
				const auto ia = infos.find(c.a), ib = infos.find(c.b);
				const bool inA = ia != infos.end(), inB = ib != infos.end();
				if (!ImportablePerson(na) || !ImportablePerson(nb) || (inA && ia->second.dead) || (inB && ib->second.dead)) {
					++res.skipped;
					continue;
				}
				if (!inA || !inB) {
					// 0.19.1: placed but not persistent, and their cell is not loaded (Shel in Largashbur) - wait for them instead of
					// dropping the couple; someone never placed anywhere (cut content) simply waits forever and is never shown
					if (!Find(MakeKey(c.a, c.b))) {
						AddPendingUnlocked(c.a, c.b, c.kind);
						++res.waiting;
					} else {
						++res.skipped;
					}
					continue;
				}
				auto* actA = RE::TESForm::LookupByID<RE::Actor>(ia->second.ref);
				auto* actB = RE::TESForm::LookupByID<RE::Actor>(ib->second.ref);
				if (!actA || !actB) {
					++res.skipped;
					continue;
				}
				ImportCandidateUnlocked(c.a, c.b, c.kind, actA, actB, KidCount(kids, c.a, c.b), lists, now, res, newMarriedRefs);
			}
			if (a_auto) {
				gameImportDone = true;
			}
		}
		res.ran = true;
		// 0.19.0: stages, quest locks, affairs, crushes and free-spirit lovers on top of the plain records - once per table version;
		// pressing the import button again must not undo quest outcomes (Camilla's new partner back to a locked crush)
		if (CoupleTableDue() || !gameImportDone) {
			ApplyCoupleTable();
		}
		if (!newMarriedRefs.empty()) {
			Amulet::ReassertAllGame(newMarriedRefs);  // a ring only where none is worn
		}
		Chronicle(std::format("Imported {} married and {} courting couples from the game", res.married, res.courting), now);
		SKSE::log::info("Ladder import ({}): {} married, {} courting, {} protected by quests, {} with children, {} skipped, {} waiting until both are seen",
			a_auto ? "automatic" : "button", res.married, res.courting, res.protectedByQuest, res.withChildren, res.skipped, res.waiting);
		return res;
	}

	void ResolvePendingImports()
	{
		if (!Settings::Get().ladderEnabled) {
			return;
		}
		bool seeded = false;
		std::unordered_set<RE::FormID> wanted;
		{
			std::scoped_lock lock(guard);
			seeded = pendingSeeded;
			for (const auto& w : pendingImports) {
				wanted.insert(w.a);
				wanted.insert(w.b);
			}
		}
		if (!seeded) {
			if (GameImportDone()) {
				SeedPendingImports();  // older save: once, then the next tick resolves
			}
			return;
		}
		if (wanted.empty()) {
			return;
		}
		// cheap: is anyone we wait for near the player right now?
		std::unordered_map<RE::FormID, RE::Actor*> found;
		if (auto* pl = RE::ProcessLists::GetSingleton()) {
			for (auto& h : pl->highActorHandles) {
				auto  ptr = h.get();
				auto* actor = ptr.get();
				auto* base = actor ? actor->GetActorBase() : nullptr;
				if (base && wanted.contains(base->GetFormID())) {
					found.try_emplace(base->GetFormID(), actor);
				}
			}
		}
		if (found.empty()) {
			return;
		}
		// their partner may be persistent and somewhere else (Chief Burguk): one pass over the actors in memory - only when someone
		// new turns up, not every scan while the player stands next to a person whose partner never exists (cut content)
		static std::unordered_set<RE::FormID> scannedFor;
		bool fresh = false;
		for (const auto& [id, actor] : found) {
			fresh = scannedFor.insert(id).second || fresh;
		}
		if (fresh) {
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			const RE::BSReadLockGuard l{ lock };
			if (map) {
				for (auto& entry : *map) {
					auto* actor = entry.second ? entry.second->As<RE::Actor>() : nullptr;
					auto* base = actor ? actor->GetActorBase() : nullptr;
					if (base && !actor->IsDeleted() && wanted.contains(base->GetFormID())) {
						found.try_emplace(base->GetFormID(), actor);
					}
				}
			}
		}
		bool any = false;
		{
			std::scoped_lock lock(guard);
			for (const auto& w : pendingImports) {
				any = any || (found.contains(w.a) && found.contains(w.b));
			}
		}
		if (!any) {
			return;
		}
		const auto  lists = LoadCoupleLists();
		const float now = DaysNow();
		ImportResult res;
		std::vector<std::pair<RE::FormID, RE::FormID>> newMarriedRefs;
		std::vector<std::string>                        made;
		{
			std::scoped_lock lock(guard);
			std::erase_if(pendingImports, [&](const PendingImport& w) {
				const auto fa = found.find(w.a), fb = found.find(w.b);
				if (fa == found.end() || fb == found.end()) {
					return false;  // still waiting for one of them
				}
				if (fa->second->IsDead() || fb->second->IsDead()) {
					SKSE::log::info("Ladder import: {} + {} - one of them is dead, no longer waiting", NpcName(w.a), NpcName(w.b));
					return true;
				}
				// children: both NPCs carry their Parent/Child records
				std::unordered_map<RE::FormID, std::unordered_set<RE::FormID>> kids;
				for (const auto id : { w.a, w.b }) {
					auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(id);
					if (!npc || !npc->relationships) {
						continue;
					}
					for (auto* rel : *npc->relationships) {
						if (rel && rel->npc1 && rel->npc2 && rel->assocType && rel->assocType->GetFormID() == kParentChildAssoc) {
							kids[rel->npc1->GetFormID()].insert(rel->npc2->GetFormID());
						}
					}
				}
				if (ImportCandidateUnlocked(w.a, w.b, w.kind, fa->second, fb->second, KidCount(kids, w.a, w.b), lists, now, res, newMarriedRefs)) {
					made.push_back(std::format("{} and {}", NpcName(w.a), NpcName(w.b)));
				}
				return true;  // made, or skipped for good (already paired, married to the player ...)
			});
		}
		if (!newMarriedRefs.empty()) {
			Amulet::ReassertAllGame(newMarriedRefs);
		}
		for (const auto& m : made) {
			Chronicle(std::format("{}: a couple from the game, now on the ladder", m), now);
		}
	}

	bool ImportOtherPair(RE::FormID a_baseA, RE::FormID a_baseB)
	{
		if (!Settings::Get().ladderEnabled || !a_baseA || !a_baseB || a_baseA == a_baseB) {
			return false;
		}
		auto* na = RE::TESForm::LookupByID<RE::TESNPC>(a_baseA);
		auto* nb = RE::TESForm::LookupByID<RE::TESNPC>(a_baseB);
		if (!ImportablePerson(na) || !ImportablePerson(nb)) {
			return false;
		}
		const float now = DaysNow();
		std::scoped_lock lock(guard);
		if (CommittedPairOfUnlocked(a_baseA) || CommittedPairOfUnlocked(a_baseB)) {
			return false;
		}
		const Key key = MakeKey(a_baseA, a_baseB);
		Pair*     p = Find(key);
		if (!p) {
			Pair fresh;
			fresh.a = key.a;
			fresh.b = key.b;
			fresh.firstDay = now;
			pairs.push_back(fresh);
			p = &pairs.back();
		}
		p->stage = Stage::Candidate;  // dating - free to move on like any ladder couple
		p->stageDay = now;
		p->lastDay = now;
		p->closeness = std::max(p->closeness, 60.0f);
		p->cold = false;
		SKSE::log::info("Ladder import (other mod): {} + {} -> dating", NpcName(p->a), NpcName(p->b));
		return true;
	}

	void FinishOtherImport(std::int32_t a_count, const char* a_source)
	{
		Chronicle(std::format("Imported {} dating couple(s) from {}", a_count, a_source && *a_source ? a_source : "other mods"), DaysNow());
	}
