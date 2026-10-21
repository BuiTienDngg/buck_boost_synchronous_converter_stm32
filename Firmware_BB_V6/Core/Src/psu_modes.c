#include "psu_modes.h"
#include <string.h>
#include <math.h>
static void solder_stop(PsuModes *m,PsuControl *p,uint8_t reason){
  Psu_Disable(p);m->solder_request=m->solder_on=0;m->solder.duty=m->solder.command=0;
  m->solder.ramp_ms=m->solder.ramp_limit_pm=m->solder.on_ms=0;
  m->solder.cycle_ready=0;
  m->solder.phase=reason?PSU_SOL_ERROR:PSU_SOL_OFF;m->solder.error=reason;
  m->solder.integral=m->solder.derivative=0;m->solder.pid_ready=0;m->solder.watch_ms=0;
}
static int solder_healthy(const PsuControl *p){
  return !p->fault && !p->config_errors && p->initialized && p->adc_age_ms<3 && p->temp_valid && p->temp_age_ms<200 &&
    p->temp_c<p->settings.temp_trip && p->vin_raw>=p->settings.vin_min && p->vin_raw<p->settings.vin_max;
}
/* Temporary user calibration, shared by both tip selections:
   ADC 1193 = 29 C, slope 0.5 C per ADC count. */
static float solder_temperature(float raw,uint8_t tip){
  (void)tip;
  return 29.0f+(raw-1193.0f)/2.0f;
}
void PsuSolder_SetTarget(PsuModes *m,int32_t degrees){
  m->solder.target=(uint16_t)(degrees<150?150:degrees>450?450:degrees);
}
static void solder_begin(PsuModes *m,uint32_t now){
  PsuSolder *s=&m->solder;
  m->discharge_inhibit=1;m->solder_request=1;m->solder_on=0;
  s->phase=PSU_SOL_ROUTE;s->error=s->valid=s->count=0;s->sum=0;s->trip_adc=0;
  s->started_ms=s->frame_ms=s->last_sample_ms=now;s->duty=s->command=s->integral=s->derivative=0;
  s->watch_ms=0;s->sleeping=s->stand_count=s->pid_ready=0;s->active_target=s->target;s->hot_ms=0;
  s->route_ms=now;s->heat_started_ms=now;s->ramp_ms=s->ramp_limit_pm=0;
  s->sample_ms=s->last_heat_ms=now;s->on_ms=s->position=0;s->cycle_ready=0;
}
void PsuSolder_Sample(PsuModes *m,uint16_t raw,uint32_t now){
  PsuSolder *s=&m->solder;
  s->raw=raw;
  if(m->mode!=PSU_MODE_SOLDER)return;
#if PSU_SOLDER_TEMP_PROTECTION
  float t=solder_temperature(raw,s->tip);
  /* Do not clamp bad readings before testing sensor/overtemperature. */
  if(raw<8 || raw>4087 || t < -30 || t>500){
    if(!s->error){s->trip_adc=raw;s->error=t>500?PSU_SOL_HOT:PSU_SOL_SENSOR;}
    s->valid=0;s->duty=0;return;
  }
#endif
  (void)now;
  if(s->count<64){s->sum+=raw;++s->count;}
}
static float solder_fraction(const PsuSolder *s){
  float u=PSU_SOLDER_KP*(s->active_target-s->temperature);
  return u<0?0:u>PSU_SOLDER_MAX_DUTY?PSU_SOLDER_MAX_DUTY:u;
}
/* Exactly one thermal PID update per 50 ms frame, after the quiet mean.
   D acts on measured temperature, avoiding a target-change derivative kick.
   Conditional integration prevents windup at either duty rail. */
static float solder_pid(PsuSolder *s){
  const float dt=PSU_SOLDER_FRAME_MS*0.001f;
  float error=s->active_target-s->temperature;
  if(!s->pid_ready){s->derivative=0;s->pid_ready=1;}
  else{
    s->derivative+=0.25f*((s->temperature-s->previous_temperature)/dt-s->derivative);
    float u=PSU_SOLDER_KP*error+s->integral-PSU_SOLDER_KD*s->derivative;
    if((error>0 && u<PSU_SOLDER_MAX_DUTY) || (error<0 && u>0))s->integral+=PSU_SOLDER_KI*dt*error;
    if(s->integral<0)s->integral=0;
    if(s->integral>1)s->integral=1;
  }
  s->previous_temperature=s->temperature;
  float u=PSU_SOLDER_KP*error+s->integral-PSU_SOLDER_KD*s->derivative;
  return u<0?0:u>PSU_SOLDER_MAX_DUTY?PSU_SOLDER_MAX_DUTY:u;
}
static void solder_tick(PsuModes *m,PsuControl *p,uint32_t now){
  PsuSolder *s=&m->solder;
  float voltage=p->settings.solder_voltage;
  float current=p->settings.solder_current;
  uint8_t was_heating=s->duty>0;
  if(was_heating){
    s->last_heat_ms=now;
    if(s->temperature<s->active_target-50 && s->command>0.5f && s->watch_ms<60000)++s->watch_ms;
  }
  s->duty=0;
  if(s->error && m->solder_request){solder_stop(m,p,s->error);return;}
  /* Process quiet means at 5 ms independently of the 50 ms frame.
     Retain the last quiet reading during an intentional ON interval. */
  if(now-s->sample_ms>=PSU_SOLDER_SAMPLE_MS){
    s->sample_ms=now;
    if(s->count>=2){
      s->temperature=solder_temperature((float)s->sum/s->count,s->tip);
      s->valid=1;s->last_sample_ms=now;
#if PSU_SOLDER_TEMP_PROTECTION
      if(s->temperature>=s->baseline+5 || s->temperature>=s->active_target-50){s->baseline=s->temperature;s->watch_ms=0;}
      if(m->solder_request && s->watch_ms>=15000){solder_stop(m,p,PSU_SOL_NO_RISE);return;}
      if(s->temperature>s->target+60){
        s->hot_ms+=PSU_SOLDER_SAMPLE_MS;
        if(m->solder_request && s->hot_ms>=1000){solder_stop(m,p,PSU_SOL_HOT);return;}
      }else s->hot_ms=0;
#endif
    }
    s->sum=0;s->count=0;
    if(!s->stand){s->stand_count=0;s->sleeping=0;s->stand_ms=now;}
    else if(s->stand_count<PSU_SOLDER_STAND_SAMPLES){if(++s->stand_count==PSU_SOLDER_STAND_SAMPLES){s->sleeping=1;s->stand_ms=now;}}
    uint16_t target=s->sleeping?s->sleep_target:s->target;
    if(target!=s->active_target){
      s->active_target=target;s->watch_ms=0;s->baseline=s->temperature;
      s->integral=s->derivative=0;s->pid_ready=0;
      /* A lower target may shorten this interval. Never extend/restart ON
         from an old reading after changing a target or leaving the stand. */
      float reduced=solder_fraction(s);
      if(reduced<s->command){s->command=reduced;s->on_ms=reduced>0?PSU_SOLDER_HEAT_MAX_MS:0;}
    }
  }
  if(!m->solder_request){s->phase=s->error?PSU_SOL_ERROR:PSU_SOL_OFF;return;}
  if(!solder_healthy(p)){solder_stop(m,p,PSU_SOL_HEALTH);return;}
  /* Only a scheduled, continuously renewed ON interval may retain a
     temperature older than 200 ms. Once OFF, quiet ADC must resume. */
  if(!was_heating && now-s->last_heat_ms>=200 &&
     now-s->last_sample_ms>=(s->phase==PSU_SOL_ROUTE?250u:200u)){
    s->valid=0;solder_stop(m,p,PSU_SOL_STALE);return;
  }
  if(s->sleeping && now-s->stand_ms>=600000u){solder_stop(m,p,PSU_SOL_OK);return;}
  if(s->phase==PSU_SOL_ROUTE){
    if(now-s->started_ms>=30000){solder_stop(m,p,PSU_SOL_RAIL_TIMEOUT);return;}
    /* Do not route a charged LAB capacitor above the solder profile. */
    if(now-s->started_ms>=100 && p->vout_raw<=voltage+0.5f && !m->solder_on){m->solder_on=1;s->route_ms=now;}
  }
  if(s->phase==PSU_SOL_ROUTE && m->solder_on && now-s->route_ms>=200 && s->valid){
    if(voltage>p->settings.vout_max || current>p->settings.i_max){solder_stop(m,p,PSU_SOL_CONFIG);return;}
    /* A previous 24 V rail must decay before selecting a 12 V cartridge.
       Never heat the tip to discharge that capacitor. */
    if(p->vout_raw>voltage+0.5f){if(now-s->started_ms>=30000)solder_stop(m,p,PSU_SOL_RAIL_TIMEOUT);return;}
    Psu_Setpoint(p,voltage,current);
    if(!Psu_Enable(p)){solder_stop(m,p,PSU_SOL_HEALTH);return;}
    s->phase=PSU_SOL_PRECHARGE;s->rail_ms=0;
  }
  if(s->phase==PSU_SOL_PRECHARGE){
    if(!p->enabled){solder_stop(m,p,PSU_SOL_HEALTH);return;}
    if(now-s->started_ms>=30000){solder_stop(m,p,PSU_SOL_RAIL_TIMEOUT);return;}
    if(p->vout_raw>=voltage*0.95f && p->vout_raw<=voltage+0.5f && p->reference_v>=voltage-0.1f && !p->precharge_hold){
      if(++s->rail_ms>=100){
        s->phase=PSU_SOL_RUN;s->frame_ms=s->heat_started_ms=now;s->position=0;
        s->ramp_ms=s->ramp_limit_pm=s->on_ms=0;s->command=0;s->cycle_ready=0;
        s->baseline=s->temperature;s->watch_ms=0;s->last_heat_ms=now;
      }
    }else s->rail_ms=0;
  }
  if(s->phase!=PSU_SOL_RUN)return;
  if(!p->enabled){solder_stop(m,p,PSU_SOL_HEALTH);return;}
  if(p->vout_raw<voltage*0.7f){
    if(++s->rail_ms>=2000){solder_stop(m,p,PSU_SOL_RAIL_TIMEOUT);return;}
    /* Recover through a new 10 ms quiet phase; never rejoin an ON pulse. */
    s->frame_ms=now;s->on_ms=s->position=0;s->cycle_ready=0;s->command=0;
    s->integral=s->derivative=0;s->pid_ready=0;
    return;
  }else s->rail_ms=0;
  if(now-s->frame_ms>=PSU_SOLDER_FRAME_MS){
    s->frame_ms=now;s->on_ms=0;s->cycle_ready=0;s->command=0;
    s->sum=0;s->count=0;
  }
  s->position=(uint16_t)(now-s->frame_ms);
  if(s->position<PSU_SOLDER_QUIET_MS)return;
  if(!s->cycle_ready){
    /* Latch exactly once from a recent quiet reading of this frame. */
    if(!s->valid || now-s->last_sample_ms>=200 || s->last_sample_ms==s->frame_ms ||
       s->last_sample_ms-s->frame_ms>=PSU_SOLDER_FRAME_MS){
      s->valid=0;solder_stop(m,p,PSU_SOL_STALE);return;
    }
    s->command=solder_pid(s);
    s->on_ms=s->command>0?PSU_SOLDER_HEAT_MAX_MS:0;
    s->cycle_ready=1;
    /* Do not publish a leftover partial quiet mean after heater PWM starts. */
    s->sum=0;s->count=0;s->sample_ms=now;
  }
  /* 10 kHz carrier duty during the heat window; 10 ms quiet remains fully OFF. */
  if(s->valid && s->position<PSU_SOLDER_QUIET_MS+s->on_ms)s->duty=s->command;
}
/* Pack totals only. A correctly rated external BMS supplies per-cell
   protection/balancing. There is no battery temperature input on this board.
   Never infer series cell count from total pack voltage. */
static float full(const PsuModes *m){return PsuModes_FullMillivolts(m)/1000.0f;}
static float minimum(const PsuModes *m){return (m->chemistry==PSU_CELL_LIFEPO4?2.0f:2.5f)*m->cells;}
static float pre_voltage(const PsuModes *m){return (m->chemistry==PSU_CELL_LIFEPO4?2.8f:3.0f)*m->cells;}
static int healthy(const PsuControl *p){return !p->fault && !p->config_errors && p->initialized && p->ready_ms>=100 && p->adc_age_ms<3 && p->temp_valid && p->temp_age_ms<200;}
uint16_t PsuModes_FullMillivolts(const PsuModes *m){return (uint16_t)((m->chemistry==PSU_CELL_LIFEPO4?3650u:4200u)*m->cells);}
uint16_t PsuModes_EndMilliamps(const PsuModes *m){uint16_t ma=m->current_ma/10u;return ma<20?20:ma;}
void PsuModes_Init(PsuModes *m){memset(m,0,sizeof(*m));m->cells=1;m->hours=6;m->current_ma=500;m->lab_mv=5000;m->lab_ma=1000;m->solder.target=300;m->solder.sleep_target=200;}
int PsuModes_Busy(const PsuModes *m,const PsuControl *p){return p->enabled || m->phase==PSU_CH_CHECK || m->solder_request;}
static void finish(PsuModes *m,PsuControl *p,uint8_t phase,uint8_t reason){Psu_Disable(p);m->phase=phase;m->error=reason;m->tail_ms=0;m->solder_request=m->solder_on=0;}
void PsuModes_Stop(PsuModes *m,PsuControl *p){
  if(m->mode==PSU_MODE_SOLDER){solder_stop(m,p,PSU_SOL_OK);return;}
  if(m->mode==PSU_MODE_CHARGE && (m->phase==PSU_CH_CHECK || p->enabled))finish(m,p,PSU_CH_STOP,PSU_CH_OK);
  else {Psu_Disable(p);m->solder_request=m->solder_on=0;}
}
void PsuModes_SetCurrent(PsuModes *m,uint16_t ma){if(m->current_ma!=ma){m->current_ma=ma;m->tail_ms=0;}}
int PsuModes_Select(PsuModes *m,PsuControl *p,uint8_t mode,uint32_t now){
  if(mode>PSU_MODE_SOLDER || PsuModes_Busy(m,p))return 0;
  if(m->mode==PSU_MODE_LAB){m->lab_mv=(uint16_t)(p->voltage_set*1000+0.5f);m->lab_ma=(uint16_t)(p->current_set*1000+0.5f);}
  Psu_Disable(p);m->mode=mode;m->changed_ms=now;m->phase=PSU_CH_IDLE;m->error=0;m->run_seconds=m->run_fraction=0;m->was_active=0;
  if(mode==PSU_MODE_CHARGE){
    m->discharge_inhibit=1;
    if(m->current_ma>p->settings.i_max*1000)m->current_ma=(uint16_t)(p->settings.i_max*1000);
    Psu_Setpoint(p,full(m),m->current_ma/1000.0f);
  } else if(mode==PSU_MODE_LAB){
    float v=m->lab_mv/1000.0f,i=m->lab_ma/1000.0f;
    Psu_Setpoint(p,v>p->settings.vout_max?p->settings.vout_max:v,i>p->settings.i_max?p->settings.i_max:i);
  } else solder_begin(m,now);
  return 1;
}
static void target(PsuModes *m,PsuControl *p){
  float v=m->phase==PSU_CH_PRE?pre_voltage(m):full(m),i=m->current_ma/1000.0f;
  if(m->phase==PSU_CH_PRE){i*=0.1f;if(i>0.2f)i=0.2f;if(i<0.05f)i=0.05f;}
  if(p->voltage_set!=v || p->current_set!=i)Psu_Setpoint(p,v,i);
}
int PsuModes_Start(PsuModes *m,PsuControl *p,uint32_t now){
  if(PsuModes_Busy(m,p))return 0;
  if(now-m->changed_ms<100){m->error=PSU_CH_SWITCH;return 0;}
  if(m->mode==PSU_MODE_LAB){int ok=Psu_Enable(p);if(ok){m->was_active=0;m->run_seconds=m->run_fraction=0;}return ok;}
  if(!healthy(p)){m->error=PSU_CH_HEALTH;return 0;}
  if(m->mode==PSU_MODE_SOLDER){solder_begin(m,now);m->was_active=0;m->run_seconds=m->run_fraction=0;return 1;}
  if(m->cells<1 || m->cells>6 || m->chemistry>PSU_CELL_LIFEPO4 || m->hours<1 || m->hours>12 ||
     m->current_ma<100 || m->current_ma>p->settings.i_max*1000 || full(m)>p->settings.vout_max){finish(m,p,PSU_CH_ERROR,PSU_CH_CONFIG);return 0;}
  if(p->vout_raw<minimum(m)){finish(m,p,PSU_CH_ERROR,PSU_CH_LOW);return 0;}
  if(p->vout_raw>full(m)+0.03f*m->cells){finish(m,p,PSU_CH_ERROR,PSU_CH_HIGH);return 0;}
  m->discharge_inhibit=1;m->phase=PSU_CH_CHECK;m->error=0;m->started_ms=m->phase_ms=now;
  m->tail_ms=m->pre_ready_ms=m->low_ms=m->mah_d10=m->charge_integral=0;m->current_seen=0;
  m->near_full=p->vout_raw>=full(m)-0.05f*m->cells;m->run_seconds=m->run_fraction=0;m->was_active=0;
  /* Tight raw OVP is applied by the ADC producer before every fast sample.
     The stock lab OVP envelope and tuned PI loops are unchanged. */
  float extra=m->cells*0.03f;if(extra<0.05f)extra=0.05f;
  m->ovp_adc=(uint16_t)((full(m)+extra)*4095.0f/(p->settings.vdda*PSU_VOUT_DIVIDER*p->settings.vout_cal));
  return 1;
}
void PsuModes_Tick(PsuModes *m,PsuControl *p,uint32_t now){
  uint8_t running_before=p->enabled || m->solder_request;
  if(m->mode==PSU_MODE_LAB && !p->enabled && p->initialized && p->adc_age_ms<3 && p->vout_raw<1.5f){
    if(m->low_output_ms<2000)++m->low_output_ms;else m->discharge_inhibit=0;
  } else m->low_output_ms=0;
  if(m->mode==PSU_MODE_SOLDER){
    solder_tick(m,p,now);
    if(!m->solder_request || !solder_healthy(p))m->solder_on=0;
  }
  if(m->mode==PSU_MODE_CHARGE && m->phase==PSU_CH_CHECK){
    if(!healthy(p))finish(m,p,PSU_CH_ERROR,PSU_CH_HEALTH);
    else if(p->vout_raw<minimum(m))finish(m,p,PSU_CH_ERROR,PSU_CH_LOW);
    else if(p->vout_raw>full(m)+0.03f*m->cells)finish(m,p,PSU_CH_ERROR,PSU_CH_HIGH);
    else if(now-m->phase_ms>=1000){
      m->phase=p->vout_raw<pre_voltage(m)?PSU_CH_PRE:PSU_CH_CCCV;m->phase_ms=now;target(m,p);
      if(!Psu_Enable(p))finish(m,p,PSU_CH_ERROR,PSU_CH_HEALTH);
    }
  }
  if(m->mode==PSU_MODE_CHARGE && (m->phase==PSU_CH_PRE || m->phase==PSU_CH_CCCV)){
    if(!p->enabled)finish(m,p,PSU_CH_ERROR,PSU_CH_HEALTH);
    else if(now-m->started_ms>=(uint32_t)m->hours*3600000u)finish(m,p,PSU_CH_ERROR,PSU_CH_TIMEOUT);
    else {
      target(m,p);
      unsigned ma=(unsigned)(p->current>0?p->current*1000+0.5f:0);if(ma>20000)ma=20000;
      m->charge_integral+=ma;if(m->charge_integral>=360000u){m->charge_integral-=360000u;++m->mah_d10;}
      float seen=m->current_ma*0.0002f;if(seen>0.05f)seen=0.05f;
      if(p->current>=seen)m->current_seen=1;
      if(p->vout_raw<minimum(m)){if(++m->low_ms>=50)finish(m,p,PSU_CH_ERROR,PSU_CH_LOW);}else m->low_ms=0;
      if(m->phase==PSU_CH_PRE){
        if(now-m->phase_ms>=1800000u)finish(m,p,PSU_CH_ERROR,PSU_CH_PRE_TIMEOUT);
        else if(now-m->phase_ms>=120000u && !m->current_seen)finish(m,p,PSU_CH_ERROR,PSU_CH_NO_CURRENT);
        else if(p->vout>=pre_voltage(m)-0.02f*m->cells){if(++m->pre_ready_ms>=5000){m->phase=PSU_CH_CCCV;m->phase_ms=now;target(m,p);}}else m->pre_ready_ms=0;
      } else if(m->phase==PSU_CH_CCCV){
        float end=PsuModes_EndMilliamps(m)*0.001f;
        if(now-m->phase_ms>=30000u && !m->current_seen && !m->near_full)finish(m,p,PSU_CH_ERROR,PSU_CH_NO_CURRENT);
        else if((m->current_seen || m->near_full) && p->state==PSU_CV && !p->precharge_hold &&
                p->vout>=full(m)-0.03f*m->cells && p->vout_raw>=full(m)-0.04f*m->cells &&
                p->vout_raw<=full(m)+0.02f*m->cells && p->current<=end && p->current_raw<=end*1.3f && p->current_limit>=end*1.5f){
          if(++m->tail_ms>=60000u)finish(m,p,PSU_CH_FULL,PSU_CH_OK);
        } else m->tail_ms=0;
      }
    }
  }
  uint8_t active=p->enabled || m->solder_request;
  if(active || running_before){
    if(!m->was_active)m->run_seconds=m->run_fraction=0;
    if(++m->run_fraction>=1000){m->run_fraction=0;if(m->run_seconds<0xFFFFFFFFu)++m->run_seconds;}
  }
  m->was_active=active;
}
