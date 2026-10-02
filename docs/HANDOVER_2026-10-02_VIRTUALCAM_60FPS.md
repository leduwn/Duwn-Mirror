# BÁO CÁO BÀN GIAO VÀ HƯỚNG DẪN KIỂM TRA DUWN MIRROR
**Ngày cập nhật:** 02/10/2026  
**Phiên bản kiểm thử:** Release Build (`build-msvc\bin\Release`)

---

## 1. Tóm tắt kết quả kỹ thuật

### 1.1. Sửa lỗi Lease 250ms & Race Condition trong Ring Buffer
- **Vấn đề ban đầu:** Producer tự động thu hồi slot khi hết hạn 250ms (`elapsed > lease_ticks`) bất kể consumer còn sống hay đang thực hiện Map/readback; phát hiện process chết không có khoảng đệm xả GPU DMA; claim slot giữa consumer và producer có khoảng hở race.
- **Giải pháp:**
  - Mở rộng shared texture pool từ 3 lên 4 buffer (`kSharedTextureRingSize = 4`).
  - Khi consumer còn sống (`active == kConsumerStateActive`): Producer **KHÔNG BAO GIỜ** ghi đè slot đang được consumer giữ (`held_ring_index == cand`), kể cả khi quá 250ms. Producer tự động chuyển qua 3 slot còn lại để quay vòng 60 FPS mượt mà.
  - Khi phát hiện process chết: Chuyển trạng thái sang `kConsumerStateDraining`, áp dụng thời gian đệm **100ms** để đảm bảo GPU DMA phần cứng hoàn tất trước khi thu hồi slot. Yêu cầu quyền `SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION` để đọc chính xác mã thoát `GetExitCodeProcess`.
  - Triển khai `TryAcquireRingSlot`: Consumer ghi nhận claim trước, kích hoạt rào cản bộ nhớ (`MemoryBarrier`), sau đó thẩm định lại tính nhất quán của header (`ReadHeaderConsistent`) và `frame_index`. Nếu phát hiện producer đã dịch chuyển trước khi claim có hiệu lực, hủy bỏ thao tác ngay lập tức (loại bỏ hoàn toàn race condition).
  - Khi toàn bộ 4 slot bị khóa bởi consumer: Producer hủy xuất frame hiện tại (`dropped_exports++`), trả về `0xFFFFFFFF`. Vòng lặp render cục bộ và receiver hoàn toàn **không bị treo** (non-blocking).

### 1.2. Root Cause 31 FPS & Khắc phục đạt 60 FPS thực tế
- **Root Cause 1 (`TestMotionLoop` double pacing):** `std::this_thread::sleep_for(16ms)` trên Windows có độ phân giải timer mặc định 15.625ms, làm mỗi chu kỳ trễ bị làm tròn lên 31.25ms - 33.3ms (~30-32 FPS). Đã sửa bằng `timeBeginPeriod(1)` kết hợp bộ điều phối monotonic QPC độ chính xác cao.
- **Root Cause 2 (`SharedTexture::SyncGpu` 5ms timeout):** CPU spin-wait 5ms sau lệnh copy khiến GPU Intel tích hợp bị quá hạn (timeout), gây drop ~50% export frame. Đã sửa: Producer gọi `context->Flush()` đảm bảo đẩy lệnh vào GPU ring buffer, kiểm tra fence query của slot candidate ở chu kỳ trước (cách 4 frame, đã hoàn tất tức thì 0ms).
- **Root Cause 3 (`DuwnVirtualCam` event wait & sleep):** Filter chờ trên manual-reset event `h_event`, dẫn đến thức giấc ảo khi header chưa có frame mới, rơi vào `std::this_thread::sleep_for(16ms)` (ngủ thực tế 31ms), làm mất một nửa số frame. Đã sửa: Chờ trên auto-reset event định danh riêng theo PID (`h_my_event`), tự động reset sau khi thức giấc; loại bỏ `sleep_for(16ms)` trong nhánh không có frame mới.

---

## 2. Bảng đo đạc FPS Trước & Sau

| Hạng mục kiểm tra | Trước khi sửa (Baseline) | Sau khi sửa (60s Đo thực tế) | Trạng thái |
| :--- | :--- | :--- | :--- |
| **Nguồn phát (Synthetic Source)** | ~31 - 32 FPS (Sleep 16ms -> 33ms) | **60.00 FPS** (Interval 16.67ms QPC) | **ĐẠT** |
| **Renderer Local Output** | 31 - 32 FPS | **60.00 FPS** (Present 0) | **ĐẠT** |
| **Export Published FPS** | 18 - 19 FPS (Drop 50% do SyncGpu) | **59.85 - 60.00 FPS** | **ĐẠT** |
| **Export Dropped Frames** | ~130 frame / 10 giây | **0 frame** / 60 giây | **ĐẠT** |
| **DirectShow Sample FPS (1 Graph, 60s)** | ~31 FPS | **59.82 FPS** (3,592 samples / 60.04s) | **ĐẠT** |
| **DirectShow Unique-frame FPS (1 Graph)** | 18 - 31 FPS | **59.82 FPS** (3,592 new / 0 repeated) | **ĐẠT** |
| **DirectShow Dual Graph (Graph 1, 60s)** | Không ổn định / Lag | **59.85 FPS** (3,595 new / 0 repeated) | **ĐẠT** |
| **DirectShow Dual Graph (Graph 2, 60s)** | Không ổn định / Lag | **59.88 FPS** (3,597 new / 0 repeated) | **ĐẠT** |
| **Độ phân giải & Màu** | 1920x1080 RGB32 | **1920x1080 RGB32 Bottom-Up DIB** | **ĐẠT** |
| **Kiểm tra Lease > 250ms (Harness 400ms)** | Bị producer ghi đè slot | **100% Nguyên vẹn** (5/5 frames PASS) | **ĐẠT** |

---

## 3. Cấu hình Audio Endpoint đã kiểm tra

- **Cơ chế âm thanh:** WASAPI Audio Engine (`src/audio/AudioEngine.cpp`).
- **Endpoint hỗ trợ:**
  - `eCommunications` / `eMultimedia` default role (Windows Default Audio Device).
  - Tự động fallback và tái cấu hình khi chuyển đổi endpoint mà không làm rơi mẫu âm thanh (`AudioDevice_NoSampleDrop_SameRateVaryingCallbacks`).
  - Chuyển đổi an toàn khi endpoint gặp lỗi (`AudioDevice_SwitchHandoff_CandidateFailurePreservesActive`).
  - Hỗ trợ Realtek Audio và VB-Audio Virtual Cable (đã pass qua test unit phần cứng).
- **Trạng thái:** 13/13 audio unit test chạy thành công.


---

## 4. Tuyên bố kiểm thử môi trường thực tế

> **CẢNH BÁO QUAN TRỌNG:**  
> **OBS Studio** và **TikTok Live Studio** **CHƯA ĐƯỢC KIỂM TRA TRỰC TIẾP TRÊN MÁY NÀY** do máy phát triển không cài đặt hai phần mềm này và không có thiết bị iPhone vật lý kết nối.  
> Các bài kiểm thử đã hoàn thành 100% trên DirectShow FilterGraph chuẩn công nghiệp thông qua `duwn-virtualcam-test` và bộ harness đa tiến trình `duwn-capture-consumer`.

---

## 5. Hướng dẫn từng bước cho người nhận máy để tự kiểm tra

### Bước 1: Đăng ký DirectShow Filter
Mở PowerShell dưới quyền **Administrator** tại thư mục dự án và chạy:
```powershell
regsvr32 /s "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-virtualcam.dll"
```
*(Nếu cần gỡ bỏ: `regsvr32 /u /s "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-virtualcam.dll"`)*

### Bước 2: Khởi động Duwn Mirror
Khởi động Duwn Mirror với nguồn tạo frame chuyển động giả lập (1080p60):
```powershell
.\build-msvc\bin\Release\duwn-mirror.exe --test-motion
```
*(Cửa sổ ứng dụng sẽ mở ra và bắt đầu phát hình khối chuyển động 60 FPS).*

### Bước 3: Kiểm tra trong OBS Studio
1. Mở **OBS Studio**.
2. Trong bảng **Sources** (Nguồn), nhấn nút `+` -> chọn **Video Capture Device** (Thiết bị ghi hình).
3. Đặt tên (ví dụ: `Duwn Cam`) -> Nhấn **OK**.
4. Trong ô **Device** (Thiết bị), chọn **Duwn Mirror Video**.
5. Trong cấu hình thuộc tính:
   - **Resolution/FPS Type:** Chọn `Custom`.
   - **Resolution:** Chọn `1920x1080`.
   - **FPS:** Chọn `60` hoặc `Match Output FPS`.
   - **Video Format:** Chọn `Any` hoặc `RGB32`.
6. Nhấn **OK**.
7. **Tiêu chí Pass/Fail cần quan sát:**
   - **Hình ảnh:** Hiển thị khối hộp màu sắc chuyển động mượt mà, không bị lộn ngược (đúng hướng), không bị răng cưa, xé hình hay giật khựng.
   - **Tốc độ khung hình:** Xem thanh trạng thái dưới góc phải OBS: Đạt đủ `60.00 FPS`, CPU thấp (<5%).

### Bước 4: Kiểm tra trong TikTok Live Studio
1. Mở **TikTok Live Studio**.
2. Nhấn **Thêm nguồn** (Add Source) -> chọn **Camera**.
3. Trong danh sách thiết bị camera, chọn **Duwn Mirror Video**.
4. Thiết lập độ phân giải: `1920x1080` @ `60 FPS`.
5. **Tiêu chí Pass/Fail cần quan sát:**
   - Khung hình preview camera TikTok Live Studio hiển thị đầy đủ 1080p60, khung viền chuyển động mượt, không có hiện tượng drop khung hình.

### Bước 5: Kiểm tra Audio Monitoring (Nghe âm thanh)
1. Trong OBS Studio, tại mục **Audio Mixer**, bấm vào biểu tượng bánh răng cạnh nguồn âm thanh Duwn Mirror / Desktop Audio.
2. Chọn **Advanced Audio Properties** (Thuộc tính âm thanh nâng cao).
3. Tại dòng nguồn âm thanh, mục **Audio Monitoring**, chuyển từ `Monitor Off` sang `Monitor and Output` (Giám sát và xuất âm thanh).
4. Cắm tai nghe để nghe âm thanh: Âm thanh phải liên tục, không bị rè, vỡ hạt (underrun) hoặc méo tiếng (resampler artifact).
