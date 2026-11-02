#include "psu_link.h"
#include "power_stage.h"
#include "analog_monitor.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TX_Q_N 24U
#define TX_SIZE 112U
static volatile uint8_t s_rxi,s_rxo;
static uint8_t s_rx[256];
static char s_line[80];
static uint8_t s_line_n,s_overflow;
static char s_tx[TX_Q_N][TX_SIZE];
static uint8_t s_txi,s_txo,s_txcount;
static PSU_LinkWrite s_write;
static uint32_t s_last_telemetry;
static uint8_t s_link_ready;
static uint8_t s_stream_enabled;

static uint32_t positive_milli(float x) {
    if (!(x>=0.0f) || x>4294900.0f) return 0U;
    return (uint32_t)(x*1000.0f+0.5f);
}
static void queue(const char *line) {
    uint32_t n;
    if (s_txcount==TX_Q_N) return; /* telemetry may drop; never block controls */
    n=strlen(line);
    if (n>TX_SIZE-3U) return;
    memcpy(s_tx[s_txi],line,n);
    s_tx[s_txi][n++]='\n'; s_tx[s_txi][n]='\0';
    s_txi=(uint8_t)((s_txi+1U)%TX_Q_N); ++s_txcount;
}
static void emit_param(const char *k,uint32_t v) {
    char line[96];
    (void)snprintf(line,sizeof(line),"P,%s,%lu",k,(unsigned long)v);queue(line);
}
static void config_report(void) {
    PSU_Tuning c;
    PSU_Status s;
    PowerStage_GetTuning(&c); s=PowerStage_GetStatus();
    emit_param("CV_KP",c.cv_kp_milli); emit_param("CV_KI",c.cv_ki_milli);
    emit_param("CC_KP",c.cc_kp_milli); emit_param("CC_KI",c.cc_ki_milli);
    emit_param("OVP_MV",c.ovp_mv); emit_param("OCP_MA",c.ocp_ma);
    emit_param("VIN_MIN_MV",c.vin_min_mv); emit_param("VIN_MAX_MV",c.vin_max_mv);
    emit_param("VSET_MAX_MV",c.vset_max_mv); emit_param("ISET_MAX_MA",c.iset_max_ma);
    emit_param("DUTY_MAX_PM",c.duty_max_permille);
    emit_param("VSET_MV",(uint32_t)s.vset_cv*10U);
    emit_param("ISET_MA",(uint32_t)s.iset_ca*10U);
    queue("OK,CFG");
}
static void command(char *s) {
    char *key,*number,*end;
    unsigned long value;
    char answer[100];
    size_t n;
    if (strcmp(s,"GET CFG")==0) {config_report();return;}
    if (strcmp(s,"PING")==0) {
        /* New PC session: discard telemetry queued while host was absent. */
        s_txi=s_txo=s_txcount=0U; s_stream_enabled=1U;
        queue("OK,PONG,PSU_V9"); return;
    }
    if (strcmp(s,"GET STATUS")==0) {
        s_stream_enabled=1U;s_last_telemetry=HAL_GetTick()-100U;
        queue("OK,STATUS");return;
    }
    if (strncmp(s,"SET ",4)!=0) {queue("ERR,COMMAND");return;}
    key=s+4;
    number=strchr(key,' ');
    if (!number) {queue("ERR,FORMAT");return;}
    *number++='\0'; n=strlen(key);
    if (!n || n>18U || !*number || strlen(number)>10U) {queue("ERR,FORMAT");return;}
    value=strtoul(number,&end,10);
    if (*end!='\0' || end==number || value>4294967295UL) {queue("ERR,VALUE");return;}
    if (!PowerStage_SetParam(key,(uint32_t)value)) {queue("ERR,RANGE_OR_CONSTRAINT");return;}
    (void)snprintf(answer,sizeof(answer),"OK,%s,%lu",key,value);
    queue(answer);
}
void PSU_Link_Init(PSU_LinkWrite fn) {
    s_write=fn;s_rxi=s_rxo=0U;s_line_n=s_overflow=0U;
    s_txi=s_txo=s_txcount=0U;
    s_last_telemetry=HAL_GetTick();s_stream_enabled=0U;s_link_ready=1U;
}
void PSU_Link_RxBytes(const uint8_t *data,uint32_t len) {
    uint32_t i;uint8_t next;
    if (!s_link_ready || !data) return;
    for(i=0;i<len;i++) {
        next=(uint8_t)(s_rxi+1U);
        if (next==s_rxo) break;
        s_rx[s_rxi]=data[i];s_rxi=next;
    }
}
void PSU_Link_Poll(void) {
    uint8_t c;
    uint32_t now;
    if (!s_link_ready || !s_write) return;
    while(s_rxo!=s_rxi) {
        c=s_rx[s_rxo];s_rxo=(uint8_t)(s_rxo+1U);
        if (c=='\r') continue;
        if (c=='\n') {
            if(s_overflow)queue("ERR,LINE_TOO_LONG");
            else if(s_line_n>0U) {s_line[s_line_n]='\0';command(s_line);}
            s_line_n=0U;s_overflow=0U;
        } else if (c>=32U && c<=126U) {
            if (s_line_n<sizeof(s_line)-1U && !s_overflow) s_line[s_line_n++]=(char)c;
            else s_overflow=1U;
        }
    }
    now=HAL_GetTick();
    if (s_stream_enabled && (uint32_t)(now-s_last_telemetry)>=100U && s_txcount<TX_Q_N-2U) {
        char line[TX_SIZE];
        PSU_Status ps=PowerStage_GetStatus();
        AnalogMonitor_Data adc;
        AnalogMonitor_GetSnapshot(&adc);
        s_last_telemetry=now;
        /* T,uptime_ms,vin_mV,vout_mV,iout_mA,power_mW,VSET_mV,ISET_mA,
             duty_per_mille,state,mode,error,fet_sensor_mV,runtime_seconds */
        (void)snprintf(line,sizeof(line),
              "T,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%u,%u,%u,%lu,%lu",
              (unsigned long)now,
              (unsigned long)positive_milli(ps.vin_v),
              (unsigned long)positive_milli(ps.vout_v),
              (unsigned long)positive_milli(ps.iout_a),
              (unsigned long)positive_milli(ps.vout_v*ps.iout_a),
              (unsigned long)ps.vset_cv*10UL,
              (unsigned long)ps.iset_ca*10UL,
              (unsigned long)positive_milli(ps.duty_applied_pct)*10UL/1000UL,
              (unsigned)ps.state,(unsigned)ps.mode,(unsigned)ps.error,
              (unsigned long)(adc.temp_fet_valid?positive_milli(adc.temp_fet_v):0U),
              (unsigned long)ps.running_seconds);
        queue(line);
    }
    if (s_txcount!=0U) {
        const char *q=s_tx[s_txo];
        if (s_write((const uint8_t *)q,(uint16_t)strlen(q))) {
            s_txo=(uint8_t)((s_txo+1U)%TX_Q_N);--s_txcount;
        }
    }
}
