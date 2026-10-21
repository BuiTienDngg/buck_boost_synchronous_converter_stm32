"""Generate the PC schema from the exact field definitions compiled by firmware."""
from pathlib import Path
import json,re
ROOT=Path(__file__).resolve().parents[1]
META={
'cv_kp':('PID điện áp CV','Kp điện áp','ratio/V',5),
'cv_ki':('PID điện áp CV','Ki điện áp','ratio/V/s',4),
'cv_kd':('PID điện áp CV','Kd điện áp','ratio·s/V',6),
'cc_kp':('PID dòng CC','Kp dòng','ratio/A',5),
'cc_ki':('PID dòng CC','Ki dòng','ratio/A/s',4),
'cc_kd':('PID dòng CC','Kd dòng','ratio·s/A',6),
'ramp_v_per_s':('Ramp điện áp','Tốc độ tăng áp','V/s',2),
'solder_voltage':('Tay hàn','Điện áp cấp tay hàn','V',2),
'solder_current':('Tay hàn','Giới hạn dòng tay hàn','A',2),
'vin_min':('Giới hạn điện áp','VIN tối thiểu','V',2),
'vin_max':('Giới hạn điện áp','VIN tối đa','V',2),
'vin_warn':('Giới hạn điện áp','Cảnh báo VIN thấp','V',2),
'vout_max':('Giới hạn điện áp','VOUT tối đa','V',2),
'ovp_percent':('Giới hạn điện áp','OVP vượt setpoint','%',2),
'ovp_offset':('Giới hạn điện áp','OVP cộng thêm','V',3),
'i_max':('Dòng và công suất','Dòng liên tục tối đa','A',2),
'i_peak':('Dòng và công suất','Ngưỡng dòng đỉnh','A',2),
'power_max':('Dòng và công suất','Công suất tối đa','W',2),
'input_current_max':('Dòng và công suất','Ngân sách dòng vào','A',2),
'efficiency':('Dòng và công suất','Hiệu suất ước tính','',3),
'temp_warn':('Nhiệt độ và NTC','Bắt đầu giảm dòng','°C',1),
'temp_trip':('Nhiệt độ và NTC','Ngắt quá nhiệt','°C',1),
'temp_reset':('Nhiệt độ và NTC','Cho phép xóa lỗi dưới','°C',1),
'ntc_r25':('Nhiệt độ và NTC','NTC tại 25 °C','Ω',0),
'ntc_pull':('Nhiệt độ và NTC','Điện trở kéo NTC','Ω',0),
'ntc_beta':('Nhiệt độ và NTC','Hệ số B NTC','K',0),
'ntc_to_gnd':('Nhiệt độ và NTC','NTC nối về GND','0/1',0),
'vdda':('Hiệu chuẩn ADC','Điện áp VDDA thực','V',3),
'vin_cal':('Hiệu chuẩn ADC','Hệ số VIN','×',6),
'vout_cal':('Hiệu chuẩn ADC','Hệ số VOUT','×',6),
'i_cal':('Hiệu chuẩn ADC','Hệ số dòng','×',6),
'i_offset':('Hiệu chuẩn ADC','Offset đầu vào đo dòng','V',6),
'adc_alpha':('Hiệu chuẩn ADC','Hệ số lọc IIR','',3),
'discharge_enabled':('Xả tụ sau OFF','Cho phép xả xung','0/1',0),
'discharge_peak':('Xả tụ sau OFF','Dòng tăng ước tính','A',2),
'discharge_interval':('Xả tụ sau OFF','Khoảng cách hai xung','ms',0),
'discharge_on_us':('Xả tụ sau OFF','Độ rộng xung tối đa','µs',0),
'discharge_stop':('Xả tụ sau OFF','Dừng xả dưới','V',2),
'discharge_timeout':('Xả tụ sau OFF','Thời gian tối đa','ms',0),
}
def schema():
    config=(ROOT/'Core/Inc/psu_config.h').read_text()
    macros=dict(re.findall(r'^#define\s+(PSU_\w+)\s+([^\s/]+)',config,re.M))
    def number(token):
        while token in macros:token=macros[token]
        return float(token.rstrip('fuU'))
    fields=[]
    for name,scale,lo,hi,default in re.findall(r'^PSU_FIELD\((\w+),\s*(\d+),\s*([^,]+),\s*([^,]+),\s*([^\)]+)\)',(ROOT/'Core/Inc/psu_settings_fields.def').read_text(),re.M):
        group,label,unit,decimals=META[name]
        fields.append(dict(key=name,scale=int(scale),minimum=number(lo.strip()),maximum=number(hi.strip()),default=number(default.strip()),group=group,label=label,unit=unit,decimals=decimals))
    assert len(fields)==len(META)
    return dict(protocol=1,schema=1,hardware=dict(vin_divider=number('PSU_VIN_DIVIDER'),vout_divider=number('PSU_VOUT_DIVIDER'),shunt_ohm=number('PSU_SHUNT_OHM'),current_gain=number('PSU_CURRENT_GAIN'),current_attenuation=number('PSU_CURRENT_ATTENUATION'),ovp_absolute=number('PSU_OUTPUT_OVP_ABS_V')),
                caps=dict(vout_mV=round(number('PSU_VOUT_MAX_V')*1000),vin_mV=round(number('PSU_VIN_MAX_V')*1000),i_mA=round(number('PSU_CURRENT_MAX_A')*1000),ipeak_mA=round(number('PSU_CURRENT_PEAK_A')*1000),power_mW=round(number('PSU_POWER_MAX_W')*1000),input_mA=round(number('PSU_INPUT_CURRENT_MAX_A')*1000),ovp_mV=round(number('PSU_OUTPUT_OVP_ABS_V')*1000)),fields=fields)
if __name__=='__main__':
    path=ROOT/'PC_App/settings_schema.json';path.parent.mkdir(exist_ok=True)
    path.write_text(json.dumps(schema(),ensure_ascii=False,indent=2),encoding='utf-8')
    print('Exported',len(schema()['fields']),'firmware settings:',path)
