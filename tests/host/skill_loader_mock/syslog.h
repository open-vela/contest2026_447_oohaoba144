#ifndef VG_HOST_SYSLOG_H
#define VG_HOST_SYSLOG_H
/* 仅替代宿主没有的 syslog；官方 loader 和其余 POSIX I/O 保持真实实现。 */
#define LOG_DEBUG 7
#define LOG_INFO 6
#define LOG_WARNING 4
#define LOG_ERR 3
static inline void syslog(int priority,const char *format,...) { (void)priority; (void)format; }
#endif