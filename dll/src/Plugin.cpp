#include "Plugin.h"

#include "Amulet.h"
#include "Babies.h"
#include "Base.h"
#include "Ladder.h"
#include "Menu.h"
#include "Simulation.h"
#include "Conversation.h"
#include "Papyrus.h"
#include "Settings.h"
#include "SceneAmmo.h"

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}
		switch (a_message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
		{
			ONPCR::Settings::DetectModules();  // first: every feature below checks which modules are installed
			using ONPCR::Settings::Installed;
			using ONPCR::Settings::Module;
			// a module's own data files only when the module is installed (the base ships none of them)
			if (Installed(Module::Weddings)) {
				ONPCR::Amulet::LoadConfig();
			}
			if (Installed(Module::Relationships)) {
				ONPCR::Simulation::LoadExclusions();
				ONPCR::Ladder::LoadProtectedCouples();
				ONPCR::Ladder::LoadPlayerPartnerFactions();
			}
			if (Installed(Module::Conversations)) {
				ONPCR::Conversation::Load();
			}
			if (Installed(Module::Families)) {
				ONPCR::Babies::LoadReasons();
			}
			ONPCR::Menu::Register();
			ONPCR::SceneAmmo::Register();
			ONPCR::Base::OnDataLoaded();
			break;
		}
		case SKSE::MessagingInterface::kPreLoadGame:
			ONPCR::SceneAmmo::Reset();
			ONPCR::Base::Reset();
			ONPCR::Simulation::RequestRebuild();  // 0.21.1: the pool (dead / teammate / excluded flags, refs) is rebuilt for the save being loaded
			break;
		case SKSE::MessagingInterface::kNewGame:
			ONPCR::SceneAmmo::Reset();
			ONPCR::Base::Reset();  // 0.26.0: a new character starts with no past lovers from the last save
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([] {
					ONPCR::Ladder::InstallRomanceConditions();
					ONPCR::Base::OnPostLoad();
				});
			}
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([] {
					// 0.19.0: their own romance lines only while the couple is together - once per session, when the NPCs are placed
					ONPCR::Ladder::InstallRomanceConditions();
					ONPCR::Simulation::EnsurePool();
					ONPCR::Base::OnPostLoad();  // 0.26.0: map unique NPCs to their references once, at load, not mid-scan
					ONPCR::Amulet::ReassertAll(ONPCR::Ladder::MarriedPairRefs());
					ONPCR::Amulet::ReassertAllGame(ONPCR::Ladder::GameMarriedPairRefs());
				});
			}
			break;
		default:
			break;
		}
	}
}

SKSEPluginInfo(
	.Version = { 0, 7, 0, 0 },
	.Name = "ONPCs"sv,
	.Author = "LSJeff"sv,
	.RuntimeCompatibility = SKSE::PluginDeclaration::RuntimeCompatibility(SKSE::VersionIndependence::AddressLibrary))

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SKSE::log::info("{} {} (build {}) loading.", Plugin::DISPLAY_NAME, Plugin::PUBLIC_VERSION, Plugin::VERSION.string("."));

	if (REL::Module::IsVR()) {
		SKSE::log::error("Skyrim VR is not supported.");
		return false;
	}

	ONPCR::Settings::Load();
	ONPCR::Ladder::Init();
	ONPCR::Papyrus::Register();
	ONPCR::Base::Register();

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		SKSE::log::error("Messaging interface unavailable.");
		return false;
	}
	return true;
}
