# Changelog

## 0.1.5-alpha
- Faithful couples: a married or engaged NPC only has scenes with their own partner. A game record that says they are also courting someone else no longer makes a pair. The same goes for groups: a hub's spouse and an outside courting partner never share a group scene. Cheating comes later with the relationships module.

## 0.1.4-alpha
- No scene in the open: when a couple (or group) that wants privacy finds no private bed, there is no scene, whatever "Scenes start in" says; a bed taken at the last moment no longer falls back to a scene where they stand.
- "Scenes end when found" checks every member of a group scene, not just the first.
- A scene slot whose OStim scene ended without the end event is freed during play too (after two scans in a row), not only after a load.
- Muted ("keep quiet") NPCs are always unmuted when their slot is freed.
- An aborted scene start cancels its OStim scene builder.
- OStim's thread API is now a requirement (it was already needed in practice: 7.5.1+).
- Group walks: the walk starts even for an NPC the script engine had not met yet; a failed walk is logged.
- Relations Finder family exclusion stays a setting, on by default (also when the ini line is missing).

## 0.1.3-alpha
- **One scene a night** (new setting, on by default): each NPC has at most one scene a night. Off = after a short break, their arousal decides whether they go again (the 0.1.2 behaviour).
- NPCs a running quest is driving (a quest or alias package: waiting for you, escorting, a job) are no longer picked for scenes.
- A sleeping couple now picks a private bed too (their own first, nobody can see it) before anyone is woken; none private = they sleep on.
- Breaks after a scene (each NPC's, each couple's, the half-hour wait after giving up for lack of privacy, and the built-in desire's last fulfilling scene) are kept in the save, so a reload no longer lets a couple start again at once.

## 0.1.2-alpha
- **Privacy.** A couple doesn't start, or stops, when anyone awake (a child or an adult) has a clear line of sight to them. Someone only within earshot (setting, ~8.5 m, through walls) doesn't stop them: they keep quiet (muted) and relax when nobody is in earshot. They pick a free bed nobody can see, their own or their partner's first, out of earshot before having to keep quiet. A couple that gives up waits about half an hour. Followers and enemy camps don't mind, as in OStim NPCs. Your own followers and mannequins never count as watching. Settings: Couples want privacy, Earshot.
- Follower scenes are now **off by default** (Followers have scenes between themselves). When on, followers still only pair with their real partners (married, engaged, courting) - never with whoever is around, as the original did.

## 0.1.1-alpha
- Relations Finder and Lover's Ledger are required: without them (or without OStim 7.5.1+, PapyrusUtil or JContainers) no scenes start, the SKSE log names what is missing and a message says so after loading.
- README: every requirement links to its Nexus page.

## 0.1.0-alpha (3 Nov 2026)
First public build of the base. Compared with OStim NPCs 2.1.0:
- The scan, partner choice, arousal and scene bookkeeping run in an SKSE plugin instead of Papyrus.
- Only known couples (spouses, fiancees, courting) are paired; never strangers or relatives; a past scene alone never makes a couple.
- Arousal states from OSL Aroused / SexLab Aroused or a built-in desire; a short break per couple after each scene.
- Group scenes for NPCs with two or more partners nearby, with all members walking to the bed together.
- OStim's own C++ thread API to see running scenes; stuck scene slots are freed after a load.
- Settings on an SKSE Menu Framework page (no MCM); a scene log.
- Fixed from OStim NPCs: the third-actor "still loaded" check was inverted; "stop when seen" no longer loops inside OStim's start event.
