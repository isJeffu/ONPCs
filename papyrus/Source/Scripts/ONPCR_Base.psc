Scriptname ONPCR_Base Hidden
; 0.26.0 base port, phase 1 - natives in ONPCs.dll (ONPCR_Settings.GetVersion() >= 19)

; known couples near the player (spouse / courting records or Reforged's own couple stages; never strangers, never family),
; only when one partner is aroused enough and the other receptive, not again too soon, best first: a flat list
; [a1, b1, a2, b2, ...], at most maxPairs pairs; an NPC can be in several pairs (take the first that passes your checks)
Actor[] Function FindPairs(int maxPairs) global native

; 0.26.0 (GetVersion() >= 21): OStim gave the couple's scene its thread id - the scene log's "start" line
Function SceneStarted(Actor a, Actor b, int threadID, ObjectReference furniture) global native

; a scene between the two ended (climaxes counted from ostim_actor_orgasm; cause 0 = OStim ended it, 1 = stopped because seen):
; past lovers from now on, the short break starts, the scene log's "end" and "after" lines
Function SceneEnded(Actor a, Actor b, int aClimaxes, int bClimaxes, int threadID, int cause) global native

; 0.26.0 phase 3 (GetVersion() >= 22): a group near the player - an NPC with two or more living partners (an orc chief and his
; wives), everyone in the mood; hub first, at most maxSize people; None / empty = no group
Actor[] Function FindGroup(int maxSize) global native

; start the engine's own "walk to" for this actor without waiting (several members walk at once); false = could not start
bool Function WalkTo(Actor who, ObjectReference target) global native

; the scene log's lines for a group scene (climaxes in the same order as the actors)
Function GroupSceneStarted(Actor[] actors, int threadID, ObjectReference furniture) global native
Function GroupSceneEnded(Actor[] actors, int[] climaxes, int threadID, int cause) global native

; 0.26.0 phase 2 (GetVersion() >= 20): is this OStim thread still running with dom in it? (OStim's own C++ API; -1 = no;
; OStim reuses thread IDs after a load, so the actor check keeps someone else's scene from looking like ours)
bool Function IsSceneRunning(int threadID, Actor dom) global native
