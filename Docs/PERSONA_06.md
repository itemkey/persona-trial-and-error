# PERSONA 0.6 — combat and individual storage compartments

Storage interaction retains the component hit by the camera ray. The server
validates both actor and stable part ID. Each door has its own replicated
inventory, capacity and opening state. Closing a loot window removes one viewer;
the last viewer closes that door. Invalid, disconnected and distant viewers are
removed on the server. The UI includes the compartment name. Automatic gates
retain director control.
Single-lid chests also accept a body hit because there is only one compartment;
multi-part cabinet bodies do not select a compartment.

Save format 5 adds compartment records and moving corpse body transforms and
velocities. Pre-upgrade saves are backed up as `.pre06`. Legacy storage records
move intact into the first compartment; they are never respawned or duplicated.
New trial supplies are distributed across compartments without changing totals.
Interrupted NPC traversal restores in falling mode instead of a custom movement
mode without its original navigation link.

The shotgun uses an 8 degree camera kick with 4 degrees remaining after recovery,
12 cm of weapon translation, 12 degrees of weapon rotation and a 0.55 second
recovery. ADS scales these to 75%. Per-frame recoil deltas preserve mouse input.
The shot's ballistic direction is sampled before recoil is applied. Pump and
shell-insertion durations follow the imported mechanical recordings.

`prepare_combat_audio.py` prepares the owner's AAC recordings as mono 48 kHz
PCM24 WAVs and records source hashes and processing in `combat_provenance.json`.
Four fire recordings are selected by shot sequence with adjacent repeats
excluded even after empty trigger pulls. The server transmits the chosen variant;
the owner does not replay its predicted shot. Human and environment impacts are
aggregated per discharge. Physical surface `Soft` selects the soft impact;
unknown surfaces use the hard impact. Near misses are evaluated against the
actual unobstructed pellet segments, once per other listener within 110 cm.

`configure_combat_update.py` imports those WAVs, updates weapon curves and hub
part identities, retargets directional death clips and bakes separate crouch,
stance transition and climb sequences onto the existing PERSONA skeleton.
The original Blender file is not modified. The native animation graph blends
the stance assets and adds bounded ground foot IK and ledge hand IK.

NPC traversal uses generated smart navigation links between validated support
surfaces up to 240 cm apart vertically. The server reserves the landing area,
sweeps the entire capsule path before and throughout the motion, and aborts on
obstructed or moving support. Occupied links are temporarily disabled so that
pathfinding chooses another edge. Link geometry is rebuilt when support moves. Height
disadvantages take priority over lateral cover selection.

`configure_climb_navigation.py` preserves 120 cm wide cover tops by using 10 cm
default navigation cells and 5 cm vertical cells. It rebuilds the existing maps'
navigation and verifies projection onto both 140 cm and 220 cm surfaces.

Death disables living physical-animation drives. Grounded standing deaths use
a short directional animation before physics; unsupported/crouched/climbing
deaths use physics immediately. The server simulates and sends intermediate
poses; clients interpolate them. Dead meshes detach from the character capsule
and stop using character network smoothing, including after the final pose.
A corpse freezes only after support contact and a 0.8 second window in which
every body remains within 2 cm and 3 degrees of its rest anchor, with bounded
linear and angular speeds. This tolerates solver jitter without freezing a
falling body. Physics bodies use bounded masses, damping,
collision separation, CCD and joint limits.

## Rebuild and checks

Use the project's resolved UE 5.8 installation. Python audio dependencies can be
installed into `.work/python_combat311` with the engine's Python 3.11:

```powershell
& "$engine/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" -m pip install --target .work/python_combat311 numpy soundfile imageio-ffmpeg
& "$engine/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" -X utf8 Scripts/prepare_combat_audio.py
./Scripts/Build.ps1
& "$engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" PersonaTrial.uproject -run=pythonscript -script=Scripts/configure_combat_update.py -unattended -nullrhi -CustomConfig=Local -nosteam
& "$engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" PersonaTrial.uproject -run=pythonscript -script=Scripts/configure_climb_navigation.py -unattended -nullrhi -CustomConfig=Local -nosteam
./Scripts/RunLocal.ps1 -Players 2 -Diagnostics -ResX 1280 -ResY 720
```

The focused black-box suites are `TestCombatCompartments.py`,
`TestCompartmentMigration.py`, `TestCombatPresentation.py`, `TestRecoilLimits.py`,
`TestNPCTraversal.py`, `TestTraversalEdges.py` and `TestRagdollCases.py`. Existing shotgun,
container-motion, trial-geometry and save-migration suites remain relevant.
Diagnostics include the selected part, each compartment's items, traversal
state/link count, intermediate corpse pose count and audio-event counters.
Video captures use the game's existing diagnostic recorder and
`EncodeGameplayCapture.py`. Reports are written under `Artifacts/Validation`.
