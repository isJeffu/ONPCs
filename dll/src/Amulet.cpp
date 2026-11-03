#include "Amulet.h"

#include "Settings.h"

#include <nlohmann/json.hpp>

namespace ONPCR::Amulet
{
	namespace
	{
		constexpr RE::FormID kPotentialMarriageFaction = 0x00019809;
		constexpr RE::FormID kVanillaWeddingRing = 0x000C5809;  // The Bond of Matrimony (Skyrim.esm)

		std::mutex                  guard;
		std::vector<RE::TESBoundObject*> amulets;
		std::vector<RE::SpellItem*>      giverSpells;
		std::vector<RE::TESFaction*>     ignoreFactions;
		RE::TESBoundObject*              ring{ nullptr };
		RE::TESBoundObject*              ownAmulet{ nullptr };  // the non-lootable Amulet of Mara this mod hands out itself
		std::vector<RE::FormID>          given;  // refs this mod gave an amulet to (saved by Ladder)

		RE::TESForm* Resolve(const std::string& a_spec)
		{
			// "Plugin.esp|0x123"
			const auto bar = a_spec.find('|');
			if (bar == std::string::npos) {
				return nullptr;
			}
			const std::string plugin = a_spec.substr(0, bar);
			std::string       idText = a_spec.substr(bar + 1);
			if (idText.rfind("0x", 0) == 0 || idText.rfind("0X", 0) == 0) {
				idText = idText.substr(2);
			}
			RE::FormID local = 0;
			try {
				local = static_cast<RE::FormID>(std::stoul(idText, nullptr, 16));
			} catch (...) {
				return nullptr;
			}
			auto* data = RE::TESDataHandler::GetSingleton();
			return data ? data->LookupForm(local, plugin) : nullptr;
		}

		std::string ConfigPath()
		{
			std::error_code ec;
			return (std::filesystem::current_path(ec) / "Data" / "SKSE" / "Plugins" / "OStimNPCsReforged_Amulets.json").string();
		}

		void Defaults(nlohmann::json& a_j)
		{
			a_j["amulets"] = { "Skyrim.esm|0xC891B", "BSHeartland.esm|0xED24B", "OStimNPCsReforged - Letters.esp|0x806" };
			a_j["giverSpells"] = { "NPCs_Wear_Amulets_Of_Mara.esp|0x808" };
			a_j["ignoreFactions"] = { "Skyrim.esm|0xA2526" };  // RiftenTempleofMaraFaction
			a_j["ring"] = "OStimNPCsReforged - Letters.esp|0x805";       // non-lootable copy of the Bond of Matrimony
			a_j["ownAmulet"] = "OStimNPCsReforged - Letters.esp|0x806";  // non-lootable copy of the Amulet of Mara
		}

		RE::Actor* ActorOf(RE::FormID a_ref)
		{
			return a_ref ? RE::TESForm::LookupByID<RE::Actor>(a_ref) : nullptr;
		}

		void StripAmulets(RE::Actor* a_actor)
		{
			auto* equip = RE::ActorEquipManager::GetSingleton();
			for (auto* amulet : amulets) {
				if (!amulet) {
					continue;
				}
				if (auto* worn = a_actor->GetWornArmor(amulet->GetFormID()); worn && equip) {
					equip->UnequipObject(a_actor, worn);
				}
				a_actor->RemoveItem(amulet, 99, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			for (auto* spell : giverSpells) {
				if (spell && a_actor->HasSpell(spell)) {
					a_actor->RemoveSpell(spell);
				}
			}
		}

		void MarriedState(RE::Actor* a_actor, bool a_gameCouple = false)
		{
			if (!a_actor) {
				return;
			}
			StripAmulets(a_actor);
			// 0.17.0: a couple taken over from the game may already wear a ring - then they keep it and we add none
			const bool hasOwnRing = a_gameCouple && (a_actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kRing) || a_actor->GetWornArmor(kVanillaWeddingRing));
			if (ring && !hasOwnRing) {
				if (!a_actor->GetWornArmor(ring->GetFormID())) {
					a_actor->AddObjectToContainer(ring, nullptr, 1, nullptr);
					if (auto* equip = RE::ActorEquipManager::GetSingleton()) {
						// applied now and forced: a queued, unforced equip was dropped by the NPC's outfit logic (0.7.x test)
						equip->EquipObject(a_actor, ring, nullptr, 1, nullptr, false, true, false, true);
					}
				}
				SKSE::log::info("Amulet: {} married - ring {}, 3D {}", a_actor->GetDisplayFullName(), a_actor->GetWornArmor(ring->GetFormID()) ? "worn" : "NOT worn", a_actor->Is3DLoaded() ? "loaded" : "not loaded");
			}
			if (auto* f = RE::TESForm::LookupByID<RE::TESFaction>(kPotentialMarriageFaction); f && a_actor->IsInFaction(f)) {
				a_actor->RemoveFromFaction(f);
			}
		}

		void SingleState(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return;
			}
			if (ring) {
				a_actor->RemoveItem(ring, 99, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			if (auto* f = RE::TESForm::LookupByID<RE::TESFaction>(kPotentialMarriageFaction); f && !a_actor->IsInFaction(f)) {
				a_actor->AddToFaction(f, 0);
			}
		}
	}

	void LoadConfig()
	{
		nlohmann::json j;
		const auto     path = ConfigPath();
		std::ifstream  in(path);
		if (in) {
			try {
				in >> j;
			} catch (const std::exception& e) {
				SKSE::log::error("Amulet: {} is not valid JSON ({}) - using defaults", path, e.what());
				j = nlohmann::json::object();
			}
		}
		if (!j.contains("amulets")) {
			Defaults(j);
			std::ofstream out(path);
			if (out) {
				out << j.dump(2);
				SKSE::log::info("Amulet: wrote default config {}", path);
			}
		}
		std::scoped_lock lock(guard);
		amulets.clear();
		giverSpells.clear();
		ignoreFactions.clear();
		ring = nullptr;
		if (j.contains("ring") && j["ring"].is_string()) {
			if (auto* f = Resolve(j["ring"].get<std::string>()); f) {
				ring = f->As<RE::TESBoundObject>();
			}
		}
		// our non-lootable ring copies the slot and models of the list's own Bond of Matrimony, so it sits and looks like it
		// (lists move rings to other slots - this one wears rings on slot 50 with a left-hand model)
		if (auto* own = ring ? ring->As<RE::TESObjectARMO>() : nullptr) {
			auto* vanilla = RE::TESForm::LookupByID<RE::TESObjectARMO>(kVanillaWeddingRing);
			if (vanilla && vanilla != own && !vanilla->armorAddons.empty()) {
				own->bipedModelData.bipedObjectSlots = vanilla->bipedModelData.bipedObjectSlots;
				own->armorAddons.clear();
				for (auto* aa : vanilla->armorAddons) {
					own->armorAddons.push_back(aa);
				}
				SKSE::log::info("Amulet: wedding ring takes slot mask {:X} and {} model(s) from the list's Bond of Matrimony", vanilla->bipedModelData.bipedObjectSlots.underlying(), own->armorAddons.size());
			}
		}
		ownAmulet = nullptr;
		if (j.contains("ownAmulet") && j["ownAmulet"].is_string()) {
			if (auto* f = Resolve(j["ownAmulet"].get<std::string>()); f) {
				ownAmulet = f->As<RE::TESBoundObject>();
			}
		}
		for (const auto& s : j.value("amulets", nlohmann::json::array())) {
			if (auto* f = Resolve(s.get<std::string>()); f) {
				if (auto* bound = f->As<RE::TESBoundObject>()) {
					amulets.push_back(bound);
				}
			}
		}
		for (const auto& s : j.value("giverSpells", nlohmann::json::array())) {
			if (auto* f = Resolve(s.get<std::string>()); f) {
				if (auto* spell = f->As<RE::SpellItem>()) {
					giverSpells.push_back(spell);
				}
			}
		}
		for (const auto& s : j.value("ignoreFactions", nlohmann::json::array())) {
			if (auto* f = Resolve(s.get<std::string>()); f) {
				if (auto* fac = f->As<RE::TESFaction>()) {
					ignoreFactions.push_back(fac);
				}
			}
		}
		SKSE::log::info("Amulet: {} amulet form(s), {} giver spell(s), {} ignored faction(s), ring {}, own amulet {}", amulets.size(), giverSpells.size(), ignoreFactions.size(), ring ? "ok" : "MISSING", ownAmulet ? "ok" : "MISSING");
	}

	bool IsLooking(RE::Actor* a_actor)
	{
		if (!a_actor || !Settings::Get().amuletEnabled) {
			return false;
		}
		std::scoped_lock lock(guard);
		for (auto* fac : ignoreFactions) {
			if (fac && a_actor->IsInFaction(fac)) {
				return false;
			}
		}
		auto* worn = a_actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kAmulet);
		if (!worn) {
			return false;
		}
		for (auto* amulet : amulets) {
			if (amulet && amulet->GetFormID() == worn->GetFormID()) {
				return true;
			}
		}
		return false;
	}

	void OnMarried(RE::FormID a_refA, RE::FormID a_refB)
	{
		if (!Settings::Get().amuletEnabled || !Settings::Get().manageMarriedAmulets) {
			return;
		}
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([a_refA, a_refB] {
				std::scoped_lock lock(guard);
				MarriedState(ActorOf(a_refA));
				MarriedState(ActorOf(a_refB));
			});
		}
	}

	void OnUnmarried(RE::FormID a_refA, RE::FormID a_refB)
	{
		if (!Settings::Get().amuletEnabled || !Settings::Get().manageMarriedAmulets) {
			return;
		}
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([a_refA, a_refB] {
				SingleState(ActorOf(a_refA));
				SingleState(ActorOf(a_refB));
			});
		}
	}

	void OnMarriedGame(RE::FormID a_refA, RE::FormID a_refB)
	{
		ReassertAllGame({ { a_refA, a_refB } });
	}

	void ReassertAllGame(const std::vector<std::pair<RE::FormID, RE::FormID>>& a_marriedRefs)
	{
		if (!Settings::Get().amuletEnabled || !Settings::Get().manageMarriedAmulets || a_marriedRefs.empty()) {
			return;
		}
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([a_marriedRefs] {
				std::scoped_lock lock(guard);
				for (const auto& [a, b] : a_marriedRefs) {
					MarriedState(ActorOf(a), true);
					MarriedState(ActorOf(b), true);
				}
			});
		}
	}

	void EnsureMarried(RE::FormID a_ref, bool a_gameCouple)
	{
		if (!Settings::Get().amuletEnabled || !Settings::Get().manageMarriedAmulets) {
			return;
		}
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([a_ref, a_gameCouple] {
				std::scoped_lock lock(guard);
				auto* actor = ActorOf(a_ref);
				if (actor && ring && actor->Is3DLoaded() && !actor->GetWornArmor(ring->GetFormID())) {
					MarriedState(actor, a_gameCouple);
				}
			});
		}
	}

	void ReassertAll(const std::vector<std::pair<RE::FormID, RE::FormID>>& a_marriedRefs)
	{
		if (!Settings::Get().amuletEnabled || !Settings::Get().manageMarriedAmulets || a_marriedRefs.empty()) {
			return;
		}
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([a_marriedRefs] {
				std::scoped_lock lock(guard);
				for (const auto& [a, b] : a_marriedRefs) {
					MarriedState(ActorOf(a));
					MarriedState(ActorOf(b));
				}
			});
		}
	}
	bool GiverModPresent()
	{
		std::scoped_lock lock(guard);
		return !giverSpells.empty();
	}

	bool CanWearOwnAmulet(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		std::scoped_lock lock(guard);
		for (auto* fac : ignoreFactions) {
			if (fac && a_actor->IsInFaction(fac)) {
				return false;
			}
		}
		auto* worn = a_actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kAmulet);
		if (!worn) {
			return true;
		}
		return worn->GetGoldValue() < 200;  // a cheap necklace may be swapped; keep anything valuable
	}

	void GiveAmulet(RE::FormID a_ref)
	{
		{
			std::scoped_lock lock(guard);
			if (!ownAmulet && (amulets.empty() || !amulets.front())) {
				return;
			}
			if (std::find(given.begin(), given.end(), a_ref) == given.end()) {
				given.push_back(a_ref);
			}
		}
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([a_ref] {
				auto* actor = ActorOf(a_ref);
				RE::TESBoundObject* amulet = nullptr;
				{
					std::scoped_lock lock(guard);
					amulet = ownAmulet ? ownAmulet : (amulets.empty() ? nullptr : amulets.front());
				}
				if (!actor || !amulet) {
					return;
				}
				actor->AddObjectToContainer(amulet, nullptr, 1, nullptr);
				if (auto* equip = RE::ActorEquipManager::GetSingleton()) {
					equip->EquipObject(actor, amulet);
				}
			});
		}
	}

	void TakeBackAmulet(RE::FormID a_ref)
	{
		{
			std::scoped_lock lock(guard);
			auto it = std::find(given.begin(), given.end(), a_ref);
			if (it == given.end()) {
				return;
			}
			given.erase(it);
		}
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([a_ref] {
				if (auto* actor = ActorOf(a_ref)) {
					std::scoped_lock lock(guard);
					StripAmulets(actor);
				}
			});
		}
	}

	bool GaveAmulet(RE::FormID a_ref)
	{
		std::scoped_lock lock(guard);
		return std::find(given.begin(), given.end(), a_ref) != given.end();
	}

	std::vector<RE::FormID> GivenRefs()
	{
		std::scoped_lock lock(guard);
		return given;
	}

	void SetGivenRefs(std::vector<RE::FormID> a_refs)
	{
		std::scoped_lock lock(guard);
		given = std::move(a_refs);
	}
}