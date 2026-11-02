"""BB V6 Power Studio. Launch normally or use --demo without a physical board."""
from __future__ import annotations
from protocol import THERMAL_FIELDS
import argparse,csv,json,queue,time,tkinter as tk,sys,traceback,tempfile
from tkinter import ttk,filedialog,messagebox
from tkinter.scrolledtext import ScrolledText
from pathlib import Path
from collections import deque
from datetime import datetime,timezone,timedelta
from protocol import FIELDS,SCHEMA,ERRORS,CHARGE_REASONS,SOLDER_REASONS,SOLDER_FIELDS,defaults,validate,upgrade_settings,scaled,physical,fault_text,block_text,save_profile,load_profile
from transport import SerialWorker,DemoWorker,ports
from fault_log import FaultJournal

BG='#0b111c';CARD='#141e2d';PANEL='#101927';TEXT='#e6edf5';MUTED='#8c9eb5'
CYAN='#51d6dc';GREEN='#67dfa3';AMBER='#ffc77a';RED='#ff7c89';LINE='#29384d'
VN=timezone(timedelta(hours=7))

class Studio(tk.Tk):
    def __init__(self,demo=False):
        super().__init__()
        self.title('BB V6 · DC Power Studio');self.geometry('1160x810');self.minsize(1000,700);self.configure(bg=BG)
        self.events=queue.Queue();self.worker=None;self.connected=False;self.ready=False;self.demo=False
        self.settings=defaults();self.caps=dict(SCHEMA['caps']);self.revision=0;self.latest={};self.last_rx=0.;self.last_ms=None
        self.ramp_supported=False;self.solder_supported=False;self.solder_inputs={};self.thermal_inputs={};self.thermal_supported=False
        self.error_journal=FaultJournal();self.fault_rows={};self.fault_sequence=0
        self.pending={};self.on_after_set=False;self.loading=False;self.dirty=False;self.basic_dirty=False
        self.history=deque(maxlen=1200);self.event_log=deque(maxlen=200);self.csv_file=None;self.csv_writer=None;self.csv_rows=0
        self.field_vars={};self.last_diag=0;self.port_items={};self.demo_requested=demo
        self._styles();self._header();self._pages();self._footer();self.load_editor(self.settings)
        self.voltage_var.trace_add('write',lambda *_:self._basic_edited());self.current_var.trace_add('write',lambda *_:self._basic_edited())
        self.bind('<Escape>',lambda _:self.panic());self.protocol('WM_DELETE_WINDOW',self.close)
        self.bind_all('<MouseWheel>',self._wheel)
        self.bind_class('TSpinbox','<MouseWheel>',self._wheel)
        self.after(60,self.poll);self.after(100,self.scan_ports)
        if demo:self.after(160,self.connect_demo)

    def _styles(self):
        style=ttk.Style(self);style.theme_use('clam')
        style.configure('.',background=BG,foreground=TEXT,font=('Segoe UI',10))
        style.configure('TFrame',background=BG);style.configure('Card.TFrame',background=CARD)
        style.configure('TLabel',background=BG,foreground=TEXT)
        style.configure('Muted.TLabel',foreground=MUTED);style.configure('Card.TLabel',background=CARD)
        style.configure('TLabelframe',background=CARD,bordercolor=LINE,relief='solid')
        style.configure('TLabelframe.Label',background=CARD,foreground=CYAN,font=('Segoe UI Semibold',11))
        style.configure('TButton',background=LINE,foreground=TEXT,borderwidth=0,padding=(12,9))
        style.map('TButton',background=[('active','#354961'),('disabled','#182232')],foreground=[('disabled','#56677c')])
        style.configure('Accent.TButton',background='#176b73',foreground='#edffff')
        style.map('Accent.TButton',background=[('active','#24828c'),('disabled','#183540')])
        style.configure('Danger.TButton',background='#873b4b',foreground='#fff0f3')
        style.map('Danger.TButton',background=[('active','#a34859'),('disabled','#352331')])
        style.configure('TEntry',fieldbackground='#0b1422',foreground=TEXT,bordercolor=LINE,padding=5)
        style.configure('TSpinbox',fieldbackground='#0b1422',foreground=TEXT,arrowcolor=MUTED,bordercolor=LINE,padding=5)
        style.configure('TCombobox',fieldbackground='#0b1422',background=LINE,foreground=TEXT,arrowcolor=MUTED,padding=5)
        style.map('TCombobox',fieldbackground=[('readonly','#0b1422')],foreground=[('readonly',TEXT)])
        style.configure('TNotebook',background=BG,borderwidth=0)
        style.configure('TNotebook.Tab',background=BG,foreground=MUTED,padding=(22,11))
        style.configure('Treeview',background=PANEL,fieldbackground=PANEL,foreground=TEXT,rowheight=28,borderwidth=0)
        style.configure('Treeview.Heading',background=CARD,foreground=CYAN,font=('Segoe UI Semibold',10))
        style.map('Treeview',background=[('selected','#234554')])
        style.map('TNotebook.Tab',background=[('selected',CARD)],foreground=[('selected',CYAN)])
        self.option_add('*TCombobox*Listbox.background',CARD);self.option_add('*TCombobox*Listbox.foreground',TEXT)

    def _header(self):
        top=ttk.Frame(self,padding=(22,17,22,10));top.pack(fill='x')
        title=ttk.Frame(top);title.pack(side='left')
        ttk.Label(title,text='DC POWER STUDIO',font=('Segoe UI Semibold',19)).pack(anchor='w')
        ttk.Label(title,text='BB V6  /  USB CONTROL & CALIBRATION',style='Muted.TLabel',font=('Segoe UI',9)).pack(anchor='w',pady=(3,0))
        self.connect_button=ttk.Button(top,text='Kết nối',style='Accent.TButton',command=self.toggle_connection);self.connect_button.pack(side='right')
        ttk.Button(top,text='Quét',command=self.scan_ports).pack(side='right',padx=7)
        self.port_var=tk.StringVar();self.port_combo=ttk.Combobox(top,textvariable=self.port_var,state='readonly',width=27);self.port_combo.pack(side='right',padx=6)
        bar=ttk.Frame(self,padding=(22,0,22,12));bar.pack(fill='x')
        self.badge=tk.Label(bar,text='● OFFLINE',bg=BG,fg=MUTED,font=('Segoe UI Semibold',10));self.badge.pack(side='left')
        self.device_label=ttk.Label(bar,text='Chọn cổng COM của board để bắt đầu.',style='Muted.TLabel');self.device_label.pack(side='left',padx=18)
        ttk.Button(bar,text='Xem DEMO',command=self.connect_demo).pack(side='right')
        self.header_panic=ttk.Button(bar,text='TẮT NGAY · Esc',style='Danger.TButton',command=self.panic)
        self.header_panic.pack(side='right',padx=9)

    def _pages(self):
        self.tabs=ttk.Notebook(self);self.tabs.pack(fill='both',expand=True,padx=22)
        self.dashboard=ttk.Frame(self.tabs);self.config_page=ttk.Frame(self.tabs);self.diagnostics=ttk.Frame(self.tabs)
        self.tabs.add(self.dashboard,text='Tổng quan');self.tabs.add(self.config_page,text='Cấu hình');self.tabs.add(self.diagnostics,text='Nhật ký & chẩn đoán')
        self._dashboard();self._configuration();self._diagnostics()

    def _dashboard(self):
        self.dashboard.columnconfigure(0,weight=3);self.dashboard.columnconfigure(1,weight=2)
        self.dashboard.rowconfigure(0,weight=1)
        left=ttk.Frame(self.dashboard,padding=(0,14,14,5));left.grid(row=0,column=0,sticky='nsew')
        left.columnconfigure(0,weight=1);left.rowconfigure(3,weight=1)
        values=ttk.Frame(left);values.grid(row=0,column=0,sticky='ew');values.columnconfigure((0,1),weight=1)
        self.voltage_readout=self._metric(values,0,'ĐIỆN ÁP OUTPUT','—.—','V',CYAN)
        self.current_readout=self._metric(values,1,'DÒNG OUTPUT','—.—','A',GREEN)
        strip=ttk.Frame(left);strip.grid(row=1,column=0,sticky='ew',pady=12)
        self.vin_var=tk.StringVar(value='VIN  —');self.temp_var=tk.StringVar(value='FET  —');self.power_var=tk.StringVar(value='POWER  —');self.mode_var=tk.StringVar(value='OFF')
        for i,var in enumerate((self.vin_var,self.temp_var,self.power_var,self.mode_var)):
            strip.columnconfigure(i,weight=1)
            label=ttk.Label(strip,textvariable=var,background=CARD,padding=(9,12),font=('Segoe UI Semibold',10))
            label.grid(row=0,column=i,sticky='ew',padx=(0,6))
            if i==3:self.mode_label=label
        duty=ttk.Frame(left);duty.grid(row=2,column=0,sticky='ew',pady=(0,12));duty.columnconfigure((0,1),weight=1)
        self.buck_duty_var=tk.StringVar(value='—');self.boost_duty_var=tk.StringVar(value='—')
        for i,(title,var,color) in enumerate((('DUTY BUCK · IN / HO',self.buck_duty_var,CYAN),('DUTY BOOST · IN / HO',self.boost_duty_var,GREEN))):
            card=ttk.Frame(duty,style='Card.TFrame',padding=(12,8));card.grid(row=0,column=i,sticky='ew',padx=(0,6 if i==0 else 0))
            ttk.Label(card,text=title,background=CARD,foreground=MUTED,font=('Segoe UI',9)).pack(anchor='w')
            ttk.Label(card,textvariable=var,background=CARD,foreground=color,font=('Consolas',20)).pack(anchor='w')
        chart=ttk.Frame(left,style='Card.TFrame',padding=14);chart.grid(row=3,column=0,sticky='nsew')
        ttk.Label(chart,text='XU HƯỚNG · 60 GIÂY',style='Card.TLabel',font=('Segoe UI Semibold',10)).pack(anchor='w')
        ttk.Label(chart,text='VOUT  ●                         IOUT  ●',background=CARD,foreground=CYAN,font=('Segoe UI',9)).pack(anchor='w',pady=(5,0))
        self.chart=tk.Canvas(chart,bg=CARD,highlightthickness=0,height=270);self.chart.pack(fill='both',expand=True);self.chart.bind('<Configure>',lambda _:self.draw_chart())
        holder=ttk.Frame(self.dashboard,style='Card.TFrame');holder.grid(row=0,column=1,sticky='nsew',pady=(14,5))
        self.control_canvas=tk.Canvas(holder,bg=CARD,highlightthickness=0,width=310,height=300)
        rail=ttk.Scrollbar(holder,orient='vertical',command=self.control_canvas.yview)
        self.control_canvas.configure(yscrollcommand=rail.set);rail.pack(side='right',fill='y');self.control_canvas.pack(side='left',fill='both',expand=True)
        controls=ttk.Frame(self.control_canvas,style='Card.TFrame',padding=19)
        window=self.control_canvas.create_window((0,0),window=controls,anchor='nw')
        controls.bind('<Configure>',lambda _:self.control_canvas.configure(scrollregion=self.control_canvas.bbox('all')))
        self.control_canvas.bind('<Configure>',lambda event:self.control_canvas.itemconfigure(window,width=event.width))
        ttk.Label(controls,text='THIẾT LẬP OUTPUT',style='Card.TLabel',font=('Segoe UI Semibold',12)).pack(anchor='w')
        self.voltage_var=tk.StringVar(value='5.00');self.current_var=tk.StringVar(value='1.00')
        self.setpoint_entries={}
        for label,var,unit in [('Điện áp đặt',self.voltage_var,'V'),('Giới hạn dòng',self.current_var,'A')]:
            ttk.Label(controls,text=label,background=CARD,foreground=MUTED).pack(anchor='w',pady=(11,3))
            row=ttk.Frame(controls,style='Card.TFrame');row.pack(fill='x')
            entry=ttk.Entry(row,textvariable=var,font=('Consolas',22),width=11);entry.pack(side='left',fill='x',expand=True);self.setpoint_entries[unit]=entry
            ttk.Label(row,text=unit,style='Card.TLabel',font=('Segoe UI',15),padding=(10,0)).pack(side='right')
        self.applied_var=tk.StringVar(value='Đã áp dụng: chưa đọc board')
        ttk.Label(controls,textvariable=self.applied_var,background=CARD,foreground=MUTED,font=('Segoe UI',9)).pack(anchor='w',pady=(9,5))
        self.set_button=ttk.Button(controls,text='Áp dụng V / A',command=self.apply_setpoint);self.set_button.pack(fill='x',pady=3)
        self.output_button=ttk.Button(controls,text='BẬT OUTPUT',style='Accent.TButton',command=self.toggle_output);self.output_button.pack(fill='x',pady=(6,3))
        self.ramp_status_var=tk.StringVar(value='Ramp tăng áp · chưa đọc board')
        ttk.Label(controls,textvariable=self.ramp_status_var,background=CARD,foreground=MUTED,font=('Segoe UI',9)).pack(anchor='w',pady=(2,3))
        self.run_info_var=tk.StringVar(value='TIME 00:00:00')
        ttk.Label(controls,textvariable=self.run_info_var,background=CARD,foreground=CYAN,font=('Consolas',10)).pack(anchor='w',pady=(2,3))
        ttk.Button(controls,text='Chỉnh ramp',command=self.show_ramp).pack(anchor='w',pady=(0,8))
        ttk.Button(controls,text='Áp / dòng tay hàn',command=self.show_solder).pack(anchor='w',pady=(0,8))
        self.preset_buttons=[]
        row=ttk.Frame(controls,style='Card.TFrame');row.pack(fill='x',pady=(2,4))
        for k in range(3):
            btn=ttk.Button(row,text=f'M{k+1}',command=lambda slot=k:self.load_preset(slot),padding=(5,8));btn.pack(side='left',fill='x',expand=True,padx=2);self.preset_buttons.append(btn)
        row=ttk.Frame(controls,style='Card.TFrame');row.pack(fill='x')
        self.assign_buttons=[]
        for k in range(3):
            btn=ttk.Button(row,text=f'Gán M{k+1}',command=lambda slot=k:self.assign_preset(slot),padding=(4,5));btn.pack(side='left',fill='x',expand=True,padx=2);self.assign_buttons.append(btn)
        ttk.Separator(controls).pack(fill='x',pady=13)
        self.fault_label=tk.Label(controls,text='Chưa có dữ liệu bảo vệ.',background=CARD,foreground=MUTED,font=('Segoe UI',10),wraplength=340,justify='left',anchor='w');self.fault_label.pack(fill='x')
        ttk.Button(controls,text='Xem nhật ký lỗi',command=lambda:self.tabs.select(self.diagnostics)).pack(anchor='w',pady=(5,0))
        row=ttk.Frame(controls,style='Card.TFrame');row.pack(fill='x',pady=7)
        self.clear_button=ttk.Button(row,text='Xóa lỗi',command=lambda:self.send('CLEAR'));self.clear_button.pack(side='left',fill='x',expand=True,padx=(0,4))
        self.drain_button=ttk.Button(row,text='Dừng xả',command=self.panic);self.drain_button.pack(side='left',fill='x',expand=True,padx=(4,0))
        self.panic_button=ttk.Button(controls,text='TẮT NGAY  ·  Esc',style='Danger.TButton',command=self.panic);self.panic_button.pack(fill='x',pady=3)
        self.limit_var=tk.StringVar(value='Giới hạn thực: —')
        ttk.Label(controls,textvariable=self.limit_var,background=CARD,foreground=MUTED,font=('Segoe UI',9),wraplength=320,justify='left').pack(anchor='w',pady=(8,0))

    def _metric(self,parent,column,label,value,unit,color):
        frame=ttk.Frame(parent,style='Card.TFrame',padding=(15,14));frame.grid(row=0,column=column,sticky='nsew',padx=(0,8 if column==0 else 0))
        ttk.Label(frame,text=label,background=CARD,foreground=MUTED,font=('Segoe UI',9)).pack(anchor='w')
        row=ttk.Frame(frame,style='Card.TFrame');row.pack(fill='x',pady=(7,0))
        number=tk.Label(row,text=value,bg=CARD,fg=color,font=('Consolas',34));number.pack(side='left')
        tk.Label(row,text=unit,bg=CARD,fg=MUTED,font=('Segoe UI',17)).pack(side='left',padx=6,pady=(13,0));return number

    def _configuration(self):
        top=ttk.Frame(self.config_page,padding=(0,12,0,9));top.pack(fill='x')
        self.read_button=ttk.Button(top,text='Đọc board',command=lambda:self.send('GET'));self.read_button.pack(side='left',padx=(0,6))
        self.config_button=ttk.Button(top,text='Áp dụng cấu hình',style='Accent.TButton',command=self.apply_config);self.config_button.pack(side='left',padx=3)
        self.save_button=ttk.Button(top,text='Lưu Flash',command=self.save_device);self.save_button.pack(side='left',padx=3)
        self.defaults_button=ttk.Menubutton(top,text='Bảo trì');self.defaults_button.pack(side='left',padx=3)
        maintenance=tk.Menu(self.defaults_button,tearoff=False)
        maintenance.add_command(label='Khôi phục cấu hình mặc định',command=lambda:self.send('DEFAULTS',self.revision))
        self.defaults_button.configure(menu=maintenance)
        ttk.Button(top,text='Lưu profile',command=self.export_profile).pack(side='right',padx=3)
        ttk.Button(top,text='Nạp profile',command=self.import_profile).pack(side='right',padx=3)
        self.dirty_var=tk.StringVar(value='Giá trị mặc định · chưa đọc board')
        ttk.Label(self.config_page,textvariable=self.dirty_var,foreground=AMBER).pack(anchor='w',pady=(0,4))
        ttk.Label(self.config_page,text='Chỉ áp dụng/lưu khi LAB hoặc SOLDER đã OFF và đã dừng xả. Mỗi thay đổi đều được board kiểm tra lại.',style='Muted.TLabel').pack(anchor='w',pady=(0,8))
        self.caps_var=tk.StringVar(value='Trần BBV6.14: VIN <60 V · VOUT ≤59,9 V · 15 A liên tục / 20 A đỉnh · 600 W · IIN ≤10 A')
        ttk.Label(self.config_page,textvariable=self.caps_var,foreground=CYAN,wraplength=1000).pack(anchor='w',pady=(0,8))
        wrapper=ttk.Frame(self.config_page);wrapper.pack(fill='both',expand=True)
        canvas=tk.Canvas(wrapper,bg=BG,highlightthickness=0);scroll=ttk.Scrollbar(wrapper,orient='vertical',command=canvas.yview)
        self.config_canvas=canvas
        canvas.configure(yscrollcommand=scroll.set);scroll.pack(side='right',fill='y');canvas.pack(side='left',fill='both',expand=True)
        inner=ttk.Frame(canvas);win=canvas.create_window((0,0),window=inner,anchor='nw')
        inner.bind('<Configure>',lambda _:canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>',lambda e:canvas.itemconfigure(win,width=e.width))
        groups={};counts={};inner.columnconfigure((0,1),weight=1)
        for field in sorted(SCHEMA['fields'],key=lambda f:0 if f['key']=='ramp_v_per_s' else 1 if f['key'] in SOLDER_FIELDS else 2):
            group=field['group']
            if group not in groups:
                idx=len(groups);box=ttk.LabelFrame(inner,text=group,padding=14)
                box.grid(row=idx//2,column=idx%2,sticky='nsew',padx=(0,10),pady=(0,12));box.columnconfigure(1,weight=1);groups[group]=box;counts[group]=0
            box=groups[group];row=counts[group];counts[group]+=1
            ttk.Label(box,text=field['label'],background=CARD).grid(row=row,column=0,sticky='w',padx=(0,12),pady=5)
            var=tk.StringVar();self.field_vars[field['key']]=var
            step=10**(-field['decimals'])
            entry=ttk.Spinbox(box,textvariable=var,from_=field['minimum'],to=field['maximum'],increment=step,width=10)
            entry.grid(row=row,column=1,sticky='ew',pady=5)
            if field['key'] in THERMAL_FIELDS:self.thermal_inputs[field['key']]=entry;entry.configure(state='disabled')
            if field['key']=='ramp_v_per_s':
                self.ramp_input=entry;entry.configure(state='disabled')
                ttk.Label(box,text='Áp dụng khi OFF. Tăng Vset đi qua ramp;\ngiảm Vset có hiệu lực ngay.',background=CARD,foreground=MUTED,font=('Segoe UI',9)).grid(row=1,column=0,columnspan=3,sticky='w',pady=(7,0))
            if field['key'] in SOLDER_FIELDS:
                self.solder_inputs[field['key']]=entry;entry.configure(state='disabled')
                if field['key']=='solder_current':
                    ttk.Label(box,text='Dùng chung C245/C210 · chỉ sửa khi OFF.\nÁp dụng cấu hình → Lưu Flash để giữ sau reset.',background=CARD,foreground=MUTED,font=('Segoe UI',9)).grid(row=2,column=0,columnspan=3,sticky='w',pady=(7,0))
            ttk.Label(box,text=field['unit'],background=CARD,foreground=MUTED,width=9).grid(row=row,column=2,sticky='w',padx=(7,0))
            var.trace_add('write',lambda *_:self._config_edited())

    def _diagnostics(self):
        top=ttk.Frame(self.diagnostics,padding=(0,12));top.pack(fill='x')
        self.csv_button=ttk.Button(top,text='Bắt đầu ghi CSV',command=self.toggle_csv);self.csv_button.pack(side='left')
        ttk.Button(top,text='Sao chép mẫu mới nhất',command=self.copy_sample).pack(side='left',padx=8)
        ttk.Button(top,text='Xuất nhật ký lỗi',command=self.export_errors).pack(side='left',padx=8)
        pages=ttk.Notebook(self.diagnostics);pages.pack(fill='both',expand=True)
        errors=ttk.Frame(pages);raw=ttk.Frame(pages);pages.add(errors,text='Lịch sử lỗi');pages.add(raw,text='Thông số thô')
        holder=ttk.Frame(errors);holder.pack(fill='both',expand=True,pady=10)
        self.fault_tree=ttk.Treeview(holder,columns=('time','kind','summary'),show='headings',height=9)
        for key,label,width in [('time','Thời gian',130),('kind','Loại',105),('summary','Sự kiện',720)]:
            self.fault_tree.heading(key,text=label);self.fault_tree.column(key,width=width,stretch=key=='summary')
        bar=ttk.Scrollbar(holder,orient='vertical',command=self.fault_tree.yview);bar.pack(side='right',fill='y')
        self.fault_tree.configure(yscrollcommand=bar.set);self.fault_tree.pack(fill='both',expand=True)
        self.fault_tree.bind('<<TreeviewSelect>>',self.show_error_detail)
        for tag,color in [('protection',RED),('warning',AMBER),('cleared',GREEN),('complete',GREEN)]:self.fault_tree.tag_configure(tag,foreground=color)
        self.fault_detail=ScrolledText(errors,height=7,bg=PANEL,fg=TEXT,font=('Consolas',9),relief='flat',padx=12,pady=10,state='disabled');self.fault_detail.pack(fill='x')
        ttk.Label(errors,text='Ghi tự động khi app nhận dữ liệu · JSONL tại '+str(self.error_journal.folder),style='Muted.TLabel',wraplength=1050).pack(anchor='w',pady=6)
        self.diag_text=ScrolledText(raw,height=20,bg=PANEL,fg=TEXT,insertbackground=TEXT,font=('Consolas',10),relief='flat',padx=14,pady=12,state='disabled');self.diag_text.pack(fill='both',expand=True)
        self.log_text=ScrolledText(raw,height=5,bg=BG,fg=MUTED,font=('Consolas',9),relief='flat',padx=8,pady=8,state='disabled');self.log_text.pack(fill='x',pady=(8,0))

    def append_error(self,event):
        self.fault_sequence+=1;row=str(self.fault_sequence);self.fault_rows[row]=event
        label={'protection':'Bảo vệ','warning':'Cảnh báo','cleared':'Đã xóa','complete':'Hoàn tất','app_error':'Ứng dụng'}.get(event['kind'],event['kind'])
        self.fault_tree.insert('',0,iid=row,values=(event['time'][11:23],label,event['summary']),tags=(event['kind'],))
        for old in self.fault_tree.get_children()[500:]:self.fault_tree.delete(old);self.fault_rows.pop(old,None)
        self.fault_tree.selection_set(row);self.show_error_detail()

    def show_error_detail(self,_=None):
        selected=self.fault_tree.selection()
        if not selected:return
        self.fault_detail.configure(state='normal');self.fault_detail.delete('1.0','end')
        self.fault_detail.insert('end',json.dumps(self.fault_rows[selected[0]],ensure_ascii=False,indent=2));self.fault_detail.configure(state='disabled')

    def export_errors(self):
        if not self.error_journal.path.exists():self.note('Chưa có lỗi để xuất.');return
        path=filedialog.asksaveasfilename(title='Xuất nhật ký lỗi',defaultextension='.jsonl',filetypes=[('JSON Lines','*.jsonl')])
        if path:
            try:Path(path).write_text(self.error_journal.path.read_text(encoding='utf-8'),encoding='utf-8');self.note('Đã xuất nhật ký lỗi.')
            except OSError as exc:self.note('Không xuất được nhật ký: '+str(exc),True)

    def _footer(self):
        row=ttk.Frame(self,padding=(22,10));row.pack(side='bottom',fill='x',before=self.tabs)
        self.notice_var=tk.StringVar(value='Chưa kết nối. Ngắt USB không tự tắt output trên board.')
        ttk.Label(row,textvariable=self.notice_var,foreground=MUTED,wraplength=1050).pack(anchor='w')

    def note(self,text,error=False):
        self.notice_var.set(text);stamp=datetime.now(VN).strftime('%H:%M:%S')
        self.event_log.append(f'{stamp}  {text}')
        self.log_text.configure(state='normal');self.log_text.delete('1.0','end');self.log_text.insert('end','\n'.join(self.event_log));self.log_text.see('end');self.log_text.configure(state='disabled')
        if error:
            try:self.append_error(self.error_journal.record('app_error',text,device=self.port_var.get(),demo=self.demo))
            except OSError:self.notice_var.set(text+' · Không ghi được file nhật ký.')
            self.bell()

    def scan_ports(self):
        if self.worker:return
        try:self.port_items={f'{p.device} · {p.description}':p.device for p in ports()}
        except OSError as exc:self.note(str(exc),True);return
        self.port_combo['values']=list(self.port_items)
        if self.port_var.get() not in self.port_items:self.port_var.set(next(iter(self.port_items),'Không tìm thấy cổng COM'))

    def toggle_connection(self):
        if self.worker:
            self.worker.stop();self.connect_button.configure(text='Đang ngắt…',state='disabled');return
        port=self.port_items.get(self.port_var.get())
        if not port:self.note('Chưa có cổng COM. Kiểm tra cáp USB data và nạp firmware mới.',True);return
        self.demo=False;self.worker=SerialWorker(port,self.events);self.connect_button.configure(text='Đang mở…',state='disabled');self.worker.start()

    def connect_demo(self):
        if self.worker:self.note('Ngắt kết nối hiện tại trước khi vào DEMO.');return
        self.demo=True;self.port_var.set('DEMO · không kết nối COM');self.worker=DemoWorker(self.events);self.worker.start()

    def _wheel(self,event):
        selected=self.tabs.select()
        if selected==str(self.config_page):target=self.config_canvas
        elif selected==str(self.dashboard) and event.x_root>=self.control_canvas.winfo_rootx():target=self.control_canvas
        else:return
        target.yview_scroll(int(-event.delta/120),'units');return 'break'

    def send(self,op,*args,settings=None):
        if not self.worker or not self.connected:return
        if op!='GET' and not self.ready:self.note('Chưa xác nhận giao thức cấu hình. Đọc board hoặc nạp firmware BBV6.2.',True);return
        try:
            identifier=self.worker.submit(op,*args,settings=settings);self.pending[identifier]=op
            self.note(f'Đã gửi {op} · chờ board xác nhận…');self.refresh_controls();return identifier
        except (ValueError,queue.Full) as exc:self.note(str(exc) or 'Hàng đợi lệnh đang đầy.',True)

    def _basic_edited(self):
        if not self.loading:self.basic_dirty=True

    def _config_edited(self):
        if not self.loading:self.dirty=True;self.dirty_var.set('● Thay đổi chưa áp dụng lên board')

    def load_editor(self,settings):
        self.loading=True
        for key,var in self.field_vars.items():
            field=FIELDS[key];var.set(f"{physical(settings,key):.{field['decimals']}f}")
        self.loading=False;self.dirty=False
        self.dirty_var.set('Cấu hình trên board · không có thay đổi' if self.ready else 'Giá trị mặc định · chưa đọc board')

    def editor_settings(self):
        result={key:scaled(key,var.get()) for key,var in self.field_vars.items()};validate(result);return result

    def setpoint(self):
        try:mv=int(round(float(self.voltage_var.get().replace(',','.'))*1000));ma=int(round(float(self.current_var.get().replace(',','.'))*1000))
        except (ValueError,OverflowError) as exc:raise ValueError('Điện áp và dòng cần là số hữu hạn.') from exc
        if self.latest.get('mode')==1:
            ch=self.latest.get('charge',{});mv=ch.get('cells',1)*(3650 if ch.get('chem')==1 else 4200)
            if ma<100:raise ValueError('Dòng sạc tối thiểu 0,10 A.')
        if not 1500<=mv<=self.settings['vout_max'] or not 50<=ma<=self.settings['i_max']:raise ValueError('Setpoint vượt giới hạn cấu hình trên board.')
        return mv,ma

    def apply_setpoint(self):
        try:self.send('SET',*self.setpoint())
        except ValueError as exc:self.note(str(exc),True)

    def toggle_output(self):
        if self.latest.get('enabled') or self.latest.get('change') or self.latest.get('mode')==1 and self.latest.get('charge',{}).get('phase')==1:self.send('OFF');return
        if self.basic_dirty:
            try:
                values=self.setpoint();self.on_after_set=True
                if not self.send('SET',*values):self.on_after_set=False
            except ValueError as exc:self.on_after_set=False;self.note(str(exc),True)
        else:self.send('ON')

    def panic(self):
        self.on_after_set=False
        if self.worker and self.connected:self.worker.panic();self.note('Yêu cầu OFF ưu tiên; đang chờ board xác nhận.')

    def apply_config(self):
        try:
            candidate=self.editor_settings()
            if not self.thermal_supported:
                for key in THERMAL_FIELDS:
                    if candidate[key]!=self.settings[key]:raise ValueError('Firmware BBV6.48 required for thermal PID')
                    candidate.pop(key)
            if self.solder_supported:
                if candidate['solder_voltage']>candidate['vout_max'] or candidate['solder_current']>candidate['i_max']:
                    raise ValueError('Áp/dòng tay hàn không được vượt VOUT tối đa và dòng liên tục trong cấu hình.')
            else:
                for key in SOLDER_FIELDS:
                    if candidate[key]!=self.settings[key]:raise ValueError('Nạp firmware BBV6.28 để chỉnh áp/dòng tay hàn.')
                    candidate.pop(key)
            for field,cap in (('vin_max','vin_mV'),('vout_max','vout_mV'),('i_max','i_mA'),('i_peak','ipeak_mA'),('power_max','power_mW'),('input_current_max','input_mA')):
                if cap in self.caps and candidate[field]>self.caps[cap]:
                    raise ValueError(f'{field}: vượt trần firmware đang nối. Nạp BBV6.14 để dùng giới hạn mới.')
            if not self.ramp_supported:
                if candidate['ramp_v_per_s']!=self.settings['ramp_v_per_s']:
                    raise ValueError('Nạp firmware BBV6.8 để chỉnh ramp. Board hiện tại dùng ramp cố định.')
                candidate.pop('ramp_v_per_s')
            self.send('CFG',self.revision,settings=candidate)
        except ValueError as exc:self.note(str(exc),True)

    def show_ramp(self):
        self.tabs.select(self.config_page);self.config_canvas.yview_moveto(0)
        if self.ramp_supported:self.ramp_input.focus_set()
        else:self.note('Nạp BBV6.8 để chỉnh ramp; firmware cũ vẫn đọc và chỉnh PID bình thường.')

    def show_solder(self):
        self.tabs.select(self.config_page);self.config_canvas.yview_moveto(0)
        if self.solder_supported:self.solder_inputs['solder_voltage'].focus_set()
        else:self.note('Nạp firmware BBV6.28 để chỉnh điện áp và dòng cấp tay hàn.')

    def save_device(self):
        if self.dirty:self.note('Áp dụng cấu hình trước khi lưu Flash.',True);return
        self.send('SAVE')

    def load_preset(self,k):
        pair=getattr(self,'presets',[[5000,1000],[12000,3000],[24000,2000]])[k]
        self.voltage_var.set(f'{pair[0]/1000:.2f}');self.current_var.set(f'{pair[1]/1000:.2f}')
        self.note(f'Đã chọn M{k+1}; nhấn Áp dụng V/A để gửi.')

    def assign_preset(self,k):
        try:self.send('PRESET',k,*self.setpoint())
        except ValueError as exc:self.note(str(exc),True)

    def import_profile(self):
        path=filedialog.askopenfilename(title='Nạp profile cấu hình',filetypes=[('BB V6 profile','*.json')])
        if not path:return
        try:
            obj=load_profile(Path(path));self.load_editor(obj['settings']);self.dirty=True;self.dirty_var.set('● Profile đã nạp vào form, chưa áp dụng lên board')
            self.voltage_var.set(f"{obj['set_mV']/1000:.2f}");self.current_var.set(f"{obj['set_mA']/1000:.2f}")
            self.note('Profile đã nạp. Áp dụng cấu hình, rồi gửi V/A khi cần.')
        except (ValueError,OSError) as exc:self.note(str(exc),True)

    def export_profile(self):
        try:
            settings=self.editor_settings();mv=int(round(float(self.voltage_var.get().replace(',','.'))*1000));ma=int(round(float(self.current_var.get().replace(',','.'))*1000))
            validate(settings)
            path=filedialog.asksaveasfilename(title='Lưu profile',defaultextension='.json',filetypes=[('BB V6 profile','*.json')])
            if path:save_profile(Path(path),settings,mv,ma);self.note('Đã lưu profile trên máy tính.')
        except (ValueError,OverflowError,OSError) as exc:self.note(str(exc),True)

    def toggle_csv(self):
        if self.csv_file:self.stop_csv();return
        path=filedialog.asksaveasfilename(title='Ghi dữ liệu đo',defaultextension='.csv',filetypes=[('CSV','*.csv')])
        if not path:return
        try:
            self.csv_file=open(path,'w',newline='',encoding='utf-8-sig')
            self.csv_fields=['host_time_vn','ms','vin_mV','vout_mV','iout_mA','temp_dC','enabled','state','fault','set_mV','set_mA','ilim_mA','ratio_pm','buck_pm','boost_ho_pm','adc_age','ref_mV']
            self.csv_writer=csv.DictWriter(self.csv_file,fieldnames=self.csv_fields);self.csv_writer.writeheader();self.csv_rows=0
            self.csv_button.configure(text='Dừng ghi CSV');self.note('Đang ghi CSV · '+path)
        except OSError as exc:self.stop_csv();self.note(str(exc),True)

    def stop_csv(self):
        if self.csv_file:self.csv_file.close()
        self.csv_file=self.csv_writer=None;self.csv_button.configure(text='Bắt đầu ghi CSV')

    def copy_sample(self):
        self.clipboard_clear();self.clipboard_append(json.dumps({k:v for k,v in self.latest.items() if not k.startswith('_')},ensure_ascii=False));self.note('Đã sao chép mẫu JSON mới nhất.')

    def handle_reply(self,reply):
        self.pending.pop(reply.get('id'),None)
        if reply.get('protocol')!=1:return
        try:settings=upgrade_settings(reply['settings'])
        except (ValueError,KeyError,TypeError) as exc:self.ready=False;self.note('Cấu hình trả về không tương thích: '+str(exc),True);return
        self.ready=True;self.settings=settings;self.revision=reply['rev'];self.presets=reply['presets']
        if isinstance(reply.get('caps'),dict):self.caps={k:v for k,v in reply['caps'].items() if type(v) is int and v>0}
        c=self.caps
        self.caps_var.set(f"Trần board: VIN {c.get('vin_mV',0)/1000:g} V · VOUT {c.get('vout_mV',0)/1000:g} V · {c.get('i_mA',0)/1000:g} A liên tục / {c.get('ipeak_mA',0)/1000:g} A đỉnh"+(f" · {c['power_mW']/1000:g} W" if 'power_mW' in c else '')+(f" · IIN {c['input_mA']/1000:g} A" if 'input_mA' in c else ''))
        self.ramp_supported='ramp_v_per_s' in reply['settings']
        self.solder_supported=SOLDER_FIELDS<=set(reply['settings'])
        self.thermal_supported=THERMAL_FIELDS<=set(reply['settings'])
        for entry in self.thermal_inputs.values():entry.configure(state='normal' if self.thermal_supported else 'disabled')
        for entry in self.solder_inputs.values():entry.configure(state='normal' if self.solder_supported else 'disabled')
        self.ramp_input.configure(state='normal' if self.ramp_supported else 'disabled')
        self.ramp_status_var.set(f"Ramp tăng áp · {self.settings['ramp_v_per_s']/1000:g} V/s"+('' if self.ramp_supported else ' (cố định)'))
        self.device_label.configure(text=('DEMO · Dữ liệu giả lập, không điều khiển board' if self.demo else reply.get('fw','BB V6')+' · USB CDC · Đã xác nhận giao thức'))
        op=reply.get('op')
        if reply.get('ok') and (not self.dirty or op in ('CFG','DEFAULTS')):self.load_editor(self.settings)
        if not self.basic_dirty or (op=='SET' and reply.get('ok')):
            self.loading=True;self.voltage_var.set(f"{reply['set_mV']/1000:.2f}");self.current_var.set(f"{reply['set_mA']/1000:.2f}");self.loading=False;self.basic_dirty=False
        for k,btn in enumerate(self.preset_buttons):
            v,a=self.presets[k];btn.configure(text=f'M{k+1}\n{v/1000:g} V / {a/1000:g} A')
        if not reply.get('ok'):
            self.on_after_set=False;error=reply.get('error','unknown')
            self.note(ERRORS.get(error,'Board từ chối: '+error)+(' '+block_text(reply.get('start_block',0)) if error=='blocked' else ''),True)
        else:
            self.note('Đã lưu cấu hình và preset vào Flash.' if op=='SAVE' else f'Board đã xác nhận {op}.')
            if op=='SET' and self.on_after_set:self.on_after_set=False;self.send('ON')

    def telemetry(self,data):
        required=['ms','vin_mV','vout_mV','iout_mA','temp_dC','fault','state','set_mV','set_mA']
        if any(type(data.get(k)) is not int for k in required):return
        ms=data['ms']
        if self.last_ms is not None and ms<self.last_ms and self.last_ms-ms<2147483648:
            self.ready=False;self.on_after_set=False;self.pending.clear();self.history.clear()
            if self.worker:self.worker.panic()
            self.note('Board vừa khởi động lại; đã yêu cầu OFF và đọc lại cấu hình.')
        previous_mode=self.latest.get('mode',0)
        self.last_ms=ms;self.last_rx=data.get('_received',time.monotonic());self.latest=data
        if data.get('mode',0)!=previous_mode:self.basic_dirty=False;self.on_after_set=False
        if data.get('mode')==1:
            ch=data.get('charge',{});self.loading=True
            self.voltage_var.set(f"{ch.get('cells',1)*(3.65 if ch.get('chem')==1 else 4.2):.2f}")
            if not self.basic_dirty:self.current_var.set(f"{ch.get('set_mA',500)/1000:.2f}")
            self.loading=False
        self.voltage_readout.configure(text=f"{data['vout_mV']/1000:05.2f}");self.current_readout.configure(text=f"{data['iout_mA']/1000:05.2f}")
        self.vin_var.set(f"VIN  {data['vin_mV']/1000:.2f} V");self.temp_var.set(f"FET  {data['temp_dC']/10:.1f} °C" if data.get('temp_valid') else 'FET  SENSOR ?')
        self.power_var.set(f"{data['vout_mV']*data['iout_mA']/1e6:.2f} W")
        for key,var in (('buck_pm',self.buck_duty_var),('boost_ho_pm',self.boost_duty_var)):
            pm=data.get(key)
            var.set(f'{pm/10:.1f} %' if type(pm) in (int,float) and 0<=pm<=1000 else '—')
        drain=data.get('drain',{}).get('state',0);mode='FAULT' if data['fault'] else 'DRAIN' if drain==2 else 'CC' if data['state']==3 else 'RAMP' if data.get('enabled') and data['state']==1 else 'CV' if data.get('enabled') else 'OFF'
        charge=data.get('charge',{})
        if not data['fault'] and data.get('mode')==1:mode='CHG '+{0:'OFF',1:'CHECK',2:'PRE',3:'CC' if data['state']==3 else 'CV',4:'FULL',5:'STOP',6:'ERROR'}.get(charge.get('phase'),'?')
        elif not data['fault'] and data.get('mode')==2:
            sol=data.get('solder',{})
            mode='SOLDER ERROR' if sol.get('reason') else 'SOLDER STAND' if sol.get('stand') and data.get('enabled') else 'SOLDER HEAT' if sol.get('phase')==3 else 'SOLDER WAIT' if data.get('enabled') else 'SOLDER ON' if data.get('change') else 'SOLDER OFF'
        self.mode_var.set(mode);self.applied_var.set(f"Đã áp dụng: {data['set_mV']/1000:.2f} V / {data['set_mA']/1000:.2f} A")
        self.mode_label.configure(foreground=RED if mode in ('FAULT','CHG ERROR','SOLDER ERROR') else AMBER if mode in ('DRAIN','RAMP','CHG CHECK','CHG PRE','SOLDER WAIT','SOLDER STAND') else GREEN if mode in ('CC','CHG FULL','SOLDER ON','SOLDER HEAT') else CYAN if mode in ('CV','CHG CV','CHG CC') else MUTED)
        sec=data.get('time_s',0);self.run_info_var.set(f'TIME {sec//3600:02d}:{sec//60%60:02d}:{sec%60:02d}'+(f" · {charge.get('mAh10',0)/10:.1f} mAh" if data.get('mode')==1 else ''))
        if type(data.get('ramp_mV_s')) is int:
            self.ramp_status_var.set(f"Ramp tăng áp · {data['ramp_mV_s']/1000:g} V/s"+(' · đang tăng' if data.get('ramp_active') else ''))
        available_power=min(self.settings['power_max']/1000,self.settings['input_current_max']/1000*data['vin_mV']/1000*self.settings['efficiency']/1000)
        self.limit_var.set(f"Giới hạn thực: {data.get('ilim_mA',0)/1000:.2f} A · P cho phép ≈{available_power:.1f} W\nIIN ước lượng ≤{self.settings['input_current_max']/1000:g} A · ADC {data.get('adc_age','?')} ms")
        info=fault_text(data['fault']);color=RED if data['fault'] else GREEN
        if data.get('config'):info=f"Khóa cấu hình phần cứng: 0x{data['config']:X}";color=RED
        elif not data['fault'] and data.get('start_block'):info=block_text(data['start_block']);color=AMBER
        elif not data['fault'] and data.get('mode')==1:
            info='Sạc đã dừng: '+CHARGE_REASONS.get(charge['reason'],str(charge['reason'])) if charge.get('reason') else 'Sạc hoàn tất · output đã tắt' if charge.get('phase')==4 else f"{'LiFePO₄' if charge.get('chem')==1 else 'Li-ion/LiPo'} {charge.get('cells',1)}S · BMS ngoài · chọn loại pin trên thiết bị"
            color=RED if charge.get('reason') else GREEN
        elif not data['fault'] and data.get('mode')==2:
            sol=data.get('solder',{})
            tip='C210' if sol.get('tip')==1 else 'C245'
            adc=f"ADC PA3 {sol['adc']} / 4095" if type(sol.get('adc')) is int else 'ADC PA3 chưa có dữ liệu'
            reading=adc if sol.get('protect')==0 else f"{sol.get('temp_dC',0)/10:.1f}°C" if sol.get('valid') else 'chưa có dữ liệu nhiệt'
            info='Tay hàn dừng: '+SOLDER_REASONS.get(sol['reason'],str(sol['reason'])) if sol.get('reason') else f"{tip} · {reading} · SET {sol.get('active_C',sol.get('target_C',300))}°C · PWM {sol.get('duty_pm',0)/10:.1f}% · {'đang gác' if sol.get('stand') else 'đang dùng'}"
            if sol.get('protect')==0:info+=' · BẢO VỆ NHIỆT MŨI TẮT'
            color=RED if sol.get('reason') else AMBER if sol.get('stand') or sol.get('protect')==0 else GREEN
        elif not data['fault'] and drain>=4:info={4:'Xả dừng: timeout',5:'Xả dừng: điện áp không giảm',6:'Xả dừng: điện áp tăng',7:'Xả đã hủy'}.get(drain,'Xả dừng');color=AMBER
        elif not data['fault'] and drain==2:info='Đang xả tụ nhẹ đến 0,5 V. IOUT không đo dòng xả.';color=AMBER
        elif not data['fault'] and data.get('enabled') and data.get('cv_sat'):info='Lệnh đã chạm trần: đối chiếu PWM và nguồn driver.';color=AMBER
        self.fault_label.configure(text=info,fg=color)
        try:
            event=self.error_journal.observe(data,self.device_label.cget('text'),self.demo,self.settings if self.ready else None)
            if event:self.append_error(event)
        except OSError as exc:self.notice_var.set('Không ghi được nhật ký lỗi: '+str(exc))
        self.history.append((self.last_rx,data['vout_mV']/1000,data['iout_mA']/1000));self.draw_chart()
        if self.csv_writer:
            try:
                row={key:data.get(key,'') for key in self.csv_fields};row['host_time_vn']=datetime.now(VN).isoformat(timespec='milliseconds')
                self.csv_writer.writerow(row);self.csv_rows+=1
                if self.csv_rows%10==0:self.csv_file.flush()
            except OSError as exc:self.stop_csv();self.note('Không ghi được CSV: '+str(exc),True)
        if time.monotonic()-self.last_diag>.5:
            self.last_diag=time.monotonic();self.diag_text.configure(state='normal');self.diag_text.delete('1.0','end')
            self.diag_text.insert('end',json.dumps({k:v for k,v in data.items() if not k.startswith('_')},ensure_ascii=False,indent=2));self.diag_text.configure(state='disabled')

    def draw_chart(self):
        canvas=self.chart;canvas.delete('all');w=canvas.winfo_width();h=canvas.winfo_height()
        if w<100 or h<80:return
        x0,y0,x1,y1=47,20,w-45,h-28;now=time.monotonic()
        points=[p for p in self.history if now-p[0]<=60]
        vmax=max(6.,self.latest.get('set_mV',5000)/1000*1.15,max((p[1] for p in points),default=0)*1.1)
        imax=max(1.2,self.latest.get('set_mA',1000)/1000*1.15,max((p[2] for p in points),default=0)*1.1)
        for i in range(5):
            y=y1-(y1-y0)*i/4;canvas.create_line(x0,y,x1,y,fill=LINE)
            canvas.create_text(x0-8,y,text=f'{vmax*i/4:.1f}',anchor='e',fill=MUTED,font=('Segoe UI',8))
            canvas.create_text(x1+7,y,text=f'{imax*i/4:.2f}',anchor='w',fill=MUTED,font=('Segoe UI',8))
        for seconds in (60,45,30,15,0):
            x=x0+(60-seconds)/60*(x1-x0);canvas.create_text(x,y1+14,text=f'−{seconds}s' if seconds else '0s',fill=MUTED,font=('Segoe UI',8))
        canvas.create_text(x0,y0-10,text='V',fill=CYAN,font=('Segoe UI',8));canvas.create_text(x1,y0-10,text='A',fill=GREEN,font=('Segoe UI',8))
        for index,maximum,color in [(1,vmax,CYAN),(2,imax,GREEN)]:
            coords=[]
            for point in points:coords.extend((x0+max(0,min(1,(point[0]-now+60)/60))*(x1-x0),y1-max(0,min(1,point[index]/maximum))*(y1-y0)))
            if len(coords)>=4:canvas.create_line(*coords,fill=color,width=2)

    def refresh_controls(self):
        fresh=self.connected and time.monotonic()-self.last_rx<1.5
        operating_mode=self.latest.get('mode',0)
        active=bool(self.latest.get('enabled') or self.latest.get('change') or operating_mode==1 and self.latest.get('charge',{}).get('phase')==1);drain=self.latest.get('drain',{}).get('state',0)
        free=self.ready and fresh and not self.pending;idle=free and not active and drain not in (1,2)
        self.output_button.configure(text='TẮT OUTPUT' if active else 'BẬT OUTPUT',state='normal' if free and (active or (operating_mode!=2 and not self.latest.get('fault') and self.latest.get('ready_ms',150)>=100)) else 'disabled')
        self.set_button.configure(state='normal' if free and operating_mode!=2 else 'disabled')
        self.setpoint_entries['V'].configure(state='normal' if operating_mode==0 else 'readonly')
        self.setpoint_entries['A'].configure(state='disabled' if operating_mode==2 else 'normal')
        for button in [self.config_button,self.defaults_button,self.save_button]:button.configure(state='normal' if idle and (operating_mode==0 or operating_mode==2 and self.solder_supported) else 'disabled')
        for button in self.assign_buttons:button.configure(state='normal' if idle and operating_mode==0 else 'disabled')
        self.read_button.configure(state='normal' if self.connected and not self.pending else 'disabled')
        self.clear_button.configure(state='normal' if free and not active and self.latest.get('fault') and self.latest.get('ready_ms',0)>=100 else 'disabled')
        self.drain_button.configure(state='normal' if self.connected and drain in (1,2) else 'disabled')
        self.panic_button.configure(state='normal' if self.connected else 'disabled')
        self.header_panic.configure(state='normal' if self.connected else 'disabled')
        self.badge.configure(text='● DEMO · KHÔNG KẾT NỐI BOARD' if self.demo and self.connected else '● LIVE' if fresh else '● MẤT DỮ LIỆU' if self.connected else '● OFFLINE',fg=AMBER if self.demo else GREEN if fresh else MUTED)
        self.port_combo.configure(state='disabled' if self.worker else 'readonly')

    def poll(self):
        for _ in range(120):
            try:kind,value=self.events.get_nowait()
            except queue.Empty:break
            if kind=='connected':
                self.connected=True;self.ready=False;self.last_ms=None;self.pending.clear();self.connect_button.configure(text='Ngắt kết nối',state='normal');self.send('GET')
            elif kind=='disconnected':
                self.worker=None;self.connected=self.ready=False;self.pending.clear();self.on_after_set=False
                self.connect_button.configure(text='Kết nối',state='normal');self.device_label.configure(text='Đã ngắt USB · chưa xác nhận trạng thái output');self.note('Đã ngắt kết nối. Output trên board không tự tắt theo app.')
                self.scan_ports()
            elif kind=='reply':self.handle_reply(value)
            elif kind=='telemetry':self.telemetry(value)
            elif kind=='error':self.pending.clear();self.ready=False;self.on_after_set=False;self.note(value,True)
            elif kind=='notice':self.pending.clear();self.on_after_set=False;self.note(value)
        self.refresh_controls();self.after(50,self.poll)

    def close(self):
        if self.worker:self.worker.stop()
        self.stop_csv();self.destroy()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--demo',action='store_true');parser.add_argument('--smoke-test',action='store_true');parser.add_argument('--report');args=parser.parse_args()
    app=Studio(args.demo or args.smoke_test)
    if args.smoke_test:app.withdraw()
    if args.smoke_test:app.error_journal=FaultJournal((Path(args.report).resolve().parent if args.report else Path(tempfile.mkdtemp()))/'fault_log_smoke')
    result={'ok':False}
    if args.smoke_test:
        def failed(kind,value,tb):
            result['error']=''.join(traceback.format_exception(kind,value,tb));app.close()
        app.report_callback_exception=failed
        def verify():
            assert app.connected and app.ready and app.demo
            assert len(app.field_vars)==43
            assert app.latest.get('protocol')==1 and not app.latest.get('enabled')
            app.apply_setpoint();app.after(350,verify_config)
        def verify_config():
            assert not app.pending
            app.field_vars['cv_kp'].set('0.02000');app.field_vars['ramp_v_per_s'].set('2.50')
            app.field_vars['solder_voltage'].set('9.00');app.field_vars['solder_current'].set('2.00')
            for key,value in dict(vin_max='55',vout_max='50',i_max='13',i_peak='18',power_max='400',input_current_max='9').items():app.field_vars[key].set(value)
            for key,value in dict(solder_kp='0.020000',solder_ki='0.003000',solder_kd='0.004000',solder_duty_max='42.0').items():app.field_vars[key].set(value)
            app.apply_config();app.after(350,finish)
        def finish():
            assert app.revision==1 and app.settings['cv_kp']==20000 and not app.dirty
            assert app.ramp_supported and app.settings['ramp_v_per_s']==2500
            assert app.solder_supported and app.settings['solder_voltage']==9000 and app.settings['solder_current']==2000
            assert all(app.settings[k]==v for k,v in dict(vin_max=55000,vout_max=50000,i_max=13000,i_peak=18000,power_max=400000,input_current_max=9000).items())
            assert app.thermal_supported and app.settings['solder_kp']==20000 and app.settings['solder_duty_max']==42000
            result['limits_configurable']=True
            app.save_device();app.after(350,verify_saved)
        def verify_saved():
            assert not app.pending and app.worker.backend.saved['ramp_v_per_s']==2500
            assert all(app.worker.backend.saved[k]==v for k,v in dict(solder_kp=20000,solder_ki=3000,solder_kd=4000,solder_duty_max=42000).items())
            result['thermal_pid_saved']=True
            assert app.worker.backend.saved['solder_voltage']==9000 and app.worker.backend.saved['solder_current']==2000
            legacy=dict(app.worker.backend.reply(type('Req',(),dict(identifier=0,op='GET'))()))
            legacy['settings']={k:v for k,v in legacy['settings'].items() if k not in SOLDER_FIELDS|THERMAL_FIELDS};legacy['fw']='BBV6.27'
            app.handle_reply(legacy)
            assert app.ready and app.ramp_supported and not app.solder_supported and not app.thermal_supported
            assert all(str(entry['state'])=='disabled' for entry in app.solder_inputs.values())
            legacy['settings'].pop('ramp_v_per_s');legacy['fw']='BBV6.7'
            app.handle_reply(legacy)
            assert app.ready and not app.ramp_supported and str(app.ramp_input['state'])=='disabled'
            result.update(config_fields=len(app.field_vars),ramp_saved_mV_s=2500,legacy_compatible=True)
            app.handle_reply(app.worker.backend.reply(type('Req',(),dict(identifier=0,op='GET'))()))
            assert app.solder_supported
            result.update(solder_voltage_saved_mV=9000,solder_current_saved_mA=2000)
            sample=dict(app.latest);sample.update(fault=1,ms=sample['ms']+1,trip={'cause':1,'io_adc':650})
            app.telemetry(sample);app.telemetry(sample)
            sample=dict(sample,fault=0,ms=sample['ms']+1);app.telemetry(sample)
            assert len(app.error_journal.entries)==2 and len(app.fault_tree.get_children())==2
            result['fault_events_logged']=2
            sample=dict(sample,buck_pm=900,boost_ho_pm=720,ms=sample['ms']+1)
            app.telemetry(sample)
            assert app.buck_duty_var.get()=='90.0 %' and app.boost_duty_var.get()=='72.0 %'
            sample=dict(sample,buck_pm=0,boost_ho_pm=1000,ms=sample['ms']+1);app.telemetry(sample)
            assert app.buck_duty_var.get()=='0.0 %' and app.boost_duty_var.get()=='100.0 %'
            sample=dict(sample,ms=sample['ms']+1);sample.pop('buck_pm');sample['boost_ho_pm']=None
            app.telemetry(sample)
            assert app.buck_duty_var.get()=='—' and app.boost_duty_var.get()=='—'
            result['duty_percent']=True;result['missing_duty_unknown']=True
            sample=dict(sample,mode=1,enabled=1,state=3,time_s=3723,ms=sample['ms']+1,
                        charge=dict(chem=1,cells=4,phase=3,reason=0,set_mA=500,end_mA=50,mAh10=1234))
            app.telemetry(sample);app.refresh_controls()
            assert app.mode_var.get()=='CHG CC' and app.voltage_var.get()=='14.60' and app.current_var.get()=='0.50'
            assert str(app.setpoint_entries['V']['state'])=='readonly'
            assert str(app.config_button['state'])=='disabled' and str(app.output_button['state'])=='normal'
            assert '01:02:03' in app.run_info_var.get() and '123.4 mAh' in app.run_info_var.get()
            sample=dict(sample,enabled=0,ms=sample['ms']+1,charge=dict(sample['charge'],phase=4))
            app.telemetry(sample);app.refresh_controls()
            assert app.mode_var.get()=='CHG FULL' and app.error_journal.entries[-1]['kind']=='complete'
            sample=dict(sample,ms=sample['ms']+1,charge=dict(sample['charge'],phase=6,reason=4))
            app.telemetry(sample)
            assert app.mode_var.get()=='CHG ERROR' and app.error_journal.entries[-1]['kind']=='protection'
            sample=dict(sample,mode=2,change=1,ms=sample['ms']+1,charge=dict(sample['charge'],reason=0))
            app.telemetry(sample);app.refresh_controls()
            assert app.mode_var.get()=='SOLDER ON' and str(app.output_button['state'])=='normal'
            assert str(app.setpoint_entries['A']['state'])=='disabled' and str(app.set_button['state'])=='disabled'
            sample=dict(sample,change=0,ms=sample['ms']+1);app.telemetry(sample);app.refresh_controls()
            assert app.mode_var.get()=='SOLDER OFF' and str(app.output_button['state'])=='disabled'
            assert str(app.config_button['state'])=='normal' and str(app.save_button['state'])=='normal'
            sample=dict(sample,enabled=1,change=1,ms=sample['ms']+1,solder=dict(tip=1,phase=3,reason=0,temp_dC=2985,adc=1449,protect=0,target_C=300,active_C=300,stand=0,valid=1,duty_pm=200))
            app.telemetry(sample);assert app.mode_var.get()=='SOLDER HEAT' and '1449' in app.fault_label.cget('text') and 'MŨI TẮT' in app.fault_label.cget('text')
            sample=dict(sample,ms=sample['ms']+1,solder=dict(sample['solder'],protect=1))
            app.telemetry(sample);assert '298.5' in app.fault_label.cget('text') and 'MŨI TẮT' not in app.fault_label.cget('text')
            sample=dict(sample,enabled=0,ms=sample['ms']+1,solder=dict(sample['solder'],reason=4,phase=4))
            app.telemetry(sample);assert app.mode_var.get()=='SOLDER ERROR' and app.error_journal.entries[-1]['data']['solder']['reason']==4
            result.update(charger_ui=True,charger_journal=True,solder_ui=True)
            result['ok']=True
            if sys.stdout:print('Tk UI/demo/configuration smoke test: PASS',flush=True)
            app.close()
        app.after(900,verify)
        app.after(8000,app.close)
    app.mainloop()
    if args.report:Path(args.report).write_text(json.dumps(result,indent=2),encoding='utf-8')
    if args.smoke_test and not result['ok']:raise SystemExit(1)

if __name__=='__main__':main()
