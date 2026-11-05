#include "psu_remote.h"
#include <string.h>
static char *word(char **at) {
  char *s=*at,*start;
  while(*s==' ' || *s=='\t' || *s=='\r')++s;
  if(!*s){*at=s;return 0;}start=s;
  while(*s && *s!=' ' && *s!='\t' && *s!='\r')++s;
  if(*s)*s++=0;
  *at=s;return start;
}
static int integer(const char *s,int32_t *out) {
  uint32_t v=0,negative=0,maximum=2147483647u;
  if(!s || !*s)return 0;
  if(*s=='-'){negative=1;++s;maximum=2147483648u;}
  if(!*s)return 0;
  while(*s){unsigned d=(unsigned)(*s++-'0');if(d>9 || v>(maximum-d)/10u)return 0;v=v*10u+d;}
  *out=negative?(int32_t)(0u-v):(int32_t)v;return 1;
}
const char *PsuRemote_Parse(char *line,const PsuSettings *base,PsuRemoteCommand *out) {
  char *at=line,*id=word(&at),*op;int32_t n;
  memset(out,0,sizeof(*out));
  if(!id || *id++!='@' || !integer(id,&n) || n<=0)return "bad_id";
  out->id=(uint32_t)n;op=word(&at);if(!op)return "syntax";
  if(!strcmp(op,"GET"))out->op=REM_GET;
  else if(!strcmp(op,"ON"))out->op=REM_ON;
  else if(!strcmp(op,"OFF"))out->op=REM_OFF;
  else if(!strcmp(op,"CLEAR"))out->op=REM_CLEAR;
  else if(!strcmp(op,"SAVE"))out->op=REM_SAVE;
  else if(!strcmp(op,"SET") || !strcmp(op,"PRESET") || !strcmp(op,"HEAT")) {
    out->op=!strcmp(op,"SET")?REM_SET:!strcmp(op,"HEAT")?REM_HEAT:REM_PRESET;
    unsigned count=out->op==REM_PRESET?3:2;
    for(unsigned k=0;k<count;++k)if(!integer(word(&at),&out->arg[k]))return "syntax";
  } else if(!strcmp(op,"CFG") || !strcmp(op,"DEFAULTS")) {
    out->op=!strcmp(op,"CFG")?REM_CONFIG:REM_DEFAULTS;
    if(!integer(word(&at),&n) || n<0)return "bad_revision";
    out->revision=(uint32_t)n;
    if(out->op==REM_DEFAULTS)PsuSettings_Defaults(&out->settings);
    else {
      char *pair;unsigned count=0;uint64_t seen=0;
      out->settings=*base;
      while((pair=word(&at))!=0) {
        char *equal=strchr(pair,'=');
        if(!equal || equal==pair || !integer(equal+1,&n))return "syntax";
        *equal=0;
        int index=PsuSettings_SetScaled(&out->settings,pair,n);
        if(!index)return "invalid_setting";
        uint64_t bit=1ULL<<(index-1);
        if(seen&bit)return "duplicate_setting";
        seen|=bit;
        ++count;
      }
      if(!count)return "empty_config";
      return PsuSettings_Validate(&out->settings);
    }
  } else return "unknown_command";
  return word(&at)?"extra_argument":0;
}
