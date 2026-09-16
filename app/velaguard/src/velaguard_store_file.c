#include "velaguard_store_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif

/* 调用者使用受控目录，禁止同时改名、替换目录或由其他写者修改槽文件。 */
static int native_open(void *context, const char *path, int flags,
                        unsigned mode)
{
  (void)context;
#ifdef _WIN32
  return _open(path, flags | _O_BINARY, (int)mode);
#else
  return open(path, flags, (mode_t)mode);
#endif
}

static ptrdiff_t native_read(void *context, int fd, void *buf, size_t n)
{
  (void)context;
#ifdef _WIN32
  return (ptrdiff_t)_read(fd, buf, (unsigned)n);
#else
  return (ptrdiff_t)read(fd, buf, n);
#endif
}

static ptrdiff_t native_write(void *context, int fd, const void *buf, size_t n)
{
  (void)context;
#ifdef _WIN32
  return (ptrdiff_t)_write(fd, buf, (unsigned)n);
#else
  return (ptrdiff_t)write(fd, buf, n);
#endif
}

static int native_sync(void *context, int fd)
{
  (void)context;
#ifdef _WIN32
  return _commit(fd);
#else
  return fsync(fd);
#endif
}

static int native_close(void *context, int fd)
{
  (void)context;
#ifdef _WIN32
  return _close(fd);
#else
  return close(fd);
#endif
}

static int native_directory(void *context, const char *path)
{
  int rc;
  (void)context;
#ifdef _WIN32
  struct _stat info;
  rc = _stat(path, &info);
  if (rc == 0 && (info.st_mode & _S_IFMT) != _S_IFDIR)
#else
  struct stat info;
  rc = stat(path, &info);
  if (rc == 0 && !S_ISDIR(info.st_mode))
#endif
    {
      errno = ENOTDIR;
      return -1;
    }

  return rc;
}

static int native_parent(void *context, const char *path)
{
#ifdef _WIN32
  (void)context;
  (void)path;
  /* CRT 无目录同步接口，必须由经验证的专用适配提供此能力。 */
  errno = ENOTSUP;
  return -1;
#else
  int fd;
  int result;
  int flags = O_RDONLY;
#  ifdef O_DIRECTORY
  flags |= O_DIRECTORY;
#  endif
  do
    {
      fd = native_open(context, path, flags, 0);
    }
  while (fd < 0 && errno == EINTR);

  if (fd < 0)
    {
      return -1;
    }

  do
    {
      result = native_sync(context, fd);
    }
  while (result < 0 && errno == EINTR);

  /* close 失败同样保守返回错误，不重试可能已释放的 fd。 */
  if (native_close(context, fd) != 0)
    {
      result = -1;
    }

  return result;
#endif
}

void vg_store_file_native_ops(vg_store_file_ops_t *ops)
{
  if (ops != NULL)
    {
      ops->context = NULL;
      ops->open_file = native_open;
      ops->read_file = native_read;
      ops->write_file = native_write;
      ops->sync_file = native_sync;
      ops->close_file = native_close;
      ops->sync_parent = native_parent;
      ops->check_directory = native_directory;
    }
}

int vg_store_file_init(vg_store_file_t *file, const char *directory,
                        const vg_store_file_ops_t *ops)
{
  vg_store_file_ops_t selected;
  size_t n;

  if (file == NULL || directory == NULL)
    {
      return VG_ERR_INVALID_MESSAGE;
    }

  /* 为分隔符、slotN.json 和 NUL 留出空间，避免写出截断路径。 */
  n = strlen(directory);
  if (n == 0 || n + sizeof("/slot0.json") > VG_STORE_FILE_PATH_CAPACITY)
    {
      return VG_ERR_INVALID_MESSAGE;
    }

  if (ops == NULL)
    {
      vg_store_file_native_ops(&selected);
    }
  else
    {
      selected = *ops;
    }

  if (selected.open_file == NULL || selected.read_file == NULL ||
      selected.write_file == NULL || selected.sync_file == NULL ||
      selected.close_file == NULL || selected.sync_parent == NULL ||
      selected.check_directory == NULL)
    {
      return VG_ERR_INVALID_MESSAGE;
    }

  if (selected.check_directory(selected.context, directory) != 0)
    {
      return VG_STORE_IO_ERROR;
    }

  memcpy(file->directory, directory, n + 1);
  file->ops = selected;
  return VG_OK;
}

static int slot_path(vg_store_file_t *file, unsigned slot, char *path)
{
  int n;
  if (file == NULL || slot > 1)
    {
      return -1;
    }

  n = snprintf(path, VG_STORE_FILE_PATH_CAPACITY, "%s/slot%u.json",
               file->directory, slot);
  return n > 0 && n < VG_STORE_FILE_PATH_CAPACITY ? 0 : -1;
}

static int vg_store_file_read(void *context, unsigned slot, char *buf, size_t cap,
                     size_t *length)
{
  vg_store_file_t *file = context;
  char path[VG_STORE_FILE_PATH_CAPACITY];
  char extra;
  size_t total = 0;
  ptrdiff_t count;
  int fd;
  int result = -1;

  if (buf == NULL || length == NULL || cap == 0 ||
      cap > VG_STORE_MAX_BYTES || slot_path(file, slot, path) != 0)
    {
      return -1;
    }

  do
    {
      fd = file->ops.open_file(file->ops.context, path, O_RDONLY, 0);
    }
  while (fd < 0 && errno == EINTR);

  if (fd < 0)
    {
      if (errno == ENOENT &&
          file->ops.check_directory(file->ops.context, file->directory) == 0)
        {
          return 1;
        }

      return -1;
    }

  for (;;)
    {
      /* 满 cap 后再读一个字节，区分恰好满与超长，不能静默截断。 */
      size_t available = total < cap ? cap - total : 1;
      void *destination = total < cap ? (void *)(buf + total) : &extra;
      count = file->ops.read_file(file->ops.context, fd, destination, available);
      if (count < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          break;
        }

      if (count == 0)
        {
          result = 0;
          break;
        }

      if ((size_t)count > available || total == cap)
        {
          break;
        }

      total += (size_t)count;
    }

  if (file->ops.close_file(file->ops.context, fd) != 0)
    {
      result = -1;
    }

  if (result == 0)
    {
      *length = total;
    }

  return result;
}

static int vg_store_file_write(void *context, unsigned slot, const char *buf, size_t length)
{
  vg_store_file_t *file = context;
  char path[VG_STORE_FILE_PATH_CAPACITY];
  size_t total = 0;
  ptrdiff_t count;
  int fd;
  int result = -1;

  if (buf == NULL || length == 0 || length > VG_STORE_MAX_BYTES ||
      slot_path(file, slot, path) != 0)
    {
      return -1;
    }

  do
    {
      fd = file->ops.open_file(file->ops.context, path, O_WRONLY | O_CREAT | O_TRUNC,
                               0600);
    }
  while (fd < 0 && errno == EINTR);

  if (fd < 0)
    {
      return -1;
    }

  while (total < length)
    {
      count = file->ops.write_file(file->ops.context, fd, buf + total,
                                   length - total);
      if (count < 0 && errno == EINTR)
        {
          continue;
        }

      /* 零进展不能无限循环；非法回调长度也必须拒绝。 */
      if (count <= 0 || (size_t)count > length - total)
        {
          goto close_out;
        }

      total += (size_t)count;
    }

  do
    {
      result = file->ops.sync_file(file->ops.context, fd);
    }
  while (result < 0 && errno == EINTR);

close_out:
  if (file->ops.close_file(file->ops.context, fd) != 0)
    {
      result = -1;
    }

  if (result == 0)
    {
      result = file->ops.sync_parent(file->ops.context, file->directory);
    }

  return result == 0 ? 0 : -1;
}

vg_store_io_t vg_store_file_io(vg_store_file_t *file)
{
  vg_store_io_t io;
  io.context = file;
  io.read = vg_store_file_read;
  io.write_sync = vg_store_file_write;
  return io;
}
