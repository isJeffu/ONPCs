#pragma once

namespace ONPCR::SceneAmmo
{
	// 0.20.2: arrows and bolts off in OStim scenes, back on after.
	// OStim's "remove weapons" takes ammo off too, but followers (Auri, Aela, Faendal...) put their
	// quiver straight back on. On ostim_thread_start every actor in the scene (OStimExcitementFaction)
	// has its ammo taken off and remembered; any ammo they equip while the scene runs is taken off
	// again; on ostim_thread_end the remembered ammo goes back on. Ini [General] SceneAmmoOff (default on).
	void Register();  // after kDataLoaded
	void Reset();     // new game / load: forget everything (scenes do not survive a load)
}
