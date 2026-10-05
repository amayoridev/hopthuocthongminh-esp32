# 💊 Hộp Thuốc Thông Minh 7 Ngày (Smart Pill Box ESP32)

Dự án thiết kế và chế tạo hộp thuốc thông minh dành cho người cao tuổi và người mắc bệnh mạn tính. Hệ thống sử dụng vi điều khiển **ESP32**, cho phép người nhà cài đặt lịch uống thuốc dễ dàng qua giao diện **Web App nội bộ** và nhận thông báo giám sát từ xa qua **Zalo**.

Đặc biệt, dự án tối ưu hóa giao diện vật lý chỉ với **1 nút bấm duy nhất** và **cảm biến nắp đậy**, giúp người lớn tuổi dễ dàng thao tác mà không cần học cách sử dụng các thiết bị công nghệ phức tạp.

## ✨ Tính năng nổi bật

- 🌐 **Web App Nội Bộ (Captive Portal):** Cài đặt 21 cữ thuốc (Sáng/Trưa/Chiều trong 7 ngày) trực tiếp qua trình duyệt web trên điện thoại/máy tính mà không cần cài đặt thêm App.
- 📲 **Thông Báo Zalo (Giám Sát Từ Xa):** Tự động đẩy thông báo qua Zalo cho người nhà khi đến giờ uống thuốc, khi bệnh nhân đã uống (tắt chuông), hoặc khi bệnh nhân quên đóng nắp hộp bảo quản thuốc.
- 💡 **Nhắc nhở Trực Quan:** Báo động bằng còi Buzzer, màn hình LCD I2C và 7 đèn LED chỉ thị chính xác ngăn thuốc của ngày cần uống.
- ⚙️ **Giao Diện Vật Lý Tối Giản:** 
  - **Nhấn nhanh:** Tắt chuông báo / Xem cữ thuốc tiếp theo.
  - **Nhấn giữ 1s:** Chuyển đổi màn hình xem trạng thái (WiFi, Version, IP...).
  - **Nhấn giữ 5s:** Factory Reset (Xóa trắng WiFi và lịch trình, khôi phục cài đặt gốc).
- 🧠 **Cơ Chế Auto-Return:** Tự động tính toán và hiển thị cữ thuốc sắp tới trên màn hình sau 10 giây không có tương tác vật lý.
- 🔋 **An Toàn Dữ Liệu & Điện Năng:** Lưu trữ lịch trình trong bộ nhớ NVS (EEPROM) của ESP32. Tích hợp module RTC DS3231 giữ thời gian thực xác, tự động hoạt động bình thường ngay khi có điện lại mà không cần Internet.

## 🛠 Phần cứng sử dụng (BOM)

- 1 x Vi điều khiển **ESP32** (Tối ưu hóa, thay thế cho cụm Arduino + ESP8266 cũ để tăng độ bền và giảm giá thành).
- 1 x Module thời gian thực **RTC DS3231** (Giao tiếp I2C).
- 1 x Màn hình **LCD 16x2** + Module I2C.
- 7 x Đèn LED đơn 5mm (Hiển thị 7 ngăn thuốc).
- 1 x Còi chíp (Active Buzzer).
- 1 x Nút nhấn (Push button).
- 1 x Công tắc hành trình (Microswitch) - Cảm biến đóng/mở nắp hộp.
- Testboard, nguồn hạ áp, điện trở, vỏ hộp tự thiết kế.

## 🔌 Sơ đồ nối dây (Pinout ESP32)

| Thiết bị | Chân ESP32 | Ghi chú |
| :--- | :--- | :--- |
| **Buzzer** | `GPIO 27` | Nối với chân dương của còi |
| **Nút bấm đa nhiệm** | `GPIO 13` | Nối GND, cấu hình `INPUT_PULLUP` trong code |
| **LED Trạng thái** | `GPIO 2` | LED báo trạng thái hệ thống |
| **7 LED (Ngăn thuốc)**| `4, 5, 18, 19, 32, 33, 25` | Tương ứng từ Thứ 2 đến Chủ Nhật |
| **LCD & RTC (I2C)** | `GPIO 21 (SDA)`, `GPIO 22 (SCL)` | Đấu song song 2 thiết bị vào bus I2C |
| **Cảm biến nắp hộp** | `(Cập nhật sau)` | Công tắc hành trình ngắt chuông/cảnh báo |

> *Lưu ý: LCD cấp nguồn 5V riêng, ESP32 và RTC cấp nguồn 3.3V. Chỉ nối chung đường GND (Mass). Không cấp 5V vào chân 3.3V của ESP32.*

## 💻 Yêu cầu phần mềm & Thư viện

- **IDE:** Arduino IDE (Cài đặt Board Manager cho ESP32).
- **Các thư viện bắt buộc:**
  - `WiFi.h`, `WebServer.h`, `HTTPClient.h`, `WiFiClientSecure.h`, `DNSServer.h`, `EEPROM.h` (Tích hợp sẵn trong core ESP32).
  - `ArduinoJson` (Benoit Blanchon)
  - `RTClib` (Adafruit)
  - `LiquidCrystal_I2C` (Frank de Brabander)

## 🚀 Hướng dẫn cài đặt & Khởi động

1. **Nạp Code:** Tải mã nguồn `smart_pill_box.ino` và nạp vào ESP32 thông qua Arduino IDE.
2. **Cài đặt lần đầu (Captive Portal):**
   - Khi mạch cấp nguồn lần đầu (chưa có WiFi), ESP32 sẽ tự phát ra một mạng WiFi có tên **"Cài Đặt Hộp Thuốc"**.
   - Dùng điện thoại kết nối vào WiFi này. Giao diện Web sẽ tự động bật lên (hoặc truy cập `192.168.4.1`).
   - Nhập tên WiFi gia đình, Mật khẩu, Zalo Bot Token và Zalo Chat ID. Nhấn Lưu.
3. **Sử dụng Web App:** 
   - Sau khi kết nối WiFi thành công, ESP32 sẽ gửi tin nhắn IP nội bộ (VD: `http://192.168.1.15`) về Zalo.
   - Nhấp vào link để mở giao diện quản lý 21 cữ thuốc, bật/tắt báo thức và test các tính năng phần cứng trực tiếp trên Web.

## 📝 Nhật ký thay đổi (Changelog)

- **V9.x (Bản hiện tại):** 
  - Thay thế cụm mạch *Arduino UNO + ESP8266* bằng vi điều khiển *ESP32* nguyên khối để giảm độ trễ, tối ưu hóa kích thước và tiết kiệm chi phí.
  - Loại bỏ mạch chuyển kênh TCA9548A, khắc phục triệt để lỗi xung đột tín hiệu I2C gây rác màn hình.
  - Tối giản điều khiển: Loại bỏ mắt thu hồng ngoại (IR Remote), tích hợp toàn bộ vào 1 nút nhấn đa chức năng.
  - Chuyển việc cấu hình lịch từ Zalo Bot (thiếu ổn định) sang Web Server tĩnh nhúng trực tiếp trong ESP32. Giữ lại Zalo làm kênh cảnh báo 1 chiều.

## 🔮 Hướng phát triển tương lai (To-Do)
- [ ] Thiết kế lại vỏ hộp và khay thuốc bằng công nghệ in 3D.
- [ ] Tích hợp mạch sạc pin Lithium 18650 và IC TP4056 để duy trì hoạt động cảnh báo ngay cả khi cúp điện lưới.
- [ ] Bổ sung module âm thanh DFPlayer Mini để phát lời nhắc nhở bằng giọng nói thay cho tiếng còi Buzzer truyền thống.

## 📜 Giấy phép
Dự án được thực hiện cho mục đích Nghiên cứu Khoa học Kỹ thuật (NCKH) cấp trường.
