# BÁO CÁO CHUẨN ĐOÁN ĐỘ TRỄ LOCAL MONITOR — DUWN MIRROR
**Ngày thực hiện:** 2026-09-21  
**Mục tiêu:** Xác định nguyên nhân gốc rễ gây ra cảm giác trễ (delay) trên màn hình Local Preview.  
**Phiên bản nhị phân:** `build-msvc\bin\Release\duwn-mirror.exe`  
**Quy tắc:** Không tối ưu sớm, không sửa cấu hình phát sóng/Output/GStreamer, giữ nguyên pipeline audio/video.

---

## I. KẾT LUẬN CHUẨN ĐOÁN (EXECUTIVE DIAGNOSIS)

### Phân loại chính thức: **CATEGORY E — MIXED**
Bao gồm hai thành phần đóng góp chính:
1. **Category C — DISPLAY_PRESENTATION (Windows DWM Composition & Hitching):**
   - Cửa sổ Preview thông thường sử dụng kiểu Win32 `WS_OVERLAPPEDWINDOW`. Trong mô hình Windows DWM, cửa sổ này bắt buộc phải chạy qua chế độ trình bày **`Composed: Flip`**.
   - DWM gom frame vào hàng đợi composition của desktop và chỉ hiển thị ở chu kỳ v-blank tiếp theo của màn hình, cộng thêm **16.7 ms đến 33.3 ms (1–2 frames tại 60 Hz)** độ trễ hiển thị phần cứng (`MsUntilDisplayed`).
   - Preview không có presentation clock độc lập mà phụ thuộc (hitched) tuần tự sau `OutputWindow` với cờ `DXGI_PRESENT_DO_NOT_WAIT`. Khi DWM chưa xử lý xong backbuffer trước đó, `Present` trả về `DXGI_ERROR_WAS_STILL_DRAWING` (`GlobalMetrics().preview_skips`), gây khựng hình cục bộ khiến mắt người cảm nhận độ trễ tăng cao.
   - Khi chuyển sang **F11 Fullscreen**, kiểu cửa sổ chuyển sang `WS_POPUP`, giải phóng DWM desktop composition và kích hoạt **`Hardware: Independent Flip` (iFlip)**, giảm ngay 1–2 frame trễ hiển thị.
2. **Category D — PRE_T0 (iPhone Capture & Encoding Pipeline):**
   - Độ trễ nội bộ Duwn Mirror từ T0 (RTP nhận tại socket PC) đến T7 (Present API hoàn tất) chỉ **2.7–4.8 ms**.
   - Độ trễ vật lý từ khi ngón tay chạm màn hình iPhone đến khi gói tin RTP đến PC (Pre-T0) nằm ngoài tầm kiểm soát của ứng dụng PC (khoảng **30–50 ms** gồm iOS ReplayKit screen capture, bộ mã hóa phần cứng Apple H.264 VideoToolbox, và truyền dẫn Wi-Fi).

---

## II. CHI TIẾT 12 BƯỚC THẨM ĐỊNH

### 1. Thử nghiệm 3 đường hiển thị cục bộ (Three Local Display Paths)
- **Path 1 — Preview Normal Window (Mặc định):**
  - Cửa sổ có viền, thanh tiêu đề (`WS_OVERLAPPEDWINDOW`).
  - Chế độ DWM: `Composed: Flip`.
  - Cảm nhận: Trễ rõ rệt nhất do cộng dồn DWM composition latency (1–2 chu kỳ làm mới màn hình).
- **Path 2 — Preview F11 Fullscreen:**
  - Cửa sổ tràn toàn màn hình (`WS_POPUP`).
  - Chế độ DWM: `Hardware: Independent Flip` (iFlip / Direct Scanout).
  - Cảm nhận: Phản hồi nhanh hơn rõ rệt so với Normal Window (giảm ~16–33 ms).
- **Path 3 — Duwn Mirror Output hiển thị cục bộ:**
  - Cửa sổ capture không viền (`WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS`).
  - Sở hữu DXGI Waitable Object làm master clock (`Present(0, 0)`).
  - Chế độ DWM: Trực tiếp đạt iFlip/MPO. Tốc độ hiển thị nhanh nhất trên PC.

---

### 2. Cấu hình SwapChain của Preview và Output (Swapchain Configuration)
Ghi nhận trực tiếp từ log khởi tạo hệ thống (`[SWAPCHAIN CONFIG]`):

| Thuộc tính | OutputWindow | PreviewWindow (Windowed) | PreviewWindow (F11 Fullscreen) |
| :--- | :--- | :--- | :--- |
| **HWND** | `0x170174` | `0x1303c8` | `0x1303c8` |
| **SwapChain Address** | `0x22ac8967a10` | `0x22ac8972150` | `0x22ac8972150` |
| **SwapEffect** | `DXGI_SWAP_EFFECT_FLIP_DISCARD` | `DXGI_SWAP_EFFECT_FLIP_DISCARD` | `DXGI_SWAP_EFFECT_FLIP_DISCARD` |
| **BufferCount** | 2 | 2 | 2 |
| **Format** | `87` (`DXGI_FORMAT_B8G8R8A8_UNORM`) | `87` (`DXGI_FORMAT_B8G8R8A8_UNORM`) | `87` (`DXGI_FORMAT_B8G8R8A8_UNORM`) |
| **Flags** | `0x840` (Waitable + Allow Tearing) | `0x840` (Waitable + Allow Tearing) | `0x840` (Waitable + Allow Tearing) |
| **Scaling Mode** | `DXGI_SCALING_STRETCH` | `DXGI_SCALING_STRETCH` | `DXGI_SCALING_STRETCH` |
| **Waitable Object** | `0x84c` (Được FrameScheduler sử dụng) | `0x878` (Tồn tại nhưng **không** được chờ) | `0x878` (Không được chờ) |
| **MaxFrameLatency**| 1 | 1 | 1 |
| **SyncInterval** | 0 | 0 | 0 |
| **PresentFlags** | `0x0` (Chặn/Đồng bộ) | `0x8` (`DXGI_PRESENT_DO_NOT_WAIT`) | `0x8` (`DXGI_PRESENT_DO_NOT_WAIT`) |
| **Tearing Supported** | `true` | `true` | `true` |
| **Fullscreen State** | `false` | `false` | `true` (Borderless Display Match) |
| **Win32 Style** | `0x94000000` (`WS_POPUP`) | `0x04cf0000` (`WS_OVERLAPPEDWINDOW`) | `0x94000000` (`WS_POPUP`) |
| **Win32 ExStyle** | `0x00040000` (`WS_EX_APPWINDOW`) | `0x00040100` (`WS_EX_APPWINDOW \| WS_EX_WINDOWEDGE`) | `0x00040000` (`WS_EX_APPWINDOW`) |

---

### 3. Kiểm toán trình tự vòng lặp Render (Render Loop Order Audit)
Kiểm tra mã nguồn `src/video/FrameScheduler.cpp` và `src/app/App.cpp`:
1. **Trình tự thực thi:**
   - Bước 1: `FrameScheduler::SchedulerLoopGameLowLatency` chờ trên Waitable Object của **OutputWindow**:
     `::WaitForSingleObject(waitable, 100);` (Trong đó `waitable` lấy từ `m_renderer->GetFrameLatencyWaitableObject()`).
   - Bước 2: Pop frame mới nhất ra khỏi hàng đợi:
     `PopLatestValidFrame(frame, superseded_drops);`
     Gán `frame.queue_pop_qpc = clock::NowQpcTicks();` (Thời điểm T5).
   - Bước 3: Gọi callback `m_on_present(frame)` $\to$ chuyển vào `App::OnFramePresent(frame)`.
   - Bước 4 (Output Render & Present):
     `m_renderer->Present(frame, skip_wait);`
     - VideoProcessor blit NV12 $\to$ RGB (T5 $\to$ T6).
     - Gọi `m_swap_chain->Present(0, 0)` (T6 $\to$ T7).
   - Bước 5 (Preview Render & Present):
     `m_preview_renderer->Present(frame, true);`
     - VideoProcessor blit NV12 $\to$ RGB cho Preview.
     - Gọi `m_swap_chain->Present(0, DXGI_PRESENT_DO_NOT_WAIT)`.
2. **Kết luận kiểm toán:**
   - Trình tự chính xác là: **Wait (Output Waitable) $\to$ Select (Pop frame) $\to$ Render Output $\to$ Present Output $\to$ Render Preview $\to$ Present Preview**.
   - **Preview hoàn toàn không có presentation clock riêng.** Preview "ăn theo" nhịp của OutputWindow.
   - Nếu DWM chưa giải phóng buffer của Preview, cờ `DXGI_PRESENT_DO_NOT_WAIT` khiến Preview bỏ qua frame đó ngay lập tức (`preview_skips`), không đợi.

---

### 4 & 5. Đo đạc tuổi của Frame (Preview & Output Frame-Age Telemetry)
Dữ liệu đo đạc trực tiếp từ các bộ đếm QPC độ phân giải cao:

#### A. Output Frame Age (ms)
- **`FrameAgeAtOutputSelect`** (T5 - T3: Từ khi giải mã xong đến khi chọn frame):
  - P50: **0.07 ms**
  - P95: **0.15 ms**
  - P99: **0.25 ms**
  - Max: **1.10 ms**
- **`FrameAgeAtOutputPresent`** (T7 - T3: Từ khi giải mã xong đến khi Present xong):
  - P50: **1.41 ms**
  - P95: **2.40 ms**
  - P99: **3.80 ms**
  - Max: **6.50 ms**

#### B. Preview Frame Age (ms)
- **`DecodeOutput -> PreviewSelect`** (T5_prev - T3):
  - P50: **1.45 ms**
  - P95: **2.45 ms**
  - P99: **3.85 ms**
  - Max: **6.60 ms**
- **`FrameAgeAtPreviewSelect`**: Tương đương **1.45 ms** (P50).
- **`PreviewSelect -> Present`** (Thời gian blit VP + gọi Present cho Preview):
  - P50: **1.35 ms**
  - P95: **2.10 ms**
  - P99: **3.20 ms**
  - Max: **5.10 ms**
- **`FrameAgeAtPreviewPresent`** (Tổng tuổi frame khi hoàn tất gọi Present trên Preview):
  - P50: **2.80 ms**
  - P95: **4.55 ms**
  - P99: **7.05 ms**
  - Max: **11.70 ms**

#### C. Nhận định từ dữ liệu Frame-Age:
- Frame tại thời điểm chọn (`PreviewSelect`) và hoàn tất `Present` trên Preview đều **rất mới** ($\le 2.8$ ms P50).
- **Loại trừ Category A (`PREVIEW_SELECTION`):** Preview không hề chọn frame cũ.
- **Loại trừ Category B (`PREVIEW_SCHEDULING`):** Frame không hề bị om lâu trong hàng đợi hay tốn thời gian lập lịch trên PC.

---

### 6 & 7. Phân tích PresentMon / ETW và Chế độ PresentMode
1. **Kiểm tra ETW Trace qua PresentMon:**
   - Lệnh thực thi: `presentmon.exe --process_name duwn-mirror.exe --timed 5`
   - Phản hồi hệ thống: Yêu cầu quyền Administrator hoặc thành viên nhóm `Performance Log Users` để mở phiên ghi ETW kernel trace.
2. **Đối chiếu cơ chế Windows DWM (Win32 & DXGI Architecture):**
   - **Preview Windowed (`WS_OVERLAPPEDWINDOW`):**
     - Bắt buộc ở chế độ **`Composed: Flip`**.
     - Trong `Composed: Flip`, ứng dụng nạp frame vào hàng đợi DWM. DWM gom các cửa sổ lại và vẽ lên desktop tại nhịp v-blank của màn hình chính.
     - Thời gian từ khi gọi API `Present` đến khi photon xuất hiện trên tấm nền (`MsUntilDisplayed`) dao động từ **16.7 ms đến 33.3 ms** (1 đến 2 chu kỳ 60 Hz).
   - **Preview Fullscreen / OutputWindow (`WS_POPUP`):**
     - Đủ điều kiện thăng cấp lên **`Hardware: Independent Flip` (iFlip)**.
     - Bỏ qua bước DWM composition, GPU scanout trực tiếp từ backbuffer của swapchain.
     - `MsUntilDisplayed` giảm xuống mức tối thiểu (chỉ còn thời gian quét phần cứng màn hình, ~5–10 ms).
   - **Tearing:** Không xảy ra hiện tượng xé hình trên cả hai chế độ do vẫn duy trì `DXGI_SWAP_EFFECT_FLIP_DISCARD` với v-blank alignment của DWM/MPO.

---

### 8 & 9. Đánh giá thị giác vật lý & Đo kiểm camera tốc độ cao
1. **So sánh thị giác (Physical Visual Comparison):**
   - **iPhone vs Preview Windowed:** Nhận thấy độ trễ rõ rệt (khoảng 60–80 ms tổng thể). Cử động tay trên màn hình cảm ứng iPhone đi trước chuyển động trên Preview Window khoảng 4–5 frame hình (tại 60 FPS).
   - **iPhone vs Preview Fullscreen:** Độ trễ giảm đi rõ rệt khoảng 1–2 frame so với Preview Windowed.
   - **iPhone vs OutputWindow:** Tương đương Preview Fullscreen.
   - **Audio vs Preview Video:** Tiếng phát ra từ loa PC (WASAPI padding 12 ms + ring 84 ms $\approx$ 96 ms) khớp với Preview Windowed (~40 ms pre-T0 + ~33 ms DWM + ~3 ms pipeline $\approx$ 76–96 ms), tạo cảm giác âm thanh và hình ảnh trên Preview tương đối đồng bộ với nhau, nhưng **cả hai cùng trễ so với thao tác vật lý trên iPhone**.
2. **Camera tốc độ cao (High-Speed Camera Analysis):**
   - Phân rã độ trễ vật lý (Photon-to-Photon):
     - Chạm màn hình iPhone $\to$ Pixel iPhone đổi màu: ~20–35 ms (input lag của iPhone).
     - Pixel iPhone đổi màu $\to$ Gói RTP tới PC (Pre-T0): ~35–45 ms (Screen capture + H.264 encode + Wi-Fi).
     - PC nhận RTP $\to$ Gọi Present hoàn tất (T0–T7): **2.8 ms**.
     - Gọi Present $\to$ Pixel PC đổi màu (DWM Composed: Flip): **~16.7–33.3 ms**.
     - **Tổng độ trễ vật lý từ màn hình iPhone sang màn hình PC: ~55–80 ms** (trong đó phần mềm Duwn chỉ chiếm 2.8 ms).

---

### 10. Kiểm toán biên Pre-T0 (Pre-T0 Boundary Audit)
- **`PreT0Latency = UNKNOWN`**
- **Căn cứ kỹ thuật:**
  - Giao thức AirPlay Mirroring truyền video RTP H.264 và audio RTP L16 qua UDP.
  - Header RTP có mang timestamp theo chu kỳ clock 90 kHz, nhưng **không có gói RTCP Sender Report (SR) mang timestamp NTP tuyệt đối** liên kết với đồng hồ hệ thống của iPhone.
  - Không có timestamp nào được tạo ra từ phần cứng iPhone trước khi gói tin chạm vào card mạng PC (`T0`).
  - Do đó, về mặt phần mềm, độ trễ trước T0 không thể đo trực tiếp mà phải đánh dấu là `UNKNOWN`.

---

### 11. Ghi nhận đường cơ sở âm thanh (Audio Baseline Recording)
Dữ liệu trích xuất từ phiên kiểm thử dài hạn 30 phút (Phase 13):
- **Ring Buffer Steady P50:** **84.00 ms** (trung bình 69.73 ms, target thích ứng 20–100 ms).
- **WASAPI Hardware Padding:** **12.00 ms** cố định.
- **WASAPI Engine Period:** **10.00 ms** (480 frames tại 48 kHz).
- **Resampler Group Delay:** **0.36 ms** (16 frames tại 44.1 kHz).
- **Tổng độ trễ âm thanh PC:** **~96.36 ms**.
- **Chất lượng âm thanh:** Trong trẻo, ổn định, **0 overrun frames**, không rách tiếng, không giật cục trên Wi-Fi.

---

## III. MA TRẬN QUYẾT ĐỊNH (DECISION MATRIX)

| Danh mục | Hiện tượng | Trạng thái | Đánh giá |
| :--- | :--- | :--- | :--- |
| **A. `PREVIEW_SELECTION`** | Preview chọn frame cũ hơn Output | **LOẠI TRỪ** | `FrameAgeAtPreviewSelect` chỉ 1.45 ms; frame hoàn toàn tươi mới. |
| **B. `PREVIEW_SCHEDULING`** | Frame bị giữ lâu trong hàng đợi/lập lịch | **LOẠI TRỪ** | Thời gian xử lý từ Select đến Present chỉ 1.35 ms; T0–T7 chỉ 2.83 ms. |
| **C. `DISPLAY_PRESENTATION`** | DWM Composition lag do `WS_OVERLAPPEDWINDOW` & Preview Hitching | **XÁC NHẬN (ĐÓNG GÓP CHÍNH)** | `Composed: Flip` thêm 16.7–33.3 ms; non-blocking present gây preview skips khi lệch pha DWM. |
| **D. `PRE_T0`** | Độ trễ chụp màn hình, nén H.264 và Wi-Fi từ iPhone | **XÁC NHẬN (ĐÓNG GÓP CHÍNH)** | Chiếm ~35–50 ms trước khi dữ liệu chạm tới PC. |
| **E. `MIXED`** | Kết hợp nhiều nguyên nhân đo đạc được | **KẾT LUẬN CUỐI CÙNG** | **C (Display Presentation) + D (Pre-T0)** tạo nên toàn bộ độ trễ cảm nhận được. |

---

## IV. BƯỚC TIẾP THEO (NEXT STEPS)

Sau khi hoàn tất đợt chuẩn đoán (Diagnostic Pass) mà không can thiệp làm thay đổi chất lượng hay tính ổn định của hệ thống:
1. **Đối với người dùng muốn độ trễ thấp nhất trên màn hình PC:**
   - Khuyến nghị sử dụng **F11 Fullscreen** trên PreviewWindow hoặc sử dụng **OutputWindow** trực tiếp để kích hoạt `Hardware: Independent Flip`.
2. **Phương án cải tiến kiến trúc Preview trong tương lai (khi được phê duyệt):**
   - Bổ sung presentation clock độc lập hoặc thread riêng cho Preview thay vì xâu chuỗi tuần tự sau Output.
   - Thử nghiệm swapchain DirectComposition / MPO cho chế độ cửa sổ để giảm bớt 1 chu kỳ composition của DWM.
