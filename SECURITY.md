# Chính sách bảo mật (Security Policy)

[Tiếng Việt](SECURITY.md)

Dự án Duwn Mirror coi trọng tính bảo mật và sự an toàn của người dùng. Mặc dù ứng dụng hoạt động 100% cục bộ (không lưu trữ dữ liệu đám mây), việc đảm bảo an toàn cho các cổng mạng và dịch vụ AirPlay là ưu tiên hàng đầu.

---

## 🛡️ Các phiên bản được hỗ trợ

Chúng tôi cung cấp bản vá bảo mật cho phiên bản mới nhất:

| Phiên bản | Hỗ trợ bảo mật |
| :--- | :--- |
| `1.1.x` | :white_check_mark: Được hỗ trợ tích cực |
| `< 1.1.0` | :x: Ngừng hỗ trợ |

---

## 🔒 Báo cáo lỗ hổng bảo mật

Nếu bạn phát hiện một lỗ hổng bảo mật trong Duwn Mirror, xin vui lòng **KHÔNG tạo public issue** trên GitHub.

Thay vào đó, hãy thông báo bảo mật bằng một trong các phương thức sau:
1. **GitHub Private Vulnerability Reporting:** Truy cập tab **Security** của repository -> bấm **Report a vulnerability**.
2. **Email trực tiếp:** Gửi email đến người duy trì dự án tại địa chỉ liên hệ trên hồ sơ GitHub (`leduwn`).

### Nội dung báo cáo cần cung cấp:
- Loại lỗ hổng (ví dụ: tràn bộ đệm buffer overflow trong giải mã RTP, rò rỉ tài nguyên, phân quyền tường lửa, v.v.).
- Chi tiết các bước tái hiện hoặc mã khai thác minh chứng (Proof of Concept - PoC).
- Mức độ ảnh hưởng và các kịch bản tấn công tiềm năng.
- Phiên bản Duwn Mirror và môi trường thử nghiệm.

### Quy trình xử lý của chúng tôi:
- **Xác nhận tiếp nhận:** Trong vòng 48 giờ kể từ khi nhận được báo cáo.
- **Phân tích & kiểm chứng:** Đội ngũ phát triển sẽ xác minh phạm vi ảnh hưởng và xây dựng bản vá khắc phục.
- **Công bố có trách nhiệm (Responsible Disclosure):** Bản vá sẽ được phát hành trong phiên bản mới nhất trước khi thông tin chi tiết về lỗ hổng được công bố công khai.
