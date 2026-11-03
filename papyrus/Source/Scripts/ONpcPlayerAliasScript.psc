Scriptname ONpcPlayerAliasScript extends ReferenceAlias  

ONpcMain property ONpc auto

; Reforged 0.8.0: the couple loop (ladder, letters, pregnancy, voice) runs here every 10 s, day and night
Event OnInit()
	RegisterForSingleUpdate(10.0)
EndEvent

Event OnPlayerLoadGame()
	ONpc.OnLoad()
	RegisterForSingleUpdate(5.0)
endevent

Event OnUpdate()
	ONpc.CoupleUpdate()
	RegisterForSingleUpdate(10.0)
EndEvent
