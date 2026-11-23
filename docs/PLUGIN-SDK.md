# LumaCast — Hướng dẫn viết plug-in shader (SDK, API phiên bản 1)

Plug-in shader thêm **Effect** (xử lý một hình) hoặc **Transition** (chuyển giữa hai hình) bằng **GLSL**, không cần biên dịch C++.
Effect/Transition dựng sẵn của LumaCast dùng **cùng định dạng** này, nên có thể xem chúng như ví dụ (`src/render/BuiltinEffects.cpp`)
và các plug-in mẫu trong thư mục `plugins/`.

> Plug-in native (DLL, C ABI) chưa có trong phiên bản này; xem `PLAN.md`.

## 1. Cài đặt và nạp
Mỗi plug-in là **một thư mục** chứa `plugin.json` và một file shader. LumaCast quét hai nơi (thư mục sau ghi đè thư mục trước nếu trùng `id`):
1. `<thư mục cài LumaCast>\plugins\` — plug-in đi kèm (4 plug-in mẫu).
2. `%AppData%\LumaCast\plugins\` — plug-in của bạn (nút **Mở thư mục plug-in** ở dock *Plug-in*).

```
plugins/
  my-glow/
    plugin.json
    glow.frag
```
- Sửa file `.frag` hoặc `plugin.json` → LumaCast **tự nạp lại** sau khoảng 0,3 giây (hoặc bấm *Quét lại*); không cần khởi động lại.
- Dock **Plug-in** liệt kê mọi Effect/Transition, tick để bật/tắt, và hiện **lý do lỗi** nếu plug-in không nạp được.
- Effect nằm ở tab **Effect** của Inspector (gắn vào Layer); Transition nằm trong danh sách hiệu ứng ở thanh chuyển.

## 2. `plugin.json`
```json
{
  "id": "com.example.vignette",
  "name": "Vignette (tối góc)",
  "version": "1.0.0",
  "apiVersion": 1,
  "type": "effect",
  "author": "Tên bạn",
  "description": "Làm tối dần về phía bốn góc.",
  "entry": "vignette.frag",
  "params": [
    { "name": "strength", "label": "Độ tối", "type": "float", "min": 0, "max": 1, "default": 0.6 },
    { "name": "tint", "label": "Màu viền", "type": "color", "default": [0, 0, 0, 1] }
  ]
}
```
| Trường | Bắt buộc | Quy tắc |
|---|---|---|
| `id` | có | duy nhất; chỉ chữ thường, số, `.`, `_`, `-`; **không** bắt đầu bằng `builtin.` (dành riêng) |
| `apiVersion` | có | phải bằng **1** |
| `type` | có | `"effect"` hoặc `"transition"` |
| `entry` | có | tên file shader **nằm trong chính thư mục plug-in** (không được chứa đường dẫn như `../x`) |
| `name`, `version`, `author`, `description` | không | hiển thị trong Plugin Manager |
| `params` | không | danh sách tham số, xem mục 3 |

## 3. Tham số
Mỗi tham số trở thành một **uniform GLSL cùng tên** và một ô chỉnh trong giao diện (sinh tự động).

| `type` | Giao diện | Uniform GLSL | Trường |
|---|---|---|---|
| `float` | ô số | `uniform float` | `min`, `max`, `default` |
| `int` | ô số nguyên | `uniform int` | `min`, `max`, `default` |
| `bool` | hộp tick | `uniform bool` | `default` |
| `choice` | danh sách chọn | `uniform int` (chỉ số 0…) | `choices` (≥ 2 chuỗi), `default` (chỉ số hoặc chuỗi) |
| `color` | nút chọn màu (có alpha) | `uniform vec4` (RGBA 0–1) | `default`: `[r,g,b,a]` (0–1) hoặc `"#AARRGGBB"` |
| `point` | hai ô x, y | `uniform vec2` | `default`: `[x, y]` |

`name` phải là định danh GLSL hợp lệ, không trùng tên dành riêng (`uInput`, `uFrom`, `uTo`, `uProgress`, `uSize`, `uTime`, `vUv`, `fragColor`,
`main`, `effect`, `transition`, `sampleInput`, `sampleFrom`, `sampleTo`) và không bắt đầu bằng `gl_`. `label` là nhãn hiển thị (mặc định = `name`).
Giá trị luôn được kẹp về khoảng hợp lệ trước khi gửi cho shader.

## 4. Viết shader
Bạn **chỉ viết một hàm**. LumaCast tự thêm `#version 330 core`, các uniform, hàm tiện ích, tham số và `main()`.

**Effect** — định nghĩa `vec4 effect(vec2 uv)`:
```glsl
vec4 effect(vec2 uv) {
    vec4 c = sampleInput(uv);                     // màu của hình đầu vào tại uv
    float d = length((uv - 0.5) * vec2(uSize.x / uSize.y, 1.0));
    return vec4(mix(c.rgb, tint.rgb, smoothstep(0.7, 1.2, d) * strength), c.a);
}
```
Có sẵn: `uniform vec2 uSize` (kích thước hình đầu vào, điểm ảnh), `uniform float uTime` (giây từ lúc engine chạy), `vec4 sampleInput(vec2 uv)`.

**Transition** — định nghĩa `vec4 transition(vec2 uv)`:
```glsl
vec4 transition(vec2 uv) {
    return mix(sampleFrom(uv), sampleTo(uv), step(uv.x, uProgress));   // quét từ trái sang phải
}
```
Có sẵn: `sampleFrom(uv)` (hình cũ), `sampleTo(uv)` (hình mới), `uniform float uProgress` (0 → 1, **đã áp dụng easing** khi chạy tự động; T-bar kéo tay đi thẳng),
`uSize`, `uTime`.

Quy ước tọa độ: `uv` trong [0,1], gốc `(0,0)` ở **góc dưới-trái**, `y` hướng lên. Màu là RGBA không nhân alpha (straight alpha).
Mọi lỗi biên dịch được báo kèm **số dòng trong file của bạn** (dòng 1 là dòng đầu của file shader).

## 5. Hành vi khi có lỗi (an toàn)
| Tình huống | Kết quả |
|---|---|
| `plugin.json` sai, thiếu file, `apiVersion` khác 1, `id` trùng/bị cấm | không nạp, liệt kê kèm lý do trong dock Plug-in |
| Shader biên dịch lỗi | Effect đó **bị bỏ qua** (hình đi qua không đổi), lỗi hiện ở tab Effect và trong log; sửa file là tự biên dịch lại |
| Transition lỗi hoặc thiếu | rơi về **Fade** |
| Project dùng plug-in không có/đã tắt | mở project bình thường; Effect hiện "⚠ thiếu plug-in", **dữ liệu tham số được giữ nguyên** |

## 6. Giới hạn đã biết (đọc trước khi phát hành plug-in)
- **Không có bảo vệ chống shader chạy vô hạn hoặc quá nặng** (vòng lặp vô hạn có thể treo GPU/driver). Chỉ chạy plug-in bạn tin cậy. Watchdog chỉ *phát hiện và báo* khi luồng render treo.
- Effect chạy ở **độ phân giải của nguồn** (ví dụ video 1080p → chạy 1080p), không phải độ phân giải hiển thị. Nguồn 1×1 (màu trơn) không có chi tiết không gian để xử lý.
- Mỗi plug-in là **một lượt vẽ** (một shader). Hiệu ứng nhiều lượt (blur tách trục, bloom nhiều tầng) chưa hỗ trợ; dùng nhiều mẫu trong một lượt như `builtin.blur`.
- Chỉ có `uInput`/`uFrom`/`uTo`; chưa có texture phụ (ảnh LUT, mặt nạ) và chưa có đầu vào âm thanh.
- Hiệu năng nhiều Effect ở 4K **chưa được đo** (xem `TEST-PLAN.md`, B5).
