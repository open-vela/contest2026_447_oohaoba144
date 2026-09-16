# Offline application build evidence — 2026-09-14

Evidence: host-tested + build-verified. No hardware operation this round.

- Board: SF32LB52-DevKit-LCD.
- Baseline config: vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh.
- Existing output reused: cmake_out/velaguard_core_protocol.
- Entry: CONFIG_INIT_ENTRYPOINT/CONFIG_INIT_ENTRYNAME="velaguard_main"; CONFIG_INIT_STACKSIZE=32768.
- Team option and cJSON enabled; CONFIG_LV_FONT_SIMSUN_16_CJK=y.
- Build epoch macro: 1789369083 (CMake configure timestamp); a reconfigure/build changes artifact identity.
- Team HEAD: bd7b45c2db3d1467489ab46df77e11200d1e8b70, detached + uncommitted source. HEAD alone does not reproduce this candidate; source SHA manifest is required.
- nuttx HEAD dd92bcf425738734d1b8aed09c2bd4dbe3f2e438.
- packages HEAD 927618869eee9129c3670cd54397ede523c57d64.
- vendor/sifli HEAD af6f365eaa04a674af0467aa1a803bc4c77691ba.
- Shared tracked source diffs empty. Original logs/a0 preserved. No commit/push/reset/delete.

Build command (from Windows):
```powershell
wsl.exe -d Ubuntu -- bash -lc 'cd /home/oohb144/workspace/openvela-contest && source build/envsetup.sh && cmake --build cmake_out/velaguard_core_protocol --parallel 8'
```
Uses existing environment; no sync/install/baseline rebuild. Apply only the independent output .config additions recorded above. Original configure invocation is in core_protocol.md.

Final build log: cmake_out/velaguard_core_protocol/offline_app_build_release.log.
First complete application build 2228 targets passed. RTC follow-up initially failed because editing inserted literal newline escapes in include directives; fixed and retry passed. Final application change (alarm interrupts history view) rebuilt successfully.
Final marker VELAGUARD_APP_BUILD_PASS, outer exit 0. RWX/build-id and shared SiFli warnings remain; no claim of zero warnings.
bin_host tooling diagnostics are historical, not fixed/reinstalled here.

Final binary:
- /home/oohb144/workspace/openvela-contest/cmake_out/velaguard_core_protocol/nuttx.bin
- 1774224 bytes
- SHA256 0d2e4a0372348bdd377bae2af4f9e1f7e8fc4d9b3de47e21cb02fa30becd884f
- Frozen Windows copy: D:/date/code/openvela/.artifacts/velaguard-offline-0d2e4a03.bin
- SRAM static 196004 / 524288; Flash 1774224 / 16777216; PSRAM static 0 (8 MiB runtime heap region available only after successful init).

System.map includes velaguard_main, vg_runtime_start/reload/tick/create/ack/snooze/list/status,
vg_commands_execute, vg_store_save/load, vg_store_file_*, vg_cron_* and vg_agent_cron_*.
This is actual application integration, unlike the earlier diagnostic builds that discarded Store.

Original baseline remains:
cmake_out/sf32lb52_devkit_lcd/nuttx.bin
SHA256 d401791ee012ac5f8a080edf0256b851fa2abc039ae90707641a9a6bf69f3b02.

Portable tests: logs/offline_app six runners; later gateway final 8 tests in logs/gateway/09-green-cli-encoding.txt.
Memory measurement: logs/offline_app/store_memory.txt, 1511716-byte conservative host allocation peak and zero outstanding instrumented payload.
Limits, hardware prerequisites, source rationale: ../../OFFLINE_APP_P0.md.
