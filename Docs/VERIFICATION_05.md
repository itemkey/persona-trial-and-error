# PERSONA 0.5 — verification

Verified on 27–28 October 2026 with Unreal 5.8.3, Windows Development,
an Intel i7-13700 and RTX 3090. The playable package is `Artifacts/Windows`;
launch it through the repository's `Start_Game.cmd`.

## Build and assets

- Editor and Game builds succeeded: `.work/overhaul-build10.log` and
  `.work/overhaul-game10.log`.
- Full cook/package succeeded with no cook errors or warnings:
  `.work/overhaul-package-readability.log`. The final character material update
  was cooked, staged and archived successfully with zero cook errors/warnings
  in `.work/overhaul-package-final.log`. The executable includes the final
  physics safeguard from the builds above.
- Production lighting was rebuilt for the changed maps. The lighting commandlet
  completed with zero errors and 35 warnings concerning engine quality settings
  and actors whose meshes are assigned at runtime (`.work/overhaul-lighting.log`).
- Blueprint defaults, combat profile, recoil curves, sight alignment, retargeted
  attacks/hit/landing clips, cover geometry and hinge metadata were regenerated.
  All 21 moving parts passed pivot/direction validation.
- Python compilation and `git diff --check` pass. The original Blender source
  remains byte-identical: SHA-256
  `6a099a7ba51570a62a1f75c61fccf5371c0fe62e93d4f20c9d907401f1f41a37`.

## Packaged regression results

Four independent Windows processes use the local IP driver with 100 ms packet
lag and 2% loss. The host renders at 1920×1080; remote clients use NullRHI.
The ultrawide check renders at 2560×1080. Reports live in `Artifacts/Validation`.

| Suite | Passed | Report |
|---|---:|---|
| Manual shotgun loading, pump, safety, cancellation and attachment | 14/14 | `phase4-packaged-shotgun.json` |
| Host/remote movement, crouch, ADS, Tab focus, throws, damage, six hit regions and perception loss | 84/84 | `phase5-packaged-gameplay.json` |
| Ceiling clearance, steep throws/shots, sprint momentum, corpse Tab and ultrawide ADS | 14/14 | `phase5-edge-cases.json` |
| Eight hub container types: endpoints, reversal, intermediate poses and replication | 40/40 | `phase5-container-motion.json` |
| Authored hub interactions | 11/11 | `phase4-packaged-hub-interactions.json` |
| Concurrent/stale loot requests, stack conservation, range rejection and G gestures | 14/14 | `phase4-packaged-loot.json` |
| Four-player scaling, reinforcements, cold restart, corpses, extraction and purge | 29/29 | `phase5-scaling-4p.json` |
| Solo 5/10/20/30 waves, cold restart, corpses, extraction and purge | 26/26 | `phase5-scaling-1p.json` |
| Version 3 migration, exact backup, populations, identities, rejoin and wipe | 9/9 | `phase5-save-migration.json` |
| Corner throws, rejected releases, melee dodging, interruption and impairment expiry | 16/16 | `phase5-adversarial.json` |
| Moving lift cargo, opposing arena doors and sliding spawn gate | 14/14 | `phase5-trial-geometry.json` |
| Remote firing, late spectator, disconnect/rejoin and fixed run difficulty | 9/9 | `phase4-packaged-clients.json` |
| Solo ordinary-input combat: kills, incoming damage and tactical variety | 3/3 | `phase5-playtest-1p.json` |
| Four-player ordinary-input combat: kills, incoming damage and tactical variety | 3/3 | `phase5-playtest-4p.json` |

All 280 regression assertions and six combat-playtest assertions passed.
`phase5-summary.json` records the suite counts and packaged executable SHA-256.

The throw suite found a real ammo-box tunnelling failure despite CCD. The final
build adds an authoritative sweep between physics poses. Maximum-charge throws
of shotgun, ammunition and hammer then passed the two-centimetre wall tests at
30/60/120 FPS, plus steep floor/ceiling throws and release while sprinting.

The telemetry reader now permits file deletion sharing on Windows. Ordinary
Python file reads had occasionally forced Unreal's atomic snapshot replacement
to retry for 500 ms, contaminating attack timing. The affected suite was rerun
after fixing the reader.

Cold loading also exposed an Unreal morph-buffer assertion with frozen corpse
poses. The package enables `r.MorphTarget.ForceUpdate=1`; subsequent rendered
version 4 reload and version 3 migration both completed successfully.

Version 3's default-valued `Version` field may be absent from Unreal save tags.
The class keeps its legacy default of 3 while new saves explicitly write 4.
Migration was tested with an omitted version tag and a byte-exact `.pre05`
backup. Only diagnostic saves under `SaveGames/Validation` were manipulated.

## Performance sample

The four-player lifecycle uses diagnostic damage to reach the large populations;
it is distinct from the ordinary-input combat exercise. The camera was inside
the arena for these samples. Twelve samples over three seconds per population,
60 FPS cap, one rendered host plus three headless clients on the same machine:

| Population | Median game thread | Median render thread | Median GPU | Median frame |
|---|---:|---:|---:|---:|
| 30 living NPCs plus 83 accumulated corpses | 8.43 ms | 1.61 ms | 5.16 ms | 16.67 ms |
| 154 corpses after all four waves | 9.38 ms | 1.71 ms | 5.53 ms | 16.67 ms |

This is a short local sample, not a sustained performance guarantee for other
hardware. Screenshot/video capture adds overhead and is excluded from this table.
The final solo build also passed the same population profile: median game thread
5.38 ms / GPU 3.90 ms with 30 living NPCs and 35 corpses; 4.36 ms / 3.88 ms with
all 65 corpses.

## Rendered combat

`PlaytestOverhaul.py` drives ordinary movement, aiming, firing, manual pumping
and shell loading. It does not refill health or inject damage during combat.
Setup equipment matches the baseline supply allocation: 40 carried rounds in
solo (the three-slot inventory limit), or 30 rounds per player in a group of
four. Crate stock is not consumed by the setup fixture. The driver refreshes
aim immediately before firing to avoid artificial tracking delay from separate
diagnostic commands.

The final solo run killed seven enemies, completed wave one and ended in death
during wave two after 86.89 seconds. Ten tactical states were observed. Earlier
solo runs showed both successful first-wave kiting and rapid defeat; the first
90-second run without damage is not treated as a complete combat acceptance run.
The final four-player run killed eleven enemies in 89.36 seconds, included
participant deaths and exhibited nine tactical states. Samples stayed within
the two-committed-attacks-per-target limit. Separate targeted tests verify
normal/heavy damage, windup dodging, wall rejection and arm interruption.

Twenty-second excerpts are saved as `Artifacts/Previews/PERSONA_05_combat_1p.mp4`
and `PERSONA_05_combat_4p.mp4`. The diagnostic recorder captures roughly 8–12
images per second; the encoded 30 FPS video repeats those frames at their
recorded timestamps. It is visual evidence, not a measure of gameplay frame rate.
Occasional named screenshots replace a capture frame; the encoder preserves
the gap rather than shifting subsequent frames. `-CaptureAudio` on the packaged
test launcher overrides background muting for that process only; both recordings
contain verified nonzero game audio. Normal launch settings are unchanged.

Rendered review prompted an additional material correction: NPCs retain the
graphic silhouette style but have a calibrated dark-grey floor in shadows,
making attack poses distinguishable against unlit walls. Map lightmaps remain
valid because this changes only dynamic character materials.

## Evidence and reproduction

Rendered PNG evidence in `Artifacts/Previews` includes `PERSONA_05_ads`,
`PERSONA_05_ads_ultrawide`, `PERSONA_05_damage`, `PERSONA_05_HUB_Chest_01`
and `PERSONA_05_HUB_Stove_Oven`. The sights remain on the center ray with the
receiver below it. The chest lifts backward and the oven folds outward/down.

```powershell
Scripts/RunPackagedLocal.ps1 -Players 4 -Diagnostics -RenderHost -NetworkEmulation
$env:PERSONA_PACKAGED='1'
$env:PERSONA_DIAGNOSTICS="$PWD/Artifacts/Windows/PersonaTrial/.work/Diagnostics"
$python='D:/save_artapps/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
& $python Scripts/TestShotgun.py
& $python Scripts/TestOverhaul.py --players 4 --report phase5-packaged-gameplay
& $python Scripts/TestOverhaulEdges.py
& $python Scripts/TestContainerMotion.py
& $python Scripts/TestSurvivalScaling.py --players 4 --cold
& $python Scripts/TestOverhaulMigration.py --players 4
Scripts/StopLocal.ps1
```

Each suite describes its fixture prerequisites. A fresh managed hub starts an
isolated validation sector. `StopLocal.ps1` stops only registered test processes.
The asset regeneration workflow and editable tuning are documented in
[PERSONA_05.md](PERSONA_05.md).

Rendered automated input exercises can verify presentation and combat behavior;
they do not replace subjective playtesting by a person. Steam networking between
separate physical PCs and long-duration soak performance are not verified here.
