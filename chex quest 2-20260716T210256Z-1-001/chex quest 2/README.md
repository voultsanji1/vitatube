# Chex Quest 2 — PS Vita port

Homebrew PS Vita conversion of **Chex Quest 2** built on **[doomgeneric](https://github.com/ozkl/doomgeneric)** (Chocolate Doom–style codebase). Runs the PWAD **`CHEX2.WAD`** on top of the Chex **`CHEX.WAD`** IWAD, applies console-friendly controls, OPL-style music playback, packaged as a Vita **`.vpk`**.

---

## Controls

| Input | Action |
|--------|--------|
| Left stick | Move forward / back, strafe |
| Right stick | Turn left / right |
| D‑Pad ↑ / ↓ | Up / Down (menus, etc.) |
| D‑Pad ← / → | Change weapon quickly; hold to repeat through weapons |
| Cross (X) | Use / activate |
| Square | Fire |
| Circle | Alt (combinable with sticks for sideways strafe) |
| Triangle | Automap |
| R | Fire |
| L | Run (Shift) |
| Start | Menu (Esc — save/load from menu) |
| Select | Enter / confirm |
| Top touch strip | Weapons 1–7 (one change per tap; lift finger between slots) |

---

## Files you must provide

WAD contents are **not** in this repository (copyright). Supply your own legally obtained files.

| Role | Typical filename | Purpose |
|------|-------------------|---------|
| IWAD | `CHEX.WAD` | Chex Quest 1 IWAD (**required** — Chex Quest 2 is intended to load on top of this, not as a standalone IWAD; see [Doom Wiki — CHEX2.WAD](https://doomwiki.org/wiki/CHEX2.WAD)) |
| PWAD | `CHEX2.WAD` | Episodes/maps/data for Chex Quest 2 |

Place them **on Vita** under:

```
ux0:/data/chexquest2/CHEX.WAD
ux0:/data/chexquest2/CHEX2.WAD
```

The port also probes a few **`chex.wad` / `chex2.wad`** style spellings — see **`doomgeneric_vita.c`** if you rename files.

**Package:** Install the shipped **`ChexQuest2Vita.vpk`** (e.g. with VitaShell) on a Vita with a suitable **homebrew** setup (Henkaku / similar). Unsigned retail Vitas cannot install `.vpk` homebrew.

**Debug:** `ux0:/data/chexquest2/debug.log`.

---

## Known limitations & bugs

- **Engine:** Behaviour matches **doomgeneric** limits (vanilla Doom feature set plus what this codebase patches). Compatibility with odd PWAD lumps is “best effort,” not identical to desktop source ports such as PrBoom/ZDoom.
- **PWAD patches:** Extra patches (sprites, DEH loading, etc.) were added mainly for PWAD-heavy ports; uncommon lump combinations can still theoretically **crash**, **silent-fail sprites**, or show **graphic glitches** on edge cases nobody hit on Vita yet.
- **Missing `CHEX2.WAD`:** If only the IWAD is found, you get **Chex Quest 1–style** play; CQ2 content needs **`CHEX2.WAD`** (by design in **`main`**).
- **Music / audio:** Tracks use the built-in **OPL** path; fidelity and quirks may differ from Windows desktop releases.
- **Performance:** Targets ~320×200 software rendering upscaled on Vita screen; slowdowns possible in hectic scenes versus a gaming PC.

If something fails, attach **`debug.log`** and describe whether you installed `CHEX.WAD` **and** `CHEX2.WAD` exactly on `ux0:` as above.

---

## Credits / license

Project code is shipped as-is for educational and **homebrew** use. **Chex Quest** / **Doom–family** trademarks and WAD data remain with their respective owners — do not distribute copyrighted IWAD/PWAD with this repo.  
Engine: **[doomgeneric](https://github.com/ozkl/doomgeneric)** (ozkl), **VitaSDK** maintainers.
