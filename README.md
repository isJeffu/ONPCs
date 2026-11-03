# ONPCs - NPC Couples for OStim

A GPL-3.0 successor to **[OStim NPCs - NPC Sex Lives Improved](https://www.nexusmods.com/skyrimspecialedition/mods/82888)** by **Aietos**,
rebuilt around an SKSE plugin. This is **not** an official update of Aietos's mod.

> **Status: 0.1.0-alpha.** Expect bugs. Use a new game or a test save.

## What it does
At night, couples who already belong together - spouses, fiancees and courting couples from the game's own records - may have
an OStim scene when they are near you, in their own bed if one is free. No random strangers, no relatives.

- **Arousal decides when.** Reads OSL Aroused or SexLab Aroused if installed; otherwise a built-in desire that grows with the
  days since a couple's last fulfilling scene. States: Indifferent, Content, Interested, Yearning, Aching.
- **Group scenes:** someone with two or more partners nearby (an orc chief and his wives) can gather them for one scene; they
  walk to the bed together.
- Furniture choice, walking to the bed, followers and enemy camps, "stop when seen", Beeing Female period skip and the
  match-maker spells, as in OStim NPCs.
- A short break after each scene; a frustrated couple may try again sooner than a fulfilled one.
- A scene log (`ONPCs_SceneLog.log`, next to the SKSE logs) records how each scene went.

## Requirements
- Skyrim SE / AE, [SKSE](https://skse.silverlock.org/), Address Library for SKSE Plugins
- OStim Standalone 7.5.1 or newer
- PapyrusUtil, JContainers
- SKSE Menu Framework (settings page; without it the INI still works)

Optional: OSL Aroused or SexLab Aroused, Lover's Ledger, Relations Finder, Beeing Female.

## Install
1. **Uninstall OStim NPCs** - ONPCs replaces it (it uses the same script names).
2. Install the release zip with your mod manager. The plugin is ESL-flagged.

Settings: F3 (SKSE Menu Framework) > ONPCs, or `Data\SKSE\Plugins\ONPCs.ini`.

## Planned add-on modules
Relationships, Daily Life, Weddings & Letters, Families, Conversations - separate plugins that plug into this base. The base
switches their features on only when their plugin is installed.

## Repository layout
| Folder | What |
|---|---|
| `dll/` | the SKSE plugin `ONPCs.dll` (C++23, CommonLibSSE-NG, CMake + vcpkg) |
| `papyrus/Source/Scripts/` | Papyrus sources (compile against OStim, PapyrusUtil, JContainers, SKSE, Beeing Female sources) |
| `data/` | `ONPCs.esp` and its SEQ file |
| `third-party/` | source of the furniture guard DLL shipped with OStim NPCs 2.1.0 |

### Building the DLL
1. `git clone --recurse-submodules` (CommonLibSSE-NG is a submodule in `dll/extern/CommonLibSSE-NG`).
2. Install vcpkg and set `VCPKG_ROOT`.
3. With Visual Studio 2026 (or edit the generator in `dll/CMakePresets.json`):
   `cmake --preset skyrim` then `cmake --build --preset skyrim-release` in `dll/`.
   The DLL and the default INI are copied to `build/SKSE/Plugins/`.

## Licence and credits
GPL-3.0-or-later (see `LICENSE`).
- **Aietos** - OStim NPCs, which this is forked from.
- The furniture guard included with OStim NPCs 2.1.0 (source in `third-party/`).
- The OStim team - OStim Standalone and its API (`dll/include/ThirdParty/OStimNG`, GPL-3.0).
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) - GPL-3.0 with modding exceptions.
- SKSE Menu Framework (Thiago099, Ege Yilmaz) - MIT; SimpleIni (Brodie Thiesfield) - MIT.
- tetherball88 - Lover's Ledger and Relations Finder, called by name only (no code included).

See `THIRD-PARTY-NOTICES.md`. Made with AI assistance.
