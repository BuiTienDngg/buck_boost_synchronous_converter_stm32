# BBV6.22-TEST18 — sửa luồng giao diện gây TIMING

Log người dùng có `fault=256`, `timing_src=2`, `timing_value=251`: foreground đã không phục vụ trong 251 ms, vượt giới hạn 250 ms. `solder.reason=6` xuất hiện khi phần tay hàn nhận thấy nguồn đã fault. Các số ADC/VIN/nhiệt trong snapshot này không phải nguyên nhân trực tiếp của TIMING. Log ghi BBV6.20 / protect=1, chưa phải bản test18.

Đường LCD đồng bộ có thể chiếm foreground quá lâu khi đổi layout. Bản sửa phục vụ nút/encoder/USB giữa các lần truyền SPI ngắn, nhịp khoảng 5 ms trong lúc vẽ. Không gọi lại LCD hay ghi Flash bên trong callback; không chặn ngắt điều khiển. Watchdog chỉ được feed khi thấy TIM3 tiếp tục chạy. Parser USB giới hạn lượng byte mỗi lượt để dữ liệu liên tục không chiếm vòng chính. Sau khi vẽ xong, chờ chu kỳ tiếp theo thay vì lập tức vẽ bù.

Không tăng ngưỡng 250 ms hoặc xóa lỗi TIMING. LCD/SPI thật sự treo vẫn ngắt nguồn. Bảo vệ deadline heater và TIM3 giữ nguyên. Snapshot LCD dùng bộ nhớ tĩnh; xử lý nút chỉ snapshot các cờ cần dùng, giảm stack của callback lồng nhau.

Kiểm chứng mô phỏng chạy driver LCD C thật cùng application/control/DMA/SysTick: giả lập một lượt vẽ 789 ms, không lỗi TIMING; USB OFF được xử lý sau 2 ms và nút OFF sau 32 ms (gồm debounce). Khi cố tình ngừng foreground, vẫn chốt TIMING tại 251 ms. Các thời gian này là kết quả mô phỏng với SPI giả lập chậm, không phải phép đo board thực tế.

Thêm telemetry `ui_draw_ms`: thời gian lớn nhất của một lượt vẽ LCD, bao gồm xử lý xen giữa. Giá trị này có thể >250 ms mà không lỗi nếu foreground vẫn phục vụ trong lúc vẽ. `timing_src/timing_value` vẫn ghi tình trạng thiếu phục vụ thực sự.

Bản này giữ TEST18: SOLDER 18 V/6 A, hiện ADC thô, `solder.test18=1`, `solder.protect=0`. Nạp `build/Firmware_BB_V6_BBV6_22_TEST18.hex` hoặc `build/Firmware_BB_V6.hex` (cùng nội dung). Ngắt/kết nối lại app để đọc lại tên firmware BBV6.22-TEST18. Chưa nạp hoặc bật nguồn/heater từ máy tính trong lượt sửa này; app v6.19 dùng tiếp được.
