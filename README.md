# OpenKey - Custom Build by kaitobui25

Đây là bản build cá nhân của OpenKey (Win32) với các tính năng tùy chỉnh riêng biệt để tối ưu hóa trải nghiệm gõ phím đa ngôn ngữ.

### 🚀 Các tính năng bổ sung trong bản build này:
- **Hỗ trợ IME Nhật Bản thông minh (Windows 10/11):** 
    - **Auto-Switch:** Tự động chuyển OpenKey sang chế độ gõ **Tiếng Anh (E)** ngay khi bạn bật chế độ gõ tiếng Nhật (Hiragana) bằng phím CapsLock hoặc thao tác chuột.
    - **Tương thích ứng dụng hiện đại:** Cải tiến thuật toán sử dụng GetGUIThreadInfo và Keyboard Layout (0x0411) để hoạt động chính xác trên các ứng dụng đa tiến trình như **Microsoft Teams**, Discord, VS Code.
    - **Auto-Restore:** Tự động khôi phục lại chế độ gõ **Tiếng Việt (V)** khi bạn tắt chế độ gõ tiếng Nhật hoặc chuyển về bàn phím ngôn ngữ khác.
    - **Lợi ích:** Loại bỏ hoàn toàn lỗi gạch chân, mất ký tự hoặc xung đột phím khi gõ tiếng Nhật mà quên tắt bộ gõ tiếng Việt, ngay cả khi dùng CapsLock để chuyển chế độ bên trong IME Nhật.
    - **Auto-Switch:** Tự động chuyển OpenKey sang chế độ gõ **Tiếng Anh (E)** ngay khi bạn bật chế độ gõ tiếng Nhật (Hiragana) bằng phím `CapsLock` hoặc thao tác chuột.
    - **Auto-Restore:** Tự động khôi phục lại chế độ gõ **Tiếng Việt (V)** khi bạn tắt chế độ gõ tiếng Nhật.
    - **Lợi ích:** Loại bỏ hoàn toàn lỗi gạch chân, mất ký tự hoặc xung đột phím khi gõ tiếng Nhật mà quên tắt bộ gõ tiếng Việt.
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
