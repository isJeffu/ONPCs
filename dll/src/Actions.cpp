#include "Actions.h"

namespace ONPCR::Actions
{
	namespace
	{
		constexpr auto kPlugin = "ONPCs.esp"sv;
		constexpr RE::FormID kSpellTarget = 0x80F;  // ONPCsMatchMakerTarget
		constexpr RE::FormID kSpellSelf = 0x810;    // ONPCsMatchMakerSelf
	}

	void SetMatchMakerSpells(bool a_add)
	{
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			return;
		}
		tasks->AddTask([a_add] {
			auto* data = RE::TESDataHandler::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!data || !player) {
				return;
			}
			int changed = 0;
			for (const auto id : { kSpellTarget, kSpellSelf }) {
				auto* spell = data->LookupForm<RE::SpellItem>(id, kPlugin);
				if (!spell) {
					SKSE::log::warn("Match-maker spell {:X} not found in {}", id, kPlugin);
					continue;
				}
				if (a_add && !player->HasSpell(spell)) {
					player->AddSpell(spell);
					++changed;
				} else if (!a_add && player->HasSpell(spell)) {
					player->RemoveSpell(spell);
					++changed;
				}
			}
			RE::SendHUDMessage::ShowHUDMessage(a_add ? "Match-maker spells added" : "Match-maker spells removed");
			SKSE::log::info("Match-maker spells {}: {} changed", a_add ? "added" : "removed", changed);
		});
	}
}
