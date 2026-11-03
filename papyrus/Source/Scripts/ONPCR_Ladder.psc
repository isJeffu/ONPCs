Scriptname ONPCR_Ladder Hidden
{OStim NPCs Reforged - NPC-to-NPC relationship ladder (phase 2). Implemented by ONPCs.dll 0.3.0+.
 Stages: 0 none, 1 casual, 2 candidate, 3 engaged, 4 married, 5 jilted, 6 divorced, 7 widowed, 8 affair, 9 crush, 10 thrall.
 Pairs are stored by base form and saved in the SKSE co-save.}

; a scene between two NPCs happened (real or simulated). vanillaRelation: 0 none, 1 game-record courting, 2 game-record married (ignored)
Function RecordEncounter(Actor a, Actor b, bool offscreen, int vanillaRelation) global native
Function AllowPlayerChosen(Actor a, Actor b) global native  ; 0.20.3: the player paired them (match-maker) - the same-voice block does not apply
int Function GetStage(Actor a, Actor b) global native
string Function GetStageName(int stage) global native
; committed = candidate, engaged or married
bool Function IsCommittedTo(Actor a, Actor b) global native
bool Function IsCommittedToOther(Actor a, Actor other) global native
int Function GetPartnerStage(Actor a) global native
; the committed partner's BASE form (ActorBase), or None
ActorBase Function GetPartnerBase(Actor a) global native
; day-based promotions; the scan loop calls this
Function Tick() global native
; debug / force
bool Function SetStage(Actor a, Actor b, int stage) global native

; slice 2 (DLL 0.4.0+): simulated encounters waiting to be written to Lover's Ledger - flat triples [refA, refB, day]
int[] Function PopLedgerWrites() global native
int Function GetPoolSize() global native
int Function GetLastSimDay() global native

; slice 3 (DLL 0.5.0+): M.A.R.A.S archetypes (-1 = unknown) and news for courier letters
Function SetArchetype(Actor a, int socialClass, int skillType, int temperament) global native
; actor REF ids of NPCs whose archetype is still unknown (each is handed out once per session)
int[] Function GetArchetypeRequests(int max) global native
; flat triples [type, refA, refB]: type 1 engaged, 2 married, 3 breakup, 4 started courting; refs may be 0 when unknown
int[] Function PopNews() global native
; mourning: a widowed NPC takes no new partner for MourningDays (x pace, x temperament)
bool Function IsMourning(Actor a) global native
; 0.21.2: in a follower mod's own romance faction with the player (Couples json "playerPartnerFactions") - counts like a spouse
bool Function InPlayerPartnerFaction(Actor a) global native

; slice 4 (DLL 0.6.0+): pregnancy engine feedback - expecting pauses drift, a child makes the couple hard to split
Function SetPregnant(Actor woman, bool pregnant) global native
bool Function IsPregnant(Actor a) global native
bool Function CanHaveChildren(Actor a, Actor b) global native
int[] Function PlanChat(Actor a, Actor b) global native
int[] Function PlanPart(Actor a, Actor b, string partId) global native
bool Function PostponeWedding(Actor a, Actor b) global native
Actor[] Function GetFertilityCheckRefs() global native
Function SetEngineChildren(Actor a, int verdict) global native
Function AddChild(Actor mother, Actor father) global native
; actor REF ids of every committed pair as flat pairs [refA, refB]
int[] Function GetCommittedPairRefs() global native

; slice 5 voice: couples whose both partners are loaded within maxDist of the player - refA, refB, stage triples (stages 2-6)
int[] Function GetVoicePairs(float maxDist) global native
; the committed partner (dating / engaged / married) in the world, or None
Actor Function GetPartnerRef(Actor a) global native
; a married NPC near the player: put the wedding ring on again if it is not worn
Function EnsureRing(Actor a) global native
; true while the actor is still saying a line
bool Function IsSpeaking(Actor a) global native
; free for a chat: not walking anywhere, not asleep, weapon away, not in combat / mounted / talking to the player (furniture is fine)
bool Function IsIdle(Actor a) global native
; seated or using any furniture (does not get up to walk over)
bool Function IsSeated(Actor a) global native
; sleep-overs: married couples with a shared home - refA, refB, home location id triples
int[] Function GetMarriedHomes() global native
; a running quest is driving this NPC (alias packages, or a quest-owned current package)
bool Function IsQuestDriven(Actor a) global native
; the NPC's own routine says go to bed right now (current package is a sleep procedure)
bool Function IsOwnBedtime(Actor a) global native
; widowed NPCs alive and loaded near the player: survivorRef, deadIsFemale (0/1), wasMarried (0/1) triples
int[] Function GetMourners(float maxDist) global native
; "ask about their partner": hidden faction rank = couple stage for loaded NPCs near the player; returns how many changed
int Function SyncStageFaction(float maxDist) global native
; date nights: dating (2) and engaged (3) couples - refA, refB, stage triples
int[] Function GetDatingPairs() global native
; do the two NPCs live in the same town / city / settlement (editor locations)?
bool Function SameSettlement(Actor a, Actor b) global native
; real weddings: engaged couples with a wedding set - refA, refB, game hour of the ceremony (day x 24 + 16) triples
int[] Function GetWeddings() global native
; the ceremony is over (or nobody held it): engaged -> married through the usual promotion
bool Function CompleteWedding(Actor a, Actor b) global native
; everyday chat: would the speaker get a line from this topic right now (any line's conditions pass)?
bool Function CanSay(Actor speaker, Topic t, Actor target) global native
; real weddings: guests (actor refs) - children of either partner always, then close family, then friends, up to maxOthers
int[] Function GetWeddingGuests(Actor a, Actor b, int maxOthers) global native
; OnlyMetCouples: remember everyone the player is near (same cell, within maxDist) or has talked to; returns how many were new
int Function NoteMetNearby(float maxDist) global native
; the wedding register in the Temple of Mara: the text of its pop-up (upcoming weddings with their Skyrim dates)
string Function GetWeddingRegister() global native
int Function GetWeddingRegisterPages() global native	; 0.17.2: 8 weddings per page, at least 1
string Function GetWeddingRegisterPage(int page) global native	; page from 1 (clamped); ends with "Page x of y" when there is more than one
; 0.13.3: another mod's wedding is running at the Temple of Mara (Love Is In The Air) - ours gets a new date
bool Function OtherWeddingBusy() global native
; 0.14.0: the partner who will not come to the wedding (stood up at the altar); None = both come / not decided yet
Actor Function GetAltarRunner(Actor a, Actor b) global native
; 0.14.0: the player got an invitation letter - the wedding waits for them (up to 3 more days)
bool Function SetWeddingInvite(Actor a, Actor b) global native
; 0.14.0: someone objected (attacked the bride or groom) during the ceremony - the wedding is off for now, a new date follows
bool Function CallOffWedding(Actor a, Actor b) global native
; 0.14.2: sleep-over candidates - married couples who do not live together: visitorRef, hostRef, home triples
int[] Function GetSleepOverPairs() global native
; 0.22.0 households (bridge 13): married couples who live apart move in together - moverRef, hostRef, homeLocId, sameTown quads
; (widowed pairs stay while the surviving mover lives: the widow keeps the house; sameTown 0 = different towns, no household)
int[] Function GetHouseholds() global native
; the faction owning the home cell the actor stands in ("<X> House Faction"), None when not faction-owned or not in the home
Faction Function GetHomeFaction(Actor who, Location home) global native
; the nearest free bed (no owner of its own) in the actor's loaded cell of the home, a bed before a bedroll; None = none free
ObjectReference Function FindFreeBed(Actor who, Location home) global native
; kind 0 = the home town's centre, 1 = its inn (pick chooses among several), 2 = the home's own marker; None = no such spot
ObjectReference Function FindTownSpot(Location home, int kind, int pick) global native
; a line on the News page
Function AddNewsLine(string text) global native
; 0.22.2 (bridge 14): generic-named (PoolExclusions.json) or a guard - never picked for a scene
bool Function IsSceneExcluded(Actor a) global native
; 0.23.0 (bridge 15): family chats - loaded relatives near the player as refA, refB, code triples (1 parent -> child, 2 siblings,
; 3 grandparent -> grandchild, 4 other kin), and one family part for two of them (rel 1 = a is b's parent when no record says so)
int[] Function GetFamilyPairs(float maxDist) global native
int[] Function PlanFamily(Actor a, Actor b, int rel) global native
; 0.24.0 (bridge 16): baby stages - the "where is the child" reasons of OStimNPCsReforged_ChildAway.json (index from 0; -1 / "" without
; the file) and the Couples page notes ("baby Ada (carried)", "Ada away: <place>", "child Ada at home"), rebuilt once a game hour
int Function ChildAwayReasonCount() global native
int Function PickChildAwayReason(Actor mother, Actor father) global native	; a place that fits a parent's race and home (father may be None)
int Function PickChildAwayReasonFor(Actor mother, Actor father, int classMother, int classFather) global native	; 0.25.0 (bridge 17): + each parent's M.A.R.A.S social class (-1 unknown) for the estate / kindly widow places
string Function ChildAwayPlace(int reason) global native
; 0.25.1 (bridge 18): saves keep the place by its id, so a reordered or once-missing JSON can't move an away child
string Function ChildAwayReasonId(int reason) global native
string Function ChildAwayPlaceById(string id) global native
int Function ChildAwayDialogueRank(string id) global native	; ONPCR_ChildAway rank the answers expect (0 = no line)
Function ClearBabyNotes() global native
Function AddBabyNote(Actor mother, Actor father, string text) global native

; 0.17.0 (DLL 0.17.0+): "Import from other mods" - ONpcMain reads Lover's Ledger and hands pairs over
; true when the pair became (or already was upgraded to) a dating couple
bool Function ImportPair(ActorBase a, ActorBase b) global native
; adds the "Imported N dating couple(s) from <source>" line to the News page
Function FinishOtherImport(int count, string source) global native
; the placed actor for a base NPC the ladder knows (simulation pool, or seen in a scene), or None
Actor Function GetActorForBase(ActorBase b) global native
