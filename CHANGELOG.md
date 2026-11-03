# Changelog

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
