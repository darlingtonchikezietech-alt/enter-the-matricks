# Toolchain (locked in T-0005)

The versions this project builds with. Change them only through a task that rebuilds and re-tests.

## Locked versions

| Component | Version | Verified how |
|---|---|---|
| Unreal Engine | **5.8.4** (changelist 58902052, Epic launcher build) | Editor target compiled |
| `EngineAssociation` in `ETM.uproject` | `5.8` | Editor target compiled |
| XR plugin | Engine **OpenXR** plugin 1.0 + **XRBase** 1.0 (both ship with 5.8.4) | Compiled only |
| Meta XR plugin | **Not used** | See "XR plugin choice" |
| Visual Studio | Community 2026, MSVC 14.50.35717 | Editor target compiled |
| Windows SDK | 10.0.22621.0 | Editor target compiled |
| .NET (UnrealBuildTool) | 10.0, bundled with the engine | Editor target compiled |
| Android platform | android-36 | From the engine's `Android_SDK.json`; **not built yet** |
| Android build-tools | 36.0.0 | Same; not built yet |
| Android NDK | r27c (27.2.12479018); engine accepts r27c to r29 | Same; not built yet |
| CMake (Android) | 3.22.1 | Same; not built yet |
| JDK | 21 (Epic's documented version; engine needs 17 or newer) | **Not built yet** |
| `MinSDKVersion` / `TargetSDKVersion` | 32 / 32 | From Epic's 5.8 VR template; not built yet |
| Git / Git LFS | 2.56.0 / 3.8.1 | In use |
| Python (tracker tools) | 3.13 | In use |

Where the engine and Epic's web documentation disagree on Android versions, follow `Engine/Config/Android/Android_SDK.json` in the installed engine.

## XR plugin choice

The project uses the engine's built-in OpenXR plugin, with no Meta XR plugin. The bible asks for OpenXR with platform features behind adapters, and the scaffold needs only head tracking, two controllers and recentering, all of which are core OpenXR 1.0.

This keeps the P-15 constraint: nothing here depends on a Horizon OS feature newer than v2.13. That is a statement about what the code uses. It has **not** been confirmed on a Quest 2; T-0006 does that.

If a later task needs a Meta-only feature (for example application spacewarp or fixed foveated rendering control), add the Meta XR plugin in its own task and check its minimum Horizon OS version against P-15 first.

## What T-0005 verified

| Check | Result |
|---|---|
| `ETMEditor Win64 Development` compiles | **Yes**, zero errors, zero warnings (17 Nov 2026) |
| Project loads in the editor (headless commandlet, `UnrealEditor-Cmd -run=DumpHiddenCategories`) | **Yes**, `ETM` module loads, 0 errors, 0 warnings. The OpenXR loader logged one runtime message: the installed Oculus PC runtime supports OpenXR 1.0 and declined 1.1 |
| Editor opened interactively, VR Preview | Not tried |
| Android / Quest package | Not tried: this engine install has no Android target yet |
| Runs on a Quest 2 | Not tried (T-0006) |

Compile fixes made to get there:

- `ETM.Build.cs`: added the `XRBase` module. In 5.8 `HeadMountedDisplayFunctionLibrary.h` lives in the XRBase plugin, not the `HeadMountedDisplay` module.
- `ETM.uproject`: enabled the `XRBase` plugin and set `EngineAssociation` to `5.8`.

No file outside T-0005's paths needed changing.

## Config checked against 5.8.4

Every key in `Config/DefaultEngine.ini` was checked against the engine's `RendererSettings.h` and `AndroidRuntimeSettings.h`.

- Replaced `r.DefaultFeature.AntiAliasing`, which is no longer a project setting, with `r.AntiAliasingMethod=3` (MSAA).
- Added the exclusions for Nanite, virtual shadow maps and mesh distance fields.
- Added the Quest packaging keys from Epic's 5.8 VR template: `bPackageForOpenXRImmersive`, arm64 only, data inside the APK, supported devices, SDK levels.
- `Config/DefaultGame.ini`: `ProjectID` was not a valid GUID; replaced with a generated one.
- The 72 Hz refresh rate is not a config key in the engine's OpenXR plugin. Set and confirm it on the device in T-0006.

## Build

Close the Unreal Editor first.

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ETMEditor Win64 Development "-project=$PWD\ETM.uproject" -waitmutex -NoHotReload
```

## Machine setup notes

- **Smart App Control must be off** on Windows 11, or UnrealBuildTool cannot load the rules DLL it compiles.
- Visual Studio 2022 is not enough for UE 5.8; use 2026.
- To package for Quest, add the Android target in the Epic Games Launcher: Library › 5.8 › Options › Target Platforms › Android. Then run `Engine\Extras\Android\SetupAndroid.bat android-36 36.0.0 3.22.1 27.2.12479018`.
- Android Studio 2026.2 bundles Java 25, which is untested with this engine. If Gradle fails, install JDK 21 and point `JAVA_HOME` at it.
- Installing on a Quest 2 needs Developer Mode, which only the headset's owner account can enable.
- The git hooks call `python3`. On Windows, make sure `python3` runs a real Python and not the Microsoft Store alias.
- On first open the editor may add an `AndroidFileServer` block with a per-machine token to `Config/DefaultEngine.ini` and create `Config/DefaultEditor.ini`. Do not commit either.
