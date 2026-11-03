Scriptname ONpcSubthreadPackages extends Quest

Function TravelToActor(Actor Act, Actor TargetActor, ReferenceAlias Navigator1, ReferenceAlias Navigator2, ReferenceAlias NavigationTarget)
	act.StartCombat(act)
	TargetActor.StartCombat(TargetActor)

	NavigationTarget.ForceRefTo(TargetActor)

	Navigator1.ForceRefTo(Act)
	Navigator2.ForceRefTo(TargetActor)

	Act.EvaluatePackage()
	TargetActor.EvaluatePackage()
EndFunction

Function TravelToFurniture(Actor Act, Actor Act2, ObjectReference NavigationTargetRef, ReferenceAlias Navigator1, ReferenceAlias Navigator2, ReferenceAlias NavigationTarget)
	NavigationTarget.ForceRefTo(NavigationTargetRef)
	
	Navigator1.ForceRefTo(Act)
	Navigator2.ForceRefTo(Act2)

	Act.EvaluatePackage()
	Act2.EvaluatePackage()
EndFunction

Function ClearAliases(Actor Act, Actor Act2, ReferenceAlias Navigator1, ReferenceAlias Navigator2, ReferenceAlias NavigationTarget)
	Navigator1.clear()
	Navigator2.clear()
	NavigationTarget.clear()

	Act.EvaluatePackage()
	Act2.EvaluatePackage()
EndFunction

Function PackageDoNothing(Actor Act, Actor Act2, ReferenceAlias Navigator1, ReferenceAlias Navigator2)
	Navigator1.ForceRefTo(Act)
	Navigator2.ForceRefTo(Act2)

	Act.EvaluatePackage()
	Act2.EvaluatePackage()
EndFunction
