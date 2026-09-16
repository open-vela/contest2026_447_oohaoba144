#ifndef VELAGUARD_STORE_FILE_H
#define VELAGUARD_STORE_FILE_H

#include "velaguard_store.h"

#define VG_STORE_FILE_PATH_CAPACITY 384

/* 系统调用表：失败返回 -1 并设置 errno，read/write 返回实际字节数。
 * close 不重试；即使返回 EINTR，描述符是否已关闭也不能跨平台假定。
 * sync_parent 必须保证目录项耐久性，不能把“不支持”当作成功。
 * 回调及 context 的生命周期必须覆盖 file；所有操作只能串行。
 */
typedef struct
{
  void *context;
  int (*open_file)(void *, const char *, int, unsigned);
  ptrdiff_t (*read_file)(void *, int, void *, size_t);
  ptrdiff_t (*write_file)(void *, int, const void *, size_t);
  int (*sync_file)(void *, int);
  int (*close_file)(void *, int);
  int (*sync_parent)(void *, const char *);
  int (*check_directory)(void *, const char *);
} vg_store_file_ops_t;

typedef struct
{
  char directory[VG_STORE_FILE_PATH_CAPACITY];
  vg_store_file_ops_t ops;
} vg_store_file_t;

/* directory 必须预先存在；模块不创建目录、不删除文件。
 * ops=NULL 使用本机调用。POSIX 默认同步目录；Windows CRT 默认不支持
 * 目录耐久性屏障并返回 ENOTSUP，因此不能把默认 Windows 保存当作成功。
 * 槽必须是两个独立普通文件，不得使用符号链接或硬链接；模块未验证
 * 链接身份。目录不得由其他写者同时修改、替换或改名。
 * 可注入已验证的文件系统专用屏障。主机进程恢复测试的模拟屏障不构成
 * 真实断电耐久性保证。任一 load 错误之后必须停止修改/保存。
 */
int vg_store_file_init(vg_store_file_t *, const char *,
                        const vg_store_file_ops_t *);
/* 仅在 init 成功后调用；file 本体必须比返回的 io 活得更久。 */
vg_store_io_t vg_store_file_io(vg_store_file_t *);
void vg_store_file_native_ops(vg_store_file_ops_t *);

#endif
