#include <nuttx/config.h>
#include <nuttx/cache.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mtd/mtd.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "velaguard_nor.h"

/* Windows LLP64 的 unsigned long 装不下指针；主机 mock 使用 uintptr_t。
 * NuttX 目标保留 ioctl 的原始 unsigned long ABI。 */
#ifdef VG_NOR_HOST_TEST
typedef uintptr_t vg_nor_ioctl_arg_t;
#else
typedef unsigned long vg_nor_ioctl_arg_t;
#endif

struct vg_nor_proxy_s
{
  struct mtd_dev_s mtd;
  struct mtd_dev_s *lower;
  struct inode *inode;
  struct mtd_geometry_s geometry;
  size_t capacity;
};

static struct vg_nor_proxy_s g_nor;
static bool g_prepared;

static int vg_nor_range(struct vg_nor_proxy_s *proxy, off_t start,
                         size_t count, size_t unit, size_t *bytes)
{
  size_t units = proxy->capacity / unit;
  if (start < 0) return -EINVAL;
  if (count > SIZE_MAX / unit || count * unit > PTRDIFF_MAX)
    return -EOVERFLOW;
  if ((uintmax_t)start > units || count > units - (size_t)start)
    return -EINVAL;
  *bytes = count * unit;
  return 0;
}

static int vg_nor_buffer(const void *buffer, size_t bytes)
{
  uintptr_t start = (uintptr_t)buffer;
  if (bytes != 0 && buffer == NULL) return -EINVAL;
  if (bytes > UINTPTR_MAX - start) return -EOVERFLOW;
  return 0;
}

static int vg_nor_erase(struct mtd_dev_s *dev, off_t start, size_t count)
{
  struct vg_nor_proxy_s *proxy = (struct vg_nor_proxy_s *)dev;
  size_t bytes;
  int rc = vg_nor_range(proxy, start, count, proxy->geometry.erasesize, &bytes);
  if (rc < 0) return rc;
  if (count == 0) return 0;
  return MTD_ERASE(proxy->lower, start, count);
}

static ssize_t vg_nor_bread(struct mtd_dev_s *dev, off_t start, size_t count,
                            uint8_t *buffer)
{
  struct vg_nor_proxy_s *proxy = (struct vg_nor_proxy_s *)dev;
  size_t bytes;
  int rc = vg_nor_range(proxy, start, count, proxy->geometry.blocksize, &bytes);
  if (rc < 0) return rc;
  rc = vg_nor_buffer(buffer, bytes);
  if (rc < 0) return rc;
  if (count == 0) return 0;
  return MTD_BREAD(proxy->lower, start, count, buffer);
}

static ssize_t vg_nor_bwrite(struct mtd_dev_s *dev, off_t start, size_t count,
                             const uint8_t *buffer)
{
  struct vg_nor_proxy_s *proxy = (struct vg_nor_proxy_s *)dev;
  size_t bytes;
  int rc = vg_nor_range(proxy, start, count, proxy->geometry.blocksize, &bytes);
  if (rc < 0) return rc;
  rc = vg_nor_buffer(buffer, bytes);
  if (rc < 0) return rc;
  if (count == 0) return 0;
  if (proxy->lower->bwrite == NULL) return -ENOSYS;

  /* 清理的是 VFS/LittleFS 实际提交的缓存，而不是更上层 JSON 的地址。
   * end 为排他边界，缓存行对齐和屏障由 NuttX 架构实现处理。 */
  up_clean_dcache((uintptr_t)buffer, (uintptr_t)buffer + bytes);
  return MTD_BWRITE(proxy->lower, start, count, buffer);
}

static ssize_t vg_nor_read(struct mtd_dev_s *dev, off_t start, size_t count,
                           uint8_t *buffer)
{
  struct vg_nor_proxy_s *proxy = (struct vg_nor_proxy_s *)dev;
  size_t bytes;
  int rc = vg_nor_range(proxy, start, count, 1, &bytes);
  if (rc < 0) return rc;
  rc = vg_nor_buffer(buffer, bytes);
  if (rc < 0) return rc;
  if (count == 0) return 0;
  return MTD_READ(proxy->lower, start, count, buffer);
}

#ifdef CONFIG_MTD_BYTE_WRITE
static ssize_t vg_nor_write(struct mtd_dev_s *dev, off_t start, size_t count,
                            const uint8_t *buffer)
{
  struct vg_nor_proxy_s *proxy = (struct vg_nor_proxy_s *)dev;
  size_t bytes;
  int rc = vg_nor_range(proxy, start, count, 1, &bytes);
  if (rc < 0) return rc;
  rc = vg_nor_buffer(buffer, bytes);
  if (rc < 0) return rc;
  if (count == 0) return 0;
  if (proxy->lower->write == NULL) return -ENOSYS;
  up_clean_dcache((uintptr_t)buffer, (uintptr_t)buffer + bytes);
  return MTD_WRITE(proxy->lower, start, count, buffer);
}
#endif

static int vg_nor_ioctl(struct mtd_dev_s *dev, int cmd, vg_nor_ioctl_arg_t arg)
{
  struct vg_nor_proxy_s *proxy = (struct vg_nor_proxy_s *)dev;
  return MTD_IOCTL(proxy->lower, cmd, arg);
}

static int vg_nor_isbad(struct mtd_dev_s *dev, off_t block)
{
  struct vg_nor_proxy_s *proxy = (struct vg_nor_proxy_s *)dev;
  size_t bytes;
  int rc = vg_nor_range(proxy, block, 1, proxy->geometry.erasesize, &bytes);
  if (rc < 0) return rc;
  return MTD_ISBAD(proxy->lower, block);
}

static int vg_nor_markbad(struct mtd_dev_s *dev, off_t block)
{
  struct vg_nor_proxy_s *proxy = (struct vg_nor_proxy_s *)dev;
  size_t bytes;
  int rc = vg_nor_range(proxy, block, 1, proxy->geometry.erasesize, &bytes);
  if (rc < 0) return rc;
  return MTD_MARKBAD(proxy->lower, block);
}

int vg_nor_prepare(void)
{
  uint64_t capacity;
  int rc;
  if (g_prepared) return 0;
  memset(&g_nor, 0, sizeof(g_nor));
  rc = find_mtddriver(VG_NOR_SOURCE, &g_nor.inode);
  if (rc < 0) return rc;
  if (g_nor.inode == NULL) return -ENODEV;
  g_nor.lower = g_nor.inode->u.i_mtd;
  if (g_nor.lower == NULL)
    {
      rc = -ENODEV;
      goto fail;
    }
  rc = MTD_IOCTL(g_nor.lower, MTDIOC_GEOMETRY,
                 (vg_nor_ioctl_arg_t)(uintptr_t)&g_nor.geometry);
  if (rc < 0) goto fail;
  if (g_nor.geometry.blocksize == 0 || g_nor.geometry.erasesize == 0 ||
      g_nor.geometry.neraseblocks == 0 ||
      g_nor.geometry.erasesize % g_nor.geometry.blocksize != 0)
    {
      rc = -EINVAL;
      goto fail;
    }
  capacity = (uint64_t)g_nor.geometry.erasesize * g_nor.geometry.neraseblocks;
  if (capacity > SIZE_MAX || capacity > PTRDIFF_MAX)
    {
      rc = -EOVERFLOW;
      goto fail;
    }
  if (g_nor.lower->bwrite == NULL || g_nor.lower->bread == NULL ||
      g_nor.lower->erase == NULL)
    {
      rc = -ENOSYS;
      goto fail;
    }
  g_nor.capacity = (size_t)capacity;
  g_nor.mtd.erase = vg_nor_erase;
  g_nor.mtd.bread = vg_nor_bread;
  g_nor.mtd.bwrite = vg_nor_bwrite;
  g_nor.mtd.read = vg_nor_read;
#ifdef CONFIG_MTD_BYTE_WRITE
  g_nor.mtd.write = vg_nor_write;
#endif
  g_nor.mtd.ioctl = vg_nor_ioctl;
  g_nor.mtd.isbad = vg_nor_isbad;
  g_nor.mtd.markbad = vg_nor_markbad;
  g_nor.mtd.name = "velaguard-cache-nor";
  rc = register_mtddriver(VG_NOR_DEVICE, &g_nor.mtd, 0600, NULL);
  if (rc < 0) goto fail;
  /* 此后 inode 永久持有，不能释放后留下悬空的 lower 指针。 */
  g_prepared = true;
  return 0;
fail:
  close_mtddriver(g_nor.inode);
  memset(&g_nor, 0, sizeof(g_nor));
  return rc;
}
