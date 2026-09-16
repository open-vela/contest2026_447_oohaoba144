# Official SF32LB52-DevKit-LCD baseline build

## Result

- Evidence state: `build-verified`
- Date: 2026-09-13 (Asia/Shanghai)
- Source workspace: `/home/oohb144/workspace/openvela-contest`
- Team repository: `contest2026_447_oohaoba144`
- Team repository commit: `bd7b45c2db3d1467489ab46df77e11200d1e8b70`
- Board configuration: `vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh`
- CMake generator: Ninja
- Artifact: `cmake_out/sf32lb52_devkit_lcd/nuttx.bin`
- Artifact size: `1542848` bytes
- Artifact SHA256 for this build instance: `d401791ee012ac5f8a080edf0256b851fa2abc039ae90707641a9a6bf69f3b02`

## Reproduction commands

Run from PowerShell; every Linux command is routed through WSL:

```powershell
wsl.exe -d Ubuntu -- bash -lc 'cd /home/oohb144/workspace/openvela-contest && source build/envsetup.sh && cmake -B /home/oohb144/workspace/openvela-contest/cmake_out/sf32lb52_devkit_lcd -S /home/oohb144/workspace/openvela-contest/nuttx -GNinja -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh -DCUSTOM_MODULE_PATH=/home/oohb144/workspace/openvela-contest/build/cmake -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"'
wsl.exe -d Ubuntu -- bash -lc 'cd /home/oohb144/workspace/openvela-contest && source build/envsetup.sh && cmake --build /home/oohb144/workspace/openvela-contest/cmake_out/sf32lb52_devkit_lcd --parallel 8'
wsl.exe -d Ubuntu -- bash -lc 'cd /home/oohb144/workspace/openvela-contest && sha256sum cmake_out/sf32lb52_devkit_lcd/nuttx.bin'
```

## Observed output

- Configure completed and selected `sf32lb52_devkit_lcd / nsh`.
- Full build completed at Ninja target `2237/2237`.
- Incremental build completed with exit code `0` and rebuilt 14 targets.
- Linker emitted existing warnings about RWX load segments and discarded build-id sections; no build error occurred.
- Memory report at final link: flash `1542848 B / 16 MB` (`9.20%`), SRAM `121120 B / 512 KB` (`23.10%`), PSRAM `0 B / 8 MB`.
- The binary hash is build-instance-specific: the upstream `lib_utsname.c` embeds `__DATE__` and `__TIME__`, and the generated build rule recompiles it during each build. Recompute SHA256 after every fresh build.

## Boundary

This is a host build result only. No physical board was connected, flashed, or observed; hardware evidence remains pending.
