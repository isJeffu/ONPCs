# Changelog

## 0.1.0-alpha (unreleased)
First public build of the base. Compared with OStim NPCs 2.1.0:
- The scan, partner choice, arousal and scene bookkeeping run in an SKSE plugin instead of Papyrus.
- Only known couples (spouses, fiancees, courting) are paired; never strangers or relatives; a past scene alone never makes a couple.
- Arousal states from OSL Aroused / SexLab Aroused or a built-in desire; a short break per couple after each scene.
- Group scenes for NPCs with two or more partners nearby, with all members walking to the bed together.
- OStim's own C++ thread API to see running scenes; stuck scene slots are freed after a load.
- Settings on an SKSE Menu Framework page (no MCM); a scene log.
- Fixed from OStim NPCs: the third-actor "still loaded" check was inverted; "stop when seen" no longer loops inside OStim's start event.
