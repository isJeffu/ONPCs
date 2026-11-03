Scriptname ONPCR_BeeingFemaleBridge Hidden
{OStim NPCs Reforged - the only script that calls Beeing Female NG's own scripts. ONpcMain calls it only when BeeingFemale.esm
 is loaded, so the mod works without Beeing Female (this script simply never loads then).
 0.21.4 (handoff 15): with "baby item" births, Beeing Female turns a carried baby into a child only when the PLAYER is the father
 (FWAbilityBeeingFemale, NPC-mother branch). This hatches the babies of two NPCs on Beeing Female's own clock, with its own functions.
 Compiled against signature-only stubs (tools\papyrus-stubs\BeeingFemale) - Beeing Female's sources do not compile in this list.}

; the mothers to look at: Beeing Female's tracked women, re-read once a game day. Beeing Female drops idle women from its list
; after a while, so a woman who still carries a baby stays on ours until her last baby is gone.
Function RefreshMothers(Actor player, float now) global
	if now - StorageUtil.GetFloatValue(None, "ONPCR.BabyMothersRead", -99.0) >= 1.0
		StorageUtil.SetFloatValue(None, "ONPCR.BabyMothersRead", now)
		int s = StorageUtil.FormListCount(None, "FW.SavedNPCs")
		while s > 0
			s -= 1
			Actor w = StorageUtil.FormListGet(None, "FW.SavedNPCs", s) as Actor
			if w && w != player && StorageUtil.FormListCount(w, "FW.BabyItemArmor") > 0
				StorageUtil.FormListAdd(None, "ONPCR.BabyMothers", w, false)
			endif
		endwhile
	endif
EndFunction

; once per game hour from ONpcMain.HatchNpcBabies; returns how many babies hatched
int Function HatchNpcBabies(Actor player) global
	FWSystem sys = Game.GetFormFromFile(0x000D62, "BeeingFemale.esm") as FWSystem
	if !sys
		return 0
	endif
	FWAddOnManager mgr = Game.GetFormFromFile(0x001829, "BeeingFemale.esm") as FWAddOnManager
	float now = Utility.GetCurrentGameTime()
	RefreshMothers(player, now)
	int hatched = 0
	int i = StorageUtil.FormListCount(None, "ONPCR.BabyMothers")
	while i > 0
		i -= 1
		Actor m = StorageUtil.FormListGet(None, "ONPCR.BabyMothers", i) as Actor
		if !m || m.IsDead() || StorageUtil.FormListCount(m, "FW.BabyItemArmor") == 0
			StorageUtil.FormListRemoveAt(None, "ONPCR.BabyMothers", i)
		elseif m.Is3DLoaded()		; like Beeing Female: an unloaded actor's inventory read is not reliable
			if HatchOne(sys, mgr, m, player, now)
				hatched += 1
			endif
		endif
	endwhile
	return hatched
EndFunction

; the oldest due baby whose father is not the player (the player's babies are Beeing Female's own); one per mother per pass
bool Function HatchOne(FWSystem sys, FWAddOnManager mgr, Actor m, Actor player, float now) global
	FWUtility.PruneOrphanBabyIdentities(m)		; sold / dropped baby items leave no orphan entry (Beeing Female does the same first)
	float due = 723.0		; fallback when Beeing Female's add-on manager cannot be reached
	if mgr
		due = mgr.ActorCustomMatureTimeInHours(m) / 24.0 / 5.0		; Beeing Female's own baby-item time
	endif
	int n = StorageUtil.FormListCount(m, "FW.BabyItemArmor")
	int idx = 0
	while idx < n
		Actor father = StorageUtil.FormListGet(m, "FW.BabyItemFather", idx) as Actor
		Armor arm = StorageUtil.FormListGet(m, "FW.BabyItemArmor", idx) as Armor
		float dob = StorageUtil.FloatListGet(m, "FW.BabyItemDOB", idx)
		if father != player && arm && dob > 0.0 && now - dob >= due && (m.IsEquipped(arm) || m.GetItemCount(arm) > 0)
			return Hatch(sys, m, father, arm, dob, idx, now)
		endif
		idx += 1
	endwhile
	return false
EndFunction

; the same steps as Beeing Female's ProcessBabyItemTransitionToChild, through its own functions: the child keeps the name, sex and
; race recorded at birth and stays with the mother (bMothersChild)
bool Function Hatch(FWSystem sys, Actor m, Actor father, Armor arm, float dob, int idx, float now) global
	; Beeing Female's per-mother hatch claim (a spawn takes seconds): never two hatches of the same baby
	float nowReal = Utility.GetCurrentRealTime()
	float claim = StorageUtil.GetFloatValue(m, "FW.BabyItemHatching", 0.0)
	if claim > 0.0 && nowReal >= claim && nowReal - claim < 600.0
		return false
	endif
	StorageUtil.SetFloatValue(m, "FW.BabyItemHatching", nowReal)
	int sex = StorageUtil.IntListGet(m, "FW.BabyItemSex", idx)
	string babyName = StorageUtil.StringListGet(m, "FW.BabyItemName", idx)
	Race babyRace = StorageUtil.FormListGet(m, "FW.BabyItemRace", idx) as Race
	Actor child = sys.SpawnChildActor(m, father, babyRace, sex, babyName, true)
	bool ok = child != None
	if ok
		FWUtility.RemoveBabyItemIdentityAt(m, idx)
		StorageUtil.FormListAdd(None, "FW.Babys", child, false)		; Beeing Female's Children tab lists it
		m.UnequipItem(arm)
		m.RemoveItem(arm, 1, true)
		FWUtility.SendChildSpawnedEvent(m, father, child, babyName)
		Debug.Trace("ONPCR: hatched " + m.GetDisplayName() + "'s baby after " + ((now - dob) as int) + " days (" + child.GetDisplayName() + ")")
	else
		Debug.Trace("ONPCR: could not hatch " + m.GetDisplayName() + "'s baby (Beeing Female found no child body) - trying again next hour")
	endif
	StorageUtil.UnsetFloatValue(m, "FW.BabyItemHatching")
	return ok
EndFunction

; ---- 0.24.0 (handoff 21): baby stages - carried -> away -> child, for every Beeing Female baby item (the player's too) ----
; Skyrim has no toddler body (the smallest child looks four or five), so a baby item stays with its mother for carriedDays, then the
; little one lives away from home - a record on the mother, parents can say where - until appearsDays after birth, when the child
; appears beside a parent through Beeing Female's own spawn (and then grows on Beeing Female's clock, which starts at the spawn).
; AWAY record = parallel StorageUtil lists on the mother: ONPCR.AwayName / AwaySex / AwayRace / AwayFather / AwayDOB / AwayReason /
; AwayDay; global FormList ONPCR.AwayMothers. A missing father or race is stored as the mother herself (a None would shift the lists).
; Children that came home: ONPCR.HomeName / HomeFather on the mother, global ONPCR.HomeMothers (Couples page notes only).

; once per game hour from ONpcMain.HatchNpcBabies when [Pregnancy] bBabyStages is on; returns how many babies changed stage
int Function BabyStages(Actor player, float carriedDays, float appearsDays) global
	FWSystem sys = Game.GetFormFromFile(0x000D62, "BeeingFemale.esm") as FWSystem
	if !sys
		return 0
	endif
	FWAddOnManager mgr = Game.GetFormFromFile(0x001829, "BeeingFemale.esm") as FWAddOnManager
	float now = Utility.GetCurrentGameTime()
	if appearsDays <= carriedDays
		appearsDays = carriedDays + 1.0
	endif
	RefreshMothers(player, now)
	; Beeing Female hatches the player's babies (and an NPC mother's when the player is the father) itself at Mature time / 5. If
	; that comes first, leave those babies to it.
	bool bfFirst = false
	if mgr && mgr.ActorCustomMatureTimeInHours(player) / 24.0 / 5.0 <= carriedDays
		bfFirst = true
		if StorageUtil.GetIntValue(None, "ONPCR.BabyClockWarned", 0) == 0
			StorageUtil.SetIntValue(None, "ONPCR.BabyClockWarned", 1)
			Debug.Trace("ONPCR: Beeing Female's own clock is shorter than the carried stage - its babies (yours) will hatch as before")
		endif
	endif
	int changed = 0
	; stage 2: carried -> away, mothers whose 3D is loaded (like Beeing Female: an unloaded inventory read is not reliable)
	int i = StorageUtil.FormListCount(None, "ONPCR.BabyMothers")
	while i > 0
		i -= 1
		Actor m = StorageUtil.FormListGet(None, "ONPCR.BabyMothers", i) as Actor
		if !m || m.IsDead() || StorageUtil.FormListCount(m, "FW.BabyItemArmor") == 0
			StorageUtil.FormListRemoveAt(None, "ONPCR.BabyMothers", i)
		elseif m.Is3DLoaded() && SendAwayOne(m, player, now, carriedDays, bfFirst)
			changed += 1
		endif
	endwhile
	if SendAwayOne(player, player, now, carriedDays, bfFirst)
		changed += 1
	endif
	; stage 3: away -> child
	i = StorageUtil.FormListCount(None, "ONPCR.AwayMothers")
	while i > 0
		i -= 1
		Actor m = StorageUtil.FormListGet(None, "ONPCR.AwayMothers", i) as Actor
		if !m || StorageUtil.StringListCount(m, "ONPCR.AwayName") == 0
			StorageUtil.FormListRemoveAt(None, "ONPCR.AwayMothers", i)
		elseif ComeHomeOne(sys, m, player, now, appearsDays)
			changed += 1
		endif
	endwhile
	RefreshRanks(player, now)		; 0.25.0
	ChildLetters(player, now, appearsDays)
	RebuildNotes(player)
	return changed
EndFunction

; 0.25.1: with [Pregnancy] bBabyStages switched off mid-save, children already away still come home on time and their parents'
; factions keep updating (no new baby goes away - those hatch the old way). Called by ONpcMain.HatchNpcBabies.
int Function ComeHomePass(Actor player, float appearsDays) global
	if StorageUtil.FormListCount(None, "ONPCR.AwayMothers") == 0 && StorageUtil.FormListCount(None, "ONPCR.AwayParents") == 0 && StorageUtil.FormListCount(None, "ONPCR.HomecomingParents") == 0
		return 0
	endif
	FWSystem sys = Game.GetFormFromFile(0x000D62, "BeeingFemale.esm") as FWSystem
	if !sys
		return 0
	endif
	float now = Utility.GetCurrentGameTime()
	int changed = 0
	int i = StorageUtil.FormListCount(None, "ONPCR.AwayMothers")
	while i > 0
		i -= 1
		Actor m = StorageUtil.FormListGet(None, "ONPCR.AwayMothers", i) as Actor
		if !m || StorageUtil.StringListCount(m, "ONPCR.AwayName") == 0
			StorageUtil.FormListRemoveAt(None, "ONPCR.AwayMothers", i)
		elseif ComeHomeOne(sys, m, player, now, appearsDays)
			changed += 1
		endif
	endwhile
	RefreshRanks(player, now)
	RebuildNotes(player)
	return changed
EndFunction

; the oldest baby of this mother that has been carried long enough; one per mother per pass
bool Function SendAwayOne(Actor m, Actor player, float now, float carriedDays, bool bfFirst) global
	if StorageUtil.FormListCount(m, "FW.BabyItemArmor") == 0
		return false
	endif
	FWUtility.PruneOrphanBabyIdentities(m)		; sold / dropped baby items leave no orphan entry (Beeing Female does the same first)
	int n = StorageUtil.FormListCount(m, "FW.BabyItemArmor")
	int idx = 0
	while idx < n
		Actor father = StorageUtil.FormListGet(m, "FW.BabyItemFather", idx) as Actor
		Armor arm = StorageUtil.FormListGet(m, "FW.BabyItemArmor", idx) as Armor
		float dob = StorageUtil.FloatListGet(m, "FW.BabyItemDOB", idx)
		bool bfOwn = m == player || father == player
		if arm && dob > 0.0 && now - dob >= carriedDays && !(bfOwn && bfFirst) && (m.IsEquipped(arm) || m.GetItemCount(arm) > 0)
			return SendAway(m, father, arm, dob, idx, player, now)
		endif
		idx += 1
	endwhile
	return false
EndFunction

; copy Beeing Female's identity into an AWAY record, then Beeing Female's own removal steps (identity, Babys list, the item)
bool Function SendAway(Actor m, Actor father, Armor arm, float dob, int idx, Actor player, float now) global
	float nowReal = Utility.GetCurrentRealTime()
	float claim = StorageUtil.GetFloatValue(m, "FW.BabyItemHatching", 0.0)
	if claim > 0.0 && nowReal >= claim && nowReal - claim < 600.0
		return false		; Beeing Female is hatching one of hers right now
	endif
	string babyName = StorageUtil.StringListGet(m, "FW.BabyItemName", idx)
	if babyName == ""
		babyName = "the baby"
	endif
	Form babyRace = StorageUtil.FormListGet(m, "FW.BabyItemRace", idx)
	if !babyRace
		babyRace = m
	endif
	Form fatherForm = father
	if !father
		fatherForm = m
	endif
	int reason
	if ONPCR_Settings.GetVersion() >= 17
		reason = ONPCR_Ladder.PickChildAwayReasonFor(m, father, SocialClassOf(m, player), SocialClassOf(father, player))
	else
		reason = ONPCR_Ladder.PickChildAwayReason(m, father)
	endif
	PadAwayLetters(m)
	StorageUtil.StringListAdd(m, "ONPCR.AwayName", babyName, true)
	StorageUtil.IntListAdd(m, "ONPCR.AwaySex", StorageUtil.IntListGet(m, "FW.BabyItemSex", idx), true)
	StorageUtil.FormListAdd(m, "ONPCR.AwayRace", babyRace, true)
	StorageUtil.FormListAdd(m, "ONPCR.AwayFather", fatherForm, true)
	StorageUtil.FloatListAdd(m, "ONPCR.AwayDOB", dob, true)
	StorageUtil.IntListAdd(m, "ONPCR.AwayReason", reason, true)
	StorageUtil.FloatListAdd(m, "ONPCR.AwayDay", now, true)
	StorageUtil.IntListAdd(m, "ONPCR.AwayLetters", 0, true)
	string reasonId = ""
	if ONPCR_Settings.GetVersion() >= 18
		reasonId = ONPCR_Ladder.ChildAwayReasonId(reason)
	endif
	StorageUtil.StringListAdd(m, "ONPCR.AwayReasonId", reasonId, true)		; 0.25.1
	StorageUtil.FormListAdd(None, "ONPCR.AwayMothers", m, false)
	StorageUtil.FormListAdd(None, "ONPCR.AwayParents", m, false)			; 0.25.1: re-ranked even if the mother's record goes
	if father
		StorageUtil.FormListAdd(None, "ONPCR.AwayParents", father, false)
	endif
	FWUtility.RemoveBabyItemIdentityAt(m, idx)
	; Beeing Female's player list (a twin with the same item keeps it); only for YOUR babies - on the NPC-mother path the bases are shared
	if m == player && StorageUtil.FormListFind(m, "FW.BabyItemArmor", arm) < 0
		StorageUtil.FormListRemove(None, "FW.Babys", arm, false)
	endif
	m.UnequipItem(arm)
	m.RemoveItem(arm, 1, true)
	SetAwayRank(m, player)
	SetAwayRank(father, player)
	string place = ONPCR_Ladder.ChildAwayPlace(reason)
	if reasonId != ""
		place = ONPCR_Ladder.ChildAwayPlaceById(reasonId)
	endif
	if place == ""
		place = "with family"
	endif
	string whose = m.GetDisplayName() + "'s baby "
	if m == player
		whose = "Your baby "
	endif
	Debug.Trace("ONPCR: " + whose + babyName + " is away after " + ((now - dob) as int) + " days - " + place)
	ONPCR_Ladder.AddNewsLine(whose + babyName + " is now living " + place + ".")
	return true
EndFunction

; the first away child of this mother whose time has come and whose parent is near; one per mother per pass
bool Function ComeHomeOne(FWSystem sys, Actor m, Actor player, float now, float appearsDays) global
	int n = StorageUtil.StringListCount(m, "ONPCR.AwayName")
	int k = 0
	while k < n
		float dob = StorageUtil.FloatListGet(m, "ONPCR.AwayDOB", k)
		if now - dob >= appearsDays
			Actor father = AwayFather(m, k)
			Actor anchor = None
			if !m.IsDead()
				anchor = m
			elseif father && !father.IsDead()
				anchor = father
			endif
			if !anchor
				Debug.Trace("ONPCR: " + StorageUtil.StringListGet(m, "ONPCR.AwayName", k) + " never comes home - both parents are dead")
				DropAway(m, k, player)
				return true
			elseif anchor.Is3DLoaded() && ComeHome(sys, m, father, anchor, k, player)
				return true
			endif		; 0.25.1: a child that cannot spawn (no child body for its race) no longer blocks this mother's other children
		endif
		k += 1
	endwhile
	return false
EndFunction

; Beeing Female's own spawn, like Hatch(): the child appears beside the anchor parent with the name, sex and race from birth
bool Function ComeHome(FWSystem sys, Actor m, Actor father, Actor anchor, int k, Actor player) global
	string babyName = StorageUtil.StringListGet(m, "ONPCR.AwayName", k)
	int sex = StorageUtil.IntListGet(m, "ONPCR.AwaySex", k)
	Race babyRace = StorageUtil.FormListGet(m, "ONPCR.AwayRace", k) as Race
	Actor spawnAt = m
	if m.IsDead()
		spawnAt = anchor		; Beeing Female places the child at the "mother" - the real mother is set back below
	endif
	; the player's own child gets Beeing Female's player-child setup (follows you, adoptable), every other child stays with its parents.
	; 0.25.1: bMothersChild ("stays with the NPC mother") only when it spawns at an NPC - an orphan of yours spawns at you and is yours
	Actor child = sys.SpawnChildActor(spawnAt, father, babyRace, sex, babyName, spawnAt != player)
	if !child
		string failKey = "ONPCR.SpawnFailWarned." + babyName
		if StorageUtil.GetIntValue(m, failKey, 0) == 0
			StorageUtil.SetIntValue(m, failKey, 1)
			Debug.Trace("ONPCR: " + babyName + " could not come home (Beeing Female found no child body) - trying again every hour")
		endif
		return false
	endif
	if spawnAt != m
		StorageUtil.SetFormValue(child, "FW.Child.Mother", m)
	endif
	StorageUtil.FormListAdd(None, "FW.Babys", child, false)		; Beeing Female's Children tab lists it
	FWUtility.SendChildSpawnedEvent(m, father, child, babyName)
	StorageUtil.StringListAdd(m, "ONPCR.HomeName", babyName, true)
	Form fatherForm = father
	if !father
		fatherForm = m
	endif
	StorageUtil.FormListAdd(m, "ONPCR.HomeFather", fatherForm, true)
	StorageUtil.FormListAdd(None, "ONPCR.HomeMothers", m, false)
	; 0.25.0: for 7 days the parents say the child is home ("How is the little one?" / "How is our little one?")
	float now = Utility.GetCurrentGameTime()
	if m == player || father == player
		MarkHomecoming(m, "ONPCR.OurHomeDay", now, player)
		MarkHomecoming(father, "ONPCR.OurHomeDay", now, player)
	else
		MarkHomecoming(m, "ONPCR.ChildHomeDay", now, player)
		MarkHomecoming(father, "ONPCR.ChildHomeDay", now, player)
	endif
	DropAway(m, k, player)
	string to = anchor.GetDisplayName()
	if anchor == player
		to = "you"
	endif
	Debug.Trace("ONPCR: " + babyName + " came home to " + anchor.GetDisplayName() + " (" + child.GetDisplayName() + ")")
	ONPCR_Ladder.AddNewsLine(babyName + " is old enough to come home to " + to + ".")
	return true
EndFunction

Actor Function AwayFather(Actor m, int k) global
	Actor f = StorageUtil.FormListGet(m, "ONPCR.AwayFather", k) as Actor
	if f == m
		return None
	endif
	return f
EndFunction

; remove AWAY record k from every list; the parents leave the ChildAway faction when no away child of theirs is left
Function DropAway(Actor m, int k, Actor player) global
	Actor father = AwayFather(m, k)
	StorageUtil.StringListRemoveAt(m, "ONPCR.AwayName", k)
	StorageUtil.IntListRemoveAt(m, "ONPCR.AwaySex", k)
	StorageUtil.FormListRemoveAt(m, "ONPCR.AwayRace", k)
	StorageUtil.FormListRemoveAt(m, "ONPCR.AwayFather", k)
	StorageUtil.FloatListRemoveAt(m, "ONPCR.AwayDOB", k)
	StorageUtil.IntListRemoveAt(m, "ONPCR.AwayReason", k)
	StorageUtil.FloatListRemoveAt(m, "ONPCR.AwayDay", k)
	if StorageUtil.IntListCount(m, "ONPCR.AwayLetters") > k
		StorageUtil.IntListRemoveAt(m, "ONPCR.AwayLetters", k)
	endif
	if StorageUtil.StringListCount(m, "ONPCR.AwayReasonId") > k
		StorageUtil.StringListRemoveAt(m, "ONPCR.AwayReasonId", k)
	endif
	if StorageUtil.StringListCount(m, "ONPCR.AwayName") == 0
		StorageUtil.FormListRemove(None, "ONPCR.AwayMothers", m, true)
	endif
	SetAwayRank(m, player)
	SetAwayRank(father, player)
EndFunction

; 0.25.0 (baby stages part 2): what the "How is the little one?" / "How is our little one?" topics read - dialogue conditions
; cannot see StorageUtil, so each parent carries factions (Letters esp; review\Little One Lines - build plan 0.25.0.md):
;   ONPCR_ChildAway 000F52 = the newest away child's place (reason + 1), ONPCR_ChildAwayAge 000F53 = its age (1 under two, 2 under
;   three, 3 older), ONPCR_ChildAwayWho 000F54 = 1 mother whose child's father died, 2 father whose child's mother died, 3 split up
;   (jilted / divorced), ONPCR_ChildHome 000F55 = 1 for 7 days after a child came home, ONPCR_OurChild 000F56 = the parent of YOUR
;   child: 1 away, 2 came home in the last 7 days. Children with the player count only for ONPCR_OurChild. Not in a faction = no line.
Function SetAwayRank(Actor who, Actor player) global
	if !who || who == player
		return
	endif
	Faction fAway = Game.GetFormFromFile(0x000F52, "OStimNPCsReforged - Letters.esp") as Faction
	if !fAway
		return		; an older Letters esp
	endif
	Faction fAge = Game.GetFormFromFile(0x000F53, "OStimNPCsReforged - Letters.esp") as Faction
	Faction fWho = Game.GetFormFromFile(0x000F54, "OStimNPCsReforged - Letters.esp") as Faction
	Faction fHome = Game.GetFormFromFile(0x000F55, "OStimNPCsReforged - Letters.esp") as Faction
	Faction fOurs = Game.GetFormFromFile(0x000F56, "OStimNPCsReforged - Letters.esp") as Faction
	float now = Utility.GetCurrentGameTime()
	int reason = -1
	int age = 0
	int situation = 0
	bool oursAway = false
	float newest = -1.0
	int i = StorageUtil.FormListCount(None, "ONPCR.AwayMothers")
	while i > 0
		i -= 1
		Actor m = StorageUtil.FormListGet(None, "ONPCR.AwayMothers", i) as Actor
		if m
			int k = StorageUtil.StringListCount(m, "ONPCR.AwayName")
			while k > 0
				k -= 1
				Actor father = AwayFather(m, k)
				if m == who || father == who
					if m == player || father == player
						oursAway = true
					else
						float day = StorageUtil.FloatListGet(m, "ONPCR.AwayDay", k)
						if day > newest
							newest = day
							reason = AwayRank(m, k, player) - 1		; 0.25.1: by place id, not JSON order
							age = AgeBand(now - StorageUtil.FloatListGet(m, "ONPCR.AwayDOB", k))
							situation = Situation(who, m, father)
						endif
					endif
				endif
			endwhile
		endif
	endwhile
	SetRankOrRemove(who, fAway, reason + 1)
	if reason < 0
		age = 0
		situation = 0
	endif
	SetRankOrRemove(who, fAge, age)
	SetRankOrRemove(who, fWho, situation)
	int home = 0
	if now - StorageUtil.GetFloatValue(who, "ONPCR.ChildHomeDay", -99.0) < 7.0
		home = 1
	endif
	SetRankOrRemove(who, fHome, home)
	int ours = 0
	if now - StorageUtil.GetFloatValue(who, "ONPCR.OurHomeDay", -99.0) < 7.0
		ours = 2
	elseif oursAway
		ours = 1
	endif
	SetRankOrRemove(who, fOurs, ours)
EndFunction

Function SetRankOrRemove(Actor who, Faction f, int rank) global
	if !f
		return
	elseif rank > 0
		if who.GetFactionRank(f) != rank
			who.SetFactionRank(f, rank)
		endif
	elseif who.IsInFaction(f)
		who.RemoveFromFaction(f)
	endif
EndFunction

; the child's age in game days -> 1 (first year away, under two), 2 (two to three), 3 (three until home)
int Function AgeBand(float ageDays) global
	if ageDays < 730.0
		return 1
	elseif ageDays < 1095.0
		return 2
	endif
	return 3
EndFunction

; 1 = the mother speaks and the father has died, 2 = the father speaks and the mother has died, 3 = the parents split up, else 0
int Function Situation(Actor who, Actor m, Actor father) global
	if !father
		return 0
	elseif who == m && father.IsDead()
		return 1
	elseif who == father && m.IsDead()
		return 2
	endif
	int stage = ONPCR_Ladder.GetStage(m, father)
	if stage == 5 || stage == 6		; jilted / divorced
		return 3
	endif
	return 0
EndFunction

; M.A.R.A.S social class of an NPC parent (-1 for the player, no parent, or no M.A.R.A.S - the estate / widow places then never fit)
int Function SocialClassOf(Actor a, Actor player) global
	if !a || a == player || !Game.IsPluginInstalled("TT_MARAS.esp")
		return -1
	endif
	return ONPCR_MarasBridge.SocialClass(a)
EndFunction

; ONPCR.AwayLetters came with 0.25.0: records made before it get a 0 entry first, so every list keeps the same index per child
; 0.25.1: the same for ONPCR.AwayReasonId ("" = filled in from the stored slot on the next pass, see AwayReasonId)
Function PadAwayLetters(Actor m) global
	int n = StorageUtil.StringListCount(m, "ONPCR.AwayName")
	while StorageUtil.IntListCount(m, "ONPCR.AwayLetters") < n
		StorageUtil.IntListAdd(m, "ONPCR.AwayLetters", 0, true)
	endwhile
	while StorageUtil.StringListCount(m, "ONPCR.AwayReasonId") < n
		StorageUtil.StringListAdd(m, "ONPCR.AwayReasonId", "", true)
	endwhile
EndFunction

; 0.25.1: the place id of away child k. Records from 0.24/0.25.0 only kept a slot number - it is turned into an id once; a child saved
; while the JSON was missing (slot -1) gets a place now that one fits. "" = no place (no JSON, or a DLL before bridge 18).
string Function AwayReasonId(Actor m, int k, Actor player) global
	if ONPCR_Settings.GetVersion() < 18
		return ""
	endif
	PadAwayLetters(m)
	string id = StorageUtil.StringListGet(m, "ONPCR.AwayReasonId", k)
	if id != ""
		return id
	endif
	int reason = StorageUtil.IntListGet(m, "ONPCR.AwayReason", k)
	if reason < 0 || reason >= ONPCR_Ladder.ChildAwayReasonCount()
		Actor father = AwayFather(m, k)
		reason = ONPCR_Ladder.PickChildAwayReasonFor(m, father, SocialClassOf(m, player), SocialClassOf(father, player))
		if reason < 0
			return ""		; still no JSON - try again next pass
		endif
		StorageUtil.IntListSet(m, "ONPCR.AwayReason", k, reason)
	endif
	id = ONPCR_Ladder.ChildAwayReasonId(reason)
	StorageUtil.StringListSet(m, "ONPCR.AwayReasonId", k, id)
	return id
EndFunction

; ONPCR_ChildAway rank for away child k (place slot + 1 as the answers expect, 0 = no line)
int Function AwayRank(Actor m, int k, Actor player) global
	if ONPCR_Settings.GetVersion() >= 18
		return ONPCR_Ladder.ChildAwayDialogueRank(AwayReasonId(m, k, player))
	endif
	return StorageUtil.IntListGet(m, "ONPCR.AwayReason", k) + 1
EndFunction

; the place text for away child k ("" = none)
string Function AwayPlace(Actor m, int k, Actor player) global
	string id = AwayReasonId(m, k, player)
	if id != ""
		return ONPCR_Ladder.ChildAwayPlaceById(id)
	endif
	return ONPCR_Ladder.ChildAwayPlace(StorageUtil.IntListGet(m, "ONPCR.AwayReason", k))
EndFunction

Function MarkHomecoming(Actor who, string dayKey, float now, Actor player) global
	if who && who != player && !who.IsDead()
		StorageUtil.SetFloatValue(who, dayKey, now)
		StorageUtil.FormListAdd(None, "ONPCR.HomecomingParents", who, false)
	endif
EndFunction

; every pass: ages move on and homecomings end after 7 days, so every parent with an away child or a recent homecoming is re-ranked
Function RefreshRanks(Actor player, float now) global
	int i = StorageUtil.FormListCount(None, "ONPCR.AwayMothers")
	while i > 0
		i -= 1
		Actor m = StorageUtil.FormListGet(None, "ONPCR.AwayMothers", i) as Actor
		if m
			SetAwayRank(m, player)
			StorageUtil.FormListAdd(None, "ONPCR.AwayParents", m, false)		; 0.25.1: fills the list for records made before it
			int k = StorageUtil.StringListCount(m, "ONPCR.AwayName")
			while k > 0
				k -= 1
				Actor f = AwayFather(m, k)
				SetAwayRank(f, player)
				if f
					StorageUtil.FormListAdd(None, "ONPCR.AwayParents", f, false)
				endif
			endwhile
		endif
	endwhile
	; 0.25.1: a parent whose away record vanished (its mother's form went) is re-ranked too, so the away factions don't stay for good
	Faction fAway = Game.GetFormFromFile(0x000F52, "OStimNPCsReforged - Letters.esp") as Faction
	Faction fOurs = Game.GetFormFromFile(0x000F56, "OStimNPCsReforged - Letters.esp") as Faction
	i = StorageUtil.FormListCount(None, "ONPCR.AwayParents")
	while i > 0
		i -= 1
		Actor p = StorageUtil.FormListGet(None, "ONPCR.AwayParents", i) as Actor
		if p
			SetAwayRank(p, player)
		endif
		if !p || !fAway || (!p.IsInFaction(fAway) && !(fOurs && p.GetFactionRank(fOurs) == 1))
			StorageUtil.FormListRemoveAt(None, "ONPCR.AwayParents", i)
		endif
	endwhile
	i = StorageUtil.FormListCount(None, "ONPCR.HomecomingParents")
	while i > 0
		i -= 1
		Actor p = StorageUtil.FormListGet(None, "ONPCR.HomecomingParents", i) as Actor
		if p
			SetAwayRank(p, player)
		endif
		if !p || (now - StorageUtil.GetFloatValue(p, "ONPCR.ChildHomeDay", -99.0) >= 7.0 && now - StorageUtil.GetFloatValue(p, "ONPCR.OurHomeDay", -99.0) >= 7.0)
			StorageUtil.FormListRemoveAt(None, "ONPCR.HomecomingParents", i)
		endif
	endwhile
EndFunction

; idea 4: courier letters about YOUR away child - 1 at age two, 2 at age three, 3 a month before the homecoming. Only the latest due
; letter is sent (the earlier ones are marked done); one per pass. Queued here, sent by ONpcMain (it owns the courier and the letter
; quests): ONPCR.ChildLetterId (1-3) + ONPCR.ChildLetterName.
Function ChildLetters(Actor player, float now, float appearsDays) global
	int i = StorageUtil.FormListCount(None, "ONPCR.AwayMothers")
	while i > 0
		i -= 1
		Actor m = StorageUtil.FormListGet(None, "ONPCR.AwayMothers", i) as Actor
		if m
			PadAwayLetters(m)
			int k = StorageUtil.StringListCount(m, "ONPCR.AwayName")
			while k > 0
				k -= 1
				if m == player || AwayFather(m, k) == player
					float age = now - StorageUtil.FloatListGet(m, "ONPCR.AwayDOB", k)
					int sent = StorageUtil.IntListGet(m, "ONPCR.AwayLetters", k)
					int letter = 0
					if age >= appearsDays - 30.0
						letter = 3
					elseif age >= 1095.0
						letter = 2
					elseif age >= 730.0
						letter = 1
					endif
					int bit = 0
					if letter == 3
						bit = 4
					elseif letter == 2
						bit = 2
					elseif letter == 1
						bit = 1
					endif
					if bit > 0 && Math.LogicalAnd(sent, bit) == 0
						StorageUtil.IntListSet(m, "ONPCR.AwayLetters", k, bit * 2 - 1)		; this one and every earlier one
						StorageUtil.IntListAdd(None, "ONPCR.ChildLetterId", letter, true)
						StorageUtil.StringListAdd(None, "ONPCR.ChildLetterName", StorageUtil.StringListGet(m, "ONPCR.AwayName", k), true)
						Debug.Trace("ONPCR: letter " + letter + " about " + StorageUtil.StringListGet(m, "ONPCR.AwayName", k) + " queued")
						return
					endif
				endif
			endwhile
		endif
	endwhile
EndFunction

; the Couples page notes, rebuilt every pass: "baby Ada (carried)", "Ada away: at the Temple of Mara in Riften", "child Ada at home"
Function RebuildNotes(Actor player) global
	ONPCR_Ladder.ClearBabyNotes()
	int i = StorageUtil.FormListCount(None, "ONPCR.BabyMothers")
	while i > 0
		i -= 1
		NoteCarried(StorageUtil.FormListGet(None, "ONPCR.BabyMothers", i) as Actor)
	endwhile
	NoteCarried(player)
	i = StorageUtil.FormListCount(None, "ONPCR.AwayMothers")
	while i > 0
		i -= 1
		Actor m = StorageUtil.FormListGet(None, "ONPCR.AwayMothers", i) as Actor
		if m
			int k = StorageUtil.StringListCount(m, "ONPCR.AwayName")
			while k > 0
				k -= 1
				string place = AwayPlace(m, k, player)
				if place == ""
					place = "with family"
				endif
				ONPCR_Ladder.AddBabyNote(m, AwayFather(m, k), StorageUtil.StringListGet(m, "ONPCR.AwayName", k) + " away: " + place)
			endwhile
		endif
	endwhile
	i = StorageUtil.FormListCount(None, "ONPCR.HomeMothers")
	while i > 0
		i -= 1
		Actor m = StorageUtil.FormListGet(None, "ONPCR.HomeMothers", i) as Actor
		if m
			int k = StorageUtil.StringListCount(m, "ONPCR.HomeName")
			while k > 0
				k -= 1
				Actor f = StorageUtil.FormListGet(m, "ONPCR.HomeFather", k) as Actor
				if f != m
					ONPCR_Ladder.AddBabyNote(m, f, "child " + StorageUtil.StringListGet(m, "ONPCR.HomeName", k) + " at home")
				endif
			endwhile
		endif
	endwhile
EndFunction

Function NoteCarried(Actor m) global
	if !m
		return
	endif
	int n = StorageUtil.FormListCount(m, "FW.BabyItemArmor")
	int idx = 0
	while idx < n
		Actor father = StorageUtil.FormListGet(m, "FW.BabyItemFather", idx) as Actor
		if father
			ONPCR_Ladder.AddBabyNote(m, father, "baby " + StorageUtil.StringListGet(m, "FW.BabyItemName", idx) + " (carried)")
		endif
		idx += 1
	endwhile
EndFunction
