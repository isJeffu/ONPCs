#include "Simulation.h"

#include "Amulet.h"
#include "Liita.h"
#include "Ladder.h"
#include "Settings.h"

#include <numeric>
#include <fstream>
#include <nlohmann/json.hpp>

namespace ONPCR::Simulation
{
	namespace
	{
		constexpr RE::FormID kLocTypeHold = 0x00016771;
		constexpr RE::FormID kPlayerMarriedFaction = 0x000C6472;
		constexpr RE::FormID kPlayerBase = 0x00000007;
		constexpr RE::FormID kMarasTrackedFaction = 0x7;  // in TT_MARAS.esp: rank 1 = engaged, 2 = married to the player
		constexpr RE::FormID kMarasOpenRelationships = 0xC18;  // TTM_SpouseOpenRelationships
		constexpr RE::FormID kActorTypeNPC = 0x00013794;
		constexpr RE::FormID kVampireKeyword = 0x000A82BB;
		// races that are never people, even when the base record is flagged unique
		constexpr std::array<RE::FormID, 6> kNotPeople{ 0x00013795, 0x00013796, 0x00013797, 0x00013798, 0x00035D59, 0x000D205E };  // Creature, Undead, Daedra, Animal, Dragon, Ghost

		// name-based exclusions (OStimNPCsReforged_PoolExclusions.json): generic-named uniques such as "Refugee" or "The Pit Keeper"
		std::vector<std::string> exclNames;
		std::vector<std::string> exclStarts;
		std::vector<std::string> exclContains;

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		bool IsExcludedName(const char* a_name)
		{
			if (!a_name || !*a_name) {
				return true;
			}
			const std::string n = Lower(a_name);
			for (const auto& x : exclNames) {
				if (n == x) {
					return true;
				}
			}
			for (const auto& x : exclStarts) {
				if (n.rfind(x, 0) == 0) {
					return true;
				}
			}
			for (const auto& x : exclContains) {
				if (n.find(x) != std::string::npos) {
					return true;
				}
			}
			return false;
		}

		bool IsPerson(const RE::TESRace* a_race)
		{
			if (!a_race || !a_race->HasKeywordID(kActorTypeNPC)) {
				return false;
			}
			const bool vampire = a_race->HasKeywordID(kVampireKeyword);
			for (const auto kw : kNotPeople) {
				if (a_race->HasKeywordID(kw) && !(vampire && kw == 0x00013796)) {
					return false;  // vampire races carry Undead on purpose and stay in
				}
			}
			return true;
		}

		struct Npc
		{
			RE::FormID              base{ 0 };
			RE::FormID              ref{ 0 };
			bool                    female{ false };
			RE::FormID              loc{ 0 };
			RE::FormID              hold{ 0 };
			bool                    teammate{ false };
			bool                    excluded{ false };  // the player's partner (vanilla or M.A.R.A.S), or married through Love Is In The Air
			bool                    looking{ false };   // wears an Amulet of Mara
			std::vector<RE::FormID> family;
		};

		std::mutex        poolGuard;
		std::vector<Npc>  pool;
		std::int32_t      poolBuiltDay{ -1 };
		std::size_t       lastPoolSize{ 0 };

		// a home location nobody really lives in: dungeons, caves, camps, lairs, crypts, jails (the location or one it belongs to)
		bool IsNoHomeLocation(RE::BGSLocation* a_loc)
		{
			static constexpr const char* kTypes[] = { "LocTypeDungeon", "LocTypeClearable", "LocTypeBanditCamp", "LocTypeDraugrCrypt",
				"LocTypeAnimalDen", "LocTypeHagravenNest", "LocTypeWarlockLair", "LocTypeVampireLair", "LocTypeForswornCamp",
				"LocTypeGiantCamp", "LocTypeFalmerHive", "LocTypeDwarvenAutomatons", "LocTypeDragonPriestLair", "LocTypeSprigganGrove",
				"LocTypeJail" };
			int depth = 0;
			for (auto* l = a_loc; l && depth < 3; l = l->parentLoc, ++depth) {
				if (l->HasKeywordString("LocTypeCity") || l->HasKeywordString("LocTypeTown") || l->HasKeywordString("LocTypeSettlement") ||
					l->HasKeywordString("LocTypeHold")) {
					return false;  // reached a real place first (a town jail is caught above, before its town)
				}
				for (const auto* t : kTypes) {
					if (l->HasKeywordString(t)) {
						return true;
					}
				}
			}
			return false;
		}

		RE::FormID HoldOf(RE::BGSLocation* a_loc)
		{
			auto* holdKw = RE::TESForm::LookupByID<RE::BGSKeyword>(kLocTypeHold);
			RE::BGSLocation* cur = a_loc;
			RE::BGSLocation* top = a_loc;
			int guard = 0;
			while (cur && guard++ < 16) {
				if (holdKw && cur->HasKeyword(holdKw)) {
					return cur->GetFormID();
				}
				top = cur;
				cur = cur->parentLoc;
			}
			return top ? top->GetFormID() : 0;
		}

		bool IsPlayersPartner(RE::Actor* a_actor, RE::TESNPC* a_base, RE::TESFaction* a_marasFaction, RE::TESFaction* a_marasOpen)
		{
			const int policy = Settings::Get().playerPartnerRomances;
			if (policy >= 2) {
				return false;  // polygamy: the player's partners stay on the ladder
			}
			auto* playerBase = RE::TESForm::LookupByID<RE::TESNPC>(kPlayerBase);
			if (playerBase) {
				auto* rel = RE::BGSRelationship::GetRelationship(a_base, playerBase);
				if (rel && rel->level.get() == RE::BGSRelationship::RELATIONSHIP_LEVEL::kLover) {
					return true;
				}
			}
			auto* married = RE::TESForm::LookupByID<RE::TESFaction>(kPlayerMarriedFaction);
			if (married && a_actor->IsInFaction(married)) {
				return true;
			}
			if (Ladder::InPlayerPartnerFaction(a_actor)) {
				return true;  // 0.21.2: a follower mod's own romance with the player (Couples json "playerPartnerFactions")
			}
			if (a_marasFaction) {
				const auto rank = a_actor->GetFactionRank(a_marasFaction, false);
				if (rank == 1 || rank == 2) {
					if (policy == 1 && rank == 2 && a_marasOpen && a_actor->IsInFaction(a_marasOpen)) {
						return false;  // an open-relationship spouse may see other NPCs
					}
					return true;
				}
			}
			return false;
		}

		void CollectFamily(RE::TESNPC* a_base, std::vector<RE::FormID>& a_out, bool a_inLaws)
		{
			if (!a_base || !a_base->relationships) {
				return;
			}
			for (auto* rel : *a_base->relationships) {
				if (!rel || !rel->assocType || !rel->npc1 || !rel->npc2) {
					continue;
				}
				RE::TESNPC* other = rel->npc1 == a_base ? rel->npc2 : rel->npc1;
				const bool family = rel->assocType->flags.all(RE::BGSAssociationType::FLAGS::kFamily);
				const bool spouse = rel->assocType->GetFormID() == 0x0142CA;
				if (family) {
					a_out.push_back(other->GetFormID());
					if (a_inLaws) {
						// the family member's spouse is an in-law
						if (other->relationships) {
							for (auto* r2 : *other->relationships) {
								if (r2 && r2->assocType && r2->assocType->GetFormID() == 0x0142CA && r2->npc1 && r2->npc2) {
									a_out.push_back(r2->npc1 == other ? r2->npc2->GetFormID() : r2->npc1->GetFormID());
								}
							}
						}
					}
				} else if (spouse && a_inLaws) {
					// the spouse's family are in-laws
					CollectFamily(other, a_out, false);
				}
			}
		}

		void BuildPool(std::int32_t a_day)
		{
			std::vector<Npc> fresh;
			auto* data = RE::TESDataHandler::GetSingleton();
			RE::TESFaction* maras = data ? data->LookupForm<RE::TESFaction>(kMarasTrackedFaction, "TT_MARAS.esp") : nullptr;
			RE::TESFaction* marasOpen = data ? data->LookupForm<RE::TESFaction>(kMarasOpenRelationships, "TT_MARAS.esp") : nullptr;
			const float now = Ladder::DaysNow();
			{
				const auto& [map, lock] = RE::TESForm::GetAllForms();
				const RE::BSReadLockGuard l{ lock };
				if (map) {
					for (auto& entry : *map) {
						auto* form = entry.second;
						if (!form || form->GetFormType() != RE::FormType::ActorCharacter) {
							continue;
						}
						auto* actor = form->As<RE::Actor>();
						if (!actor || actor->IsPlayerRef() || actor->IsDeleted() || actor->IsDisabled()) {
							continue;  // disabled = not in the world (yet): quest-gated people like Titus Mede II on the Katariah
						}
						auto* base = actor->GetActorBase();
						if (!base || !base->IsUnique() || actor->IsChild() || !base->GetName() || !*base->GetName()) {
							continue;
						}
						auto* race = actor->GetRace();
						if (!IsPerson(race) || actor->IsGhost()) {
							continue;  // people only: no dragons, draugr, atronachs, ghosts, voices of the dead
						}
						if (base->UsesTemplate() || base->baseTemplateForm) {
							continue;  // generic soldiers, watchmen, attendants: unique-flagged copies of a template
						}
						if (IsExcludedName(base->GetName())) {
							continue;  // generic-named unique (see OStimNPCsReforged_PoolExclusions.json)
						}
						if (Ladder::IsReservedFollower(actor)) {
							continue;  // a custom follower is the player's own unless allowed on the Couples page
						}
						if (Ladder::TalkedToPlayer(actor)) {
							Ladder::NoteMet(base->GetFormID());  // OnlyMetCouples: anyone the player has ever spoken to
						}
						if (actor->IsDead()) {
							Ladder::MarkWidowed(base->GetFormID(), now);
							continue;
						}
						auto* loc = actor->GetEditorLocation();
						if (!loc) {
							continue;
						}
						// 0.12.5 (couples audit): nobody who lives in a dungeon, cave, camp, lair or jail - captives, bosses, cultists and
						// prisoners (Arvel the Swift, Voice of Namira, Torture Victims, Cidhna Mine inmates). Mines are fine (miners).
						if (IsNoHomeLocation(loc)) {
							continue;
						}
						// ... and nobody hostile to the player (bandit bosses, warlocks, Falmer servants, ratway thugs)
						if (auto* player = RE::PlayerCharacter::GetSingleton(); player && actor->IsHostileToActor(player)) {
							continue;
						}
						Npc n;
						n.base = base->GetFormID();
						n.ref = actor->GetFormID();
						n.female = base->IsFemale();
						n.loc = loc->GetFormID();
						n.hold = HoldOf(loc);
						n.teammate = actor->IsPlayerTeammate();
						n.excluded = IsPlayersPartner(actor, base, maras, marasOpen) || Liita::IsMarried(actor);  // 0.13.3: or married through Love Is In The Air
						n.looking = Amulet::IsLooking(actor) || Amulet::GaveAmulet(actor->GetFormID());
						CollectFamily(base, n.family, true);
						fresh.push_back(std::move(n));
					}
				}
			}
			{
				std::scoped_lock lock(poolGuard);
				pool.swap(fresh);
				poolBuiltDay = a_day;
				lastPoolSize = pool.size();
			}
			// the ladder keeps its own base -> ref copy (no pool lock needed for lookups); set outside poolGuard
			{
				std::unordered_map<RE::FormID, RE::FormID> refs;
				for (const auto& n : pool) {
					refs.emplace(n.base, n.ref);
				}
				Ladder::SetPoolRefs(std::move(refs));
			}
			SKSE::log::info("Simulation: pool rebuilt - {} unique NPCs with a home location", pool.size());
			Ladder::DebugNotify(std::format("pool rebuilt: {} NPCs", pool.size()));
		}

		float RelationMultiplier(RE::FormID a_x, RE::FormID a_y)
		{
			auto* nx = RE::TESForm::LookupByID<RE::TESNPC>(a_x);
			auto* ny = RE::TESForm::LookupByID<RE::TESNPC>(a_y);
			if (!nx || !ny) {
				return 1.0f;
			}
			auto* rel = RE::BGSRelationship::GetRelationship(nx, ny);
			if (!rel) {
				return 1.0f;
			}
			using L = RE::BGSRelationship::RELATIONSHIP_LEVEL;
			switch (rel->level.get()) {
			case L::kLover: return 4.0f;
			case L::kAlly: return 2.5f;
			case L::kFriend: return 1.5f;
			case L::kAcquaintance: return 1.0f;
			case L::kRival:
			case L::kFoe:
			case L::kEnemy:
			case L::kArchnemesis: return 0.0f;
			default: return 2.0f;  // confidant
			}
		}

		float GenderWeight(const Npc& a_x, const Npc& a_y, const Settings::Data& a_s)
		{
			if (a_x.female != a_y.female) {
				return a_s.weightMF / 100.0f;
			}
			return (a_x.female ? a_s.weightFF : a_s.weightMM) / 100.0f;
		}

		bool InFamily(const Npc& a_x, const Npc& a_y)
		{
			return std::find(a_x.family.begin(), a_x.family.end(), a_y.base) != a_x.family.end() ||
				   std::find(a_y.family.begin(), a_y.family.end(), a_x.base) != a_y.family.end();
		}

		// social class / skill flavour from M.A.R.A.S archetypes (neutral when unknown or disabled)
		float ArchetypeMultiplier(const Npc& a_x, const Npc& a_y, const Settings::Data& a_s)
		{
			if (!a_s.useMarasArchetypes) {
				return 1.0f;
			}
			const auto ax = Ladder::GetArchetype(a_x.base);
			const auto ay = Ladder::GetArchetype(a_y.base);
			if (ax.socialClass < 0 || ay.socialClass < 0) {
				return 1.0f;
			}
			float m = 1.0f;
			const int diff = std::abs(ax.socialClass - ay.socialClass);
			if (ax.temperament == 0 && ay.socialClass < ax.socialClass) {
				return 0.0f;  // Proud: own class or higher only
			}
			if (ax.socialClass == 0 || ay.socialClass == 0) {
				m *= (ax.socialClass == ay.socialClass) ? 1.0f : 0.3f;  // outcasts keep to outcasts
			} else if (diff == 1) {
				m *= 0.7f;
			} else if (diff >= 2) {
				m *= 0.2f;
			}
			if (ax.socialClass == 5 || ay.socialClass == 5) {
				m *= 0.6f;  // Religious: lower base chance
			}
			if (ax.skillType >= 0 && ay.skillType >= 0) {
				const bool natural = (ax.skillType == 3 && ay.skillType == 4) || (ax.skillType == 4 && ay.skillType == 3) ||
									 (ax.skillType == 2 && ay.skillType == 5) || (ax.skillType == 5 && ay.skillType == 2);
				if (ax.skillType == ay.skillType || natural) {
					m *= 1.2f;
				}
			}
			return m;
		}

		float StrayTemperamentMultiplier(const Npc& a_x, const Settings::Data& a_s)
		{
			if (!a_s.useMarasArchetypes) {
				return 1.0f;
			}
			switch (Ladder::GetArchetype(a_x.base).temperament) {
			case 1:
			case 3: return 0.5f;  // Humble, Romantic
			case 4: return 2.0f;  // Independent
			default: return 1.0f;
			}
		}

		void DistributeOwnAmulets(std::int32_t a_day, std::mt19937& a_rng);

		void SimulateDay(std::int32_t a_day)
		{
			const auto s = Settings::Get();
			RE::FormID playerLoc = 0;
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				if (auto* loc = player->GetCurrentLocation()) {
					playerLoc = loc->GetFormID();
				}
			}
			std::scoped_lock lock(poolGuard);
			const std::size_t n = pool.size();
			if (n < 2) {
				return;
			}
			std::unordered_map<RE::FormID, std::vector<std::size_t>> byLoc;
			std::unordered_map<RE::FormID, std::vector<std::size_t>> byHold;
			for (std::size_t i = 0; i < n; ++i) {
				byLoc[pool[i].loc].push_back(i);
				byHold[pool[i].hold].push_back(i);
			}
			std::mt19937 rng(static_cast<std::uint32_t>(a_day) * 2654435761u ^ 0x5bd1e995u);
			std::vector<std::size_t> order(n);
			std::iota(order.begin(), order.end(), 0);
			std::shuffle(order.begin(), order.end(), rng);
			std::unordered_set<RE::FormID> met;
			std::uniform_real_distribution<float> roll(0.0f, 1.0f);
			int encounters = 0;

			struct Cand
			{
				std::size_t idx;
				float       chance;
			};
			std::vector<Cand> cands;

			for (const auto i : order) {
				const Npc& x = pool[i];
				if (x.excluded || x.teammate || x.loc == playerLoc || met.count(x.base) || Ladder::IsMourning(x.base, static_cast<float>(a_day)) ||
					Ladder::IsHeartbroken(x.base, static_cast<float>(a_day))) {
					continue;
				}
				Ladder::Stage xStage = Ladder::Stage::None;
				std::uint8_t  xStrikes = 0;
				bool          xCold = false;
				const RE::FormID xPartner = Ladder::CommittedPartnerOf(x.base, &xStage, &xStrikes, &xCold);

				cands.clear();
				auto consider = [&](std::size_t j, float a_holdFactor) {
					if (j == i) {
						return;
					}
					const Npc& y = pool[j];
					if (y.excluded || y.teammate || y.loc == playerLoc || met.count(y.base) || InFamily(x, y)) {
						return;
					}
					if (s.onlyMetCouples && !(Ladder::PlayerMet(x.base) && Ladder::PlayerMet(y.base))) {
						// OnlyMetCouples: strangers to the player can still meet as an existing couple, but nothing new starts
						const auto st = Ladder::StageBetween(x.base, y.base);
						if (st == Ladder::Stage::None || st == Ladder::Stage::Jilted || st == Ladder::Stage::Divorced || st == Ladder::Stage::Widowed) {
							return;
						}
					}
					const float gw = GenderWeight(x, y, s);
					if (gw <= 0.0f) {
						return;
					}
					float chance = (s.dailyChance / 100.0f) * a_holdFactor * gw;
					if (xPartner != 0) {
						if (y.base == xPartner) {
							float stageMult = 1.0f;
							switch (xStage) {
							case Ladder::Stage::Candidate: stageMult = 2.0f; break;
							case Ladder::Stage::Engaged: stageMult = 3.0f; break;
							case Ladder::Stage::Married: stageMult = xCold ? 0.5f : 4.0f; break;
							default: break;
							}
							chance *= stageMult * RelationMultiplier(x.base, y.base);
						} else if (!s.infidelity) {
							return;  // infidelity off: committed NPCs only meet their own partner
						} else {
							// committed elsewhere: only the stray chance applies
							chance = (s.strayChance / 100.0f) * (xStrikes > 0 ? s.strayRepeatMult : 1.0f) * a_holdFactor * gw * StrayTemperamentMultiplier(x, s);
							if (xCold) {
								chance *= 2.0f;
							}
						}
					} else {
						chance *= RelationMultiplier(x.base, y.base) * ArchetypeMultiplier(x, y, s);
						const auto enc = Ladder::EncountersBetween(x.base, y.base);
						chance *= 1.0f + 0.1f * static_cast<float>(std::min<std::uint32_t>(enc, 10));
					}
					// y committed to someone else: y would be straying
					const RE::FormID yPartner = Ladder::CommittedPartnerOf(y.base);
					if (yPartner != 0 && yPartner != x.base) {
						if (!s.infidelity) {
							return;
						}
						if (x.looking) {
							return;  // someone looking for marriage has no flings with the committed
						}
						chance *= s.strayChance / 100.0f;
					}
					if ((x.looking || y.looking) && xPartner == 0 && yPartner == 0) {
						chance *= s.lookingMultiplier;
					}
					if (chance <= 0.0f) {
						return;
					}
					cands.push_back({ j, std::min(chance, 0.95f) });
				};
				for (const auto j : byLoc[x.loc]) {
					consider(j, 1.0f);
				}
				if (s.sameHoldFactor > 0.0f) {
					for (const auto j : byHold[x.hold]) {
						if (pool[j].loc != x.loc) {
							consider(j, s.sameHoldFactor);
						}
					}
				}
				if (cands.empty()) {
					continue;
				}
				std::sort(cands.begin(), cands.end(), [](const Cand& l, const Cand& r) { return l.chance > r.chance; });
				const std::size_t limit = std::min<std::size_t>(cands.size(), static_cast<std::size_t>(std::max(1, s.shortlist)));
				for (std::size_t c = 0; c < limit; ++c) {
					if (roll(rng) >= cands[c].chance) {
						continue;
					}
					const Npc& y = pool[cands[c].idx];
					const bool mayPair = Ladder::CommunityMayPair(x.loc, a_day);
					const bool eager = x.looking || y.looking;
					if (Ladder::RecordEncounterByBase(x.base, y.base, true, Ladder::VanillaRelation::None, mayPair ? x.loc : 0, eager)) {
						Ladder::DebugNotify(std::format("day {}: {} + {}", a_day, Ladder::NpcName(x.base), Ladder::NpcName(y.base)));
						if (s.writeLoversLedger) {
							Ladder::QueueLedgerWrite(x.ref, y.ref, a_day);
						}
						met.insert(x.base);
						met.insert(y.base);
						++encounters;
					}
					break;
				}
			}
			if (encounters > 0) {
				SKSE::log::info("Simulation: day {} - {} off-screen encounter(s)", a_day, encounters);
				Ladder::DebugNotify(std::format("day {}: {} off-screen encounter(s)", a_day, encounters));
			}
			DistributeOwnAmulets(a_day, rng);
		}

		// no amulet-giver mod: a few marriage-minded singles put an Amulet of Mara on by themselves
		void DistributeOwnAmulets(std::int32_t a_day, std::mt19937& a_rng)
		{
			const auto s = Settings::Get();
			if (!s.amuletEnabled || !s.ownAmulets || Amulet::GiverModPresent() || s.ownAmuletShare <= 0.0f) {
				return;
			}
			// poolGuard held by the caller
			std::size_t singles = 0, looking = 0;
			for (const auto& n : pool) {
				if (n.excluded || n.teammate) {
					continue;
				}
				if (Ladder::CommittedPartnerOf(n.base) == 0) {
					++singles;
					if (n.looking) {
						++looking;
					}
				}
			}
			if (singles == 0 || static_cast<float>(looking) >= static_cast<float>(singles) * s.ownAmuletShare / 100.0f) {
				return;
			}
			std::uniform_real_distribution<float> roll(0.0f, 1.0f);
			const float dailyChance = (s.ownAmuletShare / 100.0f) / 60.0f;  // reaches the share over roughly two months
			int handed = 0;
			for (auto& n : pool) {
				if (n.excluded || n.teammate || n.looking || Ladder::CommittedPartnerOf(n.base) != 0 || Ladder::IsMourning(n.base, static_cast<float>(a_day))) {
					continue;
				}
				float chance = dailyChance;
				if (s.useMarasArchetypes) {
					switch (Ladder::GetArchetype(n.base).temperament) {
					case 3: chance *= 2.0f; break;  // Romantic
					case 2: chance *= 1.5f; break;  // Jealous
					case 4: chance *= 0.2f; break;  // Independent
					default: break;
					}
				}
				if (roll(a_rng) >= chance) {
					continue;
				}
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(n.ref);
				if (!actor || !Amulet::CanWearOwnAmulet(actor)) {
					continue;
				}
				Amulet::GiveAmulet(n.ref);
				n.looking = true;
				if (++handed >= 3) {
					break;  // a few a day at most
				}
			}
			if (handed) {
				SKSE::log::info("Simulation: day {} - {} NPC(s) put on an Amulet of Mara", a_day, handed);
				Ladder::DebugNotify(std::format("day {}: {} NPC(s) put on an Amulet of Mara", a_day, handed));
			}
		}

		bool DriftPaused(RE::FormID a_base)
		{
			// poolGuard is held by the caller
			for (const auto& n : pool) {
				if (n.base == a_base) {
					return n.teammate;
				}
			}
			return false;
		}
	}

	std::size_t PoolSize()
	{
		return lastPoolSize;
	}

	void LoadExclusions()
	{
		std::error_code ec;
		const auto path = (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OStimNPCsReforged_PoolExclusions.json").string();
		nlohmann::json j = nlohmann::json::object();
		std::ifstream in(path);
		if (in) {
			try {
				in >> j;
			} catch (const std::exception& e) {
				SKSE::log::error("Simulation: {} is not valid JSON ({}) - no name exclusions", path, e.what());
				j = nlohmann::json::object();
			}
		} else {
			SKSE::log::warn("Simulation: {} missing - no name exclusions", path);
		}
		std::scoped_lock lock(poolGuard);
		exclNames.clear();
		exclStarts.clear();
		exclContains.clear();
		for (const auto& s : j.value("names", nlohmann::json::array())) {
			if (s.is_string()) {
				exclNames.push_back(Lower(s.get<std::string>()));
			}
		}
		for (const auto& s : j.value("startsWith", nlohmann::json::array())) {
			if (s.is_string()) {
				exclStarts.push_back(Lower(s.get<std::string>()));
			}
		}
		for (const auto& s : j.value("contains", nlohmann::json::array())) {
			if (s.is_string()) {
				exclContains.push_back(Lower(s.get<std::string>()));
			}
		}
		SKSE::log::info("Simulation: name exclusions loaded - {} names, {} prefixes, {} fragments", exclNames.size(), exclStarts.size(), exclContains.size());
	}

	bool IsSceneExcluded(RE::Actor* a_actor)
	{
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		if (!base) {
			return false;
		}
		if (a_actor->IsGuard()) {
			return true;
		}
		std::scoped_lock lock(poolGuard);
		return IsExcludedName(base->GetName());
	}

	void RequestRebuild()
	{
		poolBuiltDay = -1;
	}

	void EnsurePool()
	{
		if (poolBuiltDay < 0) {
			BuildPool(static_cast<std::int32_t>(Ladder::DaysNow()));
		}
	}

	std::vector<RE::FormID> ArchetypeRequests(std::size_t a_max)
	{
		std::vector<RE::FormID> out;
		std::scoped_lock lock(poolGuard);
		for (const auto& n : pool) {
			if (out.size() >= a_max) {
				break;
			}
			if (!Ladder::ArchetypeKnownOrRequested(n.base)) {
				Ladder::MarkArchetypeRequested(n.base);
				out.push_back(n.ref);
			}
		}
		return out;
	}

	std::int32_t LastSimulatedDay()
	{
		return Ladder::LastSimDay();
	}

	void Tick()
	{
		const auto s = Settings::Get();
		if (!s.ladderEnabled) {
			return;
		}
		// 0.17.0: the first scan after the update (or on a new game) takes the game's own couples in, once per save
		if (!Ladder::GameImportDone()) {
			Ladder::ImportGameCouples(true);
		}
		Ladder::ResolvePendingImports();  // 0.19.1: game couples who were not in memory at the import, once both are seen
		// 0.19.0: an older save gets the couple table once (stages, quest locks, affairs ...); quests that move a locked couple are watched
		if (Ladder::CoupleTableDue()) {
			Ladder::ApplyCoupleTable();
		}
		Ladder::ApplyProtectedCouples();  // 0.21.0
		Ladder::WatchQuests();
		Ladder::WatchRelationships();  // 0.20.0
		if (!s.simulationEnabled) {
			Ladder::Tick();
			return;
		}
		const std::int32_t today = static_cast<std::int32_t>(Ladder::DaysNow());
		std::int32_t last = Ladder::LastSimDay();
		if (last <= 0 && today <= 0) {
			// a new game is still on day 0, and 0 also means "not started yet" - wait for day 1 instead of starting over
			// every scan (it used to log the first-run line every 10 seconds all through day 0)
			return;
		}
		if (last <= 0) {
			// first run on this save: nothing is simulated until the next game day starts
			Ladder::SetLastSimDay(today);
			SKSE::log::info("Simulation: first run on this save - starts tomorrow (day {})", today + 1);
			return;
		}
		if (today <= last) {
			return;
		}
		const std::int32_t cap = std::max(1, s.catchUpCapDays);
		const std::int32_t missed = today - last;
		const std::int32_t days = std::min(missed, cap);
		BuildPool(today);
		auto paused = [](RE::FormID a_base) { return DriftPaused(a_base); };
		for (std::int32_t d = last + 1; d <= last + days; ++d) {
			SimulateDay(d);
			{
				std::scoped_lock lock(poolGuard);
				Ladder::AdvanceDay(static_cast<float>(d), paused);
			}
		}
		Ladder::SetLastSimDay(today);  // if more than the cap was missed, the rest is skipped
		if (missed > cap) {
			SKSE::log::info("Simulation: {} day(s) missed, simulated {} (cap)", missed, days);
			Ladder::DebugNotify(std::format("{} day(s) missed, simulated {} (cap)", missed, days));
		}
	}

	// testing button: simulate one more day right now, even if today already ran, and say what changed
	std::string RunDayNow()
	{
		const auto s = Settings::Get();
		if (!s.ladderEnabled) {
			return "Nothing ran - couples are switched off.";
		}
		const auto old = Ladder::ChronicleSnapshot();
		const auto pairsBefore = Ladder::Snapshot().size();
		const std::int32_t today = static_cast<std::int32_t>(Ladder::DaysNow());
		if (s.simulationEnabled) {
			BuildPool(today);
			SimulateDay(today);
			auto paused = [](RE::FormID a_base) { return DriftPaused(a_base); };
			std::scoped_lock lock(poolGuard);
			Ladder::AdvanceDay(static_cast<float>(today), paused);
		}
		Ladder::Tick();
		const auto fresh = Ladder::ChronicleSnapshot();
		const auto pairsAfter = Ladder::Snapshot().size();
		// new items = those above the old newest one (the list keeps only the last 50, so sizes alone can lie)
		std::size_t news = fresh.size();
		if (!old.empty()) {
			for (std::size_t i = 0; i < fresh.size(); ++i) {
				if (fresh[i].text == old.front().text && fresh[i].day == old.front().day) {
					news = i;
					break;
				}
			}
		}
		return std::format("One extra day simulated{}: {} new news item(s), {} pair(s) now ({:+}). See the News and weddings page.",
			s.simulationEnabled ? "" : " (ladder only - off-screen world is off)", news, pairsAfter,
			static_cast<long long>(pairsAfter) - static_cast<long long>(pairsBefore));
	}

	std::vector<Person> FindPeople(const std::string& a_needle, std::size_t a_max)
	{
		std::vector<Person> out;
		const std::string needle = Lower(a_needle);
		if (needle.size() < 2) {
			return out;
		}
		std::scoped_lock lock(poolGuard);
		for (const auto& n : pool) {
			auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(n.base);
			const char* name = npc ? npc->GetName() : nullptr;
			if (name && Lower(name).find(needle) != std::string::npos) {
				out.push_back({ n.base, n.ref, name });
				if (out.size() >= a_max) {
					break;
				}
			}
		}
		return out;
	}
}
