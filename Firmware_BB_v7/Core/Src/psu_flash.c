#include "psu_flash.h"
#include "stm32f1xx_hal.h"
#include <string.h>
#include <stddef.h>

#define PSU_FLASH_A         0x0800F800UL
#define PSU_FLASH_B         0x0800FC00UL
#define PSU_FLASH_MAGIC     0x50535546UL /* 'PSUF' */
#define PSU_FLASH_VERSION   10UL
#define PSU_FLASH_COMMIT    0xC0DECAFEUL
#define PSU_PAGE_SIZE       1024UL

typedef struct {
    uint32_t magic,version,sequence;
    PSU_Tuning tuning;
    uint32_t vset_mv,iset_ma;
    uint32_t crc,commit;
} PSU_FlashRecord;

/* All words are native-aligned, no padding with F1 AAPCS / uint32_t fields. */
static uint32_t crc32_words(const uint8_t *bytes, uint32_t size) {
    uint32_t i,j,c=0xFFFFFFFFUL;
    for(i=0;i<size;i++) {
        c^=(uint32_t)bytes[i];
        for(j=0;j<8U;j++) c=(c>>1U)^((c&1U)?0xEDB88320UL:0U);
    }
    return ~c;
}
static uint8_t cfg_valid(const PSU_Tuning *t,uint32_t v,uint32_t i) {
    if (t->cv_kp_milli>20000U || t->cv_ki_milli>100000U ||
        t->cc_kp_milli>50000U || t->cc_ki_milli>1000000U ||
        t->ovp_mv<5000U || t->ovp_mv>30000U ||
        t->ocp_ma<500U || t->ocp_ma>6500U ||
        t->vin_min_mv<5000U || t->vin_min_mv>35000U ||
        t->vin_max_mv<10000U || t->vin_max_mv>40000U ||
        t->vset_max_mv<3000U || t->vset_max_mv>28000U ||
        (t->vset_max_mv%10U)!=0U ||
        t->iset_max_ma<100U || t->iset_max_ma>5000U ||
        (t->iset_max_ma%10U)!=0U ||
        t->duty_max_permille<100U || t->duty_max_permille>970U ||
        t->v_rise_mv_s<100U || t->v_rise_mv_s>30000U ||
        t->v_fall_mv_s<100U || t->v_fall_mv_s>30000U ||
        t->vset_max_mv+500U>t->ovp_mv ||
        t->iset_max_ma+200U>t->ocp_ma ||
        t->vin_min_mv+1000U>=t->vin_max_mv ||
        v<3000U || v>t->vset_max_mv || (v%10U)!=0U ||
        i<100U || i>t->iset_max_ma || (i%10U)!=0U) return 0U;
    return 1U;
}
static uint8_t read_page(uint32_t addr,PSU_FlashRecord *r) {
    memcpy(r,(const void *)(uintptr_t)addr,sizeof(*r));
    if (r->magic!=PSU_FLASH_MAGIC || r->version!=PSU_FLASH_VERSION ||
        r->commit!=PSU_FLASH_COMMIT || r->sequence==0xFFFFFFFFUL ||
        r->sequence==0U) return 0U;
    if (crc32_words((const uint8_t*)r,(uint32_t)offsetof(PSU_FlashRecord,crc))!=r->crc) return 0U;
    return cfg_valid(&r->tuning,r->vset_mv,r->iset_ma);
}
static uint8_t latest(PSU_FlashRecord *out,uint32_t *selected) {
    PSU_FlashRecord a,b;
    uint8_t va=read_page(PSU_FLASH_A,&a),vb=read_page(PSU_FLASH_B,&b);
    if (!va && !vb) return 0U;
    if (va && (!vb || (int32_t)(a.sequence-b.sequence)>0)) {
        *out=a;*selected=PSU_FLASH_A;
    } else { *out=b;*selected=PSU_FLASH_B; }
    return 1U;
}
uint8_t PSU_Flash_Load(PSU_Tuning *cfg,uint32_t *v,uint32_t *i) {
    PSU_FlashRecord r;uint32_t addr;
    if (!cfg || !v || !i || !latest(&r,&addr)) return 0U;
    *cfg=r.tuning; *v=r.vset_mv; *i=r.iset_ma;
    return 1U;
}
uint8_t PSU_Flash_Save(const PSU_Tuning *cfg,uint32_t v,uint32_t i) {
    PSU_FlashRecord r,old;
    FLASH_EraseInitTypeDef erase;
    uint32_t addr,active=0U,error=0U,off,half;
    HAL_StatusTypeDef st=HAL_OK;
    if (!cfg || !cfg_valid(cfg,v,i)) return 0U;
    if (latest(&old,&active)) {
        addr=(active==PSU_FLASH_A)?PSU_FLASH_B:PSU_FLASH_A;
    } else { addr=PSU_FLASH_A; }
    memset(&r,0,sizeof(r));
    r.magic=PSU_FLASH_MAGIC;r.version=PSU_FLASH_VERSION;
    /* Sequence increments when switching pages. */
    if (active!=0U) {
        r.sequence=old.sequence+1U;
        if (!r.sequence || r.sequence==0xFFFFFFFFUL) r.sequence=1U;
    } else r.sequence=1U;
    r.tuning=*cfg;r.vset_mv=v;r.iset_ma=i;
    r.crc=crc32_words((const uint8_t*)&r,(uint32_t)offsetof(PSU_FlashRecord,crc));
    r.commit=PSU_FLASH_COMMIT;
    erase.TypeErase=FLASH_TYPEERASE_PAGES;
    erase.PageAddress=addr;
    erase.NbPages=1U;
    if (HAL_FLASH_Unlock()!=HAL_OK) return 0U;
    st=HAL_FLASHEx_Erase(&erase,&error);
    if (st==HAL_OK) {
        /* Program the commit marker last. Only a fully verified record is valid. */
        for(off=0U;off<sizeof(r);off+=2U) {
            half=0U;
            memcpy(&half,((const uint8_t*)&r)+off,2U);
            st=HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,addr+off,(uint64_t)(half&0xFFFFU));
            if (st!=HAL_OK) break;
        }
    }
    (void)HAL_FLASH_Lock();
    if (st!=HAL_OK) return 0U;
    {
        PSU_FlashRecord check;
        return read_page(addr,&check) && check.sequence==r.sequence;
    }
}
