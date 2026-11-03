#include "Liita.h"

#include "Ladder.h"

#undef AddForm  // windows.h (winspool) maps it to AddFormW, hiding BGSListForm::AddForm

namespace ONPCR::Liita
{
	namespace
	{
		constexpr const char* kPlugin = "LIITA.esp";
		constexpr RE::FormID  kExcludedList = 0x000100;   // LIITA_ExcludedNpcs
		constexpr RE::FormID  kMarriedFaction = 0x000D62;  // LIITA_Faction
		constexpr RE::FormID  kWeddingGlobal = 0x000D63;   // LIITA_GlobalWIP (a wedding is in progress)

		struct Forms
		{
			RE::BGSListForm* list{ nullptr };
			RE::TESFaction*  married{ nullptr };
			RE::TESGlobal*   wedding{ nullptr };
		};

		// looked up once (forms never move after data load); all null without LIITA.esp
		const Forms& Get()
		{
			static const Forms forms = [] {
				Forms f;
				if (auto* dh = RE::TESDataHandler::GetSingleton(); dh && dh->LookupModByName(kPlugin)) {
					f.list = dh->LookupForm<RE::BGSListForm>(kExcludedList, kPlugin);
					f.married = dh->LookupForm<RE::TESFaction>(kMarriedFaction, kPlugin);
					f.wedding = dh->LookupForm<RE::TESGlobal>(kWeddingGlobal, kPlugin);
					SKSE::log::info("Love Is In The Air found - exclusion list {}, married faction {}, wedding flag {}", f.list != nullptr,
						f.married != nullptr, f.wedding != nullptr);
				}
				return f;
			}();
			return forms;
		}

		std::mutex                     guard;
		std::unordered_set<RE::FormID> added;  // forms we put into the exclusion list (refs and bases)

		// the list's runtime additions (what Papyrus FormList.AddForm / RemoveAddedForm work on); the forms LIITA.esp itself ships
		// are in list->forms and never touched
		void RemoveAdded(RE::BGSListForm* a_list, RE::FormID a_id)
		{
			auto* arr = a_list->scriptAddedTempForms;
			if (!arr) {
				return;
			}
			for (auto it = arr->begin(); it != arr->end(); ++it) {
				if (*it == a_id) {
					arr->erase(it);
					if (a_list->scriptAddedFormCount > 0) {
						--a_list->scriptAddedFormCount;
					}
					return;
				}
			}
		}

		bool Live(Ladder::Stage a_s)
		{
			return a_s == Ladder::Stage::Casual || a_s == Ladder::Stage::Candidate || a_s == Ladder::Stage::Engaged;
		}
	}

	bool Present()
	{
		return Get().list != nullptr;
	}

	bool IsMarried(RE::Actor* a_actor)
	{
		const auto* f = Get().married;
		return f && a_actor && a_actor->IsInFaction(f);
	}

	bool WeddingBusy()
	{
		const auto* g = Get().wedding;
		return g && g->value != 0.0f;
	}

	void Sync()
	{
		auto* list = Get().list;
		if (!list) {
			return;
		}
		std::unordered_set<RE::FormID>                   want;
		std::vector<std::pair<RE::FormID, RE::FormID>> lost;
		for (const auto& p : Ladder::Snapshot()) {
			if (!Live(p.stage) && p.stage != Ladder::Stage::Married) {
				continue;
			}
			const RE::FormID refA = Ladder::RefOfBase(p.a);
			const RE::FormID refB = Ladder::RefOfBase(p.b);
			auto*            actA = refA ? RE::TESForm::LookupByID<RE::Actor>(refA) : nullptr;
			auto*            actB = refB ? RE::TESForm::LookupByID<RE::Actor>(refB) : nullptr;
			// one of them married someone else through Love Is In The Air: our (not yet married) pair is over
			if (Live(p.stage) && !p.protectedVanilla && (IsMarried(actA) || IsMarried(actB))) {
				lost.emplace_back(p.a, p.b);
				continue;
			}
			if (!p.IsCommitted()) {
				continue;  // casual pairs are not exclusive
			}
			for (const auto id : { p.a, p.b, refA, refB }) {
				if (id) {
					want.insert(id);
				}
			}
		}
		for (const auto& [a, b] : lost) {
			Ladder::EndForOtherMarriage(a, b);
		}
		std::scoped_lock lock(guard);
		int              in = 0, out = 0;
		for (const auto id : want) {
			if (added.contains(id)) {
				continue;
			}
			auto* form = RE::TESForm::LookupByID(id);
			if (!form || list->HasForm(form)) {
				continue;  // already listed by LIITA itself (its ini excludes, its own couples): not ours to remove later
			}
			list->AddForm(form);
			added.insert(id);
			++in;
		}
		for (auto it = added.begin(); it != added.end();) {
			if (want.contains(*it)) {
				++it;
				continue;
			}
			RemoveAdded(list, *it);
			it = added.erase(it);
			++out;
		}
		if (in || out) {
			SKSE::log::info("Love Is In The Air: {} added to / {} taken off its exclusion list ({} of ours listed)", in, out, added.size());
		}
	}

	std::vector<RE::FormID> Added()
	{
		std::scoped_lock lock(guard);
		return { added.begin(), added.end() };
	}

	void SetAdded(std::vector<RE::FormID> a_ids)
	{
		std::scoped_lock lock(guard);
		added = std::unordered_set<RE::FormID>(a_ids.begin(), a_ids.end());
		added.erase(0);
	}
}
