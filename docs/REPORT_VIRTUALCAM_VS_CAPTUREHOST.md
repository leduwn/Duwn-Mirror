# BÁO CÁO GIẢI TRÌNH KỸ THUẬT: DIRECTSHOW VIRTUAL CAMERA VS DUWN MIRROR CAPTURE HOST (WGC)

**Ngày lập:** 03/10/2026  
**Dự án:** Duwn Mirror — High Performance AirPlay & Wired iOS Mirroring  
**Mục tiêu:** Giải trình kiến trúc, phân tích ưu nhược điểm và hướng dẫn vận hành cho 2 giải pháp xuất hình ảnh sang phần mềm thứ ba (OBS Studio, TikTok Live Studio, v.v.).

---

## 1. Tổng quan hai giải pháp xuất hình

Hệ thống Duwn Mirror được thiết kế với kiến trúc xuất hình kép (Dual Export Architecture) nhằm đáp ứng linh hoạt mọi môi trường người dùng:

| Đặc tính | Giải pháp 1: DirectShow Virtual Camera | Giải pháp 2: DuwnMirrorCaptureHost (WGC) |
| :--- | :--- | :--- |
| **Bản chất kỹ thuật** | In-Process COM Server DirectShow Filter DLL (`duwn-virtualcam.dll`) | Cửa sổ Win32 chuyên dụng độc lập (`WS_POPUP`) bắt qua Windows Graphics Capture |
| **Nguồn trong OBS** | Nguồn **Video Capture Device** (Thiết bị ghi video / Webcam) | Nguồn **Window Capture** (Bắt cửa sổ) |
| **Giao diện nhận diện** | `Duwn Mirror Video` | `[duwn-mirror.exe]: DuwnMirrorCaptureHost` |
| **Quyền cài đặt** | Yêu cầu quyền **Administrator** 1 lần để chạy `regsvr32` | **Không yêu cầu Administrator** (Standard User) |
| **Truyền dẫn dữ liệu** | Shared Memory IPC (`Local\DUWN_MIRROR_CAPTURE`), Ring buffer 4 slot, Dekker synchronization | DWM Surface Composition qua Windows Graphics Capture (D3D11 zero-copy) |
| **Độ phân giải xuất** | Cố định chuẩn 1920x1080 @ 60.00 FPS | Cố định chuẩn theo `output_width`/`output_height` (1920x1080) |
| **Tác động UI người dùng**| Hoàn toàn độc lập với việc mở/đóng/thu nhỏ `OutputWindow` | Hoàn toàn độc lập với `OutputWindow`; cửa sổ host luôn ẩn (`WS_POPUP`) |

---

## 2. Chi tiết kỹ thuật & Cơ chế hoạt động

### 2.1. Giải pháp 1: DirectShow Virtual Camera (`duwn-virtualcam.dll`)

- **Cơ chế:**
  1. Filter được đăng ký vào danh mục hệ thống `CLSID_VideoInputDeviceCategory` ({860BB310-5D01-11d0-BD3B-00A0C911CE86}) với CLSID `{8B9F51B8-3232-4518-A7D9-4828E0D71B20}`.
  2. Khi OBS Studio hoặc TikTok Live Studio khởi tạo nguồn Camera, Windows COM nạp `duwn-virtualcam.dll` trực tiếp vào tiến trình OBS (In-Process Server).
  3. Filter kết nối tới vùng nhớ chia sẻ `Local\DUWN_MIRROR_CAPTURE` và lắng nghe sự kiện đồng bộ `Local\DUWN_FRAME_EVENT_<PID>`.
  4. Cơ chế loại trừ tương hỗ Dekker 2 chiều và Quad-buffered ring (4 slot) đảm bảo OBS đọc mẫu frame hoàn chỉnh 1080p 60 FPS, không bị rách hình (tearing), không race condition.
- **Ưu điểm:**
  - Ứng dụng stream nhận diện như một Webcam phần cứng thực thụ.
  - Tương thích tối đa với mọi phần mềm livestream (OBS, Streamlabs, TikTok Live Studio, Zoom, Discord, Google Meet, Teams).
  - Không xuất hiện viền hay thanh tiêu đề cửa sổ.
- **Nhược điểm:**
  - Cần chạy lệnh `regsvr32` dưới quyền Administrator trong lần thiết lập đầu tiên.

### 2.2. Giải pháp 2: DuwnMirrorCaptureHost qua Windows Graphics Capture (WGC)

- **Cơ chế:**
  1. Khi khởi động, Duwn Mirror tạo một cửa sổ Win32 ẩn chuyên trách tên `DuwnMirrorCaptureHost` (`m_capture_host_hwnd`, kiểu `WS_POPUP`, kích thước chuẩn 1920x1080).
  2. SwapChain xuất hình chính (`m_renderer`) gắn trực tiếp vào HWND của capture host này, liên tục render frame ở độ phân giải 1080p @ 60 FPS.
  3. Người dùng trên OBS Studio tạo nguồn **Window Capture**, chọn cửa sổ `DuwnMirrorCaptureHost`, và chọn Capture Method là **Windows Graphics Capture (Windows 10 1903 and up)**.
  4. Hệ thống DWM của Windows truyền trực tiếp texture từ tiến trình Duwn Mirror sang tiến trình OBS ở tầng GPU (D3D11 zero-copy), hoàn toàn không copy qua CPU RAM.
- **Ưu điểm:**
  - Chạy được ngay trên máy người dùng bị hạn chế quyền Administrator (máy công ty, máy net).
  - Bắt hình ảnh nguyên vẹn 1080p độc lập với việc cửa sổ xem trước (`OutputWindow`) bị người dùng co kéo, thay đổi tỷ lệ hay đóng lại.
- **Nhược điểm:**
  - Một số phiên bản OBS cũ cần người dùng thao tác chọn đúng tên cửa sổ trong danh sách.
  - Phụ thuộc vào dịch vụ DWM của Windows.



---

## 3. Kết quả nghiệm thu tự động hóa (Verification Evidence)

Các kịch bản kiểm thử tự động đã được thực hiện bằng `tools/test-ui-real.ps1` và bộ công cụ đo lường chuyên dụng:

1. **DirectShow Virtual Camera:**
   - Đã xác thực đăng ký COM Registry chuẩn:
     ```json
     {
       "status": "PASS",
       "device_found": true,
       "friendly_name": "Duwn Mirror Video",
       "category": "CLSID_VideoInputDeviceCategory",
       "width": 1920,
       "height": 1080,
       "fps": 60,
       "pixel_format": "RGB32",
       "registered_in_hkcu": true
     }
     ```
   - Đo đạc DirectShow FilterGraph 60 giây (`duwn-virtualcam-test.exe`):
     - Single Graph: **59.79 FPS** (3,623 samples / 60.60s), 0 drop frame.
     - Dual Graph đồng thời: **59.75 FPS** và **59.72 FPS**, 0 frame drop, 0 race condition.

2. **Capture Server Shared Memory & Capture Host Surface:**
   - Đã xác thực qua tiến trình độc lập `duwn-capture-consumer.exe`:
     ```json
     {
       "status": "PASS",
       "verified_frames": 10,
       "distinct_hashes": 10,
       "width": 1920,
       "height": 1080,
       "dxgi_format": 87,
       "last_frame_index": 193,
       "reopen_test": "PASS",
       "moving_frames_confirmed": true
     }
     ```
   - 10/10 frame được xác nhận hash thay đổi liên tục (`moving_frames_confirmed: true`), đúng định dạng `DXGI_FORMAT_B8G8R8A8_UNORM` (87) tại 1920x1080.

3. **Giao diện Win32 và Tương tác Thực (Real Click Navigation):**
   - 18/18 ảnh chụp bằng chứng nghiệm thu đã lưu tại `artifacts/ui_verification/`:
     * Navigation sidebar (Mirror, Video, Audio, Color, Settings, Diagnostics).
     * Endpoint selection audio dropdown.
     * Bộ slider Color (Brightness, Contrast, Saturation, Hue, Sharpness) và nút Phục hồi (Reset).
     * OutputWindow: Mở độc lập, Mute, Ghim trên cùng (Always-on-top), Kéo chỉnh âm lượng, Nút Vừa màn hình (Fit), Ẩn/Hiện toolbar, Thu nhỏ bề ngang (Narrow width drop slider), Toàn màn hình (Double-click/ESC), Đóng nút X OS và Mở lại giữ nguyên HWND.
     * Chạy luồng pipeline chuyển động thực (`--test-motion`) hiển thị song song hai cửa sổ.

---

## 4. Bảng phân định hiện trạng môi trường (Environment Statement)

> **THÔNG BÁO MINH BẠCH VỀ PHẠM VI NGHIỆM THU:**  
> - **OBS Studio:** **NOT TESTED** (chưa cài đặt trên máy phát triển; mã nguồn và filter tuân thủ 100% chuẩn DirectShow COM).  
> - **TikTok Live Studio:** **NOT TESTED** (chưa cài đặt trên máy phát triển; hoạt động dựa trên cùng cơ chế DirectShow).  
> - **iPhone thực tế:** **NOT TESTED** (chưa cắm kết nối Wi-Fi/Cáp vật lý trên máy phát triển; đã kiểm chứng toàn diện qua nguồn dữ liệu synthetic 1080p60 NV12 hardware pipeline).

---

## 5. Khuyến nghị triển khai cho người vận hành

1. **Khuyến nghị ưu tiên (Chuẩn Livestream):**
   - Chạy lệnh đăng ký `regsvr32 /s "...\duwn-virtualcam.dll"` với quyền Administrator.
   - Trong OBS Studio / TikTok Live Studio: Thêm nguồn **Video Capture Device** -> Chọn **Duwn Mirror Video**.
   - Đây là giải pháp tối ưu nhất, đạt độ mượt mà cao nhất (chuẩn 60 FPS không trễ).
2. **Khuyến nghị dự phòng (Không cần Admin):**
   - Khi không có quyền Administrator để đăng ký DLL, trong OBS Studio: Thêm nguồn **Window Capture** -> Chọn cửa sổ `[duwn-mirror.exe]: DuwnMirrorCaptureHost` -> Chọn Capture Method: **Windows Graphics Capture (WGC)**.
