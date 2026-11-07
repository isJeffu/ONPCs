#include "Base.h"

#include "Ladder.h"
#include "Plugin.h"
#include "Settings.h"
#include "ThirdParty/OStimNG/OstimNG-API-Thread.h"

#include <random>

namespace ONPCR::Base
{
	namespace
	{
		constexpr auto kScript = "ONPCR_Base";
		constexpr auto kLedgerPlugin = "TT_LoversLedger.esp";
		constexpr auto kOslPlugin = "OSLAroused.esp";       // OSL Aroused: every NPC's arousal, read in one batch call (Natural Arousal shapes it)
		constexpr auto kSlaPlugin = "SexLabAroused.esm";    // the SexLab Aroused standard (SLO Aroused NG, Aroused Redux, classic SLA)
		constexpr RE::FormID kSlaFrameworkQuest = 0x04290F;  // sla_Framework, script slaFrameworkScr: int GetActorArousal(Actor)

		// Skyrim.esm association types (ASTP)
		constexpr RE::FormID kSpouse = 0x0142CA;
		constexpr RE::FormID kCourting = 0x01EE23;
		constexpr RE::FormID kFamily[] = {
			0x0142C6,  // ParentChild
			0x0142C5,  // Siblings
			0x0142C8,  // Cousins
			0x0142C7,  // AuntUncle
			0x01E803,  // GrandparentGrandchild
			0x01E90B,  // GreatGrandparentGreatgrandchild
			0x01E80B,  // GrandAuntUncle
			0x01E917,  // InLawParentChild
			0x01E764,  // InLawBrotherSister
			0x01E80D,  // InLawAuntUncle
			0x01E90A,  // InLawGrandparentGrandchild
		};

		enum class Bond
		{
			kNone,
			kLedger,    // a past scene in Lover's Ledger
			kCasual,    // Reforged's own couples: met and had a casual encounter
			kCourting,  // a courting record, or Reforged's candidate stage
			kEngaged,
			kSpouse,
			kFamily,
			kEnded  // Reforged recorded a break-up (jilted / divorced) or a side relationship: never a pair
		};

		const char* BondName(Bond a_bond)
		{
			switch (a_bond) {
			case Bond::kSpouse:
				return "spouses";
			case Bond::kEngaged:
				return "engaged";
			case Bond::kCourting:
				return "courting";
			case Bond::kCasual:
				return "casual";
			case Bond::kLedger:
				return "past lovers";
			default:
				return "-";
			}
		}

		float Score(Bond a_bond)
		{
			switch (a_bond) {
			case Bond::kSpouse:
				return 5.0f;
			case Bond::kEngaged:
				return 4.0f;
			case Bond::kCourting:
				return 3.0f;
			case Bond::kCasual:
				return 2.0f;
			default:
				return 1.0f;
			}
		}

		// ---- arousal states (0.26.0): every source's 0..100 value read as a state; the settings name states, not numbers ----
		constexpr float kStateFloor[] = { 0.0f, 20.0f, 40.0f, 60.0f, 85.0f };
		constexpr const char* kStateName[] = { "Indifferent", "Content", "Interested", "Yearning", "Aching" };

		int StateOf(float a_value)
		{
			for (int i = 4; i > 0; --i) {
				if (a_value >= kStateFloor[i]) {
					return i;
				}
			}
			return 0;
		}

		enum class Source
		{
			kOsl,    // OSL Aroused
			kSla,    // a SexLab Aroused-standard mod
			kDesire  // none installed: Reforged's own desire
		};

		Source ArousalSource()
		{
			static const Source source = [] {
				auto* dh = RE::TESDataHandler::GetSingleton();
				const Source s = dh && dh->LookupModByName(kOslPlugin) ? Source::kOsl : dh && dh->LookupModByName(kSlaPlugin) ? Source::kSla : Source::kDesire;
				SKSE::log::info("Base: arousal from {}", s == Source::kOsl ? "OSL Aroused" : s == Source::kSla ? "a SexLab Aroused-type mod (SexLabAroused.esm)" : "Reforged's own desire (no arousal mod installed)");
				return s;
			}();
			return source;
		}

		std::mutex mutex;  // the caches below: the VM thread (natives, callbacks) and the task thread (Reset, OnPostLoad)

		// Lover's Ledger has no C++ API: each NPC's lovers are asked once per session through its Papyrus native
		// (TTLL_Store.GetAllLovers, async) the first time that NPC is near; scene ends add pairs right away
		std::unordered_map<RE::FormID, std::unordered_set<RE::FormID>> ledgerLovers;  // base -> lover bases
		std::unordered_set<RE::FormID>                                 ledgerAsked;   // bases already asked this session
		std::uint32_t                                                  generation = 0;  // bumped by Reset: late answers from the last save are dropped

		// arousal is what starts a scene: every nearby NPC's value is asked each scan (async) and used by the next; values
		// older than kArousalFresh count as unknown (an NPC who left and came back is asked again)
		struct Mood
		{
			float value;
			float day;
		};
		std::unordered_map<RE::FormID, Mood> arousal;  // actor ref -> 0..100 and when it was read
		// cooldown: a couple's last scene together (game day) from Lover's Ledger, asked once per session; scene ends update it
		std::unordered_map<std::uint64_t, float> lastTogether;  // pair key (two bases) -> day, <= 0 = never
		std::unordered_map<std::uint64_t, float> lastAsked;     // pair key -> day it was asked (timeout: kAskTimeout)
		bool                                     askFailLogged = false;
		// Reforged's own desire (no arousal mod): days since the NPC's last FULFILLING scene - this session's, else Lover's
		// Ledger's own "lasttime" for that NPC (asked once per session); a frustrating scene does not reset it
		std::unordered_map<RE::FormID, float> lastFulfilled;  // actor ref -> day
		std::unordered_map<RE::FormID, float> npcLastTime;    // actor ref -> Lover's Ledger lasttime (<= 0 = never)
		std::unordered_set<RE::FormID>        npcAsked;

		// 0.26.0 phase 2: OStim's own thread API (C++, no Papyrus) - who is in a scene right now. OStim sends its thread events
		// for the PLAYER's scenes only, so breaks after a scene come from our own scene ends (SceneEnded), not from OStim
		OstimNG_API::Thread::IThreadInterface* ostimThreads = nullptr;
		std::string                            missingRequirements;  // set once at kDataLoaded; non-empty = no scenes start
		std::unordered_map<RE::FormID, float>    lastSceneOf;  // actor ref -> game day their last Reforged scene ended
		constexpr float                          kArousalFresh = 0.05f;  // game days (~1 h): an older arousal value counts as unknown
		constexpr float                          kAskTimeout = 0.02f;    // game days (~30 min): no Lover's Ledger answer = never together

		std::uint64_t PairKey(RE::FormID a_x, RE::FormID a_y)
		{
			const auto lo = std::min(a_x, a_y);
			const auto hi = std::max(a_x, a_y);
			return (static_cast<std::uint64_t>(lo) << 32) | hi;
		}

		// 0.1.3: breaks read from the co-save. The load runs inside Ladder's callback (its own lock held), so the records wait
		// here under a leaf lock and are merged by the next caller that holds `mutex` (OnPostLoad, a scan, a save)
		struct Loaded
		{
			std::vector<std::pair<RE::FormID, float>>    scene;
			std::vector<std::pair<RE::FormID, float>>    fulfilled;
			std::vector<std::pair<std::uint64_t, float>> together;
			bool                                         any = false;
		};
		std::mutex loadMutex;  // leaf: never held while taking another lock
		Loaded     loaded;

		// caller holds the mutex; a value this session already has (a scene since the load) wins when it is later
		void MergeLoaded()
		{
			Loaded l;
			{
				std::scoped_lock lock{ loadMutex };
				if (!loaded.any) {
					return;
				}
				l = std::move(loaded);
				loaded = {};
			}
			for (const auto& [id, day] : l.scene) {
				auto& v = lastSceneOf[id];
				v = std::max(v, day);
			}
			for (const auto& [id, day] : l.fulfilled) {
				auto& v = lastFulfilled[id];
				v = std::max(v, day);
			}
			for (const auto& [key, day] : l.together) {
				auto& v = lastTogether[key];
				v = std::max(v, day);
			}
		}

		float Today()
		{
			auto* cal = RE::Calendar::GetSingleton();
			return cal ? cal->GetDaysPassed() : 0.0f;
		}

		// a fixed number per NPC (and per night): Reforged's desire gets its libido and nightly mood from it, the same every load
		std::uint32_t Mix(std::uint32_t a_x)
		{
			a_x ^= a_x >> 16;
			a_x *= 0x7FEB352Du;
			a_x ^= a_x >> 15;
			a_x *= 0x846CA68Bu;
			a_x ^= a_x >> 16;
			return a_x;
		}

		// Reforged's own desire, 0..100, when no arousal mod is installed (caller holds the mutex): it builds with the days since
		// their last fulfilling scene, plus a fixed libido (-15..15) and a nightly mood (-10..10)
		float DesireOf(RE::Actor* a_actor, float a_today)
		{
			const auto ref = a_actor->GetFormID();
			float      last = -1.0f;
			if (const auto f = lastFulfilled.find(ref); f != lastFulfilled.end()) {
				last = f->second;
			} else if (const auto n = npcLastTime.find(ref); n != npcLastTime.end() && n->second > 0.0f) {
				last = n->second;
			}
			const float days = last > 0.0f ? std::max(0.0f, a_today - last) : 1.0f;  // never / unknown: as if a day ago (~Interested, no burst after a load)
			const auto  base = a_actor->GetActorBase() ? a_actor->GetActorBase()->GetFormID() : ref;
			const float libido = static_cast<float>(Mix(base) % 31) - 15.0f;
			// "tonight" changes at noon, not at midnight in the middle of the night
			const float tonight = static_cast<float>(Mix(base ^ static_cast<std::uint32_t>(std::floor(a_today + 0.5f))) % 21) - 10.0f;
			return std::clamp(15.0f + 25.0f * days + libido + tonight, 0.0f, 100.0f);
		}

		// unique NPC base -> its placed reference, so a spouse who isn't loaded can still be found dead, disabled or alive
		std::unordered_map<RE::FormID, RE::FormID> refOfBase;
		bool                                       refMapBuilt = false;
		std::unordered_set<RE::FormID>             widowedLogged;

		void BuildRefMap()
		{
			const auto& [forms, lock] = RE::TESForm::GetAllForms();
			{
				RE::BSReadLockGuard guard{ lock };
				if (forms) {
					for (auto& [id, form] : *forms) {
						auto* actor = form ? form->As<RE::Actor>() : nullptr;
						auto* base = actor ? actor->GetActorBase() : nullptr;
						if (base && base->IsUnique()) {
							refOfBase.emplace(base->GetFormID(), actor->GetFormID());
						}
					}
				}
			}
			refMapBuilt = true;
			SKSE::log::info("Base: {} unique NPC reference(s) mapped", refOfBase.size());
		}

		// the NPC's placed reference, if we know it (caller holds the mutex)
		RE::Actor* RefOf(RE::TESNPC* a_base)
		{
			const auto it = a_base ? refOfBase.find(a_base->GetFormID()) : refOfBase.end();
			return it != refOfBase.end() ? RE::TESForm::LookupByID<RE::Actor>(it->second) : nullptr;
		}

		// false only when we know the NPC is gone: dead, or disabled (a mod removed them); unknown counts as alive
		bool IsAround(RE::TESNPC* a_base)
		{
			if (!a_base) {
				return false;
			}
			auto* ref = RefOf(a_base);
			return !ref || (!ref->IsDead() && !ref->IsDisabled());
		}

		bool IsFamilyType(RE::FormID a_assoc) { return std::ranges::find(kFamily, a_assoc) != std::end(kFamily); }

		// what the live relationship records say about two bases; records are kept on both NPCs, so either side is read
		Bond RecordBond(RE::TESNPC* a_x, RE::TESNPC* a_y)
		{
			Bond best = Bond::kNone;
			for (auto* side : { a_x, a_y }) {
				if (!side || !side->relationships) {
					continue;
				}
				for (auto* rel : *side->relationships) {
					if (!rel || !rel->assocType) {
						continue;
					}
					if (!((rel->npc1 == a_x && rel->npc2 == a_y) || (rel->npc1 == a_y && rel->npc2 == a_x))) {
						continue;
					}
					const auto assoc = rel->assocType->GetFormID();
					if (IsFamilyType(assoc)) {
						return Bond::kFamily;
					}
					// they no longer like each other (a quest or mod set rival or worse): never a pair, spouses included
					const auto level = rel->level.get();
					if (level >= RE::BGSRelationship::RELATIONSHIP_LEVEL::kRival) {
						return Bond::kEnded;
					}
					if (assoc == kSpouse) {
						best = Bond::kSpouse;
					} else if (assoc == kCourting && best != Bond::kSpouse) {
						best = Bond::kCourting;
					}
				}
			}
			return best;
		}

		// Reforged's own couple record wins over the game's: a recorded break-up ends a vanilla marriage too
		Bond LadderBond(RE::Actor* a_x, RE::Actor* a_y)
		{
			switch (Ladder::GetStage(a_x, a_y)) {
			case Ladder::Stage::Casual:
				return Bond::kCasual;
			case Ladder::Stage::Candidate:
				return Bond::kCourting;
			case Ladder::Stage::Engaged:
				return Bond::kEngaged;
			case Ladder::Stage::Married:
				return Bond::kSpouse;
			case Ladder::Stage::Jilted:
			case Ladder::Stage::Divorced:
			case Ladder::Stage::Affair:
			case Ladder::Stage::Crush:
			case Ladder::Stage::Thrall:
				return Bond::kEnded;
			default:
				return Bond::kNone;  // no record, ended quietly, or widowed: the game's records decide
			}
		}

		Bond BondOf(RE::Actor* a_x, RE::Actor* a_y)
		{
			const Bond record = RecordBond(a_x->GetActorBase(), a_y->GetActorBase());
			if (record == Bond::kFamily || record == Bond::kEnded) {
				return record;
			}
			// Reforged's own couple stages only while the Relationships module is installed (without it nothing keeps them
			// current, and an old "jilted" in the co-save would block a married couple for good)
			if (!Settings::Installed(Settings::Module::Relationships)) {
				return record;
			}
			const Bond ladder = LadderBond(a_x, a_y);
			return ladder != Bond::kNone ? ladder : record;
		}

		// how committed a bond is: 0 casual, 1 courting, 2 engaged, 3 married
		int Level(Bond a_bond)
		{
			switch (a_bond) {
			case Bond::kSpouse:
				return 3;
			case Bond::kEngaged:
				return 2;
			case Bond::kCourting:
				return 1;
			default:
				return 0;
			}
		}

		// the strongest living commitment a_actor has to someone other than a_except (0 none, 1 courting, 2 engaged, 3 married),
		// from the game's records or Reforged's couples (caller holds the mutex); a widow / widower is free (logged once per session)
		int CommitmentElsewhere(RE::Actor* a_actor, RE::Actor* a_except)
		{
			int best = 0;
			if (Settings::Installed(Settings::Module::Relationships) && Ladder::IsCommittedToOther(a_actor, a_except)) {
				switch (Ladder::GetPartnerStage(a_actor)) {
				case Ladder::Stage::Married:
					return 3;
				case Ladder::Stage::Engaged:
					best = 2;
					break;
				default:
					best = 1;
					break;
				}
			}
			auto* npc = a_actor->GetActorBase();
			auto* except = a_except->GetActorBase();
			if (!npc || !npc->relationships) {
				return best;
			}
			for (auto* rel : *npc->relationships) {
				if (!rel || !rel->assocType) {
					continue;
				}
				const auto assoc = rel->assocType->GetFormID();
				if (assoc != kSpouse && assoc != kCourting) {
					continue;
				}
				auto* other = rel->npc1 == npc ? rel->npc2 : rel->npc1;
				if (!other || other == except) {
					continue;
				}
				auto* otherRef = RefOf(other);
				if (otherRef && LadderBond(a_actor, otherRef) == Bond::kEnded) {
					continue;  // that marriage / courtship ended in Reforged's records
				}
				if (IsAround(other)) {
					best = std::max(best, assoc == kSpouse ? 3 : 1);
					if (best == 3) {
						return best;
					}
					continue;
				}
				if (assoc == kSpouse && widowedLogged.insert(npc->GetFormID()).second) {
					SKSE::log::info("Base: {} is widowed ({} is gone)", npc->GetName(), other->GetName());
				}
			}
			return best;
		}

		// 0.1.5 (user 7 Nov, "cheating will come later"): the base is faithful - a pair is blocked when either of them is more
		// committed to someone else than to this partner (a married NPC courting someone else: only the spouse; casual: only
		// when neither has anyone). Two spouses (an orc chief's wives) stay fine. Caller holds the mutex.
		bool CommittedAbove(RE::Actor* a_x, RE::Actor* a_y, Bond a_bond)
		{
			const int level = Level(a_bond);
			const int floor = a_bond == Bond::kCasual ? 0 : level;  // casual: any commitment elsewhere blocks
			return CommitmentElsewhere(a_x, a_y) > floor || CommitmentElsewhere(a_y, a_x) > floor;
		}

		bool LedgerPair(RE::TESNPC* a_x, RE::TESNPC* a_y)
		{
			for (auto [from, to] : { std::pair{ a_x, a_y }, std::pair{ a_y, a_x } }) {
				const auto it = ledgerLovers.find(from->GetFormID());
				if (it != ledgerLovers.end() && it->second.contains(to->GetFormID())) {
					return true;
				}
			}
			return false;
		}

		class LoversCallback final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			LoversCallback(RE::FormID a_base, std::uint32_t a_generation) : base(a_base), gen(a_generation) {}
			void operator()(RE::BSScript::Variable a_result) override
			{
				std::unordered_set<RE::FormID> found;
				if (a_result.IsArray()) {
					for (auto* form : a_result.Unpack<std::vector<RE::TESForm*>>()) {
						if (form) {
							found.insert(form->GetFormID());
						}
					}
				}
				std::scoped_lock lock{ mutex };
				if (gen != generation) {
					return;  // asked in a save that is no longer loaded
				}
				ledgerLovers[base].insert(found.begin(), found.end());
			}
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			RE::FormID    base;
			std::uint32_t gen;
		};

		// a single number answer (arousal, a last time) stored by a_store under the mutex, dropped if the save changed
		class FloatCallback final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			FloatCallback(std::function<void(float)> a_store, std::uint32_t a_generation) : store(std::move(a_store)), gen(a_generation) {}
			void operator()(RE::BSScript::Variable a_result) override
			{
				const float value = a_result.IsFloat() ? a_result.GetFloat() : a_result.IsInt() ? static_cast<float>(a_result.GetSInt()) : 0.0f;
				std::scoped_lock lock{ mutex };
				if (gen == generation) {
					store(value);
				}
			}
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::function<void(float)> store;
			std::uint32_t              gen;
		};

		struct PairAsk
		{
			RE::NiPointer<RE::Actor> x;
			RE::NiPointer<RE::Actor> y;
			std::uint64_t            key;
		};

		// OSL Aroused's answer to GetArousalMultiple: one value per actor, in the order they were passed; a_after (optional)
		// runs on the stored values (the scene log's "after" line)
		class ArousalCallback final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			ArousalCallback(std::vector<RE::FormID> a_refs, float a_day, std::uint32_t a_generation, std::function<void(const std::vector<float>&)> a_after = nullptr) :
				refs(std::move(a_refs)), day(a_day), gen(a_generation), after(std::move(a_after)) {}
			void operator()(RE::BSScript::Variable a_result) override
			{
				if (!a_result.IsArray()) {
					return;
				}
				const auto values = a_result.Unpack<std::vector<float>>();
				{
					std::scoped_lock lock{ mutex };
					if (gen != generation) {
						return;
					}
					for (std::size_t i = 0; i < refs.size() && i < values.size(); ++i) {
						arousal[refs[i]] = { values[i], day };
					}
				}
				if (after) {
					after(values);
				}
			}
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::vector<RE::FormID>                        refs;
			float                                          day;
			std::uint32_t                                  gen;
			std::function<void(const std::vector<float>&)> after;
		};

		RE::VMHandle SlaHandle()
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* dh = RE::TESDataHandler::GetSingleton();
			auto* quest = dh ? dh->LookupForm<RE::TESQuest>(kSlaFrameworkQuest, kSlaPlugin) : nullptr;
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			return quest && policy ? policy->GetHandleForObject(RE::TESQuest::FORMTYPE, quest) : policy ? policy->EmptyHandle() : 0;
		}

		// arousal for these actors from the installed mod (no mutex held): OSL Aroused in one batch call (it also brings each
		// value up to date for time passed, as OSL's own reads do), a SexLab Aroused-type mod one call per actor
		void AskArousal(const std::vector<RE::NiPointer<RE::Actor>>& a_actors, float a_today, std::uint32_t a_generation, std::function<void(const std::vector<float>&)> a_after = nullptr)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm || a_actors.empty()) {
				return;
			}
			if (ArousalSource() == Source::kOsl) {
				std::vector<RE::Actor*> actors;
				std::vector<RE::FormID> refs;
				for (const auto& a : a_actors) {
					actors.push_back(a.get());
					refs.push_back(a->GetFormID());
				}
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb{ new ArousalCallback(std::move(refs), a_today, a_generation, std::move(a_after)) };
				auto* args = RE::MakeFunctionArguments(std::move(actors));  // not freed: see AskLedger (one per scan)
				if (!vm->DispatchStaticCall("OSLArousedNative", "GetArousalMultiple", args, cb)) {
					static bool logged = false;
					if (!std::exchange(logged, true)) {
						SKSE::log::warn("Base: OSL Aroused's GetArousalMultiple could not be called - no couple passes the arousal check");
					}
				}
			} else if (ArousalSource() == Source::kSla) {
				const auto handle = SlaHandle();
				for (const auto& a : a_actors) {
					const auto ref = a->GetFormID();
					RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb{ new FloatCallback([ref, a_today](float a_v) {
						if (a_v >= 0.0f) {  // -2 = a child, -1 = unknown
							arousal[ref] = { a_v, a_today };
						}
					}, a_generation) };
					auto* args = RE::MakeFunctionArguments(a.get());
					if (!vm->DispatchMethodCall(handle, "slaFrameworkScr", "GetActorArousal", args, cb)) {
						static bool logged = false;
						if (!std::exchange(logged, true)) {
							SKSE::log::warn("Base: slaFrameworkScr.GetActorArousal could not be called - no couple passes the arousal check");
						}
						break;
					}
				}
			}
		}

		// Lover's Ledger's last time for new couples (the break) and, for Reforged's own desire, for new NPCs (no mutex held)
		void AskLastTimes(const std::vector<PairAsk>& a_pairs, const std::vector<RE::NiPointer<RE::Actor>>& a_npcs, std::uint32_t a_generation)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) {
				return;
			}
			for (const auto& p : a_pairs) {
				const auto key = p.key;
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb{ new FloatCallback([key](float a_v) {
					if (!lastTogether.contains(key)) {  // a scene end this session already knows better
						lastTogether[key] = a_v;
					}
				}, a_generation) };
				auto* args = RE::MakeFunctionArguments(p.x.get(), p.y.get(), RE::BSFixedString("lasttime"));
				if (!vm->DispatchStaticCall("TTLL_Store", "GetLoverFlt", args, cb)) {
					std::scoped_lock lock{ mutex };
					lastTogether.try_emplace(key, 0.0f);  // can't ask: treat as never together rather than wait forever
					if (!std::exchange(askFailLogged, true)) {
						SKSE::log::warn("Base: Lover's Ledger's GetLoverFlt could not be called - couple breaks only count this session's scenes");
					}
				}
			}
			for (const auto& a : a_npcs) {
				const auto ref = a->GetFormID();
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb{ new FloatCallback([ref](float a_v) { npcLastTime[ref] = a_v; }, a_generation) };
				auto* args = RE::MakeFunctionArguments(a.get(), RE::BSFixedString("lasttime"));
				vm->DispatchStaticCall("TTLL_Store", "GetNpcFlt", args, cb);
			}
		}

		// dispatched WITHOUT the mutex held: a callback that ever ran inside the call would otherwise lock it twice
		void AskLedger(const std::vector<std::pair<RE::NiPointer<RE::Actor>, RE::FormID>>& a_ask, std::uint32_t a_generation)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) {
				return;
			}
			for (const auto& [actor, base] : a_ask) {
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb{ new LoversCallback(base, a_generation) };
				// not freed here: the VM may still read the arguments after the dispatch returns (a few bytes per NPC per session)
				auto* args = RE::MakeFunctionArguments(actor.get(), static_cast<std::int32_t>(-1));
				vm->DispatchStaticCall("TTLL_Store", "GetAllLovers", args, cb);
			}
		}

		bool LedgerInstalled()
		{
			static const bool installed = [] {
				auto* dh = RE::TESDataHandler::GetSingleton();
				return dh && dh->LookupModByName(kLedgerPlugin) != nullptr;
			}();
			return installed;
		}

		bool SexAllowed(bool a_xFemale, bool a_yFemale, const Settings::Data& a_s)
		{
			if (a_xFemale != a_yFemale) {
				return a_s.weightMF > 0;
			}
			return a_xFemale ? a_s.weightFF > 0 : a_s.weightMM > 0;
		}

		// in an OStim scene right now (any scene, any mod's) - asked from OStim with our mutex NOT held; one call per nearby NPC
		// that passed the cheap checks (OStim's thread map can change under the call - keep these few)
		bool InSceneNow(RE::Actor* a_actor) { return ostimThreads && ostimThreads->IsActorInAnyThread(a_actor->GetFormID()); }

		// still on their short break after a scene (caller holds the mutex)
		bool OnBreak(RE::Actor* a_actor, float a_today, float a_break)
		{
			const auto it = lastSceneOf.find(a_actor->GetFormID());
			return it != lastSceneOf.end() && a_today - it->second < a_break;
		}

		// loaded, alive, adult NPCs near the player (sleeping is fine); Papyrus checks the rest (followers, period, privacy...)
		bool Near(RE::Actor* a_actor, RE::PlayerCharacter* a_player, float a_radius)
		{
			if (!a_actor || a_actor == a_player || !a_actor->Is3DLoaded() || a_actor->IsDead() || a_actor->IsDisabled() ||
				a_actor->IsChild() || a_actor->IsInCombat() || !a_actor->GetActorBase() || !a_actor->HasKeywordString("ActorTypeNPC")) {
				return false;
			}
			auto* cell = a_actor->GetParentCell();
			auto* playerCell = a_player->GetParentCell();
			if (!cell || !playerCell) {
				return false;
			}
			if (cell->IsInteriorCell() || playerCell->IsInteriorCell()) {
				if (cell != playerCell) {
					return false;  // another building: not the place you are in
				}
			} else if (a_actor->GetWorldspace() != a_player->GetWorldspace()) {
				return false;
			}
			return a_actor->GetPosition().GetDistance(a_player->GetPosition()) <= a_radius;
		}

		// an actor's arousal now, by source (caller holds the mutex): -1 = not known yet (asked this scan, known by the next)
		float ArousalNow(RE::Actor* a_actor, float a_today)
		{
			if (ArousalSource() == Source::kDesire) {
				return DesireOf(a_actor, a_today);
			}
			const auto it = arousal.find(a_actor->GetFormID());
			return it == arousal.end() || a_today - it->second.day > kArousalFresh ? -1.0f : it->second.value;
		}

		struct Candidate
		{
			RE::Actor* x;
			RE::Actor* y;
			Bond       bond;
			float      score;
			float      ax;
			float      ay;
		};

		// flat list [a1, b1, a2, b2, ...], best pair first, at most a_max pairs; an NPC may be in several (Papyrus takes
		// the first pair that passes its own checks, so a busy partner never hides that NPC's other pairs); a man first in
		// a man + woman pair
		std::vector<RE::Actor*> FindPairs(RE::StaticFunctionTag*, std::int32_t a_max)
		{
			std::vector<RE::Actor*> out;
			auto*                   player = RE::PlayerCharacter::GetSingleton();
			auto*                   lists = RE::ProcessLists::GetSingleton();
			if (!player || !lists || a_max <= 0 || !missingRequirements.empty()) {
				return out;
			}
			const auto  s = Settings::Get();
			const bool  useLedger = s.useLoversLedger && LedgerInstalled();
			const bool  desire = ArousalSource() == Source::kDesire;
			const float today = Today();
			const float breakDays = std::max(0.0f, s.coupleCooldownDays);
			const int   startState = std::clamp(s.startState, 0, 4);
			const int   partnerState = std::clamp(s.partnerState, 0, 4);

			std::vector<RE::NiPointer<RE::Actor>> nearby;
			for (auto& handle : lists->highActorHandles) {
				auto ptr = handle.get();
				if (Near(ptr.get(), player, s.pairRadius) && !InSceneNow(ptr.get())) {
					nearby.push_back(ptr);
				}
			}

			std::vector<Candidate>                                       found;
			std::vector<std::pair<RE::NiPointer<RE::Actor>, RE::FormID>> ask;
			std::vector<PairAsk>                                         askLast;
			std::vector<RE::NiPointer<RE::Actor>>                        askNpc;
			std::uint32_t                                                gen = 0;
			{
				std::scoped_lock lock{ mutex };
				gen = generation;
				MergeLoaded();  // 0.1.3: breaks from the co-save, if OnPostLoad has not run yet
				if (!refMapBuilt) {
					BuildRefMap();
				}
				const float now = Ladder::DaysNow();
				std::erase_if(nearby, [&](const RE::NiPointer<RE::Actor>& a_actor) { return OnBreak(a_actor.get(), today, breakDays); });
				for (auto& actor : nearby) {
					const auto base = actor->GetActorBase()->GetFormID();
					if (useLedger && ledgerAsked.insert(base).second) {
						ask.emplace_back(actor, base);
					}
					if (useLedger && desire && npcAsked.insert(actor->GetFormID()).second) {
						askNpc.push_back(actor);
					}
				}
				for (std::size_t i = 0; i < nearby.size(); ++i) {
					for (std::size_t j = i + 1; j < nearby.size(); ++j) {
						auto* x = nearby[i].get();
						auto* y = nearby[j].get();
						auto* xb = x->GetActorBase();
						auto* yb = y->GetActorBase();
						if (!SexAllowed(xb->IsFemale(), yb->IsFemale(), s)) {
							continue;
						}
						if (Ladder::IsMourning(xb->GetFormID(), now) || Ladder::IsMourning(yb->GetFormID(), now)) {
							continue;
						}
						// a past scene alone is history, not a relationship (exes, fallen out, the odd meeting - for the relationship
						// module to judge): only a spouse / courting record or Reforged's own couple stage makes a pair
						const Bond bond = BondOf(x, y);
						if (bond == Bond::kNone || bond == Bond::kFamily || bond == Bond::kEnded) {
							continue;
						}
						if (CommittedAbove(x, y, bond)) {
							continue;  // the base is faithful: nobody pairs past a stronger commitment elsewhere (0.1.5: courting too)
						}
						// a short break after their last scene together (Lover's Ledger, asked once per session; unknown = wait a scan)
						const auto key = PairKey(xb->GetFormID(), yb->GetFormID());
						if (breakDays > 0.0f) {
							auto last = lastTogether.find(key);
							if (last == lastTogether.end() && useLedger) {
								const auto asked = lastAsked.find(key);
								if (asked == lastAsked.end()) {
									lastAsked.emplace(key, today);
									askLast.push_back({ nearby[i], nearby[j], key });
									continue;  // known by the next scan
								}
								if (today - asked->second < kAskTimeout) {
									continue;  // still waiting for the answer
								}
								last = lastTogether.emplace(key, 0.0f).first;  // no answer: treat as never together
							}
							if (last != lastTogether.end() && last->second > 0.0f && today - last->second < breakDays) {
								continue;
							}
						}
						// arousal is what starts a scene: one of them in the start state, the other at least receptive
						const float ax = ArousalNow(x, today);
						const float ay = ArousalNow(y, today);
						if (ax < 0.0f || ay < 0.0f) {
							continue;
						}
						const int sx = StateOf(ax);
						const int sy = StateOf(ay);
						if (std::max(sx, sy) < startState || std::min(sx, sy) < partnerState) {
							continue;
						}
						const float history = useLedger && LedgerPair(xb, yb) ? 0.5f : 0.0f;  // tie-break only
						const float mood = (ax + ay) / 200.0f;                                  // the most aroused couple first
						found.push_back({ x, y, bond, Score(bond) + history + mood, ax, ay });
					}
				}
			}
			if (!ask.empty()) {
				AskLedger(ask, gen);
			}
			AskLastTimes(askLast, askNpc, gen);
			if (!desire) {
				AskArousal(nearby, today, gen);
			}

			static std::mt19937 rng{ std::random_device{}() };
			std::ranges::shuffle(found, rng);  // equal pairs in a fresh order every scan
			std::ranges::stable_sort(found, [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

			std::string summary;
			for (const auto& c : found) {
				if (static_cast<std::int32_t>(out.size() / 2) >= a_max) {
					break;
				}
				const bool swap = c.x->GetActorBase()->IsFemale() && !c.y->GetActorBase()->IsFemale();
				out.push_back(swap ? c.y : c.x);
				out.push_back(swap ? c.x : c.y);
				if (out.size() <= 8) {
					summary += std::format("{}{} ({} {:.0f}) + {} ({} {:.0f}), {}", summary.empty() ? "" : "; ", c.x->GetName(), kStateName[StateOf(c.ax)], c.ax,
						c.y->GetName(), kStateName[StateOf(c.ay)], c.ay, BondName(c.bond));
				}
			}
			if (!out.empty()) {
				SKSE::log::info("Base: {} NPC(s) near, {} pair(s) offered: {}{}", nearby.size(), out.size() / 2, summary, out.size() > 8 ? "; ..." : "");
			}
			return out;
		}

		// ---- the scene log (0.26.0): ONPCs_SceneLog.log next to the SKSE logs - how each live scene went, to
		// tune the arousal rules and, later, the simulated off-screen outcomes. No effect on play. ----
		std::mutex logMutex;

		struct OpenScene
		{
			RE::FormID                            x;
			RE::FormID                            y;
			float                                 startDay;
			std::chrono::steady_clock::time_point startReal;
		};
		std::unordered_map<std::int32_t, OpenScene> openScenes;  // OStim thread id -> scene (under mutex)

		void WriteLog(const std::string& a_text)
		{
			auto dir = SKSE::log::log_directory();
			if (!dir) {
				return;
			}
			std::scoped_lock lock{ logMutex };
			std::ofstream    file(*dir / "ONPCs_SceneLog.log", std::ios::app);
			if (file) {
				file << a_text << '\n';
			}
		}

		std::string When(float a_day)
		{
			auto*       cal = RE::Calendar::GetSingleton();
			const float hour = cal ? cal->GetHour() : 0.0f;
			const int   h = static_cast<int>(hour);
			const int   m = static_cast<int>((hour - static_cast<float>(h)) * 60.0f);
			return std::format("day {:.0f}, {:02}:{:02}", std::floor(a_day), h, m);
		}

		std::string Where(RE::Actor* a_actor)
		{
			auto*       cell = a_actor ? a_actor->GetParentCell() : nullptr;
			auto*       loc = a_actor ? a_actor->GetCurrentLocation() : nullptr;
			const char* cellName = cell ? cell->GetFullName() : nullptr;
			const char* locName = loc ? loc->GetFullName() : nullptr;
			std::string out = locName && *locName ? locName : "";
			if (cellName && *cellName && (!locName || std::strcmp(cellName, locName) != 0)) {
				out += out.empty() ? cellName : std::string(", ") + cellName;
			}
			return out.empty() ? "somewhere" : out;
		}

		std::string Person(RE::Actor* a_actor, float a_value)
		{
			const char* name = a_actor && a_actor->GetName() ? a_actor->GetName() : "?";
			return a_value >= 0.0f ? std::format("{} ({} {:.0f})", name, kStateName[StateOf(a_value)], a_value) : std::format("{} (arousal unknown)", name);
		}

		// OStim gave the couple's scene its thread id: the "start" line
		void SceneStarted(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y, std::int32_t a_threadID, RE::TESObjectREFR* a_furniture)
		{
			if (!a_x || !a_y || !a_x->GetActorBase() || !a_y->GetActorBase()) {
				return;
			}
			const float today = Today();
			float       ax = -1.0f;
			float       ay = -1.0f;
			Bond        bond = Bond::kNone;
			{
				std::scoped_lock lock{ mutex };
				ax = ArousalNow(a_x, today);
				ay = ArousalNow(a_y, today);
				bond = BondOf(a_x, a_y);
				openScenes[a_threadID] = { a_x->GetFormID(), a_y->GetFormID(), today, std::chrono::steady_clock::now() };
			}
			const char* furn = a_furniture && a_furniture->GetBaseObject() ? a_furniture->GetBaseObject()->GetName() : nullptr;
			WriteLog(std::format("\n[{}] {}\n  start: {} + {}, {}, {}", When(today), Where(a_x), Person(a_x, ax), Person(a_y, ay), BondName(bond),
				a_furniture ? (furn && *furn ? furn : "furniture") : "no furniture"));
		}

		// the "after" line: each partner's state once the arousal mod has applied its own after-scene change
		void WriteAfter(const std::string& a_xName, const std::string& a_yName, float a_ax, float a_ay, std::int32_t a_xClimax, std::int32_t a_yClimax)
		{
			auto feel = [](float a_v, std::int32_t a_climax) {
				if (a_climax <= 0) {
					return "Frustrated";
				}
				return a_v >= 0.0f && StateOf(a_v) >= 3 ? "Eager" : "Fulfilled";
			};
			WriteLog(std::format("  after: {} {} ({:.0f}), {} {} ({:.0f})", a_xName, feel(a_ax, a_xClimax), a_ax, a_yName, feel(a_ay, a_yClimax), a_ay));
		}

		// both answers of a SexLab Aroused-type "after" read: the second one to arrive writes the line (same save only)
		struct AfterPair
		{
			float            v[2]{ -1.0f, -1.0f };
			std::atomic<int> left{ 2 };
			std::string      xName;
			std::string      yName;
			std::int32_t     xClimax;
			std::int32_t     yClimax;
			std::uint32_t    gen;
		};

		class AfterCallback final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			AfterCallback(std::shared_ptr<AfterPair> a_pair, int a_index) : pair(std::move(a_pair)), index(a_index) {}
			void operator()(RE::BSScript::Variable a_result) override
			{
				pair->v[index] = a_result.IsInt() ? static_cast<float>(a_result.GetSInt()) : a_result.IsFloat() ? a_result.GetFloat() : -1.0f;
				if (pair->left.fetch_sub(1) == 1) {
					bool same = false;
					{
						std::scoped_lock lock{ mutex };
						same = pair->gen == generation;
					}
					if (same) {
						WriteAfter(pair->xName, pair->yName, pair->v[0], pair->v[1], pair->xClimax, pair->yClimax);
					}
				}
			}
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::shared_ptr<AfterPair> pair;
			int                        index;
		};

		// main thread, 8 s after a scene: each partner's arousal after the mod's own change, then the "after" line
		void ReadAfter(RE::FormID a_x, RE::FormID a_y, const std::string& a_xName, const std::string& a_yName, std::int32_t a_xClimax, std::int32_t a_yClimax, std::uint32_t a_gen)
		{
			RE::NiPointer<RE::Actor> x{ RE::TESForm::LookupByID<RE::Actor>(a_x) };
			RE::NiPointer<RE::Actor> y{ RE::TESForm::LookupByID<RE::Actor>(a_y) };
			if (!x || !y) {
				return;
			}
			if (ArousalSource() == Source::kOsl) {
				AskArousal({ x, y }, Today(), a_gen, [a_xName, a_yName, a_xClimax, a_yClimax](const std::vector<float>& a_v) {
					WriteAfter(a_xName, a_yName, a_v.size() > 0 ? a_v[0] : -1.0f, a_v.size() > 1 ? a_v[1] : -1.0f, a_xClimax, a_yClimax);
				});
				return;
			}
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) {
				return;
			}
			auto pair = std::make_shared<AfterPair>();
			pair->xName = a_xName;
			pair->yName = a_yName;
			pair->xClimax = a_xClimax;
			pair->yClimax = a_yClimax;
			pair->gen = a_gen;
			const auto handle = SlaHandle();
			int        i = 0;
			for (auto* a : { x.get(), y.get() }) {
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb{ new AfterCallback(pair, i++) };
				auto* args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(a));
				vm->DispatchMethodCall(handle, "slaFrameworkScr", "GetActorArousal", args, cb);
			}
		}

		// a scene between the two ended: past lovers from now on, the break starts, and (Reforged's own desire) whoever
		// climaxed is fulfilled; the "end" line, and the "after" line a few seconds later
		void SceneEnded(RE::StaticFunctionTag*, RE::Actor* a_x, RE::Actor* a_y, std::int32_t a_xClimax, std::int32_t a_yClimax, std::int32_t a_threadID, std::int32_t a_cause)
		{
			auto* xb = a_x ? a_x->GetActorBase() : nullptr;
			auto* yb = a_y ? a_y->GetActorBase() : nullptr;
			if (!xb || !yb) {
				return;
			}
			const float               today = Today();
			std::optional<OpenScene>  open;
			std::uint32_t             gen = 0;
			{
				std::scoped_lock lock{ mutex };
				gen = generation;
				ledgerLovers[xb->GetFormID()].insert(yb->GetFormID());
				ledgerLovers[yb->GetFormID()].insert(xb->GetFormID());
				lastTogether[PairKey(xb->GetFormID(), yb->GetFormID())] = today;  // the couple's break starts now
				lastSceneOf[a_x->GetFormID()] = today;                             // and each of theirs
				lastSceneOf[a_y->GetFormID()] = today;
				if (a_xClimax > 0) {
					lastFulfilled[a_x->GetFormID()] = today;  // a frustrating scene does not reset desire
				}
				if (a_yClimax > 0) {
					lastFulfilled[a_y->GetFormID()] = today;
				}
				if (const auto it = openScenes.find(a_threadID); it != openScenes.end()) {
					open = it->second;
					openScenes.erase(it);
				}
			}

			std::string length = "length unknown";
			if (open) {
				const auto realSec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - open->startReal).count();
				length = std::format("{:.0f} game min ({} s)", (today - open->startDay) * 24.0f * 60.0f, realSec);
			}
			const char* cause = a_cause == 1                       ? "stopped: they were seen" :
			                    a_cause == 2                       ? "stopped: someone else saw them" :
			                    (a_xClimax > 0 && a_yClimax > 0)   ? "both finished" :
			                    (a_xClimax > 0 || a_yClimax > 0)   ? "one finished, OStim ended it" :
			                                                         "ended without a climax (OStim's NPC timer or another mod)";
			const std::string xName = a_x->GetName() ? a_x->GetName() : "?";
			const std::string yName = a_y->GetName() ? a_y->GetName() : "?";
			WriteLog(std::format("  end:   {}, {} {} climax(es), {} {} - {}", length, xName, a_xClimax, yName, a_yClimax, cause));

			if (ArousalSource() == Source::kDesire) {
				float ax = 0.0f;
				float ay = 0.0f;
				{
					std::scoped_lock lock{ mutex };
					ax = DesireOf(a_x, today);
					ay = DesireOf(a_y, today);
				}
				WriteAfter(xName, yName, ax, ay, a_xClimax, a_yClimax);
				return;
			}
			// the arousal mod applies its own after-scene change in its scene-end handler (Papyrus, can lag on a heavy list): read
			// 8 s later. The background thread only waits and hands over FormIDs - the actors are looked up on the main thread
			const RE::FormID xId = a_x->GetFormID();
			const RE::FormID yId = a_y->GetFormID();
			std::thread([xId, yId, xName, yName, a_xClimax, a_yClimax, gen] {
				std::this_thread::sleep_for(std::chrono::seconds(8));
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([xId, yId, xName, yName, a_xClimax, a_yClimax, gen] { ReadAfter(xId, yId, xName, yName, a_xClimax, a_yClimax, gen); });
				}
			}).detach();
		}

		// ---- phase 3: group scenes (0.26.0) ----

		// loaded NPCs near the player who are not in a scene right now (no mutex held: asks OStim)
		std::vector<RE::NiPointer<RE::Actor>> GatherNearby(RE::PlayerCharacter* a_player, const Settings::Data& a_s)
		{
			std::vector<RE::NiPointer<RE::Actor>> nearby;
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return nearby;
			}
			for (auto& handle : lists->highActorHandles) {
				auto ptr = handle.get();
				if (Near(ptr.get(), a_player, a_s.pairRadius) && !InSceneNow(ptr.get())) {
					nearby.push_back(ptr);
				}
			}
			return nearby;
		}

		// a group: an NPC with two or more living partners near (an orc chief and his wives - the game's own records, or
		// Reforged's couples), everyone at least receptive, one of them in the start state, the partners getting along
		// with each other (no family, no bad blood). Hub first; empty = no group. Uses last scan's arousal values.
		std::vector<RE::Actor*> FindGroup(RE::StaticFunctionTag*, std::int32_t a_maxSize)
		{
			std::vector<RE::Actor*> out;
			auto*                   player = RE::PlayerCharacter::GetSingleton();
			const auto              s = Settings::Get();
			if (!player || !s.groupScenes || !missingRequirements.empty()) {
				return out;
			}
			const int   maxSize = std::clamp(std::min<int>(a_maxSize, s.maxGroupSize), 3, 5);
			const float today = Today();
			const float breakDays = std::max(0.0f, s.coupleCooldownDays);
			const int   startState = std::clamp(s.startState, 0, 4);
			const int   partnerState = std::clamp(s.partnerState, 0, 4);
			auto        nearby = GatherNearby(player, s);
			// asleep (or going to bed / getting up) = not in a group: the couple path wakes a sleeper gently, a group would
			// move someone out of the bed animation
			std::erase_if(nearby, [](const RE::NiPointer<RE::Actor>& a_actor) {
				auto*      state = a_actor->AsActorState();
				const auto ss = state ? state->GetSitSleepState() : RE::SIT_SLEEP_STATE::kNormal;
				return ss == RE::SIT_SLEEP_STATE::kWantToSleep || ss == RE::SIT_SLEEP_STATE::kWaitingForSleepAnim ||
				       ss == RE::SIT_SLEEP_STATE::kIsSleeping || ss == RE::SIT_SLEEP_STATE::kWantToWake;
			});
			std::uint32_t gen = 0;

			struct Member
			{
				RE::Actor* a;
				float      v;
			};
			std::vector<Member> best;
			float               bestScore = -1.0f;
			{
				std::scoped_lock lock{ mutex };
				gen = generation;
				MergeLoaded();  // 0.1.3: breaks from the co-save, if OnPostLoad has not run yet
				if (!refMapBuilt) {
					BuildRefMap();
				}
				const float now = Ladder::DaysNow();
				std::erase_if(nearby, [&](const RE::NiPointer<RE::Actor>& a_actor) { return OnBreak(a_actor.get(), today, breakDays); });
				for (auto& hubPtr : nearby) {
					auto*       hub = hubPtr.get();
					const float ah = ArousalNow(hub, today);
					if (ah < 0.0f || StateOf(ah) < partnerState || Ladder::IsMourning(hub->GetActorBase()->GetFormID(), now)) {
						continue;
					}
					std::vector<Member> partners;
					for (auto& mPtr : nearby) {
						auto* m = mPtr.get();
						if (m == hub || !SexAllowed(hub->GetActorBase()->IsFemale(), m->GetActorBase()->IsFemale(), s)) {
							continue;
						}
						const Bond b = BondOf(hub, m);
						if (b != Bond::kSpouse && b != Bond::kEngaged && b != Bond::kCourting) {
							continue;
						}
						if (CommittedAbove(hub, m, b)) {
							continue;  // 0.1.5: a married hub's outside courtship (or a partner married elsewhere) stays out of the group
						}
						const float am = ArousalNow(m, today);
						if (am < 0.0f || StateOf(am) < partnerState || Ladder::IsMourning(m->GetActorBase()->GetFormID(), now)) {
							continue;
						}
						bool getAlong = true;
						for (const auto& p : partners) {
							// partners share the scene too: the sex weights apply between them, and no family or bad blood
							// (the game's records and Reforged's own - exes, jilted, an affair found out)
							const Bond pb = BondOf(p.a, m);
							if (pb == Bond::kFamily || pb == Bond::kEnded || !SexAllowed(p.a->GetActorBase()->IsFemale(), m->GetActorBase()->IsFemale(), s)) {
								getAlong = false;
								break;
							}
						}
						if (getAlong) {
							partners.push_back({ m, am });
						}
					}
					if (partners.size() < 2) {
						continue;
					}
					std::ranges::sort(partners, [](const Member& a, const Member& b) { return a.v > b.v; });
					if (static_cast<int>(partners.size()) > maxSize - 1) {
						partners.resize(static_cast<std::size_t>(maxSize - 1));
					}
					if (StateOf(std::max(ah, partners.front().v)) < startState) {
						continue;  // someone has to be in the mood to start it
					}
					float total = ah;
					for (const auto& p : partners) {
						total += p.v;
					}
					const float score = total / static_cast<float>(partners.size() + 1);
					if (score > bestScore) {
						bestScore = score;
						best = { Member{ hub, ah } };
						best.insert(best.end(), partners.begin(), partners.end());
					}
				}
			}
			if (ArousalSource() != Source::kDesire) {
				AskArousal(nearby, today, gen);  // fresh values for the next scan (a group scan skips FindPairs)
			}
			std::string summary;
			for (const auto& m : best) {
				out.push_back(m.a);
				summary += std::format("{}{} ({} {:.0f})", summary.empty() ? "" : ", ", m.a->GetName(), kStateName[StateOf(m.v)], m.v);
			}
			if (!out.empty()) {
				SKSE::log::info("Base: group offered: {}", summary);
			}
			return out;
		}

		// the engine's own "walk to" (Actor.PathToReference, latent) started for this actor without waiting - several group
		// members walk at the same time; no package is built and nothing in the plugin changes
		bool WalkTo(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::TESObjectREFR* a_target)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!a_actor || !a_target || !policy) {
				return false;
			}
			const auto handle = policy->GetHandleForObject(RE::Actor::FORMTYPE, a_actor);
			// 0.1.4: the method call needs the actor's script object; one is made and bound if the VM has none yet (as CommonLib
			// does when it hands a form to Papyrus)
			RE::BSTSmartPointer<RE::BSScript::Object> object;
			if (!vm->FindBoundObject(handle, "Actor", object)) {
				auto* bind = vm->GetObjectBindPolicy();
				if (vm->CreateObject("Actor", object) && object && bind) {
					bind->BindObject(object, handle);
				}
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
			auto* args = RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(a_target), 1.0f);
			const bool ok = vm->DispatchMethodCall(handle, "Actor", "PathToReference", args, cb);
			if (!ok) {
				SKSE::log::warn("Base: walk for {} could not start (PathToReference dispatch failed)", a_actor->GetName() ? a_actor->GetName() : "?");
			}
			return ok;
		}

		// ---- privacy (0.27.2, user 3 Nov): couples don't want to be seen; within earshot they keep quiet -------------------
		// a witness = an awake person (not in the scene, not the player - "stop when seen" handles you) near the spot; children
		// count like adults. Seen = a clear line of sight from them to anyone in the scene or to the bed, at any distance (only
		// actors the game runs near you are looked at). Heard = within the earshot setting, sight or not (walls don't stop it).
		// not a witness: asleep, knocked out, bleeding out, in a kill move, a mannequin or anything without AI, your own
		// followers (they stand next to you - "stop when found" decides for you and them), someone in an OStim scene
		bool NotAWitness(RE::Actor* a_actor)
		{
			auto*      state = a_actor->AsActorState();
			const auto ss = state ? state->GetSitSleepState() : RE::SIT_SLEEP_STATE::kNormal;
			if (ss == RE::SIT_SLEEP_STATE::kIsSleeping || ss == RE::SIT_SLEEP_STATE::kWaitingForSleepAnim) {
				return true;
			}
			if ((state && (state->IsUnconscious() || state->IsBleedingOut())) || a_actor->IsInKillMove() || !a_actor->IsAIEnabled() ||
				a_actor->IsPlayerTeammate()) {
				return true;
			}
			if (auto* race = a_actor->GetRace(); race && race->GetFormEditorID() && std::string_view(race->GetFormEditorID()) == "ManakinRace") {
				return true;
			}
			return InSceneNow(a_actor);
		}

		constexpr float kOutdoorSight = 4000.0f;   // units (~57 m): beyond this nobody outdoors counts as watching
		constexpr float kNoPrivacyWait = 0.02f;    // game days (~30 min): a couple that gave up for lack of privacy waits this long
		constexpr std::size_t kMaxBedsLooked = 8;  // the nearest free beds checked for privacy (line-of-sight checks are not free)

		// 0 nobody, 1 someone within earshot, 2 someone can see; a_who = that person
		int Witness(const std::vector<RE::TESObjectREFR*>& a_spots, const std::vector<RE::Actor*>& a_exclude, RE::Actor** a_who)
		{
			auto* lists = RE::ProcessLists::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!lists || a_spots.empty()) {
				return 0;
			}
			const float earshot = std::max(0.0f, Settings::Get().earshot);
			int         result = 0;
			for (auto& h : lists->highActorHandles) {
				auto  ptr = h.get();
				auto* a = ptr.get();
				if (!a || a == player || a->IsDead() || a->IsDisabled() || !a->Is3DLoaded() || !a->HasKeywordString("ActorTypeNPC") ||
					std::ranges::find(a_exclude, a) != a_exclude.end() || NotAWitness(a)) {
					continue;
				}
				for (auto* spot : a_spots) {
					if (!spot) {
						continue;
					}
					auto*      cell = spot->GetParentCell();
					const bool indoors = cell && cell->IsInteriorCell();
					if (indoors && cell != a->GetParentCell()) {
						continue;  // indoors: only people in the same building
					}
					if (!indoors && a->GetPosition().GetDistance(spot->GetPosition()) > kOutdoorSight) {
						continue;  // outdoors: too far to make anything out
					}
					bool unused = false;
					if (a->HasLineOfSight(spot, unused)) {
						if (a_who) {
							*a_who = a;
						}
						return 2;
					}
					if (result == 0 && earshot > 0.0f && a->GetPosition().GetDistance(spot->GetPosition()) <= earshot) {
						result = 1;
						if (a_who) {
							*a_who = a;
						}
					}
				}
			}
			return result;
		}

		// Papyrus: 0 private, 1 overheard (keep quiet), 2 seen (stop / don't start)
		std::int32_t Privacy(RE::StaticFunctionTag*, std::vector<RE::Actor*> a_actors)
		{
			std::erase(a_actors, nullptr);
			std::vector<RE::TESObjectREFR*> spots(a_actors.begin(), a_actors.end());
			return Witness(spots, a_actors, nullptr);
		}

		// Papyrus: the name of whoever made Privacy() answer 1 or 2 (for the logs), "" if nobody
		RE::BSFixedString Witnesser(RE::StaticFunctionTag*, std::vector<RE::Actor*> a_actors)
		{
			std::erase(a_actors, nullptr);
			std::vector<RE::TESObjectREFR*> spots(a_actors.begin(), a_actors.end());
			RE::Actor* who = nullptr;
			Witness(spots, a_actors, &who);
			return who && who->GetName() ? who->GetName() : "";
		}

		// Papyrus: the best free bed for them - nobody may see it; their own (or a partner's, or their home's) beats a
		// stranger's, out of earshot beats having to keep quiet. None = no private bed.
		RE::TESObjectREFR* PickPrivateBed(RE::StaticFunctionTag*, std::vector<RE::Actor*> a_actors, std::vector<RE::TESObjectREFR*> a_beds)
		{
			std::erase(a_actors, nullptr);
			std::erase(a_beds, nullptr);
			if (a_actors.empty()) {
				return nullptr;
			}
			// the nearest few only
			const auto from = a_actors.front()->GetPosition();
			std::ranges::sort(a_beds, [&](auto* a, auto* b) { return a->GetPosition().GetDistance(from) < b->GetPosition().GetDistance(from); });
			if (a_beds.size() > kMaxBedsLooked) {
				a_beds.resize(kMaxBedsLooked);
			}
			RE::TESObjectREFR* best = nullptr;
			int                bestScore = -1;
			for (auto* bed : a_beds) {
				if (!bed) {
					continue;
				}
				const int w = Witness({ bed }, a_actors, nullptr);
				if (w == 2) {
					continue;
				}
				bool own = false;
				for (auto* m : a_actors) {
					if (bed->IsAnOwner(m, true, true)) {
						own = true;
						break;
					}
					if (auto* cell = bed->GetParentCell(); cell && cell->IsInteriorCell()) {
						auto* owner = cell->GetOwner();
						if (owner && (owner == m->GetActorBase() || (owner->Is(RE::FormType::Faction) && m->IsInFaction(owner->As<RE::TESFaction>())))) {
							own = true;
							break;
						}
					}
				}
				const int score = (own ? 2 : 0) + (w == 0 ? 1 : 0);
				if (score > bestScore) {
					bestScore = score;
					best = bed;
				}
			}
			if (best) {
				SKSE::log::info("Base: private bed {:08X} ({}{})", best->GetFormID(), bestScore >= 2 ? "their own" : "not theirs",
					bestScore % 2 ? ", out of earshot" : ", must keep quiet");
			}
			return best;
		}

		// Papyrus: they walked to the spot and someone could see them - they give up for now; nobody here starts again for
		// about half an hour (uses the same break the base keeps after a scene)
		void NoPrivacy(RE::StaticFunctionTag*, std::vector<RE::Actor*> a_actors)
		{
			const float today = Today();
			const float breakDays = std::max(0.0f, Settings::Get().coupleCooldownDays);
			std::scoped_lock lock{ mutex };
			for (auto* a : a_actors) {
				if (a) {
					lastSceneOf[a->GetFormID()] = today - breakDays + kNoPrivacyWait;
				}
			}
		}

		struct OpenGroup
		{
			std::vector<RE::FormID>               ids;
			float                                 startDay;
			std::chrono::steady_clock::time_point startReal;
		};
		std::unordered_map<std::int32_t, OpenGroup> openGroups;  // OStim thread id -> group (under mutex)

		void GroupSceneStarted(RE::StaticFunctionTag*, std::vector<RE::Actor*> a_actors, std::int32_t a_threadID, RE::TESObjectREFR* a_furniture)
		{
			std::erase(a_actors, nullptr);
			if (a_actors.size() < 2) {
				return;
			}
			const float today = Today();
			std::string who;
			{
				std::scoped_lock lock{ mutex };
				OpenGroup g{ {}, today, std::chrono::steady_clock::now() };
				for (auto* a : a_actors) {
					g.ids.push_back(a->GetFormID());
					who += std::format("{}{}", who.empty() ? "" : " + ", Person(a, ArousalNow(a, today)));
				}
				openGroups[a_threadID] = std::move(g);
			}
			const char* furn = a_furniture && a_furniture->GetBaseObject() ? a_furniture->GetBaseObject()->GetName() : nullptr;
			WriteLog(std::format("\n[{}] {}\n  start (group of {}): {}, {}", When(today), Where(a_actors.front()), a_actors.size(), who,
				a_furniture ? (furn && *furn ? furn : "furniture") : "no furniture"));
		}

		void GroupSceneEnded(RE::StaticFunctionTag*, std::vector<RE::Actor*> a_actors, std::vector<std::int32_t> a_climaxes, std::int32_t a_threadID, std::int32_t a_cause)
		{
			std::vector<std::pair<RE::Actor*, std::int32_t>> members;
			for (std::size_t i = 0; i < a_actors.size(); ++i) {
				if (a_actors[i] && a_actors[i]->GetActorBase()) {
					members.emplace_back(a_actors[i], i < a_climaxes.size() ? a_climaxes[i] : 0);
				}
			}
			if (members.size() < 2) {
				return;
			}
			const float               today = Today();
			std::optional<OpenGroup>  open;
			std::string               after;
			int                       finished = 0;
			{
				std::scoped_lock lock{ mutex };
				for (std::size_t i = 0; i < members.size(); ++i) {
					auto* a = members[i].first;
					lastSceneOf[a->GetFormID()] = today;  // everyone's break starts
					if (members[i].second > 0) {
						lastFulfilled[a->GetFormID()] = today;
						++finished;
					}
					for (std::size_t j = i + 1; j < members.size(); ++j) {
						lastTogether[PairKey(a->GetActorBase()->GetFormID(), members[j].first->GetActorBase()->GetFormID())] = today;
					}
				}
				if (const auto it = openGroups.find(a_threadID); it != openGroups.end()) {
					open = it->second;
					openGroups.erase(it);
				}
				if (ArousalSource() == Source::kDesire) {
					for (const auto& [a, c] : members) {
						const float v = DesireOf(a, today);
						after += std::format("{}{} {} ({:.0f})", after.empty() ? "" : ", ", a->GetName(), c <= 0 ? "Frustrated" : StateOf(v) >= 3 ? "Eager" : "Fulfilled", v);
					}
				}
			}
			std::string length = "length unknown";
			if (open) {
				const auto realSec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - open->startReal).count();
				length = std::format("{:.0f} game min ({} s)", (today - open->startDay) * 24.0f * 60.0f, realSec);
			}
			std::string counts;
			for (const auto& [a, c] : members) {
				counts += std::format("{}{} {}", counts.empty() ? "" : ", ", a->GetName(), c);
			}
			const char* cause = a_cause == 1 ? "stopped: they were seen" : a_cause == 2 ? "stopped: someone else saw them" : finished == static_cast<int>(members.size()) ? "everyone finished" :
			                    finished > 0                                                                    ? "some finished, OStim ended it" :
			                                                                                                      "ended without a climax (OStim's NPC timer or another mod)";
			WriteLog(std::format("  end:   {}, climaxes: {} - {}", length, counts, cause));
			if (!after.empty()) {
				WriteLog(std::format("  after: {}", after));
			}
		}

		// a scene slot's OStim thread is still running WITH the slot's own NPC in it (OStim reuses thread IDs after a load, so an
		// ID alone could be someone else's scene); unknown without OStim's API = assume yes, so nothing is freed by mistake
		bool IsSceneRunning(RE::StaticFunctionTag*, std::int32_t a_threadID, RE::Actor* a_dom)
		{
			if (a_threadID < 0) {
				return false;
			}
			if (!ostimThreads) {
				return true;
			}
			const auto id = static_cast<std::uint32_t>(a_threadID);
			return ostimThreads->IsThreadValid(id) && (!a_dom || ostimThreads->GetActorPosition(id, a_dom->GetFormID()) >= 0);
		}

		bool Bind(RE::BSScript::IVirtualMachine* a_vm)
		{
			a_vm->RegisterFunction("FindPairs", kScript, FindPairs);
			a_vm->RegisterFunction("SceneStarted", kScript, SceneStarted);
			a_vm->RegisterFunction("SceneEnded", kScript, SceneEnded);
			a_vm->RegisterFunction("IsSceneRunning", kScript, IsSceneRunning);
			a_vm->RegisterFunction("FindGroup", kScript, FindGroup);
			a_vm->RegisterFunction("WalkTo", kScript, WalkTo);
			a_vm->RegisterFunction("GroupSceneStarted", kScript, GroupSceneStarted);
			a_vm->RegisterFunction("GroupSceneEnded", kScript, GroupSceneEnded);
			a_vm->RegisterFunction("Privacy", kScript, Privacy);
			a_vm->RegisterFunction("Witnesser", kScript, Witnesser);
			a_vm->RegisterFunction("PickPrivateBed", kScript, PickPrivateBed);
			a_vm->RegisterFunction("NoPrivacy", kScript, NoPrivacy);
			return true;
		}
	}

	void Register()
	{
		auto* papyrus = SKSE::GetPapyrusInterface();
		if (!papyrus || !papyrus->Register(Bind)) {
			SKSE::log::error("Base: Papyrus interface unavailable - ONPCR_Base natives not registered.");
		}
	}

	void OnDataLoaded()
	{
		// the interface's later functions (IsActorInAnyThread, GetActorPosition) exist from 7.5.1 on: an older OStim.dll that still
		// answers "V1" has a shorter table, and calling them would crash - so check the file version first
		wchar_t path[MAX_PATH]{};
		const auto module = GetModuleHandleW(L"OStim.dll");
		const auto version = module && GetModuleFileNameW(module, path, MAX_PATH) ? REL::GetFileVersion(path) : std::nullopt;
		if (!version || *version < REL::Version{ 7, 5, 1, 0 }) {
			SKSE::log::warn("Base: OStim.dll {} - the thread API needs 7.5.1 or newer; busy NPCs are left to the Papyrus checks",
				version ? version->string(".") : std::string("not found"));
		} else {
			ostimThreads = OstimNG_API::Thread::GetAPI("ONPCs", Plugin::VERSION);
			SKSE::log::info("Base: OStim {} thread API {}", version->string("."), ostimThreads ? "connected" : "not available");
		}
		(void)ArousalSource();  // logs which arousal source is used

		// 0.1.0-alpha: hard requirements (user 3 Nov: the base must not run without Relations Finder and Lover's Ledger)
		std::string missing;
		const auto  need = [&](const wchar_t* a_dll, const char* a_name) {
			if (!GetModuleHandleW(a_dll)) {
				missing += missing.empty() ? a_name : std::string(", ") + a_name;
			}
		};
		if (!version || *version < REL::Version{ 7, 5, 1, 0 }) {
			missing = "OStim Standalone 7.5.1+";
		} else if (!ostimThreads) {
			missing = "OStim's thread API";  // 0.1.4: without it a stuck scene slot could never be freed
		}
		need(L"RelationsFinder.dll", "Relations Finder");
		need(L"LoversLedger.dll", "Lover's Ledger");
		need(L"PapyrusUtil.dll", "PapyrusUtil");
		need(L"JContainers64.dll", "JContainers");
		missingRequirements = missing;
		if (missing.empty()) {
			SKSE::log::info("Base: every requirement found");
		} else {
			SKSE::log::error("Base: missing {} - no NPC scenes will start", missing);
		}
	}

	// ---- 0.1.3: the breaks survive a save / load (co-save records in ONPCs' own co-save, written from Ladder's callbacks) ----
	namespace
	{
		constexpr std::uint32_t kRecScene = 'BASS';      // each NPC's last scene (or "gave up for lack of privacy") - their break
		constexpr std::uint32_t kRecTogether = 'BAST';   // each couple's last scene together
		constexpr std::uint32_t kRecFulfilled = 'BASF';  // Reforged's own desire: each NPC's last fulfilling scene
		constexpr std::uint32_t kRecVersion = 1;
		constexpr float         kKeepDays = 10.0f;  // older values no longer change anything (breaks are hours, desire tops out in days)

		bool ReadRaw(SKSE::SerializationInterface* a_intfc, void* a_out, std::uint32_t a_size) { return a_intfc->ReadRecordData(a_out, a_size) == a_size; }

		void WriteRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, const std::vector<std::pair<RE::FormID, float>>& a_rows)
		{
			if (!a_intfc->OpenRecord(a_type, kRecVersion)) {
				return;
			}
			const auto n = static_cast<std::uint32_t>(a_rows.size());
			a_intfc->WriteRecordData(&n, sizeof(n));
			for (const auto& [id, day] : a_rows) {
				a_intfc->WriteRecordData(&id, sizeof(id));
				a_intfc->WriteRecordData(&day, sizeof(day));
			}
		}
	}

	void Save(SKSE::SerializationInterface* a_intfc)
	{
		std::vector<std::pair<RE::FormID, float>> scene;
		std::vector<std::pair<RE::FormID, float>> fulfilled;
		std::vector<std::pair<std::uint64_t, float>> together;
		const float keep = Today() - kKeepDays;
		{
			std::scoped_lock lock{ mutex };
			MergeLoaded();
			for (const auto& [id, day] : lastSceneOf) {
				if (day > keep) {
					scene.emplace_back(id, day);
				}
			}
			for (const auto& [id, day] : lastFulfilled) {
				if (day > keep) {
					fulfilled.emplace_back(id, day);
				}
			}
			for (const auto& [key, day] : lastTogether) {
				if (day > 0.0f && day > keep) {  // "never" (0) is only a cache: asked again from Lover's Ledger
					together.emplace_back(key, day);
				}
			}
		}
		WriteRecord(a_intfc, kRecScene, scene);
		WriteRecord(a_intfc, kRecFulfilled, fulfilled);
		if (a_intfc->OpenRecord(kRecTogether, kRecVersion)) {
			const auto n = static_cast<std::uint32_t>(together.size());
			a_intfc->WriteRecordData(&n, sizeof(n));
			for (const auto& [key, day] : together) {
				const auto lo = static_cast<RE::FormID>(key >> 32);
				const auto hi = static_cast<RE::FormID>(key & 0xFFFFFFFF);
				a_intfc->WriteRecordData(&lo, sizeof(lo));
				a_intfc->WriteRecordData(&hi, sizeof(hi));
				a_intfc->WriteRecordData(&day, sizeof(day));
			}
		}
	}

	bool LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version)
	{
		if (a_type != kRecScene && a_type != kRecTogether && a_type != kRecFulfilled) {
			return false;
		}
		std::uint32_t n = 0;
		if (a_version != kRecVersion || !ReadRaw(a_intfc, &n, sizeof(n))) {
			return true;  // ours, but a layout this build doesn't know: skipped
		}
		std::scoped_lock lock{ loadMutex };
		loaded.any = true;
		for (std::uint32_t i = 0; i < n; ++i) {
			if (a_type == kRecTogether) {
				RE::FormID lo = 0, hi = 0, rlo = 0, rhi = 0;
				float      day = 0.0f;
				if (!ReadRaw(a_intfc, &lo, sizeof(lo)) || !ReadRaw(a_intfc, &hi, sizeof(hi)) || !ReadRaw(a_intfc, &day, sizeof(day))) {
					break;
				}
				if (a_intfc->ResolveFormID(lo, rlo) && a_intfc->ResolveFormID(hi, rhi)) {
					loaded.together.emplace_back(PairKey(rlo, rhi), day);
				}
				continue;
			}
			RE::FormID id = 0, resolved = 0;
			float      day = 0.0f;
			if (!ReadRaw(a_intfc, &id, sizeof(id)) || !ReadRaw(a_intfc, &day, sizeof(day))) {
				break;
			}
			if (a_intfc->ResolveFormID(id, resolved)) {
				(a_type == kRecScene ? loaded.scene : loaded.fulfilled).emplace_back(resolved, day);
			}
		}
		SKSE::log::info("Base: {} break record(s) loaded ({:c}{:c}{:c}{:c})", n, static_cast<char>(a_type >> 24), static_cast<char>(a_type >> 16),
			static_cast<char>(a_type >> 8), static_cast<char>(a_type));
		return true;
	}

	void OnPostLoad()
	{
		{
			std::scoped_lock lock{ mutex };
			MergeLoaded();
			if (!refMapBuilt) {
				BuildRefMap();
			}
		}
		if (!missingRequirements.empty()) {
			RE::SendHUDMessage::ShowHUDMessage(std::format("ONPCs: missing {} - NPC scenes are off.", missingRequirements).c_str());
		}
	}

	void Reset()
	{
		std::scoped_lock lock{ mutex };
		++generation;
		ledgerLovers.clear();
		ledgerAsked.clear();
		arousal.clear();
		askFailLogged = false;
		lastSceneOf.clear();
		lastTogether.clear();
		lastAsked.clear();
		lastFulfilled.clear();
		npcLastTime.clear();
		npcAsked.clear();
		openScenes.clear();
		openGroups.clear();
		refOfBase.clear();
		refMapBuilt = false;
		widowedLogged.clear();
		std::scoped_lock load{ loadMutex };
		loaded = {};
	}
}
