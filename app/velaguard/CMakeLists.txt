if(CONFIG_LVX_USE_DEMO_CONTEST2026_447_VELAGUARD)
  # LCD 的 CTP_INT 是 PA31，不能沿用官方 defconfig 中的 ULP PA41。
  if(CONFIG_ARCH_BOARD_CUSTOM_DIR MATCHES "sf32lb52_devkit_lcd$" AND
     (NOT CONFIG_INPUT_FT6146 OR NOT CONFIG_TOUCH_IRQ_PIN EQUAL 31))
    message(FATAL_ERROR "VelaGuard LCD requires FT6146 and CONFIG_TOUCH_IRQ_PIN=31. Correct the build .config; do not edit upstream defconfig.")
  endif()
  string(TIMESTAMP VG_BUILD_EPOCH "%s" UTC)
  # 仅此适配单元需裁剪未调用的官方 builtin init；不改板级 NOOPT。
  set_source_files_properties(src/velaguard_tools.c PROPERTIES COMPILE_OPTIONS "-O2")
  nuttx_add_application(
    NAME velaguard
    PRIORITY 100
    STACKSIZE 32768
    MODULE ${CONFIG_LVX_USE_DEMO_CONTEST2026_447_VELAGUARD}
    SRCS src/velaguard_main.c src/velaguard_core.c src/velaguard_protocol.c
         src/velaguard_store.c src/velaguard_store_file.c src/velaguard_runtime.c
         src/velaguard_commands.c src/velaguard_cron.c src/velaguard_skill.c src/velaguard_skill_loader.c src/velaguard_tools.c src/velaguard_agent.c src/velaguard_ui_logic.c src/velaguard_font_cn16.c src/velaguard_nor.c src/velaguard_clock.c src/velaguard_rgb.c
    COMPILE_FLAGS -DVG_BUILD_EPOCH=${VG_BUILD_EPOCH}LL -DSOC_BF0_HCPU -DSF32LB52X -DUSE_HAL_DRIVER -DHAL_TICK_PER_SECOND=1000
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_LIST_DIR}/include
      ${CMAKE_SOURCE_DIR}/../packages/ai_agent/include
      ${CMAKE_SOURCE_DIR}/../packages/ai_agent/src
      ${CMAKE_SOURCE_DIR}/../apps/netutils/cjson/cJSON
      ${CMAKE_SOURCE_DIR}/../vendor/sifli/chips/sf32lb52/include
      ${CMAKE_SOURCE_DIR}/../vendor/sifli/chips/drivers/Include
      ${CMAKE_SOURCE_DIR}/../vendor/sifli/chips/drivers/cmsis/Include
      ${CMAKE_SOURCE_DIR}/../vendor/sifli/chips/drivers/cmsis/sf32lb52x
      ${CMAKE_SOURCE_DIR}/../vendor/sifli/chips/external/CMSIS/Include
      ${CMAKE_SOURCE_DIR}/../vendor/sifli/chips/boards/include/config/sf32lb52x
      ${CMAKE_SOURCE_DIR}/../vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/include)
endif()
