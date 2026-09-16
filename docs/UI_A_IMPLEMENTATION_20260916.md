# VelaGuard UI A implementation evidence

Date: 2026-09-16

## Scope

The approved "守护环" design adds four touch pages to the existing offline reminder demo:

1. 守护：next-reminder ring, trusted wall-time state and offline persistence message.
2. 任务：selected task state, remaining time, guarded rearm/snooze action, next task and Demo shortcut.
3. 记录：persisted history item and next-record navigation.
4. 设备：runtime, storage, display, touch and RTC health.

Horizontal gestures and the four bottom targets select pages. A live alert owns a full-screen overlay until its captured task is acknowledged or snoozed. A new alert returns the underlying page to 守护.

## Time integrity

The watch face displays `时间未同步` while `rtc_valid=false`; it does not render an invented clock. When RTC is valid, the face derives `HH:MM` with the contest device's China Standard Time offset (`UTC+8`). Relative reminder countdowns remain usable without RTC.

## Design references

- InfiniTime watch faces: https://github.com/InfiniTimeOrg/InfiniTime/blob/main/doc/gettingStarted/Watchfaces.md
- InfiniTime UI guidance: https://github.com/InfiniTimeOrg/InfiniTime/blob/main/doc/ui_guidelines.md
- Open-Smartwatch watch faces: https://github.com/Open-Smartwatch/open-smartwatch-os/blob/master/docs/firmware/apps/watchfaces.md
- LVGL: https://github.com/lvgl/lvgl

No third-party source code or bitmap asset was copied. The implementation uses the project's existing LVGL dependency and generated Noto Sans SC font.

## Verification

- UI logic host test: PASS.
- Chinese font integrity test: PASS, 7,540 glyphs.
- Existing ARM build directory: `cmake_out/velaguard_core_protocol`.
- Board configuration SHA256: `2749e2f85b99c9feb60678e4dbae41b4c823906d328bef1a4ecdae616781a4c7`.
- ARM build: 19/19 incremental steps completed; existing linker RWX/build-id warnings remain.
- Final flash usage reported by linker: 2,665,504 bytes.
- Final static SRAM usage reported by linker: 203,812 bytes.
- Frozen candidate: `D:\date\code\openvela\.artifacts\velaguard-ui-a-25c19591.bin`.
- Candidate SHA256: `25c195916158aad49003c96dabb436d619704105534715fa5ded5127bf77252b`.
- Candidate end address when written at `0x12010000`: `0x1229AC20`, below storage start `0x129A0000`.

## Hardware update

The candidate was flashed with user authorization and `--verify` returned exit code 0. A controlled `burn_reset` captured SFBL and application startup. Read-only status confirmed ready runtime plus display, touch and storage, with app/ui error 0. Explicit host-time synchronization succeeded and `rtc_valid` became true. Physical appearance and swipe behavior still require direct observation.
