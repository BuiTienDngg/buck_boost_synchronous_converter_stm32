"""Deduplicated protection history on the PC; no changes to board protection."""
from pathlib import Path
from datetime import datetime,timezone,timedelta
from collections import deque
from copy import deepcopy
import os,json
from protocol import fault_text,block_text,CHARGE_REASONS,SOLDER_REASONS
VN=timezone(timedelta(hours=7))


class FaultJournal:
    def __init__(self,folder=None):
        self.folder=Path(folder) if folder else Path(os.environ.get('LOCALAPPDATA',Path.home()))/'BB_V6_Studio'/'logs'
        self.path=self.folder/(datetime.now(VN).strftime('errors_%Y%m%d_%H%M%S')+f'_{os.getpid()}.jsonl')
        self.entries=deque(maxlen=500);self.signature=None;self.last_ms=None;self.touch_logged=False

    def record(self,kind,summary,data=None,device='',demo=False):
        event=dict(time=datetime.now(VN).isoformat(timespec='milliseconds'),kind=kind,summary=summary,
                   device=device,demo=demo,data=deepcopy(data or {}))
        self.folder.mkdir(parents=True,exist_ok=True)
        with self.path.open('a',encoding='utf-8') as file:file.write(json.dumps(event,ensure_ascii=False)+'\n')
        self.entries.append(event);return event

    def observe(self,data,device='',demo=False,settings=None):
        ms=data.get('ms',0)
        if self.last_ms is not None and ms<self.last_ms and self.last_ms-ms<2147483648:
            self.signature=None;self.touch_logged=False
        self.last_ms=ms
        drain=data.get('drain',{}).get('state',0)
        charge=data.get('charge',{}) if data.get('mode') in (1,2) else {}
        solder_error=data.get('solder',{}).get('reason',0) if data.get('mode')==2 else 0
        state=(data.get('fault',0),data.get('config',0),data.get('start_block',0),drain if drain>=4 else 0,solder_error or charge.get('reason',0),int(data.get('mode')==1 and charge.get('phase')==4))
        touch=int(data.get('touch',{}).get('errors',0)>0)
        if state==self.signature:
            if touch and not self.touch_logged:
                event=self.record('warning','Touch: đã có lỗi SPI; xem thông số thô',data,device,demo)
                self.touch_logged=True;return event
            return None
        previous=self.signature
        if not any(state):
            if previous and any(previous):
                event=self.record('cleared','Các cảnh báo đã hết / bảo vệ đã được xóa',data,device,demo);self.signature=state;return event
            self.signature=state
            return None
        fault,config,block,stopped,charge_error,full=state;parts=[]
        if fault:parts.append(fault_text(fault))
        if config:parts.append(f'Khóa cấu hình 0x{config:X}')
        if block:parts.append('Chặn ON: '+block_text(block))
        if stopped:parts.append({4:'Xả dừng: timeout',5:'Xả dừng: không giảm áp',6:'Xả dừng: áp tăng',7:'Xả đã hủy'}.get(stopped,'Xả dừng'))
        if charge_error:parts.append(('Tay hàn đã dừng: ' if data.get('mode')==2 else 'Sạc đã dừng: ')+(SOLDER_REASONS if solder_error else CHARGE_REASONS).get(charge_error,str(charge_error)))
        if full:parts.append('Sạc hoàn tất; output đã tắt')
        if touch:parts.append('Touch: lỗi giao tiếp SPI; encoder vẫn dùng được')
        snapshot={key:value for key,value in data.items() if not key.startswith('_')}
        if settings:snapshot['settings']=dict(settings)
        kind='protection' if fault or config or charge_error else 'complete' if full and not block and not stopped else 'warning'
        event=self.record(kind,' · '.join(parts),snapshot,device,demo)
        self.signature=state;self.touch_logged=bool(touch);return event
