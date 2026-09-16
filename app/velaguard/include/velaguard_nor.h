#ifndef VELAGUARD_NOR_H
#define VELAGUARD_NOR_H

#define VG_NOR_DEVICE "/dev/vgnor"
#define VG_NOR_SOURCE "/dev/config0"

/* 返回 0 或负 errno。只注册缓存同步代理，不格式化、不擦除或挂载。
 * 成功后代理和底层 inode 保持到重启；初始化必须由主线程串行调用。
 */
int vg_nor_prepare(void);

#endif
