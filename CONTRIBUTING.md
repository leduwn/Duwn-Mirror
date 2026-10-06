# Hướng dẫn đóng góp (Contributing Guide)

[Tiếng Việt](CONTRIBUTING.md)

Cảm ơn bạn đã quan tâm đóng góp cho dự án Duwn Mirror! Chúng tôi hoan nghênh mọi đóng góp từ cộng đồng: từ sửa lỗi, tối ưu hiệu năng, cải thiện tài liệu đến đề xuất tính năng mới.

---

## 📋 Mục lục

1. [Quy tắc ứng xử](#quy-tắc-ứng-xử)
2. [Báo cáo sự cố & Đề xuất tính năng](#báo-cáo-sự-cố--đề-xuất-tính-năng)
3. [Thiết lập môi trường phát triển](#thiết-lập-môi-trường-phát-triển)
4. [Quy chuẩn mã nguồn & Kiến trúc](#quy-chuẩn-mã-nguồn--kiến-trúc)
5. [Quy trình kiểm thử (Unit Testing)](#quy-trình-kiểm-thử-unit-testing)
6. [Quy chuẩn Commit & Tạo Pull Request](#quy-chuẩn-commit--tạo-pull-request)

---

## Quy tắc ứng xử

- Tôn trọng và hỗ trợ các thành viên khác trong cộng đồng.
- Thảo luận mang tính xây dựng, tập trung vào kỹ thuật và trải nghiệm người dùng.

---

## Báo cáo sự cố & Đề xuất tính năng

- **Báo lỗi (Bug Reports):** Trước khi tạo issue mới, vui lòng tìm kiếm các issue hiện có xem lỗi đã được báo cáo chưa. Khi tạo bug report, hãy sử dụng mẫu có sẵn và cung cấp:
  - Phiên bản Duwn Mirror đang chạy.
  - Phiên bản Windows và mẫu GPU (NVIDIA/AMD/Intel).
  - Mẫu iPhone/iPad và phiên bản iOS/iPadOS.
  - Mô tả chi tiết các bước tái hiện lỗi kèm hình ảnh/log nếu có.
- **Đề xuất tính năng (Feature Requests):** Mô tả rõ bài toán bạn cần giải quyết, vì sao tính năng này hữu ích cho cộng đồng và phương án đề xuất (nếu có).

---

## Thiết lập môi trường phát triển

### Yêu cầu công cụ
- **Hệ điều hành:** Windows 10 (22H2+) hoặc Windows 11 64-bit.
- **IDE / Trình biên dịch:** Visual Studio 2022 (MSVC v143) với thành phần *Desktop development with C++*.
- **Hệ thống build:** CMake 3.25 trở lên.
- **Bộ cài đặt (Tùy chọn):** WiX Toolset v5 (`dotnet tool install --global wix`) và .NET 8 SDK.

### Các bước lấy mã nguồn và biên dịch

```powershell
# 1. Clone repository
git clone https://github.com/leduwn/Duwn-Mirror.git
cd Duwn-Mirror

# 2. Cấu hình CMake với preset debug hoặc release
cmake --preset debug

# 3. Biên dịch dự án
cmake --build build/debug --config Debug
```

Thư mục thực thi đầu ra: `build/debug/bin/`.

---

## Quy chuẩn mã nguồn & Kiến trúc

Dự án viết bằng **C++20** hiện đại, tuân thủ cấu trúc mô-đun phân tách độc lập:

- `src/common/`: Cấu trúc dữ liệu dùng chung, Logger, Version metadata, CPU detection.
- `src/network/`: Xử lý giao thức mạng, RTP receiver, mDNS / Bonjour, packet assembly.
- `src/airplay/`: Máy chủ AirPlay RTSP/FairPlay handling, session management.
- `src/audio/`: WASAPI audio engine, adaptive ring buffer, stereo 48 kHz output.
- `src/video/`: Bộ giải mã Media Foundation (MFVideoDecoder), FrameScheduler, D3D11 rendering.
- `src/capture/`: Quản lý shared GPU texture và bộ lọc DirectShow Virtual Camera (`duwn-virtualcam.dll`).
- `src/ui/`: Giao diện Win32 / GDI / DirectWrite, Telemetry HUD, bảng điều khiển, localization.
- `src/app/`: Điều phối vòng đời ứng dụng chính, quản lý cửa sổ (MainWindow, OutputWindow).

### Nguyên tắc code:
- **Tối giản & chính xác:** Không thêm trừu tượng không cần thiết; tối ưu độ trễ xử lý đa luồng.
- **An toàn bộ nhớ:** Sử dụng Smart Pointers (`std::unique_ptr`, `std::shared_ptr`), COM Pointers (`Microsoft::WRL::ComPtr`).
- **Xử lý tài nguyên đa luồng:** Quản lý race conditions bằng `std::atomic` hoặc mutex scoped guard. Tránh lock kéo dài trong render loop.

---

## Quy trình kiểm thử (Unit Testing)

Tất cả các thay đổi logic cốt lõi phải đi kèm kiểm thử và đảm bảo toàn bộ bộ test suite hiện có đều pass:

```powershell
# Chạy bộ unit tests
ctest --test-dir build/debug --output-on-failure -C Debug
```

Bộ unit tests nằm tại `tests/unit/` bao gồm kiểm thử giao thức RTP, frame scheduler, UI state, localization, và vòng đời session.

---

## Quy chuẩn Commit & Tạo Pull Request

### Quy ước Commit (Conventional Commits)
Sử dụng tiền tố rõ ràng theo chuẩn:
- `feat:` Tính năng mới cho người dùng.
- `fix:` Sửa lỗi.
- `perf:` Cải thiện hiệu năng xử lý / độ trễ.
- `docs:` Thay đổi tài liệu hướng dẫn.
- `refactor:` Tái cấu trúc mã nguồn không đổi hành vi.
- `test:` Bổ sung hoặc sửa đổi kiểm thử.
- `chore:` Thay đổi cấu hình build, CI, metadata.

### Tạo Pull Request
1. Tạo nhánh riêng từ `main`: `git checkout -b feat/ten-tinh-nang`.
2. Kiểm tra format và chạy toàn bộ unit tests (`ctest`).
3. Đẩy nhánh lên fork và mở Pull Request vào nhánh `main`.
4. Điền đầy đủ thông tin theo mẫu PR template để người đánh giá (reviewer) dễ dàng kiểm tra.
