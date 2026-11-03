scriptname ONpcMMSpellEffect extends ActiveMagicEffect


ONpcMMSpellAliasScript property MatchMakerSpell auto


event OnEffectStart(Actor TargetRef, Actor CasterRef)
	int status = MatchMakerSpell.RegisterActor(TargetRef)

	if status == 1
		Debug.Notification(TargetRef.GetLeveledActorBase().GetName() + " successfully selected for OStim scene.")
	elseif status == 2
		Debug.Notification(TargetRef.GetLeveledActorBase().GetName() + " was removed from selection for OStim scene.")
	else
		Debug.Notification(TargetRef.GetLeveledActorBase().GetName() + " can not be selected for OStim scene.")
	endIf
endEvent
