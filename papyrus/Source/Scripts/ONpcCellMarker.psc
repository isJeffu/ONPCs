Scriptname ONpcCellMarker extends ObjectReference
 
Actor property PlayerRef auto

ONpcMain property ONpc auto


Event OnCellDetach()
	if !ONpc.SettingCellChange && ONpc.GetNightGlobal() > 0
		ONpc.SettingCellChange = true
		Utility.Wait(0.1) ; Required
		Self.MoveTo(PlayerRef)
		ONpc.UpdateCurrentPlayerLocation(PlayerRef.GetCurrentLocation())
		ONpc.SettingCellChange = false
	endif
EndEvent
