#include "velaguard_rgb.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
static void check_word(const vg_rgb_plan_t *p)
{
  uint32_t word = p->word;
  unsigned count = 0;
  do {
    assert(count < 24);
    assert((word >> 31) == p->bits[count]);
    word <<= 1;
    count++;
  } while (word != 0x80000000u);
  assert(count == 24);
}
int main(void)
{
  vg_rgb_plan_t p;
  unsigned i;
  assert(vg_rgb_plan(240000000,0x80,0x01,0x55,&p)==0);
  assert(p.period==300 && p.zero_high==72 && p.one_high==180 && p.reset==24000);
  for(i=0;i<24;i++) assert(p.bits[i]==((0x018055u>>(23-i))&1));
  check_word(&p);
  assert(vg_rgb_plan(24000000,0,0,0,&p)==0);
  assert(p.period==30 && p.zero_high==7 && p.one_high==18 && p.reset==2400);
  for(i=0;i<24;i++) assert(p.bits[i]==0);
  check_word(&p);
  assert(vg_rgb_plan(48000000,255,255,255,&p)==0);
  for(i=0;i<24;i++) assert(p.bits[i]==1);
  check_word(&p);
  assert(vg_rgb_plan(23999999,0,0,0,&p)==-ERANGE);
  assert(vg_rgb_plan(240000001,0,0,0,&p)==-ERANGE);
  assert(vg_rgb_plan(0,0,0,0,&p)==-ERANGE);
  assert(vg_rgb_plan(24000000,0,0,0,0)==-EINVAL);
  assert(vg_rgb_plan(25000000,0,0,0,&p)==0 && p.period==31 && p.zero_high==8 && p.one_high==19 && p.reset==2500);
  puts("PASS RGB plan: GRB/MSB, zero/ones, 24..240MHz bounds, nearest rounding, invalid input");
  return 0;
}
