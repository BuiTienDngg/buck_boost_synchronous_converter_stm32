#include <sys/stat.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
extern char _end[], _heap_limit[];
static char *heap_at;
void *_sbrk(ptrdiff_t increment) {
  char *before; if (!heap_at) heap_at=_end; before=heap_at;
  if (increment<0 || (uintptr_t)increment>(uintptr_t)_heap_limit-(uintptr_t)heap_at) {errno=ENOMEM;return (void *)-1;}
  heap_at+=increment;return before;
}
int _write(int file,const void *data,size_t length) {(void)file;(void)data;(void)length;errno=ENOSYS;return -1;}
int _read(int file,void *data,size_t length) {(void)file;(void)data;(void)length;errno=ENOSYS;return -1;}
int _close(int file) {(void)file;errno=EBADF;return -1;}
int _lseek(int file,int offset,int whence) {(void)file;(void)offset;(void)whence;errno=ESPIPE;return -1;}
int _fstat(int file,struct stat *status) {(void)file;(void)status;errno=ENOSYS;return -1;}
int _isatty(int file) {(void)file;return 0;}
