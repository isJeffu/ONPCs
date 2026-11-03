Scriptname ONPCR_MarasBridge Hidden
{OStim NPCs Reforged - the only script that references M.A.R.A.S. ONpcMain calls it only when TT_MARAS.esp is loaded,
 so the mod works without M.A.R.A.S (this script simply never loads then).}

; ask M.A.R.A.S for the archetypes of up to max pool NPCs the DLL has not classified yet
Function FillArchetypes(int max) global
	int[] refs = ONPCR_Ladder.GetArchetypeRequests(max)
	int i = 0
	while i < refs.Length
		Actor a = Game.GetFormEx(refs[i]) as Actor
		if a
			ONPCR_Ladder.SetArchetype(a, MARAS.DetermineSocialClass(a), MARAS.DetermineSkillType(a), MARAS.DetermineTemperament(a))
		endif
		i += 1
	endwhile
EndFunction

; 0.25.0: M.A.R.A.S social class (0 Outcast ... 7 Rulers) for the "where is the little one" places (estate / kindly widow)
int Function SocialClass(Actor a) global
	if !a
		return -1
	endif
	return MARAS.DetermineSocialClass(a)
EndFunction

; is the player courting this NPC in M.A.R.A.S terms?
bool Function IsPlayerCandidate(Actor a) global
	return MARAS.IsNPCStatus(a, "candidate")
EndFunction
