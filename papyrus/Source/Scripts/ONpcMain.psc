Scriptname ONpcMain extends Quest

; ============================================================
; Runtime state
; ============================================================


bool Scanning

; ============================================================
; MCM / behavior settings
; ============================================================

int property ActiveScenes auto

float property ScanFreq auto
float property ScanRadius auto

int property WeightThreesome auto
int property WeightMF auto
int property WeightFF auto
int property WeightMM auto
int property MaxScenes auto
int property MinRelation auto
int property MinNight auto
int property MaxNight auto
int property MaxEnemyScenesPerNight auto
int property MaxFollowerScenesPerNight auto

bool property ONpcDisabled auto

bool property AllowActiveFollowers auto
bool property ActiveFollowersEngageOtherNPCs auto
bool property AllowElderRace auto
bool property AllowPlayerThreesomes auto
bool property AllowCommonEnemies auto
bool property StopWhenFound auto
bool property TravelToLocation auto
bool property EnemiesTravelToLocation auto
bool property AllowPlayersPartners auto

bool property EnableFurniture auto
bool property FurnitureOnlyBeds auto
bool property EnemiesIgnoreScenesStartInRules auto

bool property TMSFurnitureGuardEnabled = true auto
bool property TMSFurnitureGuardDebug = false auto
float property TMSFurnitureGuardRadius = 4096.0 auto
float property TMSFurnitureNearbyBlockDistance = 192.0 auto

int property ScenesStartIn auto
int property Anywhere = 0 AutoReadOnly
int property AnyFurniture = 1 AutoReadOnly
int property BedsOnly = 2 AutoReadOnly


bool property FollowersNoScenesDungeons auto
bool property NoScenesInTowns auto
bool property NoScenesInGuilds auto
bool property NoScenesInInns auto

bool property SettingCellChange auto

; ============================================================
; OStim NPCs Reforged - relationship settings (Relations Finder + Lover's Ledger)
; ============================================================
float property StrayChance = 5.0 auto			; % chance per scan that a married/courting NPC strays
float property StrayRepeatMult = 2.0 auto		; multiplier when Lover's Ledger shows a past stray
float property HistoryWeight = 1.0 auto			; weight of the Lover's Ledger pair score
float property StrangerBaseScore = 1.0 auto		; base score every allowed stranger pair gets
bool property UseRelationsFinderFamily = true auto	; exclude family of any degree (Relations Finder)
bool property UseLoversLedger = true auto		; prefer pairs with history (Lover's Ledger)
bool property LadderAvailable = false auto		; Reforged phase 2: ONPCR_Ladder natives present (DLL 0.3.0+)
bool property MarasInstalled = false auto		; TT_MARAS.esp loaded (archetypes + candidate letters)
; 0.18.0 Beeing Female period handling (forms looked up on every settings reload; None without Beeing Female)
bool SkipPeriodScenes = true
MagicEffect BFCramps
Armor BFTampon
Spell BFBleedLow
Spell BFBleedHigh
float LastPregnancyPollHour = -1.0		; Reforged slice 4: Beeing Female state polling, once per game hour
float LastHatchHour = -1.0				; 0.21.4: Beeing Female baby items of two NPCs, checked once per game hour
Actor HeldA = None					; 0.12.3: the two people kept together for a chat (both stay put until it ends)
Actor HeldB = None
float HeldUntil = 0.0					; real time - released after this even if a chat never finished

GlobalVariable property ONpcIsNight auto
GlobalVariable property GameHour auto

; ============================================================
; Forms / references
; ============================================================

Actor property PlayerRef auto

Message property InviteMessage auto

AssociationType property ParentChild auto
AssociationType property Siblings auto
AssociationType property Cousins auto
AssociationType property Courting auto
AssociationType property Spouse auto

Faction property BanditFaction auto
Faction property ForswornFaction auto
Faction property NecromancerFaction auto
Faction property WarlockFaction auto
Faction property VampireFaction auto
Faction property VampireThrallFaction auto
Faction property Dlc2CultistFaction auto
Faction property BardSingerFaction auto
Faction property BardJobFaction auto
Faction property FollowerFaction auto
Faction property PlayerCourtingFaction auto
Faction property PlayerMarriedFaction auto

Keyword property LocTypeCity auto
Keyword property LocTypeTown auto
Keyword property LocTypeSettlement auto
Keyword property LocTypeInn auto
Keyword property LocTypeGuild auto
Keyword property LocTypeDungeon auto

Spell property CellChangeSpell auto

Race property ElderRace auto

OSexIntegrationMain property OStim auto

Quest property ONpcSubthreadQuest auto

; ============================================================
; JContainers runtime arrays
; ============================================================

int property NPCsInScene auto
int property FurnituresInUse auto
int property NPCsHadSexThisNight auto

bool property JArraysCreated auto

; ============================================================
; Optional dependency flags / runtime counters
; ============================================================

bool ORomanceInstalled
bool OPrivacyInstalled

int property FollowerScenesThisNight auto
int FollowerChanceLastHourChecked
int FollowerOtherNPCsLastHourChecked

int property EnemyScenesThisNight auto
bool property CurrentLocationEnemyScene auto
Location CurrentPlayerLocation

Faction OPFollowFaction

Package OPFollowPackage
Package OPApproachFGF1
Package OPApproachFGF2
Package OPApproachFGF1Follower
Package OPApproachElisif
Package OPApproachF1HouseCarls
Package OPApproachCoralyn
Package OPCoraApproachFollower


; ███████╗██╗   ██╗███████╗███╗   ██╗████████╗███████╗
; ██╔════╝██║   ██║██╔════╝████╗  ██║╚══██╔══╝██╔════╝
; █████╗  ██║   ██║█████╗  ██╔██╗ ██║   ██║   ███████╗
; ██╔══╝  ╚██╗ ██╔╝██╔══╝  ██║╚██╗██║   ██║   ╚════██║
; ███████╗ ╚████╔╝ ███████╗██║ ╚████║   ██║   ███████║
; ╚══════╝  ╚═══╝  ╚══════╝╚═╝  ╚═══╝   ╚═╝   ╚══════╝


; ============================================================
; Events / lifecycle
; ============================================================

Event OnInit()
	OStim = OUtils.GetOStim()
	ONpcSubthreadQuest = Game.GetFormFromFile(0x000814, "ONPCs.esp") as Quest

	OnLoad()
EndEvent


Function OnLoad()
	Scanning = false
	ActiveScenes = 0


	ORomanceInstalled = Game.IsPluginInstalled("ORomance.esp")
	; Reforged: settings bridge (ONPCs.dll) - re-read now and whenever the menu changes a value
	RegisterForModEvent("ONPCR_SettingsChanged", "OnReforgedSettingsChanged")
	RegisterForModEvent("ONPCR_VoiceTest", "OnReforgedVoiceTest")
	RegisterForModEvent("ONPCR_ImportOthers", "OnReforgedImportOthers")
	RegisterForModEvent("ONPCR_FertilityCheck", "OnReforgedFertilityCheck")
	ReloadReforgedSettings()
	OPrivacyInstalled = Game.IsPluginInstalled("OPrivacy.esp")

	if (OPrivacyInstalled)
		OPFollowFaction = Game.GetFormFromFile(0x00015C34, "OPrivacy.esp") as Faction

		OPFollowPackage = GetOPrivacyPackage(0x000012C3)
		OPApproachFGF1 = GetOPrivacyPackage(0x00131C1E)
		OPApproachFGF2 = GetOPrivacyPackage(0x0010E4C1)
		OPApproachFGF1Follower = GetOPrivacyPackage(0x00131C1D)
		OPApproachElisif = GetOPrivacyPackage(0x001A1365)
		OPApproachF1HouseCarls = GetOPrivacyPackage(0x001C9B8D)
		OPApproachCoralyn = GetOPrivacyPackage(0x004297FF)
		OPCoraApproachFollower = GetOPrivacyPackage(0x00442D10)
	endif

	if !PlayerRef.HasSpell(CellChangeSpell)
		PlayerRef.AddSpell(CellChangeSpell, false)
	endif

	FreeStuckSubthreads()

	if !ONpcDisabled
		RestartScanning()
	endif
EndFunction


Event OnUpdate()
	; the couple work (ladder tick, ledger, archetypes, news, pregnancy, voice) runs on its own day-and-night timer from
	; ONpcPlayerAliasScript since 0.8.0 - this loop only scans at night and sleeps through the day
	if !ONpcDisabled

		bool isNightTime = IsNight()

		if isNightTime && !JArraysCreated
			CreateJArrays()
		endif
	
		if !Scanning && isNightTime && !PlayerRef.IsInCombat() && ActiveScenes < MaxScenes && LocationIsValid()
			ONpcSubthread subthreadToUse = GetUnusedSubthread()

			if subthreadToUse
				Scan(subthreadToUse)
			endif
		endif

		if isNightTime
			RegisterForSingleUpdate(ScanFreq)
		else
			ResetNightVariables(0, true)
			RegisterForSingleUpdateGameTime(GetTimeUntilNight())
		endif
	endif
EndEvent


Event OnUpdateGameTime()
	PrintToConsole("Night has fallen...")

	ResetNightVariables(1, true)

	RegisterForSingleUpdate(ScanFreq)
EndEvent


; ███████╗ ██████╗ █████╗ ███╗   ██╗███╗   ██╗██╗███╗   ██╗ ██████╗ 
; ██╔════╝██╔════╝██╔══██╗████╗  ██║████╗  ██║██║████╗  ██║██╔════╝ 
; ███████╗██║     ███████║██╔██╗ ██║██╔██╗ ██║██║██╔██╗ ██║██║  ███╗
; ╚════██║██║     ██╔══██║██║╚██╗██║██║╚██╗██║██║██║╚██╗██║██║   ██║
; ███████║╚██████╗██║  ██║██║ ╚████║██║ ╚████║██║██║ ╚████║╚██████╔╝
; ╚══════╝ ╚═════╝╚═╝  ╚═╝╚═╝  ╚═══╝╚═╝  ╚═══╝╚═╝╚═╝  ╚═══╝ ╚═════╝ 


; ============================================================
; Main scan / scene selection
; ============================================================

Function Scan(ONpcSubthread SubthreadToUse)
	Scanning = true

	Actor Dom
	Actor Sub
	Actor Third

	; 0.26.0 phase 3: a group first (an NPC with two or more partners near, everyone in the mood) - they gather at a bed
	if ONPCR_Settings.GetVersion() >= 22
		Actor[] group = PickGroupFromDll()
		if group && group.Length >= 3 && StartGroup(SubthreadToUse, group)
			Scanning = false
			return
		endif	; no group (or no bed for one when scenes need furniture): a couple instead
	endif

	if ONPCR_Settings.GetVersion() >= 19
		; 0.26.0 base port, phase 1: the DLL lists known couples near the player (spouses, courting, past lovers in Lover's
		; Ledger - never strangers, never family), best first; this script keeps its own per-actor checks, the furniture
		; search and the scene. (0.26.0: OStim NPCs' old cell scan is gone - the DLL ships with the mod.)
		Actor[] chosen = PickPairFromDll()
		if chosen && chosen.Length == 2
			Dom = chosen[0]
			Sub = chosen[1]
		endif
	else
		Debug.Trace("ONPCR: the Reforged DLL is too old for this script (bridge " + ONPCR_Settings.GetVersion() + ", needs 19+) - no NPC scenes")
	endif

	if !Dom || !Sub
		Scanning = false
		return
	endif

	if !Dom.Is3DLoaded() || !Sub.Is3DLoaded() || (Third && !Third.Is3DLoaded())
		Scanning = false
		return
	endif

	ContinueScene(SubthreadToUse, Dom, Sub, Third)
EndFunction


; 0.26.0: the DLL's pairs, checked one by one with this script's own rules (followers, period, privacy, busy...);
; an NPC can be in several pairs, so a busy partner never hides their other pairs
Actor[] Function PickPairFromDll()
	Actor[] pairs = ONPCR_Base.FindPairs(16)
	Actor a
	Actor b
	int i = 0
	while i + 1 < pairs.Length
		a = pairs[i]
		b = pairs[i + 1]
		if a && b && ActorIsValid(a) && ActorIsValid(b) && PairRulesOk(a, b) && !IsRelationsFinderFamily(a, b)
			Actor[] pick = new Actor[2]
			pick[0] = a
			pick[1] = b
			return pick
		endif
		i += 2
	endwhile
	return None
EndFunction


; the old per-pair rules the DLL does not know: no enemy with a non-enemy, enemy couples only where this location rolled an
; enemy scene, and followers only on their hourly chance (as GetPartnerForActor did)
Bool Function PairRulesOk(Actor a, Actor b)
	bool enemyA = isEnemy(a)
	bool enemyB = isEnemy(b)
	if enemyA != enemyB
		return false
	endif
	if enemyA && !CurrentLocationEnemyScene
		return false
	endif
	if (IsFollower(a) || IsFollower(b)) && !WillBeFollowerOnNpcScene()
		return false
	endif
	return true
EndFunction


; 0.26.0 phase 3: the DLL's group, each member checked with this script's own rules; anyone who fails is left out (a group
; needs at least three, the hub first)
Actor[] Function PickGroupFromDll()
	Actor[] offered = ONPCR_Base.FindGroup(5)
	if !offered || offered.Length < 3
		return None
	endif
	Actor hub = offered[0]
	if !hub || hub.GetSleepState() != 0 || !ActorIsValid(hub)
		return None
	endif
	Actor[] kept = PapyrusUtil.ActorArray(1, hub)
	Actor m
	int i = 1
	while i < offered.Length
		m = offered[i]
		if m && m.GetSleepState() == 0 && ActorIsValid(m) && PairRulesOk(hub, m) && !IsRelationsFinderFamily(hub, m) && !FamilyOfAny(m, kept, 1)
			kept = PapyrusUtil.PushActor(kept, m)
		endif
		i += 1
	endwhile
	if kept.Length < 3
		return None
	endif
	return kept
EndFunction


; two partners who share the hub are in one scene together: Relations Finder family between them (sisters, cousins) also
; keeps one out
bool Function FamilyOfAny(Actor m, Actor[] kept, int from)
	int k = from
	while k < kept.Length
		if IsRelationsFinderFamily(kept[k], m)
			return true
		endif
		k += 1
	endwhile
	return false
EndFunction


; a bed (or other furniture) big enough for the group, near the hub; none = OStim's standing scenes, unless the settings
; want scenes on furniture only (the same rule as a couple); false = no group this scan
bool Function StartGroup(ONpcSubthread SubthreadToUse, Actor[] group)
	ObjectReference furnitureRef
	if EnableFurniture
		furnitureRef = FindEmptyFurniture(group[0], group[1], group[2], group.Length)
		if (ScenesStartIn == AnyFurniture || ScenesStartIn == BedsOnly) && !furnitureRef
			if !(IsEnemy(group[0]) && IsEnemy(group[1]) && EnemiesIgnoreScenesStartInRules)
				return false
			endif
		endif
	endif
	ActiveScenes += 1
	int i = 0
	while i < group.Length
		JArray.addForm(NPCsInScene, group[i])
		i += 1
	endwhile
	SubthreadToUse.SetupGroupScene(group, furnitureRef)
	return true
EndFunction


; Relations Finder knows family the game records don't link directly (in-laws, cousins): one lookup, only for the chosen pair
Bool Function IsRelationsFinderFamily(Actor a, Actor b)
	if !UseRelationsFinderFamily
		return false
	endif
	return GetFamilyOf(a).Find(b) >= 0
EndFunction



; the chosen pair: furniture or bed, then the subthread takes over (unchanged from OStim NPCs)
Function ContinueScene(ONpcSubthread SubthreadToUse, Actor Dom, Actor Sub, Actor Third)
	bool alternativeBedSearch

	; If either actor is sleeping, we use an alternate bed search
	; Since the normal one will not find the bed they're sleeping on
	; We then use the bed the actor was sleeping on to prevent them from clipping into the bed
	if Dom.GetSleepState() != 0 || Sub.GetSleepState() != 0
		alternativeBedSearch = true
	endif

	ObjectReference furnitureRef

	if alternativeBedSearch
		furnitureRef = FindEmptyBed(Dom, Sub, Third)

		if !furnitureRef
			Scanning = false
			return
		endif
	elseif EnableFurniture
		furnitureRef = FindEmptyFurniture(Dom, Sub, Third)

		if (ScenesStartIn == AnyFurniture || ScenesStartIn == BedsOnly) && !furnitureRef
			if IsEnemy(Dom) && IsEnemy(Sub)
				if !EnemiesIgnoreScenesStartInRules
					Scanning = False
					return
				endif
			else
				Scanning = false
				return
			endif
		endif
	endif

	ActiveScenes += 1

	JArray.addForm(NPCsInScene, Dom)
	JArray.addForm(NPCsInScene, Sub)

	if Third
		JArray.addForm(NPCsInScene, Third)
	endif

	SubthreadToUse.SetupScene(Dom, Sub, furnitureRef, alternativeBedSearch, None)

	Scanning = false
EndFunction


; ============================================================
; TMS Furniture Guard helpers
; ============================================================

Function TMS_FurnitureGuardLog(String MessageText)
	if TMSFurnitureGuardDebug
		Debug.Trace("[TMS ONPC FurnitureGuard] " + MessageText)
	endif
EndFunction


Bool Function TMS_IsIgnoredFurnitureActor(Actor Act, Actor Dom, Actor Sub = None, Actor Third = None)
	if !Act
		return true
	endif

	if Act == Dom || Act == Sub || Act == Third
		return true
	endif

	return false
EndFunction


Bool Function TMS_IsFurnitureGuardEnabled()
	if !TMSFurnitureGuardEnabled
		return false
	endif

	return true
EndFunction


Float Function TMS_GetFurnitureGuardRadius()
	if TMSFurnitureGuardRadius <= 0.0
		return 4096.0
	endif

	return TMSFurnitureGuardRadius
EndFunction


Float Function TMS_GetNearbyBlockDistance()
	if TMSFurnitureNearbyBlockDistance <= 0.0
		return 192.0
	endif

	return TMSFurnitureNearbyBlockDistance
EndFunction


Bool Function TMS_IsFurnitureOccupiedByOther(ObjectReference FurnitureToCheck, Actor Dom, Actor Sub = None, Actor Third = None)
	if !TMS_IsFurnitureGuardEnabled()
		return false
	endif

	if !FurnitureToCheck
		return false
	endif

	Bool occupied = TMS_ONPCFurnitureGuardNative.IsFurnitureActuallyOccupied(FurnitureToCheck, TMS_GetFurnitureGuardRadius(), Dom, Sub, Third)

	if occupied
		TMS_FurnitureGuardLog("scan gate: native occupied furniture " + FurnitureToCheck)
		return true
	endif

	return false
EndFunction


Bool Function TMS_IsFurnitureUnsafeForSceneStart(ObjectReference FurnitureToCheck, Actor Dom, Actor Sub = None, Actor Third = None)
	if !TMS_IsFurnitureGuardEnabled()
		return false
	endif

	if !FurnitureToCheck
		return false
	endif

	Bool unsafe = TMS_ONPCFurnitureGuardNative.IsFurnitureBlockedOrOccupied(FurnitureToCheck, TMS_GetFurnitureGuardRadius(), TMS_GetNearbyBlockDistance(), Dom, Sub, Third)

	if unsafe
		TMS_FurnitureGuardLog("scene gate: furniture blocked or occupied " + FurnitureToCheck)
		return true
	endif

	return false
EndFunction


; ============================================================
; Furniture selection
; ============================================================

ObjectReference Function FindEmptyFurniture(Actor Dom, Actor Sub = None, Actor Third = None, int ActorCount = 2)
	ObjectReference[] Furnitures = OFurniture.FindFurniture(ActorCount, Dom, (OStim.FurnitureSearchDistance + 15) * 100.0, 96)

	int i = Furnitures.Length

	bool useFurniture

	ObjectReference currentFurniture

	While i > 0
		i -= 1

		currentFurniture = Furnitures[i]

		; Check if furniture is already being used by another OStim scene
		; Unfortunately, we have to do it our own way with JArrays since we can't reliable mark furniture
		; as being in use with Skyrim's native functions
		if JArray.FindForm(FurnituresInUse, currentFurniture) == -1
			useFurniture = true

			; If scenes are set to only start in beds, then we won't return furniture that aren't beds
			if FurnitureOnlyBeds || ScenesStartIn == BedsOnly
				if !IsFurnitureBed(currentFurniture)
					useFurniture = false
				endif
			endif

			if useFurniture && TMS_IsFurnitureOccupiedByOther(currentFurniture, Dom, Sub, Third)
				useFurniture = false
			endif

			if useFurniture
				JArray.addForm(FurnituresInUse, currentFurniture)
				return currentFurniture
			endif
		endif
	EndWhile

	return none
EndFunction


ObjectReference Function FindEmptyBed(Actor Dom, Actor Sub = None, Actor Third = None)
	ObjectReference[] Beds = OSANative.FindBed(Dom, (OStim.FurnitureSearchDistance + 15) * 100.0, 96.0)

	Int i = Beds.Length

	While i > 0
		i -= 1

		ObjectReference Bed = Beds[i]

		if JArray.FindForm(FurnituresInUse, Bed) == -1
			if !TMS_IsFurnitureOccupiedByOther(Bed, Dom, Sub, Third)
				JArray.addForm(FurnituresInUse, Bed)
				return Bed
			endif
		endif
	EndWhile

	Return None
EndFunction

;  █████╗  ██████╗████████╗ ██████╗ ██████╗     ██╗   ██╗████████╗██╗██╗     ███████╗
; ██╔══██╗██╔════╝╚══██╔══╝██╔═══██╗██╔══██╗    ██║   ██║╚══██╔══╝██║██║     ██╔════╝
; ███████║██║        ██║   ██║   ██║██████╔╝    ██║   ██║   ██║   ██║██║     ███████╗
; ██╔══██║██║        ██║   ██║   ██║██╔══██╗    ██║   ██║   ██║   ██║██║     ╚════██║
; ██║  ██║╚██████╗   ██║   ╚██████╔╝██║  ██║    ╚██████╔╝   ██║   ██║███████╗███████║
; ╚═╝  ╚═╝ ╚═════╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝     ╚═════╝    ╚═╝   ╚═╝╚══════╝╚══════╝


; ============================================================
; Actor validation / filtering
; ============================================================

Bool Function ActorIsValid(Actor Act)
	if IsInvalidNpc(Act) || IsPlayerORomancePartner(Act) || ActorIsInCombatState(Act) || Act.GetCurrentScene() != None || !Act.Is3DLoaded() || Act.IsInDialogueWithPlayer() || OStim.IsActorActive(Act) || IsInJArrays(Act) || ActorHasOPrivacyPackage(Act) || IsPlayerPartner(Act)
		return false
	endif

	return true
EndFunction


Bool Function ActorIsInCombatState(Actor Act)
	return Act.GetCombatState() != 0 || Act.IsBleedingOut() || Act.IsWeaponDrawn() || Act.IsUnconscious() || Act.IsInKillMove() || Act.IsArrested() || Act.IsCommandedActor() || Act.IsOnMount()
EndFunction


Bool Function IsInvalidNpc(Actor Act)
	return !Act || Act == PlayerRef || !Act.HasKeywordString("ActorTypeNPC") || Act.IsDead() || Act.isDisabled() || Act.IsChild() || Act.isGhost() || IsInvalidNpcUserPreferences(Act)
EndFunction


Bool Function IsInPeriod(Actor Act)
	return BFCramps && Act && Act.HasMagicEffect(BFCramps)
EndFunction


; 0.18.0: OStim undresses the tampon and Beeing Female does not put a new one on during a scene, so its next check
; paints the bleeding effect over her (clothes too). After the scene: tampon back on, bleeding effect off.
Function PeriodAfterScene(Actor Act)
	if !IsInPeriod(Act) || !BFTampon
		return
	endif
	if Act.GetItemCount(BFTampon) < 1
		return  ; none with her: Beeing Female's own check hands NPCs new ones
	endif
	if !Act.IsEquipped(BFTampon)
		Act.EquipItem(BFTampon, false, true)
	endif
	; equipping can take a moment on an NPC, so don't wait for IsEquipped - she has one on, the bleeding goes
	if BFBleedLow
		Act.DispelSpell(BFBleedLow)
		Act.RemoveSpell(BFBleedLow)
	endif
	if BFBleedHigh
		Act.DispelSpell(BFBleedHigh)
		Act.RemoveSpell(BFBleedHigh)
	endif
EndFunction


Bool Function IsPlayerPartner(Actor Act)
	if (AllowPlayersPartners)
		return false
	endif
	
	return Act.GetRelationshipRank(PlayerRef) == 4 || Act.IsInFaction(PlayerCourtingFaction) || Act.IsInFaction(PlayerMarriedFaction) || ONPCR_Ladder.InPlayerPartnerFaction(Act)	; 0.21.2: a follower mod's own romance (Couples json)
EndFunction


Bool Function IsFollower(Actor Act)
	return Act.IsPlayerTeamMate() || Act.IsInFaction(FollowerFaction)
EndFunction


Bool Function IsInvalidNpcUserPreferences(Actor Act)
	if Act.GetRace() == ElderRace && !AllowElderRace
		return true
	endif

	; 0.18.0: no scenes for a woman in her period (Beeing Female gives cramps for the whole phase)
	if SkipPeriodScenes && IsInPeriod(Act)
		return true
	endif

	if !AllowActiveFollowers && !ActiveFollowersEngageOtherNPCs && IsFollower(Act)
		return true
	endif

	if (AllowActiveFollowers || ActiveFollowersEngageOtherNPCs) && IsFollower(Act)
		if FollowerScenesThisNight >= MaxFollowerScenesPerNight
			return true
		endif

		if FollowersNoScenesDungeons && IsDungeon(PlayerRef.GetCurrentLocation())
			return true
		endif
	endif

	; 0.22.2: generic-named NPCs ("Hjaalmarch Guard Quartermaster") and guards - the same name list as the off-screen pool; enemies keep their own rules below
	if !isEnemy(Act) && ONPCR_Settings.GetVersion() >= 14 && ONPCR_Ladder.IsSceneExcluded(Act)
		return true
	endif

	if !AllowCommonEnemies && isEnemy(Act)
		return true
	endif

	if AllowCommonEnemies && isEnemy(Act) && EnemyScenesThisNight >= MaxEnemyScenesPerNight
		return true
	endif

	return false
EndFunction


Bool Function IsPlayerORomancePartner(Actor Act)
	if !ORomanceInstalled
		return false 
	endif 

	return StorageUtil.GetIntValue(Act, "or_k_part", -1) == 1
EndFunction


Bool Function ActorHasOPrivacyPackage(Actor Act)
	if OPrivacyInstalled
		if Act.IsInFaction(OPFollowFaction)
			return true
		endif

		bool approachEnabled = (Game.GetFormFromFile(0x001646B2, "OPrivacy.esp") as GlobalVariable).GetValueInt() == 1

		if approachEnabled
			Package currentActorPackage = Act.GetCurrentPackage()

			return currentActorPackage == OPFollowPackage || currentActorPackage == OPApproachFGF1 || currentActorPackage == OPApproachFGF2 || currentActorPackage == OPApproachFGF1Follower || currentActorPackage == OPApproachElisif || currentActorPackage == OPApproachF1HouseCarls || currentActorPackage == OPApproachCoralyn || currentActorPackage == OPCoraApproachFollower
		endif
	endif

	return false
EndFunction


Bool Function IsInJArrays(Actor Act)
	return JArray.FindForm(NPCsInScene, Act) != -1 || JArray.FindForm(NPCsHadSexThisNight, Act) != -1
EndFunction


Bool Function IsBard(Actor Act)
	return Act.IsInFaction(BardSingerFaction) || Act.IsInFaction(BardJobFaction)
EndFunction


Bool Function isEnemy(Actor Act)
	return Act.IsInFaction(BanditFaction) || Act.IsInFaction(ForswornFaction) || Act.IsInFaction(NecromancerFaction) || Act.IsInFaction(WarlockFaction) || Act.IsInFaction(VampireFaction) || Act.IsInFaction(VampirethrallFaction) || Act.IsInFaction(Dlc2CultistFaction)
EndFunction


Bool Function InvitePlayer()
	int button = invitemessage.show()
	if button == 0
		return true
	endif
	return false
EndFunction


; Reforged: read the six settings from the DLL bridge; without the DLL the script defaults stay
Function ReloadReforgedSettings()
	LadderAvailable = false
	if SKSE.GetPluginVersion("ONPCs") < 0
		return
	endif
	LadderAvailable = ONPCR_Settings.GetVersion() >= 6
	MarasInstalled = Game.IsPluginInstalled("TT_MARAS.esp")
	; main
	ONpcDisabled = ONPCR_Settings.GetBool("Disabled", ONpcDisabled)
	StopWhenFound = ONPCR_Settings.GetBool("StopWhenFound", StopWhenFound)
	TravelToLocation = ONPCR_Settings.GetBool("TravelToLocation", TravelToLocation)
	EnemiesTravelToLocation = ONPCR_Settings.GetBool("EnemiesTravelToLocation", EnemiesTravelToLocation)
	; followers and enemies
	AllowActiveFollowers = ONPCR_Settings.GetBool("AllowActiveFollowers", AllowActiveFollowers)
	ActiveFollowersEngageOtherNPCs = ONPCR_Settings.GetBool("ActiveFollowersEngageOtherNPCs", ActiveFollowersEngageOtherNPCs)
	AllowPlayersPartners = ONPCR_Settings.GetBool("AllowPlayersPartners", AllowPlayersPartners)
	AllowElderRace = ONPCR_Settings.GetBool("AllowElderRace", AllowElderRace)
	AllowCommonEnemies = ONPCR_Settings.GetBool("AllowCommonEnemies", AllowCommonEnemies)
	FollowersNoScenesDungeons = ONPCR_Settings.GetBool("FollowersNoScenesDungeons", FollowersNoScenesDungeons)
	; 0.18.0: Beeing Female period (soft - nothing happens without Beeing Female)
	SkipPeriodScenes = ONPCR_Settings.GetBool("SkipPeriodScenes", SkipPeriodScenes)
	BFCramps = None
	BFTampon = None
	BFBleedLow = None
	BFBleedHigh = None
	if Game.GetModByName("BeeingFemale.esm") != 255
		BFCramps = Game.GetFormFromFile(0x0053C6, "BeeingFemale.esm") as MagicEffect
		BFTampon = Game.GetFormFromFile(0x064358, "BeeingFemale.esm") as Armor
		BFBleedLow = Game.GetFormFromFile(0x006EBC, "BeeingFemale.esm") as Spell
		BFBleedHigh = Game.GetFormFromFile(0x006EBD, "BeeingFemale.esm") as Spell
	endif
	; furniture
	EnableFurniture = ONPCR_Settings.GetBool("EnableFurniture", EnableFurniture)
	FurnitureOnlyBeds = ONPCR_Settings.GetBool("FurnitureOnlyBeds", FurnitureOnlyBeds)
	ScenesStartIn = ONPCR_Settings.GetInt("ScenesStartIn", ScenesStartIn)
	EnemiesIgnoreScenesStartInRules = ONPCR_Settings.GetBool("EnemiesIgnoreScenesStartInRules", EnemiesIgnoreScenesStartInRules)
	; locations
	NoScenesInTowns = ONPCR_Settings.GetBool("NoScenesInTowns", NoScenesInTowns)
	NoScenesInGuilds = ONPCR_Settings.GetBool("NoScenesInGuilds", NoScenesInGuilds)
	NoScenesInInns = ONPCR_Settings.GetBool("NoScenesInInns", NoScenesInInns)
	; limits and timing
	MaxScenes = ONPCR_Settings.GetInt("MaxScenes", MaxScenes)
	MaxFollowerScenesPerNight = ONPCR_Settings.GetInt("MaxFollowerScenesPerNight", MaxFollowerScenesPerNight)
	MaxEnemyScenesPerNight = ONPCR_Settings.GetInt("MaxEnemyScenesPerNight", MaxEnemyScenesPerNight)
	MinNight = ONPCR_Settings.GetInt("MinNight", MinNight)
	MaxNight = ONPCR_Settings.GetInt("MaxNight", MaxNight)
	ScanFreq = ONPCR_Settings.GetInt("ScanFreq", ScanFreq as int) as float
	; 0.26.0: the pair weights, minimum relationship, stray chance, history weight and stranger score are no longer read here -
	; the DLL picks the couples (the weights are its on/off per pairing); their properties stay declared for old saves
	UseRelationsFinderFamily = ONPCR_Settings.GetBool("UseRelationsFinderFamily", UseRelationsFinderFamily)
	UseLoversLedger = ONPCR_Settings.GetBool("UseLoversLedger", UseLoversLedger)
	; advanced
	TMSFurnitureGuardDebug = ONPCR_Settings.GetBool("FurnitureGuardDebug", TMSFurnitureGuardDebug)
EndFunction

; strArg = the key that changed ("*" after a reset). Keeps the two side effects the old MCM had.
Event OnReforgedSettingsChanged(string eventName, string strArg, float numArg, Form sender)
	bool wasDisabled = ONpcDisabled
	int oldMinNight = MinNight
	ReloadReforgedSettings()
	if wasDisabled != ONpcDisabled
		if ONpcDisabled
			ResetNightVariables(0, true)
		else
			RestartScanning()
		endif
	elseif MinNight != oldMinNight && !ONpcDisabled && !IsNight()
		RestartScanning()
	endif
EndEvent

Actor[] Function MergeActors(Actor[] a, Actor[] b)
	if !b
		return a
	endif
	int i = 0
	while i < b.Length
		if b[i] && a.Find(b[i]) < 0
			a = PapyrusUtil.PushActor(a, b[i])
		endif
		i += 1
	endwhile
	return a
EndFunction

; every family relation Relations Finder knows, blood and in-law, up to great-grandparents
Actor[] Function GetFamilyOf(Actor act)
	Actor[] fam = PapyrusUtil.ActorArray(0)
	fam = MergeActors(fam, TTRF_RelationsFinder.GetParentChildren(act))
	fam = MergeActors(fam, TTRF_RelationsFinder.GetSiblings(act))
	fam = MergeActors(fam, TTRF_RelationsFinder.GetCousins(act))
	fam = MergeActors(fam, TTRF_RelationsFinder.GetUncleAuntNieceNephew(act))
	fam = MergeActors(fam, TTRF_RelationsFinder.GetGrandParentChild(act))
	fam = MergeActors(fam, TTRF_RelationsFinder.GetGreatGrandparentChild(act))
	fam = MergeActors(fam, TTRF_RelationsFinder.GetGrandAuntUncleNieceNephew(act))
	fam = MergeActors(fam, TTRF_RelationsFinder.GetInLawSiblings(act))
	fam = MergeActors(fam, TTRF_RelationsFinder.GetInLawParentChild(act))
	fam = MergeActors(fam, TTRF_RelationsFinder.GetInLawAuntUncleNieceNephew(act))
	fam = MergeActors(fam, TTRF_RelationsFinder.GetInLawGrandParentChild(act))
	return fam
EndFunction


Bool Function WillBeFollowerOnNpcScene()
	if AllowActiveFollowers && !ActiveFollowersEngageOtherNPCs
		return false
	endif

	if !AllowActiveFollowers && ActiveFollowersEngageOtherNPCs
		return true
	endif

	return OUtils.ChanceRoll(50)
EndFunction


; ██╗      ██████╗  ██████╗ █████╗ ████████╗██╗ ██████╗ ███╗   ██╗    ██╗   ██╗████████╗██╗██╗     ███████╗
; ██║     ██╔═══██╗██╔════╝██╔══██╗╚══██╔══╝██║██╔═══██╗████╗  ██║    ██║   ██║╚══██╔══╝██║██║     ██╔════╝
; ██║     ██║   ██║██║     ███████║   ██║   ██║██║   ██║██╔██╗ ██║    ██║   ██║   ██║   ██║██║     ███████╗
; ██║     ██║   ██║██║     ██╔══██║   ██║   ██║██║   ██║██║╚██╗██║    ██║   ██║   ██║   ██║██║     ╚════██║
; ███████╗╚██████╔╝╚██████╗██║  ██║   ██║   ██║╚██████╔╝██║ ╚████║    ╚██████╔╝   ██║   ██║███████╗███████║
; ╚══════╝ ╚═════╝  ╚═════╝╚═╝  ╚═╝   ╚═╝   ╚═╝ ╚═════╝ ╚═╝  ╚═══╝     ╚═════╝    ╚═╝   ╚═╝╚══════╝╚══════╝


; ============================================================
; Location checks
; ============================================================

Bool Function IsSettlement(Location Loc)
	return Loc.HasKeyword(LocTypeTown) || Loc.HasKeyword(LocTypeCity) || Loc.HasKeyword(LocTypeSettlement)
EndFunction


Bool Function IsInn(Location Loc)
	return Loc.HasKeyword(LocTypeInn)
EndFunction


Bool Function IsGuild(Location Loc)
	return Loc.HasKeyword(LocTypeGuild)
EndFunction


Bool Function IsDungeon(Location Loc)
	; Reforged: wilderness has no location (None) - the upstream call logged an error every scan there
	return Loc && Loc.HasKeyword(LocTypeDungeon)
EndFunction


Bool Function LocationIsValid()
	Location playerLocation = PlayerRef.GetCurrentLocation()

	if !playerLocation
		return true
	endif

	if NoScenesInTowns && IsSettlement(playerLocation)
		return false
	endif

	if NoScenesInGuilds && IsGuild(playerLocation)
		return false
	endif

	if NoScenesInInns && IsInn(playerLocation)
		return false
	endif

	return true
EndFunction


; ███╗   ██╗██╗ ██████╗ ██╗  ██╗████████╗    ██╗   ██╗████████╗██╗██╗     ███████╗
; ████╗  ██║██║██╔════╝ ██║  ██║╚══██╔══╝    ██║   ██║╚══██╔══╝██║██║     ██╔════╝
; ██╔██╗ ██║██║██║  ███╗███████║   ██║       ██║   ██║   ██║   ██║██║     ███████╗
; ██║╚██╗██║██║██║   ██║██╔══██║   ██║       ██║   ██║   ██║   ██║██║     ╚════██║
; ██║ ╚████║██║╚██████╔╝██║  ██║   ██║       ╚██████╔╝   ██║   ██║███████╗███████║
; ╚═╝  ╚═══╝╚═╝ ╚═════╝ ╚═╝  ╚═╝   ╚═╝        ╚═════╝    ╚═╝   ╚═╝╚══════╝╚══════╝


; ============================================================
; Time helpers
; ============================================================

Bool Function IsNight()
	float hour = CurrentHour()

	return hour <= MaxNight || hour >= MinNight
EndFunction


Float Function CurrentHour()
	return GameHour.GetValue()
EndFunction


Float Function GetTimeUntilNight()
	if IsNight()
		return 0.05
	else 
		float ret = (MinNight - CurrentHour())
		if ret < 0.1
			ret = 0.05
		endif 
		return ret
	endif
EndFunction


;  ██████╗ ███████╗███╗   ██╗███████╗██████╗  █████╗ ██╗         ██╗   ██╗████████╗██╗██╗     ███████╗
; ██╔════╝ ██╔════╝████╗  ██║██╔════╝██╔══██╗██╔══██╗██║         ██║   ██║╚══██╔══╝██║██║     ██╔════╝
; ██║  ███╗█████╗  ██╔██╗ ██║█████╗  ██████╔╝███████║██║         ██║   ██║   ██║   ██║██║     ███████╗
; ██║   ██║██╔══╝  ██║╚██╗██║██╔══╝  ██╔══██╗██╔══██║██║         ██║   ██║   ██║   ██║██║     ╚════██║
; ╚██████╔╝███████╗██║ ╚████║███████╗██║  ██║██║  ██║███████╗    ╚██████╔╝   ██║   ██║███████╗███████║
;  ╚═════╝ ╚══════╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝     ╚═════╝    ╚═╝   ╚═╝╚══════╝╚══════╝


; ============================================================
; Utility helpers
; ============================================================

Function PrintToConsole(String In) Global
	MiscUtil.PrintConsole("OStim NPCs: " + In)
EndFunction


Function UpdateCurrentPlayerLocation(Location newLocation)
	if newLocation == None
		CurrentLocationEnemyScene = False
		CurrentPlayerLocation = None
	elseif CurrentPlayerLocation != newLocation
		; RNG on top of RNG because one RNG isn't enough
		int locationEnemySceneChance = Utility.RandomInt(30, 100)
		CurrentLocationEnemyScene = Utility.RandomInt(0, 99) < locationEnemySceneChance
		CurrentPlayerLocation = newLocation
	endif
EndFunction


Function RestartScanning()
	if isNight()
		ResetNightVariables(1, false)
		RegisterForSingleUpdate(ScanFreq)
	else
		RegisterForSingleUpdateGameTime(GetTimeUntilNight())
	endif
EndFunction


; 0.26.0 phase 2: on load, a slot whose OStim scene is gone (saved mid-scene, the end event never came) stayed "busy" for good.
; Free those, and count the slots that are really busy (a slot still walking its NPCs over has no scene yet and is kept)
Function FreeStuckSubthreads()
	if ONPCR_Settings.GetVersion() < 20 || !ONpcSubthreadQuest
		return
	endif
	int busy = 0
	int i = 0
	ONpcSubthread st
	while i < 4
		st = ONpcSubthreadQuest.GetNthAlias(i) as ONpcSubthread
		if st && st.IsThreadInUse()
			if st.GetOStimThreadID() >= 0 && !ONPCR_Base.IsSceneRunning(st.GetOStimThreadID(), st.GetDomActor())
				st.FreeStuckSlot()
			else
				busy += 1
			endif
		endif
		i += 1
	endwhile
	ActiveScenes = busy
EndFunction


ONpcSubthread Function GetUnusedSubthread()
	int i = 0
	int max = 4

	while i < max 
		ONpcSubthread thread = ONpcSubthreadQuest.GetNthAlias(i) as ONpcSubthread

		if thread && !thread.IsThreadInUse()
			return thread
		endif

		i += 1
	endwhile
EndFunction


Bool Function IsFurnitureBed(ObjectReference FurnitureToCheck)
	String furnitureType = OFurniture.GetFurnitureType(FurnitureToCheck)

	return furnitureType == "bedroll" || furnitureType == "singlebed" || furnitureType == "doublebed"
EndFunction


Function SetIsNightGlobal(float value)
	ONpcIsNight.SetValue(value)
EndFunction


Int Function GetNightGlobal()
	return ONpcIsNight.GetValue() as int
EndFunction


Int Function GetFollowerSceneChance()
	float hour = CurrentHour()

	int totalNightHours = (24 - MinNight) + MaxNight

	float hoursSinceNightStarted = 0.0

	if hour >= 0 && hour <= MaxNight
		hoursSinceNightStarted = (24 - MinNight) + hour
	else
		hoursSinceNightStarted = hour - MinNight
	endif

	; as night progresses, the chance of followers engaging in OStim scenes increases
	return Math.Ceiling((hoursSinceNightStarted / totalNightHours) * 100)

EndFunction


; ============================================================
; JArray management
; ============================================================

Function CreateJArrays()
	NPCsInScene = JArray.object()
	JValue.retain(NPCsInScene, "npcsInScene")

	FurnituresInUse = JArray.object()
	JValue.retain(FurnituresInUse, "furnituresInUse")

	NPCsHadSexThisNight = JArray.object()
	JValue.retain(NPCsHadSexThisNight, "npcsHadSexThisNight")

	JArraysCreated = true
EndFunction


Function ClearJArrays()
	JArray.Clear(NPCsInScene)
	JArray.Clear(NPCsHadSexThisNight)
	JArray.Clear(FurnituresInUse)

	JValue.release(NPCsInScene)
	JValue.release(NPCsHadSexThisNight)
	JValue.release(FurnituresInUse)

	JArraysCreated = false
EndFunction


Function ResetNightVariables(int NightValue, bool ResetJArrays)
	FollowerChanceLastHourChecked = -1
	FollowerOtherNPCsLastHourChecked = -1
	EnemyScenesThisNight = 0
	FollowerScenesThisNight = 0

	SetIsNightGlobal(NightValue)

	if JArraysCreated && ResetJArrays
		ClearJArrays()
	endif
EndFunction


Package Function GetOPrivacyPackage(int PackageID)
	return Game.GetFormFromFile(PackageID, "OPrivacy.esp") as Package
EndFunction

; Reforged phase 2: a real NPC-NPC scene ended - feed the ladder (game-record couples are flagged so they are protected / left alone)
Function LadderRecordScene(Actor a, Actor b)
	if !LadderAvailable || !a || !b || a == PlayerRef || b == PlayerRef
		return
	endif
	int vanillaRelation = 0
	if TTRF_RelationsFinder.GetSpouse(a) == b
		vanillaRelation = 2
	elseif TTRF_RelationsFinder.GetCourting(a) == b
		vanillaRelation = 1
	endif
	ONPCR_Ladder.RecordEncounter(a, b, false, vanillaRelation)
EndFunction

; Reforged phase 2 slice 2: write the DLL's simulated encounters into Lover's Ledger (unique NPCs are persistent, so their refs resolve while unloaded)
Function FlushLadderLedger()
	if !UseLoversLedger
		return
	endif
	int[] w = ONPCR_Ladder.PopLedgerWrites()
	if !w || w.Length < 3
		return
	endif
	int i = 0
	while i + 2 < w.Length
		Actor a = Game.GetFormEx(w[i]) as Actor
		Actor b = Game.GetFormEx(w[i + 1]) as Actor
		float day = w[i + 2] as float
		if a && b
			TTLL_Store.SetLoverInt(a, b, "exclusivesex", TTLL_Store.GetLoverInt(a, b, "exclusivesex") + 1)
			TTLL_Store.SetLoverInt(b, a, "exclusivesex", TTLL_Store.GetLoverInt(b, a, "exclusivesex") + 1)
			TTLL_Store.SetLoverFlt(a, b, "lasttime", day)
			TTLL_Store.SetLoverFlt(b, a, "lasttime", day)
			TTLL_Store.SetNpcFlt(a, "lasttime", day)
			TTLL_Store.SetNpcFlt(b, "lasttime", day)
			TTLL_Store.SetLoverFlt(a, b, "orgasms", TTLL_Store.GetLoverFlt(a, b, "orgasms") + 1.0)
			TTLL_Store.SetLoverFlt(b, a, "orgasms", TTLL_Store.GetLoverFlt(b, a, "orgasms") + 1.0)
			TTLL_Store.IncrementInt(a, "exclusivesex")
			TTLL_Store.IncrementInt(b, "exclusivesex")
			Debug.Trace("ONPCR: ledger write " + a.GetDisplayName() + " + " + b.GetDisplayName() + " day " + day)
			HandToPregnancyEngine(a, b)
		endif
		i += 3
	endwhile
EndFunction

; Reforged phase 2 slice 3: courier letters about couples close to the player
Bool Function IsFriendOfPlayer(Actor a)
	return a && a != PlayerRef && a.GetRelationshipRank(PlayerRef) >= ONPCR_Settings.GetInt("LetterMinRank", 3)
EndFunction

Bool Function IsPlayerCandidate(Actor a)
	return a && a != PlayerRef && MarasInstalled && ONPCR_MarasBridge.IsPlayerCandidate(a)
EndFunction

; a named courier letter (0.8.1): ONPCR_LetterQuest (Letters plugin 0x8DE) holds Writer + Partner (StoresText); the letter
; reference is forced into its UsesStoredText alias, which bakes the names into that copy - as vanilla WIAddItem07 does
; 0.25.0: writer / partner may also be a placeholder reference renamed to a child who is away (no actor exists yet, see
; SendChildLetters); it is kept on the quest and deleted when that quest slot is used again
Function SendLetter(WICourierScript courier, bool letters, int letterId, string text, ObjectReference writer, ObjectReference partner)
	if ONPCR_Settings.GetInt("Notifications", 1) >= 1
		Debug.Notification(text)
	endif
	if !letters || !courier || !writer || !partner
		return
	endif
	Book letter = Game.GetFormFromFile(letterId, "OStimNPCsReforged - Letters.esp") as Book
	; 0.8.3: a pool of 20 letter quests (FormList 0x8F2), used in turn - restarting a quest wipes the names of the letter it made,
	; so each letter keeps its names until 20 newer letters have been sent
	FormList pool = Game.GetFormFromFile(0x8F2, "OStimNPCsReforged - Letters.esp") as FormList
	if !letter || !pool || pool.GetSize() == 0
		return
	endif
	int slot = StorageUtil.GetIntValue(None, "ONPCR_LetterSlot", 0) % pool.GetSize()
	StorageUtil.SetIntValue(None, "ONPCR_LetterSlot", slot + 1)
	Quest q = pool.GetAt(slot) as Quest
	if !q
		return
	endif
	if q.IsRunning()
		q.Stop()
		int w = 0
		while q.IsRunning() && w < 20
			Utility.Wait(0.1)
			w += 1
		endwhile
	endif
	ObjectReference oldHolder = StorageUtil.GetFormValue(q, "ONPCR.LetterHolder") as ObjectReference
	if oldHolder
		StorageUtil.UnsetFormValue(q, "ONPCR.LetterHolder")
		oldHolder.Delete()
	endif
	q.Start()
	(q.GetAlias(0) as ReferenceAlias).ForceRefTo(writer)
	(q.GetAlias(1) as ReferenceAlias).ForceRefTo(partner)
	if !(partner as Actor)
		StorageUtil.SetFormValue(q, "ONPCR.LetterHolder", partner)
	endif
	ObjectReference letterRef = PlayerRef.PlaceAtMe(letter, 1, false, true)
	(q.GetAlias(2) as ReferenceAlias).ForceRefTo(letterRef)
	courier.addItemToContainer(letterRef)
EndFunction

; 0.25.0 (baby stages part 2, idea 4): letters about YOUR child while it lives away, queued by ONPCR_BeeingFemaleBridge.ChildLetters.
; The letter books (Letters esp 000F97-000F99) name the child through <Alias=Partner>; a child who is away has no actor, so a
; hidden placed copy of the letter renamed to the child fills that alias.
Function SendChildLetters()
	int n = StorageUtil.IntListCount(None, "ONPCR.ChildLetterId")
	if n == 0
		return
	endif
	bool letters = ONPCR_Settings.GetBool("LettersEnabled", true) && Game.IsPluginInstalled("OStimNPCsReforged - Letters.esp")
	WICourierScript courier = Game.GetFormFromFile(0x039F82, "Skyrim.esm") as WICourierScript
	; 0.25.1: text replacement reads the BASE name (SKSE: "Text Replacement does not use" SetDisplayName), so the holder is a copy of a
	; spare MiscItem (Letters esp 000F9A) whose base is renamed to the child right before the letter bakes its stored text
	MiscObject token = Game.GetFormFromFile(0x000F9A, "OStimNPCsReforged - Letters.esp") as MiscObject
	int i = 0
	while i < n
		int which = StorageUtil.IntListGet(None, "ONPCR.ChildLetterId", i)
		string childName = StorageUtil.StringListGet(None, "ONPCR.ChildLetterName", i)
		Book letter = Game.GetFormFromFile(0x000F96 + which, "OStimNPCsReforged - Letters.esp") as Book
		if letter && letters && courier && token
			token.SetName(childName)
			ObjectReference holder = PlayerRef.PlaceAtMe(token, 1, false, true)
			SendLetter(courier, letters, 0x000F96 + which, "A letter about " + childName, holder, holder)
		endif
		i += 1
	endwhile
	StorageUtil.IntListClear(None, "ONPCR.ChildLetterId")
	StorageUtil.StringListClear(None, "ONPCR.ChildLetterName")
EndFunction


; ---- sleep-overs (0.9.0, experimental, [Home] SleepOvers): the married partner who lives elsewhere spends the night around their
; spouse at the shared home (package 0x8F4 = Sandbox near the linked ref with keyword 0x8F3, sleeping allowed).
; 0.9.1 - schedules are respected: it starts only when the visitor's OWN routine says bedtime, only while the host is at the
; shared home, never when a quest drives either of them (and a running one ends the moment a quest takes over); it ends when the
; host is up in the morning (05:00+) and at 07:00 at the latest.
Function SleepOvers()
	Package pack = Game.GetFormFromFile(0x8F4, "OStimNPCsReforged - Letters.esp") as Package
	Keyword link = Game.GetFormFromFile(0x8F3, "OStimNPCsReforged - Letters.esp") as Keyword
	if !pack || !link
		return
	endif
	if !ONPCR_Settings.GetBool("SleepOvers", false)
		EndSleepOvers(pack, link)
		return
	endif
	float hour = (Utility.GetCurrentGameTime() - Math.Floor(Utility.GetCurrentGameTime())) * 24.0
	; 0.14.2 performance: ending a stay only walks the guests staying over right now (a short list) ...
	int n = StorageUtil.FormListCount(None, "ONPCR_SleepOver")
	while n > 0
		n -= 1
		Actor v = StorageUtil.FormListGet(None, "ONPCR_SleepOver", n) as Actor
		if !v
			StorageUtil.FormListRemoveAt(None, "ONPCR_SleepOver", n)
		else
			Actor hst = v.GetLinkedRef(link) as Actor
			string why = ""
			if !hst
				why = "no host"
			elseif ONPCR_Ladder.IsQuestDriven(v) || ONPCR_Ladder.IsQuestDriven(hst)
				why = "a quest needs them"
			elseif hour >= 7.0 && hour < 19.0
				why = "morning"
			elseif hour >= 5.0 && hour < 19.0 && hst.GetSleepState() == 0
				why = "the host is up"
			elseif v.IsDead() || hst.IsDead() || v.IsInCombat() || v.IsPlayerTeammate()
				why = "no longer possible"
			endif
			if why != ""
				EndSleepOver(v, pack, link, why)
			endif
		endif
	endwhile
	; ... and new stays are only looked for in the evening and at night, among couples who live apart (the DLL picks them)
	if hour >= 7.0 && hour < 19.0
		return
	endif
	int[] h
	if ONPCR_Settings.GetVersion() >= 12
		h = ONPCR_Ladder.GetSleepOverPairs()
	else
		h = new int[1]
	endif
	int i = 0
	while i + 2 < h.Length
		Actor visitor = Game.GetFormEx(h[i]) as Actor
		Actor host = Game.GetFormEx(h[i + 1]) as Actor
		Location home = Game.GetFormEx(h[i + 2]) as Location
		if visitor && host && home && StorageUtil.FormListFind(None, "ONPCR_Household", visitor) < 0	; 0.22.0: a household replaces the sleep-over
			if StorageUtil.FormListFind(None, "ONPCR_SleepOver", visitor) < 0
				if !visitor.IsDead() && !host.IsDead() && !visitor.IsPlayerTeammate() && !visitor.IsGuard() && !visitor.IsInCombat() && ONPCR_Ladder.IsOwnBedtime(visitor) && host.IsInLocation(home) && !ONPCR_Ladder.IsQuestDriven(visitor) && !ONPCR_Ladder.IsQuestDriven(host)
					PO3_SKSEFunctions.SetLinkedRef(visitor, host, link)
					ActorUtil.AddPackageOverride(visitor, pack, 90, 1)
					visitor.EvaluatePackage()
					StorageUtil.FormListAdd(None, "ONPCR_SleepOver", visitor, false)
					Debug.Trace("ONPCR: sleep-over " + visitor.GetDisplayName() + " -> " + host.GetDisplayName() + " (" + home.GetName() + ")")
					if ONPCR_Settings.GetBool("DebugNotifications", false)
						Debug.Notification("[Reforged] " + visitor.GetDisplayName() + " spends the night with " + host.GetDisplayName())
					endif
				endif
			endif
		endif
		i += 3
	endwhile
EndFunction

Function EndSleepOver(Actor v, Package pack, Keyword link, string why)
	ActorUtil.RemovePackageOverride(v, pack)
	PO3_SKSEFunctions.SetLinkedRef(v, None, link)
	v.EvaluatePackage()
	StorageUtil.FormListRemove(None, "ONPCR_SleepOver", v, true)
	Debug.Trace("ONPCR: sleep-over of " + v.GetDisplayName() + " ended - " + why)
EndFunction

; setting switched off: everybody goes back to their own routine
Function EndSleepOvers(Package pack, Keyword link)
	int n = StorageUtil.FormListCount(None, "ONPCR_SleepOver")
	while n > 0
		n -= 1
		Actor v = StorageUtil.FormListGet(None, "ONPCR_SleepOver", n) as Actor
		if v
			EndSleepOver(v, pack, link, "setting off")
		else
			StorageUtil.FormListRemoveAt(None, "ONPCR_SleepOver", n)
		endif
	endwhile
EndFunction

; ---- households (0.22.0, handoff 17, [Home] Households): married couples who live apart in the same town move in together ----
; The mover (the partner whose editor location is not the shared home) gets ONPCR_HouseholdPackage (000E11: the sleep-over sandbox,
; radius 800, linked by ONPCR_HouseholdLink 000E0E to the host) from 19:00 to 07:00 and keeps their own day routine. Once loaded
; inside the home they join the house's owning faction ("<X> House Faction" - its beds become theirs, no trespassing); a home
; that is not faction-owned gives them a free bed of their own instead. The widow keeps the house (linked to the home's marker
; once the host is dead). Nights out ([Home] NightsOut): some evenings (DateNightChance) both go to the town inn, 19:00-22:00
; (the household package 000E11 at priority 88, its link pointed at the inn). Beeing Female children
; ([Home] HouseholdChildren): at night near their parent (the host of a household, else the mother, else the father), by day out
; in their town (the sleep-over package 0x8F4, link 0x8F3, around the town centre). A 4th PACK in the Letters esp hangs the
; game at load (play-test 23 Oct), so no new packages. Weddings (priority 90) and quests
; always win; followers and guards never move in. Log lines start with "Household:".
int HouseholdStamp = 0
float LastHouseholdKidsHour = -1.0

Function Households()
	Package pack = Game.GetFormFromFile(0xE11, "OStimNPCsReforged - Letters.esp") as Package
	Keyword link = Game.GetFormFromFile(0xE0E, "OStimNPCsReforged - Letters.esp") as Keyword
	if !pack || !link
		return
	endif
	if !ONPCR_Settings.GetBool("Households", false) || ONPCR_Settings.GetVersion() < 13
		EndHouseholds("setting off")
		return
	endif
	float hour = CurrentHour()
	bool night = hour >= 19.0 || hour < 7.0
	HouseholdStamp += 1
	int[] h = ONPCR_Ladder.GetHouseholds()
	int i = 0
	while i + 3 < h.Length
		Actor mover = Game.GetFormEx(h[i]) as Actor
		Actor host = Game.GetFormEx(h[i + 1]) as Actor
		Location home = Game.GetFormEx(h[i + 2]) as Location
		if mover && host && home
			if h[i + 3] == 0
				if StorageUtil.GetIntValue(mover, "ONPCR_HH_ApartLogged", 0) == 0
					StorageUtil.SetIntValue(mover, "ONPCR_HH_ApartLogged", 1)
					Debug.Trace("Household: " + mover.GetDisplayName() + " + " + host.GetDisplayName() + " live in different towns - no household")
				endif
			else
				if StorageUtil.FormListFind(None, "ONPCR_Household", mover) < 0 && !mover.IsGuard() && !mover.IsPlayerTeammate()
					StartHousehold(mover, host, home)
				endif
				StorageUtil.SetIntValue(mover, "ONPCR_HH_Stamp", HouseholdStamp)	; a follower for now stays listed, HouseholdTick keeps them out
			endif
		endif
		i += 4
	endwhile
	int n = StorageUtil.FormListCount(None, "ONPCR_Household")
	while n > 0
		n -= 1
		Actor m = StorageUtil.FormListGet(None, "ONPCR_Household", n) as Actor
		if !m
			StorageUtil.FormListRemoveAt(None, "ONPCR_Household", n)
		elseif StorageUtil.GetIntValue(m, "ONPCR_HH_Stamp", 0) != HouseholdStamp
			EndHousehold(m, "no longer married")
		else
			HouseholdTick(m, pack, link, night)
		endif
	endwhile
	NightsOut(hour)
	if ONPCR_Settings.GetBool("HouseholdChildren", true) && Game.IsPluginInstalled("BeeingFemale.esm")
		HouseholdKids(night)
	else
		EndHouseholdKids()
	endif
EndFunction

Function StartHousehold(Actor mover, Actor host, Location home)
	StorageUtil.FormListAdd(None, "ONPCR_Household", mover, false)
	StorageUtil.SetFormValue(mover, "ONPCR_HH_Host", host)
	StorageUtil.SetFormValue(mover, "ONPCR_HH_Home", home)
	Debug.Trace("Household: " + mover.GetDisplayName() + " moves in with " + host.GetDisplayName() + " (" + home.GetName() + ")")
	SettleIn(mover, home)	; 0.22.3: join the house faction at once (the DLL finds it without the cell loaded) - bed rights and locked doors from the first evening; a home without a faction still waits for the loaded bed search
	if StorageUtil.GetIntValue(mover, "ONPCR_HH_Announced", 0) == 0
		StorageUtil.SetIntValue(mover, "ONPCR_HH_Announced", 1)
		HouseholdNews(mover, mover.GetDisplayName() + " has moved in with " + host.GetDisplayName())
	endif
EndFunction

; News page always; an on-screen line only with Notifications on and the mover near the player
Function HouseholdNews(Actor who, string text)
	ONPCR_Ladder.AddNewsLine(text)
	if ONPCR_Settings.GetInt("Notifications", 1) >= 1 && who.Is3DLoaded() && who.GetDistance(PlayerRef) < 4096.0
		Debug.Notification(text)
	endif
EndFunction

; the reference the mover stays near at night: the host, or the home's own marker once the host is dead (the widow keeps the house)
ObjectReference Function HouseholdAnchor(Actor mover)
	Actor host = StorageUtil.GetFormValue(mover, "ONPCR_HH_Host") as Actor
	if host && !host.IsDead()
		return host
	endif
	ObjectReference spot = ONPCR_Ladder.FindTownSpot(StorageUtil.GetFormValue(mover, "ONPCR_HH_Home") as Location, 2, 0)
	if spot
		return spot
	endif
	ObjectReference bed = StorageUtil.GetFormValue(mover, "ONPCR_HH_Bed") as ObjectReference
	if bed
		return bed
	endif
	return host
EndFunction

Function HouseholdTick(Actor m, Package pack, Keyword link, bool night)
	if StorageUtil.FormListFind(None, "ONPCR_NightOut", m) >= 0
		return		; a night out has the package and the link (pointed at the inn) until 22:00
	endif
	bool want = night && !m.IsDead() && !m.IsInCombat() && !m.IsPlayerTeammate() && !ONPCR_Ladder.IsQuestDriven(m) && StorageUtil.FormListFind(None, "ONPCR_Wedding", m) < 0
	bool isOn = StorageUtil.GetIntValue(m, "ONPCR_HH_On", 0) == 1
	if want
		ObjectReference anchor = HouseholdAnchor(m)
		if anchor && (!isOn || m.GetLinkedRef(link) != anchor)
			PO3_SKSEFunctions.SetLinkedRef(m, anchor, link)
			ActorUtil.AddPackageOverride(m, pack, 85, 1)
			m.EvaluatePackage()
			StorageUtil.SetIntValue(m, "ONPCR_HH_On", 1)
			if ONPCR_Settings.GetBool("DebugNotifications", false)
				Debug.Notification("[Reforged] " + m.GetDisplayName() + " goes home for the night")
			endif
		endif
		if m.Is3DLoaded() && StorageUtil.GetIntValue(m, "ONPCR_HH_Settled", 0) == 0
			SettleIn(m, StorageUtil.GetFormValue(m, "ONPCR_HH_Home") as Location)
		endif
	elseif isOn
		ActorUtil.RemovePackageOverride(m, pack)
		PO3_SKSEFunctions.SetLinkedRef(m, None, link)
		m.EvaluatePackage()
		StorageUtil.SetIntValue(m, "ONPCR_HH_On", 0)
	endif
EndFunction

; inside the home for the first time: join the house faction (vanilla homes), else take a free bed; nothing free = try again later
Function SettleIn(Actor who, Location home)
	if !home
		return
	endif
	Faction f = ONPCR_Ladder.GetHomeFaction(who, home)
	if f
		if !who.IsInFaction(f)
			who.AddToFaction(f)
			StorageUtil.SetFormValue(who, "ONPCR_HH_Faction", f)
		endif
		StorageUtil.SetIntValue(who, "ONPCR_HH_Settled", 1)
		Debug.Trace("Household: " + who.GetDisplayName() + " joins the house (" + home.GetName() + ")")
		return
	endif
	if who.IsChild()
		return		; a child bed cannot be told from an adult one by its record - children rely on the house faction
	endif
	ObjectReference bed = ONPCR_Ladder.FindFreeBed(who, home)
	if bed
		bed.SetActorOwner(who.GetActorBase())
		StorageUtil.SetFormValue(who, "ONPCR_HH_Bed", bed)
		StorageUtil.SetIntValue(who, "ONPCR_HH_Settled", 1)
		Debug.Trace("Household: bed for " + who.GetDisplayName() + " -> " + bed)
	endif
EndFunction

; leave the house: package, link, faction / bed, keys
Function Unsettle(Actor who)
	Faction f = StorageUtil.GetFormValue(who, "ONPCR_HH_Faction") as Faction
	if f
		who.RemoveFromFaction(f)
	endif
	ObjectReference bed = StorageUtil.GetFormValue(who, "ONPCR_HH_Bed") as ObjectReference
	if bed && bed.GetActorOwner() == who.GetActorBase()
		bed.SetActorOwner(None)
	endif
	StorageUtil.UnsetFormValue(who, "ONPCR_HH_Faction")
	StorageUtil.UnsetFormValue(who, "ONPCR_HH_Bed")
	StorageUtil.UnsetIntValue(who, "ONPCR_HH_Settled")
EndFunction

Function EndHousehold(Actor m, string why)
	Package pack = Game.GetFormFromFile(0xE11, "OStimNPCsReforged - Letters.esp") as Package
	Keyword link = Game.GetFormFromFile(0xE0E, "OStimNPCsReforged - Letters.esp") as Keyword
	ActorUtil.RemovePackageOverride(m, pack)
	PO3_SKSEFunctions.SetLinkedRef(m, None, link)
	EndNightOut(m, "the household ended")
	m.EvaluatePackage()
	Unsettle(m)
	Actor host = StorageUtil.GetFormValue(m, "ONPCR_HH_Host") as Actor
	StorageUtil.FormListRemove(None, "ONPCR_Household", m, true)
	StorageUtil.UnsetFormValue(m, "ONPCR_HH_Host")
	StorageUtil.UnsetFormValue(m, "ONPCR_HH_Home")
	StorageUtil.UnsetIntValue(m, "ONPCR_HH_On")
	StorageUtil.UnsetIntValue(m, "ONPCR_HH_Stamp")
	Debug.Trace("Household: " + m.GetDisplayName() + " ended - " + why)
	if why == "no longer married" && !m.IsDead()
		StorageUtil.UnsetIntValue(m, "ONPCR_HH_Announced")
		HouseholdNews(m, m.GetDisplayName() + " has moved out")
	endif
EndFunction

; setting switched off: everybody back to their own routine (children too)
Function EndHouseholds(string why)
	int n = StorageUtil.FormListCount(None, "ONPCR_Household")
	while n > 0
		n -= 1
		Actor m = StorageUtil.FormListGet(None, "ONPCR_Household", n) as Actor
		if m
			EndHousehold(m, why)
		else
			StorageUtil.FormListRemoveAt(None, "ONPCR_Household", n)
		endif
	endwhile
	EndNightsOut()
	EndHouseholdKids()
EndFunction

; ---- nights out: once per evening (19:00-20:00) each household rolls DateNightChance; both go to the town inn until 22:00 ----
Function NightsOut(float hour)
	int n = StorageUtil.FormListCount(None, "ONPCR_NightOut")
	while n > 0
		n -= 1
		Actor v = StorageUtil.FormListGet(None, "ONPCR_NightOut", n) as Actor
		if !v
			StorageUtil.FormListRemoveAt(None, "ONPCR_NightOut", n)
		else
			string why = ""
			if hour >= 22.0 || hour < 19.0
				why = "the evening is over"
			elseif ONPCR_Ladder.IsQuestDriven(v) || StorageUtil.FormListFind(None, "ONPCR_Wedding", v) >= 0
				why = "a quest needs them"
			elseif v.IsDead() || v.IsInCombat() || v.IsPlayerTeammate()
				why = "no longer possible"
			endif
			if why != ""
				EndNightOut(v, why)
			endif
		endif
	endwhile
	if !ONPCR_Settings.GetBool("NightsOut", true) || hour < 19.0 || hour >= 20.0
		return
	endif
	int day = Math.Floor(Utility.GetCurrentGameTime())
	int chance = ONPCR_Settings.GetInt("DateNightChance", 30)
	Package outPack = Game.GetFormFromFile(0xE11, "OStimNPCsReforged - Letters.esp") as Package
	Keyword outLink = Game.GetFormFromFile(0xE0E, "OStimNPCsReforged - Letters.esp") as Keyword
	if !outPack || !outLink
		return
	endif
	int i = StorageUtil.FormListCount(None, "ONPCR_Household")
	while i > 0
		i -= 1
		Actor m = StorageUtil.FormListGet(None, "ONPCR_Household", i) as Actor
		if m && StorageUtil.GetIntValue(m, "ONPCR_HH_OutDay", -1) != day
			StorageUtil.SetIntValue(m, "ONPCR_HH_OutDay", day)
			Actor host = StorageUtil.GetFormValue(m, "ONPCR_HH_Host") as Actor
			if host && Utility.RandomInt(1, 100) <= chance && NightOutFree(m) && NightOutFree(host) && !host.IsGuard()
				ObjectReference inn = ONPCR_Ladder.FindTownSpot(StorageUtil.GetFormValue(m, "ONPCR_HH_Home") as Location, 1, m.GetFormID())
				if inn
					StartNightOut(m, inn, outPack, outLink)
					StartNightOut(host, inn, outPack, outLink)
					Debug.Trace("Household: night out - " + m.GetDisplayName() + " + " + host.GetDisplayName() + " at the inn (" + inn + ")")
					if ONPCR_Settings.GetBool("DebugNotifications", false)
						Debug.Notification("[Reforged] " + m.GetDisplayName() + " and " + host.GetDisplayName() + " go to the inn tonight")
					endif
				elseif StorageUtil.GetIntValue(m, "ONPCR_HH_NoInnLogged", 0) == 0
					StorageUtil.SetIntValue(m, "ONPCR_HH_NoInnLogged", 1)
					Debug.Trace("Household: no inn found for " + m.GetDisplayName() + "'s town - no nights out")
				endif
			endif
		endif
	endwhile
EndFunction

bool Function NightOutFree(Actor x)
	return x && !x.IsDead() && !x.IsInCombat() && !x.IsPlayerTeammate() && x.GetSleepState() == 0 && !ONPCR_Ladder.IsQuestDriven(x) && StorageUtil.FormListFind(None, "ONPCR_Wedding", x) < 0 && StorageUtil.FormListFind(None, "ONPCR_NightOut", x) < 0
EndFunction

Function StartNightOut(Actor x, ObjectReference inn, Package outPack, Keyword outLink)
	PO3_SKSEFunctions.SetLinkedRef(x, inn, outLink)
	ActorUtil.AddPackageOverride(x, outPack, 88, 1)
	x.EvaluatePackage()
	StorageUtil.FormListAdd(None, "ONPCR_NightOut", x, false)
EndFunction

Function EndNightOut(Actor x, string why)
	if StorageUtil.FormListFind(None, "ONPCR_NightOut", x) < 0
		return
	endif
	ActorUtil.RemovePackageOverride(x, Game.GetFormFromFile(0xE11, "OStimNPCsReforged - Letters.esp") as Package)
	PO3_SKSEFunctions.SetLinkedRef(x, None, Game.GetFormFromFile(0xE0E, "OStimNPCsReforged - Letters.esp") as Keyword)
	x.EvaluatePackage()
	StorageUtil.FormListRemove(None, "ONPCR_NightOut", x, true)
	Debug.Trace("Household: night out of " + x.GetDisplayName() + " ended - " + why)
EndFunction

Function EndNightsOut()
	int n = StorageUtil.FormListCount(None, "ONPCR_NightOut")
	while n > 0
		n -= 1
		Actor v = StorageUtil.FormListGet(None, "ONPCR_NightOut", n) as Actor
		if v
			EndNightOut(v, "setting off")
		else
			StorageUtil.FormListRemoveAt(None, "ONPCR_NightOut", n)
		endif
	endwhile
EndFunction

; ---- Beeing Female children of NPC couples: home at night near their parent, out in their town by day ----
; The parent: the host of the parents' household (the mover's home), else the mother, else the father. Divorce: the household
; ends, so they follow their mother; one parent dies: the survivor; both dead: they keep the place they had. Re-read once a game
; hour from Beeing Female's FW.Babys list; packages checked every pass (a short list).
Function HouseholdKids(bool night)
	float hr = Math.Floor(Utility.GetCurrentGameTime() * 24.0)
	if hr != LastHouseholdKidsHour
		LastHouseholdKidsHour = hr
		int c = StorageUtil.FormListCount(None, "FW.Babys")
		while c > 0
			c -= 1
			Actor kid = StorageUtil.FormListGet(None, "FW.Babys", c) as Actor
			if kid && !kid.IsDead()
				Actor kidParent = KidParent(kid)
				if kidParent
					StorageUtil.FormListAdd(None, "ONPCR_HH_Kids", kid, false)
					StorageUtil.SetFormValue(kid, "ONPCR_HH_Parent", kidParent)
				endif
			endif
		endwhile
	endif
	Package nightPack = Game.GetFormFromFile(0xE11, "OStimNPCsReforged - Letters.esp") as Package
	Package dayPack = Game.GetFormFromFile(0x8F4, "OStimNPCsReforged - Letters.esp") as Package
	Keyword link = Game.GetFormFromFile(0xE0E, "OStimNPCsReforged - Letters.esp") as Keyword
	Keyword townLink = Game.GetFormFromFile(0x8F3, "OStimNPCsReforged - Letters.esp") as Keyword
	if !nightPack || !dayPack || !link || !townLink
		return
	endif
	int n = StorageUtil.FormListCount(None, "ONPCR_HH_Kids")
	while n > 0
		n -= 1
		Actor k = StorageUtil.FormListGet(None, "ONPCR_HH_Kids", n) as Actor
		if !k || k.IsDead() || k.IsPlayerTeammate() || StorageUtil.FormListFind(None, "FW.Babys", k) < 0
			if k
				EndKid(k)
			else
				StorageUtil.FormListRemoveAt(None, "ONPCR_HH_Kids", n)
			endif
		elseif !ONPCR_Ladder.IsQuestDriven(k)
			KidTick(k, night, nightPack, dayPack, link, townLink)
		endif
	endwhile
EndFunction

; None = not an NPC couple's child (the player's, or no kidParent known and alive)
Actor Function KidParent(Actor kid)
	Actor mother = StorageUtil.GetFormValue(kid, "FW.Child.Mother") as Actor
	Actor father = StorageUtil.GetFormValue(kid, "FW.Child.Father") as Actor
	if mother == PlayerRef || father == PlayerRef || (!mother && !father)
		return None
	endif
	; a household of the two parents: the child lives with the host
	Actor mover = None
	if mother && StorageUtil.FormListFind(None, "ONPCR_Household", mother) >= 0 && StorageUtil.GetFormValue(mother, "ONPCR_HH_Host") == father
		mover = mother
	elseif father && StorageUtil.FormListFind(None, "ONPCR_Household", father) >= 0 && StorageUtil.GetFormValue(father, "ONPCR_HH_Host") == mother
		mover = father
	endif
	if mover
		Actor host = StorageUtil.GetFormValue(mover, "ONPCR_HH_Host") as Actor
		if host && !host.IsDead()
			return host
		endif
		return mover	; the widow keeps the house - and the children
	endif
	if mother && !mother.IsDead()
		return mother
	elseif father && !father.IsDead()
		return father
	endif
	return StorageUtil.GetFormValue(kid, "ONPCR_HH_Parent") as Actor	; both gone: keep the last one (their house)
EndFunction

Function KidTick(Actor k, bool night, Package nightPack, Package dayPack, Keyword link, Keyword townLink)
	if StorageUtil.FormListFind(None, "ONPCR_Wedding", k) >= 0
		return		; a wedding guest (the wedding uses the same package and link as the day out)
	endif
	Actor kidParent = StorageUtil.GetFormValue(k, "ONPCR_HH_Parent") as Actor
	if !kidParent
		return
	endif
	Location home = kidParent.GetEditorLocation()
	if StorageUtil.FormListFind(None, "ONPCR_Household", kidParent) >= 0
		home = StorageUtil.GetFormValue(kidParent, "ONPCR_HH_Home") as Location
	endif	; a host: the home they host is their own editor location
	ObjectReference town = None
	if !night
		town = ONPCR_Ladder.FindTownSpot(home, 0, 0)
	endif
	int mode = 1	; 1 = near the kidParent, 2 = out in town
	if town
		mode = 2
	endif
	if StorageUtil.GetIntValue(k, "ONPCR_HH_Mode", 0) != mode || (mode == 1 && k.GetLinkedRef(link) != kidParent) || (mode == 2 && k.GetLinkedRef(townLink) != town)
		if mode == 2
			ActorUtil.RemovePackageOverride(k, nightPack)
			PO3_SKSEFunctions.SetLinkedRef(k, town, townLink)
			ActorUtil.AddPackageOverride(k, dayPack, 85, 1)
		else
			ActorUtil.RemovePackageOverride(k, dayPack)
			PO3_SKSEFunctions.SetLinkedRef(k, kidParent, link)
			ActorUtil.AddPackageOverride(k, nightPack, 85, 1)
		endif
		k.EvaluatePackage()
		StorageUtil.SetIntValue(k, "ONPCR_HH_Mode", mode)
		Debug.Trace("Household: child " + k.GetDisplayName() + " -> " + kidParent.GetDisplayName() + " (mode " + mode + ")")
	endif
	if StorageUtil.GetFormValue(k, "ONPCR_HH_Home") != home
		Unsettle(k)		; a new home (divorce, a kidParent died): leave the old house
		StorageUtil.SetFormValue(k, "ONPCR_HH_Home", home)
	endif
	if night && k.Is3DLoaded() && StorageUtil.GetIntValue(k, "ONPCR_HH_Settled", 0) == 0
		SettleIn(k, home)
	endif
EndFunction

Function EndKid(Actor k)
	ActorUtil.RemovePackageOverride(k, Game.GetFormFromFile(0xE11, "OStimNPCsReforged - Letters.esp") as Package)
	PO3_SKSEFunctions.SetLinkedRef(k, None, Game.GetFormFromFile(0xE0E, "OStimNPCsReforged - Letters.esp") as Keyword)
	if StorageUtil.FormListFind(None, "ONPCR_Wedding", k) < 0	; never undo a wedding's package
		ActorUtil.RemovePackageOverride(k, Game.GetFormFromFile(0x8F4, "OStimNPCsReforged - Letters.esp") as Package)
		PO3_SKSEFunctions.SetLinkedRef(k, None, Game.GetFormFromFile(0x8F3, "OStimNPCsReforged - Letters.esp") as Keyword)
	endif
	k.EvaluatePackage()
	Unsettle(k)
	StorageUtil.FormListRemove(None, "ONPCR_HH_Kids", k, true)
	StorageUtil.UnsetFormValue(k, "ONPCR_HH_Parent")
	StorageUtil.UnsetFormValue(k, "ONPCR_HH_Home")
	StorageUtil.UnsetIntValue(k, "ONPCR_HH_Mode")
EndFunction

Function EndHouseholdKids()
	int n = StorageUtil.FormListCount(None, "ONPCR_HH_Kids")
	while n > 0
		n -= 1
		Actor k = StorageUtil.FormListGet(None, "ONPCR_HH_Kids", n) as Actor
		if k
			EndKid(k)
		else
			StorageUtil.FormListRemoveAt(None, "ONPCR_HH_Kids", n)
		endif
	endwhile
	LastHouseholdKidsHour = -1.0
EndFunction

; ---- date nights: dating and engaged couples from the same town meet in the evening (18:00-22:00) ----
; Same package and link keyword as sleep-overs (married couples never date, so the two lists never share a visitor).
; Planned once per evening (18:00-20:00), DateNightChance % per couple; ends at 22:00, when a quest needs either of them,
; when they are no longer a couple, or when the setting goes off.
Function DateNights()
	Package pack = Game.GetFormFromFile(0x8F4, "OStimNPCsReforged - Letters.esp") as Package
	Keyword link = Game.GetFormFromFile(0x8F3, "OStimNPCsReforged - Letters.esp") as Keyword
	if !pack || !link
		return
	endif
	if !ONPCR_Settings.GetBool("DateNights", false)
		EndDateNights(pack, link)
		return
	endif
	float now = Utility.GetCurrentGameTime()
	int day = Math.Floor(now)
	float hour = (now - day) * 24.0

	int n = StorageUtil.FormListCount(None, "ONPCR_DateNight")
	while n > 0
		n -= 1
		Actor v = StorageUtil.FormListGet(None, "ONPCR_DateNight", n) as Actor
		if !v
			StorageUtil.FormListRemoveAt(None, "ONPCR_DateNight", n)
		else
			Actor host = StorageUtil.GetFormValue(v, "ONPCR_DateHost") as Actor
			string why = ""
			if !host
				why = "no host"
			elseif hour >= 22.0 || hour < 18.0
				why = "the evening is over"
			elseif ONPCR_Ladder.IsQuestDriven(v) || ONPCR_Ladder.IsQuestDriven(host)
				why = "a quest needs them"
			elseif v.IsDead() || host.IsDead() || v.IsInCombat() || v.IsPlayerTeammate()
				why = "no longer possible"
			elseif ONPCR_Ladder.GetPartnerRef(v) != host
				why = "no longer a couple"
			endif
			if why != ""
				EndDateNight(v, pack, link, why)
			endif
		endif
	endwhile

	if hour < 18.0 || hour >= 20.0 || StorageUtil.GetIntValue(None, "ONPCR_DatePlanDay", -1) == day
		return
	endif
	StorageUtil.SetIntValue(None, "ONPCR_DatePlanDay", day)
	int chance = ONPCR_Settings.GetInt("DateNightChance", 30)
	int[] p = ONPCR_Ladder.GetDatingPairs()
	int i = 0
	while i + 2 < p.Length
		Actor a = Game.GetFormEx(p[i]) as Actor
		Actor b = Game.GetFormEx(p[i + 1]) as Actor
		if a && b && Utility.RandomInt(1, 100) <= chance && ONPCR_Ladder.SameSettlement(a, b)
			Actor visitor = a
			Actor host = b
			if Utility.RandomInt(0, 1) == 1
				visitor = b
				host = a
			endif
			if !DateHostReady(host) && DateHostReady(visitor)
				Actor t = visitor
				visitor = host
				host = t
			endif
			if DateHostReady(host) && DateVisitorFree(visitor) && !ONPCR_Ladder.IsQuestDriven(host)
				PO3_SKSEFunctions.SetLinkedRef(visitor, host, link)
				ActorUtil.AddPackageOverride(visitor, pack, 90, 1)
				visitor.EvaluatePackage()
				StorageUtil.FormListAdd(None, "ONPCR_DateNight", visitor, false)
				StorageUtil.SetFormValue(visitor, "ONPCR_DateHost", host)
				Debug.Trace("ONPCR: date night " + visitor.GetDisplayName() + " -> " + host.GetDisplayName())
				if ONPCR_Settings.GetBool("DebugNotifications", false)
					Debug.Notification("[Reforged] " + visitor.GetDisplayName() + " goes to see " + host.GetDisplayName() + " this evening")
				endif
			endif
		endif
		i += 3
	endwhile
EndFunction

; the host is alive, at home (in their own editor location) and free
bool Function DateHostReady(Actor h)
	return h && !h.IsDead() && !h.IsInCombat() && !h.IsPlayerTeammate() && h.IsInLocation(h.GetEditorLocation())
EndFunction

; the visitor may leave their routine for the evening
bool Function DateVisitorFree(Actor v)
	if !v || v.IsDead() || v.IsInCombat() || v.IsPlayerTeammate() || v.IsGuard() || v.GetSleepState() != 0
		return false
	endif
	if ONPCR_Ladder.IsQuestDriven(v) || ONPCR_Ladder.IsOwnBedtime(v)
		return false
	endif
	return StorageUtil.FormListFind(None, "ONPCR_SleepOver", v) < 0 && StorageUtil.FormListFind(None, "ONPCR_DateNight", v) < 0 && StorageUtil.FormListFind(None, "ONPCR_Wedding", v) < 0
EndFunction

Function EndDateNight(Actor v, Package pack, Keyword link, string why)
	ActorUtil.RemovePackageOverride(v, pack)
	PO3_SKSEFunctions.SetLinkedRef(v, None, link)
	v.EvaluatePackage()
	StorageUtil.FormListRemove(None, "ONPCR_DateNight", v, true)
	StorageUtil.UnsetFormValue(v, "ONPCR_DateHost")
	Debug.Trace("ONPCR: date night of " + v.GetDisplayName() + " ended - " + why)
EndFunction

; ---- real weddings (0.11.0, [Ladder] RealWeddings): the DLL sets a wedding two days after the engagement time is up ----
; 11:00  the couple dresses for the wedding and goes to the Temple of Mara (moved there if far from you; they wait near the altar)
; 12:00-17:00 whenever you are in the temple, Maramal holds the ceremony with his own lines, then they are married (vows + a guest's
;        congratulations through the usual married news) and you get a thank-you letter if one of them is close to you
; 17:00  nobody held it: they marry quietly, change back and go back to their routine
Function Weddings()
	Package pack = Game.GetFormFromFile(0x8F4, "OStimNPCsReforged - Letters.esp") as Package
	Keyword link = Game.GetFormFromFile(0x8F3, "OStimNPCsReforged - Letters.esp") as Keyword
	ObjectReference altar = Game.GetFormFromFile(0x047A15, "Skyrim.esm") as ObjectReference	; MarriageLoverMarkerREF
	if !pack || !link || !altar
		return
	endif
	if !ONPCR_Settings.GetBool("RealWeddings", false)
		EndWeddingGuests(pack, link, None, None)
		return
	endif
	int hourNow = Math.Floor(Utility.GetCurrentGameTime() * 24.0)
	int[] h = ONPCR_Ladder.GetWeddings()
	int i = 0
	while i + 2 < h.Length
		Actor a = Game.GetFormEx(h[i]) as Actor
		Actor b = Game.GetFormEx(h[i + 1]) as Actor
		int w = h[i + 2]
		Actor runner = None
		if a && b && ONPCR_Settings.GetVersion() >= 11
			runner = ONPCR_Ladder.GetAltarRunner(a, b)
		endif
		if runner
			StoodUp(a, b, runner, w, hourNow, altar, pack, link)	; 0.14.0: one of them will not come
		elseif a && b
			bool late = hourNow >= w + 5
			bool held = false
			if late && hourNow < w + 9 && !a.IsDead() && !b.IsDead() && PlayerRef.GetParentCell() == altar.GetParentCell()
				; 0.12.4: you waited / slept in the temple through the afternoon - the ceremony is held now instead of quietly
				WeddingGather(a, b, altar, pack, link)
				WeddingCeremony(a, b, altar, pack, link)
				held = ONPCR_Ladder.GetStage(a, b) != 3	; married now - guests stay for their congratulations as usual
			endif
			if held
				; the late ceremony took place - nothing else this round (still engaged = it could not: married quietly below)
			elseif hourNow < w + 5 && hourNow >= w - 1 && (ONPCR_Ladder.IsQuestDriven(a) || ONPCR_Ladder.IsQuestDriven(b) || (ONPCR_Settings.GetVersion() >= 10 && ONPCR_Ladder.OtherWeddingBusy()))
				; 0.12.4: one of them is busy with a quest (Keerava's wedding happened without her) - put the wedding off
				; 0.13.3: ... or Love Is In The Air is holding its own wedding at the temple (Maramal can only marry one couple)
				if ONPCR_Ladder.PostponeWedding(a, b)
					Debug.Trace("ONPCR: wedding of " + a.GetDisplayName() + " and " + b.GetDisplayName() + " put off - a quest has one of them, or another wedding is on")
				endif
				EndWeddingGuests(pack, link, a, b)
			elseif late || a.IsDead() || b.IsDead()
				if !a.IsDead() && !b.IsDead() && ONPCR_Ladder.GetStage(a, b) == 3
					ONPCR_Ladder.CompleteWedding(a, b)
					Debug.Trace("ONPCR: wedding of " + a.GetDisplayName() + " and " + b.GetDisplayName() + " held quietly")
				endif
				EndWeddingGuests(pack, link, a, b)
			elseif hourNow >= w - 1
				WeddingGather(a, b, altar, pack, link)
				if StorageUtil.GetIntValue(a, "ONPCR_WedGuestsFor", 0) != w
					StorageUtil.SetIntValue(a, "ONPCR_WedGuestsFor", w)
					InviteWeddingGuests(a, b, altar, pack, link)
				endif
				if hourNow >= w
					WeddingCeremony(a, b, altar, pack, link)
				endif
			endif
		endif
		i += 3
	endwhile
	; anyone still marked for a wedding that is no longer pending (broke up, married through the Couples page...)
	int n = StorageUtil.FormListCount(None, "ONPCR_Wedding")
	while n > 0
		n -= 1
		Actor x = StorageUtil.FormListGet(None, "ONPCR_Wedding", n) as Actor
		if !x
			StorageUtil.FormListRemoveAt(None, "ONPCR_Wedding", n)
		elseif h.Find(x.GetFormID()) < 0 && !GuestOfPending(x, h) && hourNow >= StorageUtil.GetIntValue(WeddingOwner(x), "ONPCR_WedUntil", 0)
			EndWeddingGuest(x, pack, link)
		endif
	endwhile
EndFunction

; 0.14.0 (idea from Love Is In The Air): stood up at the altar. The one who comes waits at the altar with the guests and, once the
; afternoon starts, asks "Where are you?" now and then while you are there; at 16:00 they give up ("Now, let's just pretend this never
; happened." or "It can't be helped.") and go home. The DLL ends the engagement at 17:00 (jilted, breakup news).
Function StoodUp(Actor a, Actor b, Actor runner, int w, int hourNow, ObjectReference altar, Package pack, Keyword link)
	Actor waiting = a
	if runner == a
		waiting = b
	endif
	if hourNow < w - 1 || waiting.IsDead()
		return
	endif
	bool near = CanSpeak(waiting) && waiting.GetParentCell() == PlayerRef.GetParentCell() && waiting.GetDistance(PlayerRef) < 2500.0
	if hourNow < w + 4
		WeddingGather(waiting, waiting, altar, pack, link)
		if StorageUtil.GetIntValue(waiting, "ONPCR_WedGuestsFor", 0) != w
			StorageUtil.SetIntValue(waiting, "ONPCR_WedGuestsFor", w)
			InviteWeddingGuests(a, b, altar, pack, link)
		endif
		if hourNow >= w && near
			float now = Utility.GetCurrentGameTime()
			if now - StorageUtil.GetFloatValue(waiting, "ONPCR_WhereAt", -100.0) >= 0.03	; about every 45 game minutes
				StorageUtil.SetFloatValue(waiting, "ONPCR_WhereAt", now)
				SpeakLine(waiting, 0xD27)	; "Where are you?"
			endif
		endif
	elseif StorageUtil.GetIntValue(waiting, "ONPCR_LeftAltarFor", 0) != w
		StorageUtil.SetIntValue(waiting, "ONPCR_LeftAltarFor", w)
		if near
			SpeakLine(waiting, 0xD2C)	; "Now, let's just pretend this never happened." / "It can't be helped."
		endif
		Debug.Trace("ONPCR: " + runner.GetDisplayName() + " never came to the wedding - " + waiting.GetDisplayName() + " leaves the temple")
		EndWeddingGuests(pack, link, a, b)
	endif
EndFunction

; 0.14.0 (idea from Love Is In The Air): attacking the bride, the groom or the priest stops the ceremony - the wedding is off for now
bool Function CeremonyDisrupted(Actor a, Actor b, Actor priest)
	if a.IsDead() || b.IsDead()
		return true
	endif
	if !ONPCR_Settings.GetBool("ObjectionStopsWedding", true)
		return false	; 0.14.1 setting off: the ceremony goes on whatever happens
	endif
	return a.IsInCombat() || b.IsInCombat() || priest.IsInCombat()
EndFunction

Function StopCeremony(Actor a, Actor b, Actor priest, Package pack, Keyword link)
	priest.ClearLookAt()
	a.ClearLookAt()
	b.ClearLookAt()
	Debug.Trace("ONPCR: the wedding of " + a.GetDisplayName() + " and " + b.GetDisplayName() + " was stopped (someone objected)")
	Debug.Notification("The wedding of " + a.GetDisplayName() + " and " + b.GetDisplayName() + " was stopped")
	if ONPCR_Settings.GetVersion() >= 11
		ONPCR_Ladder.CallOffWedding(a, b)
	endif
	EndWeddingGuests(pack, link, a, b)
EndFunction

; both go to the temple and stay near the altar; one far from the player (not loaded) is moved there straight away
Function WeddingGather(Actor a, Actor b, ObjectReference altar, Package pack, Keyword link)
	Actor[] two = new Actor[2]
	two[0] = a
	two[1] = b
	int k = 0
	while k < 2
		Actor x = two[k]
		if StorageUtil.FormListFind(None, "ONPCR_Wedding", x) < 0 && !ONPCR_Ladder.IsQuestDriven(x) && !x.IsPlayerTeammate() && !x.IsInCombat()
			if StorageUtil.FormListFind(None, "ONPCR_DateNight", x) >= 0
				EndDateNight(x, pack, link, "wedding day")
			endif
			if !x.Is3DLoaded() || x.GetParentCell() != PlayerRef.GetParentCell()
				x.MoveTo(altar, 0.0, 0.0, 0.0, false)
			endif
			PO3_SKSEFunctions.SetLinkedRef(x, altar, link)
			ActorUtil.AddPackageOverride(x, pack, 90, 1)
			x.EvaluatePackage()
			StorageUtil.FormListAdd(None, "ONPCR_Wedding", x, false)
			Debug.Trace("ONPCR: " + x.GetDisplayName() + " goes to the Temple of Mara for the wedding")
		endif
		if StorageUtil.FormListFind(None, "ONPCR_Wedding", x) >= 0
			DressForWedding(x)	; every update: an NPC re-dressed by a cell load gets the wedding clothes back on
		endif
		k += 1
	endwhile
EndFunction

; vanilla wedding clothes: the bride's dress, sandals and wreath (shown as your list's Wedding Outfit Commission version),
; Fine Clothes and Fine Boots for a groom - by each partner's own sex, so two brides or two grooms work too
Form[] Function WeddingClothes(Actor x)
	Form[] f = new Form[3]
	if x.GetActorBase().GetSex() == 1
		f[0] = Game.GetFormFromFile(0x088956, "Skyrim.esm")	; Wedding Dress
		f[1] = Game.GetFormFromFile(0x088958, "Skyrim.esm")	; Wedding Sandals
		f[2] = Game.GetFormFromFile(0x08895A, "Skyrim.esm")	; Wedding Wreath
	else
		f[0] = Game.GetFormFromFile(0x086991, "Skyrim.esm")	; Fine Clothes
		f[1] = Game.GetFormFromFile(0x086993, "Skyrim.esm")	; Fine Boots
	endif
	return f
EndFunction

; first time: remember what they wear on body, feet, circlet and head, to put it back afterwards
Function DressForWedding(Actor x)
	if StorageUtil.GetIntValue(x, "ONPCR_WedDressed", 0) == 0
		int[] slots = new int[4]
		slots[0] = 0x4		; body
		slots[1] = 0x80		; feet
		slots[2] = 0x1000	; circlet
		slots[3] = 0x2		; hair / hats
		int s = 0
		while s < 4
			Form w = x.GetWornForm(slots[s])
			if w
				StorageUtil.FormListAdd(x, "ONPCR_WedOwn", w, false)
			endif
			s += 1
		endwhile
		StorageUtil.SetIntValue(x, "ONPCR_WedDressed", 1)
	endif
	Form[] f = WeddingClothes(x)
	int i = 0
	while i < f.Length
		if f[i]
			if x.GetItemCount(f[i]) == 0
				x.AddItem(f[i], 1, true)
				StorageUtil.FormListAdd(x, "ONPCR_WedGiven", f[i], false)
			endif
			if !x.IsEquipped(f[i])
				x.EquipItem(f[i], true, true)	; kept on until the wedding day is over
			endif
		endif
		i += 1
	endwhile
EndFunction

; back into their own clothes; the wedding clothes this mod handed out are taken back
Function UndressAfterWedding(Actor x)
	if StorageUtil.GetIntValue(x, "ONPCR_WedDressed", 0) == 0
		return
	endif
	Form[] f = WeddingClothes(x)
	int i = 0
	while i < f.Length
		if f[i]
			x.UnequipItem(f[i], false, true)
		endif
		i += 1
	endwhile
	int n = StorageUtil.FormListCount(x, "ONPCR_WedGiven")
	while n > 0
		n -= 1
		Form g = StorageUtil.FormListGet(x, "ONPCR_WedGiven", n)
		if g
			x.RemoveItem(g, 1, true)
		endif
	endwhile
	n = StorageUtil.FormListCount(x, "ONPCR_WedOwn")
	while n > 0
		n -= 1
		Form o = StorageUtil.FormListGet(x, "ONPCR_WedOwn", n)
		if o && x.GetItemCount(o) > 0
			x.EquipItem(o, false, true)
		endif
	endwhile
	StorageUtil.FormListClear(x, "ONPCR_WedGiven")
	StorageUtil.FormListClear(x, "ONPCR_WedOwn")
	StorageUtil.UnsetIntValue(x, "ONPCR_WedDressed")
EndFunction

; the player is in the temple, so is the couple, and Maramal is free: the ceremony
Function WeddingCeremony(Actor a, Actor b, ObjectReference altar, Package pack, Keyword link)
	Cell temple = altar.GetParentCell()
	Actor priest = Game.GetFormFromFile(0x019DD5, "Skyrim.esm") as Actor	; MaramalRef
	Quest own = Quest.GetQuest("RelationshipMarriageWedding")
	if PlayerRef.GetParentCell() != temple || a.GetParentCell() != temple || b.GetParentCell() != temple
		return
	endif
	if !priest || !CanSpeak(priest) || priest.GetParentCell() != temple || (own && own.IsRunning()) || ONPCR_Ladder.IsQuestDriven(priest)
		return
	endif
	if ONPCR_Settings.GetVersion() >= 10 && ONPCR_Ladder.OtherWeddingBusy()
		return	; 0.13.3: Maramal is busy with a Love Is In The Air wedding
	endif
	if !CanSpeak(a) || !CanSpeak(b) || Utility.IsInMenuMode()
		return
	endif
	Debug.Trace("ONPCR: wedding ceremony " + a.GetDisplayName() + " + " + b.GetDisplayName())
	ObjectReference spot = Game.GetFormFromFile(0x047A14, "Skyrim.esm") as ObjectReference	; MarriagePriestMarkerREF
	if spot
		priest.PathToReference(spot, 0.5)
	endif
	a.PathToReference(altar, 0.5)
	b.PathToReference(a, 0.5)
	; 0.13.1: a word between the two of them while Maramal takes his place ("It's our wedding day...")
	PlayPlan(a, b, ONPCR_Ladder.PlanPart(a, b, "m5"))
	if CeremonyDisrupted(a, b, priest)
		StopCeremony(a, b, priest, pack, link)
		return
	endif
	a.SetLookAt(priest)
	b.SetLookAt(priest)
	priest.SetLookAt(a)
	if Utility.RandomInt(1, 3) == 1
		SpeakLine(priest, 0xD79)	; 0.15.0 (spliced, reviewed): just "Let's begin the ceremony." - so he does not always say the same
	elseif a.GetActorBase().GetSex() == 1 || b.GetActorBase().GetSex() == 1
		SpeakLine(priest, 0x962)	; "Ah, here's the blushing bride now..."
	else
		SpeakLine(priest, 0xD1A)	; 0.13.2: "Ah, here's the proud groom now. Let's begin the ceremony."
	endif
	; Maramal's speech; 0.14.0: attacking the bride, groom or priest between his lines stops the wedding
	int[] speech = new int[5]
	speech[0] = 0x965	; "We gather here today, under Mara's loving gaze..."
	speech[1] = 0x968
	speech[2] = 0x96B
	speech[3] = 0x96E
	speech[4] = 0x971	; "Do you agree to be bound together, in love, now and forever?"
	int s = 0
	while s < 5
		if CeremonyDisrupted(a, b, priest)
			StopCeremony(a, b, priest, pack, link)
			return
		endif
		SpeakLine(priest, speech[s])
		s += 1
	endwhile
	; 0.13.2: the vows answer the question right away ("I do. Now and forever."), then Maramal declares them wed
	if CeremonyDisrupted(a, b, priest)
		StopCeremony(a, b, priest, pack, link)
		return
	endif
	Converse(a, b, 0x93D, 0x93D)
	if CeremonyDisrupted(a, b, priest)
		StopCeremony(a, b, priest, pack, link)
		return
	endif
	a.SetLookAt(priest)
	b.SetLookAt(priest)
	SpeakLine(priest, 0xD1C)	; "Under the authority of Mara, the Divine of Love, I declare this couple to be wed."
	priest.ClearLookAt()
	a.ClearLookAt()
	b.ClearLookAt()
	; the married news that follows only has a guest congratulate them (vows were said above); the letter becomes a thank-you
	StorageUtil.SetIntValue(a, "ONPCR_VowsSaid", 1)
	StorageUtil.SetIntValue(a, "ONPCR_WedAttended", 1)
	StorageUtil.SetIntValue(b, "ONPCR_WedAttended", 1)
	ONPCR_Ladder.CompleteWedding(a, b)
	; 0.13.1: married - "We face the world together now. Where should we live?" and an answer
	PlayPlan(a, b, ONPCR_Ladder.PlanPart(a, b, "m1"))
	; they stay dressed at the temple for another hour (vows and congratulations come with the married news), then go home
	int until = Math.Floor(Utility.GetCurrentGameTime() * 24.0) + 1
	StorageUtil.SetIntValue(a, "ONPCR_WedUntil", until)
	StorageUtil.SetIntValue(b, "ONPCR_WedUntil", until)
	WeddingGuestsCongratulate(a)
EndFunction

; ---- wedding guests (0.12.1): children of either partner always, then close family, then friends (WeddingGuests, default 6) ----
Function InviteWeddingGuests(Actor a, Actor b, ObjectReference altar, Package pack, Keyword link)
	int[] g = ONPCR_Ladder.GetWeddingGuests(a, b, ONPCR_Settings.GetInt("WeddingGuests", 6))
	; 0.14.0: guests take a seat on the temple benches (sandbox near the altar, sitting only) instead of milling around
	Package guestPack = None
	if ONPCR_Settings.GetBool("SeatedGuests", true)
		guestPack = Game.GetFormFromFile(0xD31, "OStimNPCsReforged - Letters.esp") as Package
	endif
	if !guestPack
		guestPack = pack
	endif
	int i = 0
	int came = 0
	while i < g.Length
		Actor x = Game.GetFormEx(g[i]) as Actor
		if x && !x.IsDead() && !x.IsInCombat() && !ONPCR_Ladder.IsQuestDriven(x) && StorageUtil.FormListFind(None, "ONPCR_Wedding", x) < 0
			if StorageUtil.FormListFind(None, "ONPCR_DateNight", x) >= 0
				EndDateNight(x, pack, link, "a wedding to go to")
			endif
			if !x.Is3DLoaded() || x.GetParentCell() != PlayerRef.GetParentCell()
				x.MoveTo(altar, Utility.RandomFloat(-250.0, 250.0), Utility.RandomFloat(-250.0, 250.0), 0.0, false)
			endif
			PO3_SKSEFunctions.SetLinkedRef(x, altar, link)
			ActorUtil.AddPackageOverride(x, guestPack, 90, 1)
			x.EvaluatePackage()
			StorageUtil.FormListAdd(None, "ONPCR_Wedding", x, false)
			StorageUtil.SetFormValue(x, "ONPCR_WedGuestOf", a)
			came += 1
		endif
		i += 1
	endwhile
	Debug.Trace("ONPCR: " + came + " guests come to the wedding of " + a.GetDisplayName() + " and " + b.GetDisplayName())
EndFunction

; whose wedding a guest came for (the couple themselves: their own ref)
Actor Function WeddingOwner(Actor x)
	Actor o = StorageUtil.GetFormValue(x, "ONPCR_WedGuestOf") as Actor
	if o
		return o
	endif
	return x
EndFunction

; a guest stays while the wedding they came for is still ahead
bool Function GuestOfPending(Actor x, int[] h)
	Actor o = StorageUtil.GetFormValue(x, "ONPCR_WedGuestOf") as Actor
	return o && h.Find(o.GetFormID()) >= 0
EndFunction

; after the ceremony: two of the guests (children too) wish the couple well
Function WeddingGuestsCongratulate(Actor owner)
	int said = 0
	int n = StorageUtil.FormListCount(None, "ONPCR_Wedding")
	int i = 0
	while i < n && said < 2
		Actor x = StorageUtil.FormListGet(None, "ONPCR_Wedding", i) as Actor
		if x && WeddingOwner(x) == owner && x != owner && CanSpeak(x) && x.GetParentCell() == owner.GetParentCell()
			x.SetLookAt(owner)
			SpeakLine(x, 0x942)
			x.ClearLookAt()
			said += 1
		endif
		i += 1
	endwhile
EndFunction

; a = None: everybody (setting off)
Function EndWeddingGuests(Package pack, Keyword link, Actor a, Actor b)
	int n = StorageUtil.FormListCount(None, "ONPCR_Wedding")
	while n > 0
		n -= 1
		Actor x = StorageUtil.FormListGet(None, "ONPCR_Wedding", n) as Actor
		if !x
			StorageUtil.FormListRemoveAt(None, "ONPCR_Wedding", n)
		elseif !a || x == a || x == b || WeddingOwner(x) == a || WeddingOwner(x) == b
			EndWeddingGuest(x, pack, link)
		endif
	endwhile
EndFunction

Function EndWeddingGuest(Actor x, Package pack, Keyword link)
	ActorUtil.RemovePackageOverride(x, pack)
	Package guestPack = Game.GetFormFromFile(0xD31, "OStimNPCsReforged - Letters.esp") as Package
	if guestPack
		ActorUtil.RemovePackageOverride(x, guestPack)	; 0.14.0 seated guests
	endif
	PO3_SKSEFunctions.SetLinkedRef(x, None, link)
	x.EvaluatePackage()
	StorageUtil.FormListRemove(None, "ONPCR_Wedding", x, true)
	StorageUtil.UnsetIntValue(x, "ONPCR_WedUntil")
	StorageUtil.UnsetFormValue(x, "ONPCR_WedGuestOf")
	UndressAfterWedding(x)
	Debug.Trace("ONPCR: " + x.GetDisplayName() + " leaves the wedding")
EndFunction

Function EndDateNights(Package pack, Keyword link)
	int n = StorageUtil.FormListCount(None, "ONPCR_DateNight")
	while n > 0
		n -= 1
		Actor v = StorageUtil.FormListGet(None, "ONPCR_DateNight", n) as Actor
		if v
			EndDateNight(v, pack, link, "setting off")
		else
			StorageUtil.FormListRemoveAt(None, "ONPCR_DateNight", n)
		endif
	endwhile
EndFunction
; ---- Voice test (Couples page button): the two nearest NPCs become a married couple and say every voice moment in turn ----
string[] Function VoiceTestNames()
	string[] n = new string[21]
	n[0] = "dating (courting)"
	n[1] = "greeting"
	n[2] = "engaged (it's settled)"
	n[3] = "engaged (wedding soon)"
	n[4] = "married greeting"
	n[5] = "married goodbye"
	n[6] = "goodbye"
	n[7] = "gift"
	n[8] = "gift thanks"
	n[9] = "wedding vow"
	n[10] = "wedding congratulations"
	n[11] = "jealousy (caught)"
	n[12] = "stray excuse"
	n[13] = "not buying it"
	n[14] = "forgiving"
	n[15] = "breakup"
	n[16] = "meeting the ex"
	n[17] = "heartbroken"
	n[18] = "widowed (husband)"
	n[19] = "widowed (wife)"
	n[20] = "lost boyfriend"
	return n
EndFunction

; Couples page "Import from other mods" (0.17.0): Lover's Ledger NPC pairs with at least numArg scenes together become dating couples.
; The ladder refuses anyone already committed, children, creatures and the player. Relations Finder reads the same game records the
; DLL imports itself, M.A.R.A.S holds the player's partners - nothing to import from those two.
Event OnReforgedImportOthers(string eventName, string strArg, float numArg, Form sender)
	if !LadderAvailable
		return
	endif
	int needed = numArg as int
	if needed < 1
		needed = 1
	endif
	int added = 0
	Form[] everyone = TTLL_Store.GetAllNPCs()
	if everyone
		Form playerBase = PlayerRef.GetActorBase()
		int i = 0
		while i < everyone.Length && i < 3000
			ActorBase nb = everyone[i] as ActorBase
			Actor na = None
			if nb && nb != playerBase
				na = ONPCR_Ladder.GetActorForBase(nb)
			endif
			if na
				Form[] lovers = TTLL_Store.GetAllLovers(na, -1)
				int j = 0
				while lovers && j < lovers.Length
					ActorBase lb = lovers[j] as ActorBase
					if lb && lb != playerBase && lb.GetFormID() > nb.GetFormID()
						Actor la = ONPCR_Ladder.GetActorForBase(lb)
						if la && TTLL_Store.GetLoverInt(na, la, "exclusivesex") >= needed
							if ONPCR_Ladder.ImportPair(nb, lb)
								added += 1
							endif
						endif
					endif
					j += 1
				endwhile
			endif
			i += 1
		endwhile
	endif
	ONPCR_Ladder.FinishOtherImport(added, "Lover's Ledger")
EndEvent

Event OnReforgedVoiceTest(string eventName, string strArg, float numArg, Form sender)
	int w = 0
	while Utility.IsInMenuMode() && w < 600
		Utility.WaitMenuMode(0.5)
		w += 1
	endwhile
	Utility.Wait(1.0)
	Actor[] found = MiscUtil.ScanCellNPCs(PlayerRef, 1500.0, None, true)
	Actor a = None
	Actor b = None
	float da = 999999.0
	float db = 999999.0
	int i = 0
	while i < found.Length
		Actor x = found[i]
		if x && x != PlayerRef && !x.IsChild() && x.Is3DLoaded() && x.HasKeyword(Game.GetFormFromFile(0x013794, "Skyrim.esm") as Keyword) && !x.HasKeyword(Game.GetFormFromFile(0x013795, "Skyrim.esm") as Keyword) && !x.HasKeyword(Game.GetFormFromFile(0x013798, "Skyrim.esm") as Keyword)
			float d = x.GetDistance(PlayerRef)
			if d < da
				b = a
				db = da
				a = x
				da = d
			elseif d < db
				b = x
				db = d
			endif
		endif
		i += 1
	endwhile
	if !a || !b
		Debug.Notification("[Reforged] voice test: need two people near you")
		return
	endif
	Debug.Notification("[Reforged] voice test: " + a.GetDisplayName() + " + " + b.GetDisplayName())
	ONPCR_Ladder.SetStage(a, b, 4)
	StorageUtil.SetFloatValue(a, "ONPCR_VoiceAt", Utility.GetCurrentGameTime() + 1.0)	; keep the normal chat loop away during the test
	StorageUtil.SetFloatValue(b, "ONPCR_VoiceAt", Utility.GetCurrentGameTime() + 1.0)
	int[] ids = new int[21]
	ids[0] = 0x807
	ids[1] = 0x90B
	ids[2] = 0x80F
	ids[3] = 0x82E
	ids[4] = 0x817
	ids[5] = 0x824
	ids[6] = 0x913
	ids[7] = 0x946
	ids[8] = 0x94A
	ids[9] = 0x93D
	ids[10] = 0x942
	ids[11] = 0x847
	ids[12] = 0x91B
	ids[13] = 0x920
	ids[14] = 0x926
	ids[15] = 0x837
	ids[16] = 0x83F
	ids[17] = 0x88F
	ids[18] = 0x92D
	ids[19] = 0x931
	ids[20] = 0x935
	string[] names = VoiceTestNames()
	Actor walker = WalkOver(a, b)
	if !walker
		Debug.Notification("[Reforged] voice test: could not bring them together - talking where they stand")
	endif
	a.SetLookAt(b)
	b.SetLookAt(a)
	int m = 0
	while m < ids.Length
		Topic t = VoiceTopic(ids[m])
		int s = 0
		while s < 2
			Actor speaker = a
			if s == 1
				speaker = b
			endif
			WaitUntilQuiet(a)
			WaitUntilQuiet(b)
			Utility.Wait(0.6)
			Debug.Notification("[Reforged] test: " + speaker.GetDisplayName() + " - " + names[m])
			Debug.Trace("ONPCR: voice test " + speaker.GetDisplayName() + " (" + speaker.GetVoiceType() + ") - " + names[m])
			speaker.Say(t)
			Utility.Wait(0.8)
			if !ONPCR_Ladder.IsSpeaking(speaker)
				Debug.Notification("[Reforged] test: (no line for this voice)")
			endif
			WaitUntilQuiet(speaker)
			s += 1
		endwhile
		m += 1
	endwhile
	a.ClearLookAt()
	b.ClearLookAt()
	ReleaseTalk()
	Debug.Notification("[Reforged] voice test finished")
EndEvent
; 0.12.4: the Followers page asks what the pregnancy mod says about each follower. Beeing Female keeps its "no pregnancy"
; switches in StorageUtil (per person, per race, global - FWAddOnManager.ActorCanBecomePregnant reads the same keys), so they are
; read directly, no Beeing Female scripts needed. Without Beeing Female nothing is sent (the column stays "Automatic (yes)").
Event OnReforgedFertilityCheck(string eventName, string strArg, float numArg, Form sender)
	if Game.GetModByName("BeeingFemale.esm") == 255
		return
	endif
	bool allOff = StorageUtil.GetIntValue(None, "FW.AddOn.Global_DisablePregnancy", 0) != 0
	Actor[] people = ONPCR_Ladder.GetFertilityCheckRefs()
	int i = 0
	while i < people.Length
		Actor x = people[i]
		if x
			int verdict = 1
			if allOff
				verdict = 0
			elseif x.GetActorBase().GetSex() == 1
				if StorageUtil.GetIntValue(x, "FW.AddOn.DisablePregnancy", 0) != 0 || StorageUtil.GetIntValue(x.GetRace(), "FW.AddOn.DisablePregnancy", 0) != 0
					verdict = 0
				endif
			endif
			ONPCR_Ladder.SetEngineChildren(x, verdict)
		endif
		i += 1
	endwhile
EndEvent

; ---- Reforged couple loop (called every 10 s by ONpcPlayerAliasScript, day and night) ----
bool CoupleUpdateBusy = false
Function CoupleUpdate()
	if !LadderAvailable || CoupleUpdateBusy
		return
	endif
	CoupleUpdateBusy = true
	if HeldA && Utility.GetCurrentRealTime() > HeldUntil
		ReleaseTalk()		; a chat that never finished (interrupted, menu, combat) - let them go
	endif
	ONPCR_Ladder.Tick()
	FlushLadderLedger()
	if MarasInstalled
		ONPCR_MarasBridge.FillArchetypes(40)
	endif
	ProcessLadderNews()
	PollPregnancy()
	PregnancyRules()	; 0.21.3
	HatchNpcBabies()	; 0.21.4
	ONPCR_Ladder.SyncStageFaction(4096.0)
	ONPCR_Ladder.NoteMetNearby(1500.0)
	VoiceCouples()
	FriendChats()	; 0.16.0
	FamilyChats()	; 0.23.0
	Mourners()
	SleepOvers()
	Households()	; 0.22.0
	DateNights()
	Weddings()
	CoupleUpdateBusy = false
EndFunction

; ---- slice 5: voice lines (own topics in the Letters plugin; each line borrows a vanilla line's audio) ----
; with Debug notifications on, say on screen who was asked to speak (to tell a silent voice from a line that never fired)
Function VoiceDebug(Actor speaker, int topicId)
	Debug.Trace("ONPCR: voice topic " + topicId + " -> " + speaker.GetDisplayName() + " (" + speaker.GetVoiceType() + ")")
	if ONPCR_Settings.GetBool("DebugNotifications", false)
		Debug.Notification("[Reforged] " + speaker.GetDisplayName() + " speaks (topic " + topicId + ")")
	endif
EndFunction

; a short conversation: they look at each other and take turns (A, B, A, B ...). Each speaker waits until the other has
; actually finished (engine speaking flag via ONPCR_Ladder.IsSpeaking), then a short pause. A line the speaker has no audio for
; is skipped. topics: up to 4 topic ids, 0 = stop.
Function Converse(Actor a, Actor b, int t1, int t2, int t3 = 0, int t4 = 0)
	a.SetLookAt(b)
	b.SetLookAt(a)
	int[] ids = new int[4]
	ids[0] = t1
	ids[1] = t2
	ids[2] = t3
	ids[3] = t4
	int n = 0
	while n < 4 && ids[n] != 0
		Actor speaker = a
		Actor listener = b
		if n % 2 == 1
			speaker = b
			listener = a
		endif
		Topic t = VoiceTopic(ids[n])
		if !t || !CanSpeak(speaker) || speaker.GetDistance(listener) > 1000.0
			n = 4
		else
			WaitUntilQuiet(listener)
			Utility.Wait(0.4)
			speaker.Say(t)
			VoiceDebug(speaker, ids[n])
			Utility.Wait(0.6)
			WaitUntilQuiet(speaker)
			n += 1
		endif
	endwhile
	if a.Is3DLoaded()
		a.ClearLookAt()
	endif
	if b.Is3DLoaded()
		b.ClearLookAt()
	endif
	ReleaseTalk()
EndFunction

Function WaitUntilQuiet(Actor x)
	int w = 0
	while w < 40 && ONPCR_Ladder.IsSpeaking(x)	; at most ~10 s
		Utility.Wait(0.25)
		w += 1
	endwhile
	Debug.Trace("ONPCR: waited " + (w * 0.25) + " s for " + x.GetDisplayName() + " to finish speaking")
EndFunction

Topic Function VoiceTopic(int id)
	return Game.GetFormFromFile(id, "OStimNPCsReforged - Letters.esp") as Topic
EndFunction

bool Function CanSpeak(Actor x)
	return x && x.Is3DLoaded() && !x.IsDead() && !x.IsInCombat() && x.GetSleepState() == 0 && !x.IsInDialogueWithPlayer() && !OActor.IsInOStim(x)
EndFunction

bool Function VoiceReady(Actor a, Actor b)
	float cooldown = ONPCR_Settings.GetFloat("VoiceCooldownHours", 3.0) / 24.0
	float now = Utility.GetCurrentGameTime()
	return now - StorageUtil.GetFloatValue(a, "ONPCR_VoiceAt", -100.0) >= cooldown && now - StorageUtil.GetFloatValue(b, "ONPCR_VoiceAt", -100.0) >= cooldown
EndFunction

Function MarkVoiced(Actor a, Actor b)
	float now = Utility.GetCurrentGameTime()
	StorageUtil.SetFloatValue(a, "ONPCR_VoiceAt", now)
	StorageUtil.SetFloatValue(b, "ONPCR_VoiceAt", now)
EndFunction

; couples near the player say something to each other now and then (one line per update, cooldown per couple)
; 0.16.0 friend chats: now and then a married or engaged NPC near you tells a friend or relative standing close by (not their
; partner) about their partner or their wedding, and the friend answers (parts f1 / f2; lines limited by the speaker's own couple:
; married / engaged, warm / cold, husband / wife). At most one per game hour, each NPC at most once a day.
Function FriendChats()
	if !ONPCR_Settings.GetBool("VoiceLines", true) || !ONPCR_Settings.GetBool("FriendChats", true)
		return
	endif
	float now = Utility.GetCurrentGameTime()
	if now - LastFriendChat < 1.0 / 24.0 || Utility.RandomInt(1, 4) != 1
		return
	endif
	int[] v = ONPCR_Ladder.GetVoicePairs(3000.0)
	int i = 0
	while i + 2 < v.Length
		int stage = v[i + 2]
		if stage == 3 || stage == 4
			int k = 0
			while k < 2
				Actor x = Game.GetFormEx(v[i + k]) as Actor
				Actor partner = Game.GetFormEx(v[i + 1 - k]) as Actor
				if x && CanSpeak(x) && ONPCR_Ladder.IsIdle(x) && now - StorageUtil.GetFloatValue(x, "ONPCR_FriendChatAt", -100.0) >= 1.0
					Actor[] near = MiscUtil.ScanCellNPCs(x, 700.0, None, false)
					int j = 0
					while j < near.Length
						Actor y = near[j]
						if y && y != x && y != partner && y != PlayerRef && !y.IsChild() && CanSpeak(y) && ONPCR_Ladder.IsIdle(y) && x.GetRelationshipRank(y) >= 1
							string part = "f1"
							if stage == 3
								part = "f2"
							endif
							int[] plan = ONPCR_Ladder.PlanPart(x, y, part)
							if plan.Length > 0
								LastFriendChat = now
								StorageUtil.SetFloatValue(x, "ONPCR_FriendChatAt", now)
								Debug.Trace("ONPCR: friend chat (" + part + ") " + x.GetDisplayName() + " -> " + y.GetDisplayName())
								PlayPlan(x, y, plan)
								return
							endif
						endif
						j += 1
					endwhile
				endif
				k += 1
			endwhile
		endif
		i += 3
	endwhile
EndFunction
float LastFriendChat = -100.0

; 0.23.0 family chats (handoff 19): parents, children, siblings and grandparents near you now and then say an everyday line to
; each other (the "family" parts of the conversation file - never romance: the DLL refuses a pair with a couple stage, and the
; couple planner refuses relatives). Pairs from the game's relationship records (DLL GetFamilyPairs) plus Beeing Female children
; of NPC couples with their mother and father (no record - rel 1). One in four ticks, at most one an hour, each speaker once a day.
Function FamilyChats()
	if !ONPCR_Settings.GetBool("VoiceLines", true) || !ONPCR_Settings.GetBool("FamilyChats", true) || ONPCR_Settings.GetVersion() < 15
		return
	endif
	float now = Utility.GetCurrentGameTime()
	if now - LastFamilyChat < 1.0 / 24.0 || Utility.RandomInt(1, 4) != 1
		return
	endif
	int[] v = ONPCR_Ladder.GetFamilyPairs(3000.0)
	int start = 0
	if v.Length > 3
		start = Utility.RandomInt(0, v.Length / 3 - 1) * 3	; not always the same family first
	endif
	int i = 0
	while i + 2 < v.Length
		int k = (start + i) % v.Length
		if TryFamilyChat(Game.GetFormEx(v[k]) as Actor, Game.GetFormEx(v[k + 1]) as Actor, v[k + 2], now)
			return
		endif
		i += 3
	endwhile
	int c = StorageUtil.FormListCount(None, "FW.Babys")
	while c > 0
		c -= 1
		Actor kid = StorageUtil.FormListGet(None, "FW.Babys", c) as Actor
		if kid && kid.Is3DLoaded() && kid.GetParentCell() == PlayerRef.GetParentCell()
			if TryFamilyChat(StorageUtil.GetFormValue(kid, "FW.Child.Mother") as Actor, kid, 1, now) || TryFamilyChat(StorageUtil.GetFormValue(kid, "FW.Child.Father") as Actor, kid, 1, now)
				return
			endif
		endif
	endwhile
EndFunction
float LastFamilyChat = -100.0

bool Function TryFamilyChat(Actor x, Actor y, int rel, float now)
	if !x || !y || x == y || x == PlayerRef || y == PlayerRef || x.GetDistance(y) > 700.0
		return false
	endif
	if now - StorageUtil.GetFloatValue(x, "ONPCR_FamilyChatAt", -100.0) < 1.0 || now - StorageUtil.GetFloatValue(y, "ONPCR_FamilyChatAt", -100.0) < 1.0
		return false
	endif
	if !CanSpeak(x) || !CanSpeak(y) || !ONPCR_Ladder.IsIdle(x) || !ONPCR_Ladder.IsIdle(y) || ONPCR_Ladder.IsQuestDriven(x) || ONPCR_Ladder.IsQuestDriven(y)
		return false
	endif
	int[] plan = ONPCR_Ladder.PlanFamily(x, y, rel)
	if plan.Length < 3
		return false
	endif
	LastFamilyChat = now
	StorageUtil.SetFloatValue(x, "ONPCR_FamilyChatAt", now)
	StorageUtil.SetFloatValue(y, "ONPCR_FamilyChatAt", now)
	string relName = "kin"
	if rel == 1
		relName = "parent"
	elseif rel == 2
		relName = "siblings"
	elseif rel == 3
		relName = "grandparent"
	endif
	Debug.Trace("ONPCR: family chat (" + relName + ") " + x.GetDisplayName() + " -> " + y.GetDisplayName() + " (" + (plan.Length / 3) + " steps)")
	if WalkOver(x, y)
		PlayPlan(x, y, plan)
	endif
	ReleaseTalk()
	return true
EndFunction

Function VoiceCouples()
	; also keeps wedding rings on married couples near the player, whether voice lines are on or not
	bool speak = ONPCR_Settings.GetBool("VoiceLines", true)
	bool dbg = ONPCR_Settings.GetBool("DebugNotifications", false)
	int[] v = ONPCR_Ladder.GetVoicePairs(3000.0)
	int i = 0
	while i + 2 < v.Length
		Actor a = Game.GetFormEx(v[i]) as Actor
		Actor b = Game.GetFormEx(v[i + 1]) as Actor
		int stage = v[i + 2]
		if stage == 4 && a && b
			ONPCR_Ladder.EnsureRing(a)
			ONPCR_Ladder.EnsureRing(b)
		endif
		if speak && a && b
			; why a nearby couple does not chat (trace only with Debug notifications on)
			string why = ""
			if !CanSpeak(a) || !CanSpeak(b)
				why = "busy (combat / asleep / talking to you / in a scene)"
			elseif a.GetDistance(b) >= 1200.0
				why = "too far apart"
			elseif !ONPCR_Ladder.IsIdle(a) || !ONPCR_Ladder.IsIdle(b)
				why = "not idle (walking somewhere, weapon out, asleep...)"
			elseif !VoiceReady(a, b)
				why = "cooldown"
			endif
			if why != ""
				if dbg && why != "cooldown"
					Debug.Trace("ONPCR: " + a.GetDisplayName() + " + " + b.GetDisplayName() + " no chat: " + why)
				endif
			else
				int first = 0
				int reply = 0
				int third = 0
				int fourth = 0
				if stage == 2
					first = 0x90B		; dating (0.10.0): a greeting each, then "are you... interested in me?" / "I am..."
					reply = 0x90B
					third = 0x807
					fourth = 0x807
				elseif stage == 3
					first = 0x82E		; engaged: wedding soon, wedding soon back, "it's settled", "Together."
					reply = 0x82E
					third = 0x80F
					fourth = 0x80F
				elseif stage == 4
					first = 0x817		; married: greeting, greeting back, then a goodbye each (0.10.0: mixed with the generic
					reply = 0x817		; greetings / goodbyes, and now and then a small gift instead of the goodbye)
					third = 0x824
					fourth = 0x824
					if Utility.RandomInt(0, 2) == 0
						first = 0x90B
						reply = 0x90B
					endif
					int r = Utility.RandomInt(0, 3)
					if r == 0
						third = 0x946
						fourth = 0x94A
					elseif r == 1
						third = 0x913
						fourth = 0x913
					endif
				elseif stage == 5 || stage == 6
					first = 0x83F		; jilted / divorced: a cold word, and a cold word back
					reply = 0x837
				endif
				; everyday conversations (0.13.0): most chats are built from the reviewed parts - a greeting, one or two topics that
				; fit the moment, a parting - planned by the DLL for these two voices; the romance moments above now and then
				if stage >= 1 && stage <= 6 && Utility.RandomInt(0, 4) > 0
					Actor s1 = a
					Actor s2 = b
					if Utility.RandomInt(0, 1) == 1
						s1 = b
						s2 = a
					endif
					if PlannedChat(s1, s2)
						return
					endif
				endif
				if first
					Actor speaker = a
					if Utility.RandomInt(0, 1) == 1
						speaker = b
					endif
					Actor other = b
					if speaker == b
						other = a
					endif
					MarkVoiced(a, b)
					speaker = WalkOver(speaker, other)
					if speaker
						if speaker == b
							other = a
						else
							other = b
						endif
						Converse(speaker, other, first, reply, third, fourth)
					endif
					return
				endif
			endif
		endif
		i += 3
	endwhile
EndFunction

; ---- everyday chat (0.12.0): topics in the Letters plugin, one per step, each holding many vanilla lines (Random), every
; line limited to the voices that recorded it. ONPCR_Ladder.CanSay asks whether a speaker has any line in a step right now.
; Topic ids are filled in at build time; 0 = not built yet (chat falls back to the romance lines).
int Function TalkTopicId(int stepIndex)
	; 0 open, 1 reply, 2 think aloud, 3 part, 4 cold reply, 5 date-night extra,
	; exchanges: 6 ask for a drink, 7 hand it over, 8 none to give, 9 offer a drink, 10 accept, 11 decline, 12 after drinking,
	; 13 ask how they feel, 14 how they feel
	int[] ids = new int[15]
	ids[0] = 0
	ids[1] = 0
	ids[2] = 0
	ids[3] = 0
	ids[4] = 0
	ids[5] = 0
	ids[6] = 0
	ids[7] = 0
	ids[8] = 0
	ids[9] = 0
	ids[10] = 0
	ids[11] = 0
	ids[12] = 0
	ids[13] = 0
	ids[14] = 0
	return ids[stepIndex]
EndFunction

bool Function SmallTalkReady()
	return TalkTopicId(0) != 0 && ONPCR_Settings.GetVersion() >= 9
EndFunction

; who can say this step? the preferred speaker, else the other one, else None
Actor Function TalkSpeaker(int stepIndex, Actor preferred, Actor other)
	Topic t = VoiceTopic(TalkTopicId(stepIndex))
	if !t
		return None
	endif
	if ONPCR_Ladder.CanSay(preferred, t, other)
		return preferred
	elseif ONPCR_Ladder.CanSay(other, t, preferred)
		return other
	endif
	return None
EndFunction

; a: starts, b: answers. Needs at least an opener and one more line, or it does nothing and returns false.
bool Function SmallTalk(Actor a, Actor b, int stage)
	; now and then an exchange instead: a question gets an answer that fits, and a drink really changes hands
	int roll = Utility.RandomInt(0, 9)
	if stage <= 4 && roll <= 2 && DrinkExchange(a, b)
		return true
	elseif stage <= 4 && (roll == 3 || (roll <= 5 && (ONPCR_Ladder.IsPregnant(a) || ONPCR_Ladder.IsPregnant(b)))) && FeelExchange(a, b)
		return true
	endif
	Actor opener = TalkSpeaker(0, a, b)
	if !opener
		return false
	endif
	Actor listener = b
	if opener == b
		listener = a
	endif
	int replyStep = 1
	if stage >= 5
		replyStep = 4	; jilted / divorced: cold answers
	endif
	Actor answerer = None
	Topic rt = VoiceTopic(TalkTopicId(replyStep))
	if rt && ONPCR_Ladder.CanSay(listener, rt, opener)
		answerer = listener
	endif
	Actor parter = TalkSpeaker(3, listener, opener)
	if !answerer && !parter
		return false
	endif
	MarkVoiced(a, b)
	Actor walker = WalkOver(opener, listener)
	if !walker
		return false
	endif
	opener.SetLookAt(listener)
	listener.SetLookAt(opener)
	Debug.Trace("ONPCR: small talk " + opener.GetDisplayName() + " + " + listener.GetDisplayName())
	SpeakLine(opener, TalkTopicId(0))
	if answerer
		SpeakLine(answerer, TalkTopicId(replyStep))
	endif
	if stage <= 4 && Utility.RandomInt(0, 2) == 0
		Actor thinker = TalkSpeaker(2, opener, listener)
		if thinker
			SpeakLine(thinker, TalkTopicId(2))
		endif
	endif
	if parter
		SpeakLine(parter, TalkTopicId(3))
	endif
	opener.ClearLookAt()
	listener.ClearLookAt()
	ReleaseTalk()
	return true
EndFunction

; 0.13.0: a planned conversation - ONPCR_Ladder.PlanChat gives op, role, topic triples (op 0 say, 1 look around, 2 hand a drink
; to the other one). Walks them together, holds them there, plays it through, lets them go. False = nothing fits right now.
bool Function PlannedChat(Actor a, Actor b)
	int[] plan = ONPCR_Ladder.PlanChat(a, b)
	if plan.Length < 3
		return false
	endif
	MarkVoiced(a, b)
	if !WalkOver(a, b)
		return false
	endif
	Debug.Trace("ONPCR: conversation " + a.GetDisplayName() + " + " + b.GetDisplayName() + " (" + (plan.Length / 3) + " steps)")
	PlayPlan(a, b, plan)
	ReleaseTalk()
	return true
EndFunction

; plays op, role, topic triples between a and b where they stand (0.13.1: shared by everyday chats and the wedding moments)
Function PlayPlan(Actor a, Actor b, int[] plan)
	a.SetLookAt(b)
	b.SetLookAt(a)
	int i = 0
	while i + 2 < plan.Length && CanSpeak(a) && CanSpeak(b)
		Actor who = a
		Actor other = b
		if plan[i + 1] == 1
			who = b
			other = a
		endif
		if plan[i] == 0
			Topic t = Game.GetFormEx(plan[i + 2]) as Topic
			if t
				WaitUntilQuiet(a)
				WaitUntilQuiet(b)
				Utility.Wait(0.4)
				who.Say(t)
				Utility.Wait(0.6)
				WaitUntilQuiet(who)
			endif
		elseif plan[i] == 1
			who.ClearLookAt()		; looks around
			Utility.Wait(1.5)
			who.SetLookAt(other)
		elseif plan[i] == 2
			Form drink = FindDrink(who)
			if drink
				HandDrink(who, other, drink)
			endif
		endif
		i += 3
	endwhile
	if a.Is3DLoaded()
		a.ClearLookAt()
	endif
	if b.Is3DLoaded()
		b.ClearLookAt()
	endif
EndFunction

; ---- exchanges ----
; the first alcoholic drink in x's inventory (vanilla AlcoholicDrinksList - Requiem, SunHelm & co. add theirs to it)
Form Function FindDrink(Actor x)
	FormList drinks = Game.GetFormFromFile(0x072EA2, "Skyrim.esm") as FormList
	if !drinks
		return None
	endif
	int i = 0
	int n = drinks.GetSize()
	while i < n
		Form d = drinks.GetAt(i)
		if d && x.GetItemCount(d) > 0
			return d
		endif
		i += 1
	endwhile
	return None
EndFunction

; giver hands the drink over, taker drinks it (vanilla drink animation when standing; equipping a drink makes an NPC drink it)
Function HandDrink(Actor giver, Actor taker, Form drink)
	giver.RemoveItem(drink, 1, true, taker)
	if !ONPCR_Ladder.IsSeated(taker)
		Idle drinkIdle = Game.GetFormFromFile(0x0FD68B, "Skyrim.esm") as Idle	; IdleDrink
		if drinkIdle
			taker.PlayIdle(drinkIdle)
			Utility.Wait(2.5)
		endif
	endif
	taker.EquipItem(drink, false, true)
EndFunction

; "Got any mead?" -> has one: "Here, take this." + the drink + "Thank you."; none: "Sorry, fresh out."
; or the other way round: "Can I get you a drink?" (only when the asker has one) -> accepted (mostly) and drunk, or declined
bool Function DrinkExchange(Actor a, Actor b)
	bool offer = Utility.RandomInt(0, 1) == 1 && FindDrink(a)
	Actor asker = TalkSpeaker(9, a, b)
	if !offer
		asker = TalkSpeaker(6, a, b)
	endif
	if !asker
		return false
	endif
	Actor other = b
	if asker == b
		other = a
	endif
	if offer && !FindDrink(asker)
		return false	; the one who can say the offer line has nothing to offer
	endif
	MarkVoiced(a, b)
	if !WalkOver(asker, other)
		return false
	endif
	asker.SetLookAt(other)
	other.SetLookAt(asker)
	if offer
		SpeakLine(asker, TalkTopicId(9))
		if Utility.RandomInt(0, 3) == 0
			SpeakLine(other, TalkTopicId(11))	; declines
		else
			SpeakLine(other, TalkTopicId(10))
			HandDrink(asker, other, FindDrink(asker))
			SpeakLine(other, TalkTopicId(12))
		endif
	else
		SpeakLine(asker, TalkTopicId(6))
		Form drink = FindDrink(other)
		if drink
			SpeakLine(other, TalkTopicId(7))
			HandDrink(other, asker, drink)
			SpeakLine(asker, TalkTopicId(12))
		else
			SpeakLine(other, TalkTopicId(8))
		endif
	endif
	asker.ClearLookAt()
	other.ClearLookAt()
	Debug.Trace("ONPCR: drink exchange " + asker.GetDisplayName() + " + " + other.GetDisplayName() + " (offer " + offer + ")")
	ReleaseTalk()
	return true
EndFunction

; "How are you feeling?" -> a fitting answer
bool Function FeelExchange(Actor a, Actor b)
	; mostly for an expecting partner (asked by the other one); otherwise only now and then
	bool pa = ONPCR_Ladder.IsPregnant(a)
	bool pb = ONPCR_Ladder.IsPregnant(b)
	if !pa && !pb && Utility.RandomInt(0, 2) != 0
		return false
	endif
	if pa && !pb
		Actor t = a
		a = b
		b = t
	endif
	Actor asker = TalkSpeaker(13, a, b)
	if !asker
		return false
	endif
	Actor other = b
	if asker == b
		other = a
	endif
	Topic ft = VoiceTopic(TalkTopicId(14))
	if !ft || !ONPCR_Ladder.CanSay(other, ft, asker)
		return false
	endif
	MarkVoiced(a, b)
	if !WalkOver(asker, other)
		return false
	endif
	SpeakLine(asker, TalkTopicId(13))
	SpeakLine(other, TalkTopicId(14))
	ReleaseTalk()
	return true
EndFunction

; when apart, one partner walks over (vanilla PathToReference, latent). A seated partner never gets up - the standing one
; walks; both seated and apart = no chat. Returns who walked/speaks first, or None if it did not work out.
Actor Function WalkOver(Actor speaker, Actor other)
	if speaker.GetDistance(other) <= 220.0
		HoldTogether(speaker, other)
		return speaker
	endif
	if ONPCR_Ladder.IsSeated(speaker)
		Actor swap = speaker
		speaker = other
		other = swap
	endif
	if ONPCR_Ladder.IsSeated(speaker)
		Debug.Trace("ONPCR: " + speaker.GetDisplayName() + " + " + other.GetDisplayName() + " both seated, too far apart")
		return None
	endif
	Debug.Trace("ONPCR: " + speaker.GetDisplayName() + " walks over to " + other.GetDisplayName())
	if ONPCR_Settings.GetBool("DebugNotifications", false)
		Debug.Notification("[Reforged] " + speaker.GetDisplayName() + " walks over to " + other.GetDisplayName())
	endif
	; 0.12.2: never block. PathToReference waited until arrival - with a blocked path or a partner who left, the whole couple
	; loop froze (no chats, letters or news). Now a "walk to your partner" package (Letters 0x977, link keyword 0x976), checked
	; every second, given up after 20 s or when the partner leaves the cell.
	Package walk = Game.GetFormFromFile(0x977, "OStimNPCsReforged - Letters.esp") as Package
	Keyword link = Game.GetFormFromFile(0x976, "OStimNPCsReforged - Letters.esp") as Keyword
	if !walk || !link
		return None
	endif
	PO3_SKSEFunctions.SetLinkedRef(speaker, other, link)
	ActorUtil.AddPackageOverride(speaker, walk, 100, 1)
	speaker.EvaluatePackage()
	int t = 0
	while t < 20 && speaker.GetDistance(other) > 200.0 && speaker.GetParentCell() == other.GetParentCell() && CanSpeak(speaker) && CanSpeak(other)
		Utility.Wait(1.0)
		t += 1
	endwhile
	ActorUtil.RemovePackageOverride(speaker, walk)
	PO3_SKSEFunctions.SetLinkedRef(speaker, None, link)
	speaker.EvaluatePackage()
	if speaker.GetParentCell() != other.GetParentCell() || speaker.GetDistance(other) > 320.0 || !CanSpeak(speaker) || !CanSpeak(other)
		Debug.Trace("ONPCR: walk over given up after " + t + " s (partner left, path blocked or busy)")
		return None
	endif
	HoldTogether(speaker, other)
	return speaker
EndFunction

; 0.12.3: once together, both stay put for the chat (a "go to your partner" package on each, linked to the other - they
; stand where they are once close). Before, the walker was let go on arrival and both drifted back to what they were doing.
; Seated people are left alone (the package would make them stand up). Released by ReleaseTalk, or after 60 s at the latest.
Function HoldTogether(Actor a, Actor b)
	ReleaseTalk()
	Package walk = Game.GetFormFromFile(0x977, "OStimNPCsReforged - Letters.esp") as Package
	Keyword link = Game.GetFormFromFile(0x976, "OStimNPCsReforged - Letters.esp") as Keyword
	if !walk || !link
		return
	endif
	HeldA = a
	HeldB = b
	HeldUntil = Utility.GetCurrentRealTime() + 60.0
	if !ONPCR_Ladder.IsSeated(a)
		PO3_SKSEFunctions.SetLinkedRef(a, b, link)
		ActorUtil.AddPackageOverride(a, walk, 100, 1)
		a.EvaluatePackage()
	endif
	if !ONPCR_Ladder.IsSeated(b)
		PO3_SKSEFunctions.SetLinkedRef(b, a, link)
		ActorUtil.AddPackageOverride(b, walk, 100, 1)
		b.EvaluatePackage()
	endif
EndFunction

Function ReleaseTalk()
	if !HeldA && !HeldB
		return
	endif
	; let the last line finish first - a line only counts as "speaking" a moment after Say, so wait a beat, then until both are
	; quiet, then a short pause before they turn away (0.12.4: one used to walk off during the other's last line)
	Utility.Wait(0.8)
	if HeldA && HeldA.Is3DLoaded()
		WaitUntilQuiet(HeldA)
	endif
	if HeldB && HeldB.Is3DLoaded()
		WaitUntilQuiet(HeldB)
	endif
	Utility.Wait(1.0)
	Package walk = Game.GetFormFromFile(0x977, "OStimNPCsReforged - Letters.esp") as Package
	Keyword link = Game.GetFormFromFile(0x976, "OStimNPCsReforged - Letters.esp") as Keyword
	Actor[] held = new Actor[2]
	held[0] = HeldA
	held[1] = HeldB
	HeldA = None
	HeldB = None
	int i = 0
	while i < 2
		if held[i] && walk
			ActorUtil.RemovePackageOverride(held[i], walk)
			if link
				PO3_SKSEFunctions.SetLinkedRef(held[i], None, link)
			endif
			held[i].EvaluatePackage()
		endif
		i += 1
	endwhile
EndFunction

; a ladder event that happens in front of the player is said out loud (news types: 4 dating, 1 engaged, 3 breakup)
Function SayMoment(Actor a, Actor b, int newsType)
	if !ONPCR_Settings.GetBool("VoiceLines", true) || !CanSpeak(a) || !CanSpeak(b)
		return
	endif
	if a.GetDistance(PlayerRef) > 3000.0 || a.GetDistance(b) > 1500.0
		return
	endif
	if newsType == 2
		WeddingMoment(a, b)
		return
	endif
	int topicId = 0
	int reply = 0
	if newsType == 4
		topicId = 0x807
		reply = 0x807
	elseif newsType == 1
		topicId = 0x80F
		reply = 0x80F
	elseif newsType == 3
		topicId = 0x837
		reply = 0x83F
	endif
	Topic t = VoiceTopic(topicId)
	if t
		; a promotion from the F3 page happens with the menu open (game paused) - a line said then is lost, so wait for it to close
		int w = 0
		while Utility.IsInMenuMode() && w < 600
			Utility.WaitMenuMode(0.5)
			w += 1
		endwhile
		Utility.Wait(1.0)
		if !CanSpeak(a)
			return
		endif
		MarkVoiced(a, b)
		Converse(a, b, topicId, reply)
	endif
EndFunction

; 0.10.0: a couple marries in front of the player - both say the vow, then the nearest bystander congratulates them
Function WeddingMoment(Actor a, Actor b)
	int w = 0
	while Utility.IsInMenuMode() && w < 600
		Utility.WaitMenuMode(0.5)
		w += 1
	endwhile
	Utility.Wait(1.0)
	if !CanSpeak(a) || !CanSpeak(b)
		return
	endif
	MarkVoiced(a, b)
	Actor walker = WalkOver(a, b)
	; 0.13.2: after a temple ceremony the vows were already said there - only the congratulations follow
	if StorageUtil.GetIntValue(a, "ONPCR_VowsSaid", 0) == 1 || StorageUtil.GetIntValue(b, "ONPCR_VowsSaid", 0) == 1
		StorageUtil.UnsetIntValue(a, "ONPCR_VowsSaid")
		StorageUtil.UnsetIntValue(b, "ONPCR_VowsSaid")
	else
		Converse(a, b, 0x93D, 0x93D)
	endif
	Actor[] near = MiscUtil.ScanCellNPCs(a, 1200.0, None, true)
	Actor guest = None
	float best = 999999.0
	int i = 0
	while i < near.Length
		Actor x = near[i]
		if x && x != a && x != b && x != PlayerRef && !x.IsChild() && CanSpeak(x)
			float dd = x.GetDistance(a)
			if dd < best
				guest = x
				best = dd
			endif
		endif
		i += 1
	endwhile
	if guest
		guest.SetLookAt(a)
		SpeakLine(guest, 0x942)		; "Congratulations on your wedding. I'm so happy for both of you."
		guest.ClearLookAt()
	endif
EndFunction

; 0.10.0: a widowed NPC near the player mentions their loss now and then (idle, once per game day)
Function Mourners()
	if !ONPCR_Settings.GetBool("VoiceLines", true)
		return
	endif
	int[] m = ONPCR_Ladder.GetMourners(700.0)
	int i = 0
	while i + 2 < m.Length
		Actor s = Game.GetFormEx(m[i]) as Actor
		if s && CanSpeak(s) && ONPCR_Ladder.IsIdle(s) && Utility.GetCurrentGameTime() - StorageUtil.GetFloatValue(s, "ONPCR_MournAt", -100.0) >= 1.0
			StorageUtil.SetFloatValue(s, "ONPCR_MournAt", Utility.GetCurrentGameTime())
			int topicId = 0x935		; lost boyfriend
			if m[i + 2] == 1
				topicId = 0x92D		; husband
				if m[i + 1] == 1
					topicId = 0x931	; wife
				endif
			elseif m[i + 1] == 1
				topicId = 0x939		; lost girlfriend
			endif
			SpeakLine(s, topicId)
			return
		endif
		i += 3
	endwhile
EndFunction

; jealousy: a committed NPC starts a scene with someone else while their partner is close by
Function JealousyStart(Actor x, Actor other)
	if !LadderAvailable || !x || !other || !ONPCR_Settings.GetBool("VoiceLines", true) || !ONPCR_Settings.GetBool("Infidelity", true)
		return
	endif
	Actor p = ONPCR_Ladder.GetPartnerRef(x)
	if p && p != other && p != PlayerRef && CanSpeak(p) && p.GetDistance(x) < 2000.0
		Topic t = VoiceTopic(0x847)
		if t
			p.Say(t)
			VoiceDebug(p, 0x847)
			StorageUtil.SetFormValue(x, "ONPCR_CaughtBy", p)
		endif
	endif
EndFunction

; 0.10.0: after the scene, the argument - the strayer's excuse, the partner not buying it, then forgiveness when the couple
; survived the stray (still dating / engaged / married) or being thrown out and heartbreak when it broke them up
Function JealousyEnd(Actor x)
	if !LadderAvailable || !x
		return
	endif
	Actor p = StorageUtil.GetFormValue(x, "ONPCR_CaughtBy") as Actor
	if !p
		return
	endif
	StorageUtil.UnsetFormValue(x, "ONPCR_CaughtBy")
	Utility.Wait(2.0)
	if !CanSpeak(p) || !CanSpeak(x) || p.GetDistance(x) > 3000.0
		return
	endif
	if p.GetDistance(x) > 300.0 && !ONPCR_Ladder.IsSeated(p)
		p.PathToReference(x, 1.0)
	endif
	p.SetLookAt(x)
	x.SetLookAt(p)
	SpeakLine(x, 0x91B)		; "I didn't mean nothing by it!..."
	SpeakLine(p, 0x920)		; "Yeah, sure it is. You don't fool me."
	int stage = ONPCR_Ladder.GetStage(x, p)
	if stage >= 2 && stage <= 4
		SpeakLine(p, 0x926)	; "I guess I can look the other way, this time."
	else
		SpeakLine(p, 0x837)	; "You, of all people. Get out!"
		SpeakLine(p, 0x88F)	; heartbroken
	endif
	if p.Is3DLoaded()
		p.ClearLookAt()
	endif
	if x.Is3DLoaded()
		x.ClearLookAt()
	endif
EndFunction

; one line, after whoever is talking nearby has finished
Function SpeakLine(Actor speaker, int topicId)
	Topic t = VoiceTopic(topicId)
	if !t || !CanSpeak(speaker)
		return
	endif
	WaitUntilQuiet(speaker)
	Utility.Wait(0.4)
	speaker.Say(t)
	VoiceDebug(speaker, topicId)
	Utility.Wait(0.6)
	WaitUntilQuiet(speaker)
EndFunction
; only ever raises: a rank the game (or another mod) set higher stays
Function RaiseRank(Actor a, Actor b, int r)
	if a.GetRelationshipRank(b) < r
		a.SetRelationshipRank(b, r)
	endif
	if b.GetRelationshipRank(a) < r
		b.SetRelationshipRank(a, r)
	endif
EndFunction

; a breakup leaves them acquaintances (rivalries set by the game stay)
Function DropRank(Actor a, Actor b)
	if a.GetRelationshipRank(b) > 0
		a.SetRelationshipRank(b, 0)
	endif
	if b.GetRelationshipRank(a) > 0
		b.SetRelationshipRank(a, 0)
	endif
EndFunction

Function ProcessLadderNews()
	int[] n = ONPCR_Ladder.PopNews()
	if !n || n.Length < 3
		return
	endif
	bool letters = ONPCR_Settings.GetBool("LettersEnabled", true) && Game.IsPluginInstalled("OStimNPCsReforged - Letters.esp")
	int candStage = ONPCR_Settings.GetInt("CandidateLetterStage", 2)
	; Notifications: 0 none, 1 friends of yours + NPCs you are courting (letters too), 2 every couple (strangers get a notification, no letter)
	int notif = ONPCR_Settings.GetInt("Notifications", 1)
	; the game's relationship rank follows the couple (dating 2 confidant, engaged 3 ally, married 4 lover; breakup 0)
	bool rankOn = ONPCR_Settings.GetBool("RankFollowsLadder", true)
	WICourierScript courier = Game.GetFormFromFile(0x039F82, "Skyrim.esm") as WICourierScript
	int i = 0
	while i + 2 < n.Length
		int newsType = n[i]
		Actor a = Game.GetFormEx(n[i + 1]) as Actor
		Actor b = Game.GetFormEx(n[i + 2]) as Actor
		if a && b && newsType == 8
			; 0.19.0: a secret couple or a side relationship ended - no news, only the rank drops so their own romance lines stop
			DropRank(a, b)
		elseif a && b
			if rankOn
				if newsType == 4
					RaiseRank(a, b, 2)
				elseif newsType == 1
					RaiseRank(a, b, 3)
				elseif newsType == 2
					RaiseRank(a, b, 4)
				endif
			endif
			if newsType == 3
				DropRank(a, b)  ; 0.19.0: always, even with RankFollowsLadder off - the romance-line switch needs it
			endif
			SayMoment(a, b, newsType)
			string names = a.GetDisplayName() + " and " + b.GetDisplayName()
			bool candidate = IsPlayerCandidate(a) || IsPlayerCandidate(b)
			bool friend = IsFriendOfPlayer(a) || IsFriendOfPlayer(b)
			bool close = friend || candidate
			Actor writer = a
			Actor other = b
			; the writer is the one close to you; an NPC you are courting always writes their own letter
			if (IsPlayerCandidate(b) && !IsPlayerCandidate(a)) || !(IsPlayerCandidate(a) || IsFriendOfPlayer(a))
				writer = b
				other = a
			endif
			if newsType == 4
				; started dating: only an NPC the player is courting writes, and only if the setting says so
				if candidate && candStage == 1
					SendLetter(courier, letters, 0x803, names + " are now dating", writer, other)
				elseif notif >= 2
					Debug.Notification(names + " are now dating")
				endif
			elseif newsType == 1
				if candidate && candStage == 2
					SendLetter(courier, letters, 0x803, names + " are engaged", writer, other)
				elseif close
					SendLetter(courier, letters, 0x800, names + " are engaged", writer, other)
				elseif notif >= 2
					Debug.Notification(names + " are engaged")
				endif
			elseif newsType == 7
				; real weddings: the date is set - two days from now, in the afternoon (noon to 17:00), Temple of Mara in Riften
				if close
					SendLetter(courier, letters, 0x974, names + " will marry at the Temple of Mara in two days (afternoon)", writer, other)
					if ONPCR_Settings.GetVersion() >= 11
						ONPCR_Ladder.SetWeddingInvite(a, b)	; 0.14.0: you were invited - they wait up to 3 more days for you
					endif
				elseif notif >= 1
					Debug.Notification(names + " will marry at the Temple of Mara in Riften in two days, in the afternoon")
				endif
			elseif newsType == 2 && StorageUtil.GetIntValue(a, "ONPCR_WedAttended", 0) == 1
				; you were at the ceremony: a thank-you instead of the usual news
				StorageUtil.UnsetIntValue(a, "ONPCR_WedAttended")
				StorageUtil.UnsetIntValue(b, "ONPCR_WedAttended")
				StorageUtil.UnsetIntValue(a, "ONPCR_VowsSaid")
				if close
					SendLetter(courier, letters, 0x975, names + " thank you for coming to their wedding", writer, other)
				endif
			elseif newsType == 2
				if candidate && candStage == 3
					SendLetter(courier, letters, 0x803, names + " have married", writer, other)
				elseif close
					SendLetter(courier, letters, 0x801, names + " have married", writer, other)
				elseif notif >= 2
					Debug.Notification(names + " have married")
				endif
			elseif newsType == 3
				if close
					SendLetter(courier, letters, 0x802, names + " have parted ways", writer, other)
				elseif notif >= 2
					Debug.Notification(names + " have parted ways")
				endif
			elseif newsType == 5
				; a = the survivor, b = the one who died
				if IsFriendOfPlayer(a) || IsPlayerCandidate(a)
					SendLetter(courier, letters, 0x804, a.GetDisplayName() + " has lost " + b.GetDisplayName(), a, b)
				elseif notif >= 2
					Debug.Notification(a.GetDisplayName() + " has lost " + b.GetDisplayName())
				endif
			elseif newsType == 6 && ONPCR_Settings.GetBool("Gossip", true) && (close || notif >= 2) && notif >= 1
				; gossip only - a = the one who strayed, b = their partner
				Debug.Notification("Word is " + a.GetDisplayName() + " has been seeing someone behind " + b.GetDisplayName() + "'s back")
			endif
		endif
		i += 3
	endwhile
EndFunction

; ---- 0.21.3 who can have children (user rules 2026-10-22): vampires never until cured, automatons / androids (Ashe, Lyra) never,
; Khajiit and Argonians only with their own kind (every Khajiit sub-race is one kind). This mod's own couples are gated in the DLL
; (CanHaveChildren). This function makes Beeing Female follow the same rules for ITS scenes (OStim, the player's, its random NPC
; insemination) without editing Beeing Female: its per-person switch FW.AddOn.DisablePregnancy (read by
; FWAddOnManager.ActorCanBecomePregnant) and the sperm it already stored on a woman (FW.SpermName / FW.SpermAmount lists - an amount
; of 0 is below its impregnation minimum and is cleaned up by Beeing Female itself). Runs every 30 s for the women around you, the
; player included; a cure takes effect within that time. The switch is only put back when this mod set it (marker ONPCR.PregBlock).
; A vampire = has the vampire NPC trait spell (Serana gets it from Dawnguard's NPC mental model alias, cleared by her cure), or the
; Vampire keyword on the record (Valerica, Harkon, Alva) or on the race (vampire races, a turned player).
; Research: Notes and Reports\Serana vampirism source + one vampire test 2026-10-22.md
Spell PregVampTrait
Keyword PregVampKeyword
ActorBase PregAshe
ActorBase PregLyra
bool PregFormsReady = false
int PregRuleTick = 0

Function PregnancyRules()
	PregRuleTick += 1
	if PregRuleTick < 3
		return
	endif
	PregRuleTick = 0
	if !Game.IsPluginInstalled("BeeingFemale.esm") || !ONPCR_Settings.GetBool("PregnancyRules", true)
		return
	endif
	if !PregFormsReady
		PregVampTrait = Game.GetFormFromFile(0x000F5B5C, "Skyrim.esm") as Spell
		PregVampKeyword = Game.GetFormFromFile(0x000A82BB, "Skyrim.esm") as Keyword
		if Game.IsPluginInstalled("Ashe - Fire and Blood.esp")
			PregAshe = Game.GetFormFromFile(0x005900, "Ashe - Fire and Blood.esp") as ActorBase
		endif
		if Game.IsPluginInstalled("Lyra.esp")
			PregLyra = Game.GetFormFromFile(0x000988, "Lyra.esp") as ActorBase
		endif
		PregFormsReady = true
	endif
	bool ownKind = ONPCR_Settings.GetBool("BeastChildrenOwnKind", true)
	Actor[] near = MiscUtil.ScanCellNPCs(PlayerRef, 4096.0, None, false)
	int i = -1
	while i < near.Length
		Actor x = PlayerRef
		if i >= 0
			x = near[i]
		endif
		if x && !x.IsChild() && !x.IsDead() && x.GetActorBase().GetSex() == 1
			ApplyPregnancyRules(x, ownKind)
		endif
		i += 1
	endwhile
EndFunction

Function ApplyPregnancyRules(Actor w, bool ownKind)
	bool blocked = NoChildrenPerson(w)
	int marker = StorageUtil.GetIntValue(w, "ONPCR.PregBlock", 0)
	if blocked && marker == 0
		if StorageUtil.GetIntValue(w, "FW.AddOn.DisablePregnancy", 0) == 0
			StorageUtil.SetIntValue(w, "FW.AddOn.DisablePregnancy", 1)
			StorageUtil.SetIntValue(w, "ONPCR.PregBlock", 1)
			Debug.Trace("ONPCR: pregnancy rules - " + w.GetDisplayName() + " cannot get pregnant (vampire / automaton)")
		endif
	elseif !blocked && marker == 1
		StorageUtil.SetIntValue(w, "FW.AddOn.DisablePregnancy", 0)
		StorageUtil.UnsetIntValue(w, "ONPCR.PregBlock")
		Debug.Trace("ONPCR: pregnancy rules - " + w.GetDisplayName() + " can have children again (cured)")
	endif
	; sperm Beeing Female already stored from a father the rules forbid
	int n = StorageUtil.FormListCount(w, "FW.SpermName")
	int m = StorageUtil.FloatListCount(w, "FW.SpermAmount")
	if m < n
		n = m
	endif
	int c = 0
	while c < n
		Actor father = StorageUtil.FormListGet(w, "FW.SpermName", c) as Actor
		if father && StorageUtil.FloatListGet(w, "FW.SpermAmount", c) > 0.0
			if NoChildrenPerson(father) || (ownKind && BeastKind(w) != BeastKind(father))
				StorageUtil.FloatListSet(w, "FW.SpermAmount", c, 0.0)
				Debug.Trace("ONPCR: pregnancy rules - " + father.GetDisplayName() + " cannot father a child with " + w.GetDisplayName())
			endif
		endif
		c += 1
	endwhile
EndFunction

; no children by nature: a vampire right now, or an automaton / android
bool Function NoChildrenPerson(Actor a)
	if !a
		return false
	endif
	ActorBase b = a.GetActorBase()
	if (PregAshe && b == PregAshe) || (PregLyra && b == PregLyra)
		return true
	endif
	if PregVampTrait && a.HasSpell(PregVampTrait)
		return true
	endif
	if PregVampKeyword
		if a.HasKeyword(PregVampKeyword)
			return true
		endif
		Race r = a.GetRace()
		if r && r.HasKeyword(PregVampKeyword)
			return true
		endif
	endif
	return false
EndFunction

; 0 men and mer, 1 Khajiit (any sub-race), 2 Argonian - by the race's name, the same words the DLL's Kind() uses
int Function BeastKind(Actor a)
	Race r = a.GetRace()
	if !r
		return 0
	endif
	string n = r.GetName()
	if StringUtil.Find(n, "Argonian") >= 0 || StringUtil.Find(n, "Saxhleel") >= 0 || StringUtil.Find(n, "Lukiul") >= 0
		return 2
	endif
	if StringUtil.Find(n, "Khajiit") >= 0 || StringUtil.Find(n, "Ohmes") >= 0 || StringUtil.Find(n, "Raht") >= 0 || StringUtil.Find(n, "Cathay") >= 0 || StringUtil.Find(n, "Suthay") >= 0 || StringUtil.Find(n, "Tojay") >= 0 || StringUtil.Find(n, "Pahmar") >= 0 || StringUtil.Find(n, "Dagi") >= 0
		return 1
	endif
	return 0
EndFunction

; Reforged slice 4: a simulated man-woman encounter goes to the pregnancy engine the same way the player's scenes do
; (Beeing Female NG, Fertility Mode and Fertility Mode Reloaded all listen for FertilityModeAddSperm; their own cycle decides)
Function HandToPregnancyEngine(Actor a, Actor b)
	if !ONPCR_Settings.GetBool("PregnancyEvents", true)
		return
	endif
	int sexA = a.GetActorBase().GetSex()
	int sexB = b.GetActorBase().GetSex()
	if sexA == sexB
		return
	endif
	if !ONPCR_Ladder.CanHaveChildren(a, b)
		return		; one of them cannot (an automaton), or Khajiit / Argonian with another kind - Followers page, Relationships page
	endif
	Actor woman = a
	Actor father = b
	if sexA == 0
		woman = b
		father = a
	endif
	int h = ModEvent.Create("FertilityModeAddSperm")
	if h
		ModEvent.PushForm(h, woman)
		ModEvent.PushString(h, father.GetDisplayName())
		ModEvent.PushForm(h, father)
		ModEvent.Send(h)
	endif
EndFunction

; 0.21.4 (handoff 15): Beeing Female only turns a carried baby item into a child when the player is the father. Babies of two NPCs
; hatch here instead, on Beeing Female's own clock and through its own functions (ONPCR_BeeingFemaleBridge), once per game hour,
; for mothers whose 3D is loaded. Switch: [Pregnancy] bHatchNpcBabies.
Function HatchNpcBabies()
	float hour = Math.Floor(CurrentHour())
	if hour == LastHatchHour
		return
	endif
	LastHatchHour = hour
	if !Game.IsPluginInstalled("BeeingFemale.esm")
		return
	endif
	; 0.24.0 (handoff 21): baby stages - every baby item (the player's too) is carried, then away, then comes home as a child
	if ONPCR_Settings.GetBool("BabyStages", true) && ONPCR_Settings.GetVersion() >= 16
		ONPCR_BeeingFemaleBridge.BabyStages(PlayerRef, ONPCR_Settings.GetInt("BabyCarriedDays", 365) as float, ONPCR_Settings.GetInt("ChildAppearsDays", 1642) as float)
		SendChildLetters()		; 0.25.0
	else
		if ONPCR_Settings.GetBool("HatchNpcBabies", true)
			ONPCR_BeeingFemaleBridge.HatchNpcBabies(PlayerRef)
		endif
		; 0.25.1: stages switched off mid-save - children already away still come home, their parents' factions keep updating
		if ONPCR_Settings.GetVersion() >= 16
			ONPCR_BeeingFemaleBridge.ComeHomePass(PlayerRef, ONPCR_Settings.GetInt("ChildAppearsDays", 1642) as float)
		endif
	endif
EndFunction

; Reforged slice 4: read Beeing Female NG's state for every committed couple once per game hour
; FW.CurrentState 4-9 = pregnant; FW.NumBirth counts births - a rise means a child was born
Function PollPregnancy()
	if !Game.IsPluginInstalled("BeeingFemale.esm")
		return
	endif
	float hour = Math.Floor(CurrentHour())
	if hour == LastPregnancyPollHour
		return
	endif
	LastPregnancyPollHour = hour
	int[] refs = ONPCR_Ladder.GetCommittedPairRefs()
	if !refs || refs.Length < 2
		return
	endif
	int i = 0
	while i + 1 < refs.Length
		Actor a = Game.GetFormEx(refs[i]) as Actor
		Actor b = Game.GetFormEx(refs[i + 1]) as Actor
		if a && b && a.GetActorBase().GetSex() != b.GetActorBase().GetSex()
			Actor woman = a
			Actor father = b
			if a.GetActorBase().GetSex() == 0
				woman = b
				father = a
			endif
			int st = StorageUtil.GetIntValue(woman, "FW.CurrentState", 0)
			ONPCR_Ladder.SetPregnant(woman, st >= 4 && st < 10)
			int births = StorageUtil.GetIntValue(woman, "FW.NumBirth", 0)
			int seen = StorageUtil.GetIntValue(woman, "ONPCR.NumBirthSeen", -1)
			if seen < 0
				StorageUtil.SetIntValue(woman, "ONPCR.NumBirthSeen", births)
			elseif births > seen
				StorageUtil.SetIntValue(woman, "ONPCR.NumBirthSeen", births)
				ONPCR_Ladder.AddChild(woman, father)
			endif
		endif
		i += 2
	endwhile
EndFunction
