# The Dark Mod — Better VR

A VR version of [The Dark Mod](https://www.thedarkmod.com) 2.14, built on the official engine source.

**Status:** early. This branch ports the OpenXR rendering layer from
[fholger/thedarkmodvr](https://github.com/fholger/thedarkmodvr) (written for TDM 2.11) onto the 2.14 engine.
Head tracking and stereo rendering first; tracked hands and physical interaction come next.

## Using a build

1. Have a normal TDM 2.14 install.
2. Copy the files from a build (`TheDarkModVRx64.exe`, `openxr_loader.dll`, `glprogs_vr/`, ...) into the TDM folder.
   The VR exe uses its own config (`darkmod_vr.cfg`) and shader folder, so the normal game is unaffected.
3. Make sure your headset runtime (e.g. SteamVR) is set as the default OpenXR runtime, start it, then run `TheDarkModVRx64.exe`.

## Building

GitHub Actions builds the Windows exe on every push (see `.github/workflows/build.yml`).
Locally: Visual Studio 2022, plus the third-party libraries from TDM's SVN
(`ThirdParty/artefacts`, see `.github/README.md`), and copy `vr_thirdparty/*` into `ThirdParty/artefacts/`.

## License

Engine code: GPL v3 (see `LICENSE.txt`). Game assets belong to The Dark Mod team under CC BY-NC-SA 3.0.
