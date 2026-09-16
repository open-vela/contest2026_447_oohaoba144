include $(APPDIR)/Make.defs
PROGNAME = velaguard
PRIORITY = 100
STACKSIZE = 32768
MODULE = $(CONFIG_LVX_USE_DEMO_CONTEST2026_447_VELAGUARD)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/demos/contest2026_447_velaguard/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/src
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/netutils/cjson/cJSON
CFLAGS += -DSOC_BF0_HCPU -DSF32LB52X -DUSE_HAL_DRIVER -DHAL_TICK_PER_SECOND=1000
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/sifli/chips/sf32lb52/include
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/sifli/chips/drivers/Include
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/sifli/chips/drivers/cmsis/Include
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/sifli/chips/drivers/cmsis/sf32lb52x
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/sifli/chips/external/CMSIS/Include
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/sifli/chips/boards/include/config/sf32lb52x
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/include
MAINSRC = src/velaguard_main.c
CSRCS = src/velaguard_core.c src/velaguard_protocol.c src/velaguard_store.c src/velaguard_store_file.c
CSRCS += src/velaguard_runtime.c src/velaguard_commands.c src/velaguard_cron.c src/velaguard_skill.c src/velaguard_skill_loader.c src/velaguard_tools.c src/velaguard_agent.c src/velaguard_ui_logic.c src/velaguard_font_cn16.c src/velaguard_nor.c src/velaguard_clock.c src/velaguard_rgb.c
src/velaguard_tools.c_CFLAGS += -O2
src/velaguard_tools.c_CELFFLAGS += -O2
include $(APPDIR)/Application.mk
