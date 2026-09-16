#include "velaguard_rgb.h"
#include <errno.h>
#include <stddef.h>
#include <stdbool.h>

int vg_rgb_plan(uint32_t hz, uint8_t red, uint8_t green, uint8_t blue,
                vg_rgb_plan_t *plan)
{
  uint32_t grb;
  unsigned i;
  if (!plan) return -EINVAL;
  if (hz < 24000000 || hz > 240000000) return -ERANGE;
  plan->period = (hz + 400000) / 800000;
  plan->zero_high = (uint32_t)(((uint64_t)hz * 300 + 500000000) / 1000000000);
  plan->one_high = (uint32_t)(((uint64_t)hz * 750 + 500000000) / 1000000000);
  plan->reset = (hz + 9999) / 10000; /* 低电平至少 100us。 */
  grb = ((uint32_t)green << 16) | ((uint32_t)red << 8) | blue;
  plan->word = (grb << 8) | 0x80u; /* Sentinel ends exactly 24 shifts. */
  for (i = 0; i < 24; i++) plan->bits[i] = (uint8_t)((grb >> (23 - i)) & 1);
  return 0;
}

#ifdef __NuttX__
#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include "bf0_hal.h"

static bool g_rgb_ready;

/* 短临界区不调用 Flash 中的 HAL；停走的 CYCCNT 也必须有限退出。 */
static inline __attribute__((always_inline)) int rgb_wait(uint32_t start,
                                                         uint32_t cycles)
{
  uint32_t budget = cycles + 64;
  while ((uint32_t)(DWT->CYCCNT - start) < cycles)
    {
      if (--budget == 0) return -ETIMEDOUT;
    }
  return 0;
}
static __attribute__((section(".ramfunc"), noinline, optimize("O2", "omit-frame-pointer"))) int
rgb_emit(const vg_rgb_plan_t *plan)
{
  GPIO_TypeDef *gpio = hwp_gpio1 + 1;
  irqstate_t flags;
  uint32_t start, grb = plan->word;
  const uint32_t period = plan->period, zero = plan->zero_high;
  const uint32_t one = plan->one_high, reset = plan->reset;
  int rc;
  /* All plan/PSRAM reads precede the timing-critical pulse loop. */
  /* Sentinel avoids a per-bit loop counter spilling to the stack. */
  gpio->DOCR = 1;
  rc = rgb_wait(DWT->CYCCNT, reset);
  if (rc) return rc;
  flags = enter_critical_section();
  do
    {
      const uint32_t high = (grb & 0x80000000u) ? one : zero;
      grb <<= 1;
      start = DWT->CYCCNT;
      gpio->DOSR = 1;
      rc = rgb_wait(start, high);
      gpio->DOCR = 1;
      if (rc) break;
      rc = rgb_wait(start, period);
      if (rc) break;
    } while (grb != 0x80000000u);
  gpio->DOCR = 1;
  leave_critical_section(flags);
  if (!rc) rc = rgb_wait(DWT->CYCCNT, reset);
  return rc;
}
int vg_rgb_init(void)
{
  GPIO_InitTypeDef config = {0};
  int rc;
  if (g_rgb_ready) return 0;
  rc = HAL_PIN_Set(PAD_PA32, GPIO_A32, PIN_PULLDOWN, 1);
  if (rc) return -EIO;
  config.Pin = 32;
  config.Mode = GPIO_MODE_OUTPUT;
  config.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(hwp_gpio1, &config);
  (hwp_gpio1 + 1)->DOCR = 1;
  if (!HAL_DBG_DWT_IsInit()) HAL_DBG_DWT_Init();
  rc = rgb_wait(DWT->CYCCNT, 100);
  if (rc) return rc;
  g_rgb_ready = true;
  rc = vg_rgb_off();
  if (rc) g_rgb_ready = false;
  return rc;
}
int vg_rgb_set(uint8_t red, uint8_t green, uint8_t blue)
{
  vg_rgb_plan_t plan;
  uint32_t hz;
  int rc;
  if (!g_rgb_ready) return -ENODEV;
  hz = HAL_RCC_GetHCLKFreq(CORE_ID_DEFAULT);
  /* 数学规划低频可用；软件输出要求更高 HCLK 留出寄存器访问余量。 */
  if (hz < 96000000)
    {
      (hwp_gpio1 + 1)->DOCR = 1;
      return -ERANGE;
    }
  rc = vg_rgb_plan(hz, red, green, blue, &plan);
  if (!rc) rc = rgb_emit(&plan);
  if (rc) (hwp_gpio1 + 1)->DOCR = 1;
  return rc;
}
#else
int vg_rgb_init(void) { return -ENOSYS; }
int vg_rgb_set(uint8_t red, uint8_t green, uint8_t blue)
{
  (void)red; (void)green; (void)blue;
  return -ENOSYS;
}
#endif
int vg_rgb_off(void)
{
  return vg_rgb_set(0, 0, 0);
}
