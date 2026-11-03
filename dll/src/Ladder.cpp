#include "Ladder.h"

#include "Amulet.h"
#include "Babies.h"
#include "Liita.h"
#include "Settings.h"

#include <nlohmann/json.hpp>

namespace ONPCR::Ladder
{
	namespace
	{
		constexpr std::uint32_t kUniqueID = 'ONPR';
		constexpr std::uint32_t kRecordPairs = 'LADR';
		constexpr std::uint32_t kRecordState = 'LADS';
		// v4: Pair.home, v5: Pair.weddingDay, v6: Pair.weddingNotice, v7: altarRoll, invitedPlayer, waitedDays,
		// v8 (0.19.0): frozen, secret, familyBlock, exposed, activeA, byPlayer, quest, endedFrom, endedBy
		constexpr std::uint32_t kRecordVersion = 8;

		std::mutex               guard;
		std::vector<Pair>        pairs;
		std::set<std::pair<RE::FormID, RE::FormID>> playerChosen;  // 0.20.3: pairs the player brought together (match-maker), this session only
		bool                     protectedPassDone{ false };  // 0.21.0: the protected-couples list was applied to this save (reset on load)
		std::vector<LedgerWrite> ledgerQueue;
		std::int32_t             lastSimDay{ 0 };
		bool                     gameImportDone{ false };  // 0.17.0: the one-time import of the game's couples has run in this save (co-save record LADI)
		// 0.19.0 (record LADQ): which couple-table version this save has had, when Spread the Love was finished (-1 not yet), and
		// which quest outcomes were already applied (bit per quest key) - so nothing is done twice
		std::uint32_t            tableApplied{ 0 };
		float                    spreadLoveDay{ -1.0f };
		std::uint32_t            questDone{ 0 };
		std::vector<std::string> modRulesOn;  // 0.19.2 (record LADX): plugins whose couple-table rules are in effect in this save (lower case, sorted)
		std::vector<std::string> outcomesDone;  // 0.19.3 (record LADZ): Couples.json quest outcomes already applied in this save (by id)
		// 0.20.0 (record LADV): the relationship watcher's last look - pair (lo id << 32 | hi id) -> level | spouse 0x100 | courting 0x200
		std::unordered_map<std::uint64_t, std::uint16_t> rankSnapshot;
		bool                                             rankSnapshotTaken{ false };
		std::unordered_map<RE::FormID, Style> styleChoice;  // the player's per-person style choices (record LADW)
		// 0.19.1 (record LADU): game couples the import could not see (a partner's cell was not loaded) - taken in once both are seen.
		// pendingSeeded = this save has the list (a save that imported before 0.19.1 fills it once)
		struct PendingImport
		{
			RE::FormID   a;
			RE::FormID   b;
			std::int32_t kind;  // 1 courting / lover rank, 2 spouse
		};
		std::vector<PendingImport> pendingImports;
		bool                       pendingSeeded{ false };
		std::vector<RE::FormID>  pregnant;  // base IDs of women an engine reports as expecting (saved)

		// custom (mod-added) followers the player has let into the ladder - base IDs, saved in record LADF. Own lock: it is
		// read from Eligible, which can run while guard is held.
		std::mutex              followerGuard;
		std::vector<RE::FormID> allowedFollowers;
		// NPCs the player has met (talked to, or been near) - base IDs, saved in record LADM; for OnlyMetCouples
		std::unordered_set<RE::FormID> playerMet;
		// custom followers seen travelling with the player (base IDs, record LADK) - catches followers with their own follow system
		// (Inigo, Val Serano, ...) that never join the vanilla follower factions; and the player's per-person children choice
		// (records LADC = never, LADY = always; neither = automatic rule)
		std::unordered_set<RE::FormID> knownFollowers;
		std::unordered_set<RE::FormID> childrenNever;
		std::unordered_set<RE::FormID> childrenAlways;

		void WriteSet(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, const std::unordered_set<RE::FormID>& a_set)
		{
			if (a_intfc->OpenRecord(a_type, kRecordVersion)) {
				const std::uint32_t n = static_cast<std::uint32_t>(a_set.size());
				a_intfc->WriteRecordData(&n, sizeof(n));
				for (const auto id : a_set) {
					a_intfc->WriteRecordData(&id, sizeof(id));
				}
			}
		}

		bool IsVanillaFile(const RE::TESForm* a_form)
		{
			const auto* file = a_form ? a_form->GetFile(0) : nullptr;
			if (!file) {
				return true;  // created at runtime: not a mod follower
			}
			const std::string_view name = file->GetFilename();
			for (const auto* v : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" }) {
				if (_stricmp(name.data(), v) == 0) {
					return true;
				}
			}
			return false;
		}

		struct CommunityMonth
		{
			RE::FormID   community;
			std::int32_t month;
			std::int32_t count;
		};
		std::vector<CommunityMonth> communityMonths;  // not saved - resets on load, which is harmless
		std::unordered_map<RE::FormID, Archetype> archetypes;  // per session, filled by Papyrus from M.A.R.A.S
		std::vector<News>                          news;
		std::unordered_map<RE::FormID, RE::FormID> poolRefs;  // base -> actor ref from the last pool build

		// News page: the last kChronicleMax ladder events, oldest first (saved in record LADN)
		constexpr std::uint32_t      kRecordChronicle = 'LADN';
		constexpr std::size_t        kChronicleMax = 50;
		std::mutex                   chronicleGuard;
		std::deque<ChronicleEntry>   chronicle;

		std::size_t ChronicleSize()
		{
			std::scoped_lock clock(chronicleGuard);
			return chronicle.size();
		}

		std::unordered_map<RE::FormID, RE::FormID> knownRefs;  // base -> actor ref, NPCs seen in real scenes (session only)

		RE::FormID RefOf(RE::FormID a_base)
		{
			if (const auto p = poolRefs.find(a_base); p != poolRefs.end()) {
				return p->second;
			}
			const auto it = knownRefs.find(a_base);
			return it != knownRefs.end() ? it->second : 0;
		}

		// how strongly a location ties its resident to it: a shop, inn or castle cannot be left behind
		int HomeWeight(const RE::BGSLocation* a_loc)
		{
			if (!a_loc) {
				return 0;
			}
			auto has = [&](RE::FormID a_kw) {
				auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(a_kw);
				return kw && a_loc->HasKeyword(kw);
			};
			if (has(0x01CB86) || has(0x01CB87) || has(0x01CD57)) {  // LocTypeStore, LocTypeInn, LocTypeCastle
				return 3;
			}
			if (has(0x01CB85) || has(0x018EF0)) {  // LocTypeHouse, LocTypeFarm
				return 2;
			}
			return has(0x0130DC) ? 1 : 0;  // LocTypeDwelling
		}

		// married couple's shared home: the stronger tie wins, then the higher M.A.R.A.S social class, then the woman's
		RE::FormID ChooseHome(const Pair& a_pair)
		{
			auto locOf = [](RE::FormID a_ref) -> RE::BGSLocation* {
				auto* actor = a_ref ? RE::TESForm::LookupByID<RE::Actor>(a_ref) : nullptr;
				return actor ? actor->GetEditorLocation() : nullptr;
			};
			auto* la = locOf(RefOf(a_pair.a));
			auto* lb = locOf(RefOf(a_pair.b));
			if (!la || !lb) {
				return la ? la->GetFormID() : (lb ? lb->GetFormID() : 0);
			}
			if (la == lb) {
				return la->GetFormID();
			}
			const int wa = HomeWeight(la), wb = HomeWeight(lb);
			if (wa != wb) {
				return (wa > wb ? la : lb)->GetFormID();
			}
			const auto ca = archetypes.find(a_pair.a), cb = archetypes.find(a_pair.b);
			const int  sa = ca != archetypes.end() ? ca->second.socialClass : -1;
			const int  sb = cb != archetypes.end() ? cb->second.socialClass : -1;
			if (sa != sb && sa >= 0 && sb >= 0) {
				return (sa > sb ? la : lb)->GetFormID();
			}
			auto* nb = RE::TESForm::LookupByID<RE::TESNPC>(a_pair.b);
			return (nb && nb->IsFemale() ? lb : la)->GetFormID();
		}

		// strikes a betrayed NPC tolerates before the couple breaks (temperament, else the setting)
		int StrikesTolerated(RE::FormID a_betrayed)
		{
			const int base = std::max(1, Settings::Get().strayStrikes);
			if (!Settings::Get().useMarasArchetypes) {
				return base;
			}
			auto it = archetypes.find(a_betrayed);
			if (it == archetypes.end()) {
				return base;
			}
			switch (it->second.temperament) {
			case 2: return 1;    // Jealous
			case 1: return 3;    // Humble
			case 4: return 99;   // Independent shrugs it off
			default: return base;
			}
		}

		// Romantic couples climb faster, Independent ones slower (the slower of the two decides)
		float ThresholdMultiplier(RE::FormID a_x, RE::FormID a_y)
		{
			if (!Settings::Get().useMarasArchetypes) {
				return 1.0f;
			}
			float m = 1.0f;
			for (const auto id : { a_x, a_y }) {
				auto it = archetypes.find(id);
				if (it == archetypes.end()) {
					continue;
				}
				if (it->second.temperament == 4) {
					m = std::max(m, 1.5f);
				} else if (it->second.temperament == 3 && m == 1.0f) {
					m = 0.75f;
				}
			}
			return m;
		}

		bool Roll(float a_percent)
		{
			if (a_percent <= 0.0f) {
				return false;
			}
			if (a_percent >= 100.0f) {
				return true;
			}
			thread_local std::mt19937             rng{ std::random_device{}() };
			std::uniform_real_distribution<float> d(0.0f, 100.0f);
			return d(rng) < a_percent;
		}

		// how two people's M.A.R.A.S archetypes tilt a roll (1 = neutral). a_kind: 0 = a spark at all, 1 = straight to dating,
		// 2 = moving up a stage. Temperament: Proud 0, Humble 1, Jealous 2, Romantic 3, Independent 4; Orator skill 5; class 0-7.
		float ArchetypeWeight(RE::FormID a_x, RE::FormID a_y, int a_kind)
		{
			if (!Settings::Get().useMarasArchetypes) {
				return 1.0f;
			}
			float w = 1.0f;
			int   cls[2] = { -1, -1 };
			int   i = 0;
			for (const auto id : { a_x, a_y }) {
				auto it = archetypes.find(id);
				if (it != archetypes.end()) {
					const auto& a = it->second;
					switch (a.temperament) {
					case 3: w *= a_kind == 0 ? 1.5f : (a_kind == 1 ? 1.6f : 1.3f); break;  // romantic: falls fast, skips ahead
					case 4: w *= a_kind == 0 ? 0.5f : (a_kind == 1 ? 0.3f : 0.7f); break;  // independent: slow, rarely skips
					case 2: w *= a_kind == 0 ? 1.0f : (a_kind == 1 ? 1.4f : 1.2f); break;  // jealous: commits quickly
					case 0: w *= a_kind == 0 ? 0.8f : 1.0f; break;                          // proud: harder to win
					case 1: w *= a_kind == 2 ? 1.1f : 1.0f; break;                          // humble: steady
					default: break;
					}
					if (a.skillType == 5 && a_kind == 0) {
						w *= 1.2f;  // orator: charming
					}
					cls[i] = a.socialClass;
				}
				++i;
			}
			if (a_kind == 0 && cls[0] >= 0 && cls[1] >= 0) {
				const int gap = std::abs(cls[0] - cls[1]);
				w *= gap == 0 ? 1.2f : (gap >= 4 ? 0.5f : 1.0f);  // like with like; a noble and a pauper rarely
			}
			return std::clamp(w, 0.1f, 3.0f);
		}

		struct Key
		{
			RE::FormID a;
			RE::FormID b;
		};

		Key MakeKey(RE::FormID a_x, RE::FormID a_y)
		{
			return a_x < a_y ? Key{ a_x, a_y } : Key{ a_y, a_x };
		}

		RE::TESNPC* BaseOf(RE::Actor* a_actor)
		{
			return a_actor ? a_actor->GetActorBase() : nullptr;
		}

		bool Eligible(RE::Actor* a_actor)
		{
			if (!a_actor || a_actor->IsPlayerRef()) {
				return false;
			}
			if (IsReservedFollower(a_actor)) {
				return false;  // a custom follower the player has not let in
			}
			auto* base = a_actor->GetActorBase();
			if (!base || !base->IsUnique() || a_actor->IsChild()) {
				return false;
			}
			if (Liita::IsMarried(a_actor)) {
				return false;  // 0.13.3: married through Love Is In The Air - taken
			}
			return true;
		}

		Pair* Find(const Key& a_key)
		{
			for (auto& p : pairs) {
				if (p.a == a_key.a && p.b == a_key.b) {
					return &p;
				}
			}
			return nullptr;
		}

		float Pace()
		{
			const auto s = Settings::Get();
			return s.worldPace > 0.05f ? s.worldPace : 1.0f;
		}

		Style StyleOfUnlocked(RE::FormID a_base);  // Ladder_Couples.inc: the player's choice, else the built-in list

		// a_endedBy: who ended it (1 = a, 2 = b, 0 = neither) - they get over it faster (Heartbreak)
		void Breakup(Pair& a_pair, float a_now, std::uint8_t a_endedBy = 0)
		{
			const bool wasMarried = a_pair.stage == Stage::Married;
			a_pair.endedFrom = static_cast<std::uint8_t>(a_pair.stage);
			a_pair.endedBy = a_endedBy;
			switch (a_pair.stage) {
			case Stage::Engaged: a_pair.stage = Stage::Jilted; break;
			case Stage::Married: a_pair.stage = Stage::Divorced; break;
			default: a_pair.stage = Stage::None; break;
			}
			a_pair.stageDay = a_now;
			a_pair.closeness = 0.0f;
			a_pair.straysByA = 0;
			a_pair.straysByB = 0;
			a_pair.cold = false;
			a_pair.home = 0;
			SKSE::log::info("Ladder: {} + {} -> {}", NpcName(a_pair.a), NpcName(a_pair.b), StageName(a_pair.stage));
			const bool quiet = a_pair.secret || IsSideStage(static_cast<Stage>(a_pair.endedFrom));
			if (!quiet) {
				// 0.19.3: say who ended it when someone did
				const RE::FormID ender = a_endedBy == 1 ? a_pair.a : a_endedBy == 2 ? a_pair.b : 0;
				const RE::FormID other = ender == a_pair.a ? a_pair.b : a_pair.a;
				Chronicle(ender && a_pair.stage == Stage::None ? std::format("{} has broken it off with {}", NpcName(ender), NpcName(other)) :
				                                                 StagePhrase(NpcName(a_pair.a), NpcName(a_pair.b), a_pair.stage),
					a_now);
			}
			if (news.size() < 200) {
				// 3 = breakup news (letters, voice moment, rank drop); 8 = 0.19.0 silent: only the relationship rank drops, so their own
				// romance lines stop (secret couples and side relationships make no news)
				news.push_back({ quiet ? 8 : 3, a_pair.a, a_pair.b });
			}
			a_pair.secret = false;
			a_pair.exposed = false;
			a_pair.familyBlock = false;
			if (wasMarried) {
				Amulet::OnUnmarried(RefOf(a_pair.a), RefOf(a_pair.b));
			}
		}

		// x had an encounter with someone else while committed: strike against x's own pair
		void HandleStray(RE::FormID a_x, RE::FormID a_with, float a_now)
		{
			if (!Settings::Get().infidelity) {
				return;  // infidelity off: nothing counts as cheating
			}
			auto* own = CommittedPairOfUnlocked(a_x);
			if (!own) {
				return;
			}
			const RE::FormID partner = own->a == a_x ? own->b : own->a;
			if (partner == a_with) {
				return;
			}
			if (const Pair* with = Find(MakeKey(a_x, a_with)); with && (with->IsCommitted() || with->stage == Stage::Thrall)) {
				return;  // 0.19.0: a second wife of an orc chief is not cheating, and a thrall has no say in it
			}
			if (own->frozen) {
				return;  // 0.19.0: a quest-locked couple takes no strikes - their quest decides what happens to them
			}
			const auto s = Settings::Get();
			auto& strikes = own->a == a_x ? own->straysByA : own->straysByB;
			// 0.21.1: while "Game marriages can end" is off a game marriage takes no strikes (closeness and gossip still apply), so
			// switching it on later cannot break a couple over cheating that happened while it was off
			const bool gameLocked = own->imported && own->stage == Stage::Married && !s.gameMarriagesCanEnd;
			if (!gameLocked && strikes < 250) {
				++strikes;
			}
			own->closeness = std::max(0.0f, own->closeness - s.strayPenalty / (1.0f + static_cast<float>(own->children)));
			if (news.size() < 200) {
				news.push_back({ 6, a_x, partner });  // gossip: a_x strayed on partner
			}
			SKSE::log::info("Ladder: {} strayed on {} (strike {})", NpcName(a_x), NpcName(partner), strikes);
			Chronicle(std::format("{} strayed on {} with {} (strike {})", NpcName(a_x), NpcName(partner), NpcName(a_with), strikes), a_now);
			const int tolerated = std::min(250, StrikesTolerated(partner) * (own->children > 0 ? 2 : 1));
			// 0.21.0: a game marriage only breaks over cheating when "Game marriages can end" is on (protected couples never)
			if (!own->protectedVanilla && !gameLocked && strikes >= static_cast<std::uint8_t>(tolerated)) {
				Breakup(*own, a_now, own->a == a_x ? 1 : 2);  // the one who strayed is the one who ended it
			}
		}

		void Promote(Pair& a_pair, Stage a_to, float a_now)
		{
			a_pair.stage = a_to;
			a_pair.stageDay = a_now;
			a_pair.cold = false;
			a_pair.endedFrom = 0;
			a_pair.endedBy = 0;
			SKSE::log::info("Ladder: {} + {} -> {}{}", NpcName(a_pair.a), NpcName(a_pair.b), StageName(a_to), a_pair.secret ? " (secret)" : "");
			if (a_to == Stage::Engaged && a_pair.secret) {
				a_pair.secret = false;  // 0.19.0: an engagement is announced - a secret couple comes out
				Chronicle(std::format("{} and {} have been seeing each other in secret all along", NpcName(a_pair.a), NpcName(a_pair.b)), a_now);
			}
			if (IsSideStage(a_to) || a_pair.secret) {
				return;  // 0.19.0: affairs, crushes, thralls and secret couples make no news, letters, rings or amulet changes
			}
			Chronicle(StagePhrase(NpcName(a_pair.a), NpcName(a_pair.b), a_to), a_now);
			std::int32_t type = 0;
			switch (a_to) {
			case Stage::Candidate: type = 4; break;
			case Stage::Engaged: type = 1; break;
			case Stage::Married: type = 2; break;
			default: break;
			}
			if (type && news.size() < 200) {
				news.push_back({ type, a_pair.a, a_pair.b });
			}
			if (a_to == Stage::Married) {
				a_pair.home = ChooseHome(a_pair);
				Amulet::OnMarried(RefOf(a_pair.a), RefOf(a_pair.b));
			}
			if (a_to == Stage::Candidate) {
				Amulet::TakeBackAmulet(RefOf(a_pair.a));  // our own amulets come off once a couple forms
				Amulet::TakeBackAmulet(RefOf(a_pair.b));
			}
		}

		// game-record relationship between two base NPCs (Spouse / Courting association types)
		VanillaRelation GameRelation(RE::FormID a_x, RE::FormID a_y)
		{
			auto* nx = RE::TESForm::LookupByID<RE::TESNPC>(a_x);
			if (!nx || !nx->relationships) {
				return VanillaRelation::None;
			}
			for (auto* rel : *nx->relationships) {
				if (!rel || !rel->npc1 || !rel->npc2) {
					continue;
				}
				const RE::FormID other = rel->npc1->GetFormID() == a_x ? rel->npc2->GetFormID() : rel->npc1->GetFormID();
				if (other != a_y) {
					continue;
				}
				if (!rel->assocType) {
					// 0.12.5 audit: many game couples are only "Lover" rank with no association (Keerava + Talen-Jei, Alva + Hroggar,
					// Fastred + Bassianus, Brina + Horik, Elda + Brunwulf...) - several are quest-driven, so they count as courting too
					if (rel->level.get() == RE::BGSRelationship::RELATIONSHIP_LEVEL::kLover) {
						return VanillaRelation::Courting;
					}
					continue;
				}
				const RE::FormID assoc = rel->assocType->GetFormID();
				if (assoc == 0x0142CA) {
					return VanillaRelation::Married;  // Spouse
				}
				if (assoc == 0x01EE23) {
					return VanillaRelation::Courting;
				}
			}
			return VanillaRelation::None;
		}

		// community pace cap bookkeeping for a new exclusive (dating) simulated couple (guard held)
		void NoteCommunityUnlocked(RE::FormID a_community, float a_now)
		{
			if (a_community == 0) {
				return;
			}
			const std::int32_t month = static_cast<std::int32_t>(a_now) / 30;
			for (auto& c : communityMonths) {
				if (c.community == a_community && c.month == month) {
					++c.count;
					return;
				}
			}
			communityMonths.push_back({ a_community, month, 1 });
		}

		// 0.19.0 Heartbreak (guard held): someone who split up with a living partner starts nothing new for a while - by how far they
		// got (seeing each other ~1 week, dating ~3, engaged ~2 months, married ~3), x world pace, romantics 1.5x, the independent 0.5x,
		// and half as long for whoever ended it. Free spirits never are.
		bool HeartbrokenUnlocked(RE::FormID a_npc, float a_now)
		{
			const auto s = Settings::Get();
			if (!s.heartbreak || StyleOfUnlocked(a_npc) == Style::FreeSpirit) {
				return false;
			}
			float temper = 1.0f;
			if (s.useMarasArchetypes) {
				if (const auto it = archetypes.find(a_npc); it != archetypes.end()) {
					temper = it->second.temperament == 3 ? 1.5f : it->second.temperament == 4 ? 0.5f : 1.0f;
				}
			}
			for (const auto& p : pairs) {
				if ((p.a != a_npc && p.b != a_npc) || p.endedFrom == 0 || !(p.stage == Stage::None || p.stage == Stage::Jilted || p.stage == Stage::Divorced)) {
					continue;
				}
				float days = 0.0f;
				switch (static_cast<Stage>(p.endedFrom)) {
				case Stage::Casual: days = 7.0f; break;
				case Stage::Candidate: days = 21.0f; break;
				case Stage::Engaged: days = 60.0f; break;
				case Stage::Married: days = 90.0f; break;
				default: break;  // an affair or a crush that ended leaves no heartbreak
				}
				const bool ender = (p.endedBy == 1 && p.a == a_npc) || (p.endedBy == 2 && p.b == a_npc);
				days *= Pace() * temper * (ender ? 0.5f : 1.0f);
				if (days > 0.0f && (a_now - p.stageDay) < days) {
					return true;
				}
			}
			return false;
		}

		// 0.19.0 Matchmakers (guard held): for 30 days after Spread the Love is finished, sparks in Riften are 1.5x as likely
		float MatchmakerBoostUnlocked(RE::FormID a_npc, float a_now)
		{
			if (!Settings::Get().matchmakers || spreadLoveDay < 0.0f || a_now - spreadLoveDay > 30.0f) {
				return 1.0f;
			}
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(RefOf(a_npc));
			auto* riften = RE::TESForm::LookupByID<RE::BGSLocation>(0x018A58);  // RiftenLocation
			for (auto* l = actor ? actor->GetEditorLocation() : nullptr; l && riften; l = l->parentLoc) {
				if (l == riften) {
					return 1.5f;
				}
			}
			return 1.0f;
		}

		// ---- co-save ----
		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			std::scoped_lock lock(guard);
			if (a_intfc->OpenRecord(kRecordState, kRecordVersion)) {
				a_intfc->WriteRecordData(&lastSimDay, sizeof(lastSimDay));
			}
			if (a_intfc->OpenRecord('LADI', kRecordVersion)) {
				const std::uint8_t done = gameImportDone ? 1 : 0;
				a_intfc->WriteRecordData(&done, sizeof(done));
			}
			if (!a_intfc->OpenRecord(kRecordPairs, kRecordVersion)) {
				SKSE::log::error("Ladder: could not open the save record");
				return;
			}
			const std::uint32_t count = static_cast<std::uint32_t>(pairs.size());
			a_intfc->WriteRecordData(&count, sizeof(count));
			for (const auto& p : pairs) {
				a_intfc->WriteRecordData(&p.a, sizeof(p.a));
				a_intfc->WriteRecordData(&p.b, sizeof(p.b));
				a_intfc->WriteRecordData(&p.stage, sizeof(p.stage));
				a_intfc->WriteRecordData(&p.realEncounters, sizeof(p.realEncounters));
				a_intfc->WriteRecordData(&p.offscreenEncounters, sizeof(p.offscreenEncounters));
				a_intfc->WriteRecordData(&p.closeness, sizeof(p.closeness));
				a_intfc->WriteRecordData(&p.firstDay, sizeof(p.firstDay));
				a_intfc->WriteRecordData(&p.lastDay, sizeof(p.lastDay));
				a_intfc->WriteRecordData(&p.stageDay, sizeof(p.stageDay));
				a_intfc->WriteRecordData(&p.straysByA, sizeof(p.straysByA));
				a_intfc->WriteRecordData(&p.straysByB, sizeof(p.straysByB));
				const std::uint8_t flags = static_cast<std::uint8_t>((p.createdByMod ? 1 : 0) | (p.protectedVanilla ? 2 : 0) | (p.cold ? 4 : 0) | (p.imported ? 8 : 0));
				a_intfc->WriteRecordData(&flags, sizeof(flags));
				a_intfc->WriteRecordData(&p.coldSinceDay, sizeof(p.coldSinceDay));
				a_intfc->WriteRecordData(&p.children, sizeof(p.children));
				a_intfc->WriteRecordData(&p.home, sizeof(p.home));
				a_intfc->WriteRecordData(&p.weddingDay, sizeof(p.weddingDay));
				a_intfc->WriteRecordData(&p.weddingNotice, sizeof(p.weddingNotice));
				a_intfc->WriteRecordData(&p.altarRoll, sizeof(p.altarRoll));
				a_intfc->WriteRecordData(&p.invitedPlayer, sizeof(p.invitedPlayer));
				a_intfc->WriteRecordData(&p.waitedDays, sizeof(p.waitedDays));
				const std::uint8_t flags2 = static_cast<std::uint8_t>((p.frozen ? 1 : 0) | (p.secret ? 2 : 0) | (p.familyBlock ? 4 : 0) | (p.exposed ? 8 : 0) |
																	  (p.activeA ? 16 : 0) | (p.byPlayer ? 32 : 0));
				a_intfc->WriteRecordData(&flags2, sizeof(flags2));
				a_intfc->WriteRecordData(&p.quest, sizeof(p.quest));
				a_intfc->WriteRecordData(&p.endedFrom, sizeof(p.endedFrom));
				a_intfc->WriteRecordData(&p.endedBy, sizeof(p.endedBy));
			}
			if (a_intfc->OpenRecord('LADQ', kRecordVersion)) {
				a_intfc->WriteRecordData(&tableApplied, sizeof(tableApplied));
				a_intfc->WriteRecordData(&spreadLoveDay, sizeof(spreadLoveDay));
				a_intfc->WriteRecordData(&questDone, sizeof(questDone));
			}
			if (a_intfc->OpenRecord('LADX', kRecordVersion)) {
				const std::uint32_t n = static_cast<std::uint32_t>(modRulesOn.size());
				a_intfc->WriteRecordData(&n, sizeof(n));
				for (const auto& s : modRulesOn) {
					const std::uint16_t len = static_cast<std::uint16_t>(std::min<std::size_t>(s.size(), 260));
					a_intfc->WriteRecordData(&len, sizeof(len));
					a_intfc->WriteRecordData(s.data(), len);
				}
			}
			if (rankSnapshotTaken && a_intfc->OpenRecord('LADV', kRecordVersion)) {
				const std::uint32_t n = static_cast<std::uint32_t>(rankSnapshot.size());
				a_intfc->WriteRecordData(&n, sizeof(n));
				for (const auto& [k, v] : rankSnapshot) {
					a_intfc->WriteRecordData(&k, sizeof(k));
					a_intfc->WriteRecordData(&v, sizeof(v));
				}
			}
			if (a_intfc->OpenRecord('LADZ', kRecordVersion)) {
				const std::uint32_t n = static_cast<std::uint32_t>(outcomesDone.size());
				a_intfc->WriteRecordData(&n, sizeof(n));
				for (const auto& s : outcomesDone) {
					const std::uint16_t len = static_cast<std::uint16_t>(std::min<std::size_t>(s.size(), 400));
					a_intfc->WriteRecordData(&len, sizeof(len));
					a_intfc->WriteRecordData(s.data(), len);
				}
			}
			if (a_intfc->OpenRecord('LADW', kRecordVersion)) {
				const std::uint32_t n = static_cast<std::uint32_t>(styleChoice.size());
				a_intfc->WriteRecordData(&n, sizeof(n));
				for (const auto& [id, st] : styleChoice) {
					a_intfc->WriteRecordData(&id, sizeof(id));
					a_intfc->WriteRecordData(&st, sizeof(st));
				}
			}
			if (pendingSeeded && a_intfc->OpenRecord('LADU', kRecordVersion)) {  // written even when empty: it marks the list as filled
				const std::uint32_t n = static_cast<std::uint32_t>(pendingImports.size());
				a_intfc->WriteRecordData(&n, sizeof(n));
				for (const auto& w : pendingImports) {
					a_intfc->WriteRecordData(&w.a, sizeof(w.a));
					a_intfc->WriteRecordData(&w.b, sizeof(w.b));
					a_intfc->WriteRecordData(&w.kind, sizeof(w.kind));
				}
			}
			const auto givenAmulets = Amulet::GivenRefs();
			const std::uint32_t givenCount = static_cast<std::uint32_t>(givenAmulets.size());
			if (a_intfc->OpenRecord('LADG', kRecordVersion)) {
				a_intfc->WriteRecordData(&givenCount, sizeof(givenCount));
				for (const auto id : givenAmulets) {
					a_intfc->WriteRecordData(&id, sizeof(id));
				}
			}
			{
				std::scoped_lock flock(followerGuard);
				if (a_intfc->OpenRecord('LADF', kRecordVersion)) {
					const std::uint32_t n = static_cast<std::uint32_t>(allowedFollowers.size());
					a_intfc->WriteRecordData(&n, sizeof(n));
					for (const auto id : allowedFollowers) {
						a_intfc->WriteRecordData(&id, sizeof(id));
					}
				}
				if (a_intfc->OpenRecord('LADM', kRecordVersion)) {
					const std::uint32_t n = static_cast<std::uint32_t>(playerMet.size());
					a_intfc->WriteRecordData(&n, sizeof(n));
					for (const auto id : playerMet) {
						a_intfc->WriteRecordData(&id, sizeof(id));
					}
				}
				WriteSet(a_intfc, 'LADK', knownFollowers);
				WriteSet(a_intfc, 'LADC', childrenNever);
				WriteSet(a_intfc, 'LADY', childrenAlways);
			}
			{
				const auto liita = Liita::Added();  // what we put into Love Is In The Air's exclusion list
				WriteSet(a_intfc, 'LADL', std::unordered_set<RE::FormID>(liita.begin(), liita.end()));
			}
			const std::uint32_t pregCount = static_cast<std::uint32_t>(pregnant.size());
			if (a_intfc->OpenRecord('LADP', kRecordVersion)) {
				a_intfc->WriteRecordData(&pregCount, sizeof(pregCount));
				for (const auto id : pregnant) {
					a_intfc->WriteRecordData(&id, sizeof(id));
				}
			}
			{
				std::scoped_lock clock(chronicleGuard);
				if (a_intfc->OpenRecord(kRecordChronicle, kRecordVersion)) {
					const std::uint32_t n = static_cast<std::uint32_t>(chronicle.size());
					a_intfc->WriteRecordData(&n, sizeof(n));
					for (const auto& e : chronicle) {
						const std::uint16_t len = static_cast<std::uint16_t>(std::min<std::size_t>(e.text.size(), 1000));
						a_intfc->WriteRecordData(&e.day, sizeof(e.day));
						a_intfc->WriteRecordData(&len, sizeof(len));
						a_intfc->WriteRecordData(e.text.data(), len);
					}
				}
			}
			SKSE::log::info("Ladder: saved {} pair(s), last simulated day {}", count, lastSimDay);
		}

		template <class T>
		bool Read(SKSE::SerializationInterface* a_intfc, T& a_out)
		{
			return a_intfc->ReadRecordData(&a_out, sizeof(T)) == sizeof(T);
		}

		// reads a count + that many form IDs, resolving each against the current load order (unresolvable ones dropped)
		std::vector<RE::FormID> ReadFormList(SKSE::SerializationInterface* a_intfc)
		{
			std::vector<RE::FormID> out;
			std::uint32_t           n = 0;
			if (!Read(a_intfc, n)) {
				return out;
			}
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::FormID id = 0, resolved = 0;
				if (!Read(a_intfc, id)) {
					break;
				}
				if (a_intfc->ResolveFormID(id, resolved)) {
					out.push_back(resolved);
				}
			}
			return out;
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			std::vector<RE::FormID> givenLoaded;
			{
				std::scoped_lock clock(chronicleGuard);
				chronicle.clear();
			}
			std::scoped_lock lock(guard);
			pairs.clear();
			ledgerQueue.clear();
			communityMonths.clear();
			pregnant.clear();
			lastSimDay = 0;
			gameImportDone = false;
			tableApplied = 0;
			protectedPassDone = false;
			spreadLoveDay = -1.0f;
			questDone = 0;
			modRulesOn.clear();
			outcomesDone.clear();
			rankSnapshot.clear();
			rankSnapshotTaken = false;
			styleChoice.clear();
			pendingImports.clear();
			pendingSeeded = false;
			std::uint32_t type = 0, version = 0, length = 0;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (type == kRecordState) {
					Read(a_intfc, lastSimDay);
					continue;
				}
				if (type == 'LADQ') {  // 0.19.0 couple table / quest outcome state
					Read(a_intfc, tableApplied) && Read(a_intfc, spreadLoveDay) && Read(a_intfc, questDone);
					continue;
				}
				if (type == 'LADV') {  // 0.20.0 the relationship watcher's last look (ids are load-order bound: re-mapped below)
					std::uint32_t n = 0;
					Read(a_intfc, n);
					for (std::uint32_t i = 0; i < n && i < 200000; ++i) {
						std::uint64_t k = 0;
						std::uint16_t v = 0;
						if (!Read(a_intfc, k) || !Read(a_intfc, v)) {
							break;
						}
						RE::FormID x = 0, y = 0;
						if (a_intfc->ResolveFormID(static_cast<RE::FormID>(k >> 32), x) && a_intfc->ResolveFormID(static_cast<RE::FormID>(k & 0xFFFFFFFF), y)) {
							const auto lo = std::min(x, y), hi = std::max(x, y);
							rankSnapshot[(static_cast<std::uint64_t>(lo) << 32) | hi] = v;
						}
					}
					rankSnapshotTaken = true;
					continue;
				}
				if (type == 'LADZ') {  // 0.19.3 quest outcomes already applied
					std::uint32_t n = 0;
					Read(a_intfc, n);
					for (std::uint32_t i = 0; i < n && i < 4096; ++i) {
						std::uint16_t len = 0;
						if (!Read(a_intfc, len) || len > 400) {
							break;
						}
						std::string s(len, '\0');
						if (a_intfc->ReadRecordData(s.data(), len) != len) {
							break;
						}
						outcomesDone.push_back(std::move(s));
					}
					continue;
				}
				if (type == 'LADX') {  // 0.19.2 plugins whose couple rules are in effect
					std::uint32_t n = 0;
					Read(a_intfc, n);
					for (std::uint32_t i = 0; i < n && i < 256; ++i) {
						std::uint16_t len = 0;
						if (!Read(a_intfc, len) || len > 260) {
							break;
						}
						std::string s(len, '\0');
						if (a_intfc->ReadRecordData(s.data(), len) != len) {
							break;
						}
						modRulesOn.push_back(std::move(s));
					}
					std::sort(modRulesOn.begin(), modRulesOn.end());
					continue;
				}
				if (type == 'LADW') {  // 0.19.0 the player's per-person styles
					std::uint32_t n = 0;
					Read(a_intfc, n);
					for (std::uint32_t i = 0; i < n; ++i) {
						RE::FormID id = 0, resolved = 0;
						Style      st = Style::Normal;
						if (!Read(a_intfc, id) || !Read(a_intfc, st)) {
							break;
						}
						if (a_intfc->ResolveFormID(id, resolved)) {
							styleChoice[resolved] = st;
						}
					}
					continue;
				}
				if (type == 'LADU') {  // 0.19.1 game couples waiting until both are seen
					pendingSeeded = true;
					std::uint32_t n = 0;
					Read(a_intfc, n);
					for (std::uint32_t i = 0; i < n; ++i) {
						PendingImport w{};
						RE::FormID    ra = 0, rb = 0;
						if (!Read(a_intfc, w.a) || !Read(a_intfc, w.b) || !Read(a_intfc, w.kind)) {
							break;
						}
						if (a_intfc->ResolveFormID(w.a, ra) && a_intfc->ResolveFormID(w.b, rb)) {
							pendingImports.push_back({ std::min(ra, rb), std::max(ra, rb), w.kind });  // 0.21.1: a load-order change can swap the two
						}
					}
					continue;
				}
				if (type == 'LADI') {  // 0.17.0: the game's couples were imported already
					std::uint8_t done = 0;
					Read(a_intfc, done);
					gameImportDone = done != 0;
					continue;
				}
				if (type == 'LADG') {  // refs this mod handed an Amulet of Mara to (was written but never read before 0.7.2)
					givenLoaded = ReadFormList(a_intfc);
					continue;
				}
				if (type == 'LADM') {  // NPCs the player has met
					auto list = ReadFormList(a_intfc);
					std::scoped_lock flock(followerGuard);
					playerMet = std::unordered_set<RE::FormID>(list.begin(), list.end());
					continue;
				}
				if (type == 'LADK' || type == 'LADC' || type == 'LADY') {  // remembered followers, children choices
					auto list = ReadFormList(a_intfc);
					std::scoped_lock flock(followerGuard);
					auto& set = type == 'LADK' ? knownFollowers : type == 'LADC' ? childrenNever : childrenAlways;
					set = std::unordered_set<RE::FormID>(list.begin(), list.end());
					continue;
				}
				if (type == 'LADL') {  // 0.13.3: forms we put into Love Is In The Air's exclusion list
					Liita::SetAdded(ReadFormList(a_intfc));
					continue;
				}
				if (type == 'LADF') {  // custom followers the player let into the ladder
					auto list = ReadFormList(a_intfc);
					std::scoped_lock flock(followerGuard);
					allowedFollowers = std::move(list);
					continue;
				}
				if (type == 'LADP') {  // women an engine reported as expecting
					pregnant = ReadFormList(a_intfc);
					continue;
				}
				if (type == kRecordChronicle) {
					std::uint32_t n = 0;
					if (!Read(a_intfc, n)) {
						continue;
					}
					std::scoped_lock clock(chronicleGuard);
					for (std::uint32_t i = 0; i < n; ++i) {
						ChronicleEntry e;
						std::uint16_t  len = 0;
						if (!Read(a_intfc, e.day) || !Read(a_intfc, len)) {
							break;
						}
						e.text.resize(len);
						if (len && a_intfc->ReadRecordData(e.text.data(), len) != len) {
							break;
						}
						chronicle.push_back(std::move(e));
					}
					continue;
				}
				if (type != kRecordPairs) {
					continue;
				}
				std::uint32_t count = 0;
				if (!Read(a_intfc, count)) {
					break;
				}
				std::uint32_t dropped = 0;
				for (std::uint32_t i = 0; i < count; ++i) {
					Pair p;
					std::uint8_t flags = 0;
					bool ok = Read(a_intfc, p.a) && Read(a_intfc, p.b) && Read(a_intfc, p.stage) && Read(a_intfc, p.realEncounters) &&
							  Read(a_intfc, p.offscreenEncounters) && Read(a_intfc, p.closeness) && Read(a_intfc, p.firstDay) &&
							  Read(a_intfc, p.lastDay) && Read(a_intfc, p.stageDay) && Read(a_intfc, p.straysByA) && Read(a_intfc, p.straysByB) &&
							  Read(a_intfc, flags);
					if (ok && version >= 2) {
						ok = Read(a_intfc, p.coldSinceDay);
					}
					if (ok && version >= 3) {
						ok = Read(a_intfc, p.children);
					}
					if (ok && version >= 4) {
						RE::FormID home = 0;
						ok = Read(a_intfc, home);
						if (ok && home && !a_intfc->ResolveFormID(home, p.home)) {
							p.home = 0;  // its plugin is gone
						}
					}
					if (ok && version >= 5) {
						ok = Read(a_intfc, p.weddingDay);
					}
					if (ok && version >= 6) {
						ok = Read(a_intfc, p.weddingNotice);
					} else if (ok) {
						p.weddingNotice = p.weddingDay > p.stageDay;  // v5 sent the invitation when it set the day
					}
					if (ok && version >= 7) {
						ok = Read(a_intfc, p.altarRoll) && Read(a_intfc, p.invitedPlayer) && Read(a_intfc, p.waitedDays);
					}
					if (ok && version >= 8) {
						std::uint8_t flags2 = 0;
						ok = Read(a_intfc, flags2) && Read(a_intfc, p.quest) && Read(a_intfc, p.endedFrom) && Read(a_intfc, p.endedBy);
						p.frozen = (flags2 & 1) != 0;
						p.secret = (flags2 & 2) != 0;
						p.familyBlock = (flags2 & 4) != 0;
						p.exposed = (flags2 & 8) != 0;
						p.activeA = (flags2 & 16) != 0;
						p.byPlayer = (flags2 & 32) != 0;
					}
					if (!ok) {
						SKSE::log::error("Ladder: save record ended early at pair {}", i);
						break;
					}
					p.createdByMod = (flags & 1) != 0;
					p.protectedVanilla = (flags & 2) != 0;
					p.cold = (flags & 4) != 0;
					p.imported = (flags & 8) != 0;
					RE::FormID ra = 0, rb = 0;
					if (!a_intfc->ResolveFormID(p.a, ra) || !a_intfc->ResolveFormID(p.b, rb)) {
						++dropped;
						continue;
					}
					p.a = ra;
					p.b = rb;
					if (p.a > p.b) {
						std::swap(p.a, p.b);
						std::swap(p.straysByA, p.straysByB);
						if (p.altarRoll >= 2) {
							p.altarRoll = p.altarRoll == 2 ? 3 : 2;  // the one staying away keeps their side
						}
						p.activeA = !p.activeA;  // the one who pines / the vampire keeps their side
						if (p.endedBy) {
							p.endedBy = p.endedBy == 1 ? 2 : 1;
						}
					}
					pairs.push_back(p);
				}
				// (0.12.4's "game courting couples back to dating" repair is gone in 0.19.0: the couple table sets each game couple's
				// stage and quest lock, and a frozen couple never moves on its own)
				SKSE::log::info("Ladder: loaded {} pair(s), {} dropped (plugin gone)", pairs.size(), dropped);
			}
			SKSE::log::info("Ladder: loaded {} amulet hand-out(s), {} expecting, {} news line(s)", givenLoaded.size(), pregnant.size(), ChronicleSize());
			Amulet::SetGivenRefs(std::move(givenLoaded));  // Amulet never takes the ladder lock, so this is safe under it
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			Babies::ClearNotes();  // 0.25.1: no baby notes from the save that was open before this one
			{
				std::scoped_lock flock(followerGuard);
				allowedFollowers.clear();
				playerMet.clear();
				knownFollowers.clear();
				childrenNever.clear();
				childrenAlways.clear();
			}
			{
				std::scoped_lock clock(chronicleGuard);
				chronicle.clear();
			}
			Amulet::SetGivenRefs({});
			Liita::SetAdded({});
			std::scoped_lock lock(guard);
			pairs.clear();
			knownRefs.clear();
			news.clear();          // 0.21.1: letters / rank changes queued in the previous save must not land in this one
			playerChosen.clear();  // 0.21.1: match-maker pairs are per save too
			ledgerQueue.clear();
			communityMonths.clear();
			pregnant.clear();
			lastSimDay = 0;
			gameImportDone = false;
			tableApplied = 0;
			protectedPassDone = false;
			spreadLoveDay = -1.0f;
			questDone = 0;
			modRulesOn.clear();
			outcomesDone.clear();
			rankSnapshot.clear();
			rankSnapshotTaken = false;
			styleChoice.clear();
		}
	}

	const char* StageName(Stage a_stage)
	{
		switch (a_stage) {
		case Stage::None: return "none";
		case Stage::Casual: return "casual";
		case Stage::Candidate: return "candidate";
		case Stage::Engaged: return "engaged";
		case Stage::Married: return "married";
		case Stage::Jilted: return "jilted";
		case Stage::Divorced: return "divorced";
		case Stage::Widowed: return "widowed";
		case Stage::Affair: return "affair";
		case Stage::Crush: return "crush";
		case Stage::Thrall: return "thrall";
		}
		return "?";
	}

	std::string StageTitle(Stage a_stage)
	{
		if (a_stage == Stage::None) {
			return "Drifted apart";  // was "None": a pair that met but it came to nothing, or a fling that fizzled out
		}
		// the menu uses the words players see in the news (StagePhrase), not the internal names (still used by the INI and logs)
		if (a_stage == Stage::Casual) {
			return "Seeing each other";
		}
		if (a_stage == Stage::Candidate) {
			return "Dating";
		}
		std::string s = StageName(a_stage);
		if (!s.empty()) {
			s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
		}
		return s;
	}

	// 0.19.3: an ended pair says how it ended - "Drifted apart" only when it really fizzled out. 0.20.1: short (the stage column is
	// narrow) - who ended it goes on the notes line under the names
	std::string PairTitle(const Pair& a_pair)
	{
		const bool someoneEnded = a_pair.endedBy == 1 || a_pair.endedBy == 2;
		switch (a_pair.stage) {
		case Stage::None:
			switch (static_cast<Stage>(a_pair.endedFrom)) {
			case Stage::Crush: return "Crush faded";
			case Stage::Affair: return "Affair ended";
			case Stage::Thrall: return "Thrall freed";
			case Stage::Candidate: return "Broke up";
			case Stage::Casual: return someoneEnded ? "Broke up" : "Drifted apart";
			default: return "Drifted apart";
			}
		default: return StageTitle(a_pair.stage);
		}
	}

	std::string StagePhrase(const std::string& a_a, const std::string& a_b, Stage a_stage)
	{
		switch (a_stage) {
		case Stage::Casual: return a_a + " and " + a_b + " have been seeing each other";
		case Stage::Candidate: return a_a + " and " + a_b + " are now dating";
		case Stage::Engaged: return a_a + " and " + a_b + " are engaged";
		case Stage::Married: return a_a + " and " + a_b + " are married";
		case Stage::Jilted: return a_a + " and " + a_b + " have broken off their engagement";
		case Stage::Divorced: return a_a + " and " + a_b + " have divorced";
		case Stage::Widowed: return a_a + " has lost " + a_b;
		default: return a_a + " and " + a_b + " have parted ways";
		}
	}

	void DebugNotify(std::string a_msg)
	{
		if (!Settings::Get().debugNotifications) {
			return;
		}
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([m = std::move(a_msg)] { RE::SendHUDMessage::ShowHUDMessage(("[Reforged] " + m).c_str()); });
		}
	}

	void Chronicle(std::string a_text, float a_day)
	{
		{
			std::scoped_lock clock(chronicleGuard);
			chronicle.push_back({ a_day >= 0.0f ? a_day : DaysNow(), a_text });
			while (chronicle.size() > kChronicleMax) {
				chronicle.pop_front();
			}
		}
		DebugNotify(std::move(a_text));
	}

	std::vector<ChronicleEntry> ChronicleSnapshot()
	{
		std::scoped_lock clock(chronicleGuard);
		return { chronicle.rbegin(), chronicle.rend() };
	}

	std::mutex& Mutex()
	{
		return guard;
	}

	Pair* CommittedPairOfUnlocked(RE::FormID a_npc)
	{
		for (auto& p : pairs) {
			if (p.IsCommitted() && (p.a == a_npc || p.b == a_npc)) {
				return &p;
			}
		}
		return nullptr;
	}

	float DaysNow()
	{
		auto* cal = RE::Calendar::GetSingleton();
		return cal ? cal->GetDaysPassed() : 0.0f;
	}

	std::int32_t LastSimDay()
	{
		std::scoped_lock lock(guard);
		return lastSimDay;
	}

	void SetLastSimDay(std::int32_t a_day)
	{
		std::scoped_lock lock(guard);
		lastSimDay = a_day;
	}

	// "Start fresh" (Advanced page, asked twice): forget every couple and all news in this save. Married couples this mod made get
	// their marriage undone first (ring, amulet, marriage faction - same as a divorce, without the news). Kept: your follower
	// choices, children choices, who you have met, pregnancies (those belong to the pregnancy mod). The simulation restarts
	// from today, so there is no catch-up burst. Earlier save files still hold the old couples.
	std::size_t StartFresh()
	{
		std::vector<std::pair<RE::FormID, RE::FormID>> unmarry;
		std::size_t                                    count = 0;
		{
			std::scoped_lock lock(guard);
			for (const auto& p : pairs) {
				if (p.stage == Stage::Married) {
					unmarry.emplace_back(RefOf(p.a), RefOf(p.b));
				}
			}
			count = pairs.size();
			pairs.clear();
			news.clear();
			ledgerQueue.clear();
			communityMonths.clear();
			lastSimDay = static_cast<std::int32_t>(DaysNow());
		}
		for (const auto& [ra, rb] : unmarry) {
			Amulet::OnUnmarried(ra, rb);
		}
		{
			std::scoped_lock clock(chronicleGuard);
			chronicle.clear();
		}
		SKSE::log::info("Ladder: started fresh - {} pair(s) forgotten, {} marriage(s) undone", count, unmarry.size());
		return count;
	}

	std::string NpcName(RE::FormID a_base)
	{
		auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(a_base);
		if (npc && npc->GetName() && *npc->GetName()) {
			return npc->GetName();
		}
		return std::format("{:08X}", a_base);
	}

	void Init()
	{
		auto* ser = SKSE::GetSerializationInterface();
		if (!ser) {
			SKSE::log::error("Ladder: serialization interface unavailable - pairs will not persist");
			return;
		}
		ser->SetUniqueID(kUniqueID);
		ser->SetSaveCallback(OnSave);
		ser->SetLoadCallback(OnLoad);
		ser->SetRevertCallback(OnRevert);
		SKSE::log::info("Ladder: co-save registered");
	}

	bool CommunityMayPair(RE::FormID a_community, std::int32_t a_day)
	{
		const int cap = Settings::Get().maxNewCouplesPerCommunityPerMonth;
		if (cap <= 0 || a_community == 0) {
			return true;
		}
		const std::int32_t month = a_day / 30;
		std::scoped_lock lock(guard);
		for (const auto& c : communityMonths) {
			if (c.community == a_community && c.month == month) {
				return c.count < cap;
			}
		}
		return true;
	}

	void NoteCommunityPairing(RE::FormID a_community, std::int32_t a_day)
	{
		if (a_community == 0) {
			return;
		}
		const std::int32_t month = a_day / 30;
		std::scoped_lock lock(guard);
		for (auto& c : communityMonths) {
			if (c.community == a_community && c.month == month) {
				++c.count;
				return;
			}
		}
		communityMonths.push_back({ a_community, month, 1 });
	}

	// 0.20.3: both base NPCs speak with the same voice type (unknown voice = not the same)
	static bool SameVoiceType(RE::FormID a_x, RE::FormID a_y)
	{
		const auto* nx = RE::TESForm::LookupByID<RE::TESNPC>(a_x);
		const auto* ny = RE::TESForm::LookupByID<RE::TESNPC>(a_y);
		if (!nx || !ny || !nx->voiceType || !ny->voiceType) {
			return false;
		}
		return nx->voiceType == ny->voiceType;
	}

	bool RecordEncounterByBase(RE::FormID a_x, RE::FormID a_y, bool a_offscreen, VanillaRelation a_vanilla, RE::FormID a_community, bool a_eager)
	{
		if (!Settings::Get().ladderEnabled || a_x == 0 || a_y == 0 || a_x == a_y) {
			return false;
		}
		// the game's own records decide protection, whatever the caller passed
		const VanillaRelation game = GameRelation(a_x, a_y);
		if (game != VanillaRelation::None) {
			a_vanilla = game;
		}
		if (a_vanilla == VanillaRelation::Married) {
			// 0.17.0: a game marriage counts once it was imported (ImportGameCouples made the pair); one that was not imported (dead,
			// missing, married through Love Is In The Air ...) is still left alone. An imported pair is never moved or broken when
			// protected - HandleStray and the drift code check protectedVanilla.
			std::scoped_lock lock(guard);
			if (!Find(MakeKey(a_x, a_y))) {
				return false;
			}
		}
		const float now = DaysNow();
		const auto  s = Settings::Get();
		if (IsMourning(a_x, now) || IsMourning(a_y, now)) {
			return false;
		}

		std::scoped_lock lock(guard);
		const Style sx = StyleOfUnlocked(a_x);
		const Style sy = StyleOfUnlocked(a_y);
		if (sx == Style::Never || sy == Style::Never) {
			return false;  // 0.19.0: no love life at all (the Greybeards)
		}
		if (a_vanilla == VanillaRelation::None && !Find(MakeKey(a_x, a_y)) && SameVoiceType(a_x, a_y) &&
			!playerChosen.contains({ MakeKey(a_x, a_y).a, MakeKey(a_x, a_y).b })) {
			return false;  // 0.20.3: two of one voice sound like one person talking to themselves - never start a couple (a hand-added one still runs)
		}
		HandleStray(a_x, a_y, now);
		HandleStray(a_y, a_x, now);

		const Key key = MakeKey(a_x, a_y);
		Pair* p = Find(key);
		if (!p) {
			Pair fresh;
			fresh.a = key.a;
			fresh.b = key.b;
			fresh.firstDay = now;
			fresh.stageDay = now;
			fresh.createdByMod = a_vanilla == VanillaRelation::None;
			// 0.19.0: a game courting record no longer locks a couple by itself - the couple table decides which ones a quest owns
			fresh.stage = a_vanilla == VanillaRelation::Courting ? Stage::Candidate : Stage::None;
			pairs.push_back(fresh);
			p = &pairs.back();
		}
		if (a_offscreen) {
			++p->offscreenEncounters;
		} else {
			++p->realEncounters;
		}
		p->lastDay = now;
		p->closeness = std::min(100.0f, p->closeness + s.closenessGain);
		if (p->cold && p->closeness >= 50.0f) {
			p->cold = false;  // a cold marriage warms up again
		}
		// 0.19.0: a quest-locked couple only counts the meeting; a thrall never changes
		if (p->frozen || p->stage == Stage::Thrall) {
			return true;
		}
		const bool freeSpirit = sx == Style::FreeSpirit || sy == Style::FreeSpirit;
		Pair*      ox = CommittedPairOfUnlocked(a_x);
		Pair*      oy = CommittedPairOfUnlocked(a_y);
		if (ox == p) {
			ox = nullptr;
		}
		if (oy == p) {
			oy = nullptr;
		}
		const bool takenElsewhere = ox || oy;

		// 0.19.0 affair: every meeting already counted as cheating (HandleStray); now and then it comes out
		if (p->stage == Stage::Affair) {
			if (!p->exposed && s.gossip && Roll(4.0f)) {
				p->exposed = true;
				for (Pair* main : { ox, oy }) {
					if (main && !main->frozen) {
						main->closeness = std::max(0.0f, main->closeness - 30.0f);
					}
				}
				SKSE::log::info("Ladder: the affair of {} and {} has come out", NpcName(p->a), NpcName(p->b));
				Chronicle(std::format("Word is out: {} and {} are having an affair", NpcName(p->a), NpcName(p->b)), now);
				for (const RE::FormID cheat : { a_x, a_y }) {
					const Pair* main = cheat == a_x ? ox : oy;
					if (main && news.size() < 200) {
						news.push_back({ 6, cheat, main->a == cheat ? main->b : main->a });  // gossip: cheat strayed on their partner
					}
				}
			}
			if (!takenElsewhere) {
				p->stage = Stage::Casual;  // the marriage it ran behind is over: it can come out in the open (a free spirit's lover too)
				p->stageDay = now;
				p->exposed = false;
			}
			return true;
		}

		// 0.19.0 crush: now and then it becomes mutual (both single, the other one sparks too)
		if (p->stage == Stage::Crush) {
			if (!takenElsewhere && Roll(s.sparkChance * 0.5f * ArchetypeWeight(a_x, a_y, 0))) {
				Promote(*p, Stage::Casual, now);
			}
			return true;
		}

		// 0.12.0: nothing starts by itself. A meeting between two people with no relationship (or exes, at half the chance)
		// rolls SparkChance x archetypes (a scene in front of the player counts double); most sparks start as seeing each
		// other, DirectDatingShare x archetypes go straight to dating. Someone already committed elsewhere never sparks -
		// 0.19.0: but meeting the same person again and again (AffairAfter) is an affair, and a free spirit's lover who is
		// taken has an affair with them straight from a spark.
		if (IsEndedStage(p->stage)) {
			if (HeartbrokenUnlocked(a_x, now) || HeartbrokenUnlocked(a_y, now)) {
				return true;  // 0.19.0: still getting over the last one
			}
			const bool  exes = p->stage == Stage::Jilted || p->stage == Stage::Divorced;
			const float spark = s.sparkChance * ArchetypeWeight(a_x, a_y, 0) * (a_offscreen ? 1.0f : 2.0f) * (exes ? 0.5f : 1.0f) *
			                    (freeSpirit ? 1.5f : 1.0f) * MatchmakerBoostUnlocked(a_x, now);
			if (takenElsewhere) {
				if (s.infidelity && (p->Encounters() >= static_cast<std::uint32_t>(std::max(1, s.affairAfter)) || (freeSpirit && Roll(spark)))) {
					Promote(*p, Stage::Affair, now);  // quiet: no news (Promote skips side stages)
					SKSE::log::info("Ladder: {} + {} -> affair (secret)", NpcName(p->a), NpcName(p->b));
				}
				return true;
			}
			if (Roll(spark)) {
				// a simulated couple may only start dating where the community pace cap allows (a_community set)
				const bool mayDate = (!a_offscreen || a_community != 0) && !freeSpirit;
				if (mayDate && Roll(s.directDatingShare * ArchetypeWeight(a_x, a_y, 1))) {
					Promote(*p, Stage::Candidate, now);
					NoteCommunityUnlocked(a_community, now);
				} else {
					Promote(*p, Stage::Casual, now);
				}
			}
			return true;
		}
		if (freeSpirit) {
			return true;  // 0.19.0: a free spirit never goes past seeing each other
		}
		const float pace = Pace() * ThresholdMultiplier(a_x, a_y) * (a_eager ? 0.5f : 1.0f);
		const float tm = ThresholdMultiplier(a_x, a_y);
		const float moveUp = s.promoteChance * ArchetypeWeight(a_x, a_y, 2);
		if (p->stage == Stage::Casual && p->Encounters() >= static_cast<std::uint32_t>(std::max(1.0f, s.candidateEncounters * tm)) &&
			(now - p->firstDay) >= s.candidateMinDays * pace && Roll(moveUp)) {
			// exclusivity: neither may already be committed elsewhere; community pace cap for simulated couples
			if (!takenElsewhere) {
				Promote(*p, Stage::Candidate, now);
				NoteCommunityUnlocked(a_community, now);
			}
		} else if (p->stage == Stage::Candidate && !p->familyBlock && p->Encounters() >= static_cast<std::uint32_t>(std::max(1.0f, s.engagedEncounters * tm)) &&
				   (now - p->stageDay) >= s.engagedMinDays * pace && Roll(moveUp)) {
			Promote(*p, Stage::Engaged, now);
		}
		return true;
	}

	void AllowPlayerChosen(RE::Actor* a_x, RE::Actor* a_y)
	{
		auto* bx = a_x ? BaseOf(a_x) : nullptr;
		auto* by = a_y ? BaseOf(a_y) : nullptr;
		if (!bx || !by || bx == by) {
			return;
		}
		std::scoped_lock lock(guard);
		const Key key = MakeKey(bx->GetFormID(), by->GetFormID());
		playerChosen.insert({ key.a, key.b });
	}

	bool RecordEncounter(RE::Actor* a_x, RE::Actor* a_y, bool a_offscreen, VanillaRelation a_vanilla)
	{
		if (!Eligible(a_x) || !Eligible(a_y) || a_x == a_y) {
			return false;
		}
		{
			std::scoped_lock lock(guard);
			knownRefs[BaseOf(a_x)->GetFormID()] = a_x->GetFormID();
			knownRefs[BaseOf(a_y)->GetFormID()] = a_y->GetFormID();
		}
		return RecordEncounterByBase(BaseOf(a_x)->GetFormID(), BaseOf(a_y)->GetFormID(), a_offscreen, a_vanilla, 0);
	}

	// days from a_day to the nearest OTHER wedding the ladder knows of: one that is set (engaged, wedding day current) or one
	// already held (a mod-made married couple: the day they married). Very large when there is none.
	static float NearestWedding(const Pair* a_self, float a_day)
	{
		float best = 1.0e9f;
		for (const auto& p : pairs) {
			if (&p == a_self) {
				continue;
			}
			float d = -1.0f;
			if (p.stage == Stage::Engaged && p.weddingDay > p.stageDay) {
				d = std::floor(p.weddingDay);
			} else if (p.stage == Stage::Married && p.createdByMod) {
				d = std::floor(p.stageDay);
			}
			if (d >= 0.0f) {
				best = std::min(best, std::abs(d - a_day));
			}
		}
		return best;
	}

	// engaged long enough to marry? Weddings take turns: at most one every a_gap days across the whole ladder (WeddingGapDays),
	// so a couple whose time is up waits for the next free day.
	// RealWeddings off: they marry quietly as soon as their day is free.
	// RealWeddings on: the first time this is reached the wedding is set on the first free day at least two days out (noon-17:00); the
	// invitation (news 7) goes out exactly two days before; the ceremony can happen any time in the afternoon (noon to 17:00)
	// (Papyrus CompleteWedding when the player is there), otherwise they marry quietly at 17:00. A wedding day from an earlier engagement is ignored (it is older than the current stage).
	static bool MarriageDue(Pair& a_pair, float a_now, float a_after, bool a_realWeddings, float a_gap)
	{
		if (a_pair.stage == Stage::Engaged && a_pair.frozen) {
			return false;  // 0.19.0: a quest-locked couple (Vittoria + Asgeir, Keerava + Talen-Jei) marries through its quest, never on our schedule
		}
		if (a_pair.stage != Stage::Engaged || (a_now - a_pair.stageDay) < a_after) {
			return false;
		}
		if (!a_realWeddings) {
			return NearestWedding(&a_pair, std::floor(a_now)) >= a_gap;
		}
		if (a_pair.weddingDay <= a_pair.stageDay) {
			float day = std::floor(a_now) + 2.0f;
			while (NearestWedding(&a_pair, day) < a_gap) {
				day += 1.0f;
			}
			a_pair.weddingDay = day + 12.0f / 24.0f;  // the window opens at noon
			a_pair.weddingNotice = false;
			a_pair.altarRoll = 0;  // a new date: everything about the old one starts over
			a_pair.invitedPlayer = false;
			a_pair.waitedDays = 0;
			const int inDays = static_cast<int>(day - std::floor(a_now));
			SKSE::log::info("Ladder: {} + {} wedding set for day {:.2f}", NpcName(a_pair.a), NpcName(a_pair.b), a_pair.weddingDay);
			Chronicle(std::format("{} and {} set their wedding at the Temple of Mara for {} days from now", NpcName(a_pair.a), NpcName(a_pair.b), inDays), a_now);
		}
		if (!a_pair.weddingNotice && a_now >= std::floor(a_pair.weddingDay) - 2.0f) {
			a_pair.weddingNotice = true;
			if (news.size() < 200) {
				news.push_back({ 7, a_pair.a, a_pair.b });
			}
		}
		// 0.14.0 stood up at the altar: decided once, two days before (the invitations are out). A small chance for every couple, more
		// after straying or when they are not close; the one who strayed more is the one who stays away.
		if (a_pair.altarRoll == 0 && a_now >= std::floor(a_pair.weddingDay) - 2.0f) {
			const int base = Settings::Get().altarNoShowChance;  // 0.14.1 setting; 0 = never
			float     pct = static_cast<float>(base) + 10.0f * static_cast<float>(a_pair.straysByA + a_pair.straysByB) + (a_pair.closeness < 40.0f ? 10.0f : 0.0f);
			pct = std::min(pct, 40.0f);
			a_pair.altarRoll = 1;
			if (base > 0 && Roll(pct)) {
				const bool aRuns = a_pair.straysByA != a_pair.straysByB ? a_pair.straysByA > a_pair.straysByB : Roll(50.0f);
				a_pair.altarRoll = aRuns ? 2 : 3;
				SKSE::log::info("Ladder: {} will not come to the wedding with {} ({:.0f}% chance)", NpcName(aRuns ? a_pair.a : a_pair.b),
					NpcName(aRuns ? a_pair.b : a_pair.a), pct);
			}
		}
		if (a_now < a_pair.weddingDay + 5.0f / 24.0f) {
			return false;
		}
		// 17:00 and nobody held it
		if (a_pair.altarRoll >= 2) {
			const RE::FormID runner = a_pair.altarRoll == 2 ? a_pair.a : a_pair.b;
			const RE::FormID waiting = a_pair.altarRoll == 2 ? a_pair.b : a_pair.a;
			Chronicle(std::format("{} never came to the wedding - {} was left waiting at the altar", NpcName(runner), NpcName(waiting)), a_now);
			Breakup(a_pair, a_now);  // engaged -> jilted, breakup news
			return false;
		}
		if (a_pair.invitedPlayer && a_pair.waitedDays < std::clamp(Settings::Get().weddingWaitDays, 0, 7)) {
			a_pair.weddingDay += 1.0f;
			++a_pair.waitedDays;
			Chronicle(std::format("{} and {} put their wedding off a day - they are waiting for you", NpcName(a_pair.a), NpcName(a_pair.b)), a_now);
			return false;
		}
		return true;  // married quietly
	}
	void AdvanceDay(float a_day, const std::function<bool(RE::FormID)>& a_driftPaused)
	{
		const auto  s = Settings::Get();
		const float pace = Pace();
		std::scoped_lock lock(guard);
		for (auto& p : pairs) {
			// engaged -> married by time
			if (MarriageDue(p, a_day, s.marriedAfterDays * pace, s.realWeddings, static_cast<float>(std::max(1, s.weddingGapDays)))) {
				Promote(p, Stage::Married, a_day);
				continue;
			}
			if (IsEndedStage(p.stage) || p.frozen || p.stage == Stage::Thrall) {
				continue;  // 0.19.0: quest-locked couples and thralls never drift
			}
			// 0.19.0: a family block lifts after FamilyBlockDays of steady dating; a secret couple comes out after three months
			if (p.familyBlock && p.stage == Stage::Candidate && s.familyBlockDays > 0 && (a_day - p.stageDay) >= s.familyBlockDays * pace) {
				p.familyBlock = false;
				Chronicle(std::format("The families of {} and {} have come round to them", NpcName(p.a), NpcName(p.b)), a_day);
			}
			if (p.secret && p.IsCommitted() && s.gossip && (a_day - p.stageDay) >= 90.0f * pace) {
				p.secret = false;
				Chronicle(std::format("It's out: {} and {} have been together for a while", NpcName(p.a), NpcName(p.b)), a_day);
			}
			if (p.lastDay >= a_day - 0.5f) {
				continue;  // met today
			}
			if (a_driftPaused && (a_driftPaused(p.a) || a_driftPaused(p.b))) {
				continue;
			}
			if (std::find(pregnant.begin(), pregnant.end(), p.a) != pregnant.end() || std::find(pregnant.begin(), pregnant.end(), p.b) != pregnant.end()) {
				continue;  // expecting: no drift
			}
			p.closeness = std::max(0.0f, p.closeness - (s.driftPerDay / pace) / (1.0f + static_cast<float>(p.children)));
			if (p.closeness > 0.0f) {
				continue;
			}
			switch (p.stage) {
			case Stage::Casual:
				p.endedFrom = static_cast<std::uint8_t>(Stage::Casual);
				p.endedBy = 0;
				p.stage = Stage::None;
				p.stageDay = a_day;
				break;
			case Stage::Affair:
			case Stage::Crush:
				// 0.19.0: an affair fizzles out, a crush fades - quietly (the rank drop keeps their own romance lines off)
				SKSE::log::info("Ladder: {} + {} {} ended quietly", NpcName(p.a), NpcName(p.b), StageName(p.stage));
				p.endedFrom = static_cast<std::uint8_t>(p.stage);
				p.endedBy = 0;
				p.stage = Stage::None;
				p.stageDay = a_day;
				p.exposed = false;
				if (news.size() < 200) {
					news.push_back({ 8, p.a, p.b });
				}
				break;
			case Stage::Candidate:
				// 0.19.0: a couple the game itself made (Brina + Horik, 30 years together) cools but never drifts apart just because the
				// simulation rarely has them meet - only cheating can end it
				if (!p.protectedVanilla && !p.imported) {
					p.endedFrom = static_cast<std::uint8_t>(Stage::Candidate);
					p.endedBy = 0;
					if (news.size() < 200) {
						news.push_back({ 8, p.a, p.b });  // drifted apart: their own romance lines stop
					}
					p.stage = Stage::None;
					p.stageDay = a_day;
					SKSE::log::info("Ladder: {} + {} drifted apart", NpcName(p.a), NpcName(p.b));
					Chronicle(std::format("{} and {} drifted apart", NpcName(p.a), NpcName(p.b)), a_day);
				}
				break;
			case Stage::Engaged:
				p.stage = Stage::Candidate;
				p.stageDay = a_day;
				SKSE::log::info("Ladder: {} + {} engagement cooled to candidate", NpcName(p.a), NpcName(p.b));
				Chronicle(std::format("{} and {}: engagement cooled, back to dating", NpcName(p.a), NpcName(p.b)), a_day);
				break;
			case Stage::Married:
				if (!p.cold) {
					p.cold = true;
					p.coldSinceDay = a_day;
					SKSE::log::info("Ladder: {} + {} marriage went cold", NpcName(p.a), NpcName(p.b));
					Chronicle(std::format("{} and {}: marriage has gone cold", NpcName(p.a), NpcName(p.b)), a_day);
				} else if (p.imported && !s.gameMarriagesCanEnd) {
					p.coldSinceDay = a_day;  // 0.21.1: the divorce clock of a game marriage only runs while "Game marriages can end" is on
				} else if (!p.protectedVanilla && p.children == 0 && (p.straysByA > 0 || p.straysByB > 0) &&
						   (a_day - p.coldSinceDay) >= s.coldDaysBeforeDivorce * pace) {
					Breakup(p, a_day);  // couples with children never divorce over coldness
				}
				break;
			default:
				break;
			}
		}
	}

	void Tick()
	{
		// kept for the Papyrus native: the simulation (Simulation::Tick) does the day work and calls AdvanceDay;
		// without the simulation enabled we still promote engaged -> married by time.
		if (!Settings::Get().ladderEnabled || Settings::Get().simulationEnabled) {
			return;
		}
		const float now = DaysNow();
		const auto  s = Settings::Get();
		const float pace = Pace();
		std::scoped_lock lock(guard);
		for (auto& p : pairs) {
			if (MarriageDue(p, now, s.marriedAfterDays * pace, s.realWeddings, static_cast<float>(std::max(1, s.weddingGapDays)))) {
				Promote(p, Stage::Married, now);
			}
		}
	}

	std::uint32_t EncountersBetween(RE::FormID a_x, RE::FormID a_y)
	{
		std::scoped_lock lock(guard);
		auto* p = Find(MakeKey(a_x, a_y));
		return p ? p->Encounters() : 0;
	}

	Stage StageBetween(RE::FormID a_x, RE::FormID a_y)
	{
		std::scoped_lock lock(guard);
		auto* p = Find(MakeKey(a_x, a_y));
		return p ? p->stage : Stage::None;
	}

	RE::FormID CommittedPartnerOf(RE::FormID a_npc, Stage* a_stageOut, std::uint8_t* a_strikesOut, bool* a_coldOut)
	{
		std::scoped_lock lock(guard);
		auto* p = CommittedPairOfUnlocked(a_npc);
		if (!p) {
			return 0;
		}
		if (a_stageOut) {
			*a_stageOut = p->stage;
		}
		if (a_strikesOut) {
			*a_strikesOut = p->a == a_npc ? p->straysByA : p->straysByB;
		}
		if (a_coldOut) {
			*a_coldOut = p->cold;
		}
		return p->a == a_npc ? p->b : p->a;
	}

	void MarkWidowed(RE::FormID a_deadNpc, float a_day)
	{
		std::scoped_lock lock(guard);
		for (auto& p : pairs) {
			if (p.IsCommitted() && (p.a == a_deadNpc || p.b == a_deadNpc)) {
				p.stage = Stage::Widowed;
				p.stageDay = a_day;
				SKSE::log::info("Ladder: {} + {} -> widowed", NpcName(p.a), NpcName(p.b));
				Chronicle(std::format("{} has died - {} is widowed", NpcName(a_deadNpc), NpcName(p.a == a_deadNpc ? p.b : p.a)), a_day);
				if (news.size() < 200) {
					news.push_back({ 5, p.a == a_deadNpc ? p.b : p.a, a_deadNpc });
				}
				if (p.stage == Stage::Widowed) {
					Amulet::OnUnmarried(RefOf(p.a), RefOf(p.b));
				}
			}
		}
	}

	Stage GetStage(RE::Actor* a_x, RE::Actor* a_y)
	{
		auto* bx = BaseOf(a_x);
		auto* by = BaseOf(a_y);
		if (!bx || !by) {
			return Stage::None;
		}
		return StageBetween(bx->GetFormID(), by->GetFormID());
	}

	void SetPregnant(RE::FormID a_woman, bool a_pregnant)
	{
		std::scoped_lock lock(guard);
		auto it = std::find(pregnant.begin(), pregnant.end(), a_woman);
		if (a_pregnant && it == pregnant.end()) {
			pregnant.push_back(a_woman);
			SKSE::log::info("Ladder: {} is expecting", NpcName(a_woman));
			Chronicle(std::format("{} is expecting", NpcName(a_woman)));
		} else if (!a_pregnant && it != pregnant.end()) {
			pregnant.erase(it);
		}
	}

	bool IsPregnant(RE::FormID a_npc)
	{
		std::scoped_lock lock(guard);
		return std::find(pregnant.begin(), pregnant.end(), a_npc) != pregnant.end();
	}

	void AddChild(RE::FormID a_mother, RE::FormID a_father)
	{
		std::scoped_lock lock(guard);
		auto it = std::find(pregnant.begin(), pregnant.end(), a_mother);
		if (it != pregnant.end()) {
			pregnant.erase(it);
		}
		Pair* p = a_father ? Find(MakeKey(a_mother, a_father)) : nullptr;
		if (!p) {
			p = CommittedPairOfUnlocked(a_mother);
		}
		if (p && p->children < 250) {
			++p->children;
			p->closeness = 100.0f;
			p->cold = false;
			SKSE::log::info("Ladder: {} + {} have a child ({} now)", NpcName(p->a), NpcName(p->b), p->children);
			Chronicle(std::format("{} and {} have a child ({} now)", NpcName(p->a), NpcName(p->b), static_cast<int>(p->children)));
		}
	}

	bool IsMourning(RE::FormID a_npc, float a_now)
	{
		const auto  s = Settings::Get();
		if (s.mourningDays <= 0) {
			return false;
		}
		float days = static_cast<float>(s.mourningDays) * Pace();
		std::scoped_lock lock(guard);
		if (s.useMarasArchetypes) {
			auto it = archetypes.find(a_npc);
			if (it != archetypes.end()) {
				switch (it->second.temperament) {
				case 3: days *= 1.5f; break;  // Romantic
				case 1: days *= 1.25f; break; // Humble
				case 4: days *= 0.5f; break;  // Independent
				default: break;
				}
			}
		}
		for (const auto& p : pairs) {
			if (p.stage == Stage::Widowed && (p.a == a_npc || p.b == a_npc) && (a_now - p.stageDay) < days) {
				return true;
			}
		}
		return false;
	}

	bool IsHeartbroken(RE::FormID a_npc, float a_now)
	{
		std::scoped_lock lock(guard);
		return HeartbrokenUnlocked(a_npc, a_now);
	}

	bool IsCommittedTo(RE::Actor* a_x, RE::Actor* a_y)
	{
		auto* bx = BaseOf(a_x);
		auto* by = BaseOf(a_y);
		if (!bx || !by) {
			return false;
		}
		return CommittedPartnerOf(bx->GetFormID()) == by->GetFormID();
	}

	bool IsCommittedToOther(RE::Actor* a_x, RE::Actor* a_y)
	{
		auto* bx = BaseOf(a_x);
		if (!bx) {
			return false;
		}
		const RE::FormID other = BaseOf(a_y) ? BaseOf(a_y)->GetFormID() : 0;
		const RE::FormID partner = CommittedPartnerOf(bx->GetFormID());
		return partner != 0 && partner != other;
	}

	Stage GetPartnerStage(RE::Actor* a_x)
	{
		auto* bx = BaseOf(a_x);
		if (!bx) {
			return Stage::None;
		}
		Stage st = Stage::None;
		(void)CommittedPartnerOf(bx->GetFormID(), &st);
		return st;
	}

	RE::TESNPC* GetPartnerBase(RE::Actor* a_x)
	{
		auto* bx = BaseOf(a_x);
		if (!bx) {
			return nullptr;
		}
		const RE::FormID partner = CommittedPartnerOf(bx->GetFormID());
		return partner ? RE::TESForm::LookupByID<RE::TESNPC>(partner) : nullptr;
	}

	bool SetStage(RE::FormID a_a, RE::FormID a_b, Stage a_stage)
	{
		const float now = DaysNow();
		std::scoped_lock lock(guard);
		const Key key = MakeKey(a_a, a_b);
		Pair* p = Find(key);
		if (!p) {
			Pair fresh;
			fresh.a = key.a;
			fresh.b = key.b;
			fresh.firstDay = now;
			pairs.push_back(fresh);
			p = &pairs.back();
		}
		const Stage from = p->stage;
		SKSE::log::info("Ladder: forced {} + {} {} -> {}", NpcName(p->a), NpcName(p->b), StageName(from), StageName(a_stage));
		if ((a_stage == Stage::Candidate || a_stage == Stage::Engaged || a_stage == Stage::Married) && a_stage > from) {
			Promote(*p, a_stage, now);  // same path as a natural promotion: news, letters, amulet and ring handling
		} else if ((a_stage == Stage::None || a_stage == Stage::Jilted || a_stage == Stage::Divorced) &&
				   (from == Stage::Candidate || from == Stage::Engaged || from == Stage::Married)) {
			// 0.22.4 (review finding 8): "End it" on a committed pair is a real breakup - news, rank drop, home and strikes cleared,
			// "Broke up" on the Couples page (Breakup picks jilted / divorced / none from the stage they were at)
			Breakup(*p, now, 0);
		} else {
			if (from == Stage::Married && a_stage != Stage::Married) {
				Amulet::OnUnmarried(RefOf(p->a), RefOf(p->b));
			}
			p->stage = a_stage;
			p->stageDay = now;
			p->cold = false;
		}
		RE::SendHUDMessage::ShowHUDMessage(StagePhrase(NpcName(p->a), NpcName(p->b), p->stage).c_str());
		return true;
	}

	bool SetStage(RE::Actor* a_x, RE::Actor* a_y, Stage a_stage)
	{
		auto* bx = BaseOf(a_x);
		auto* by = BaseOf(a_y);
		if (!bx || !by || bx == by) {
			return false;
		}
		return SetStage(bx->GetFormID(), by->GetFormID(), a_stage);
	}

	void RememberRef(RE::FormID a_base, RE::FormID a_ref)
	{
		std::scoped_lock lock(guard);
		if (a_base && a_ref) {
			knownRefs[a_base] = a_ref;
		}
	}

	bool CreateCouple(RE::FormID a_a, RE::FormID a_b, Stage a_stage, bool a_keep)
	{
		if (!a_a || !a_b || a_a == a_b) {
			return false;
		}
		for (const auto base : { a_a, a_b }) {
			// never a child or a creature: adults of a person race only (checked on the base, and on the placed NPC when known)
			auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(base);
			if (!npc || !IsPersonRace(npc->race)) {
				return false;
			}
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(RefOfBase(base));
			if (actor && actor->IsChild()) {
				return false;
			}
		}
		if (a_stage == Stage::None) {
			a_stage = Stage::Casual;
		}
		SetStage(a_a, a_b, a_stage);
		std::scoped_lock lock(guard);
		if (Pair* p = Find(MakeKey(a_a, a_b))) {
			const float now = DaysNow();
			p->closeness = 100.0f;
			p->lastDay = now;
			p->createdByMod = false;
			p->protectedVanilla = a_keep;
			Chronicle(std::format("{} and {} were paired by hand ({}{})", NpcName(p->a), NpcName(p->b), StageName(p->stage), a_keep ? ", kept together" : ""), now);
		}
		return true;
	}

	// a partner cannot come (a quest has them): the wedding moves to the next free day, a new invitation goes out
	bool PostponeWedding(RE::FormID a_a, RE::FormID a_b)
	{
		std::scoped_lock lock(guard);
		Pair*            p = Find(MakeKey(a_a, a_b));
		if (!p || p->stage != Stage::Engaged) {
			return false;
		}
		p->weddingDay = 0.0f;
		p->weddingNotice = false;
		p->altarRoll = 0;
		p->invitedPlayer = false;
		p->waitedDays = 0;
		Chronicle(std::format("The wedding of {} and {} was put off", NpcName(p->a), NpcName(p->b)), DaysNow());
		return true;
	}

	RE::FormID AltarRunner(RE::FormID a_a, RE::FormID a_b)
	{
		std::scoped_lock lock(guard);
		const Pair*      p = Find(MakeKey(a_a, a_b));
		if (!p || p->stage != Stage::Engaged || p->altarRoll < 2) {
			return 0;
		}
		return p->altarRoll == 2 ? p->a : p->b;
	}

	bool SetWeddingInvite(RE::FormID a_a, RE::FormID a_b)
	{
		std::scoped_lock lock(guard);
		Pair*            p = Find(MakeKey(a_a, a_b));
		if (!p || p->stage != Stage::Engaged) {
			return false;
		}
		p->invitedPlayer = true;
		return true;
	}

	bool CallOffWedding(RE::FormID a_a, RE::FormID a_b)
	{
		std::scoped_lock lock(guard);
		Pair*            p = Find(MakeKey(a_a, a_b));
		if (!p || p->stage != Stage::Engaged) {
			return false;
		}
		p->weddingDay = 0.0f;  // a new date is set on the next day tick
		p->weddingNotice = false;
		p->altarRoll = 0;
		p->invitedPlayer = false;
		p->waitedDays = 0;
		p->closeness = std::max(0.0f, p->closeness - 20.0f);
		Chronicle(std::format("The wedding of {} and {} was stopped - someone objected during the ceremony", NpcName(p->a), NpcName(p->b)), DaysNow());
		return true;
	}

	bool EndForOtherMarriage(RE::FormID a_a, RE::FormID a_b)
	{
		std::scoped_lock lock(guard);
		Pair*            p = Find(MakeKey(a_a, a_b));
		if (!p || (p->stage != Stage::Casual && p->stage != Stage::Candidate && p->stage != Stage::Engaged)) {
			return false;
		}
		const float now = DaysNow();
		p->stage = p->stage == Stage::Engaged ? Stage::Jilted : Stage::None;
		p->stageDay = now;
		p->closeness = 0.0f;
		p->weddingDay = 0.0f;
		p->weddingNotice = false;
		p->altarRoll = 0;
		p->invitedPlayer = false;
		p->waitedDays = 0;
		Chronicle(std::format("{} and {} are no longer together - one of them married someone else (Love Is In The Air)", NpcName(p->a),
					  NpcName(p->b)),
			now);
		return true;
	}

	bool EndPair(RE::FormID a_a, RE::FormID a_b)
	{
		std::scoped_lock lock(guard);
		const Key key = MakeKey(a_a, a_b);
		for (auto it = pairs.begin(); it != pairs.end(); ++it) {
			if (it->a == key.a && it->b == key.b) {
				pairs.erase(it);
				return true;
			}
		}
		return false;
	}

	std::size_t ForgetStale(float a_minIdleDays, bool a_includeEnded, bool a_dryRun)
	{
		const auto  s = Settings::Get();
		const float now = DaysNow();
		// widowed pairs stay while any survivor could still be mourning (Romantic mourns longest, x1.5)
		const float mourning = s.mourningDays > 0 ? static_cast<float>(s.mourningDays) * Pace() * 1.5f : 0.0f;
		std::scoped_lock lock(guard);
		auto stale = [&](const Pair& p) {
			if (p.protectedVanilla) {
				return false;
			}
			const float idle = now - std::max(p.lastDay, p.stageDay);
			switch (p.stage) {
			case Stage::None:
				return idle >= a_minIdleDays;
			case Stage::Jilted:
			case Stage::Divorced:
				return a_includeEnded && idle >= a_minIdleDays;
			case Stage::Widowed:
				return a_includeEnded && idle >= std::max(a_minIdleDays, mourning);
			default:
				return false;
			}
		};
		const auto count = static_cast<std::size_t>(std::count_if(pairs.begin(), pairs.end(), stale));
		if (!a_dryRun && count > 0) {
			std::erase_if(pairs, stale);
			SKSE::log::info("Ladder: forgot {} stale pair(s) (idle >= {:.0f} days, ended pairs {})", count, a_minIdleDays, a_includeEnded ? "included" : "kept");
		}
		return count;
	}

	void QueueLedgerWrite(RE::FormID a_refA, RE::FormID a_refB, std::int32_t a_day)
	{
		std::scoped_lock lock(guard);
		if (ledgerQueue.size() < 5000) {
			ledgerQueue.push_back({ a_refA, a_refB, a_day });
		}
	}

	std::vector<LedgerWrite> PopLedgerWrites()
	{
		std::scoped_lock lock(guard);
		std::vector<LedgerWrite> out;
		out.swap(ledgerQueue);
		return out;
	}

	void SetArchetype(RE::FormID a_base, int a_class, int a_skill, int a_temperament)
	{
		std::scoped_lock lock(guard);
		auto& a = archetypes[a_base];
		a.socialClass = static_cast<std::int8_t>(std::clamp(a_class, -1, 7));
		a.skillType = static_cast<std::int8_t>(std::clamp(a_skill, -1, 5));
		a.temperament = static_cast<std::int8_t>(std::clamp(a_temperament, -1, 4));
	}

	Archetype GetArchetype(RE::FormID a_base)
	{
		std::scoped_lock lock(guard);
		auto it = archetypes.find(a_base);
		return it == archetypes.end() ? Archetype{} : it->second;
	}

	bool ArchetypeKnownOrRequested(RE::FormID a_base)
	{
		std::scoped_lock lock(guard);
		return archetypes.find(a_base) != archetypes.end();
	}

	void MarkArchetypeRequested(RE::FormID a_base)
	{
		std::scoped_lock lock(guard);
		archetypes.try_emplace(a_base, Archetype{ -2, -2, -2 });
	}

	std::vector<News> PopNews()
	{
		std::scoped_lock lock(guard);
		std::vector<News> out;
		out.swap(news);
		return out;
	}

	RE::FormID RefOfBase(RE::FormID a_base)
	{
		std::scoped_lock lock(guard);
		return RefOf(a_base);
	}

	void SetPoolRefs(std::unordered_map<RE::FormID, RE::FormID> a_baseToRef)
	{
		std::scoped_lock lock(guard);
		poolRefs = std::move(a_baseToRef);
	}

	std::vector<std::pair<RE::FormID, RE::FormID>> MarriedPairRefs()
	{
		std::scoped_lock lock(guard);
		std::vector<std::pair<RE::FormID, RE::FormID>> out;
		for (const auto& p : pairs) {
			if (p.stage == Stage::Married && !p.imported) {  // imported (game) marriages go through GameMarriedPairRefs: their own rings stay
				const RE::FormID ra = RefOf(p.a);
				const RE::FormID rb = RefOf(p.b);
				if (ra && rb) {
					out.emplace_back(ra, rb);
				}
			}
		}
		return out;
	}

	std::vector<std::pair<RE::FormID, RE::FormID>> CommittedPairRefs()
	{
		std::scoped_lock lock(guard);
		std::vector<std::pair<RE::FormID, RE::FormID>> out;
		for (const auto& p : pairs) {
			if (p.IsCommitted()) {
				const RE::FormID ra = RefOf(p.a);
				const RE::FormID rb = RefOf(p.b);
				if (ra && rb) {
					out.emplace_back(ra, rb);
				}
			}
		}
		return out;
	}

	std::vector<Pair> Snapshot()
	{
		std::scoped_lock lock(guard);
		return pairs;
	}

	// ---- custom followers: the player's own, out of the ladder unless allowed ----
	namespace
	{
		struct KnownForm
		{
			RE::FormID       local;
			std::string_view file;
		};
		// custom followers with their own follow system (never in the vanilla follower factions) - known by name so they are
		// kept for the player from the start, before they have ever travelled with them
		constexpr KnownForm kKnownFollowers[] = {
			{ 0x000D62, "Inigo.esp" },                     // Inigo
			{ 0x00AA0F, "AX ValSerano.esp" },              // Val Serano
			{ 0x005900, "Ashe - Fire and Blood.esp" },     // Ashe
			{ 0x559858, "Vigilant.esm" },                  // Martha (Vigilant)
		};
		// people who cannot have children by their nature (automatons and the like) - the player can still override per person
		constexpr KnownForm kNoChildren[] = {
			{ 0x005900, "Ashe - Fire and Blood.esp" },     // Ashe - an automaton
			{ 0x000988, "Lyra.esp" },                      // 0.21.3: Lyra Model-EV4 - an android (user rule 2026-10-22)
		};

		// 0.21.3: a vampire right now (user rule: no children unless cured). Serana carries the vampire trait as an alias spell of
		// Dawnguard's NPC mental model (cleared by her cure), Valerica / Harkon / Alva have the Vampire keyword on their record,
		// vampire races (vanilla, UBE, Khajiit Diversity) and a turned player have it on the race. Research: Notes and Reports\
		// Serana vampirism source + one vampire test 2026-10-22.md
		bool IsVampireNow(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			static auto* trait = RE::TESForm::LookupByID<RE::SpellItem>(0x000F5B5C);  // REQ_Trait_Vampire_NPC (vanilla vampire NPC abilities)
			static auto* vampire = RE::TESForm::LookupByID<RE::BGSKeyword>(0x000A82BB);  // Vampire
			if (trait && a_actor->HasSpell(trait)) {
				return true;
			}
			auto* base = a_actor->GetActorBase();
			auto* race = a_actor->GetRace();
			return vampire && ((base && base->HasKeyword(vampire)) || (race && race->HasKeyword(vampire)));
		}

		bool InKnownList(RE::FormID a_base, std::span<const KnownForm> a_list)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) {
				return false;
			}
			for (const auto& k : a_list) {
				if (dh->LookupFormID(k.local, k.file) == a_base) {
					return true;
				}
			}
			return false;
		}
	}

	bool IsCustomFollower(RE::Actor* a_actor)
	{
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		if (!base || IsVanillaFile(base)) {
			return false;
		}
		static auto* potential = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84D);  // PotentialFollowerFaction
		static auto* current = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84E);    // CurrentFollowerFaction
		static auto* dismissed = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84C);  // DismissedFollowerFaction
		if (a_actor->IsPlayerTeammate() || (potential && a_actor->IsInFaction(potential)) || (current && a_actor->IsInFaction(current)) ||
			(dismissed && a_actor->IsInFaction(dismissed))) {
			return true;
		}
		{
			std::scoped_lock flock(followerGuard);
			if (knownFollowers.contains(base->GetFormID())) {
				return true;
			}
		}
		return InKnownList(base->GetFormID(), kKnownFollowers);
	}

	void NoteFollowerSeen(RE::Actor* a_actor)
	{
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		if (!base || !a_actor->IsPlayerTeammate() || IsVanillaFile(base) || !IsHumanoid(a_actor)) {
			return;
		}
		std::scoped_lock flock(followerGuard);
		knownFollowers.insert(base->GetFormID());
	}

	bool IsModNpc(RE::Actor* a_actor)
	{
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		return base && !IsVanillaFile(base);
	}

	void MarkFollower(RE::FormID a_base, bool a_mark)
	{
		std::scoped_lock flock(followerGuard);
		if (a_mark) {
			knownFollowers.insert(a_base);
		} else {
			knownFollowers.erase(a_base);
		}
	}

	// ---- children ----
	namespace
	{
		std::mutex                                engineGuard;
		std::unordered_map<RE::FormID, int>       engineVerdict;  // base -> 0 no / 1 yes, as the pregnancy mod sees it (not saved; re-read)
	}

	void SetEngineChildren(RE::FormID a_base, int a_verdict)
	{
		std::scoped_lock lock(engineGuard);
		engineVerdict[a_base] = a_verdict;
	}

	int EngineChildren(RE::FormID a_base)
	{
		std::scoped_lock lock(engineGuard);
		const auto it = engineVerdict.find(a_base);
		return it == engineVerdict.end() ? -1 : it->second;
	}

	bool KnownNoChildren(RE::FormID a_base)
	{
		return InKnownList(a_base, kNoChildren);
	}

	namespace
	{
		// 0 men and mer, 1 Khajiit (any furstock, custom Khajiit races too), 2 Argonian - by race id, then by the race's name
		int Kind(const RE::TESRace* a_race)
		{
			if (!a_race) {
				return 0;
			}
			switch (a_race->GetFormID()) {
			case 0x00013745:
			case 0x00088845:
				return 1;
			case 0x00013740:
			case 0x0008883A:
				return 2;
			default:
				break;
			}
			std::string name = a_race->GetFullName() ? a_race->GetFullName() : "";
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			for (const auto* k : { "khajiit", "ohmes", "-raht", "pahmar", "cathay", "suthay", "tojay" }) {
				if (name.find(k) != std::string::npos) {
					return 1;
				}
			}
			for (const auto* k : { "argonian", "saxhleel", "lukiul" }) {
				if (name.find(k) != std::string::npos) {
					return 2;
				}
			}
			return 0;
		}
	}

	int ChildrenChoice(RE::FormID a_base)
	{
		std::scoped_lock flock(followerGuard);
		return childrenNever.contains(a_base) ? 2 : childrenAlways.contains(a_base) ? 1 : 0;
	}

	void SetChildrenChoice(RE::FormID a_base, int a_choice)
	{
		std::scoped_lock flock(followerGuard);
		childrenNever.erase(a_base);
		childrenAlways.erase(a_base);
		if (a_choice == 2) {
			childrenNever.insert(a_base);
		} else if (a_choice == 1) {
			childrenAlways.insert(a_base);
		}
	}

	bool CanHaveChildrenAlone(RE::Actor* a_actor)
	{
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		if (!base) {
			return false;
		}
		const int choice = ChildrenChoice(base->GetFormID());
		if (choice != 0) {
			return choice == 1;
		}
		if (EngineChildren(base->GetFormID()) == 0) {
			return false;  // the pregnancy mod says no (Beeing Female: pregnancy disabled for this person, their race or everyone)
		}
		if (Settings::Get().pregnancyRules && IsVampireNow(a_actor)) {
			return false;  // 0.21.3: vampires have no children until they are cured
		}
		return !InKnownList(base->GetFormID(), kNoChildren);
	}

	bool CanHaveChildren(RE::Actor* a_a, RE::Actor* a_b)
	{
		if (!CanHaveChildrenAlone(a_a) || !CanHaveChildrenAlone(a_b)) {
			return false;
		}
		// someone the player set to "always" overrides the race rule too
		auto* ba = a_a->GetActorBase();
		auto* bb = a_b->GetActorBase();
		if (ChildrenChoice(ba->GetFormID()) == 1 || ChildrenChoice(bb->GetFormID()) == 1) {
			return true;
		}
		if (!Settings::Get().beastChildrenOwnKind) {
			return true;
		}
		return Kind(a_a->GetRace()) == Kind(a_b->GetRace());
	}


	// a person, by race keywords - the same rule the simulation pool uses: an NPC race (ActorTypeNPC) that is not a creature,
	// animal, dragon, daedra, ghost or undead (vampire races carry Undead on purpose and count). Custom races (UBE races, Khajiit
	// Diversity's Ohmes-Raht, ...) count when they are NPC races, whether or not they are flagged playable - many are not.
	bool IsPersonRace(const RE::TESRace* a_race)
	{
		if (!a_race || !a_race->HasKeywordID(0x00013794)) {
			return false;
		}
		const bool vampire = a_race->HasKeywordID(0x000A82BB);
		for (const RE::FormID kw : { 0x00013795u, 0x00013796u, 0x00013797u, 0x00013798u, 0x00035D59u, 0x000D205Eu }) {
			if (a_race->HasKeywordID(kw) && !(vampire && kw == 0x00013796u)) {
				return false;
			}
		}
		return true;
	}

	bool IsHumanoid(RE::Actor* a_actor)
	{
		return a_actor && !a_actor->IsChild() && IsPersonRace(a_actor->GetRace());
	}

	bool FollowerAllowed(RE::FormID a_base)
	{
		std::scoped_lock flock(followerGuard);
		return std::find(allowedFollowers.begin(), allowedFollowers.end(), a_base) != allowedFollowers.end();
	}

	bool IsReservedFollower(RE::Actor* a_actor)
	{
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		return base && IsCustomFollower(a_actor) && !FollowerAllowed(base->GetFormID());
	}

	void AllowFollower(RE::FormID a_base, bool a_allow)
	{
		{
			std::scoped_lock flock(followerGuard);
			auto it = std::find(allowedFollowers.begin(), allowedFollowers.end(), a_base);
			if (a_allow && it == allowedFollowers.end()) {
				allowedFollowers.push_back(a_base);
			} else if (!a_allow && it != allowedFollowers.end()) {
				allowedFollowers.erase(it);
			}
		}
		Chronicle(std::format("{} {} the ladder", NpcName(a_base), a_allow ? "may now join" : "was taken out of"));
	}

	std::vector<RE::FormID> AllowedFollowers()
	{
		std::scoped_lock flock(followerGuard);
		return allowedFollowers;
	}

	// ---- NPCs the player has met (OnlyMetCouples) ----
	bool PlayerMet(RE::FormID a_base)
	{
		std::scoped_lock flock(followerGuard);
		return playerMet.contains(a_base);
	}

	void NoteMet(RE::FormID a_base)
	{
		std::scoped_lock flock(followerGuard);
		playerMet.insert(a_base);
	}

	std::size_t MetCount()
	{
		std::scoped_lock flock(followerGuard);
		return playerMet.size();
	}

	// the game's own "has talked to the player" flag (condition function GetTalkedToPC)
	bool TalkedToPlayer(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		RE::TESConditionItem cond;
		cond.next = nullptr;
		cond.data.comparisonValue.f = 1.0f;
		cond.data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kEqualTo;
		cond.data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetTalkedToPC;
		cond.data.object = RE::CONDITIONITEMOBJECT::kSelf;
		RE::ConditionCheckParams params(a_actor, nullptr);
		return cond(params);
	}

	// ---- wedding register: upcoming weddings with their Skyrim date (menu News page + the register in the Temple of Mara) ----
	std::string GameDate(int a_daysFromToday)
	{
		static constexpr std::array<int, 12> kLen{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		static constexpr std::array<const char*, 12> kMonth{ "Morning Star", "Sun's Dawn", "First Seed", "Rain's Hand", "Second Seed",
			"Midyear", "Sun's Height", "Last Seed", "Hearthfire", "Frostfall", "Sun's Dusk", "Evening Star" };
		static constexpr std::array<const char*, 7> kWeekday{ "Sundas", "Morndas", "Tirdas", "Middas", "Turdas", "Fredas", "Loredas" };
		auto* cal = RE::Calendar::GetSingleton();
		if (!cal) {
			return std::format("in {} days", a_daysFromToday);
		}
		int day = static_cast<int>(cal->GetDay());  // 1-based day of the month
		int month = static_cast<int>(cal->GetMonth()) % 12;
		int weekday = static_cast<int>(cal->GetDayOfWeek()) % 7;
		for (int i = 0; i < a_daysFromToday; ++i) {
			weekday = (weekday + 1) % 7;
			if (++day > kLen[month]) {
				day = 1;
				month = (month + 1) % 12;
			}
		}
		const char* suffix = (day % 10 == 1 && day != 11) ? "st" : (day % 10 == 2 && day != 12) ? "nd" : (day % 10 == 3 && day != 13) ? "rd" : "th";
		return std::format("{}, {}{} of {}", kWeekday[weekday], day, suffix, kMonth[month]);
	}

	std::vector<std::string> WeddingSchedule()
	{
		struct Item
		{
			float       day;
			std::string text;
		};
		std::vector<Item> items;
		const float       now = DaysNow();
		{
			std::scoped_lock lock(guard);
			for (const auto& p : pairs) {
				if (p.stage != Stage::Engaged || p.weddingDay <= p.stageDay) {
					continue;
				}
				const int in = static_cast<int>(std::floor(p.weddingDay) - std::floor(now));
				const std::string when = in <= 0 ? std::string("today") : in == 1 ? std::string("tomorrow") : std::format("in {} days", in);
				items.push_back({ p.weddingDay, std::format("{} and {} - {}, in the afternoon ({})", NpcName(p.a), NpcName(p.b), GameDate(std::max(0, in)), when) });
			}
		}
		std::sort(items.begin(), items.end(), [](const Item& x, const Item& y) { return x.day < y.day; });
		std::vector<std::string> out;
		for (auto& i : items) {
			out.push_back(std::move(i.text));
		}
		return out;
	}

#include "Ladder_Import.inc"
#include "Ladder_Couples.inc"
}
