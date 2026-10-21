# BBV6.15 — NUMBER / GRAPH trên LCD

Giữ encoder 1 giây → **DISPLAY** → chọn **NUMBER** hoặc **GRAPH** → nhấn encoder.
BACK hoặc BTN2 quay về menu. Có thể đổi cách hiển thị khi output đang ON;
việc chọn màn hình không đổi V/A, PID, giới hạn bảo vệ, chế độ LAB/CHARGE/SOLDER
hoặc trạng thái output. BTN1 vẫn dừng output ngay sau debounce khi đang ON.

- **NUMBER:** bố cục số lớn hiện tại, digit đang chỉnh màu đỏ.
- **GRAPH:** điện áp màu xanh lá, dòng màu cyan; lịch sử khoảng 30 giây, cập nhật
  4 lần/giây. Trục bên trái tính V, trục bên phải tính A; hai thang đo tự điều chỉnh
  độc lập theo dữ liệu và mức đặt. Hai đường có thể cắt nhau dù giá trị khác đơn vị.
- GRAPH vẫn có VOUT/IOUT dạng số, SETV/SETA và digit đỏ, CV/CC/RAMP/OFF/lỗi,
  VIN, nhiệt FET, công suất, TIME, OUT bên trái và M1–M3 bên phải.
- Lịch sử tiếp tục thu khi NUMBER hoặc menu đang hiện, giữ qua ON/OFF và đổi view.
  Nếu mất mẫu/ADC không còn mới, đường bị ngắt tại khoảng thiếu dữ liệu.
- Lựa chọn view và lịch sử lưu RAM; reset trở về NUMBER và bắt đầu lịch sử mới.
  Không ghi Flash khi đổi màn hình, không thay journal cấu hình V3.

Đồ thị sử dụng giá trị đo đã lọc của firmware; đây là đồ thị xu hướng, không phải
ảnh sóng PWM hoặc công cụ đo xung dòng đỉnh. ADC DMA, bảo vệ thô hai mẫu liên tiếp
và vòng điều khiển 1 kHz vẫn chạy như trước.

Chỉ thêm 121 cặp mẫu uint16, bộ đệm tọa độ và trạng thái nhỏ; không dùng framebuffer
320×240. Vẽ bằng các hàng SPI trong foreground, không vẽ trong ngắt điều khiển.
Trình dựng thử dùng chính driver C/ST7789 và kiểm tra biên tọa độ, vòng đệm,
tràn tick 32-bit, khoảng mất mẫu, đổi về NUMBER và nút OFF trong menu.

Ảnh xem trước: `build/preview/graph_cv.png`, `graph_cc.png`, `graph_fault.png`,
`menu_display.png`. Chưa nạp/đo thời gian vẽ trực tiếp trên board trong lượt này.
App Windows v6.14 tiếp tục dùng được; protocol USB và cấu hình công suất giữ nguyên.
