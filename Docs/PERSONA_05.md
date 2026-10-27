# PERSONA 0.5 — fast, grounded FPS

Movement uses `UPersonaMovement`. Sprint and ADS intent travel in saved network
moves and replay after correction. The server checks health, transit, crouch,
equipment and stamina. Initial speeds are 600 walk, 980 sprint, 280 crouch,
300 ADS and 180 crouched ADS (cm/s). Acceleration is 4200, braking 5200,
friction 6 and air control 0.55. No slide or speed-building jump mechanic.

Camera presentation is separate from raw controller aim. Crouch compensates the
capsule transition and settles in about 0.2 seconds; standing still uses Unreal's
ceiling checks. Bob follows distance travelled, with restrained acceleration,
turn and landing effects. `persona.CameraMotion 0` disables cosmetic motion;
the default is 1, with a supported range of 0–2. World FOV is 100/105/85 for
normal/sprint/ADS. Owner arms and weapon use Unreal's first-person projection.

The shotgun retains manual pumping, safety, per-shell loading and chamber state.
Pump time is 0.45 s, insertion 0.6 s, and firing out of sprint raises the weapon
for 0.14 s. `DA_Shotgun` owns the sight transform, barrel position, recoil vectors
and three editable kick/recovery curves. Sight position comes from the authored
front bead (13.382 cm above the model origin). Authoritative barrel obstruction
uses a stable pose; cosmetic recoil cannot move the server's clearance test.

Props use CCD with bounded 120 Hz substeps, up to eight per frame. Release sweeps
the scaled mesh bounds and tests the final pose, including alternate positions
near the holder. Failure keeps the item held and displays a short message.
Drops and slot releases share the same path. Restored embedded items search a
bounded area before physics resumes. Throw strength and inherited velocity stay
server authoritative.
An authoritative sweep after physics catches any remaining thin-surface tunnelling
between simulated poses and resolves contact velocity relative to the surface.

`Scripts/hinges_phase5.json` defines each Blender pivot's axis and signed opening
angle. Chest lids lift backward, oven doors fold downward, paired doors oppose
one another, and gates slide. The exporter and incremental metadata updater
share these definitions. Runtime animation eases quaternion interpolation from
the current progress, including reversal. The original `.blend` is not saved.

Tab closes loot first and consumes the key, including when a loot button owns
keyboard focus. Otherwise it opens the session panel. E and Escape still close
loot. Contextual object/key prompts and repeated control tips are removed;
crosshair, vitals, hotbar, objectives, contents and short failure messages remain.

`DA_Combat` controls NPC movement, attacks and regional reaction durations.
Sight, sound, damage and shared last-known positions feed a tactical state
machine. Hidden players break exact tracking. Candidate navigation goals score
cover, approach angle, travel distance, allied spacing and recent routes, with
seeded variation and commitment periods. Patrol/sneak/combat/sprint speeds start
at 300/200/650/900. Two committed attackers per target are allowed; the rest
reposition. Six mixed-height cover groups leave three connected arena lanes.

Melee has windup, a swept active window and recovery. Normal attacks deal 35;
the longer heavy telegraph deals 55. Strikes respect walls and apply damage only
once. Attack identity and server start time drive retargeted animation playback.
Shotgun pellets aggregate by body region; the most damaging region selects the
reaction. Legs briefly impair movement, arms interrupt attacks, and head/torso
hits stagger/recoil. An immunity interval bounds repeated impairments. Player
damage produces a directional edge flash, camera impulse and impact sound.
NPCs use `M_Observer` with a restrained shadow floor calibrated to the arena's
fixed exposure, so attack poses remain visible against dark walls. Player and
first-person arm materials retain their existing treatment.

Wave counts use `ceil(base × (1 + 0.45 × (players − 1)))`, with bases 5/10/20/30.
The participant count is captured at departure and remains fixed after deaths
or disconnects. At most 30 enemies are alive; the rest arrive as reinforcements.
Crates supply 48 rounds plus 24 per additional participant, with enough shotguns.
Corpses have a seeded 25% chance of one or two rounds. Existing refusal penalties,
rewards and extraction rules remain in effect.

Save version 4 adds encounter scaling, pending reinforcements, tactical random
state and remaining impairment timers. Version 3 saves get a `.pre05` backup;
existing wave populations, inventories, corpses and rewards are preserved.
Interrupted attacks resume with a recovery cooldown and cannot replay damage.
The renderer forces morph-buffer updates to avoid a UE 5.8 assertion when frozen
corpse poses and shoulder morphs become visible immediately after a cold load.

Regenerate tuning and hinge metadata with `Scripts/UpdateOverhaul.ps1`; add
`-Retarget` to rebuild combat clips. Full Blender regeneration uses
`Scripts/ImportPhase4.ps1`, which also applies this tuning. Bake changed geometry
with `Scripts/BakeLighting.ps1 -Quality Production`, then package with
`Scripts/Package.ps1`. Verification and remaining limits are in `VERIFICATION_05.md`.
