Scriptname TMS_ONPCFurnitureGuardNative Hidden

Bool Function IsFurnitureActuallyOccupied(ObjectReference akFurniture, Float afRadius = 4096.0, Actor akIgnoreA = None, Actor akIgnoreB = None, Actor akIgnoreC = None) Global Native

Bool Function IsFurnitureBlockedOrOccupied(ObjectReference akFurniture, Float afRadius = 4096.0, Float afBlockDistance = 192.0, Actor akIgnoreA = None, Actor akIgnoreB = None, Actor akIgnoreC = None) Global Native

Actor Function GetFurnitureOccupant(ObjectReference akFurniture, Float afRadius = 4096.0, Actor akIgnoreA = None, Actor akIgnoreB = None, Actor akIgnoreC = None) Global Native

Int Function CountFurnitureOccupants(ObjectReference akFurniture, Float afRadius = 4096.0, Actor akIgnoreA = None, Actor akIgnoreB = None, Actor akIgnoreC = None) Global Native
