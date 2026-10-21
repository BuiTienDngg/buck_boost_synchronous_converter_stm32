#include "psu_settings.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct { const char *name; uint16_t offset; int32_t scale; float lo, hi; } Field;
static const Field fields[] = {
#define PSU_FIELD(name,scale,lo,hi,value) {#name,offsetof(PsuSettings,name),scale,lo,hi},
#include "psu_settings_fields.def"
#undef PSU_FIELD
};
void PsuSettings_Defaults(PsuSettings *s) {
#define PSU_FIELD(name,scale,lo,hi,value) s->name=(float)(value);
#include "psu_settings_fields.def"
#undef PSU_FIELD
}
static float get(const PsuSettings *s,const Field *f) {
  float v; memcpy(&v,(const uint8_t *)s+f->offset,sizeof(v)); return v;
}
const char *PsuSettings_Validate(const PsuSettings *s) {
  for(unsigned k=0;k<sizeof(fields)/sizeof(fields[0]);++k) {
    float v=get(s,&fields[k]);
    if(!isfinite(v) || v<fields[k].lo || v>fields[k].hi) return fields[k].name;
  }
  if(s->vin_min+1.0f>s->vin_max || s->vin_warn<s->vin_min || s->vin_warn>s->vin_max) return "vin_order";
  if(s->i_peak<=s->i_max) return "current_order";
  if(s->temp_reset>=s->temp_warn || s->temp_warn>=s->temp_trip) return "temperature_order";
  if((s->ntc_to_gnd!=0 && s->ntc_to_gnd!=1) ||
     (s->discharge_enabled!=0 && s->discharge_enabled!=1)) return "boolean";
  if(s->discharge_on_us!=(uint32_t)s->discharge_on_us ||
     s->discharge_interval!=(uint32_t)s->discharge_interval ||
     s->discharge_timeout!=(uint32_t)s->discharge_timeout) return "integer";
  if(s->i_offset+s->i_peak*1.05f*PSU_SHUNT_OHM*PSU_CURRENT_GAIN*PSU_CURRENT_ATTENUATION/s->i_cal>s->vdda*0.95f) return "current_adc_range";
  /* Validate the selected operating envelope so legacy lower-voltage
     profiles remain valid, while an expanded profile needs ADC headroom. */
  float ovp=s->vout_max*(1.0f+s->ovp_percent*0.01f)+s->ovp_offset;
  if(ovp>PSU_OUTPUT_OVP_ABS_V)ovp=PSU_OUTPUT_OVP_ABS_V;
  if(s->vdda*0.95f*PSU_VIN_DIVIDER*s->vin_cal<s->vin_max+2.0f ||
     s->vdda*0.95f*PSU_VOUT_DIVIDER*s->vout_cal<ovp+1.0f) return "voltage_adc_range";
  return 0;
}
int PsuSettings_SetScaled(PsuSettings *s,const char *name,int32_t value) {
  for(unsigned k=0;k<sizeof(fields)/sizeof(fields[0]);++k) if(!strcmp(name,fields[k].name)) {
    float v=(float)value/(float)fields[k].scale;
    if(v<fields[k].lo || v>fields[k].hi) return 0;
    memcpy((uint8_t *)s+fields[k].offset,&v,sizeof(v)); return (int)k+1;
  }
  return 0;
}
size_t PsuSettings_Json(const PsuSettings *s,char *out,size_t capacity) {
  size_t used=0;
  if(capacity<3) return 0;
  out[used++]='{';
  for(unsigned k=0;k<sizeof(fields)/sizeof(fields[0]);++k) {
    float scaled=get(s,&fields[k])*fields[k].scale;
    long v=(long)(scaled+(scaled<0?-0.5f:0.5f));
    int n=snprintf(out+used,capacity-used,"%s\"%s\":%ld",k?",":"",fields[k].name,v);
    if(n<0 || (size_t)n>=capacity-used) return 0;
    used+=(size_t)n;
  }
  if(used+2>capacity) return 0;
  out[used++]='}';out[used]=0;return used;
}
