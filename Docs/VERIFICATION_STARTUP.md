# Cross-PC startup verification — 28–29 October 2026

Verified with UE 5.8.3 on Windows 11, Intel i5-12450H / Intel UHD Graphics.

## Causes and repairs

- Git contains source and LFS assets, but excludes `Artifacts`, `Binaries`, and
  `Intermediate`. The old `Start_Game.cmd` required an existing local package.
  The launcher now uses a current package or incrementally builds and runs the
  source project. Package receipts detect changed or deleted inputs.
- Editor/build scripts used the original PC's engine path and registration GUID.
  Engine discovery now supports the project's association, Epic Launcher records,
  `PERSONA_ENGINE_ROOT`, and an explicit parameter.
- Generated solutions contained absolute paths. They are now generated locally;
  the build targets let Unreal choose a compatible installed Windows SDK.
- This PC lacked C++ build tools and a Windows SDK. Installed Visual Studio Build
  Tools 2022 (MSVC 14.44.35229) and Windows SDK 10.0.22621.0 with user approval.
- UE required Visual C++ runtime 14.50.35719.0; the older runtime produced a modal
  startup warning that blocked a hidden rendered test. Installed UE's bundled
  runtime, included prerequisites in packaging, and made hidden local tests
  unattended.
- Intel UHD uses Unreal's D3D12 SM5 fallback on this machine. Windows packages now
  cook SM5 alongside SM6. The packaged log confirms SM5 was selected successfully.
- Windows PowerShell 5.1 and PowerShell 7 sorted fingerprint inputs differently.
  Fingerprints now use ordinal ordering and .NET SHA-256 without relying on
  `Get-FileHash` module availability in a nested PowerShell session.

## Results

- All 557 tracked LFS objects were hydrated; no asset regeneration was necessary.
- `Scripts/TestLaunch.ps1`: 26 checks passed, including both PowerShell hosts,
  paths with spaces, missing/LFS assets, failed builds, copied packages, and stale
  package detection after changes or deletions.
- Editor and Game Development builds passed.
- Final `Scripts/Package.ps1` completed successfully; cook reported zero errors
  and zero warnings. Log: `.work/startup-package-final.log`.
- Eight live startup/session checks passed in editor-game mode, and eight passed
  in the standalone package: menu, create sector, LAN search, join, client leave,
  host save/leave, continue saved sector, and close restored sector.
  Reports: `Artifacts/Validation/startup-runtime.json` and
  `Artifacts/Validation/startup-packaged.json`.
- Invoked the root `Start_Game.cmd` with no arguments. It returned exit code 0
  and launched `Artifacts/Windows/PersonaTrial/Binaries/Win64/PersonaTrial.exe`
  using the Local profile. The current package receipt matches both PowerShell
  versions.
- In the actual game window, selected the new-sector action and cooperative
  mode. The hub loaded, HUD appeared, and the camera responded to mouse input.
  The final game log contains no error/fatal or outdated-runtime entries.

The generated package is local and remains excluded from Git. On another PC,
either copy the whole `Artifacts/Windows` folder or follow `START_HERE.md` to
build from the checkout. These checks cover local networking, not Steam/VOIP.
