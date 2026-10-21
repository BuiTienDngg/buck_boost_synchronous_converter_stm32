#ifndef PSU_REMOTE_H
#define PSU_REMOTE_H
#include "psu_settings.h"
enum { REM_GET, REM_SET, REM_ON, REM_OFF, REM_CLEAR, REM_CONFIG, REM_DEFAULTS, REM_SAVE, REM_PRESET, REM_HEAT };
typedef struct {
  uint32_t id, revision;
  int32_t arg[3];
  uint8_t op;
  PsuSettings settings;
} PsuRemoteCommand;
/* Mutable ASCII line, bounded by the caller; no floats, heap or JSON parser. */
const char *PsuRemote_Parse(char *line,const PsuSettings *base,PsuRemoteCommand *out);
#endif
