# OpenKey - Custom Build by kaitobui25

Đây là bản build cá nhân của OpenKey (Win32) với các tính năng tùy chỉnh riêng biệt để tối ưu hóa trải nghiệm gõ phím đa ngôn ngữ.

### 🚀 Các tính năng bổ sung trong bản build này:
- **Hỗ trợ IME Nhật Bản thông minh (Windows 10/11):**
    - **Auto-Switch:** Tự động chuyển OpenKey sang **Tiếng Anh (E)** khi **bật** chế độ gõ tiếng Nhật (Hiragana) — chỉ khi IME vừa chuyển sang trạng thái mở, không ép E trên mỗi phím.
    - **Auto-Restore:** Tự động khôi phục **Tiếng Việt (V)** khi tắt Hiragana / IME (sau khi đã auto-chuyển E).
    - **Tương thích ứng dụng hiện đại:** Dùng `GetGUIThreadInfo` để lấy đúng ô nhập focus trên **Microsoft Teams**, Outlook, Opera, VS Code, v.v.
    - **Phím tắt E/V luôn hoạt động:** Ctrl+Shift (hoặc phím tắt tùy chỉnh) và menu tray vẫn đổi E/V được khi IME đang mở; lựa chọn **V** thủ công được giữ cho đến khi IME tắt hẳn.
    - **Lợi ích:** Giảm xung đột gõ tiếng Nhật với Telex/VNI; sửa lỗi kẹt E trong Outlook/Teams khi IME báo bật nhầm (v2.0.3).
    - *Gõ tiếng Việt có dấu trong ô đang dùng IME Nhật:* vẫn nên `Win+Space` sang English hoặc tắt Hiragana trước khi gõ.
- **Phán đoán tiếng Anh thông minh (Proactive English Detection):**
    - Tôi đã chỉnh sửa lại thuật toán kiểm tra chính tả (hàm `checkSpelling` trong file `Engine.cpp`): Áp dụng kỹ thuật "Loose Match" để kiểm tra các tổ hợp nguyên âm ngay trong lúc đang gõ.
    - **Lợi ích:** Với các từ tiếng Anh chứa tổ hợp nguyên âm vô lý trong tiếng Việt (ví dụ `ea` trong chữ `search`), bộ gõ sẽ tự động phán đoán và **nhả ngay chữ tiếng Anh (sear)** trong thời gian thực, thay vì cố bỏ dấu sai (sẻa) rồi đợi phím Space mới sửa lại như bản gốc.
    - *Lưu ý:* Những từ tiếng Anh chứa tiền tố hợp lệ trong tiếng Việt (như `ie` trong `chief`) vẫn sẽ tuân theo luật gõ dấu tự do của bộ gõ (tạm biến thành `chiè`) và tự động khôi phục thành `chief` khi bấm phím Cách (Space).
- **Ưu tiên tiếng Anh khi gõ mã/số (Alphanumeric English Priority):**
    - Sửa lại bộ lọc đầu vào của engine để không còn coi số đầu tiên là ký tự ngắt từ (Word Break).
    - **Lợi ích:** Giải quyết triệt để lỗi khó chịu khi gõ các chuỗi mã hay serial như `8WC5123` (bị biến thành `8ƯC...`) hoặc `3D` (bị biến thành `3Đ`). Giờ đây, khi bạn gõ bất kỳ số nào xen lẫn chữ cái, nó sẽ tự động nhận diện đó là mã tiếng Anh/số và ngừng bỏ dấu hoàn toàn.
- **Giao diện icon cá nhân hóa:**
    - Chế độ **Tiếng Việt (V)**: Icon chữ V được đổi sang màu **Tím (Purple)**.
    - Chế độ **Tiếng Anh (E)**: Icon chữ E được đổi sang màu **Xanh lá cây (Green)**.
    - Giúp người dùng dễ dàng nhận diện phiên bản tùy chỉnh này trên khay hệ thống.

---

### 📚 Thông tin về dự án gốc (Original Project)
Dự án này được phát triển dựa trên mã nguồn mở của **OpenKey**. Bạn có thể tham khảo phiên bản gốc, tài liệu hướng dẫn chi tiết và ủng hộ tác giả tại:
- **GitHub:** [tuyenvm/OpenKey](https://github.com/tuyenvm/OpenKey)
- **Website:** [open-key.org](http://open-key.org)
