# -*- coding: utf-8 -*-
"""Smart PSU v9.3 USB CDC tuner. Install beside Keil in Firmware_BB_v7/PC_App."""
from __future__ import annotations
import csv
import json
import queue
import threading
import time
from collections import deque
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit('Install dependencies: py -m pip install pyserial matplotlib') from exc
try:
    import matplotlib
    matplotlib.use('TkAgg')
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    from matplotlib.figure import Figure
except ImportError as exc:
    raise SystemExit('Install dependencies: py -m pip install pyserial matplotlib') from exc

# All commands use INTEGER wire units to avoid MCU printf/scanf floating-point ABI issues.
# Gains: milligain (1.000 means 1000). Voltage mV, current mA, duty permille.
PARAMS = [
    ('CV_KP', 'PI áp Kp', 'gain', '1.000'),
    ('CV_KI', 'PI áp Ki (1/s)', 'gain', '6.000'),
    ('CC_KP', 'PI dòng Kp', 'gain', '5.000'),
    ('CC_KI', 'PI dòng Ki (1/s)', 'gain', '120.000'),
    ('OVP_MV', 'Quá áp Vout (V)', '1000', '30.000'),
    ('OCP_MA', 'Quá dòng Iout (A)', '1000', '6.000'),
    ('VIN_MIN_MV', 'Vin nhỏ nhất (V)', '1000', '7.000'),
    ('VIN_MAX_MV', 'Vin lớn nhất (V)', '1000', '40.000'),
    ('VSET_MAX_MV', 'Vset lớn nhất (V)', '1000', '28.000'),
    ('ISET_MAX_MA', 'Iset lớn nhất (A)', '1000', '5.000'),
    ('DUTY_MAX_PM', 'Duty tối đa (%)', '10', '97.0'),
    ('V_RISE_MV_S', 'Ramp tăng áp (V/s)', '1000', '2.000'),
    ('V_FALL_MV_S', 'Ramp giảm áp (V/s)', '1000', '8.000'),
    ('VSET_MV', 'Vset (V)', '1000', '5.000'),
    ('ISET_MA', 'Iset (A)', '1000', '1.000'),
]
PARAM_MAP = {row[0]: row for row in PARAMS}
ROOT_BG = '#0A101A'
CARD_BG = '#141F32'
FG = '#E6EEFF'
BLUE = '#398DFF'
RED = '#FF5569'
PINK = '#FF5DB6'
# FAULT_PC_APP_V1: displayed fault codes share PSU_Error numeric mapping.
FAULT_NAMES = {
    0: 'NONE',
    1: 'ADC STALE / Mất dữ liệu ADC',
    2: 'VIN RANGE / Điện áp vào ngoài giới hạn',
    3: 'OVP / Quá áp đầu ra',
    4: 'OCP / Quá dòng đầu ra',
    5: 'TIMER CFG / Lỗi cấu hình TIM1-TIM3',
}


def encode(key: str, value: str) -> int:
    # Use Decimal so any slider/entry value maps to the wire exactly.
    from decimal import Decimal, InvalidOperation
    if key not in PARAM_MAP:
        raise ValueError(f'Unknown setting: {key}')
    try:
        scaled = Decimal(value) * Decimal(1000 if PARAM_MAP[key][2] != '10' else 10)
    except InvalidOperation as exc:
        raise ValueError(f'Invalid number for {key}') from exc
    if scaled != scaled.to_integral_value() or scaled < 0 or scaled > 2**32-1:
        raise ValueError(f'Invalid value or resolution for {key}')
    return int(scaled)


def decode(key: str, value: int) -> str:
    divider = 10 if PARAM_MAP[key][2] == '10' else 1000
    return f'{value/divider:.1f}' if divider == 10 else f'{value/divider:.3f}'


class SerialWorker:
    def __init__(self, events: queue.Queue):
        self.events = events
        self.port: serial.Serial | None = None
        self.stop_event = threading.Event()
        self.thread: threading.Thread | None = None
        self.commands: queue.Queue[str] = queue.Queue()
        self.last_telemetry = 0.0
        self.last_rx = 0.0
        self.last_probe = 0.0

    def connect(self, name: str):
        if self.thread and self.thread.is_alive():
            raise RuntimeError('Already connected')
        self.stop_event.clear()
        self.commands = queue.Queue()
        self.last_telemetry = 0.0
        self.last_rx = 0.0
        self.last_probe = 0.0
        self.thread = threading.Thread(target=self._run, args=(name,), daemon=True)
        self.thread.start()

    def send(self, line: str):
        self.commands.put(line)

    def close(self):
        self.stop_event.set()

    def note_telemetry(self):
        self.last_telemetry = time.monotonic()

    def _run(self, name: str):
        try:
            # STM32 USB CDC ignores baud rate; 115200 is a host-side placeholder.
            with serial.Serial(name, 115200, timeout=0.04, write_timeout=0.3) as port:
                self.port = port
                self.events.put(('link', f'COM opened: {name} | awaiting STM32'))
                port.reset_input_buffer()
                self.send('PING')
                self.send('GET CFG')
                self.send('GET STATUS')
                packet = bytearray()
                self.last_probe = time.monotonic()
                while not self.stop_event.is_set():
                    now = time.monotonic()
                    # A COM port opening does not guarantee CDC_Receive_FS handled
                    # the first PING. GET STATUS re-enables the STM32 stream,
                    # without clearing the MCU transmit queue like PING does.
                    if (now-self.last_probe >= 2.0 and
                            (self.last_telemetry == 0.0 or now-self.last_telemetry >= 2.5)):
                        self.send('GET STATUS')
                        self.last_probe = now
                    try:
                        for _ in range(4):
                            cmd = self.commands.get_nowait()
                            port.write((cmd+'\n').encode('ascii'))
                    except queue.Empty:
                        pass
                    data = port.read(256)
                    for b in data:
                        if b == 10:
                            if len(packet) <= 220:
                                self.events.put(('line', packet.decode('ascii', errors='replace').strip()))
                                self.last_rx = time.monotonic()
                            packet.clear()
                        elif b != 13:
                            if len(packet) < 256:
                                packet.append(b)
                            else:
                                packet.clear()
        except (serial.SerialException, OSError) as exc:
            self.events.put(('error', str(exc)))
        finally:
            self.port = None
            self.events.put(('link', 'Disconnected'))


class Tuner(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('SMART PSU • Live PI Tuner + Ramp / Flash')
        self.geometry('1220x830')
        self.configure(bg=ROOT_BG)
        self.events: queue.Queue = queue.Queue()
        self.worker = SerialWorker(self.events)
        self.values: dict[str, tk.StringVar] = {}
        self.device_values: dict[str,int] = {}
        self.buffer = deque(maxlen=2000)
        self.time_origin: float | None = None
        self.active = False
        self.refresh_due = 0.0
        self.frame_count = 0
        self.bad_frames = 0
        self.last_warning = 0.0
        self.last_fault = None
        self.connect_monotonic = 0.0
        self.status = tk.StringVar(value='Disconnected')
        self.telemetry_info = tk.StringVar(value='Telemetry: 0 samples')
        self.port_choice = tk.StringVar()
        self.cards = {}
        self._make_style()
        self._build()
        self._ports()
        self.after(60, self._poll)
        self.protocol('WM_DELETE_WINDOW', self._quit)

    def _make_style(self):
        sty = ttk.Style(self)
        if 'clam' in sty.theme_names(): sty.theme_use('clam')
        sty.configure('.', background=ROOT_BG, foreground=FG, font=('Segoe UI',10))
        sty.configure('TFrame', background=ROOT_BG)
        sty.configure('TLabel', background=ROOT_BG, foreground=FG)
        sty.configure('TLabelframe', background=ROOT_BG, foreground=FG)
        sty.configure('TLabelframe.Label', background=ROOT_BG, foreground=FG, font=('Segoe UI',11,'bold'))
        sty.configure('TButton', padding=6)
        sty.configure('TEntry', fieldbackground=CARD_BG, foreground=FG)
        sty.configure('TCombobox', fieldbackground=CARD_BG, foreground='black')

    def _build(self):
        header = ttk.Frame(self); header.pack(fill='x', padx=16, pady=10)
        ttk.Label(header,text='SMART PSU  |  USB LIVE TUNING', font=('Segoe UI',19,'bold'), foreground=PINK).pack(side='left')
        ttk.Label(header,textvariable=self.status).pack(side='right')
        bar = ttk.Frame(self); bar.pack(fill='x', padx=16, pady=(0,10))
        ttk.Label(bar,text='USB CDC:').pack(side='left')
        self.ports = ttk.Combobox(bar,textvariable=self.port_choice,width=18,state='readonly');self.ports.pack(side='left',padx=7)
        ttk.Button(bar,text='Quét cổng',command=self._ports).pack(side='left',padx=4)
        ttk.Button(bar,text='Kết nối',command=self._connect).pack(side='left',padx=4)
        ttk.Button(bar,text='Ngắt kết nối',command=self.worker.close).pack(side='left',padx=4)
        ttk.Button(bar,text='Đọc cấu hình MCU',command=lambda:self._send('GET CFG')).pack(side='left',padx=6)
        ttk.Button(bar,text='Lưu vào Flash',command=self._save_flash).pack(side='left',padx=5)
        ttk.Button(bar,text='Lưu profile JSON',command=self._save_profile).pack(side='left',padx=6)
        ttk.Button(bar,text='Mở profile JSON',command=self._load_profile).pack(side='left',padx=6)
        ttk.Button(bar,text='Xuất CSV',command=self._export).pack(side='right',padx=4)

        head2 = ttk.Frame(self); head2.pack(fill='x', padx=16)
        for name,color in [('VOUT',RED),('IOUT',BLUE),('POWER',PINK),('VIN',FG)]:
            f=tk.Frame(head2,bg=CARD_BG,highlightthickness=1,highlightbackground='#263955');f.pack(side='left',expand=True,fill='x',padx=4)
            tk.Label(f,text=name,font=('Segoe UI',10,'bold'),fg=color,bg=CARD_BG).pack(pady=(8,2))
            label=tk.Label(f,text='--.--',font=('Consolas',28,'bold'),fg=color,bg=CARD_BG)
            label.pack(pady=(0,9));self.cards[name]=label
        ttk.Label(bar,textvariable=self.telemetry_info,foreground=BLUE).pack(side='right',padx=12)
        self.secondary=tk.StringVar(value='OFF · BUCK · FET sensor: -- V · Uptime: 0s')
        ttk.Label(self,textvariable=self.secondary,font=('Segoe UI',10)).pack(anchor='w',padx=20,pady=6)
        self.fault_text=tk.StringVar(value='FAULT: NONE  |  BTN1: ON/OFF')
        self.fault_banner=tk.Label(self,textvariable=self.fault_text,
                                   bg=CARD_BG,fg=FG,font=('Segoe UI',13,'bold'),
                                   anchor='w',padx=12,pady=6)
        self.fault_banner.pack(fill='x',padx=20,pady=(0,4))
        content = ttk.Frame(self);content.pack(fill='both',expand=True,padx=16,pady=8)
        settings=ttk.LabelFrame(content,text='PI và ngưỡng bảo vệ');settings.pack(side='left',fill='y',padx=(0,10))
        for i,(key,label,typ,default) in enumerate(PARAMS):
            row=ttk.Frame(settings);row.pack(fill='x',padx=10,pady=3)
            ttk.Label(row,text=label,width=23).pack(side='left')
            var=tk.StringVar(value=default);self.values[key]=var
            ttk.Entry(row,textvariable=var,width=12).pack(side='left',padx=5)
            ttk.Button(row,text='Gửi',command=lambda k=key:self._send_param(k)).pack(side='right')
        ttk.Button(settings,text='Áp dụng tất cả',command=self._apply_all).pack(fill='x',padx=12,pady=10)
        info=('Gains đang tính theo đơn vị 1/s cho Ki.\n'
              'VSET / ISET chỉnh khi nguồn đang ON có thể tạo bước đáp ứng.\n'
              'Các ngưỡng được kiểm tra theo thứ tự gửi;\n'
              'hãy tăng giới hạn OVP/OCP trước khi tăng Max V/I.\n'
              'Bật nguồn chỉ từ BTN1 trên board, không qua app.')
        ttk.Label(settings,text=info,wraplength=335,justify='left').pack(padx=12,pady=(0,6))

        frame=ttk.Frame(content);frame.pack(side='left',fill='both',expand=True)
        self.fig=Figure(figsize=(7.2,5.3),facecolor=ROOT_BG)
        self.ax=self.fig.add_subplot(111,facecolor=CARD_BG)
        self.ax.set_title('Vout / Voltage ramp / Vset / Iout',color=FG)
        self.ax.set_xlabel('Thời gian (s)',color=FG)
        self.ax.grid(alpha=.18)
        self.ax.tick_params(colors=FG)
        self.ax2=self.ax.twinx();self.ax2.tick_params(colors=BLUE)
        self.linev,=self.ax.plot([],[],color=RED,linewidth=1.8,label='Vout (V)')
        self.liner,=self.ax.plot([],[],color=PINK,linewidth=1.6,label='Ramp Vref (V)')
        self.lineset,=self.ax.plot([],[],color='#BCC6DD',linestyle='--',linewidth=1.1,label='Vset (V)')
        self.linei,=self.ax2.plot([],[],color=BLUE,linewidth=1.5,label='Iout (A)')
        for spine in list(self.ax.spines.values())+list(self.ax2.spines.values()):spine.set_color('#355072')
        self.ax.legend(loc='upper left',facecolor=CARD_BG,labelcolor=FG,fontsize=8)
        self.canvas=FigureCanvasTkAgg(self.fig,master=frame);self.canvas.get_tk_widget().pack(fill='both',expand=True)
        self.log=tk.Text(frame,height=7,bg=CARD_BG,fg=FG,insertbackground=FG,relief='flat')
        self.log.pack(fill='x',pady=10)
        self._log('Waiting for STM32 USB CDC...')

    def _log(self,msg):
        stamp=time.strftime('%H:%M:%S')
        self.log.insert('end',f'[{stamp}] {msg}\n');self.log.see('end')
        if int(self.log.index('end-1c').split('.')[0])>400:self.log.delete('1.0','100.0')

    def _ports(self):
        ports=[p.device for p in list_ports.comports()]
        self.ports['values']=ports
        if ports and self.port_choice.get() not in ports:self.port_choice.set(ports[0])

    def _connect(self):
        if not self.port_choice.get():
            messagebox.showerror('COM','Chọn cổng USB CDC trước');return
        self.frame_count = 0
        self.bad_frames = 0
        self.last_fault = None
        self.buffer.clear()
        self.time_origin = None
        self.telemetry_info.set('Telemetry: waiting for data')
        self.connect_monotonic = time.monotonic()
        try:self.worker.connect(self.port_choice.get())
        except RuntimeError as ex:messagebox.showerror('COM',str(ex))

    def _send(self,cmd):
        if not (self.worker.thread and self.worker.thread.is_alive()):
            self._log('No connection; command not sent');return
        self.worker.send(cmd)

    def _send_param(self,key):
        try:raw=encode(key,self.values[key].get())
        except ValueError as ex:messagebox.showerror('Giá trị',str(ex));return
        self._send(f'SET {key} {raw}')

    def _apply_all(self):
        try:desired={key:encode(key,self.values[key].get()) for key in PARAM_MAP}
        except ValueError as ex:messagebox.showerror('Giá trị',str(ex));return
        old={key:self.device_values.get(key,encode(key,PARAM_MAP[key][3])) for key in desired}
        # Apply in an order that maintains inter-dependent constraints:
        # 1) reduce soft maximums, 2) loosen hard threshold,
        # 3) tighten hard threshold, 4) increase soft maximums.
        order=[]
        for key in ('VSET_MAX_MV','ISET_MAX_MA'):
            if desired[key]<old[key]:order.append(key)
        for key in ('OVP_MV','OCP_MA'):
            if desired[key]>old[key]:order.append(key)
        for key in ('OVP_MV','OCP_MA'):
            if desired[key]<=old[key]:order.append(key)
        for key in ('VSET_MAX_MV','ISET_MAX_MA'):
            if desired[key]>=old[key]:order.append(key)
        # Vin range also has a min+1V < max invariant.
        if desired['VIN_MIN_MV']<=old['VIN_MIN_MV']:
            order.extend(['VIN_MIN_MV','VIN_MAX_MV'])
        else:order.extend(['VIN_MAX_MV','VIN_MIN_MV'])
        order+=['DUTY_MAX_PM','CV_KP','CV_KI','CC_KP','CC_KI',
                'V_RISE_MV_S','V_FALL_MV_S','VSET_MV','ISET_MA']
        for key in order:self._send(f'SET {key} {desired[key]}')
        self._log('Profile transmitted. Watch acknowledgments; rejected fields return ERR.')

    def _save_flash(self):
        if not self.worker.thread or not self.worker.thread.is_alive():
            self._log('Connect to STM32 first')
            return
        self._send('SAVE')
        self._log('Flash save requested. MCU writes only while PSU is OFF.')

    def _save_profile(self):
        name=filedialog.asksaveasfilename(defaultextension='.json',filetypes=[('PSU profile','*.json')])
        if name:
            Path(name).write_text(json.dumps({key:v.get() for key,v in self.values.items()},indent=2),encoding='utf-8')
            self._log(f'Saved: {name}')

    def _load_profile(self):
        name=filedialog.askopenfilename(filetypes=[('PSU profile','*.json')])
        if name:
            try:
                loaded=json.loads(Path(name).read_text(encoding='utf-8'))
                if not isinstance(loaded,dict):raise ValueError('Invalid profile')
                for key in self.values:
                    if key in loaded:self.values[key].set(str(loaded[key]))
                self._log(f'Loaded (not applied): {name}')
            except (OSError,ValueError) as ex:messagebox.showerror('JSON',str(ex))

    def _export(self):
        name=filedialog.asksaveasfilename(defaultextension='.csv',filetypes=[('CSV','*.csv')])
        if name:
            with open(name,'w',newline='',encoding='utf-8-sig') as f:
                writer=csv.writer(f);writer.writerow(['time_s','vin_v','vout_v','iout_a','power_w','vset_v','iset_a','duty_pct','state','mode','error','fet_sensor_v','ramped_vref_v'])
                writer.writerows(self.buffer)
            self._log(f'Exported {len(self.buffer)} rows')

    def _parse_line(self,line):
        if not line:return
        cells=line.split(',')
        if cells[0]=='P' and len(cells)==3 and cells[1] in self.values:
            try:
                raw=int(cells[2]);self.device_values[cells[1]]=raw
                self.values[cells[1]].set(decode(cells[1],raw))
            except ValueError:pass
            return
        if cells[0]=='T' and len(cells) in (14,15):
            try:
                nums=[int(v) for v in cells[1:]]
                t,vin,vout,iout,power,vset,iset,duty,state,mode,error,fet,runtime=nums[:13]
                ramp_mV=nums[13] if len(nums)>13 else vset
                if self.time_origin is None:self.time_origin=t/1000.0
                point=(t/1000.0-self.time_origin,vin/1000.0,vout/1000.0,iout/1000.0,power/1000.0,
                       vset/1000.0,iset/1000.0,duty/10.0,state,mode,error,fet/1000.0,ramp_mV/1000.0)
                self.buffer.append(point)
                self.worker.note_telemetry()
                self.frame_count += 1
                self.status.set('STM32 telemetry active' if state!=2 else
                                f'STM32 telemetry  |  FAULT {FAULT_NAMES.get(error, error)}')
                self.telemetry_info.set(f'Telemetry: {self.frame_count} samples (10 Hz expected)')
                # Log only on transitions so a 10Hz stream cannot flood the event log.
                fault_now=(state==2, error)
                if fault_now!=self.last_fault:
                    if state==2:
                        self._log(f'!!! PSU FAULT [{error}]: {FAULT_NAMES.get(error, "UNKNOWN")}. '
                                  'Press BTN1 on board once to STOP/CLEAR.')
                    elif self.last_fault is not None and self.last_fault[0]:
                        self._log('PSU fault cleared; output remains OFF until next BTN1 press')
                    self.last_fault=fault_now
                if state==2:
                    self.fault_text.set(f'FAULT [{error}]: {FAULT_NAMES.get(error, "UNKNOWN")}  |  BTN1 = CLEAR')
                    self.fault_banner.config(bg='#5D1522',fg='#FFFFFF')
                else:
                    self.fault_text.set('FAULT: NONE  |  BTN1 = ON/OFF')
                    self.fault_banner.config(bg=CARD_BG,fg=FG)
                self.cards['VOUT'].config(text=f'{vout/1000:.2f} V')
                self.cards['IOUT'].config(text=f'{iout/1000:.2f} A')
                self.cards['POWER'].config(text=f'{power/1000:.2f} W')
                self.cards['VIN'].config(text=f'{vin/1000:.2f} V')
                self.secondary.set(f'{["OFF","ON","FAULT"][state] if state<3 else "?"} · '
                                   f'{"BUCK" if mode==0 else "BOOST"} · Duty {duty/10:.1f}% · '
                                   f'Fault {FAULT_NAMES.get(error, error)} · FET sensor {fet/1000:.3f} V · Uptime {runtime}s')
            except (ValueError,TypeError,IndexError) as exc:
                self.bad_frames += 1
                self._log(f'Invalid telemetry: {line[:130]} ({exc})')
            return
        if cells[0]=='T':
            self.bad_frames += 1
            self._log(f'Invalid T frame: expected 14/15 comma-fields, got {len(cells)}: {line[:130]}')
            return
        self._log(line)

    def _render_plot(self):
        if not self.buffer:return
        sample=list(self.buffer)
        xs=[r[0] for r in sample];vs=[r[2] for r in sample];am=[r[3] for r in sample]
        vr=[r[12] for r in sample];vset=[r[5] for r in sample]
        self.linev.set_data(xs,vs);self.linei.set_data(xs,am)
        self.liner.set_data(xs,vr);self.lineset.set_data(xs,vset)
        lo=max(xs[-1]-60,0); hi=max(xs[-1],1)
        self.ax.set_xlim(lo,hi)
        window=[r for r in sample if r[0]>=lo]
        self.ax.set_ylim(0,max(2,max(max(r[2],r[5],r[12]) for r in window)*1.15))
        self.ax2.set_ylim(0,max(0.5,max(r[3] for r in window)*1.15))
        self.canvas.draw_idle()

    def _poll(self):
        count=0
        while count<150:
            try:kind,data=self.events.get_nowait()
            except queue.Empty:break
            count+=1
            if kind=='line':self._parse_line(data)
            elif kind=='link':
                self.status.set(data)
                self._log(data)
            elif kind=='error':self._log('Serial error: '+data)
        now = time.monotonic()
        if self.worker.thread and self.worker.thread.is_alive() and self.connect_monotonic:
            last = self.worker.last_telemetry or self.connect_monotonic
            if now-last >= 4.0 and now-self.last_warning >= 6.0:
                msg = ('COM open but no telemetry from STM32. Check PONG/OK,STATUS '
                       'in log; verify PSU_Link_Poll() and CDC_Receive_FS() on MCU.')
                self._log(msg)
                self.status.set('COM open / NO DATA')
                self.telemetry_info.set(f'Telemetry stalled: {now-last:.0f}s, {self.frame_count} samples')
                self.last_warning = now
        if now>=self.refresh_due:
            self.refresh_due=now+0.25
            self._render_plot()
        self.after(60,self._poll)

    def _quit(self):
        self.worker.close();self.destroy()

if __name__=='__main__':
    Tuner().mainloop()
