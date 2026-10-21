"""Wire framing and configuration validation, independent of Tk and serial I/O."""
from __future__ import annotations
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path
import json

ROOT=Path(__file__).resolve().parent
SCHEMA=json.loads((ROOT/'settings_schema.json').read_text(encoding='utf-8'))
FIELDS={item['key']:item for item in SCHEMA['fields']}
FAULTS={1:'Dòng đỉnh',2:'Quá áp output',4:'Quá áp VIN',8:'VIN thấp',16:'Quá nhiệt FET',32:'Cảm biến lỗi',64:'ADC mất dữ liệu',128:'Dòng liên tục',256:'Trễ điều khiển',512:'Ngắn mạch output'}
BLOCKS={1:'Cấu hình phần cứng chưa hợp lệ',2:'Cần xóa lỗi trước',4:'ADC chưa có dữ liệu mới',8:'Kiểm tra cảm biến nhiệt',16:'VIN thấp',32:'VIN quá cao',64:'FET còn nóng',128:'Output còn dòng',512:'Chờ cảm biến ổn định'}
ERRORS={'output_off_required':'Cần OFF và dừng xả trước khi cấu hình/lưu.', 'revision_changed':'Cấu hình trên board đã thay đổi. Đọc lại trước khi áp dụng.', 'blocked':'Board chưa đủ điều kiện ON.', 'clear_conditions':'Chưa đủ điều kiện xóa lỗi.', 'setpoint_range':'Điện áp/dòng đặt vượt giới hạn trên board.', 'invalid_setting':'Một thông số sai tên hoặc ngoài khoảng cho phép.', 'temperature_order':'Cần nhiệt reset < nhiệt cảnh báo < nhiệt ngắt.', 'vin_order':'Kiểm tra thứ tự các ngưỡng VIN.', 'current_order':'Ngưỡng dòng đỉnh phải cao hơn dòng liên tục.', 'current_adc_range':'Thang đo dòng ADC không đủ cho ngưỡng đỉnh.', 'voltage_adc_range':'Thang ADC điện áp không đủ.', 'flash_failed':'Ghi Flash không thành công.', 'cancelled':'Lệnh ON đã bị hủy bởi yêu cầu OFF.', 'disconnected':'Thiết bị đã ngắt kết nối.', 'test_mode':'Firmware đang ở chế độ test, không cho ON.'}

ERRORS['touch_calibration']='Đang hiệu chuẩn touch. Thoát CAL trước khi bật output.'
ERRORS['local_mode_required']='Chế độ/menu trên thiết bị đang khóa thao tác này. Thoát menu hoặc chuyển về LAB để chỉnh cấu hình.'
CHARGE_REASONS={1:'Không có pin hoặc điện áp quá thấp',2:'Điện áp pin cao hơn cấu hình pack',3:'Kiểm tra cảm biến, nguồn vào và BMS',4:'Hết thời gian sạc',5:'Hết thời gian sạc phục hồi',6:'Không có dòng sạc',7:'Cấu hình pin vượt giới hạn nguồn',8:'Đang chờ chuyển đầu ra'}
SOLDER_REASONS={1:'Cảm biến mũi lỗi — kiểm tra PA3',2:'Mất dữ liệu nhiệt mũi',3:'Nhiệt mũi vượt ngưỡng an toàn',4:'Cấp nhiệt nhưng nhiệt độ không tăng',5:'Nguồn tay hàn chưa ổn định',6:'Kiểm tra VIN, nhiệt FET và ADC',7:'Profile tay hàn vượt giới hạn nguồn'}
SOLDER_FIELDS={'solder_voltage','solder_current'}

def scaled(key:str,text:str)->int:
    field=FIELDS[key]
    try:value=Decimal(str(text).replace(',','.'))
    except InvalidOperation as exc:raise ValueError(f"{field['label']}: cần nhập số.") from exc
    if not value.is_finite() or not Decimal(str(field['minimum']))<=value<=Decimal(str(field['maximum'])):
        raise ValueError(f"{field['label']}: cho phép {field['minimum']:g}…{field['maximum']:g} {field['unit']}.")
    if key in ('ntc_to_gnd','discharge_enabled') and value not in (0,1):raise ValueError(f"{field['label']}: cần 0 hoặc 1.")
    return int((value*field['scale']).to_integral_value(rounding=ROUND_HALF_UP))

def defaults()->dict[str,int]:return {key:scaled(key,str(f['default'])) for key,f in FIELDS.items()}

def _complete_settings(settings:dict[str,int])->dict[str,int]:
    if not isinstance(settings,dict):raise ValueError('Cấu hình phải là một object.')
    keys=set(settings);all_keys=set(FIELDS)
    if keys not in (all_keys,all_keys-{'ramp_v_per_s'},all_keys-SOLDER_FIELDS,all_keys-SOLDER_FIELDS-{'ramp_v_per_s'}):
        raise ValueError('Profile thiếu thông số hoặc khác phiên bản schema.')
    return dict(defaults(),**settings)

def upgrade_settings(settings:dict[str,int])->dict[str,int]:
    """Read legacy 36/37-field profiles without losing PID or limits."""
    settings=_complete_settings(settings);validate(settings);return settings
def physical(settings:dict[str,int],key:str)->float:return settings[key]/FIELDS[key]['scale']

def validate(settings:dict[str,int])->None:
    settings=_complete_settings(settings)
    for key,value in settings.items():
        if type(value) is not int:raise ValueError(f'{key}: giá trị wire phải là số nguyên.')
        scaled(key,str(Decimal(value)/FIELDS[key]['scale']))
    s={key:physical(settings,key) for key in FIELDS}
    if s['vin_min']+1>s['vin_max'] or not s['vin_min']<=s['vin_warn']<=s['vin_max']:raise ValueError(ERRORS['vin_order'])
    if s['i_peak']<=s['i_max']:raise ValueError(ERRORS['current_order'])
    if not s['temp_reset']<s['temp_warn']<s['temp_trip']:raise ValueError(ERRORS['temperature_order'])
    if s['ntc_to_gnd'] not in (0,1) or s['discharge_enabled'] not in (0,1):raise ValueError('Giá trị bật/tắt phải là 0 hoặc 1.')
    hw=SCHEMA['hardware']
    if s['i_offset']+s['i_peak']*1.05*hw['shunt_ohm']*hw['current_gain']*hw['current_attenuation']/s['i_cal']>s['vdda']*.95+1e-6:raise ValueError(ERRORS['current_adc_range'])
    ovp=min(s['vout_max']*(1+s['ovp_percent']*.01)+s['ovp_offset'],hw['ovp_absolute'])
    if s['vdda']*.95*hw['vin_divider']*s['vin_cal']<s['vin_max']+2-1e-6 or s['vdda']*.95*hw['vout_divider']*s['vout_cal']<ovp+1-1e-6:raise ValueError(ERRORS['voltage_adc_range'])

def command(identifier:int,verb:str,*arguments:object)->bytes:
    if not 1<=identifier<=2147483647:raise ValueError('ID outside protocol range')
    if verb not in {'GET','SET','ON','OFF','CLEAR','CFG','DEFAULTS','SAVE','PRESET','HEAT'}:raise ValueError('Unsupported command')
    parts=[f'@{identifier}',verb,*map(str,arguments)]
    if any(any(c in p for c in '\r\n\x00!?') for p in parts):raise ValueError('Invalid command character')
    line=' '.join(parts).encode('ascii')+b'\n'
    if len(line)>1024:raise ValueError('Command exceeds firmware line capacity')
    return line

def config_command(identifier:int,revision:int,settings:dict[str,int])->bytes:
    validate(settings)
    return command(identifier,'CFG',revision,*(f'{key}={settings[key]}' for key in FIELDS if key in settings))

class Framer:
    """Keep fragmented JSON lines; discard oversized/corrupt lines to newline."""
    def __init__(self):self.buffer=bytearray();self.discard=False;self.errors=0
    def feed(self,data:bytes)->list[dict]:
        messages=[]
        for byte in data:
            if byte==10:
                if not self.discard and self.buffer:
                    try:
                        obj=json.loads(self.buffer.decode('utf-8'))
                        if not isinstance(obj,dict):raise ValueError('not an object')
                        messages.append(obj)
                    except (ValueError,UnicodeError,RecursionError):self.errors+=1
                self.buffer.clear();self.discard=False
            elif not self.discard:
                if len(self.buffer)>=8192:self.buffer.clear();self.discard=True;self.errors+=1
                else:self.buffer.append(byte)
        return messages

def fault_text(mask:int)->str:return ', '.join(label for bit,label in FAULTS.items() if mask&bit) or 'Không có lỗi'
def block_text(mask:int)->str:return ', '.join(label for bit,label in BLOCKS.items() if mask&bit) or 'Đủ điều kiện'

def save_profile(path:Path,settings:dict[str,int],mv:int,ma:int)->None:
    validate(settings)
    if not 1500<=mv<=settings['vout_max'] or not 50<=ma<=settings['i_max']:raise ValueError('Setpoint vượt giới hạn của profile.')
    path.write_text(json.dumps({'schema':1,'settings':settings,'set_mV':mv,'set_mA':ma},ensure_ascii=False,indent=2),encoding='utf-8')

def load_profile(path:Path)->dict:
    if path.stat().st_size>32768:raise ValueError('Profile quá lớn.')
    obj=json.loads(path.read_text(encoding='utf-8-sig'))
    if not isinstance(obj,dict) or obj.get('schema')!=1:raise ValueError('Không đúng định dạng profile BB V6.')
    obj['settings']=upgrade_settings(obj.get('settings',{}))
    mv,ma=obj.get('set_mV'),obj.get('set_mA')
    if type(mv) is not int or type(ma) is not int or not 1500<=mv<=obj['settings']['vout_max'] or not 50<=ma<=obj['settings']['i_max']:raise ValueError('Setpoint profile không hợp lệ.')
    return obj
