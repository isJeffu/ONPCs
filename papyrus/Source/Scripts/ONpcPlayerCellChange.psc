Scriptname ONpcPlayerCellChange extends ActiveMagicEffect

Actor property PlayerRef auto
ObjectReference property InvisibleObject auto

ONpcMain property ONpc auto


Event OnEffectStart(Actor akTarget, Actor akCaster)
	if !ONpc.SettingCellChange
		ONpc.SettingCellChange = true
		Utility.Wait(0.1) ; Required
		InvisibleObject.MoveTo(PlayerRef)
		ONpc.UpdateCurrentPlayerLocation(PlayerRef.GetCurrentLocation())
		ONpc.SettingCellChange = false
	endif
EndEvent
