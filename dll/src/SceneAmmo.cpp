#include "SceneAmmo.h"

#include "Settings.h"

#include <thread>

namespace ONPCR::SceneAmmo
{
	namespace
	{
		constexpr RE::FormID kExcitementFaction = 0xD93;  // OStim.esp OStimExcitementFaction: every actor in a running scene
		constexpr int        kMaxBlocks = 30;             // re-equips taken off per actor per scene before we give up (no equip war)

		struct Entry
		{
			std::int32_t thread{ -1 };
			RE::FormID   ammo{ 0 };  // what they wore when the scene started (0 = none)
			int          blocks{ 0 };
		};

		std::mutex                             lock;
		std::unordered_map<RE::FormID, Entry>  actors;   // actor reference ID -> entry
		std::unordered_set<std::int32_t>       threads;  // running OStim threads we saw start

		RE::TESFaction* ExcitementFaction()
		{
			static RE::TESFaction* faction = [] {
				auto* dh = RE::TESDataHandler::GetSingleton();
				return dh ? dh->LookupForm<RE::TESFaction>(kExcitementFaction, "OStim.esp") : nullptr;
			}();
			return faction;
		}

		bool Enabled() { return Settings::Get().sceneAmmoOff; }

		void TakeOff(RE::Actor* a_actor, RE::TESAmmo* a_ammo)
		{
			if (auto* eq = RE::ActorEquipManager::GetSingleton()) {
				eq->UnequipObject(a_actor, a_ammo);
			}
		}

		// game thread: everyone in a scene right now who is not tracked yet joins this thread
		void Scan(std::int32_t a_thread)
		{
			auto* faction = ExcitementFaction();
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!faction || !lists) {
				return;
			}
			std::vector<RE::Actor*> candidates;
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				candidates.push_back(player);
			}
			for (auto& h : lists->highActorHandles) {
				if (auto a = h.get(); a) {
					candidates.push_back(a.get());
				}
			}
			for (auto* actor : candidates) {
				if (!actor || actor->IsDead() || !actor->IsInFaction(faction)) {
					continue;
				}
				auto* ammo = actor->GetCurrentAmmo();
				{
					std::scoped_lock l(lock);
					if (!threads.contains(a_thread) || actors.contains(actor->GetFormID())) {
						continue;
					}
					actors[actor->GetFormID()] = Entry{ a_thread, ammo ? ammo->GetFormID() : 0, 0 };
				}
				if (ammo) {
					SKSE::log::info("SceneAmmo: {:08X} '{}' ammo {:08X} off (thread {})", actor->GetFormID(), actor->GetDisplayFullName(), ammo->GetFormID(), a_thread);
					TakeOff(actor, ammo);
				}
			}
		}

		// game thread: the thread ended - its actors get their ammo back
		void Restore(std::int32_t a_thread)
		{
			std::vector<std::pair<RE::FormID, RE::FormID>> back;
			{
				std::scoped_lock l(lock);
				threads.erase(a_thread);
				for (auto it = actors.begin(); it != actors.end();) {
					if (it->second.thread == a_thread) {
						back.emplace_back(it->first, it->second.ammo);
						it = actors.erase(it);
					} else {
						++it;
					}
				}
			}
			auto* eq = RE::ActorEquipManager::GetSingleton();
			for (const auto& [refID, ammoID] : back) {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(refID);
				auto* ammo = ammoID ? RE::TESForm::LookupByID<RE::TESAmmo>(ammoID) : nullptr;
				if (!actor || !ammo || !eq || actor->IsDead() || actor->GetCurrentAmmo() == ammo) {
					continue;
				}
				const auto counts = actor->GetInventoryCounts([ammo](RE::TESBoundObject& a_obj) { return &a_obj == ammo; });
				const auto found = counts.find(ammo);
				if (found == counts.end() || found->second <= 0) {
					continue;  // used up or taken away during the scene
				}
				SKSE::log::info("SceneAmmo: {:08X} '{}' ammo {:08X} x{} back on (thread {})", refID, actor->GetDisplayFullName(), ammoID, found->second, a_thread);
				eq->EquipObject(actor, ammo, nullptr, static_cast<std::uint32_t>(found->second));
			}
		}

		void Queue(std::function<void()> a_task)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask(std::move(a_task));
			}
		}

		class ThreadSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const std::string_view name = a_event->eventName.c_str() ? a_event->eventName.c_str() : "";
				const auto thread = static_cast<std::int32_t>(a_event->numArg);
				if (name == "ostim_thread_start") {
					if (!Enabled()) {
						return RE::BSEventNotifyControl::kContinue;
					}
					{
						std::scoped_lock l(lock);
						threads.insert(thread);
					}
					Queue([thread] { Scan(thread); });
					// actors may join the excitement faction a moment after the event: look once more, paced from our own thread
					std::thread([thread] {
						std::this_thread::sleep_for(2s);
						Queue([thread] { Scan(thread); });
					}).detach();
				} else if (name == "ostim_thread_end") {
					Queue([thread] { Restore(thread); });
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class EquipSink final : public RE::BSTEventSink<RE::TESEquipEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
			{
				if (!a_event || !a_event->equipped || !a_event->actor) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const RE::FormID refID = a_event->actor->GetFormID();
				const RE::FormID objID = a_event->baseObject;
				{
					std::scoped_lock l(lock);
					auto it = actors.find(refID);
					if (it == actors.end() || it->second.blocks >= kMaxBlocks) {
						return RE::BSEventNotifyControl::kContinue;
					}
				}
				Queue([refID, objID] {
					auto* actor = RE::TESForm::LookupByID<RE::Actor>(refID);
					auto* ammo = RE::TESForm::LookupByID<RE::TESAmmo>(objID);
					if (!actor || !ammo || actor->IsInCombat() || actor->GetCurrentAmmo() != ammo) {
						return;
					}
					int blocks = 0;
					{
						std::scoped_lock l(lock);
						auto it = actors.find(refID);
						if (it == actors.end()) {
							return;  // the scene ended in the meantime
						}
						blocks = ++it->second.blocks;
						if (!it->second.ammo) {
							it->second.ammo = objID;  // had none at the start: give this one back at the end
						}
					}
					SKSE::log::info("SceneAmmo: {:08X} '{}' re-equipped ammo {:08X} in a scene - off again ({}/{})", refID, actor->GetDisplayFullName(), objID, blocks, kMaxBlocks);
					TakeOff(actor, ammo);
				});
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Register()
	{
		static ThreadSink threadSink;
		static EquipSink  equipSink;
		if (auto* modEvents = SKSE::GetModCallbackEventSource()) {
			modEvents->AddEventSink(&threadSink);
		}
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESEquipEvent>(&equipSink);
		}
		SKSE::log::info("SceneAmmo: {} (OStim excitement faction {})", Enabled() ? "on" : "off", ExcitementFaction() ? "found" : "NOT FOUND - ammo stays on");
	}

	void Reset()
	{
		std::scoped_lock l(lock);
		actors.clear();
		threads.clear();
	}
}
