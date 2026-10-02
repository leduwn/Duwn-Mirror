# BÁO CÁO BÀN GIAO VÀ HƯỚNG DẪN KIỂM TRA DUWN MIRROR
**Ngày cập nhật:** 02/10/2026  
**Phiên bản kiểm thử:** Release Build (`build-msvc\bin\Release`)

---

## 1. Tóm tắt kết quả kỹ thuật

### 1.1. Vòng đời Slot & Loại trừ tương hỗ Dekker Race-Free
- **Vấn đề ban đầu:** Producer tự động thu hồi slot khi hết hạn 250ms (`elapsed > lease_ticks`) bất kể consumer còn sống hay đang thực hiện Map/readback; claim slot giữa consumer và producer có khoảng hở race nếu producer vừa chọn slot trống còn consumer vừa claim.
- **Giải pháp:**
  - Mở rộng shared texture pool lên 4 buffer (`kSharedTextureRingSize = 4`).
  - Khi consumer còn sống (`active == kConsumerStateActive`): Producer **KHÔNG BAO GIỜ** ghi đè slot đang được consumer giữ (`held_ring_index == cand`), kể cả khi quá 250ms.
  - **Giao thức Dekker hai chiều qua `producer_reserved_slot`:**
    - Producer trước khi ghi công bố ý định bằng atomic `InterlockedExchange(&header->producer_reserved_slot, cand)` kèm `MemoryBarrier`. Nếu phát hiện consumer đã giữ slot, producer hủy bảo lưu và chuyển sang slot kế tiếp.
    - Consumer khi gọi `TryAcquireRingSlot` atomic claim `held_ring_index = cand` kèm `MemoryBarrier`, sau đó kiểm tra `producer_reserved_slot == cand`. Nếu producer đang chuẩn bị ghi, consumer **chủ động nhường bước ngay lập tức** (backoff về `0xFFFFFFFF`, trả về `false`).
    - Loại bỏ hoàn toàn khả năng producer và consumer đọc/ghi đồng thời trên cùng một slot.
  - Khi toàn bộ 4 slot bị khóa bởi consumer: Producer hủy xuất frame hiện tại (`dropped_exports++`), trả về `0xFFFFFFFF`. Vòng lặp render cục bộ hoàn toàn **không bị treo** (non-blocking).

### 1.2. Thu hồi tài nguyên sau Crash Consumer (Resource Retirement & Generation Bump)
- **Vấn đề ban đầu:** Khoảng chờ 100ms chỉ là thời gian đệm, không phải bằng chứng chứng minh GPU DMA đã kết thúc. Nếu GPU bị nghẽn phần cứng, tái sử dụng texture cũ sau 100ms có thể gây xung đột truy cập DMA.
- **Giải pháp:**
  - Loại bỏ hoàn toàn sự phụ thuộc vào timeout 100ms.
  - Khi phát hiện consumer process chết (`GetExitCodeProcess != STILL_ACTIVE` hoặc `OpenProcess` thất bại), producer thực hiện **cơ chế cách ly / thay thế tài nguyên (Resource Retirement)**:
    - Giải phóng COM reference của texture cũ trên slot đó (`m_textures[cand].Reset()`).
    - Gọi `SharedTexture::RecreateSlot` cấp phát một `ID3D11Texture2D` hoàn toàn mới trong VRAM, trích xuất DXGI shared handle mới.
    - Tăng `resource_generation` và cập nhật tức thì vào `CaptureMemoryHeader`.
    - Bộ nhớ của texture cũ được hệ điều hành WDDM thu hồi tự nhiên sau khi phần cứng GPU xả hết lệnh đang chờ. Producer và các consumer mới chuyển hẳn sang texture mới.
    - Giới hạn tài nguyên nghiêm ngặt (vẫn đúng 4 slot ring buffer), không rò rỉ bộ nhớ, bảo đảm an toàn lifetime 100% bằng cơ chế phần cứng và driver.

### 1.3. Bảo đảm Hoàn tất GPU Copy trước khi Publish Frame (SyncGpu Completion)
- **Vấn đề ban đầu:** Nếu chỉ gọi `context->Flush()` rồi publish frame ngay, trên một số cấu hình GPU consumer có thể đọc texture khi lệnh `CopyResource` chưa hoàn tất trên GPU (nguy cơ xé hình).
- **Giải pháp:**
  - `SharedTexture::SyncGpu` thực hiện `context->Flush()` và kiểm tra trực tiếp query fence của **frame hiện tại** (`m_queries[ring_index]`) bằng `GetData`.
  - Đặt timeout an toàn **12ms** (thừa đủ cho GPU DMA blit 1080p chỉ tốn 0.5 - 3ms trên Intel iGPU, đồng thời nằm gọn trong chu kỳ 16.67ms của 60 FPS).
  - Bảo đảm consumer chỉ nhận tín hiệu frame ready khi toàn bộ các vùng pixel trên texture 1080p đã hoàn tất ghi trên GPU.
  - Kiểm tra đồng thời bằng `EnsureSlotReady` ở đầu chu kỳ để xác nhận slot tái sử dụng từ vòng trước (4 frame trước) đã hoàn tất.

---

## 2. Bảng đo đạc FPS Trước & Sau (Kiểm chứng sau bản sửa hoàn thiện)

| Hạng mục kiểm tra | Trước khi sửa (Baseline) | Sau khi sửa (60s Đo thực tế) | Trạng thái |
| :--- | :--- | :--- | :--- |
| **Nguồn phát (Synthetic Source)** | ~31 - 32 FPS (Sleep 16ms -> 33ms) | **60.00 FPS** (Interval 16.67ms QPC) | **ĐẠT** |
| **Renderer Local Output** | 31 - 32 FPS | **60.00 FPS** (Present 0) | **ĐẠT** |
| **Export Published FPS** | 18 - 19 FPS (Drop 50% do SyncGpu) | **59.75 - 60.00 FPS** | **ĐẠT** |
| **Export Dropped Frames** | ~130 frame / 10 giây | **0 frame** / 60 giây | **ĐẠT** |
| **DirectShow Sample FPS (1 Graph, 60s)** | ~31 FPS | **59.79 FPS** (3,623 samples / 60.60s) | **ĐẠT** |
| **DirectShow Unique-frame FPS (1 Graph)** | 18 - 31 FPS | **59.79 FPS** (3,623 new / 0 repeated) | **ĐẠT** |
| **DirectShow Dual Graph (Graph 1, 60s)** | Không ổn định / Lag | **59.75 FPS** (3,621 new / 0 repeated) | **ĐẠT** |
| **DirectShow Dual Graph (Graph 2, 60s)** | Không ổn định / Lag | **59.72 FPS** (3,618 new / 0 repeated) | **ĐẠT** |
| **Độ phân giải & Màu** | 1920x1080 RGB32 | **1920x1080 RGB32 Bottom-Up DIB** | **ĐẠT** |
| **Toàn vẹn khung hình (Multi-Region)** | Nguy cơ xé hình khi copy dở | **100% Nguyên vẹn** (5/5 probes match) | **ĐẠT** |
| **Kiểm tra Lease > 250ms (Harness 400ms)** | Bị producer ghi đè slot | **100% Nguyên vẹn** (5/5 frames PASS) | **ĐẠT** |

---

## 3. Cấu hình Audio Endpoint & Tuyến thu âm (Audio Capture)

- **Cơ chế âm thanh:** WASAPI Audio Engine (`src/audio/AudioEngine.cpp`).
- **Endpoint hỗ trợ:**
  - `eCommunications` / `eMultimedia` default role (Windows Default Audio Device).
  - Tự động fallback và tái cấu hình khi chuyển đổi endpoint mà không làm rơi mẫu âm thanh (`AudioDevice_NoSampleDrop_SameRateVaryingCallbacks`).
  - Chuyển đổi an toàn khi endpoint gặp lỗi (`AudioDevice_SwitchHandoff_CandidateFailurePreservesActive`).
  - Hỗ trợ Realtek Audio và VB-Audio Virtual Cable (13/13 audio unit tests PASS).
- **Lưu ý quan trọng về thu âm trên OBS / phần mềm thứ ba:**
  - **Thu âm (Audio Capture):** Duwn Mirror phát âm thanh trực tiếp ra một WASAPI Audio Endpoint cụ thể trên Windows (ví dụ: Speakers, Headphones, hoặc Virtual Cable). Trong OBS Studio, chỉ cần tạo nguồn **Audio Output Capture** (hoặc dùng **Desktop Audio**) và chọn **đúng endpoint mà Duwn Mirror đang phát**. OBS sẽ thu và đẩy âm thanh trực tiếp vào stream/bản ghi.
  - **Audio Monitoring (Giám sát âm thanh):** Trong *Advanced Audio Properties* của OBS, tùy chọn `Monitor and Output` hay `Monitor Only` **CHỈ LÀ TÙY CHỌN ĐỂ NGƯỜI VẬN HÀNH NGHE KIỂM TRA** qua tai nghe riêng; **KHÔNG PHẢI** bước bắt buộc để thu âm hay phát sóng. Không bật Audio Monitoring thì OBS vẫn thu và phát âm thanh bình thường.


---

## 4. Tuyên bố kiểm thử môi trường thực tế

> **CẢNH BÁO MINH BẠCH VỀ PHẠM VI KIỂM THỬ:**  
> - **OBS Studio:** **NOT TESTED** (chưa cài đặt trên máy phát triển; cần kiểm tra trên máy đích).  
> - **TikTok Live Studio:** **NOT TESTED** (chưa cài đặt trên máy phát triển; cần kiểm tra trên máy đích).  
> - **Thiết bị iPhone vật lý (Lightning/USB-C & AirPlay):** **NOT TESTED** (chưa cắm trực tiếp trên máy phát triển).  
>
> **Các hạng mục đã kiểm thử và đạt 100% tại máy phát triển:**  
> - DirectShow FilterGraph chuẩn Windows COM qua binary `duwn-virtualcam-test.exe` (1 Graph 60s đạt 59.79 FPS, Dual Graph 60s đồng thời đạt 59.75 / 59.72 FPS, 0 repeated frame, 0 dropped export).  
> - Mô phỏng đa tiến trình độc lập `duwn-capture-consumer.exe` kiểm chứng chống race condition, lease > 250ms, và thu hồi tài nguyên sau process crash.  
> - 391/391 automated unit tests trong `duwn-unit-tests.exe` (bao gồm GPU query completion, Dekker mutual exclusion, WASAPI audio switching).

---

## 5. Hướng dẫn từng bước cho người nhận máy để tự kiểm tra

### Bước 1: Đăng ký DirectShow Filter với quyền Administrator
Mở Command Prompt hoặc PowerShell dưới quyền **Administrator** (Run as Administrator) và chạy:
```powershell
regsvr32 /s "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-virtualcam.dll"
```
*(Nếu cần gỡ bỏ filter: `regsvr32 /u /s "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-virtualcam.dll"`)*

**Giải thích kỹ thuật về việc sử dụng `regsvr32` thay vì `duwn-mirror.exe --register-vcam`:**
- `regsvr32` là công cụ chuẩn của hệ điều hành Windows để đăng ký COM In-Process Server (DLL). Lệnh này gọi trực tiếp hàm `DllRegisterServer` xuất ra từ `duwn-virtualcam.dll`, ghi thẳng các CLSID và danh mục DirectShow (`CLSID_VideoInputDeviceCategory`) vào Registry (`HKEY_CLASSES_ROOT`), đồng thời trả về mã lỗi hệ thống trực tiếp nếu thiếu quyền.
- Trong khi đó, `duwn-mirror.exe --register-vcam` phải khởi tạo toàn bộ runtime ứng dụng trước khi gọi hàm đăng ký nội bộ. Nếu ứng dụng chạy không đúng mức quyền Administrator hoặc gặp xung đột môi trường khởi động, lệnh có thể thất bại ngầm mà không có thông báo mã lỗi chuẩn của Windows. Vì vậy, `regsvr32` là phương thức chuẩn xác, trực tiếp và đáng tin cậy nhất.

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

### Bước 5: Cấu hình Audio Capture & Kiểm tra Audio
1. **Thiết lập thu âm trong OBS Studio (Audio Capture):**
   - Trong bảng **Sources**, nhấn `+` -> chọn **Audio Output Capture**.
   - Chọn đúng **Device (Endpoint)** mà Duwn Mirror đang phát âm thanh ra (ví dụ: Headphones, Realtek Speakers, hoặc Virtual Cable).
   - Âm thanh từ iPhone / Duwn Mirror sẽ được OBS thu trực tiếp và hòa âm vào stream/record.
2. **Audio Monitoring (Tùy chọn nghe kiểm tra):**
   - Nếu người vận hành muốn nghe âm thanh trực tiếp trên tai nghe của mình: Trong OBS mục **Audio Mixer**, bấm biểu tượng bánh răng -> chọn **Advanced Audio Properties**.
   - Chuyển mục **Audio Monitoring** sang `Monitor and Output`.
   - Lưu ý: Đây chỉ là tùy chọn để nghe kiểm tra; OBS vẫn thu và xuất âm thanh bình thường kể cả khi để `Monitor Off`.
3. **Tiêu chí âm thanh:** Âm thanh phát ra liên tục, không bị rè, vỡ hạt (underrun) hoặc méo tiếng do resampler.
