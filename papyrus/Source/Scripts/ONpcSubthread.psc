Scriptname ONpcSubthread extends ReferenceAlias

; ============================================================
; Runtime state
; ============================================================

int ONpcThreadID
bool ONpcThreadInUse

Actor DomActor
Actor SubActor
Actor ThirdActor

bool AlternativeBedSearchUsed
bool IsEnemyScene
bool IsFollowerScene

ObjectReference CurrentFurniture

int CurrentOStimSubthreadID

; 0.26.0 scene log: climaxes this scene (ostim_actor_orgasm) and whether we stopped it because they were seen
int DomClimaxes
int SubClimaxes
bool StoppedBecauseSeen
bool StoppedByWitness	; 0.27.2 privacy: someone else saw them
bool KeptQuiet	; 0.27.2 privacy: muted because someone is within earshot
bool Watching	; 0.26.0: "stop when seen" checks every 2 s from onUpdate while the scene runs (no loop inside the start event)

; 0.26.0 phase 3: a group scene (3-5 people; DomActor / SubActor are its first two)
bool IsGroupScene
Actor[] GroupActors
int[] GroupClimaxes

; ============================================================
; Properties
; ============================================================

OSexIntegrationMain property OStim auto

ReferenceAlias property Nav1 auto
ReferenceAlias property Nav2 auto
ReferenceAlias property Target auto

Actor property PlayerRef auto

ONpcMain property ONpc auto

ONpcSubthreadPackages property SubthreadPackages auto

Float Property TMSFurnitureFinalCheckDistance = 192.0 Auto


; ============================================================
; Events / lifecycle
; ============================================================

Event OnInit()
	ONpcThreadID = GetID()
	OStim = OUtils.GetOStim()
EndEvent


Event NpcSceneStart(string eventname, string strarg, float numarg, form sender)
	if numarg == CurrentOStimSubthreadID
		ONpc.JealousyStart(DomActor, SubActor)
		ONpc.JealousyStart(SubActor, DomActor)
		; the old "one scene a night" list, only for the pre-0.26.0 scan: with the DLL, a short break and their arousal state
		; decide whether they go again (a frustrated or eager couple may, a fulfilled one waits)
		if ONPCR_Settings.GetVersion() < 21
			JArray.addForm(ONpc.NPCsHadSexThisNight, DomActor)
			JArray.addForm(ONpc.NPCsHadSexThisNight, SubActor)

			if ThirdActor
				JArray.addForm(ONpc.NPCsHadSexThisNight, ThirdActor)
			endif
		endif

		if !IsFollowerScene && (ONpc.StopWhenFound || (ONpc.Privacy && !IsEnemyScene))
			Watching = true
			WatchScene()	; 0.27.2: the first look right away (keep quiet from the start if someone is within earshot)
		endif
	endif
EndEvent


Event NpcSceneEnd(string eventname, string strarg, float numarg, form sender)
	int cause = 0
	if numarg == CurrentOStimSubthreadID
		if StoppedBecauseSeen
			cause = 1
		elseif StoppedByWitness
			cause = 2
		endif
		if KeptQuiet
			SetQuiet(false)
		endif
		if IsGroupScene && GroupActors
			ONPCR_Base.GroupSceneEnded(GroupActors, GroupClimaxes, CurrentOStimSubthreadID, cause)	; 0.26.0 phase 3: breaks + scene log
			int gi = 2
			while gi < GroupActors.Length
				ONpc.LadderRecordScene(DomActor, GroupActors[gi])	; the hub with every partner, not just the first
				ONpc.PeriodAfterScene(GroupActors[gi])
				gi += 1
			endwhile
		elseif ONPCR_Settings.GetVersion() >= 21
			ONPCR_Base.SceneEnded(DomActor, SubActor, DomClimaxes, SubClimaxes, CurrentOStimSubthreadID, cause)	; 0.26.0: past lovers, the break, the scene log
		endif
		ONpc.LadderRecordScene(DomActor, SubActor)
		ONpc.JealousyEnd(DomActor)
		ONpc.JealousyEnd(SubActor)
		ONpc.PeriodAfterScene(DomActor)
		ONpc.PeriodAfterScene(SubActor)
		SceneEndProcedures()
	endif
EndEvent


; ============================================================
; Scene setup / start
; ============================================================

Function StartScene()
	; Do every furniture gate before registering OStim events or starting the builder.
	; This avoids stale ostim_thread_start callbacks from older/other threads making the log order confusing.
	if !TMS_RunFurnitureGate("pre-start gate")
		SceneEndProcedures()
		return
	endif

	Actor[] actors = new Actor[2]
	actors[0] = DomActor
	actors[1] = SubActor

	int BuilderID = OThreadBuilder.Create(actors)

	if CurrentFurniture == none
		OThreadBuilder.NoFurniture(BuilderID)
	else
		if !TMS_RunFurnitureGate("set-furniture gate")
			SceneEndProcedures()
			return
		endif

		if CurrentFurniture == none
			OThreadBuilder.NoFurniture(BuilderID)
		else
			OThreadBuilder.SetFurniture(BuilderID, CurrentFurniture)
		endif
	endif

	if !CheckActorsStillValid() || PrivacyBlocks()
		SceneEndProcedures()
		return
	endif

	RegisterForModEvent("ostim_thread_start", "NpcSceneStart")
	RegisterForModEvent("ostim_thread_end", "NpcSceneEnd")
	StoppedByWitness = false
	KeptQuiet = false
	DomClimaxes = 0
	SubClimaxes = 0
	StoppedBecauseSeen = false
	RegisterForModEvent("ostim_actor_orgasm", "NpcActorOrgasm")

	CurrentOStimSubthreadID = OThreadBuilder.Start(BuilderID)

	if CurrentOStimSubthreadID < 0
		SceneEndProcedures()
	elseif ONPCR_Settings.GetVersion() >= 21
		ONPCR_Base.SceneStarted(DomActor, SubActor, CurrentOStimSubthreadID, CurrentFurniture)	; 0.26.0: the scene log's start line
	endif
EndFunction


; 0.26.0: OStim sends this for every actor in every scene - count only this slot's couple
Event NpcActorOrgasm(string eventname, string strarg, float numarg, form sender)
	if numarg == CurrentOStimSubthreadID && ONpcThreadInUse
		if IsGroupScene && GroupActors
			int gi = GroupActors.Find(sender as Actor)
			if gi >= 0
				GroupClimaxes[gi] = GroupClimaxes[gi] + 1
			endif
		elseif sender == DomActor
			DomClimaxes += 1
		elseif sender == SubActor
			SubClimaxes += 1
		endif
	endif
EndEvent


; 0.26.0 phase 3: a group gathers at the furniture (or around the first of them): whoever isn't there walks over with the
; engine's own "walk to", all at the same time; after 20 s everyone still away is moved there, then OStim starts the scene
Function SetupGroupScene(Actor[] actors, ObjectReference furnitureObj)
	ONpcThreadInUse = true
	CurrentOStimSubthreadID = -1
	IsGroupScene = true
	GroupActors = actors
	GroupClimaxes = Utility.CreateIntArray(actors.Length, 0)
	DomActor = actors[0]
	SubActor = actors[1]
	ThirdActor = actors[2]	; the furniture guard ignores these three as "someone else on the bed"
	CurrentFurniture = furnitureObj
	AlternativeBedSearchUsed = false
	IsEnemyScene = false
	IsFollowerScene = false
	if ONpc.IsEnemy(DomActor) && ONpc.IsEnemy(SubActor)
		IsEnemyScene = true
		ONpc.EnemyScenesThisNight += 1
		ONpc.CurrentLocationEnemyScene = false
	endif
	int i = 0
	while i < actors.Length
		if ONpc.IsFollower(actors[i])
			IsFollowerScene = true
		endif
		i += 1
	endwhile
	if IsFollowerScene
		ONpc.FollowerScenesThisNight += 1
	endif
	RegisterForSingleUpdate(0.01)
EndFunction


; everyone in this slot's scene
Actor[] Function SceneActors()
	if IsGroupScene && GroupActors
		return GroupActors
	endif
	Actor[] a = new Actor[2]
	a[0] = DomActor
	a[1] = SubActor
	return a
EndFunction


; 0.27.2 privacy, just before OStim starts: someone awake can see them -> they don't start (they walked to the bed, saw
; someone, gave up)
bool Function PrivacyBlocks()
	if !ONpc.Privacy || IsFollowerScene || IsEnemyScene
		return false	; followers and enemy camps don't mind (as in OStim NPCs)
	endif
	Actor[] who = SceneActors()
	if ONPCR_Base.Privacy(who) == 2
		Debug.Trace("ONPCR: " + DomActor.GetDisplayName() + " and partner don't start - " + ONPCR_Base.Witnesser(who) + " can see them")
		ONPCR_Base.NoPrivacy(who)	; not again for about half an hour (no walking back and forth)
		return true
	endif
	return false
EndFunction


Function SetQuiet(bool quiet)
	Actor[] who = SceneActors()
	int i = 0
	while i < who.Length
		if who[i]
			if quiet
				OActor.Mute(who[i])
			else
				OActor.Unmute(who[i])
			endif
		endif
		i += 1
	endwhile
	KeptQuiet = quiet
EndFunction


; while the scene runs, every 2 s: you saw them ("stop when found") or someone else saw them (privacy) -> OStim stops it
; (NpcSceneEnd cleans up); someone only within earshot -> they keep quiet, and relax again once nobody is in earshot
Function WatchScene()
	if !OThread.IsRunning(CurrentOStimSubthreadID) || (!ONpc.StopWhenFound && !ONpc.Privacy)
		Watching = false
		return
	endif
	if ONpc.StopWhenFound && IsFound()
		StoppedBecauseSeen = true
		Watching = false
		OThread.Stop(CurrentOStimSubthreadID)
		return
	endif
	if ONpc.Privacy && !IsEnemyScene
		Actor[] who = SceneActors()
		int p = ONPCR_Base.Privacy(who)
		if p == 2
			Debug.Trace("ONPCR: scene stopped - " + ONPCR_Base.Witnesser(who) + " saw " + DomActor.GetDisplayName())
			StoppedByWitness = true
			Watching = false
			OThread.Stop(CurrentOStimSubthreadID)
			return
		elseif p == 1 && !KeptQuiet
			Debug.Trace("ONPCR: " + DomActor.GetDisplayName() + " and partner keep quiet - " + ONPCR_Base.Witnesser(who) + " is within earshot")
			SetQuiet(true)
		elseif p == 0 && KeptQuiet
			SetQuiet(false)
		endif
	endif
	RegisterForSingleUpdate(2.0)
EndFunction


bool Function GatherGroup()
	if !TMS_RunFurnitureGate("group pre-gather gate") || !CheckActorsStillValid()
		return false
	endif
	ObjectReference meet = CurrentFurniture
	if !meet
		meet = DomActor
	endif
	int i = 0
	while i < GroupActors.Length
		if GroupActors[i] && GroupActors[i] != meet && GroupActors[i].GetDistance(meet) > 200.0
			if !ONPCR_Base.WalkTo(GroupActors[i], meet)
				Debug.Trace("ONPCR: group walk could not start for " + GroupActors[i].GetDisplayName() + " - moved there after the wait")
			endif
		endif
		i += 1
	endwhile
	int waited = 0
	bool allThere = false
	while !allThere && waited < 20
		Utility.Wait(1.0)
		waited += 1
		if !CheckActorsStillValid()
			return false
		endif
		allThere = true
		i = 0
		while i < GroupActors.Length
			if GroupActors[i] && GroupActors[i] != meet && GroupActors[i].GetDistance(meet) > 200.0
				allThere = false
			endif
			i += 1
		endwhile
	endwhile
	; the furniture again, before anyone is moved onto it (the gate may fall back to no furniture: then they meet at the hub)
	if !TMS_RunFurnitureGate("group pre-start gate")
		return false
	endif
	if !CurrentFurniture
		meet = DomActor
	endif
	i = 0
	while i < GroupActors.Length
		if GroupActors[i] && GroupActors[i] != meet && GroupActors[i].GetDistance(meet) > 200.0
			GroupActors[i].SetPosition(meet.X, meet.Y, meet.Z)
		endif
		i += 1
	endwhile
	Debug.Trace("ONPCR: group of " + GroupActors.Length + " gathered after " + waited + " s (" + DomActor.GetDisplayName() + ")")
	return true
EndFunction


; an aborted gathering: whoever is still walking stops where they are (a walk to themselves ends the engine's walk) and
; goes back to their own routine
Function StopGroupWalks()
	int i = 0
	while i < GroupActors.Length
		Actor a = GroupActors[i]
		if a && !a.IsDead()
			ONPCR_Base.WalkTo(a, a)
			a.EvaluatePackage()
		endif
		i += 1
	endwhile
EndFunction


Function StartGroupScene()
	if !CheckActorsStillValid() || PrivacyBlocks()
		SceneEndProcedures()
		return
	endif
	int BuilderID = OThreadBuilder.Create(GroupActors)
	if BuilderID < 0
		SceneEndProcedures()
		return
	endif
	if CurrentFurniture == none
		OThreadBuilder.NoFurniture(BuilderID)
	else
		OThreadBuilder.SetFurniture(BuilderID, CurrentFurniture)
	endif
	RegisterForModEvent("ostim_thread_start", "NpcSceneStart")
	RegisterForModEvent("ostim_thread_end", "NpcSceneEnd")
	StoppedBecauseSeen = false
	StoppedByWitness = false
	KeptQuiet = false
	RegisterForModEvent("ostim_actor_orgasm", "NpcActorOrgasm")
	CurrentOStimSubthreadID = OThreadBuilder.Start(BuilderID)
	if CurrentOStimSubthreadID < 0
		SceneEndProcedures()
	else
		ONPCR_Base.GroupSceneStarted(GroupActors, CurrentOStimSubthreadID, CurrentFurniture)
	endif
EndFunction


Function SetupScene(Actor dom, Actor sub, ObjectReference furnitureObj, bool alternativeBedSearch, Actor third = none)
	ONpcThreadInUse = true
	CurrentOStimSubthreadID = -1
	IsGroupScene = false
	GroupActors = None

	DomActor = dom
	SubActor = sub
	ThirdActor = third

	AlternativeBedSearchUsed = alternativeBedSearch

	CurrentFurniture = furnitureObj

	IsEnemyScene = false
	IsFollowerScene = false

	if ONpc.IsEnemy(DomActor) && ONpc.IsEnemy(SubActor)
		IsEnemyScene = true
		ONpc.EnemyScenesThisNight += 1
		ONpc.CurrentLocationEnemyScene = false
	endif

	if ONpc.IsFollower(DomActor) || ONpc.IsFollower(SubActor)
		ONpc.FollowerScenesThisNight += 1
		IsFollowerScene = true
	endif

	RegisterForSingleUpdate(0.01)
EndFunction


; ============================================================
; Scene flow
; ============================================================

Event onUpdate()
	if !ONpcThreadInUse
		return	; a late update for a slot that has already been freed
	endif
	if Watching
		WatchScene()
		return
	endif
	if IsGroupScene
		if !GatherGroup()
			StopGroupWalks()
			SceneEndProcedures()
			return
		endif
		if !IsFollowerScene && ONpc.StopWhenFound && isFound()
			SceneEndProcedures()
			return
		endif
		StartGroupScene()
		return
	endif
	if !ONpc.TravelToLocation || (IsEnemyScene && !ONpc.EnemiesTravelToLocation)
		; If actors were sleeping, we must wake them up before initiating scene
		; Some mods change the NPCs clothes when they go to sleep
		; So if we start a scene without waking them up, OStim may fail to remove the clothes properly
		if AlternativeBedSearchUsed
			if DomActor.GetSleepState() != 0 || SubActor.GetSleepState() != 0
				; This wakes up the actors
				SubthreadPackages.PackageDoNothing(DomActor, SubActor, Nav1, Nav2)
			endif

			int numberLoops = 0

			; now we wait for them to wake up
			while DomActor.GetSleepState() != 0 || SubActor.GetSleepState() != 0
				numberLoops += 1

				if numberLoops >= 10
					SceneEndProcedures()
					return
				endif

				Utility.Wait(1)
			endwhile

			Utility.Wait(1.5)
		endif

		DomActor.setposition(SubActor.x, SubActor.y, SubActor.z)

		if CurrentFurniture != none

			if CurrentFurniture != none
				DomActor.setposition(CurrentFurniture.x, CurrentFurniture.y, CurrentFurniture.z)
				SubActor.setposition(CurrentFurniture.x, CurrentFurniture.y, CurrentFurniture.z)
			endif
		endif

		SubthreadPackages.ClearAliases(DomActor, SubActor, Nav1, Nav2, Target)

		StartScene()
	else
		if !Invite()
			SceneEndProcedures()
			return
		endif

		debug.sendanimationevent(DomActor, "IdleComeThisWay")

		if CurrentFurniture != none
			if !GoToFurniture()
				SceneEndProcedures()
				return
			endif


			if CurrentFurniture != none
				DomActor.setposition(CurrentFurniture.x, CurrentFurniture.y, CurrentFurniture.z)
				SubActor.setposition(CurrentFurniture.x, CurrentFurniture.y, CurrentFurniture.z)
			endif
		endif

		if !IsFollowerScene && ONpc.StopWhenFound && isFound()
			TMS_FinalTrace("scene dropped before start: the player was detected (Scenes end when found is on)", true)
			SceneEndProcedures()
			return
		endif

		StartScene()
	endif
EndEvent


; ============================================================
; TMS Furniture Guard gates
; ============================================================

Function TMS_FinalTrace(String msg, Bool ForceLog = false)
	if ForceLog
		Debug.Trace("[TMS ONPC FurnitureGuard] " + msg)
		return
	endif

	if ONpc && ONpc.TMSFurnitureGuardDebug
		Debug.Trace("[TMS ONPC FurnitureGuard] " + msg)
	endif
EndFunction


Float Function TMS_GetFinalCheckDistance()
	if TMSFurnitureFinalCheckDistance <= 0.0
		return 192.0
	endif

	return TMSFurnitureFinalCheckDistance
EndFunction


Bool Function TMS_CanFallbackToNoFurniture()
	if AlternativeBedSearchUsed
		return false
	endif

	if ONpc.ScenesStartIn == ONpc.AnyFurniture || ONpc.ScenesStartIn == ONpc.BedsOnly
		if IsEnemyScene && ONpc.EnemiesIgnoreScenesStartInRules
			return true
		endif

		return false
	endif

	return true
EndFunction


Function TMS_ReleaseCurrentFurnitureLock()
	if ONpc.JArraysCreated && CurrentFurniture
		JArray.EraseForm(ONpc.FurnituresInUse, CurrentFurniture)
	endif
EndFunction


Bool Function TMS_RunFurnitureGate(String StageName)
	if !CurrentFurniture
		TMS_FinalTrace(StageName + ": no furniture, allow no-furniture path")
		return true
	endif

	if !DomActor || !SubActor
		TMS_FinalTrace(StageName + ": missing actors, abort", true)
		return false
	endif

	; 0.26.0 phase 3: the guard can ignore only three people, so with four or five gathered on the bed it would see the
	; group itself as strangers - the bed was checked and reserved when it was chosen, trust that
	if IsGroupScene && GroupActors && GroupActors.Length > 3
		TMS_FinalTrace(StageName + ": group of " + GroupActors.Length + ", bed reserved at pick time " + CurrentFurniture)
		return true
	endif

	Float finalCheckDistance = TMS_GetFinalCheckDistance()
	Bool closeEnough = false

	if DomActor.GetDistance(CurrentFurniture) <= finalCheckDistance
		closeEnough = true
	endif

	if SubActor.GetDistance(CurrentFurniture) <= finalCheckDistance
		closeEnough = true
	endif

	if ThirdActor && ThirdActor.GetDistance(CurrentFurniture) <= finalCheckDistance
		closeEnough = true
	endif

	if !closeEnough
		TMS_FinalTrace(StageName + ": early safety check, actors not close yet " + CurrentFurniture)
	else
		TMS_FinalTrace(StageName + ": checking furniture " + CurrentFurniture)
	endif

	if !ONpc.TMS_IsFurnitureUnsafeForSceneStart(CurrentFurniture, DomActor, SubActor, ThirdActor)
		TMS_FinalTrace(StageName + ": furniture still available " + CurrentFurniture)
		return true
	endif

	TMS_FinalTrace(StageName + ": furniture blocked or occupied " + CurrentFurniture, true)

	TMS_ReleaseCurrentFurnitureLock()

	if TMS_CanFallbackToNoFurniture()
		TMS_FinalTrace(StageName + ": fallback to ONPC no-furniture path", true)
		CurrentFurniture = None
		return true
	endif

	TMS_FinalTrace(StageName + ": furniture required, abort subthread", true)
	return false
EndFunction


Bool Function TMS_PreStartFurnitureGate()
	return TMS_RunFurnitureGate("pre-start gate")
EndFunction


Bool Function TMS_HandleFinalFurnitureCheck()
	return TMS_RunFurnitureGate("furniture gate")
EndFunction


; ============================================================
; Cleanup
; ============================================================

; 0.26.0: the OStim thread this slot runs (-1 = none yet: still walking the NPCs over, or free)
int Function GetOStimThreadID()
	return CurrentOStimSubthreadID
EndFunction

Actor Function GetDomActor()
	return DomActor
EndFunction

; 0.26.0: after a load this slot still says "busy" but its OStim scene is gone (the end event never came) - free it
Function FreeStuckSlot()
	Debug.Trace("ONPCR: scene slot " + ONpcThreadID + " freed after a load - its OStim scene " + CurrentOStimSubthreadID + " had ended")
	SceneEndProcedures()
EndFunction


Function SceneEndProcedures()
	SubthreadPackages.ClearAliases(DomActor, SubActor, Nav1, Nav2, Target)

	UnregisterForModEvent("ostim_thread_start")
	UnregisterForModEvent("ostim_thread_end")
	UnregisterForModEvent("ostim_actor_orgasm")

	; do this check for edge case where NPC OStim scene ends after night has ended, and thus JArrays are already cleared
	if ONpc.JArraysCreated
		JArray.EraseForm(ONpc.NPCsInScene, DomActor)
		JArray.EraseForm(ONpc.NPCsInScene, SubActor)
		JArray.EraseForm(ONpc.NPCsInScene, ThirdActor)
		if IsGroupScene && GroupActors
			int gi = 2
			while gi < GroupActors.Length
				JArray.EraseForm(ONpc.NPCsInScene, GroupActors[gi])
				gi += 1
			endwhile
		endif

		JArray.EraseForm(ONpc.FurnituresInUse, CurrentFurniture)
	endif
	IsGroupScene = false
	GroupActors = None

	Watching = false
	UnregisterForUpdate()
	CurrentOStimSubthreadID = -1
	ONpcThreadInUse = false
	ONpc.activeScenes -= 1
	if ONpc.activeScenes < 0
		ONpc.activeScenes = 0	; 0.26.0: never below zero (a slot freed around a load could count twice)
	endif
EndFunction


; ============================================================
; Travel / validation helpers
; ============================================================

bool Function Invite()
	SubthreadPackages.TravelToActor(DomActor, SubActor, Nav1, Nav2, Target)
	DomActor.SetLookAt(SubActor)
	SubActor.SetLookAt(DomActor)

	int stuckTimer = 0

	while DomActor.GetDistance(SubActor) > 128
		if !CheckActorsStillValid()
			DomActor.ClearLookAt()
			SubActor.ClearLookAt()

			return false
		endif

		stuckTimer += 1

		if stuckTimer >= 20
			DomActor.setposition(SubActor.x, SubActor.y, SubActor.z)
		else
			utility.wait(1)
		endif
	endwhile

	SubthreadPackages.ClearAliases(DomActor, SubActor, Nav1, Nav2, Target)

	DomActor.ClearLookAt()
	SubActor.ClearLookAt()

	return true
EndFunction


bool Function GoToFurniture()
	if !TMS_RunFurnitureGate("go-to before travel")
		return false
	endif

	if !CurrentFurniture
		return true
	endif

	SubthreadPackages.TravelToFurniture(DomActor, SubActor, CurrentFurniture, Nav1, Nav2, Target)

	int stuckTimer = 0

	while CurrentFurniture && DomActor.GetDistance(CurrentFurniture) > 128 && SubActor.GetDistance(CurrentFurniture) > 128
		if !CheckActorsStillValid()
			return false
		endif

		stucktimer += 1

		if stucktimer >= 20
			TMS_FinalTrace("go-to after travel: travel timed out, let later gate handle it " + CurrentFurniture, true)
			return true
		else
			utility.wait(1)
		endif
	endwhile

	if !TMS_RunFurnitureGate("go-to after travel")
		return false
	endif

	SubthreadPackages.ClearAliases(DomActor, SubActor, Nav1, Nav2, Target)

	return true
EndFunction


bool Function CheckActorsStillLoaded()
	return DomActor.Is3DLoaded() && SubActor.Is3DLoaded() && (!ThirdActor || ThirdActor.Is3DLoaded())	; 0.26.0: the third was tested the wrong way round
EndFunction


bool Function CheckActorsStillAlive()
	return !DomActor.IsDead() && !SubActor.IsDead() && (!ThirdActor || (ThirdActor && !ThirdActor.IsDead()))
EndFunction


bool Function CheckActorsNotInScenes()
	return DomActor.GetCurrentScene() == None && SubActor.GetCurrentScene() == None && (!ThirdActor || (ThirdActor && ThirdActor.GetCurrentScene() == None))
EndFunction

bool Function CheckActorsAreInCombatState()
	return ONpc.ActorIsInCombatState(DomActor) || ONpc.ActorIsInCombatState(SubActor) || (ThirdActor && ONpc.ActorIsInCombatState(ThirdActor))
EndFunction


bool Function CheckActorsAreInDialogue()
	return DomActor.IsInDialogueWithPlayer() || SubActor.IsInDialogueWithPlayer()
EndFunction


bool Function CheckActorsStillValid()
	if IsGroupScene && GroupActors
		return CheckGroupStillValid()
	endif
	return CheckActorsStillLoaded() && CheckActorsStillAlive() && CheckActorsNotInScenes() && !CheckActorsAreInCombatState() && !CheckActorsAreInDialogue()
EndFunction


; 0.26.0 phase 3: every member of a group, the same checks as a couple
bool Function CheckGroupStillValid()
	int i = 0
	while i < GroupActors.Length
		Actor a = GroupActors[i]
		if !a || !a.Is3DLoaded() || a.IsDead() || a.GetCurrentScene() != None || ONpc.ActorIsInCombatState(a) || a.IsInDialogueWithPlayer()
			return false
		endif
		i += 1
	endwhile
	return true
EndFunction


bool Function IsFound()
	if PlayerRef.isdetectedby(DomActor) && (PlayerRef.haslos(DomActor) || DomActor.haslos(PlayerRef))
		return true
	endif
	return false
EndFunction


Bool Function IsThreadInUse()
	return ONpcThreadInUse
EndFunction
