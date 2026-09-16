#ifndef VG_MOCK_FS_H
#define VG_MOCK_FS_H
#include <nuttx/mtd/mtd.h>
struct inode { union {struct mtd_dev_s *i_mtd;} u; };
int find_mtddriver(const char *,struct inode **);
int close_mtddriver(struct inode *);
int register_mtddriver(const char *,struct mtd_dev_s *,mode_t,void *);
#endif
