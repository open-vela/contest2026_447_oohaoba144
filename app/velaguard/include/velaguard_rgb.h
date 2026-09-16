#ifndef VELAGUARD_RGB_H
#define VELAGUARD_RGB_H
#include <stdint.h>
typedef struct
{
  uint32_t period, zero_high, one_high, reset, word;
  uint8_t bits[24];
} vg_rgb_plan_t;
int vg_rgb_plan(uint32_t hz, uint8_t red, uint8_t green, uint8_t blue,
                vg_rgb_plan_t *plan);
/* 单调用者、主循环串行使用。目标仅本板 PA32，直接使用已链接 HAL。
 * 输出函数位于 .ramfunc；已交叉编译并初始化，实际灯色/波形待观察。
 * set 要求 HCLK 96..240MHz，规划器 24..240MHz；频率须在发送时稳定。
 * 建议调用方单色仅 16/255；本接口不私自改变输入亮度。
 * 失败使引脚低，但不能保证灯内已锁存颜色被清除；off 需要成功发送。
 * DWT 超时路径为有限轮询，其临界区可能长于正常约30us。
 */
int vg_rgb_init(void);
int vg_rgb_set(uint8_t red, uint8_t green, uint8_t blue);
int vg_rgb_off(void);
#endif
