#include "Menu.h"

#include "Actions.h"
#include "Babies.h"
#include "Ladder.h"
#include "Papyrus.h"
#include "Settings.h"
#include "Simulation.h"

#pragma warning(push)
#pragma warning(disable : 4099 4244 4267 5054)
#include "SKSEMenuFramework.h"
#pragma warning(pop)

namespace ONPCR::Menu
{
	namespace
	{
		namespace Im = ImGuiMCP;
		namespace MF = SKSEMenuFramework;

		Settings::Data draft;
		bool           haveDraft{ false };
		char           couplesFilter[64]{};
		int            staleDays{ 30 };
		bool           staleIncludeEnded{ false };
		std::string    staleResult;
		std::string    tickResult;  // what the last "Simulate one more day" changed
		std::string    importResult;  // what the last "Import the game's couples" press did
		std::string    freshResult;  // "Start fresh" outcome (Advanced page)
		char           presetName[48]{};  // "My presets" name box
		std::string    presetResult;
		std::string    PersonName(RE::Actor* a_actor);  // defined below: real name, plus a quest's rename
		// Create a couple
		char           pickSearch[2][64]{};
		RE::FormID     pickBase[2]{ 0, 0 };
		std::string    pickName[2];
		int            pickStage{ 2 };  // 1 casual, 2 dating, 3 engaged, 4 married
		bool           pickKeep{ false };
		std::string    pickResult;
		int            stageFilter{ 6 };  // 0 all, 1 seeing, 2 dating, 3 engaged, 4 married, 5 past, 6 current (1-4, the default), 7 side (affairs, crushes, thralls)

		// testing helper: put the player next to an NPC (game thread)
		void GoTo(RE::FormID a_base)
		{
			const RE::FormID ref = Ladder::RefOfBase(a_base);
			if (!ref) {
				pickResult = "Not known where that person is yet (they need to be in the simulation pool).";
				return;
			}
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([ref] {
					auto* target = RE::TESForm::LookupByID<RE::TESObjectREFR>(ref);
					auto* player = RE::PlayerCharacter::GetSingleton();
					if (target && player) {
						player->MoveTo(target);
					}
				});
			}
			pickResult = "Moving you there - close the menu.";
		}

		void PickFromCrosshair(int a_slot)
		{
			auto* pick = RE::CrosshairPickData::GetSingleton();
			auto  ref = pick ? pick->GetActiveTarget().get() : RE::NiPointer<RE::TESObjectREFR>();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			auto* base = actor ? actor->GetActorBase() : nullptr;
			if (!actor || !base) {
				pickResult = "Nobody under the crosshair - look at an NPC, then open this page.";
				return;
			}
			if (actor->IsChild() || !Ladder::IsHumanoid(actor)) {
				pickResult = std::format("{} can't be part of a couple (children and creatures never are).", PersonName(actor));
				return;
			}
			if (Ladder::IsReservedFollower(actor)) {
				pickResult = std::format("{} is a custom follower - allow them on the Custom followers page first.", PersonName(actor));
				return;
			}
			pickBase[a_slot] = base->GetFormID();
			pickName[a_slot] = PersonName(actor);
			Ladder::RememberRef(base->GetFormID(), actor->GetFormID());
			pickResult.clear();
		}

		void PickerSlot(int a_slot)
		{
			Im::PushID(100 + a_slot);
			Im::TextUnformatted(std::format("Person {}: {}", a_slot == 0 ? "A" : "B", pickBase[a_slot] ? pickName[a_slot] : std::string("(none)")).c_str());
			Im::SameLine();
			if (Im::Button("Use crosshair")) {
				PickFromCrosshair(a_slot);
			}
			Im::SameLine();
			Im::SetNextItemWidth(Im::GetFontSize() * 14.0f);
			Im::InputText("Search name", pickSearch[a_slot], sizeof(pickSearch[a_slot]));
			const auto found = Simulation::FindPeople(pickSearch[a_slot], 8);
			for (std::size_t i = 0; i < found.size(); ++i) {
				Im::PushID(static_cast<int>(i));
				if (i > 0) {
					Im::SameLine();
				}
				if (Im::Button(found[i].name.c_str())) {
					pickBase[a_slot] = found[i].base;
					pickName[a_slot] = found[i].name;
					Ladder::RememberRef(found[i].base, found[i].ref);
					pickSearch[a_slot][0] = '\0';
				}
				Im::PopID();
			}
			Im::PopID();
		}

		void Hint(const char* a_text);

		// the person's real name, with what the game shows now when a quest has renamed them (Kaidan is "Shackled Prisoner" until
		// you free him) - so the same person is recognisable
		std::string PersonName(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return "?";
			}
			const std::string shown = a_actor->GetDisplayFullName() ? a_actor->GetDisplayFullName() : "";
			auto*             base = a_actor->GetActorBase();
			std::string       real = base && base->GetName() ? base->GetName() : "";
			// some quests rename the person record itself (SetName), so even the record says the quest name - known cases here
			struct Rename
			{
				RE::FormID       local;
				const char*      file;
				const char*      name;
			};
			static constexpr Rename kRenames[] = {
				{ 0x002F9A, "0Kaidan.esp", "Kaidan" },  // Immersive Kaidan calls him "Shackled Prisoner" until his quest starts
			};
			if (auto* dh = RE::TESDataHandler::GetSingleton(); dh && base) {
				for (const auto& r : kRenames) {
					if (dh->LookupFormID(r.local, r.file) == base->GetFormID()) {
						real = r.name;
					}
				}
			}
			if (real.empty() || real == shown) {
				return shown.empty() ? real : shown;
			}
			return shown.empty() ? real : real + " (now: " + shown + ")";
		}

		constexpr const char* kClosenessHelp =
			"Closeness (0-100) is how strong a couple's bond is right now. Every meeting adds to it (Closeness per encounter, Relationships page); every day apart takes a little away (Drift per day, Off-screen world page) - slower for couples with children, paused while one of them travels with you or is expecting; a scene with someone else takes a big bite. "
			"If it runs out: a casual couple splits, a dating couple drifts apart, an engagement cools back to dating, and a marriage goes cold (they can divorce later if someone strayed and they have no children). A cold marriage warms up again once closeness is back to 50. "
			"Moving up a stage is decided by how often they meet and for how long, not by closeness. Couples that never break up still lose closeness, they just never split.";

		// Custom followers: mod-added followers stay the player's own unless allowed here (people only, custom races included)
		std::string followerResult;

		void SetFollowerAllowed(RE::Actor* a_actor, bool a_allow)
		{
			auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
			if (!base) {
				return;
			}
			if (a_allow && !Ladder::IsHumanoid(a_actor)) {
				followerResult = std::format("{} is not a person (creature or animal) - only people can join.", PersonName(a_actor));
				return;
			}
			Ladder::RememberRef(base->GetFormID(), a_actor->GetFormID());
			Ladder::AllowFollower(base->GetFormID(), a_allow);
			Simulation::RequestRebuild();
			const std::string name = PersonName(a_actor);
			const bool kids = Ladder::CanHaveChildrenAlone(a_actor);
			followerResult = a_allow ? name + (kids ? " can now date and marry like anyone else (children: see the Children column)." : " can now date and marry like anyone else - but no children (Children column).") :
			                           name + " is yours alone again.";
		}

		// Followers page: a table - name, which mod, where they stand, dating on/off, children - travelling with you first
		std::vector<RE::FormID> followerList;  // custom followers (actor refs), sorted for display
		char                    followerFilter[64]{};

		void ScanCustomFollowers()
		{
			struct Row
			{
				RE::FormID  ref;
				int         rank;  // 0 travelling with you, 1 allowed, 2 the rest
				std::string name;
			};
			std::vector<Row>               rows;
			std::unordered_set<RE::FormID> seenBases;
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			{
				const RE::BSReadLockGuard l{ lock };
				if (map) {
					for (auto& entry : *map) {
						auto* actor = entry.second ? entry.second->As<RE::Actor>() : nullptr;
						auto* base = actor ? actor->GetActorBase() : nullptr;
						if (!base || actor->IsDeleted() || actor->IsPlayerRef() || !seenBases.insert(base->GetFormID()).second) {
							continue;
						}
						if (!Ladder::IsCustomFollower(actor) || !Ladder::IsHumanoid(actor)) {
							continue;
						}
						const int rank = actor->IsPlayerTeammate() ? 0 : (Ladder::FollowerAllowed(base->GetFormID()) ? 1 : 2);
						rows.push_back({ actor->GetFormID(), rank, PersonName(actor) });
					}
				}
			}
			std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.rank != b.rank ? a.rank < b.rank : a.name < b.name; });
			followerList.clear();
			for (const auto& r : rows) {
				followerList.push_back(r.ref);
			}
			Papyrus::SendFertilityCheck(followerList);  // the Children column shows what the pregnancy mod says
		}

		void FollowersTable(const std::string& a_needle)
		{
			const Im::ImGuiTableFlags flags = Im::ImGuiTableFlags_Borders | Im::ImGuiTableFlags_RowBg | Im::ImGuiTableFlags_SizingStretchProp;
			if (!Im::BeginTable("followers", 5, flags)) {
				return;
			}
			Im::TableSetupColumn("Name", 0, 1.6f);
			Im::TableSetupColumn("From mod", 0, 1.8f);
			Im::TableSetupColumn("Now", 0, 1.2f);
			Im::TableSetupColumn("Dating and marriage", Im::ImGuiTableColumnFlags_WidthFixed, Im::GetFontSize() * 9.0f);
			Im::TableSetupColumn("Children", Im::ImGuiTableColumnFlags_WidthFixed, Im::GetFontSize() * 11.0f);
			Im::TableHeadersRow();
			int shown = 0;
			for (const auto ref : followerList) {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(ref);
				auto* base = actor ? actor->GetActorBase() : nullptr;
				if (!base) {
					continue;
				}
				const std::string name = PersonName(actor);
				if (!a_needle.empty()) {
					std::string lower = name;
					std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (lower.find(a_needle) == std::string::npos) {
						continue;
					}
				}
				++shown;
				const bool allowed = Ladder::FollowerAllowed(base->GetFormID());
				Im::PushID(static_cast<int>(ref));
				Im::TableNextRow();
				Im::TableNextColumn();
				Im::TextWrapped("%s", name.c_str());
				Im::TableNextColumn();
				const auto* file = base->GetFile(0);
				const std::string from = file ? std::string(file->GetFilename()) : std::string("?");
				Im::TextWrapped("%s", from.c_str());
				Im::TableNextColumn();
				if (actor->IsPlayerTeammate()) {
					Im::TextColored(Im::ImVec4{ 0.6f, 0.85f, 1.0f, 1.0f }, "%s", "With you");
				} else if (allowed) {
					Im::TextColored(Im::ImVec4{ 0.5f, 1.0f, 0.6f, 1.0f }, "%s", "Living their life");
				} else {
					Im::TextColored(Im::ImVec4{ 0.7f, 0.7f, 0.7f, 1.0f }, "%s", "Kept for you");
				}
				Im::TableNextColumn();
				if (Im::Button(allowed ? "Allowed - undo" : "Allow")) {
					SetFollowerAllowed(actor, !allowed);
				}
				Hint(allowed ? "They can date and marry like anyone else (whether they can have children is the Children column). Press to keep them for yourself again." :
							   "Kept for you: they never date, marry or chat with other NPCs. Press to let them.");
				Im::TableNextColumn();
				const int         choice = Ladder::ChildrenChoice(base->GetFormID());
				const int         engine = Ladder::EngineChildren(base->GetFormID());
				const bool        female = base->GetSex() == RE::SEX::kFemale;
				const std::string autoLabel = engine == 0                                ? std::string("Automatic (pregnancy mod: no)") :
				                              Ladder::KnownNoChildren(base->GetFormID()) ? std::string("Automatic (no - automaton)") :
				                              !female                                    ? std::string("Automatic (can father)") :
				                              engine == 1                                ? std::string("Automatic (pregnancy mod: yes)") :
				                                                                           std::string("Automatic (yes)");
				const char*       items[3] = { autoLabel.c_str(), "Yes", "No" };
				Im::SetNextItemWidth(Im::GetFontSize() * 10.0f);
				if (Im::BeginCombo("##children", items[choice])) {
					for (int i = 0; i < 3; ++i) {
						if (Im::Selectable(items[i], choice == i)) {
							Ladder::SetChildrenChoice(base->GetFormID(), i);
						}
					}
					Im::EndCombo();
				}
				Hint(female ? "Can she have children? Automatic = whatever your pregnancy mod says (Beeing Female: its per-person, per-race and global pregnancy switches), and no for people who cannot by nature (automatons from follower mods). Yes = Reforged adds no rules of its own (skips the Khajiit and Argonian own-kind rule) - the pregnancy mod still has the final say. No = never." : "Can he father children? Automatic = yes unless your pregnancy mod has pregnancy switched off for everyone, or he cannot by nature (automatons). No = never fathers a child, whoever his partner is. Futas: your pregnancy mod decides whether they count - Reforged goes by the body the game gives them.");
				Im::PopID();
			}
			Im::EndTable();
			if (shown == 0) {
				Im::TextColored(Im::ImVec4{ 0.7f, 0.7f, 0.7f, 1.0f }, "%s",
					followerList.empty() ? "No custom followers found. Look at one and press 'Add the person I'm looking at'." : "Nobody matches the filter.");
			}
		}

		void CustomFollowersSection()
		{
			Im::TextUnformatted("CUSTOM FOLLOWERS");
			Im::Separator();
			Im::TextWrapped("%s", "Followers added by mods are yours: they never date, marry or chat with other NPCs unless you allow them here. Vanilla followers are not affected.");
			Hint("Found automatically: anyone a mod made a follower (the game's follower factions), followers from mods with their own follow system that this mod knows, and anyone who has travelled with you since. Nobody is ever allowed automatically - it is always your choice. Only people (any race the game treats as an NPC race, custom races included).");
			if (followerList.empty() || Im::Button("Refresh list")) {
				ScanCustomFollowers();
			}
			Hint("Run again after installing a follower mod, or after someone new has joined you.");
			Im::SameLine();
			if (Im::Button("Add the person I'm looking at")) {
				auto* pick = RE::CrosshairPickData::GetSingleton();
				auto  ref = pick ? pick->GetActiveTarget().get() : RE::NiPointer<RE::TESObjectREFR>();
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (!actor) {
					followerResult = "Nobody under the crosshair - look at them, then open this page.";
				} else if (!Ladder::IsModNpc(actor)) {
					followerResult = std::format("{} is from the base game - vanilla followers live normally and are not listed.", PersonName(actor));
				} else if (!Ladder::IsHumanoid(actor)) {
					followerResult = std::format("{} is not a person (creature, animal or child).", PersonName(actor));
				} else {
					Ladder::RememberRef(actor->GetActorBase()->GetFormID(), actor->GetFormID());
					Ladder::MarkFollower(actor->GetActorBase()->GetFormID(), true);
					Simulation::RequestRebuild();
					ScanCustomFollowers();
					followerResult = std::format("{} added to the list - kept for you until you allow them.", PersonName(actor));
				}
			}
			Hint("For a follower the list missed: look at them before opening the menu, then press this.");
			Im::SameLine();
			Im::SetNextItemWidth(Im::GetFontSize() * 12.0f);
			Im::InputText("Filter##followers", followerFilter, sizeof(followerFilter));
			if (!followerResult.empty()) {
				Im::TextWrapped("%s", followerResult.c_str());
			}
			int with = 0;
			int allowedCount = 0;
			for (const auto ref : followerList) {
				auto* a = RE::TESForm::LookupByID<RE::Actor>(ref);
				if (a && a->IsPlayerTeammate()) {
					++with;
				}
				if (a && a->GetActorBase() && Ladder::FollowerAllowed(a->GetActorBase()->GetFormID())) {
					++allowedCount;
				}
			}
			Im::TextUnformatted(std::format("{} found - {} with you, {} allowed to date.", followerList.size(), with, allowedCount).c_str());
			std::string needle = followerFilter;
			std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			Im::Spacing();
			FollowersTable(needle);
		}
		void Commit(const char* a_key)
		{
			Settings::Set(draft);
			Papyrus::NotifyChanged(a_key);
		}

		void Hint(const char* a_text)
		{
			if (a_text && Im::IsItemHovered()) {
				Im::BeginTooltip();
				Im::PushTextWrapPos(Im::GetFontSize() * 32.0f);  // wrap long help at a readable width (scales with the font)
				Im::TextUnformatted(a_text);
				Im::PopTextWrapPos();
				Im::EndTooltip();
			}
		}

		void Heading(const char* a_text)
		{
			Im::Spacing();
			Im::TextUnformatted(a_text);
			Im::Separator();
		}

		void Toggle(const char* a_label, bool& a_value, const char* a_key, const char* a_hint)
		{
			if (Im::Checkbox(a_label, &a_value)) {
				Commit(a_key);
			}
			Hint(a_hint);
		}

		// The label goes on the LEFT in a fixed column with the slider after it, so the name stays on screen however wide the
		// Mod Control Panel window is (ImGui puts slider labels on the right, which ran off the screen at 1440p).
		// Returns the hidden id to give the slider.
		std::string SliderLabel(const char* a_label, const char* a_hint)
		{
			Im::AlignTextToFramePadding();
			Im::TextUnformatted(a_label);
			Hint(a_hint);
			Im::SameLine(Im::GetFontSize() * 15.0f);
			Im::SetNextItemWidth(Im::GetFontSize() * 16.0f);
			return std::string("##") + a_label;
		}

		// Sliders save when the mouse lets go, not on every frame of a drag.
		void SliderI(const char* a_label, int& a_value, int a_low, int a_high, const char* a_format, const char* a_key, const char* a_hint)
		{
			const auto id = SliderLabel(a_label, a_hint);
			Im::SliderInt(id.c_str(), &a_value, a_low, a_high, a_format, Im::ImGuiSliderFlags_AlwaysClamp);
			if (Im::IsItemDeactivated()) {
				Commit(a_key);
			}
			Hint(a_hint);
		}

		void SliderF(const char* a_label, float& a_value, float a_low, float a_high, const char* a_format, const char* a_key, const char* a_hint)
		{
			const auto id = SliderLabel(a_label, a_hint);
			Im::SliderFloat(id.c_str(), &a_value, a_low, a_high, a_format, Im::ImGuiSliderFlags_AlwaysClamp);
			if (Im::IsItemDeactivated()) {
				Commit(a_key);
			}
			Hint(a_hint);
		}

		// A setting stored as a number that stands for one of a few named choices (0 = none, 1 = friends ...): a drop-down
		// with the words instead of a slider of codes. a_first is the number the first name stands for.
		void Choice(const char* a_label, int& a_value, std::initializer_list<const char*> a_names, int a_first, const char* a_key, const char* a_hint)
		{
			const auto  id = SliderLabel(a_label, a_hint);
			const int   count = static_cast<int>(a_names.size());
			const int   index = std::clamp(a_value - a_first, 0, count - 1);
			const auto* names = a_names.begin();
			if (Im::BeginCombo(id.c_str(), names[index])) {
				for (int i = 0; i < count; ++i) {
					if (Im::Selectable(names[i], i == index)) {
						a_value = a_first + i;
						Commit(a_key);
					}
				}
				Im::EndCombo();
			}
			Hint(a_hint);
		}

		void EnsureDraft()
		{
			if (!haveDraft) {
				draft = Settings::Raw();
				haveDraft = true;
			}
		}

		void __stdcall RenderRelationships()
		{
			EnsureDraft();
			Im::TextWrapped("%s", "How NPC couples form, move up, stray and break up.");
			Im::Separator();

			Heading("Pace");
			Im::TextUnformatted(std::format("Current: {}", Settings::PaceName(draft)).c_str());
			Hint("Slow, Default and Fast set how quickly couples form and marry, who can pair up and how often they stray. Change any of those sliders by hand and it shows Custom.");
			const std::pair<const char*, Settings::Pace> paces[] = { { "Slow", Settings::Pace::Slow }, { "Default", Settings::Pace::Default }, { "Fast", Settings::Pace::Fast } };
			for (const auto& [label, pace] : paces) {
				if (Im::Button(label)) {
					Settings::ApplyPace(draft, pace);
					Commit("Pace");
				}
				Im::SameLine();
			}
			Im::NewLine();
			Hint("Slow: about half a game year from meeting to wedding, only people you have met, cheating very rare.\nDefault: about two months, only people you have met, cheating rare.\nFast: about three weeks, anyone in Skyrim, cheating now and then.");

			Heading("Who gets together");
			Toggle("NPC couples", draft.ladderEnabled, "LadderEnabled",
				"NPCs pair up over repeated meetings: seeing each other, dating, engaged, married. Off = no couples are tracked and everything below up to Children does nothing.");
			Im::BeginDisabled(!draft.ladderEnabled);
			Toggle("Only people I've met", draft.onlyMetCouples, "OnlyMetCouples",
				"New couples only form between NPCs you have met - talked to, or been near (same building or street). Strangers in towns you have never visited stay single until you meet them; couples that already exist carry on as usual. Off = anyone in Skyrim can get together off-screen.");
			SliderF("Spark chance", draft.sparkChance, 0.0f, 100.0f, "%.0f%%", "SparkChance",
				"Chance that a meeting between two single people starts anything at all. Most meetings lead nowhere; people who keep running into each other get there in the end. M.A.R.A.S archetypes tilt it: romantics fall faster, the independent rarely, orators charm, a big class gap makes it unlikely. A scene in front of you counts double; exes meeting again half.");
			SliderF("Straight to dating", draft.directDatingShare, 0.0f, 100.0f, "%.0f%%", "DirectDatingShare",
				"Of the couples that do spark, how many skip 'seeing each other' and start dating at once. Romantics and the jealous more often, the independent hardly ever.");
			SliderI("Mourning", draft.mourningDays, 0, 365, "%d d", "MourningDays",
				"A widowed NPC takes no new partner for this long. Romantics mourn half as long again, the independent half as long. Scaled by World pace.");
			Toggle("Heartbreak", draft.heartbreak, "Heartbreak",
				"After a breakup with someone still alive, nobody starts something new at once: seeing each other about a week, dating about 3 weeks, a broken engagement about 2 months, a divorce about 3 months (x World pace). Romantics take longer, the independent less, and whoever ended it gets over it twice as fast. Free spirits never are heartbroken.");
			Toggle("Matchmakers", draft.matchmakers, "Matchmakers",
				"Finishing Spread the Love (Dinya Balu's Temple of Mara pamphlets) makes new sparks in Riften half as likely again for a month.");
			Toggle("Relationship rank follows the couple", draft.rankFollowsLadder, "RankFollowsLadder",
				"Sets the game's own relationship rank between the two: dating = confidant, engaged = ally, married = lover (like vanilla spouses). Never lowers a rank that is already higher. A breakup drops them to acquaintances. Other mods and Relations Finder read this rank.");

			Heading("How fast couples move up");
			SliderF("Step-up chance", draft.promoteChance, 0.0f, 100.0f, "%.0f%%", "PromoteChance",
				"Once a couple has met often enough and known each other long enough (below), the chance per meeting that they move up a stage - seeing each other to dating, dating to engaged. Archetypes tilt it.");
			SliderI("Meetings before dating", draft.candidateEncounters, 1, 20, "%d", "CandidateEncounters",
				"Meetings (scenes, in front of you or off-screen) before a pair who are seeing each other can start dating.");
			SliderI("Days before dating", draft.candidateMinDays, 0, 90, "%d d", "CandidateMinDays",
				"They also need to have known each other at least this long.");
			SliderI("Meetings before engaged", draft.engagedEncounters, 1, 40, "%d", "EngagedEncounters",
				"Meetings in total before a dating couple can get engaged.");
			SliderI("Days before engaged", draft.engagedMinDays, 0, 90, "%d d", "EngagedMinDays",
				"Shortest time a couple dates before an engagement.");
			SliderI("Days engaged before married", draft.marriedAfterDays, 1, 90, "%d d", "MarriedAfterDays",
				"Short, like Maramal says. With Real weddings on (Daily life) the wedding is then set for two days later.");
			SliderF("World pace", draft.worldPace, 0.25f, 4.0f, "x%.2f", "WorldPace",
				"Multiplies every 'days' setting of this mod (above, mourning, the off-screen world). 2 = the world changes half as fast, 0.5 = twice as fast.");

			Heading("Closeness");
			SliderF("Closeness per meeting", draft.closenessGain, 1.0f, 100.0f, "%.0f", "ClosenessGain",
				"How much closer a couple grows each time they meet (in front of you or off-screen). Closeness runs 0 to 100; how fast it fades between meetings is 'Drift per day' on the Off-screen world page. Hover the Closeness column on the Couples page for what it does.");

			Heading("Cheating and breakups");
			Toggle("Cheating", draft.infidelity, "Infidelity",
				"Off = couples stay faithful: a dating, engaged or married NPC never has a scene with anyone else, in front of you or off-screen, and nothing counts as cheating (no jealousy, no strikes, no breakups over it). On = the settings below apply.");
			Im::BeginDisabled(!draft.infidelity);
			SliderF("Cheating chance", draft.strayChance, 0.0f, 10.0f, "%.1f %%", "StrayChance",
				"Chance per scan that a married or courting NPC pairs with someone other than their partner. Scans run every few seconds, so small numbers add up: 0.3 = rare, 1 = now and then. 0 keeps every couple faithful.");
			SliderF("Repeat cheaters", draft.strayRepeatMult, 1.0f, 5.0f, "x%.1f", "StrayRepeatMult",
				"An NPC whose Lover's Ledger already shows cheating rolls the chance times this.");
			SliderF("Closeness lost per cheat", draft.strayPenalty, 0.0f, 100.0f, "%.0f", "StrayPenalty",
				"Closeness a couple loses each time one of them has a scene with someone else (less if they have children).");
			SliderI("Cheating before a breakup", draft.strayStrikes, 1, 5, "%d", "StrayStrikes",
				"Times one of them cheats before a couple made by this mod breaks up. Game marriages (unless 'Game marriages can end' is on) and quest-locked couples never break.");
			Toggle("Game marriages can end", draft.gameMarriagesCanEnd, "GameMarriagesCanEnd",
				"Off = marriages the game already had never end. On = they follow the same rules as couples made by this mod: enough cheating breaks them, and a cold marriage with cheating and no children ends in divorce. Protected couples (quest marriages, or any with 'Keep together' ticked on the Couples page) never end. They keep their house and their old 'my husband / my wife' lines.");
			Toggle("Follow other mods' relationships", draft.watchRelationships, "WatchRelationships",
				"Every few game hours this mod looks at every NPC relationship in memory. Partners whose rank another mod drops to acquaintance split up (a marriage is only logged), two people a mod makes lovers start dating (an affair when one of them is taken), and a new spouse record marries them. This mod's own rank changes follow its couples and are never mistaken for a mod's. Off = only the built-in couple rules and quest outcomes move couples. Log lines start with 'Relationship watcher' / 'Mod change'.");
			SliderI("Same lover makes an affair", draft.affairAfter, 1, 10, "%d", "AffairAfter",
				"Someone taken who keeps meeting the same person this many times has an affair with them: a secret side relationship, every meeting still cheating, until it fizzles out or comes out (gossip, or a quest). Once can be a mistake - the default is twice.");
			Im::EndDisabled();
			Heading("Family disapproval");
			Im::BeginDisabled(!draft.ladderEnabled);
			SliderI("Family comes round after", draft.familyBlockDays, 0, 365, "%d d", "FamilyBlockDays",
				"A couple whose family is against them (Dreyla Alor and Captain Veleth) may date but not get engaged until this many days of steady dating have passed. Quest-locked ones (Olfina and Jon) wait for their quest instead. 0 = never comes round.");
			Im::EndDisabled();
			Im::EndDisabled();  // NPC couples

			Heading("Children");
			Toggle("Khajiit and Argonians: own kind only", draft.beastChildrenOwnKind, "BeastChildrenOwnKind",
				"On (lore) = Khajiit (any furstock, custom Khajiit races too) and Argonians only have children with their own kind; they can still date and marry anyone. Off = any two people can have children. People who cannot have children at all (automatons from follower mods) are set per person on the Custom followers page.");

			Heading("Your own partners");
			Choice("Your partners date NPCs", draft.playerPartnerRomances, { "Never", "Only open marriages", "Yes, like any NPC" }, 0, "PlayerPartnerRomances",
				"Off-screen only. Never = your fiances and spouses never see other NPCs. Only open marriages = spouses you flagged open in M.A.R.A.S (The Flame Divides Not). Yes = they stay on the NPC ladder like anyone else.");

			Heading("M.A.R.A.S archetypes");
			Toggle("Use M.A.R.A.S archetypes", draft.useMarasArchetypes, "UseMarasArchetypes",
				"With M.A.R.A.S installed: social class, skill type and temperament shape who pairs with whom and how breakups go. Without it everyone is neutral.");

			Heading("Amulets of Mara");
			Toggle("Amulet of Mara means looking", draft.amuletEnabled, "AmuletEnabled",
				"NPCs wearing an Amulet of Mara attract more interest, court faster and have no flings with committed NPCs. Clergy ignored.");
			Im::BeginDisabled(!draft.amuletEnabled);
			SliderF("How much more interest", draft.lookingMultiplier, 1.0f, 5.0f, "x%.1f", "LookingMultiplier",
				"How much more interest someone wearing the amulet gets.");
			Im::EndDisabled();
			Toggle("Married NPCs put the amulet away", draft.manageMarriedAmulets, "ManageMarriedAmulets",
				"An NPC-NPC marriage removes the amulet, the amulet-giver spell (e.g. NPCs Wear Amulets of Mara) and the PotentialMarriage faction, so the player cannot propose to them. Restored on divorce.");
			Toggle("Hand out amulets myself", draft.ownAmulets, "OwnAmulets",
				"Only when no amulet-giver mod is installed: a few marriage-minded singles put an Amulet of Mara on by themselves and take it off once they are courting.");
			Im::BeginDisabled(!draft.ownAmulets);
			SliderF("Share of singles wearing one", draft.ownAmuletShare, 0.0f, 50.0f, "%.0f %%", "OwnAmuletShare",
				"Of the single NPCs, how many wear an amulet this way.");
			Im::EndDisabled();
		}

		void __stdcall RenderDailyLife()
		{
			EnsureDraft();
			Im::TextWrapped("%s", "What couples do together: talking and weddings. Evenings and nights are on the Living together page.");
			Im::Separator();

			Heading("Voice lines (NPC couples)");
			Toggle("Couples speak", draft.voiceLines, "VoiceLines",
				"Couples near you greet each other and say goodbye (dating, engaged and married lines), react out loud when they get engaged or break up, and an ex they jilted gets a cold word. A partner who catches their other half in a scene nearby reacts, and is heartbroken afterwards. Vanilla voice lines only - voices without them stay silent.");
			Toggle("Talk to friends about their partner", draft.friendChats, "FriendChats",
				"Now and then a married or engaged NPC near you tells a friend or relative standing close by about their partner (fond, excusing, or fed up - it follows how the marriage is going) or their coming wedding, and the friend answers. Rare: few voices recorded such lines, at most one an hour, each NPC once a day.");
			Im::BeginDisabled(!draft.voiceLines);  // the Papyrus side needs VoiceLines too
			Toggle("Family chats", draft.familyChats, "FamilyChats",
				"Parents, children, siblings and grandparents near you now and then say an everyday line to each other (vanilla lines, vanilla voices). Never anything romantic - children are never part of a couple. Needs \"Couples speak\" on.");
			Im::EndDisabled();
			Im::BeginDisabled(!draft.voiceLines && !draft.friendChats);
			SliderF("Cooldown per couple", draft.voiceCooldownHours, 0.5f, 24.0f, "%.1f h", "VoiceCooldownHours",
				"Game hours before the same couple says something again.");
			Im::EndDisabled();


			Heading("Weddings");
			Toggle("Real weddings", draft.realWeddings, "RealWeddings",
				"When the engagement time is up the couple sets a wedding at the Temple of Mara in Riften, two days later in the afternoon (any time from noon to 17:00), and you hear of it (a letter if one of them is close to you). Come to the temple in that window and Maramal holds the ceremony - with guests saying their congratulations; otherwise they marry quietly at 17:00. Bride and groom dress for it. The date shows in the Couples page notes. Off by default while it is tested.");
			SliderI("Days between weddings", draft.weddingGapDays, 1, 60, "%d d", "WeddingGapDays",
				"Weddings take turns: at most one couple marries every this many days, anywhere. A couple whose engagement time is up waits for the next free day (with Real weddings the invitation still comes two days before). Counts every marriage the mod makes.");
			Im::BeginDisabled(!draft.realWeddings);
			SliderI("Wedding guests", draft.weddingGuests, 0, 12, "%d", "WeddingGuests",
				"Real weddings: how many guests come besides the children of either partner - children always come. Close family first (parents, siblings, grandparents), then friends (Friend, Confidant or Ally with either partner). They gather at the Temple of Mara with the couple and go home afterwards. Followers travelling with you and custom followers you have not allowed never come.");
			Toggle("Guests sit", draft.seatedGuests, "SeatedGuests",
				"Real weddings: guests take a seat on the temple benches during the ceremony instead of standing around the altar.");
			SliderI("Left at the altar chance", draft.altarNoShowChance, 0, 30, "%d%%", "AltarNoShowChance",
				"Real weddings: chance that one partner never comes. The other waits at the altar, asks \"Where are you?\" now and then while you are there, gives up at 16:00 and goes home - the engagement is over. +10% for each time one of them strayed and +10% if they are not close (at most 40%); the one who strayed more is the one who stays away. 0 = it never happens.");
			SliderI("Wait for you", draft.weddingWaitDays, 0, 7, "%d d", "WeddingWaitDays",
				"Real weddings: when you got an invitation letter and did not come, the wedding is put off a day instead of happening quietly - up to this many times. 0 = it goes ahead without you.");
			Toggle("Objecting stops the wedding", draft.objectionStopsWedding, "ObjectionStopsWedding",
				"Real weddings: attacking the bride, the groom or Maramal during the ceremony stops it. The couple stays engaged but grows less close, and a new date is set later.");
			Im::EndDisabled();
		}

		// 0.22.2: its own page (the user could not find it under Daily life)
		void __stdcall RenderLivingTogether()
		{
			EnsureDraft();
			Im::TextWrapped("%s", "Where couples spend their evenings and nights: sleep-overs, date nights, households and nights out at the inn.");
			Im::Separator();

			Heading("Living together (experimental)");
			Toggle("Sleep-overs", draft.sleepOvers, "SleepOvers",
				"Married couples who live apart: in the evening (21:00) the partner who lives elsewhere goes to the shared home (Couples page notes) and hangs around their spouse there - eating, sitting, sleeping in a free bed - and goes back to their own routine at 07:00. Never followers or guards. Off by default while it is tested.");
			Toggle("Date nights", draft.dateNights, "DateNights",
				"Dating and engaged couples who live in the same town: some evenings (18:00-22:00) one of them walks over to the other and spends the evening near them - and if you are around, they chat. Back to their own routine at 22:00, or straight away when a quest needs either of them. Never followers or guards. Off by default while it is tested.");
			Toggle("Households", draft.households, "Households",
				"Married couples who live apart in the same town: the spouse moves in for the evenings and nights (19:00-07:00) - they join the house, so its beds are theirs too - and keep their day job; their Beeing Female children live there too; a widow keeps the house. Off = the older sleep-over rule only. Never followers or guards; quests always win. Off by default while it is tested.");
			Im::BeginDisabled(!draft.households);
			Toggle("Children live at home", draft.householdChildren, "HouseholdChildren",
				"Households: the couple's Beeing Female children live in the home - out and about in town by day like other children, home from 19:00 to sleep.");
			Toggle("Nights out at the inn", draft.nightsOut, "NightsOut",
				"Households: some evenings (the date night chance below) the couple goes to their town's inn together from 19:00 to 22:00, then home to bed.");
			Im::EndDisabled();
			Im::BeginDisabled(!draft.dateNights && !(draft.households && draft.nightsOut));
			SliderI("Date night chance", draft.dateNightChance, 0, 100, "%d%%", "DateNightChance",
				"Chance each evening that a given couple has a date night (dating and engaged couples) or a night out at the inn (households).");
			Im::EndDisabled();

		}

		void __stdcall RenderWorld()
		{
			EnsureDraft();
			Im::TextWrapped("%s", "The off-screen world: NPCs you are not near still meet, court, marry and drift apart.");
			Im::Separator();

			Heading("Simulation");
			Toggle("Simulate the world while you are away", draft.simulationEnabled, "SimulationEnabled",
				"Once a game day, NPCs you are not near may meet, court, marry, drift apart and split. Off = only scenes you witness count.");
			Im::BeginDisabled(!draft.simulationEnabled);
			SliderF("Daily chance", draft.dailyChance, 0.0f, 10.0f, "%.2f %%", "DailyChance",
				"Base chance per likely pair per simulated day, before every multiplier. 0.25 is roughly one encounter per NPC every 80 days; 1 already fills the Couples page within a week.");
			SliderF("Same hold factor", draft.sameHoldFactor, 0.0f, 1.0f, "x%.2f", "SameHoldFactor",
				"Chance factor for NPCs living elsewhere in the same hold. 0 = NPCs only meet within their own community.");
			SliderI("Likely partners per NPC", draft.shortlist, 1, 20, "%d", "Shortlist",
				"How many of the best-scoring partners each NPC considers per simulated day.");
			SliderI("New couples per community per month", draft.maxNewCouplesPerCommunityPerMonth, 0, 10, "%d", "MaxNewCouplesPerCommunityPerMonth",
				"Pace cap for new courting couples in one town or hold per 30 days. 0 = no cap.");
			SliderF("Drift per day", draft.driftPerDay, 0.0f, 10.0f, "%.1f", "DriftPerDay",
				"Closeness a couple loses per day without meeting. Paused for followers and expecting couples; divided by their children.");
			SliderI("Cold days before divorce", draft.coldDaysBeforeDivorce, 1, 365, "%d d", "ColdDaysBeforeDivorce",
				"A married couple made by this mod that has gone cold and strayed divorces after this long. Never with children.");
			SliderI("Catch-up cap", draft.catchUpCapDays, 1, 90, "%d d", "CatchUpCapDays",
				"Most missed days simulated in one go after a long wait or sleep.");
			Toggle("Write off-screen meetings to Lover's Ledger", draft.writeLoversLedger, "WriteLoversLedger",
				"Off-screen meetings appear in Lover's Ledger like witnessed ones.");
			Im::EndDisabled();
		}

		// 0.22.2: its own page (the user could not find it under Off-screen world)
		void __stdcall RenderPregnancy()
		{
			EnsureDraft();
			Im::TextWrapped("%s", "Children and periods: pregnancies from NPC couples, who can have children, and Beeing Female periods in scenes.");
			if (!Settings::Installed(Settings::Module::Families)) {
				Im::TextColored(Im::ImVec4{ 1.0f, 0.8f, 0.3f, 1.0f }, "%s",
					"The Families module is not installed: only the period setting works. Everything else on this page stays off.");
			}
			Im::Separator();

			Heading("Pregnancy and periods");
			Toggle("Off-screen meetings can lead to pregnancy", draft.pregnancyEvents, "PregnancyEvents",
				"Beeing Female NG, Fertility Mode and Fertility Mode Reloaded all listen for the same event. Their own cycle decides whether anything comes of it. Expecting couples do not drift; children make a marriage very hard to break. Turn the engine's own random NPC insemination off so fathers come from real relationships.");
			Toggle("Skip women on their period for NPC scenes", draft.skipPeriodScenes, "SkipPeriodScenes",
				"Beeing Female: a woman on her period is not picked for NPC scenes. After any scene of this mod she puts her tampon back on (OStim undresses it, and without it Beeing Female paints blood over her, clothes too). Does nothing without Beeing Female.");
			Im::BeginDisabled(draft.babyStages);
			Toggle("NPC couples' baby items grow up", draft.hatchNpcBabies, "HatchNpcBabies",
				"Beeing Female with baby item births: a baby item only turns into a child when you are the father. On: babies of two NPCs turn into children too, after the same time (Beeing Female's Mature time), while the mother is near you. Does nothing without Beeing Female. Greyed out while Baby stages is on (baby stages handle every baby).");
			Im::EndDisabled();

			Heading("Baby stages");
			Toggle("Baby stages", draft.babyStages, "BabyStages",
				"Every Beeing Female baby item - yours too - goes through three stages. Carried: the mother keeps the baby item. Away: the baby item is gone and the little one lives elsewhere for a while (with grandparents, at a temple, with a wet nurse...) - ask either parent \"How is the little one?\" and they tell you where. Child: the child comes home and appears beside a parent, then grows up on Beeing Female's own clock. Off = baby items hatch on Beeing Female's own time as before. Does nothing without Beeing Female.");
			Im::BeginDisabled(!draft.babyStages);
			// each slider's range stops at the other's value, so "comes home" is always after "carried"
			SliderI("Carried for", draft.babyCarriedDays, 1, std::min(3650, draft.childAppearsDays - 1), "%d days", "BabyCarriedDays",
				"Game days the mother keeps the baby item before the little one goes away. 365 = one year. Beeing Female's own Mature time must be longer than five times this, or Beeing Female hatches your own babies first (the Papyrus log says so once).");
			SliderI("Comes home after", draft.childAppearsDays, draft.babyCarriedDays + 1, 7300, "%d days", "ChildAppearsDays",
				"Game days after the birth when the child comes home and appears beside a parent (the mother, or the father if she has died). 1642 = four and a half years: Skyrim has no toddler body, so the smallest child the game can show looks about four or five. Always after \"Carried for\".");
			Im::TextWrapped("%s", std::format("Carried until {:.1f} years old, away until {:.1f}, then at home.", draft.babyCarriedDays / 365.0f, draft.childAppearsDays / 365.0f).c_str());
			Im::EndDisabled();
			Im::Separator();
			Toggle("Pregnancy rules: vampires, automatons, beast races", draft.pregnancyRules, "PregnancyRules",
				"On: vampires have no children until they are cured (every vampire race, you when turned), automatons and androids from follower mods never, and Khajiit and Argonians only with their own kind (all Khajiit sub-races count as one kind). Applies to this mod's couples and, through Beeing Female's own per-person switches, to every Beeing Female pregnancy near you - yours included. Checked every 10 s for the people around you; a cure takes effect within that time.");

		}

		void __stdcall RenderLetters()
		{
			EnsureDraft();
			Im::TextWrapped("%s", "What you hear about NPC couples: on-screen messages, gossip and courier letters.");
			Im::Separator();

			Heading("On-screen messages");
			Choice("Messages about", draft.notifications, { "Nobody", "Friends and people you court", "Every couple in Skyrim" }, 0, "Notifications",
				"On-screen messages about NPC couples. Nobody = you only learn things in the world (letters, seeing them, the News page). Friends = friends of yours and NPCs you are courting. Every couple = everyone in Skyrim.");
			Toggle("Gossip", draft.gossip, "Gossip",
				"A message when someone close to you is seen cheating on their partner.");

			Heading("Courier letters");
			Toggle("Courier letters", draft.lettersEnabled, "LettersEnabled",
				"Friends of yours (and M.A.R.A.S candidates you are courting) write when they start dating, get engaged, marry or break up.");
			Im::BeginDisabled(!draft.lettersEnabled);
			Choice("Who writes to you", draft.letterMinRank, { "Friends and closer", "Confidants and closer", "Allies and closer", "Lovers only" }, 1, "LetterMinRank",
				"How close someone must be to you (the game's own relationship rank) before they write about their love life.");
			Choice("Someone you court writes", draft.candidateLetterStage, { "Never", "When they start dating someone", "When they get engaged", "When they marry" }, 0, "CandidateLetterStage",
				"An NPC you are courting in M.A.R.A.S who takes up with someone else tells you by letter - as soon as they start dating, once they are engaged (they have chosen), or only once they are married.");
			Im::EndDisabled();

		}

		void __stdcall RenderScenes()
		{
			EnsureDraft();
			Im::TextWrapped("%s", "The original OStim NPCs scenes: when and where two NPCs start a scene on their own.");
			Im::Separator();

			Heading("Main");
			{
				bool autoScenes = !draft.disabled;  // stored the other way round ("Disabled") - shown as a plain on switch
				if (Im::Checkbox("NPCs start scenes on their own", &autoScenes)) {
					draft.disabled = !autoScenes;
					Commit("Disabled");
				}
				Hint("Off = no NPC starts a scene by themselves (the scans around you stop). The match-maker spells still work.");
			}
			Toggle("Scenes end when found", draft.stopWhenFound, "StopWhenFound",
				"NPC scenes end, and never start, when the player is detected. Does not apply to follower scenes.");
			Toggle("NPCs walk to the furniture", draft.travelToLocation, "TravelToLocation",
				"Off = the pair is placed at the furniture directly.");
			Toggle("Enemies walk to the furniture", draft.enemiesTravelToLocation, "EnemiesTravelToLocation",
				"Same for enemies in camps and dungeons.");
			Toggle("Arrows off in scenes", draft.sceneAmmoOff, "SceneAmmoOff",
				"Arrows and bolts come off in every OStim scene (yours too) and go back on after. Followers otherwise put their quiver straight back on.");


			Heading("Followers and enemies");
			Toggle("Followers have scenes between themselves", draft.allowActiveFollowers, "AllowActiveFollowers",
				"Two of your followers may pair up at night.");
			Toggle("Followers may pair with other NPCs", draft.activeFollowersEngageOtherNPCs, "ActiveFollowersEngageOtherNPCs",
				"A follower may be picked as a partner for a nearby NPC.");
			Toggle("Allow the player's partner", draft.allowPlayersPartners, "AllowPlayersPartners",
				"Off = your lover or spouse is never picked for an NPC scene.");
			Toggle("Allow elders", draft.allowElderRace, "AllowElderRace",
				"Elder race NPCs may be picked.");
			Toggle("Enemies have scenes", draft.allowCommonEnemies, "AllowCommonEnemies",
				"Bandits, warlocks, necromancers, cultists and the like pair up in their camps.");
			Toggle("Followers: no scenes in dungeons", draft.followersNoScenesDungeons, "FollowersNoScenesDungeons",
				"Follower scenes never start inside dungeons.");


			Heading("Furniture");
			Toggle("Use furniture", draft.enableFurniture, "EnableFurniture",
				"Look for furniture before starting a scene.");
			Im::BeginDisabled(!draft.enableFurniture);
			Toggle("Only beds count as furniture", draft.furnitureOnlyBeds, "FurnitureOnlyBeds",
				"Ignore chairs, benches and the rest.");
			Im::EndDisabled();
			Im::TextUnformatted("Scenes start in");
			Hint("Where a scene may start at all. Beds only is the original default.");
			Im::SameLine();
			bool changed = false;
			changed |= Im::RadioButton("Anywhere", &draft.scenesStartIn, 0);
			Im::SameLine();
			changed |= Im::RadioButton("Any furniture", &draft.scenesStartIn, 1);
			Im::SameLine();
			changed |= Im::RadioButton("Beds only", &draft.scenesStartIn, 2);
			if (changed) {
				Commit("ScenesStartIn");
			}
			Toggle("Enemies ignore the 'scenes start in' rule", draft.enemiesIgnoreScenesStartInRules, "EnemiesIgnoreScenesStartInRules",
				"Enemy camps rarely have beds, so let them start anywhere.");


			Heading("Locations");
			Toggle("No scenes in towns", draft.noScenesInTowns, "NoScenesInTowns",
				"Nothing starts in city and town locations.");
			Toggle("No scenes in guild halls", draft.noScenesInGuilds, "NoScenesInGuilds",
				"Nothing starts in guild locations.");
			Toggle("No scenes in inns", draft.noScenesInInns, "NoScenesInInns",
				"Nothing starts in inns.");


			Heading("Limits and timing");
			SliderI("Scenes at once", draft.maxScenes, 1, 4, "%d", "MaxScenes",
				"How many NPC scenes may run at the same time.");
			SliderI("Follower scenes per night", draft.maxFollowerScenesPerNight, 1, 10, "%d", "MaxFollowerScenesPerNight", nullptr);
			SliderI("Enemy scenes per night", draft.maxEnemyScenesPerNight, 1, 20, "%d", "MaxEnemyScenesPerNight", nullptr);
			int startPM = draft.minNight - 12;
			const auto nightId = SliderLabel("Night starts", "NPCs start looking for partners from this hour.");
			Im::SliderInt(nightId.c_str(), &startPM, 1, 12, "%d PM", Im::ImGuiSliderFlags_AlwaysClamp);
			draft.minNight = startPM + 12;
			if (Im::IsItemDeactivated()) {
				Commit("MinNight");
			}
			Hint("NPCs start looking for partners from this hour.");
			SliderI("Night ends", draft.maxNight, 1, 11, "%d AM", "MaxNight",
				"NPCs stop starting scenes from this hour. 4 AM is the original default.");
			SliderI("Scan every", draft.scanFreq, 10, 120, "%d s", "ScanFreq",
				"Seconds between two scans of the NPCs around you.");


			Heading("Pair weights");
			SliderI("Male - female", draft.weightMF, 0, 100, "%d", "WeightMF",
				"0 = mixed couples never have scenes. Any other value = they may. (Scenes come from known couples, so the number no longer sets how often.)");
			SliderI("Female - female", draft.weightFF, 0, 100, "%d", "WeightFF",
				"0 = female couples never have scenes. Any other value = they may.");
			SliderI("Male - male", draft.weightMM, 0, 100, "%d", "WeightMM",
				"0 = male couples never have scenes. Any other value = they may.");


			Heading("Partner choice");
			Toggle("Exclude family (Relations Finder)", draft.useRelationsFinderFamily, "UseRelationsFinderFamily",
				"Blood and in-law family of any degree never pair, on top of the vanilla parent, sibling and cousin checks.");
			Toggle("Use Lover's Ledger", draft.useLoversLedger, "UseLoversLedger",
				"Reads when a couple was last together (for the short break after a scene) and, between two equal couples, prefers one with a shared past. A past scene alone never makes a couple. Off = the break counts only scenes since this game was loaded.");


			Heading("Match-maker spells");
			if (Im::Button("Add spells")) {
				Actions::SetMatchMakerSpells(true);
			}
			Hint("Gives you the two match-maker spells: one targets an NPC, one targets yourself.");
			Im::SameLine();
			if (Im::Button("Remove spells")) {
				Actions::SetMatchMakerSpells(false);
			}
			Hint("Takes the two spells away again.");

		}

		void __stdcall RenderAdvanced()
		{
			EnsureDraft();
			Im::TextWrapped("%s", "Testing and troubleshooting options.");
			Im::Separator();

			Heading("Testing mode");
			const bool testing = Settings::TestingMode();
			Im::TextColored(testing ? Im::ImVec4{ 1.0f, 0.8f, 0.3f, 1.0f } : Im::ImVec4{ 0.7f, 0.7f, 0.7f, 1.0f }, "%s",
				testing ? "Testing mode is ON - fast rates, every feature under test switched on." : "Testing mode is off - your own settings.");
			if (Im::Button(testing ? "Back to my own settings" : "Start testing mode")) {
				if (testing) {
					Settings::StopTesting();
				} else {
					Settings::StartTesting();
				}
				draft = Settings::Raw();
				Papyrus::NotifyChanged("*");
			}
			Hint("For testing: couples spark, date, get engaged and marry within a day or two (spark 100%, move-up 100%, engaged after 2 meetings, married after 1 day, one wedding a day), the off-screen world runs fast, and date nights (every evening), sleep-overs, real weddings, letters, all notifications and debug notifications are on. Your own settings are saved first and come back exactly as they were when you press 'Back to my own settings' - even after saving, reloading or restarting.");

			Heading("Troubleshooting");
			Toggle("Furniture guard debug log", draft.furnitureGuardDebug, "FurnitureGuardDebug",
				"Writes every furniture-guard decision to the Papyrus log. Only for troubleshooting.");
			Toggle("Debug notifications", draft.debugNotifications, "DebugNotifications",
				"On-screen line for everything the simulation and the ladder do: pool rebuilds, each off-screen encounter, strays and strikes, drift, cold marriages, promotions, pregnancies. Independent of the Notifications level. Testing only - it is chatty.");

			// 0.27.0: on this always-shown page (it used to sit on the Relationships page, which is hidden without that module)
			Heading("My presets");
			Im::TextWrapped("%s", "Save every setting of this mod under a name, and load it again later (or on another character).");
			Im::SetNextItemWidth(Im::GetFontSize() * 14.0f);
			Im::InputText("##presetname", presetName, sizeof(presetName));
			Im::SameLine();
			if (Im::Button("Save as preset")) {
				presetResult = Settings::SavePreset(presetName) ? std::format("Saved \"{}\".", presetName) : std::string("Type a name first (letters, digits, spaces).");
			}
			for (const auto& name : Settings::SavedPresets()) {
				Im::PushID(name.c_str());
				Im::TextUnformatted(name.c_str());
				Im::SameLine();
				if (Im::Button("Load")) {
					if (Settings::LoadPreset(name)) {
						draft = Settings::Raw();
						Papyrus::NotifyChanged("Preset");
						presetResult = std::format("Loaded \"{}\".", name);
					}
				}
				Im::SameLine();
				if (Im::Button("Delete")) {
					Settings::DeletePreset(name);
					presetResult = std::format("Deleted \"{}\".", name);
				}
				Im::PopID();
			}
			if (!presetResult.empty()) {
				Im::TextWrapped("%s", presetResult.c_str());
			}


			Heading("Start fresh");
			Im::TextWrapped("%s", "Forget every couple and all news in this playthrough and let the world start over from today.");
			if (Im::Button("Start fresh...")) {
				Im::OpenPopup("Start fresh?");
			}
			Hint("Asks first. Married couples this mod made are unmarried (ring, amulet - like a quiet divorce). Kept: your settings, follower and children choices, who you have met, pregnancies. Couples the game itself made (like Keerava and Talen-Jei) are picked up again the next time they meet. Lover's Ledger keeps its own records.");
			if (Im::BeginPopupModal("Start fresh?", nullptr, Im::ImGuiWindowFlags_AlwaysAutoResize)) {
				Im::PushTextWrapPos(Im::GetFontSize() * 30.0f);
				Im::TextUnformatted(std::format("This forgets all {} couple(s) and all news in this save, and unmarries the couples this mod married.", Ladder::Snapshot().size()).c_str());
				Im::Spacing();
				Im::TextColored(Im::ImVec4{ 1.0f, 0.5f, 0.5f, 1.0f }, "%s", "It cannot be undone from here.");
				Im::TextUnformatted("Your way back: your earlier save files still have the old couples - load one. Make a save now first if you might want them back.");
				Im::PopTextWrapPos();
				Im::Spacing();
				if (Im::Button("Yes, start fresh")) {
					const auto n = Ladder::StartFresh();
					freshResult = std::format("Started fresh - {} couple(s) forgotten.", n);
					Im::CloseCurrentPopup();
				}
				Im::SameLine();
				if (Im::Button("Cancel")) {
					Im::CloseCurrentPopup();
				}
				Im::EndPopup();
			}
			if (!freshResult.empty()) {
				Im::TextUnformatted(freshResult.c_str());
			}

			Im::Spacing();
			if (Im::Button("Reset everything to defaults")) {
				draft = Settings::Defaults();
				Commit("*");
			}
			Hint("Every setting on every page back to the original OStim NPCs defaults plus the Reforged defaults.");
		}

		void __stdcall RenderCouples()
		{
			const auto pairs = Ladder::Snapshot();
			const float now = Ladder::DaysNow();
			Im::TextUnformatted("NPC COUPLES");
			Im::Separator();
			Im::TextWrapped("%s", std::format("{} pair(s) tracked. Game day {:.1f}. Simulation pool {} NPCs, last simulated day {}.", pairs.size(), now, Simulation::PoolSize(), Simulation::LastSimulatedDay()).c_str());
			Hint("Every pair this mod knows about. 'Up to' / 'Back to' move a pair one stage (the button names the stage it goes to); Forget drops the pair from the list.");

			// tools fold away so the list stays the main thing on the page
			if (Im::CollapsingHeader("Add a couple")) {
				Im::Indent();
				Im::TextWrapped("%s", "Pick two people: type a few letters of a name, or look at someone before opening the menu and press Use crosshair.");
				PickerSlot(0);
				PickerSlot(1);
				// 0.19.0: how person A does love (Haelga, Sergius and Mikael are free spirits by the game's own stories)
				if (pickBase[0]) {
					int style = static_cast<int>(Ladder::GetStyle(pickBase[0]));
					Im::TextUnformatted(std::format("{}'s love life:", pickName[0]).c_str());
					Hint("Normal: dates, commits, can marry. Free spirit: several lovers at once, never settles down, never counts as taken (a married lover of theirs still cheats on their own spouse); their own flirting lines always play. Never: no love life at all. Your own romances are M.A.R.A.S's business - this only shapes NPC-to-NPC life.");
					bool changed = false;
					Im::SameLine();
					changed |= Im::RadioButton("Normal##style", &style, 0);
					Im::SameLine();
					changed |= Im::RadioButton("Free spirit##style", &style, 1);
					Im::SameLine();
					changed |= Im::RadioButton("Never##style", &style, 2);
					if (changed) {
						Ladder::SetStyle(pickBase[0], static_cast<Ladder::Style>(style));
					}
				}
				Im::TextUnformatted("Start them as:");
				Im::SameLine();
				Im::RadioButton("Seeing each other", &pickStage, 1);
				Im::SameLine();
				Im::RadioButton("Dating", &pickStage, 2);
				Im::SameLine();
				Im::RadioButton("Engaged", &pickStage, 3);
				Im::SameLine();
				Im::RadioButton("Married", &pickStage, 4);
				Hint("Dating, engaged and married run the usual news, letters and ring.");
				Im::Checkbox("Never break up", &pickKeep);
				Hint("Ticked: the couple you create here can never split up - no drifting apart, no breakup over cheating, no divorce, never cleared as stale - the same protection couples the game itself made get. They still have ups and downs (a marriage can go cold, an engagement can cool back to dating, either can stray if cheating is on), but they always stay a couple. Unticked: normal rules, they can split like anyone else.");
				Im::BeginDisabled(!pickBase[0] || !pickBase[1] || pickBase[0] == pickBase[1]);
				if (Im::Button("Create couple")) {
					Ladder::CreateCouple(pickBase[0], pickBase[1], static_cast<Ladder::Stage>(pickStage), pickKeep);
					Ladder::MarkByPlayer(pickBase[0], pickBase[1]);
					pickResult = std::format("{} and {} paired.", pickName[0], pickName[1]);
				}
				Im::EndDisabled();
				if (!pickResult.empty()) {
					Im::SameLine();
					Im::TextUnformatted(pickResult.c_str());
				}
				Im::Unindent();
				Im::Spacing();
			}
			if (Im::CollapsingHeader("Import couples")) {
				Im::Indent();
				EnsureDraft();
				Im::TextWrapped("%s", "The game already has married and courting couples (Alvor and Sigrid, Keerava and Talen-Jei ...). They are taken in automatically once per save, the first time this mod runs after an update. Press the button to run it again - couples already tracked are left as they are.");
				if (Im::Button("Import the game's couples")) {
					const auto r = Ladder::ImportGameCouples(false);
					importResult = !r.ran ? std::string("Nothing happened - couples are switched off.")
						: std::format("Imported {} married and {} courting couples ({} kept to their quests, {} with children, {} skipped). {} more are not loaded right now (their town or stronghold is far away) - they join the first time you are near them.", r.married, r.courting, r.protectedByQuest, r.withChildren, r.skipped, r.waiting);
				}
				Hint("Game marriages become Married; courting and lover-rank pairs become Dating. A couple a quest owns is protected - the quest decides, this mod never moves or splits them. Everyone else lives on the ladder like couples this mod made. Couples with children (any age) never divorce. Their home, and any ring they already wear, stay as they are.");
				if (!importResult.empty()) {
					Im::TextWrapped("%s", importResult.c_str());
				}
				Im::Spacing();
				Im::TextWrapped("%s", "For a save that already ran Lover's Ledger: pairs of NPCs who were together often enough become Dating. Not automatic, and not for new games - on a new game Ledger mostly holds what this mod wrote itself. (Relations Finder reads the same game records as the button above, so there is nothing extra to import from it; M.A.R.A.S holds your own partners, which this mod leaves out.)");
				SliderI("Scenes to count as dating", draft.ledgerImportEncounters, 1, 20, "%d", "LedgerImportEncounters",
					"How many scenes two NPCs must have had together in Lover's Ledger before they are imported as a dating couple.");
				if (Im::Button("Import from other mods")) {
					Papyrus::SendImportOthers(draft.ledgerImportEncounters);
					importResult = "Import from Lover's Ledger queued - close the menu; the result shows on the News and weddings page.";
				}
				Hint("Reads Lover's Ledger and makes a Dating couple out of every pair of NPCs with at least that many scenes together (neither already committed to someone else). Safe to press again.");
				Im::Unindent();
				Im::Spacing();
			}
			if (Im::CollapsingHeader("Testing tools")) {
				Im::Indent();
				if (Im::Button("Simulate one more day")) {
					tickResult = Simulation::RunDayNow();
				}
				Hint("Runs one extra day of the off-screen world straight away (sparks, dates, engagements, weddings planned, drifting apart) and says what changed. Each press counts as another day for closeness too. For testing.");
				Im::SameLine();
				if (Im::Button("Voice test (two nearest)")) {
					Papyrus::SendVoiceTest();
					pickResult = "Voice test queued - close the menu and listen.";
				}
				Hint("The two people nearest to you become a married couple and, once the menu is closed, say every voice moment in turn (on-screen labels).");
				if (!tickResult.empty()) {
					Im::TextWrapped("%s", tickResult.c_str());
				}
				Im::Unindent();
				Im::Spacing();
			}
			Im::Separator();

			// counts per filter: an empty stage is greyed out and shows (0), so clicking it never looks broken. Past couples (drifted
			// apart, broken up, divorced, widowed) are kept for good - exes, gossip and history - but hidden unless asked for.
			int stageCount[8] = { static_cast<int>(pairs.size()), 0, 0, 0, 0, 0, 0, 0 };
			auto filterOf = [](const Ladder::Pair& a_p) { return Ladder::IsEndedStage(a_p.stage) ? 5 : Ladder::IsSideStage(a_p.stage) ? 7 : static_cast<int>(a_p.stage); };
			for (const auto& p : pairs) {
				const int f = filterOf(p);
				++stageCount[f];
				if (f >= 1 && f <= 4) {
					++stageCount[6];
				}
			}
			if (stageFilter != 0 && stageFilter != 6 && stageCount[stageFilter] == 0) {
				stageFilter = 6;  // the stage emptied while it was selected
			}
			Im::TextUnformatted("Show:");
			static constexpr const char* kFilterNames[8] = { "All", "Seeing", "Dating", "Engaged", "Married", "Past", "Current", "Affairs, crushes" };
			for (const int f : { 6, 1, 2, 3, 4, 7, 5, 0 }) {
				Im::SameLine();
				Im::BeginDisabled(f != 0 && f != 6 && stageCount[f] == 0);
				Im::RadioButton(std::format("{} ({})##stage{}", kFilterNames[f], stageCount[f], f).c_str(), &stageFilter, f);
				Im::EndDisabled();
			}
			Hint("Current = everyone seeing each other, dating, engaged or married. Affairs, crushes = the side relationships: secret affairs of someone taken, one-sided crushes, and a vampire's thrall - not couples, never in the news. Past = couples who drifted apart, broke up, divorced or were widowed - kept as history (exes are remembered, gossip and news use them), just not shown unless you pick Past or All. The number is how many; greyed out = none.");
			Im::SetNextItemWidth(Im::GetFontSize() * 16.0f);
			Im::InputText("Search", couplesFilter, sizeof(couplesFilter));
			Hint("Show only pairs whose names contain this text.");
			std::string needle = couplesFilter;
			std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			Im::Spacing();

			const Im::ImGuiTableFlags flags = Im::ImGuiTableFlags_Borders | Im::ImGuiTableFlags_RowBg | Im::ImGuiTableFlags_SizingStretchProp;
			if (!Im::BeginTable("couples", 6, flags)) {
				return;
			}
			// 0.19.1: the notes are a grey second line under the names (their own column was squeezed off the right edge)
			Im::TableSetupColumn("Couple and notes", 0, 4.0f);
			Im::TableSetupColumn("Stage", 0, 1.0f);
			Im::TableSetupColumn("Met", 0, 0.8f);  // in front of the player / off-screen (one column - small screens)
			Im::TableSetupColumn("Closeness", 0, 0.8f);
			Im::TableSetupColumn("Days ago", 0, 0.7f);
			// fixed width, measured from the longest button pair (Promote + Demote), then Forget + Go to - a stretch column
			// clipped the last buttons
			const auto* imStyle = Im::GetStyle();
			const float actionsWidth = Im::CalcTextSize("Up to: Seeing each other").x + Im::CalcTextSize("Back to: Seeing each other").x +
			                           imStyle->FramePadding.x * 4.0f + imStyle->ItemSpacing.x + imStyle->CellPadding.x * 2.0f;
			Im::TableSetupColumn("Actions", Im::ImGuiTableColumnFlags_WidthFixed, actionsWidth);
			Im::TableHeadersRow();
			int idx = 0;
			for (const auto& p : pairs) {
				if (stageFilter != 0) {
					const int f = filterOf(p);
					if (stageFilter == 6 ? !(f >= 1 && f <= 4) : f != stageFilter) {
						continue;
					}
				}
				const bool        oneSided = p.stage == Ladder::Stage::Crush || p.stage == Ladder::Stage::Thrall;
				const std::string names = oneSided ? Ladder::NpcName(p.activeA ? p.a : p.b) + " -> " + Ladder::NpcName(p.activeA ? p.b : p.a) : Ladder::NpcName(p.a) + " + " + Ladder::NpcName(p.b);
				if (!needle.empty()) {
					std::string lower = names;
					std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (lower.find(needle) == std::string::npos) {
						continue;
					}
				}
				Im::PushID(idx++);
				Im::TableNextRow();
				Im::TableNextColumn();
				Im::TextWrapped("%s", names.c_str());
				std::string notes;
				if (const std::string why = Ladder::ProtectedWhy(p.a, p.b); !why.empty() && p.protectedVanilla) {
					notes += "protected: " + why + "; ";  // 0.21.0
				} else if (p.frozen) {
					notes += "quest-locked; ";
				} else if (p.protectedVanilla) {
					notes += "never splits; ";
				}
				if (p.secret) {
					notes += "secret; ";
				}
				if (p.familyBlock) {
					notes += "family disapproves; ";
				}
				if (p.stage == Ladder::Stage::Affair) {
					notes += p.exposed ? "found out; " : "not found out; ";
				}
				if (p.byPlayer) {
					notes += "set by you; ";
				}
				if (Ladder::IsEndedStage(p.stage) && (p.endedBy == 1 || p.endedBy == 2)) {
					notes += Ladder::NpcName(p.endedBy == 1 ? p.a : p.b) + " ended it; ";  // 0.20.1: here, not in the narrow stage column
				}
				for (const auto who : { p.a, p.b }) {
					const auto style = Ladder::GetStyle(who);
					if (style == Ladder::Style::FreeSpirit) {
						notes += Ladder::NpcName(who) + " is a free spirit; ";
					}
				}
				if (p.cold) {
					notes += "cold; ";
				}
				if (p.home) {
					auto* home = RE::TESForm::LookupByID<RE::BGSLocation>(p.home);
					notes += std::format("home: {}; ", home && home->GetName() && *home->GetName() ? home->GetName() : "?");
					// 0.22.0 households: who moves in (the partner whose editor location is not the home)
					auto* ra = RE::TESForm::LookupByID<RE::Actor>(Ladder::RefOfBase(p.a));
					auto* rb = RE::TESForm::LookupByID<RE::Actor>(Ladder::RefOfBase(p.b));
					if (Settings::Get().households && ra && rb && (p.stage == Ladder::Stage::Married || p.stage == Ladder::Stage::Widowed)) {
						auto*      la = ra->GetEditorLocation();
						auto*      lb = rb->GetEditorLocation();
						const bool aHome = la && la->GetFormID() == p.home;
						const bool bHome = lb && lb->GetFormID() == p.home;
						if (aHome != bHome) {
							auto* mover = aHome ? rb : ra;
							auto* host = aHome ? ra : rb;
							if (!mover->IsDead() && mover->IsGuard()) {  // 0.24.1: Households() never moves a guard or a follower in (ONpcMain)
								notes += std::format("{} stays at their guard post; ", mover->GetDisplayFullName());
							} else if (!mover->IsDead() && mover->IsPlayerTeammate()) {
								notes += std::format("{} is travelling with you; ", mover->GetDisplayFullName());
							} else if (!Papyrus::SameSettlement(mover, host)) {
								notes += "live in different towns; ";
							} else if (!mover->IsDead()) {
								notes += host->IsDead() ? std::format("{} keeps the house; ", mover->GetDisplayFullName())
								                        : std::format("{} lives with {} (evenings); ", mover->GetDisplayFullName(), host->GetDisplayFullName());
							}
						}
					}
				}
				if (p.stage == Ladder::Stage::Engaged && p.weddingDay > p.stageDay) {
					const int in = static_cast<int>(std::floor(p.weddingDay) - std::floor(now));
					notes += in > 0 ? std::format("wedding in {} day(s), afternoon; ", in) : std::string("wedding today, afternoon; ");
				}
				if (p.children) {
					notes += std::format("{} child(ren); ", static_cast<int>(p.children));
				}
				if (Ladder::IsPregnant(p.a) || Ladder::IsPregnant(p.b)) {
					notes += "expecting; ";
				}
				if (const auto babies = Babies::Note(p.a, p.b); !babies.empty()) {
					notes += babies + "; ";  // 0.24.0 baby stages: carried / away: place / at home
				}
				if (p.straysByA || p.straysByB) {
					notes += std::format("strays {}/{}; ", static_cast<int>(p.straysByA), static_cast<int>(p.straysByB));
				}
				if (notes.size() >= 2) {
					notes.resize(notes.size() - 2);
				}
				if (!notes.empty()) {
					Im::PushStyleColor(Im::ImGuiCol_Text, Im::ImVec4{ 0.65f, 0.65f, 0.65f, 1.0f });
					Im::TextWrapped("%s", notes.c_str());
					Im::PopStyleColor();
				}
				if (p.imported && p.stage == Ladder::Stage::Married) {
					bool keep = p.protectedVanilla;  // 0.21.0: a game marriage the player wants to keep (or free)
					if (Im::Checkbox("Keep together", &keep)) {
						Ladder::SetKeepTogether(p.a, p.b, keep);
					}
					Hint("Ticked = this marriage never ends, also when 'Game marriages can end' is on. Quest marriages start ticked.");
				}
				Im::TableNextColumn();
				Im::ImVec4 col{ 0.8f, 0.8f, 0.8f, 1.0f };
				switch (p.stage) {
				case Ladder::Stage::Candidate: col = Im::ImVec4{ 0.6f, 0.85f, 1.0f, 1.0f }; break;
				case Ladder::Stage::Engaged: col = Im::ImVec4{ 1.0f, 0.85f, 0.4f, 1.0f }; break;
				case Ladder::Stage::Married: col = Im::ImVec4{ 0.5f, 1.0f, 0.6f, 1.0f }; break;
				case Ladder::Stage::Jilted:
				case Ladder::Stage::Divorced: col = Im::ImVec4{ 1.0f, 0.5f, 0.5f, 1.0f }; break;
				case Ladder::Stage::Widowed: col = Im::ImVec4{ 0.7f, 0.6f, 0.9f, 1.0f }; break;
				case Ladder::Stage::Affair: col = Im::ImVec4{ 1.0f, 0.6f, 0.3f, 1.0f }; break;
				case Ladder::Stage::Crush: col = Im::ImVec4{ 1.0f, 0.65f, 0.85f, 1.0f }; break;
				case Ladder::Stage::Thrall: col = Im::ImVec4{ 0.65f, 0.55f, 0.75f, 1.0f }; break;
				default: break;
				}
				Im::TextColored(col, "%s", Ladder::PairTitle(p).c_str());
				Im::TableNextColumn();
				Im::TextUnformatted(std::format("{} / {}", p.realEncounters, p.offscreenEncounters).c_str());
				Im::TableNextColumn();
				{
					const char* feel = p.closeness >= 75.0f ? "close" : p.closeness >= 50.0f ? "warm" : p.closeness >= 25.0f ? "cooling" : "fading";
					const Im::ImVec4 tone = p.closeness >= 50.0f ? Im::ImVec4{ 0.5f, 1.0f, 0.6f, 1.0f } : p.closeness >= 25.0f ? Im::ImVec4{ 1.0f, 0.85f, 0.4f, 1.0f } : Im::ImVec4{ 1.0f, 0.5f, 0.5f, 1.0f };
					Im::TextColored(tone, "%s", std::format("{:.0f} {}", p.closeness, feel).c_str());
					Hint(kClosenessHelp);
				}
				Im::TableNextColumn();
				Im::TextUnformatted(std::format("{:.1f}", now - p.lastDay).c_str());
				Im::TableNextColumn();
				Ladder::Stage next = Ladder::Stage::None;
				switch (p.stage) {
				case Ladder::Stage::None:
				case Ladder::Stage::Casual: next = Ladder::Stage::Candidate; break;
				case Ladder::Stage::Candidate: next = Ladder::Stage::Engaged; break;
				case Ladder::Stage::Engaged: next = Ladder::Stage::Married; break;
				default: next = Ladder::Stage::Casual; break;  // jilted, divorced, widowed, affair, crush, thrall: seeing each other
				}
				Ladder::Stage prev = Ladder::Stage::None;
				switch (p.stage) {
				case Ladder::Stage::Married: prev = Ladder::Stage::Engaged; break;
				case Ladder::Stage::Engaged: prev = Ladder::Stage::Candidate; break;
				case Ladder::Stage::Candidate: prev = Ladder::Stage::Casual; break;
				default: prev = Ladder::Stage::None; break;
				}
				const bool canPromote = p.stage != Ladder::Stage::Married;
				const std::string promote = std::string("Up to: ") + Ladder::StageTitle(next);
				const std::string demote = prev == Ladder::Stage::None ? std::string("End it") : std::string("Back to: ") + Ladder::StageTitle(prev);
				Im::BeginDisabled(!canPromote);
				if (Im::Button(promote.c_str())) {
					Ladder::SetStage(p.a, p.b, next);
					Ladder::MarkByPlayer(p.a, p.b);  // 0.19.0: the couple table and quest locks leave it alone from now on
				}
				Im::EndDisabled();
				Im::SameLine();
				if (Im::Button(demote.c_str())) {
					Ladder::SetStage(p.a, p.b, prev);
					Ladder::MarkByPlayer(p.a, p.b);
				}
				if (Im::Button("Forget")) {
					Ladder::EndPair(p.a, p.b);
				}
				Im::SameLine();
				if (Im::Button("Go to")) {
					GoTo(p.a);
				}
				Im::PopID();
			}
			Im::EndTable();
			if (idx == 0) {
				Im::TextColored(Im::ImVec4{ 0.7f, 0.7f, 0.7f, 1.0f }, "%s",
					pairs.empty() ? "No couples yet." : (needle.empty() ? "No couples at this stage." : "No couples match the search."));
			}
			Im::Spacing();
			Im::TextWrapped("%s", "Stages: seeing each other > dating > engaged > married. Jilted, divorced and widowed are what breakups and deaths leave behind. Side relationships: an affair (someone taken, meeting a lover in secret - every meeting is cheating), a crush (one-sided, A -> B) and a thrall (a vampire and the victim they hold). Quest-locked couples stay exactly as the game wrote them until their quest moves them.");
			Im::TextWrapped("%s", kClosenessHelp);
			Im::TextWrapped("%s", "Met = meetings in front of you / off-screen. 'Up to' moves a pair one stage up and runs everything a real step up does (news, letters, ring). 'Back to' moves it one stage back. Forget removes the pair; it can form again.");
		}

		void __stdcall RenderFollowers()
		{
			EnsureDraft();
			CustomFollowersSection();
		}

		void __stdcall RenderNews()
		{
			const auto  entries = Ladder::ChronicleSnapshot();
			const float now = Ladder::DaysNow();
			Im::TextUnformatted("NEWS AND WEDDINGS");
			Im::Separator();
			// upcoming weddings (the same list the register in the Temple of Mara shows)
			const auto weddings = Ladder::WeddingSchedule();
			Im::TextColored(Im::ImVec4{ 1.0f, 0.85f, 0.4f, 1.0f }, "%s", "Upcoming weddings - Temple of Mara, Riften");
			Hint("Real weddings: engaged couples marry at the Temple of Mara in the afternoon (noon to 17:00) of their day. The same list is in the wedding register in the temple.");
			if (weddings.empty()) {
				Im::TextWrapped("%s", Settings::Get().realWeddings ? "No weddings are planned yet." : "No weddings are planned (Real weddings is off - Daily life page).");
			}
			for (const auto& w : weddings) {
				Im::BulletText("%s", w.c_str());
			}
			Im::Spacing();
			Im::Separator();
			Im::TextUnformatted(std::format("The last {} things that happened between NPC couples, newest first. Game day {:.0f} now.", entries.size(), now).c_str());
			Hint("Every stage change (dating, engaged, married, breakups), stray, drift, death and pregnancy, on-screen or simulated off-screen. Kept in the save; only the last 50 are remembered. Shown whatever the Notifications setting says.");
			Im::Spacing();
			if (entries.empty()) {
				Im::TextWrapped("%s", "Nothing yet. News appears once couples start forming (the simulation runs once per game day).");
				return;
			}
			const Im::ImGuiTableFlags flags = Im::ImGuiTableFlags_Borders | Im::ImGuiTableFlags_RowBg | Im::ImGuiTableFlags_SizingStretchProp;
			if (!Im::BeginTable("news", 3, flags)) {
				return;
			}
			Im::TableSetupColumn("Day", 0, 0.5f);
			Im::TableSetupColumn("Days ago", 0, 0.6f);
			Im::TableSetupColumn("What happened", 0, 5.0f);
			Im::TableHeadersRow();
			for (const auto& e : entries) {
				Im::TableNextRow();
				Im::TableNextColumn();
				Im::TextUnformatted(std::format("{:.0f}", e.day).c_str());
				Im::TableNextColumn();
				const float ago = now - e.day;
				Im::TextUnformatted(ago < 1.0f ? "today" : std::format("{:.0f}", ago).c_str());
				Im::TableNextColumn();
				Im::TextUnformatted(e.text.c_str());
			}
			Im::EndTable();
		}
	}

	void Register()
	{
		if (!GetModuleHandleW(L"SKSEMenuFramework")) {
			SKSE::log::warn("SKSE Menu Framework not loaded - no settings page; the INI still applies.");
			return;
		}
		MF::SetSection("ONPCs");
		// what is happening first (lists), then the settings from the big picture down to the details, testing tools last;
		// 0.26.0 alpha: a module's pages only when that module is installed
		using Settings::Installed;
		using Settings::Module;
		const bool rel = Installed(Module::Relationships);
		if (rel) {
			MF::AddSectionItem("Couples", RenderCouples);
		}
		if (Installed(Module::Weddings)) {
			MF::AddSectionItem("News and weddings", RenderNews);
		}
		if (rel) {
			MF::AddSectionItem("Custom followers", RenderFollowers);
			MF::AddSectionItem("Settings: Relationships", RenderRelationships);
		}
		if (Installed(Module::DailyLife)) {
			MF::AddSectionItem("Settings: Daily life", RenderDailyLife);
			MF::AddSectionItem("Settings: Living together", RenderLivingTogether);
		}
		if (rel) {
			MF::AddSectionItem("Settings: Off-screen world", RenderWorld);
		}
		MF::AddSectionItem("Settings: Pregnancy and periods", RenderPregnancy);  // the period skip is the base's
		if (Installed(Module::Weddings)) {
			MF::AddSectionItem("Settings: Messages and letters", RenderLetters);
		}
		MF::AddSectionItem("Settings: Scenes", RenderScenes);
		MF::AddSectionItem("Testing and reset", RenderAdvanced);
		SKSE::log::info("Settings page registered with SKSE Menu Framework.");
	}
}
