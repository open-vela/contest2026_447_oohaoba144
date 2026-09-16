#ifndef VG_MOCK_MTD_H
#define VG_MOCK_MTD_H
#include <stdint.h>
#include <sys/types.h>
#include <errno.h>
struct mtd_geometry_s { uint32_t blocksize, erasesize, neraseblocks; char model[32]; };
struct mtd_dev_s {
  int (*erase)(struct mtd_dev_s *,off_t,size_t);
  ssize_t (*bread)(struct mtd_dev_s *,off_t,size_t,uint8_t *);
  ssize_t (*bwrite)(struct mtd_dev_s *,off_t,size_t,const uint8_t *);
  ssize_t (*read)(struct mtd_dev_s *,off_t,size_t,uint8_t *);
#ifdef CONFIG_MTD_BYTE_WRITE
  ssize_t (*write)(struct mtd_dev_s *,off_t,size_t,const uint8_t *);
#endif
  int (*ioctl)(struct mtd_dev_s *,int,uintptr_t);
  int (*isbad)(struct mtd_dev_s *,off_t);
  int (*markbad)(struct mtd_dev_s *,off_t);
  const char *name;
};
#define MTDIOC_GEOMETRY 1
#define MTD_ERASE(d,s,n) ((d)->erase?(d)->erase(d,s,n):-ENOSYS)
#define MTD_BREAD(d,s,n,b) ((d)->bread?(d)->bread(d,s,n,b):-ENOSYS)
#define MTD_BWRITE(d,s,n,b) ((d)->bwrite?(d)->bwrite(d,s,n,b):-ENOSYS)
#define MTD_READ(d,s,n,b) ((d)->read?(d)->read(d,s,n,b):-ENOSYS)
#define MTD_WRITE(d,s,n,b) ((d)->write?(d)->write(d,s,n,b):-ENOSYS)
#define MTD_IOCTL(d,c,a) ((d)->ioctl?(d)->ioctl(d,c,a):-ENOSYS)
#define MTD_ISBAD(d,b) ((d)->isbad?(d)->isbad(d,b):-ENOSYS)
#define MTD_MARKBAD(d,b) ((d)->markbad?(d)->markbad(d,b):-ENOSYS)
#endif
