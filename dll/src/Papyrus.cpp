#include "Papyrus.h"

#include "Amulet.h"
#include "Babies.h"
#include "Liita.h"
#include "Ladder.h"
#include "Conversation.h"
#include "Settings.h"
#include "Simulation.h"

namespace ONPCR::Papyrus
{
	namespace
	{
		constexpr auto kScript = "ONPCR_Settings";
		constexpr auto kLadderScript = "ONPCR_Ladder";
		constexpr auto kEvent = "ONPCR_SettingsChanged";
		constexpr std::int32_t kBridgeVersion = 23;  // 22 = FindGroup, WalkTo, GroupSceneStarted / GroupSceneEnded (0.26.0 phase 3)  // 21  // 21 = ONPCR_Base.SceneStarted + SceneEnded(climaxes, thread, cause), arousal states (0.26.0)  //  // 20 = ONPCR_Base.IsSceneRunning (0.26.0 phase 2)  // 19 = ONPCR_Base.FindPairs / SceneEnded (0.26.0 base port phase 1)  // 18 = ChildAwayReasonId, ChildAwayPlaceById, ChildAwayDialogueRank (0.25.1)  // 17 = PickChildAwayReasonFor (0.25.0 little one dialogue)  // 16 = baby stages (ChildAwayReasonCount, PickChildAwayReason, ChildAwayPlace, ClearBabyNotes, AddBabyNote)  // 15 = PlanFamily, GetFamilyPairs (0.23.0 family chats)  // 14 = IsSceneExcluded  // 13 = households (GetHouseholds, GetHomeFaction, FindFreeBed, FindTownSpot, AddNewsLine)  // 12 = GetSleepOverPairs  // 11 = GetAltarRunner, SetWeddingInvite, CallOffWedding  // 10 = OtherWeddingBusy  // 9 = CanSay  // 8 = SyncStageFaction  // 7 = voice natives (GetVoicePairs, GetPartnerRef)  // 3 = ONPCR_Ladder natives, 4 = simulation + ledger queue, 5 = archetypes + news

		float GetFloat(RE::StaticFunctionTag*, RE::BSFixedString a_key, float a_default)
		{
			return Settings::GetFloat(a_key.c_str() ? a_key.c_str() : "", a_default);
		}

		bool GetBool(RE::StaticFunctionTag*, RE::BSFixedString a_key, bool a_default)
		{
			return Settings::GetBool(a_key.c_str() ? a_key.c_str() : "", a_default);
		}

		std::int32_t GetInt(RE::StaticFunctionTag*, RE::BSFixedString a_key, std::int32_t a_default)
		{
			return Settings::GetInt(a_key.c_str() ? a_key.c_str() : "", a_default);
		}

		void Reload(RE::StaticFunctionTag*)
		{
			Settings::Load();
		}

		std::int32_t GetVersion(RE::StaticFunctionTag*)
		{
			return kBridgeVersion;
		}

		// ---- ONPCR_Ladder ----
		void L_RecordEncounter(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y, bool a_offscreen, std::int32_t a_vanilla)
		{
			Ladder::RecordEncounter(a_x, a_y, a_offscreen, static_cast<Ladder::VanillaRelation>(std::clamp(a_vanilla, 0, 2)));
		}

		void L_AllowPlayerChosen(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y)
		{
			Ladder::AllowPlayerChosen(a_x, a_y);
		}

		std::int32_t L_GetStage(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y)
		{
			return static_cast<std::int32_t>(Ladder::GetStage(a_x, a_y));
		}

		RE::BSFixedString L_GetStageName(RE::StaticFunctionTag*, std::int32_t a_stage)
		{
			return RE::BSFixedString(Ladder::StageName(static_cast<Ladder::Stage>(std::clamp(a_stage, 0, 10))));  // 0.22.4: 8-10 = affair, crush, thrall
		}

		bool L_IsCommittedTo(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y)
		{
			return Ladder::IsCommittedTo(a_x, a_y);
		}

		bool L_IsCommittedToOther(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y)
		{
			return Ladder::IsCommittedToOther(a_x, a_y);
		}

		std::int32_t L_GetPartnerStage(RE::StaticFunctionTag*, RE::Actor* a_x)
		{
			return static_cast<std::int32_t>(Ladder::GetPartnerStage(a_x));
		}

		RE::TESNPC* L_GetPartnerBase(RE::StaticFunctionTag*, RE::Actor* a_x)
		{
			return Ladder::GetPartnerBase(a_x);
		}

		bool L_IsMourning(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			return a_actor && a_actor->GetActorBase() && Ladder::IsMourning(a_actor->GetActorBase()->GetFormID(), Ladder::DaysNow());
		}

		bool L_InPlayerPartnerFaction(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			return Ladder::InPlayerPartnerFaction(a_actor);
		}

		void L_SetPregnant(RE::StaticFunctionTag*, RE::Actor* a_woman, bool a_pregnant)
		{
			if (a_woman && a_woman->GetActorBase()) {
				Ladder::SetPregnant(a_woman->GetActorBase()->GetFormID(), a_pregnant);
			}
		}

		// the pregnancy mod's verdict per person (Followers page): Papyrus reads it, these carry it in and out
		std::vector<RE::Actor*> L_GetFertilityCheckRefs(RE::StaticFunctionTag*);
		void L_SetEngineChildren(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_verdict)
		{
			if (auto* base = a_actor ? a_actor->GetActorBase() : nullptr) {
				Ladder::SetEngineChildren(base->GetFormID(), a_verdict);
			}
		}

		// may these two have a child together (people who cannot, Khajiit and Argonians only with their own kind - see Ladder)
		bool L_CanHaveChildren(RE::StaticFunctionTag*, RE::Actor* a_a, RE::Actor* a_b)
		{
			return a_a && a_b && Ladder::CanHaveChildren(a_a, a_b);
		}

		bool L_IsPregnant(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			return a_actor && a_actor->GetActorBase() && Ladder::IsPregnant(a_actor->GetActorBase()->GetFormID());
		}

		void L_AddChild(RE::StaticFunctionTag*, RE::Actor* a_mother, RE::Actor* a_father)
		{
			if (a_mother && a_mother->GetActorBase()) {
				Ladder::AddChild(a_mother->GetActorBase()->GetFormID(), (a_father && a_father->GetActorBase()) ? a_father->GetActorBase()->GetFormID() : 0);
			}
		}

		std::vector<std::int32_t> L_GetCommittedPairRefs(RE::StaticFunctionTag*)
		{
			std::vector<std::int32_t> out;
			Simulation::EnsurePool();
			for (const auto& [a, b] : Ladder::CommittedPairRefs()) {
				out.push_back(static_cast<std::int32_t>(a));
				out.push_back(static_cast<std::int32_t>(b));
			}
			return out;
		}

		// slice 5 voice: couples (dating / engaged / married / jilted / divorced) whose BOTH partners are loaded within
		// a_maxDist of the player - refA, refB, stage triples. The 3D / distance reads are plain field reads.
		std::vector<std::int32_t> L_GetVoicePairs(RE::StaticFunctionTag*, float a_maxDist)
		{
			std::vector<std::int32_t> out;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return out;
			}
			const auto  ppos = player->GetPosition();
			const float max2 = a_maxDist * a_maxDist;
			auto isNear = [&](RE::FormID a_ref) -> bool {
				auto* actor = a_ref ? RE::TESForm::LookupByID<RE::Actor>(a_ref) : nullptr;
				if (!actor || !actor->Is3DLoaded() || actor->IsDead() || actor->GetParentCell() != player->GetParentCell()) {
					return false;
				}
				if (Ladder::IsReservedFollower(actor)) {
					return false;  // a custom follower the player has not let in stays quiet, even in an older couple
				}
				const auto d = actor->GetPosition() - ppos;
				return d.x * d.x + d.y * d.y + d.z * d.z <= max2;
			};
			for (const auto& p : Ladder::Snapshot()) {
				switch (p.stage) {
				case Ladder::Stage::Candidate:
				case Ladder::Stage::Engaged:
				case Ladder::Stage::Married:
				case Ladder::Stage::Jilted:
				case Ladder::Stage::Divorced:
					break;
				default:
					continue;
				}
				const RE::FormID ra = Ladder::RefOfBase(p.a);
				const RE::FormID rb = Ladder::RefOfBase(p.b);
				if (isNear(ra) && isNear(rb)) {
					out.push_back(static_cast<std::int32_t>(ra));
					out.push_back(static_cast<std::int32_t>(rb));
					out.push_back(static_cast<std::int32_t>(p.stage));
				}
			}
			return out;
		}

		// the committed partner (dating / engaged / married) as an actor in the world, or None
		RE::Actor* L_GetPartnerRef(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			auto* partner = Ladder::GetPartnerBase(a_actor);
			if (!partner) {
				return nullptr;
			}
			const RE::FormID ref = Ladder::RefOfBase(partner->GetFormID());
			return ref ? RE::TESForm::LookupByID<RE::Actor>(ref) : nullptr;
		}

		// true while the actor is saying a line - the vanilla IsTalking condition (the one dialogue itself uses). 0.8.5 read
		// !QSpeakingDone(), which was true even for idle NPCs, so every wait ran its full 10 s (user test 0.8.8 log).
		bool L_IsSpeaking(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			if (!a_actor || !a_actor->Is3DLoaded()) {
				return false;
			}
			RE::TESConditionItem cond;
			cond.next = nullptr;
			cond.data.comparisonValue.f = 1.0f;
			cond.data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kEqualTo;
			cond.data.functionData.function = RE::FUNCTION_DATA::FunctionID::kIsTalking;
			cond.data.object = RE::CONDITIONITEMOBJECT::kSelf;
			RE::ConditionCheckParams params(a_actor, nullptr);
			return cond(params);
		}

		// free for a chat: not walking anywhere, not asleep or getting into bed, weapon away, not in combat / mounted / kill move,
		// not talking to the player. Furniture is fine (seated at a table, leaning, at a work station) - 0.8.7, user's call
		bool L_IsIdle(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			if (!a_actor || !a_actor->Is3DLoaded() || a_actor->IsDead() || a_actor->IsInCombat() || a_actor->IsOnMount() || a_actor->IsInKillMove()) {
				return false;
			}
			if (a_actor->IsMoving()) {
				// 0.12.1: pottering about counts as idle - innkeepers and shopkeepers are almost never standing still. Moving
				// only rules a chat out when the routine is taking them somewhere (travel, follow, escort, patrol, ...).
				auto*      pack = a_actor->GetCurrentPackage();
				const auto proc = pack ? pack->procedureType : RE::PACKAGE_PROCEDURE_TYPE::kNone;
				const bool pottering = proc == RE::PACKAGE_PROCEDURE_TYPE::kSandbox || proc == RE::PACKAGE_PROCEDURE_TYPE::kExploreWander ||
									   proc == RE::PACKAGE_PROCEDURE_TYPE::kEat || proc == RE::PACKAGE_PROCEDURE_TYPE::kUseItemAt ||
									   proc == RE::PACKAGE_PROCEDURE_TYPE::kDoNothing;
				if (!pottering || a_actor->AsActorState()->IsSprinting() || a_actor->AsActorState()->IsRunning()) {
					return false;
				}
			}
			if (auto* state = a_actor->AsActorState()) {
				const auto s = state->GetSitSleepState();
				const bool okSeat = s == RE::SIT_SLEEP_STATE::kNormal || s == RE::SIT_SLEEP_STATE::kIsSitting;
				if (!okSeat || state->IsWeaponDrawn()) {
					return false;
				}
			}
			if (auto* proc = a_actor->GetActorRuntimeData().currentProcess; proc && proc->high && proc->high->talkingToPC) {
				return false;
			}
			return true;
		}

		// seated or using any furniture - such a partner does not get up to walk over
		bool L_IsSeated(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			auto* state = a_actor->AsActorState();
			return (state && state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) || a_actor->GetOccupiedFurniture();
		}

		// sleep-overs: married couples with a shared home whose actors are known - refA, refB, home location id triples
		std::vector<std::int32_t> L_GetMarriedHomes(RE::StaticFunctionTag*)
		{
			std::vector<std::int32_t> out;
			Simulation::EnsurePool();
			for (const auto& p : Ladder::Snapshot()) {
				if (p.stage != Ladder::Stage::Married || !p.home) {
					continue;
				}
				const RE::FormID ra = Ladder::RefOfBase(p.a);
				const RE::FormID rb = Ladder::RefOfBase(p.b);
				if (ra && rb) {
					out.push_back(static_cast<std::int32_t>(ra));
					out.push_back(static_cast<std::int32_t>(rb));
					out.push_back(static_cast<std::int32_t>(p.home));
				}
			}
			return out;
		}

		// 0.14.2 performance: sleep-over candidates only - married couples who do NOT live together (one partner's editor location is the
		// shared home, the other's is not): visitorRef, hostRef, home triples. Couples living together never need a sleep-over, so
		// Papyrus no longer walks every married pair every 10 s.
		std::vector<std::int32_t> L_GetSleepOverPairs(RE::StaticFunctionTag*)
		{
			std::vector<std::int32_t> out;
			Simulation::EnsurePool();
			for (const auto& p : Ladder::Snapshot()) {
				if (p.stage != Ladder::Stage::Married || !p.home) {
					continue;
				}
				auto* a = RE::TESForm::LookupByID<RE::Actor>(Ladder::RefOfBase(p.a));
				auto* b = RE::TESForm::LookupByID<RE::Actor>(Ladder::RefOfBase(p.b));
				if (!a || !b || a->IsDead() || b->IsDead()) {
					continue;
				}
				auto*       la = a->GetEditorLocation();
				auto*       lb = b->GetEditorLocation();
				const bool  aHome = la && la->GetFormID() == p.home;
				const bool  bHome = lb && lb->GetFormID() == p.home;
				if (aHome == bHome) {
					continue;  // living together (or neither at the home) - nothing to do
				}
				auto* visitor = aHome ? b : a;
				auto* host = aHome ? a : b;
				out.push_back(static_cast<std::int32_t>(visitor->GetFormID()));
				out.push_back(static_cast<std::int32_t>(host->GetFormID()));
				out.push_back(static_cast<std::int32_t>(p.home));
			}
			return out;
		}

		// OnlyMetCouples: remember everyone the player has been near (loaded, same cell, within a_maxDist) or has talked to
		std::int32_t L_NoteMetNearby(RE::StaticFunctionTag*, float a_maxDist)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!player || !lists) {
				return 0;
			}
			const auto   ppos = player->GetPosition();
			const float  max2 = a_maxDist * a_maxDist;
			std::int32_t added = 0;
			for (auto& h : lists->highActorHandles) {
				auto actor = h.get();
				auto* base = actor ? actor->GetActorBase() : nullptr;
				if (!base || actor->IsPlayerRef() || Ladder::PlayerMet(base->GetFormID())) {
					continue;
				}
				const auto d = actor->GetPosition() - ppos;
				const bool isNear = actor->GetParentCell() == player->GetParentCell() && d.x * d.x + d.y * d.y + d.z * d.z <= max2;
				if (isNear || Ladder::TalkedToPlayer(actor.get())) {
					Ladder::NoteMet(base->GetFormID());
					++added;
				}
			}
			return added;
		}

		// everyday chat: would a_speaker get a line from a_topic right now? True when any of its lines' conditions pass for the
		// speaker (voice lists, stage, ...). Lets the chat skip a step the speaker has no line for, instead of going silent.
		// 0.13.0 everyday conversations: op, role (0 = a, 1 = b), topic FormID triples (Conversation::Plan) - empty = nothing fits
		std::vector<std::int32_t> L_PlanChat(RE::StaticFunctionTag*, RE::Actor* a_a, RE::Actor* a_b)
		{
			return Conversation::Plan(a_a, a_b);
		}
		std::vector<std::int32_t> L_PlanPart(RE::StaticFunctionTag*, RE::Actor* a_a, RE::Actor* a_b, RE::BSFixedString a_id)
		{
			return Conversation::PlanPart(a_a, a_b, a_id.c_str());
		}

		// 0.23.0 family chats: one family part for two relatives (rel 1 = a is b's parent, for Beeing Female children with no record)
		std::vector<std::int32_t> L_PlanFamily(RE::StaticFunctionTag*, RE::Actor* a_a, RE::Actor* a_b, std::int32_t a_rel)
		{
			return Conversation::PlanFamily(a_a, a_b, a_rel);
		}
		// loaded relatives near the player: refA, refB, code triples (1 parent -> child, 2 siblings, 3 grandparent -> grandchild, 4 other kin)
		std::vector<std::int32_t> L_GetFamilyPairs(RE::StaticFunctionTag*, float a_maxDist)
		{
			return Conversation::FamilyPairs(a_maxDist);
		}

		bool L_CanSay(RE::StaticFunctionTag*, RE::Actor* a_speaker, RE::TESTopic* a_topic, RE::Actor* a_target)
		{
			if (!a_speaker || !a_topic || !a_topic->topicInfos) {
				return false;
			}
			for (std::uint32_t i = 0; i < a_topic->numTopicInfos; ++i) {
				auto* info = a_topic->topicInfos[i];
				if (info && info->objConditions.IsTrue(a_speaker, a_target ? a_target : a_speaker)) {
					return true;
				}
			}
			return false;
		}

		// date nights: dating (candidate) and engaged couples whose references are known - refA, refB, stage triples
		std::vector<std::int32_t> L_GetDatingPairs(RE::StaticFunctionTag*)
		{
			std::vector<std::int32_t> out;
			Simulation::EnsurePool();
			for (const auto& p : Ladder::Snapshot()) {
				if (p.stage != Ladder::Stage::Candidate && p.stage != Ladder::Stage::Engaged) {
					continue;
				}
				const RE::FormID ra = Ladder::RefOfBase(p.a);
				const RE::FormID rb = Ladder::RefOfBase(p.b);
				if (ra && rb) {
					out.push_back(static_cast<std::int32_t>(ra));
					out.push_back(static_cast<std::int32_t>(rb));
					out.push_back(static_cast<std::int32_t>(p.stage));
				}
			}
			return out;
		}

		// real weddings: engaged couples with a wedding set - refA, refB, game hour the ceremony window opens (day x 24 + 12, noon) triples
		std::vector<std::int32_t> L_GetWeddings(RE::StaticFunctionTag*)
		{
			std::vector<std::int32_t> out;
			Simulation::EnsurePool();
			for (const auto& p : Ladder::Snapshot()) {
				if (p.stage != Ladder::Stage::Engaged || p.weddingDay <= p.stageDay) {
					continue;
				}
				const RE::FormID ra = Ladder::RefOfBase(p.a);
				const RE::FormID rb = Ladder::RefOfBase(p.b);
				if (ra && rb) {
					out.push_back(static_cast<std::int32_t>(ra));
					out.push_back(static_cast<std::int32_t>(rb));
					out.push_back(static_cast<std::int32_t>(std::lround(p.weddingDay * 24.0f)));
				}
			}
			return out;
		}

		// wedding guests: actor refs, in order - every child of either partner (never capped), then close family (parents,
		// siblings, grandparents), then friends (Friend / Confidant / Ally rank), up to a_maxOthers of those two groups.
		// From the game's own relationship records. Skips the dead, the couple, the player's followers and reserved custom
		// followers, and anyone not placed in the world.
		std::vector<std::int32_t> L_GetWeddingGuests(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y, std::int32_t a_maxOthers)
		{
			std::vector<std::int32_t> out;
			auto* bx = a_x ? a_x->GetActorBase() : nullptr;
			auto* by = a_y ? a_y->GetActorBase() : nullptr;
			if (!bx || !by) {
				return out;
			}
			constexpr RE::FormID kParentChild = 0x0142C6, kSiblings = 0x0142C5, kGrandparent = 0x01E803;
			std::vector<RE::TESNPC*> kids, family, friends;
			auto addUnique = [](std::vector<RE::TESNPC*>& a_list, RE::TESNPC* a_npc) {
				if (a_npc && std::find(a_list.begin(), a_list.end(), a_npc) == a_list.end()) {
					a_list.push_back(a_npc);
				}
			};
			for (auto* partner : { bx, by }) {
				if (!partner->relationships) {
					continue;
				}
				for (auto* rel : *partner->relationships) {
					if (!rel || !rel->npc1 || !rel->npc2) {
						continue;
					}
					RE::TESNPC* other = rel->npc1 == partner ? rel->npc2 : rel->npc1;
					if (other == bx || other == by) {
						continue;
					}
					const RE::FormID assoc = rel->assocType ? rel->assocType->GetFormID() : 0;
					if (assoc == kParentChild) {
						addUnique(rel->npc1 == partner ? kids : family, other);  // npc1 is the parent
					} else if (assoc == kSiblings || assoc == kGrandparent) {
						addUnique(family, other);
					} else if (!assoc || !(rel->assocType->flags.all(RE::BGSAssociationType::FLAGS::kFamily))) {
						const auto lvl = rel->level.get();
						if (lvl == RE::BGSRelationship::RELATIONSHIP_LEVEL::kFriend || lvl == RE::BGSRelationship::RELATIONSHIP_LEVEL::kConfidant ||
							lvl == RE::BGSRelationship::RELATIONSHIP_LEVEL::kAlly) {
							addUnique(friends, other);
						}
					}
				}
			}
			// base -> placed actor: the ladder's known refs first, else one scan of the loaded forms (once per wedding)
			std::unordered_map<RE::TESNPC*, RE::Actor*> placed;
			auto findRef = [&](RE::TESNPC* a_npc) -> RE::Actor* {
				if (auto* known = RE::TESForm::LookupByID<RE::Actor>(Ladder::RefOfBase(a_npc->GetFormID()))) {
					return known;
				}
				if (placed.empty()) {
					const auto& [map, lock] = RE::TESForm::GetAllForms();
					const RE::BSReadLockGuard l{ lock };
					if (map) {
						for (auto& entry : *map) {
							auto* actor = entry.second ? entry.second->As<RE::Actor>() : nullptr;
							if (actor && !actor->IsDeleted() && actor->GetActorBase()) {
								placed.emplace(actor->GetActorBase(), actor);
							}
						}
					}
				}
				auto it = placed.find(a_npc);
				return it != placed.end() ? it->second : nullptr;
			};
			auto usable = [](RE::Actor* a_actor) {
				return a_actor && !a_actor->IsDead() && !a_actor->IsPlayerRef() && !a_actor->IsPlayerTeammate() && !Ladder::IsReservedFollower(a_actor);
			};
			for (auto* k : kids) {
				if (auto* r = findRef(k); usable(r)) {
					out.push_back(static_cast<std::int32_t>(r->GetFormID()));
				}
			}
			std::int32_t others = 0;
			for (auto* list : { &family, &friends }) {
				for (auto* n : *list) {
					if (others >= a_maxOthers) {
						break;
					}
					if (auto* r = findRef(n); usable(r) && Ladder::IsHumanoid(r)) {
						out.push_back(static_cast<std::int32_t>(r->GetFormID()));
						++others;
					}
				}
			}
			return out;
		}

		// the wedding register in the Temple of Mara: the text of its pop-up
		RE::BSFixedString L_GetWeddingRegister(RE::StaticFunctionTag*)
		{
			const auto list = Ladder::WeddingSchedule();
			std::string text = "Temple of Mara - weddings to come\n\n";
			if (list.empty()) {
				text += "No weddings are planned at present.";
			}
			for (const auto& l : list) {
				text += l + "\n";
			}
			return RE::BSFixedString(text);
		}

		// 0.17.2: the register in pages (a vanilla message box does not scroll): 8 weddings per page, page numbers from 1
		constexpr std::int32_t kRegisterPerPage = 8;

		std::int32_t L_GetWeddingRegisterPages(RE::StaticFunctionTag*)
		{
			const auto n = static_cast<std::int32_t>(Ladder::WeddingSchedule().size());
			return std::max<std::int32_t>(1, (n + kRegisterPerPage - 1) / kRegisterPerPage);
		}

		RE::BSFixedString L_GetWeddingRegisterPage(RE::StaticFunctionTag*, std::int32_t a_page)
		{
			const auto  list = Ladder::WeddingSchedule();
			const auto  n = static_cast<std::int32_t>(list.size());
			const auto  pages = std::max<std::int32_t>(1, (n + kRegisterPerPage - 1) / kRegisterPerPage);
			const auto  page = std::clamp<std::int32_t>(a_page, 1, pages);
			std::string text = "Temple of Mara - weddings to come\n\n";
			if (list.empty()) {
				text += "No weddings are planned at present.";
				return RE::BSFixedString(text);
			}
			for (std::int32_t i = (page - 1) * kRegisterPerPage; i < std::min(n, page * kRegisterPerPage); ++i) {
				text += list[static_cast<std::size_t>(i)] + "\n";
			}
			if (pages > 1) {
				text += std::format("\nPage {} of {}", page, pages);
			}
			return RE::BSFixedString(text);
		}

		// the ceremony is over: married now (same path as any promotion - news, letters, ring)
		bool L_PostponeWedding(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y)
		{
			auto* bx = a_x ? a_x->GetActorBase() : nullptr;
			auto* by = a_y ? a_y->GetActorBase() : nullptr;
			return bx && by && Ladder::PostponeWedding(bx->GetFormID(), by->GetFormID());
		}

		bool L_CompleteWedding(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y)
		{
			if (!a_x || !a_y || Ladder::GetStage(a_x, a_y) != Ladder::Stage::Engaged) {
				return false;
			}
			return Ladder::SetStage(a_x, a_y, Ladder::Stage::Married);
		}

		// the town / city / settlement a location belongs to (walks up the parents); the last one below the hold otherwise
		RE::BGSLocation* SettlementOf(RE::BGSLocation* a_loc)
		{
			for (auto* l = a_loc; l; l = l->parentLoc) {
				if (l->HasKeywordString("LocTypeCity") || l->HasKeywordString("LocTypeTown") || l->HasKeywordString("LocTypeSettlement")) {
					return l;
				}
				if (!l->parentLoc || l->parentLoc->HasKeywordString("LocTypeHold")) {
					return l;
				}
			}
			return nullptr;
		}

		// date nights: do the two live in the same town (editor locations)? Keeps evening visits a short walk, not a trek
		bool L_SameSettlement(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y)
		{
			if (!a_x || !a_y) {
				return false;
			}
			auto* sx = SettlementOf(a_x->GetEditorLocation());
			auto* sy = SettlementOf(a_y->GetEditorLocation());
			return sx && sx == sy;
		}

		// 0.22.0 households: married couples who live apart move in together - moverRef, hostRef, home, sameTown quads.
		// mover = the partner whose editor location is not the shared home. Widowed pairs stay listed while the surviving
		// mover is alive (the widow keeps the house); a pair whose mover is dead drops out. sameTown 0 = they live in
		// different towns: no household, Papyrus logs it once.
		std::vector<std::int32_t> L_GetHouseholds(RE::StaticFunctionTag*)
		{
			std::vector<std::int32_t> out;
			std::vector<RE::FormID>   taken;  // 0.22.3: movers AND hosts already placed - a remarried widow who hosts her new spouse is not also sent to the old house
			Simulation::EnsurePool();
			const auto pairs = Ladder::Snapshot();
			for (int pass = 0; pass < 2; ++pass) {  // married first, so a remarried widow moves in with the new spouse
				const auto want = pass == 0 ? Ladder::Stage::Married : Ladder::Stage::Widowed;
				for (const auto& p : pairs) {
					if (p.stage != want || !p.home) {
						continue;
					}
					auto* a = RE::TESForm::LookupByID<RE::Actor>(Ladder::RefOfBase(p.a));
					auto* b = RE::TESForm::LookupByID<RE::Actor>(Ladder::RefOfBase(p.b));
					if (!a || !b) {
						continue;
					}
					auto*      la = a->GetEditorLocation();
					auto*      lb = b->GetEditorLocation();
					const bool aHome = la && la->GetFormID() == p.home;
					const bool bHome = lb && lb->GetFormID() == p.home;
					if (aHome == bHome) {
						continue;  // already living together (or neither lives at the home)
					}
					auto* mover = aHome ? b : a;
					auto* host = aHome ? a : b;
					if (mover->IsDead()) {  // 0.22.3: a dead host keeps the household (HouseholdAnchor sends the widow to the home's marker) - no "moved out" + "moved in" at a death
						continue;
					}
					if (std::find(taken.begin(), taken.end(), mover->GetFormID()) != taken.end()) {
						continue;
					}
					taken.push_back(mover->GetFormID());
					taken.push_back(host->GetFormID());
					auto* sm = SettlementOf(mover->GetEditorLocation());
					auto* sh = SettlementOf(host->GetEditorLocation());
					out.push_back(static_cast<std::int32_t>(mover->GetFormID()));
					out.push_back(static_cast<std::int32_t>(host->GetFormID()));
					out.push_back(static_cast<std::int32_t>(p.home));
					out.push_back(sm && sm == sh ? 1 : 0);
				}
			}
			return out;
		}

		// is the actor's current (loaded) cell part of this location?
		RE::TESObjectCELL* CellInHome(RE::Actor* a_who, RE::BGSLocation* a_home)
		{
			auto* cell = a_who && a_home ? a_who->GetParentCell() : nullptr;
			auto* loc = cell ? cell->GetLocation() : nullptr;
			if (!loc || (loc != a_home && !loc->IsParent(a_home))) {
				return nullptr;
			}
			return cell;
		}

		// households: the faction that owns the home's cell (vanilla houses: "<X> House Faction"), None when the cell is not
		// faction-owned or the actor is not standing in the home. Joining it makes the house theirs: its beds, no trespass.
		// 0.22.3: the same without anyone standing inside - the first interior cell of the location that has a faction owner (cell
		// records and their ownership load with the game, not with the cell), so a household can settle in on its first evening
		RE::TESFaction* HomeFactionOffline(RE::BGSLocation* a_home)
		{
			auto* dh = a_home ? RE::TESDataHandler::GetSingleton() : nullptr;
			if (!dh) {
				return nullptr;
			}
			for (auto* cell : dh->interiorCells) {
				auto* loc = cell ? cell->GetLocation() : nullptr;
				if (loc && (loc == a_home || loc->IsParent(a_home))) {
					if (auto* f = cell->GetFactionOwner()) {
						return f;
					}
				}
			}
			return nullptr;
		}

		RE::TESFaction* L_GetHomeFaction(RE::StaticFunctionTag*, RE::Actor* a_who, RE::BGSLocation* a_home)
		{
			if (auto* cell = CellInHome(a_who, a_home)) {
				if (auto* f = cell->GetFactionOwner()) {
					return f;
				}
			}
			return HomeFactionOffline(a_home);
		}

		// households (homes that are not faction-owned): the nearest free bed in the actor's loaded cell of the home - can be
		// slept in, no owner of its own, not disabled; a real bed before a bedroll. None = no free bed (try again tomorrow).
		RE::TESObjectREFR* L_FindFreeBed(RE::StaticFunctionTag*, RE::Actor* a_who, RE::BGSLocation* a_home)
		{
			auto* cell = CellInHome(a_who, a_home);
			if (!cell) {
				return nullptr;
			}
			const auto         pos = a_who->GetPosition();
			RE::TESObjectREFR* best = nullptr;
			float              bestScore = 0.0f;
			cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
				auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
				auto* furn = base ? base->As<RE::TESFurniture>() : nullptr;
				if (!furn || a_ref->IsDisabled() || a_ref->IsDeleted() || !furn->furnFlags.all(RE::TESFurniture::ActiveMarker::kCanSleep)) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto* own = a_ref->extraList.GetByType<RE::ExtraOwnership>();
				if (own && own->owner) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const char* model = furn->GetModel();
				const bool  roll = model && std::strstr(model, "edroll");
				const float score = a_ref->GetPosition().GetDistance(pos) + (roll ? 100000.0f : 0.0f);
				if (!best || score < bestScore) {
					best = a_ref;
					bestScore = score;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			return best;
		}

		// a location's own marker: its world location marker, else its LocationCenterMarker special ref, else (inns) the
		// innkeeper (Boss ref type). Persistent references, so a package can lead there while the cell is unloaded.
		RE::TESObjectREFR* MarkerOf(RE::BGSLocation* a_loc, bool a_bossOk)
		{
			if (!a_loc) {
				return nullptr;
			}
			if (auto m = a_loc->worldLocMarker.get()) {
				return m.get();
			}
			RE::TESObjectREFR* boss = nullptr;
			for (const auto& s : a_loc->specialRefs) {
				const RE::FormID t = s.type ? s.type->GetFormID() : 0;
				if (t == 0x01BDF1) {  // LocationCenterMarker
					if (auto* r = RE::TESForm::LookupByID<RE::TESObjectREFR>(s.refData.refID)) {
						return r;
					}
				} else if (t == 0x0130F7 && !boss) {  // Boss
					boss = RE::TESForm::LookupByID<RE::TESObjectREFR>(s.refData.refID);
				}
			}
			return a_bossOk ? boss : nullptr;
		}

		// households: a spot for a home - a_kind 0: its town's centre (children play there by day), 1: the town's inn (nights
		// out), 2: the home itself (a widow's anchor). None = no such spot. Several inns: picked by a_pick (stable per couple).
		RE::TESObjectREFR* L_FindTownSpot(RE::StaticFunctionTag*, RE::BGSLocation* a_home, std::int32_t a_kind, std::int32_t a_pick)
		{
			if (a_kind == 2) {
				return MarkerOf(a_home, false);
			}
			auto* town = SettlementOf(a_home);
			if (!town) {
				return nullptr;
			}
			if (a_kind == 0) {
				return town == a_home ? nullptr : MarkerOf(town, false);
			}
			std::vector<RE::TESObjectREFR*> inns;
			if (auto* dh = RE::TESDataHandler::GetSingleton()) {
				for (auto* l : dh->GetFormArray<RE::BGSLocation>()) {
					if (l && l != town && l->HasKeywordString("LocTypeInn") && l->IsParent(town)) {
						if (auto* m = MarkerOf(l, true)) {
							inns.push_back(m);
						}
					}
				}
			}
			if (inns.empty()) {
				return nullptr;
			}
			const auto n = static_cast<std::int32_t>(inns.size());
			return inns[static_cast<std::size_t>(((a_pick % n) + n) % n)];
		}

		// sleep-overs: is a QUEST driving this NPC right now? - a running quest's alias that forces packages on them, or a
		// current package owned by a quest. Quests always win over a sleep-over.
		// 0.14.0: the partner who will not come to the wedding (stood up at the altar), None = both come / not decided yet
		RE::Actor* L_GetAltarRunner(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y)
		{
			if (!a_x || !a_y) {
				return nullptr;
			}
			auto* bx = a_x->GetActorBase();
			auto* by = a_y->GetActorBase();
			if (!bx || !by) {
				return nullptr;
			}
			const RE::FormID runner = Ladder::AltarRunner(bx->GetFormID(), by->GetFormID());
			return runner == bx->GetFormID() ? a_x : runner == by->GetFormID() ? a_y : nullptr;
		}

		// 0.14.0: the player got an invitation letter - the wedding waits for them (up to 3 more days)
		bool L_SetWeddingInvite(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y)
		{
			auto* bx = a_x ? a_x->GetActorBase() : nullptr;
			auto* by = a_y ? a_y->GetActorBase() : nullptr;
			return bx && by && Ladder::SetWeddingInvite(bx->GetFormID(), by->GetFormID());
		}

		// 0.14.0: someone objected (attacked the bride or groom) during the ceremony - the wedding is off for now
		bool L_CallOffWedding(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y)
		{
			auto* bx = a_x ? a_x->GetActorBase() : nullptr;
			auto* by = a_y ? a_y->GetActorBase() : nullptr;
			return bx && by && Ladder::CallOffWedding(bx->GetFormID(), by->GetFormID());
		}

		// 0.13.3: another mod's wedding is running at the Temple of Mara (Love Is In The Air) - ours waits for a new date
		bool L_OtherWeddingBusy(RE::StaticFunctionTag*)
		{
			return Liita::WeddingBusy();
		}

		bool L_IsQuestDriven(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			if (auto* pack = a_actor->GetCurrentPackage(); pack && pack->ownerQuest && pack->ownerQuest->IsRunning()) {
				return true;
			}
			if (auto* aliases = a_actor->extraList.GetByType<RE::ExtraAliasInstanceArray>()) {
				for (auto* inst : aliases->aliases) {
					if (inst && inst->quest && inst->quest->IsRunning() && inst->instancedPackages && !inst->instancedPackages->empty()) {
						return true;
					}
				}
			}
			return false;
		}

		// sleep-overs: does the NPC's own routine say "go to bed" right now (current package is a sleep procedure)?
		bool L_IsOwnBedtime(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			auto* pack = a_actor ? a_actor->GetCurrentPackage() : nullptr;
			return pack && pack->procedureType == RE::PACKAGE_PROCEDURE_TYPE::kSleep;
		}

		// widowed NPCs alive and loaded near the player: survivorRef, deadIsFemale (0/1), wasMarried (0/1) triples.
		// wasMarried = the pair still carries its shared home (set on marriage, cleared only by a breakup).
		std::vector<std::int32_t> L_GetMourners(RE::StaticFunctionTag*, float a_maxDist)
		{
			std::vector<std::int32_t> out;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return out;
			}
			const auto  ppos = player->GetPosition();
			const float max2 = a_maxDist * a_maxDist;
			for (const auto& p : Ladder::Snapshot()) {
				if (p.stage != Ladder::Stage::Widowed) {
					continue;
				}
				auto* ra = RE::TESForm::LookupByID<RE::Actor>(Ladder::RefOfBase(p.a));
				auto* rb = RE::TESForm::LookupByID<RE::Actor>(Ladder::RefOfBase(p.b));
				if (!ra || !rb || ra->IsDead() == rb->IsDead()) {
					continue;
				}
				auto* survivor = ra->IsDead() ? rb : ra;
				auto* dead = ra->IsDead() ? ra : rb;
				if (!survivor->Is3DLoaded() || survivor->GetParentCell() != player->GetParentCell()) {
					continue;
				}
				const auto dpos = survivor->GetPosition() - ppos;
				if (dpos.x * dpos.x + dpos.y * dpos.y + dpos.z * dpos.z > max2) {
					continue;
				}
				auto* deadBase = dead->GetActorBase();
				out.push_back(static_cast<std::int32_t>(survivor->GetFormID()));
				out.push_back(deadBase && deadBase->IsFemale() ? 1 : 0);
				out.push_back(p.home ? 1 : 0);
			}
			return out;
		}

		// "Ask about their partner": keeps the hidden faction ONPCR_CoupleStage (Letters 0x958) rank = the NPC's couple stage, for
		// loaded NPCs near the player. Several pairs: the live one wins (married > engaged > dating > widowed > jilted > divorced
		// > casual). NPCs ranked before whose pair is gone drop to rank 0 (no answer line matches 0).
		std::int32_t L_SyncStageFaction(RE::StaticFunctionTag*, float a_maxDist)
		{
			static std::unordered_set<RE::FormID> ranked;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* dh = RE::TESDataHandler::GetSingleton();
			auto* faction = dh ? dh->LookupForm<RE::TESFaction>(0x958, "OStimNPCsReforged - Letters.esp") : nullptr;
			if (!player || !faction) {
				return 0;
			}
			Liita::Sync();  // 0.13.3: Love Is In The Air's exclusion list follows our couples (no-op without LIITA.esp)
			// remember mod followers travelling with the player - catches the ones with their own follow system
			if (auto* pl = RE::ProcessLists::GetSingleton()) {
				for (auto& h : pl->highActorHandles) {
					auto a = h.get();
					if (a && a->IsPlayerTeammate()) {
						Ladder::NoteFollowerSeen(a.get());
					}
				}
			}
			auto priority = [](Ladder::Stage a_s) {
				switch (a_s) {
				case Ladder::Stage::Married: return 7;
				case Ladder::Stage::Engaged: return 6;
				case Ladder::Stage::Candidate: return 5;
				case Ladder::Stage::Widowed: return 4;
				case Ladder::Stage::Jilted: return 3;
				case Ladder::Stage::Divorced: return 2;
				case Ladder::Stage::Casual: return 1;
				default: return 0;
				}
			};
			std::unordered_map<RE::FormID, Ladder::Stage> best;  // actor ref -> stage
			// 0.14.0: couple mood (Letters 0xD20): 1 warm, 2 fine, 3 cold - "Is married life going well?" and, from 0.16.1,
			// the tone variants (warm / cold recordings of the same line). 0.16.1: dating and engaged couples too, taken
			// from the actor's highest couple (the married-life topic still checks stage 4 itself)
			std::unordered_map<RE::FormID, std::pair<std::int32_t, std::int32_t>> moods;  // ref -> (stage priority, mood)
			for (const auto& p : Ladder::Snapshot()) {
				if (Ladder::IsSideStage(p.stage)) {
					continue;  // 0.19.0: affairs, crushes and thralls are not a couple to the dialogue (no stage rank, no couple mood)
				}
				for (auto base : { p.a, p.b }) {
					const auto ref = Ladder::RefOfBase(base);
					if (!ref) {
						continue;
					}
					auto it = best.find(ref);
					if (it == best.end() || priority(p.stage) > priority(it->second)) {
						best[ref] = p.stage;
					}
					if (p.stage == Ladder::Stage::Married || p.stage == Ladder::Stage::Engaged || p.stage == Ladder::Stage::Candidate) {
						const auto pr = priority(p.stage);
						auto mt = moods.find(ref);
						if (mt == moods.end() || pr > mt->second.first) {
							moods[ref] = { pr, (p.cold || p.closeness < 25.0f) ? 3 : p.closeness >= 60.0f ? 1 : 2 };
						}
					}
				}
			}
			auto* moodFaction = dh->LookupForm<RE::TESFaction>(0xD20, "OStimNPCsReforged - Letters.esp");
			for (auto ref : ranked) {
				if (!best.contains(ref)) {
					best[ref] = Ladder::Stage::None;
				}
			}
			const auto   ppos = player->GetPosition();
			const float  max2 = a_maxDist * a_maxDist;
			std::int32_t changed = 0;
			for (const auto& [ref, stage] : best) {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(ref);
				if (!actor || actor->IsPlayerRef() || !actor->Is3DLoaded() || actor->GetParentCell() != player->GetParentCell()) {
					continue;
				}
				if (actor->IsChild() || !Ladder::IsHumanoid(actor)) {
					continue;  // never tag a child or a creature, whatever an old record says
				}
				const auto d = actor->GetPosition() - ppos;
				if (d.x * d.x + d.y * d.y + d.z * d.z > max2) {
					continue;
				}
				if (moodFaction) {
					const bool together = stage == Ladder::Stage::Married || stage == Ladder::Stage::Engaged || stage == Ladder::Stage::Candidate;
					const auto m = together && moods.contains(ref) ? moods[ref].second : 0;
					const bool inMood = actor->IsInFaction(moodFaction);
					if (m == 0 && inMood) {
						actor->RemoveFromFaction(moodFaction);
					} else if (m != 0 && (!inMood || actor->GetFactionRank(moodFaction, false) != m)) {
						if (inMood) {
							actor->RemoveFromFaction(moodFaction);
						}
						actor->AddToFaction(moodFaction, static_cast<std::int8_t>(m));
					}
				}
				const auto want = static_cast<std::int32_t>(stage);
				const bool inFaction = actor->IsInFaction(faction);
				if (inFaction && actor->GetFactionRank(faction, false) == want) {
					ranked.insert(ref);
					continue;
				}
				if (inFaction) {
					actor->RemoveFromFaction(faction);
				}
				actor->AddToFaction(faction, static_cast<std::int8_t>(want));
				ranked.insert(ref);
				++changed;
			}
			return changed;
		}

		void L_EnsureRing(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			if (a_actor) {
				{ auto* b = a_actor->GetActorBase(); Amulet::EnsureMarried(a_actor->GetFormID(), b && Ladder::IsGameMarried(b->GetFormID())); }
			}
		}

		void L_Tick(RE::StaticFunctionTag*)
		{
			Simulation::Tick();
		}

		// 0.17.0 "Import from other mods" (ONpcMain.OnReforgedImportOthers reads Lover's Ledger and calls these)
		bool L_ImportPair(RE::StaticFunctionTag*, RE::TESNPC* a_a, RE::TESNPC* a_b)
		{
			return a_a && a_b && Ladder::ImportOtherPair(a_a->GetFormID(), a_b->GetFormID());
		}

		void L_FinishOtherImport(RE::StaticFunctionTag*, std::int32_t a_count, std::string a_source)
		{
			Ladder::FinishOtherImport(a_count, a_source.c_str());
		}

		// 0.22.2: scene candidates near the player skip generic-named NPCs (PoolExclusions.json) and guards, like the off-screen pool
		bool L_IsSceneExcluded(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			return Simulation::IsSceneExcluded(a_actor);
		}

		// 0.22.0: a News page line written by Papyrus (households: moved in / moved out)
		void L_AddNewsLine(RE::StaticFunctionTag*, std::string a_text)
		{
			if (!a_text.empty()) {
				Ladder::Chronicle(a_text);
			}
		}

		// 0.24.0 baby stages (ONPCR_BeeingFemaleBridge): the reasons from OStimNPCsReforged_ChildAway.json and the Couples page notes
		std::int32_t L_ChildAwayReasonCount(RE::StaticFunctionTag*)
		{
			return Babies::ReasonCount();
		}

		std::int32_t L_PickChildAwayReason(RE::StaticFunctionTag*, RE::Actor* a_mother, RE::Actor* a_father)
		{
			return Babies::PickReason(a_mother, a_father);
		}

		// 0.25.0: with each parent's M.A.R.A.S social class (-1 unknown) for the estate / kindly widow places
		std::int32_t L_PickChildAwayReasonFor(RE::StaticFunctionTag*, RE::Actor* a_mother, RE::Actor* a_father, std::int32_t a_classMother, std::int32_t a_classFather)
		{
			return Babies::PickReason(a_mother, a_father, a_classMother, a_classFather);
		}

		std::string L_ChildAwayPlace(RE::StaticFunctionTag*, std::int32_t a_index)
		{
			return Babies::ReasonPlace(a_index);
		}

		// 0.25.1 (bridge 18): away places by id
		std::string L_ChildAwayReasonId(RE::StaticFunctionTag*, std::int32_t a_index)
		{
			return Babies::ReasonId(a_index);
		}

		std::string L_ChildAwayPlaceById(RE::StaticFunctionTag*, std::string a_id)
		{
			return Babies::PlaceById(a_id);
		}

		std::int32_t L_ChildAwayDialogueRank(RE::StaticFunctionTag*, std::string a_id)
		{
			return Babies::DialogueRank(a_id);
		}

		void L_ClearBabyNotes(RE::StaticFunctionTag*)
		{
			Babies::ClearNotes();
		}

		void L_AddBabyNote(RE::StaticFunctionTag*, RE::Actor* a_mother, RE::Actor* a_father, std::string a_text)
		{
			auto* m = a_mother ? a_mother->GetActorBase() : nullptr;
			auto* f = a_father ? a_father->GetActorBase() : nullptr;
			if (m && f) {
				Babies::AddNote(m->GetFormID(), f->GetFormID(), a_text);
			}
		}

		// the placed actor for a base NPC the ladder knows (simulation pool or seen in a scene); None when unknown
		RE::Actor* L_GetActorForBase(RE::StaticFunctionTag*, RE::TESNPC* a_base)
		{
			const RE::FormID ref = a_base ? Ladder::RefOfBase(a_base->GetFormID()) : 0;
			return ref ? RE::TESForm::LookupByID<RE::Actor>(ref) : nullptr;
		}

		std::vector<std::int32_t> L_PopLedgerWrites(RE::StaticFunctionTag*)
		{
			std::vector<std::int32_t> out;
			for (const auto& w : Ladder::PopLedgerWrites()) {
				out.push_back(static_cast<std::int32_t>(w.refA));
				out.push_back(static_cast<std::int32_t>(w.refB));
				out.push_back(w.day);
			}
			return out;
		}

		std::int32_t L_GetPoolSize(RE::StaticFunctionTag*)
		{
			return static_cast<std::int32_t>(Simulation::PoolSize());
		}

		std::int32_t L_GetLastSimDay(RE::StaticFunctionTag*)
		{
			return Simulation::LastSimulatedDay();
		}

		void L_SetArchetype(RE::StaticFunctionTag*, RE::Actor* a_actor, std::int32_t a_class, std::int32_t a_skill, std::int32_t a_temperament)
		{
			if (a_actor && a_actor->GetActorBase()) {
				Ladder::SetArchetype(a_actor->GetActorBase()->GetFormID(), a_class, a_skill, a_temperament);
			}
		}

		std::vector<std::int32_t> L_GetArchetypeRequests(RE::StaticFunctionTag*, std::int32_t a_max)
		{
			std::vector<std::int32_t> out;
			Simulation::EnsurePool();
			for (const auto ref : Simulation::ArchetypeRequests(static_cast<std::size_t>(std::clamp(a_max, 1, 200)))) {
				out.push_back(static_cast<std::int32_t>(ref));
			}
			return out;
		}

		std::vector<std::int32_t> L_PopNews(RE::StaticFunctionTag*)
		{
			std::vector<std::int32_t> out;
			// news carries BASE ids; Papyrus needs the actors in the world (before 0.7.2 the base ids were sent and every
			// item was silently skipped). Items whose NPC has no known reference are dropped.
			for (const auto& n : Ladder::PopNews()) {
				const RE::FormID ra = Ladder::RefOfBase(n.a);
				const RE::FormID rb = Ladder::RefOfBase(n.b);
				if (!ra || !rb) {
					SKSE::log::info("Ladder news {} dropped: no reference for {} / {}", n.type, Ladder::NpcName(n.a), Ladder::NpcName(n.b));
					continue;
				}
				out.push_back(n.type);
				out.push_back(static_cast<std::int32_t>(ra));
				out.push_back(static_cast<std::int32_t>(rb));
			}
			return out;
		}

		bool L_SetStage(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y, std::int32_t a_stage)
		{
			return Ladder::SetStage(a_x, a_y, static_cast<Ladder::Stage>(std::clamp(a_stage, 0, 7)));
		}

		bool Bind(RE::BSScript::IVirtualMachine* a_vm)
		{
			if (!a_vm) {
				return false;
			}
			a_vm->RegisterFunction("GetFloat", kScript, GetFloat);
			a_vm->RegisterFunction("GetBool", kScript, GetBool);
			a_vm->RegisterFunction("GetInt", kScript, GetInt);
			a_vm->RegisterFunction("Reload", kScript, Reload);
			a_vm->RegisterFunction("GetVersion", kScript, GetVersion);

			a_vm->RegisterFunction("RecordEncounter", kLadderScript, L_RecordEncounter);
			a_vm->RegisterFunction("AllowPlayerChosen", kLadderScript, L_AllowPlayerChosen);
			a_vm->RegisterFunction("GetStage", kLadderScript, L_GetStage);
			a_vm->RegisterFunction("GetStageName", kLadderScript, L_GetStageName);
			a_vm->RegisterFunction("IsCommittedTo", kLadderScript, L_IsCommittedTo);
			a_vm->RegisterFunction("IsCommittedToOther", kLadderScript, L_IsCommittedToOther);
			a_vm->RegisterFunction("GetPartnerStage", kLadderScript, L_GetPartnerStage);
			a_vm->RegisterFunction("GetPartnerBase", kLadderScript, L_GetPartnerBase);
			a_vm->RegisterFunction("Tick", kLadderScript, L_Tick);
			a_vm->RegisterFunction("SetStage", kLadderScript, L_SetStage);
			a_vm->RegisterFunction("ImportPair", kLadderScript, L_ImportPair);
			a_vm->RegisterFunction("FinishOtherImport", kLadderScript, L_FinishOtherImport);
			a_vm->RegisterFunction("GetActorForBase", kLadderScript, L_GetActorForBase);
			a_vm->RegisterFunction("PopLedgerWrites", kLadderScript, L_PopLedgerWrites);
			a_vm->RegisterFunction("GetPoolSize", kLadderScript, L_GetPoolSize);
			a_vm->RegisterFunction("GetLastSimDay", kLadderScript, L_GetLastSimDay);
			a_vm->RegisterFunction("SetArchetype", kLadderScript, L_SetArchetype);
			a_vm->RegisterFunction("GetArchetypeRequests", kLadderScript, L_GetArchetypeRequests);
			a_vm->RegisterFunction("PopNews", kLadderScript, L_PopNews);
			a_vm->RegisterFunction("IsMourning", kLadderScript, L_IsMourning);
			a_vm->RegisterFunction("InPlayerPartnerFaction", kLadderScript, L_InPlayerPartnerFaction);
			a_vm->RegisterFunction("SetPregnant", kLadderScript, L_SetPregnant);
			a_vm->RegisterFunction("IsPregnant", kLadderScript, L_IsPregnant);
			a_vm->RegisterFunction("CanHaveChildren", kLadderScript, L_CanHaveChildren);
			a_vm->RegisterFunction("PostponeWedding", kLadderScript, L_PostponeWedding);
			a_vm->RegisterFunction("GetFertilityCheckRefs", kLadderScript, L_GetFertilityCheckRefs);
			a_vm->RegisterFunction("SetEngineChildren", kLadderScript, L_SetEngineChildren);
			a_vm->RegisterFunction("AddChild", kLadderScript, L_AddChild);
			a_vm->RegisterFunction("GetCommittedPairRefs", kLadderScript, L_GetCommittedPairRefs);
			a_vm->RegisterFunction("GetVoicePairs", kLadderScript, L_GetVoicePairs);
			a_vm->RegisterFunction("GetPartnerRef", kLadderScript, L_GetPartnerRef);
			a_vm->RegisterFunction("EnsureRing", kLadderScript, L_EnsureRing);
			a_vm->RegisterFunction("IsSpeaking", kLadderScript, L_IsSpeaking);
			a_vm->RegisterFunction("IsIdle", kLadderScript, L_IsIdle);
			a_vm->RegisterFunction("IsSeated", kLadderScript, L_IsSeated);
			a_vm->RegisterFunction("GetMarriedHomes", kLadderScript, L_GetMarriedHomes);
			a_vm->RegisterFunction("GetSleepOverPairs", kLadderScript, L_GetSleepOverPairs);
			a_vm->RegisterFunction("GetHouseholds", kLadderScript, L_GetHouseholds);
			a_vm->RegisterFunction("GetHomeFaction", kLadderScript, L_GetHomeFaction);
			a_vm->RegisterFunction("FindFreeBed", kLadderScript, L_FindFreeBed);
			a_vm->RegisterFunction("FindTownSpot", kLadderScript, L_FindTownSpot);
			a_vm->RegisterFunction("AddNewsLine", kLadderScript, L_AddNewsLine);
			a_vm->RegisterFunction("ChildAwayReasonCount", kLadderScript, L_ChildAwayReasonCount);
			a_vm->RegisterFunction("PickChildAwayReason", kLadderScript, L_PickChildAwayReason);
			a_vm->RegisterFunction("PickChildAwayReasonFor", kLadderScript, L_PickChildAwayReasonFor);
			a_vm->RegisterFunction("ChildAwayPlace", kLadderScript, L_ChildAwayPlace);
			a_vm->RegisterFunction("ChildAwayReasonId", kLadderScript, L_ChildAwayReasonId);
			a_vm->RegisterFunction("ChildAwayPlaceById", kLadderScript, L_ChildAwayPlaceById);
			a_vm->RegisterFunction("ChildAwayDialogueRank", kLadderScript, L_ChildAwayDialogueRank);
			a_vm->RegisterFunction("ClearBabyNotes", kLadderScript, L_ClearBabyNotes);
			a_vm->RegisterFunction("AddBabyNote", kLadderScript, L_AddBabyNote);
			a_vm->RegisterFunction("IsSceneExcluded", kLadderScript, L_IsSceneExcluded);
			a_vm->RegisterFunction("PlanFamily", kLadderScript, L_PlanFamily);
			a_vm->RegisterFunction("GetFamilyPairs", kLadderScript, L_GetFamilyPairs);
			a_vm->RegisterFunction("IsQuestDriven", kLadderScript, L_IsQuestDriven);
			a_vm->RegisterFunction("OtherWeddingBusy", kLadderScript, L_OtherWeddingBusy);
			a_vm->RegisterFunction("GetAltarRunner", kLadderScript, L_GetAltarRunner);
			a_vm->RegisterFunction("SetWeddingInvite", kLadderScript, L_SetWeddingInvite);
			a_vm->RegisterFunction("CallOffWedding", kLadderScript, L_CallOffWedding);
			a_vm->RegisterFunction("IsOwnBedtime", kLadderScript, L_IsOwnBedtime);
			a_vm->RegisterFunction("GetMourners", kLadderScript, L_GetMourners);
			a_vm->RegisterFunction("SyncStageFaction", kLadderScript, L_SyncStageFaction);
			a_vm->RegisterFunction("GetDatingPairs", kLadderScript, L_GetDatingPairs);
			a_vm->RegisterFunction("SameSettlement", kLadderScript, L_SameSettlement);
			a_vm->RegisterFunction("GetWeddings", kLadderScript, L_GetWeddings);
			a_vm->RegisterFunction("CompleteWedding", kLadderScript, L_CompleteWedding);
			a_vm->RegisterFunction("CanSay", kLadderScript, L_CanSay);
			a_vm->RegisterFunction("PlanChat", kLadderScript, L_PlanChat);
			a_vm->RegisterFunction("PlanPart", kLadderScript, L_PlanPart);
			a_vm->RegisterFunction("NoteMetNearby", kLadderScript, L_NoteMetNearby);
			a_vm->RegisterFunction("GetWeddingGuests", kLadderScript, L_GetWeddingGuests);
			a_vm->RegisterFunction("GetWeddingRegister", kLadderScript, L_GetWeddingRegister);
			a_vm->RegisterFunction("GetWeddingRegisterPages", kLadderScript, L_GetWeddingRegisterPages);
			a_vm->RegisterFunction("GetWeddingRegisterPage", kLadderScript, L_GetWeddingRegisterPage);
			SKSE::log::info("Papyrus natives registered on {} and {}", kScript, kLadderScript);
			return true;
		}
	}

	bool SameSettlement(RE::Actor* a_x, RE::Actor* a_y)
	{
		return L_SameSettlement(nullptr, a_x, a_y);
	}

	void Register()
	{
		auto* papyrus = SKSE::GetPapyrusInterface();
		if (!papyrus || !papyrus->Register(Bind)) {
			SKSE::log::error("Papyrus interface unavailable - natives not registered.");
		}
	}

	void NotifyChanged(const char* a_key)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			return;
		}
		const std::string key = a_key ? a_key : "*";
		tasks->AddTask([key] {
			auto* source = SKSE::GetModCallbackEventSource();
			if (!source) {
				return;
			}
			SKSE::ModCallbackEvent event{ RE::BSFixedString(kEvent), RE::BSFixedString(key), 0.0f, nullptr };
			source->SendEvent(&event);
		});
	}

	namespace
	{
		std::mutex              fertilityGuard;
		std::vector<RE::FormID> fertilityRefs;
	}

	void SendFertilityCheck(const std::vector<RE::FormID>& a_refs)
	{
		{
			std::scoped_lock lock(fertilityGuard);
			fertilityRefs = a_refs;
		}
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([] {
				if (auto* source = SKSE::GetModCallbackEventSource()) {
					SKSE::ModCallbackEvent event{ RE::BSFixedString("ONPCR_FertilityCheck"), RE::BSFixedString(""), 0.0f, nullptr };
					source->SendEvent(&event);
				}
			});
		}
	}

	namespace
	{
		std::vector<RE::Actor*> L_GetFertilityCheckRefs(RE::StaticFunctionTag*)
		{
			std::vector<RE::Actor*> out;
			std::scoped_lock        lock(fertilityGuard);
			for (const auto id : fertilityRefs) {
				if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) {
					out.push_back(a);
				}
			}
			return out;
		}
	}

	// Couples page "Import from other mods": ONpcMain reads Lover's Ledger and imports pairs with at least a_minScenes scenes
	void SendImportOthers(int a_minScenes)
	{
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([a_minScenes] {
				if (auto* source = SKSE::GetModCallbackEventSource()) {
					SKSE::ModCallbackEvent event{ RE::BSFixedString("ONPCR_ImportOthers"), RE::BSFixedString(""), static_cast<float>(a_minScenes), nullptr };
					source->SendEvent(&event);
				}
			});
		}
	}

	void SendVoiceTest()
	{
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([] {
				if (auto* source = SKSE::GetModCallbackEventSource()) {
					SKSE::ModCallbackEvent event{ RE::BSFixedString("ONPCR_VoiceTest"), RE::BSFixedString(""), 0.0f, nullptr };
					source->SendEvent(&event);
				}
			});
		}
	}
}
