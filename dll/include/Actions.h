#pragma once

namespace ONPCR::Actions
{
	// Gives the player (or takes away) the two match-maker spells from ONPCs.esp.
	// Runs on the game thread through the SKSE task interface.
	void SetMatchMakerSpells(bool a_add);
}
