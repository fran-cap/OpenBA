# Source SVGs of ext.sharp_ui's magnified dialog sheet

Every file here is third-party art under its set's own licence (the file next to the set's folder), or
ours (`own/`, written by `tools/sharp_sprites/bake.py`). `THIRD_PARTY_NOTICES.md` at the root carries the
pinned versions and the CC BY attribution; it ships with every release package.

| Folder | Set | Licence | Licence file | Pinned |
|---|---|---|---|---|
| `phosphor/` | Phosphor Icons (Helena Zhang, Tobias Fried) | MIT | `phosphor/LICENSE` | `phosphor-icons/core` `2b75f3ad12b420c9504ef05df8d2564a28f8500e` |
| `tabler/` | Tabler Icons (Paweł Kuna) | MIT | `tabler/LICENSE` | `tabler/tabler-icons` `ed177a085b652fe04ec4f37ced867c94621e7840` |
| `lucide/` | Lucide (Lucide Contributors) | ISC | `lucide/LICENSE` | `lucide-icons/lucide` `1980e3d59b0fa1a2aeb566d5902b25ee98204001` |
| `bootstrap-icons/` | Bootstrap Icons (The Bootstrap Authors) | MIT | `bootstrap-icons/LICENSE` | npm `bootstrap-icons@1.13.2` |
| `game-icons/` | game-icons.net: `lorc/` by Lorc, `delapouite/` by Delapouite | CC BY 3.0 | `game-icons/license.txt` | `game-icons/icons` `82d948812bfe3f269ef8f731dcdb07b08160edc4` |
| `own/` | ours: the OverviewTab eyelid, the ProductionTab spiral, the Adult / Child figure, the Options gears, the OptionsGraphics brush | GPL-3.0 (the project's) | `LICENSE` at the root | generated |

The files are copied unchanged from the pinned versions; the bake recolours, combines, cuts and mirrors
them in memory (`tools/sharp_sprites/bake.py`, `build_specs()`).
