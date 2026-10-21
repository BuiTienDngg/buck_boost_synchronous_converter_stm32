#include "psu_app.h"
#include <string.h>
#define SLOT0 0x0800F800u
#define SLOT1 0x0800FC00u
#define OLD_MAGIC 0x42503631u
#define V2_MAGIC 0x42503632u
#define V3_MAGIC 0x42503633u
#define MAGIC 0x42503634u
typedef struct { uint32_t sequence;PsuPreset presets[3];uint32_t crc,commit; } Legacy;
typedef struct { uint32_t sequence;PsuPreset presets[3];uint32_t version;float settings[36];uint32_t crc,commit; } RecordV2;
typedef char V2PrefixMatches[(offsetof(PsuSettings,ramp_v_per_s)==36u*sizeof(float))?1:-1];
typedef struct { uint32_t sequence;PsuPreset presets[3];uint32_t version;float settings[37];uint32_t crc,commit; } RecordV3;
typedef char V3PrefixMatches[(offsetof(PsuSettings,solder_voltage)==37u*sizeof(float))?1:-1];
typedef struct { uint32_t sequence;PsuPreset presets[3];uint32_t version;PsuSettings settings;uint32_t crc,commit; } Record;
static uint32_t crc32(const void *data,uint32_t n) {
 const uint8_t *p=data;uint32_t c=0xFFFFFFFFu,b;
 while(n--){c^=*p++;for(b=0;b<8;++b)c=(c>>1)^((0u-(c&1u))&0xEDB88320u);}return ~c;
}
static int presets_valid(const PsuPreset p[3],float vmax,float imax) {
 for(unsigned k=0;k<3;++k)if(p[k].millivolts<1500 || p[k].millivolts>vmax*1000.0f+0.5f ||
  p[k].milliamps<50 || p[k].milliamps>imax*1000.0f+0.5f)return 0;
 return 1;
}
static int valid(const Record *r) {
 const Legacy *old=(const Legacy *)r;
 const RecordV2 *v2=(const RecordV2 *)r;
 const RecordV3 *v3=(const RecordV3 *)r;
 if(old->commit==OLD_MAGIC && old->crc==crc32(old,16) && presets_valid(old->presets,36,15))return 1;
 if(v2->commit==V2_MAGIC && v2->version==2 && v2->crc==crc32(v2,offsetof(RecordV2,crc))) {
  PsuSettings expanded;PsuSettings_Defaults(&expanded);memcpy(&expanded,v2->settings,sizeof(v2->settings));
  if(!PsuSettings_Validate(&expanded) && presets_valid(v2->presets,expanded.vout_max,expanded.i_max))return 2;
 }
 if(v3->commit==V3_MAGIC && v3->version==3 && v3->crc==crc32(v3,offsetof(RecordV3,crc))) {
  PsuSettings expanded;PsuSettings_Defaults(&expanded);memcpy(&expanded,v3->settings,sizeof(v3->settings));
  if(!PsuSettings_Validate(&expanded) && presets_valid(v3->presets,expanded.vout_max,expanded.i_max))return 3;
 }
 return r->commit==MAGIC && r->version==4 && r->crc==crc32(r,offsetof(Record,crc)) &&
  !PsuSettings_Validate(&r->settings) && presets_valid(r->presets,r->settings.vout_max,r->settings.i_max) ? 4 : 0;
}
static const Record *latest(void) {
 const Record *a=(const Record *)SLOT0,*b=(const Record *)SLOT1;
 if(!valid(a))return valid(b)?b:0;
 if(!valid(b))return a;
 return (int32_t)(b->sequence-a->sequence)>0?b:a;
}
int PsuStorage_LoadAll(PsuPreset p[3],PsuSettings *settings) {
 const Record *r=latest();if(!r)return 0;
 memcpy(p,r->presets,sizeof(r->presets));
 int version=valid(r);
 if(version==4)*settings=r->settings;
 else {
  PsuSettings_Defaults(settings);
  if(version==2)memcpy(settings,((const RecordV2 *)r)->settings,sizeof(((const RecordV2 *)r)->settings));
  if(version==3)memcpy(settings,((const RecordV3 *)r)->settings,sizeof(((const RecordV3 *)r)->settings));
 }
 return 1;
}
int PsuStorage_SaveAll(const PsuPreset p[3],const PsuSettings *settings) {
 const Record *old=latest();Record next;uint32_t error,at,offset;
 FLASH_EraseInitTypeDef erase={0};HAL_StatusTypeDef status;
 if(PsuSettings_Validate(settings) || !presets_valid(p,settings->vout_max,settings->i_max))return 0;
 at=old==(const Record *)SLOT0?SLOT1:SLOT0;
 memset(&next,0xFF,sizeof(next));next.sequence=old?old->sequence+1:1;
 memcpy(next.presets,p,sizeof(next.presets));next.version=4;next.settings=*settings;
 next.crc=crc32(&next,offsetof(Record,crc));next.commit=MAGIC;
 if(HAL_FLASH_Unlock()!=HAL_OK)return 0;
 erase.TypeErase=FLASH_TYPEERASE_PAGES;erase.PageAddress=at;erase.NbPages=1;
 status=HAL_FLASHEx_Erase(&erase,&error);
 for(offset=0;status==HAL_OK && offset<sizeof(next);offset+=2){
  uint16_t word;memcpy(&word,(const uint8_t *)&next+offset,2);
  status=HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,at+offset,word);
 }
 HAL_FLASH_Lock();return status==HAL_OK && valid((const Record *)at);
}
/* Compatibility API for older callers and journal regression fixtures. */
int PsuStorage_Load(PsuPreset p[3]){PsuSettings s;return PsuStorage_LoadAll(p,&s);}
int PsuStorage_Save(const PsuPreset p[3]){PsuSettings s;PsuSettings_Defaults(&s);return PsuStorage_SaveAll(p,&s);}
