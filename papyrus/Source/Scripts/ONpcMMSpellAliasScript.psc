scriptname ONpcMMSpellAliasScript extends ReferenceAlias

actor[] actorsForScene

Actor property PlayerRef auto

Spell property MatchMakerSpellTarget auto
Spell property MatchMakerSpellSelf auto

OSexIntegrationMain property OStim auto


event OnInit()
	if !PlayerRef.HasSpell(MatchMakerSpellTarget)
		PlayerRef.AddSpell(MatchMakerSpellTarget, true)
	endIf
	if !PlayerRef.HasSpell(MatchMakerSpellSelf)
		PlayerRef.AddSpell(MatchMakerSpellSelf, true)
	endIf

	OnPlayerLoadGame()
endEvent


event OnPlayerLoadGame()
	OStim = OUtils.GetOStim()
	actorsForScene = PapyrusUtil.ResizeActorArray(actorsForScene, 0)
endEvent


event OnUpdate()
	StartScene()
endEvent


int function RegisterActor(actor act)
	; 0 -> failure
	; 1 -> success
	; 2 -> removed
	int status = 0

	if !act.isChild() && act.HasKeywordString("ActorTypeNPC")
		if PapyrusUtil.CountActor(actorsForScene, act) == 0
			if actorsForScene.Length < 3
				actorsForScene = PapyrusUtil.PushActor(actorsForScene, act)
				status = 1
			endif
		else
			actorsForScene = PapyrusUtil.RemoveActor(actorsForScene, act)
			status = 2
		endif
	endif

	if actorsForScene.Length > 1
		RegisterForSingleUpdate(5.0)
	endif

	return status
endFunction


function StartScene()
	if actorsForScene.length < 2
		return
	endif

	if PapyrusUtil.CountActor(actorsForScene, PlayerRef) > 0
		if actorsForScene.Length == 2
			OStim.StartScene(actorsForScene[0], actorsForScene[1])
		elseif actorsForScene.Length == 3
			OStim.StartScene(actorsForScene[0], actorsForScene[1], zThirdActor = actorsForScene[2])
		endif
	else
		ONPCR_Ladder.AllowPlayerChosen(actorsForScene[0], actorsForScene[1])  ; 0.20.3: player-made pair, same voice is fine
		if actorsForScene.Length == 3
			ONPCR_Ladder.AllowPlayerChosen(actorsForScene[0], actorsForScene[2])
			ONPCR_Ladder.AllowPlayerChosen(actorsForScene[1], actorsForScene[2])
		endif
		if actorsForScene.Length == 2
			OStim.GetUnusedSubthread().StartSubthreadScene(actorsForScene[0], actorsForScene[1])
		elseif actorsForScene.Length == 3
			OStim.GetUnusedSubthread().StartSubthreadScene(actorsForScene[0], actorsForScene[1], zThirdActor = actorsForScene[2])
		endif
	endif

	actorsForScene = PapyrusUtil.ResizeActorArray(actorsForScene, 0)
endFunction
