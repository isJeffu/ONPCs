Scriptname ONPCR_Settings Hidden
{OStim NPCs Reforged - native settings bridge. Implemented by ONPCs.dll.
 Values live in Data\SKSE\Plugins\ONPCs.ini and in the F3 menu (SKSE Menu Framework).
 The DLL fires the mod event "ONPCR_SettingsChanged" (strArg = the changed key, "*" = everything) whenever the player changes a value.}

; keys: StrayChance, StrayRepeatMult, HistoryWeight, StrangerBaseScore (floats); any int key also works
float Function GetFloat(string key, float default) global native

; keys: Disabled, AllowActiveFollowers, AllowPlayersPartners, ActiveFollowersEngageOtherNPCs, AllowElderRace, AllowCommonEnemies, StopWhenFound, TravelToLocation, EnemiesTravelToLocation, EnableFurniture, FurnitureOnlyBeds, EnemiesIgnoreScenesStartInRules, FollowersNoScenesDungeons, NoScenesInTowns, NoScenesInGuilds, NoScenesInInns, UseRelationsFinderFamily, UseLoversLedger, FurnitureGuardDebug
bool Function GetBool(string key, bool default) global native

; re-read the INI from disk
Function Reload() global native

; 2 = this bridge version (GetInt + per-key change events); use SKSE.GetPluginVersion("ONPCs") to test presence first
int Function GetVersion() global native

; keys: ScenesStartIn, MinRelation, MaxScenes, MaxFollowerScenesPerNight, MaxEnemyScenesPerNight, MinNight, MaxNight, ScanFreq, WeightMF, WeightFF, WeightMM, LetterMinRank, CandidateLetterStage, Notifications, PlayerPartnerRomances
int Function GetInt(string key, int default) global native
