#ifndef PSU_SETTINGS_H
#define PSU_SETTINGS_H
#include <stddef.h>
#include <stdint.h>
#include "psu_config.h"
typedef struct {
#define PSU_FIELD(name,scale,lo,hi,value) float name;
#include "psu_settings_fields.def"
#undef PSU_FIELD
} PsuSettings;
void PsuSettings_Defaults(PsuSettings *s);
/* Returns NULL for a valid, mutually consistent configuration. */
const char *PsuSettings_Validate(const PsuSettings *s);
int PsuSettings_SetScaled(PsuSettings *s, const char *name, int32_t value);
size_t PsuSettings_Json(const PsuSettings *s, char *out, size_t capacity);
#endif
