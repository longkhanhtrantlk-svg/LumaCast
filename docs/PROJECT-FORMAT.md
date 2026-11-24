# Định dạng file project LumaCast (phiên bản 1)

Mã nguồn tham chiếu: `src/core/Project.cpp` (`saveProject`, `loadProject`). Kiểm thử: `tests/test_render.cpp`, mục "Lưu / mở project".

## 1. Tổng quan
- JSON UTF-8, phần mở rộng khuyến nghị `.lumacast.json`.
- Ghi **nguyên tử** (`QSaveFile`): mất điện giữa chừng không làm hỏng file cũ.
- Đường dẫn media lưu **tương đối** so với thư mục chứa file project; khi mở được đổi lại thành tuyệt đối.
- Tự lưu: `<tên project>.autosave` cạnh file; project chưa đặt tên: `%AppData%/LumaCast/untitled.autosave.json`.
  Lưu thành công sẽ xoá bản tự lưu cũ.
- Trường lạ bị bỏ qua; trường thiếu dùng giá trị mặc định (xem bảng).

**Không đổi ở M5c:** vẫn `version` 1, không thêm trường. Cài đặt điều khiển ngoài (cổng, LAN, token), Khoá vận hành, tuỳ chọn tự khởi động và "project gần nhất" là **cài đặt theo máy/phiên** (`QSettings`), cố ý **không** lưu vào file project để mở project trên máy khác không mang theo token hay mở cổng mạng.

## 2. Cấu trúc

```json
{
  "app": "LumaCast",
  "version": 1,
  "fps": 60,
  "sources":  [ ... ],
  "scenes":   [ ... ],
  "channels": [ ... ],
  "outputs":  [ ... ]
}
```

| Trường gốc | Kiểu | Bắt buộc | Mặc định | Ghi chú |
|---|---|---|---|---|
| `app` | chuỗi | có | — | phải đúng `"LumaCast"` |
| `version` | số nguyên | có | — | hiện phải bằng `1` |
| `fps` | số | không | 60 | nhịp master, 1–240 |

### 2.1 `sources[]`
| Trường | Kiểu | Ghi chú |
|---|---|---|
| `id` | chuỗi | **duy nhất, không rỗng**; Layer tham chiếu theo `id` |
| `type` | chuỗi | `video` · `image` · `color` · `testpattern` |
| `path` | chuỗi | `video`/`image`: đường dẫn tương đối |
| `color` | chuỗi `#AARRGGBB` | `color` |
| `label` | chuỗi | `color`, `testpattern` |
| `width`, `height` | số nguyên | `testpattern`, mặc định 1920×1080 |

### 2.2 `scenes[]`
| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `name` | chuỗi | `"Scene"` | |
| `width`, `height` | số nguyên ≥ 1 | 1920, 1080 | độ phân giải thiết kế |
| `background` | `#AARRGGBB` | `#ff000000` | |
| `duration` | số ≥ 0,1 | 10 | giây; thời lượng đồng hồ Scene dùng cho keyframe (giá trị không hợp lệ → 10) |
| `loop` | bool | true | lặp lại khi hết thời lượng |
| `restartOnAir` | bool | true | chạy lại đồng hồ từ 0 mỗi khi Scene lên Program / bắt đầu chuyển vào |
| `layers[]` | mảng | — | **thứ tự vẽ từ dưới lên**: phần tử cuối nằm trên cùng |

`layers[]`:
| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `source` | chuỗi | — | phải trùng một `sources[].id` (nếu không: lỗi nạp) |
| `x`, `y`, `w`, `h` | số | 0, 0, 1, 1 | chuẩn hoá theo Scene, gốc ở góc trên-trái |
| `opacity` | số 0–1 | 1 | |
| `fit` | chuỗi | `fit` | `stretch` · `fit` · `fill` |
| `visible` | bool | true | |
| `effects[]` | mảng | (rỗng) | chuỗi Effect, chạy **từ phần tử đầu đến cuối** trước khi Layer được vẽ vào Scene (trường tuỳ chọn, thêm ở M4a, vẫn `version` 1) |

`effects[]`:
| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `id` | chuỗi | — | `builtin.blur`, `builtin.glow`, `builtin.colorgrade`, `builtin.rgbsplit`, `builtin.pixelate`, `builtin.noise` hoặc `id` của plug-in; thiếu plug-in **không** làm lỗi nạp (Effect bị bỏ qua, dữ liệu được giữ) |
| `enabled` | bool | true | |
| `params` | đối tượng | (mặc định của Effect) | tên tham số → giá trị: số (`float`/`int`/`choice` theo chỉ số), `true/false`, `"#AARRGGBB"` (màu), `[x, y]` (điểm). Giá trị lạ/thiếu được kẹp hoặc lấy mặc định khi dùng |

Các trường tuỳ chọn của Layer thêm ở M4b (mặc định = không đổi):
| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `rotation` | số | 0 | độ, **theo chiều kim đồng hồ**, quanh tâm layer |
| `crop` | `[x, y, w, h]` | `[0,0,1,1]` | vùng của nguồn được dùng, chuẩn hoá, gốc trên-trái, **tính theo hình đã xoay** (video điện thoại); `w`,`h` ≤ 0 → bỏ qua |
| `blend` | chuỗi | `normal` | `normal` · `add` · `multiply` · `screen` (giá trị lạ → `normal`) |
| `tracks[]` | mảng | (rỗng) | keyframe, xem dưới |

`tracks[]`: mỗi phần tử `{ "prop": "x"|"y"|"w"|"h"|"opacity"|"rotation", "keys": [ { "t": giây, "v": giá trị, "i": kiểu nối } ] }`.
`i` là cách nối từ key này đến key kế tiếp: `linear` (mặc định) · `easein` · `easeout` · `easeinout` · `hold`.
Thuộc tính có track thì **track ghi đè** giá trị tĩnh; trước key đầu và sau key cuối giữ giá trị của key đó. Khi nạp: key tự sắp xếp theo `t`, `t` âm → 0,
giá trị không phải số bị bỏ, `prop` lạ hoặc trùng và track rỗng bị bỏ, `i` lạ → `linear`.

Chưa lưu trong project: Transition đang chọn (kiểu, tham số, easing, Stinger) — chỉ là trạng thái phiên làm việc.

### 2.3 `channels[]`
| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `name` | chuỗi | `"Channel"` | |
| `width`, `height` | số nguyên | 1920, 1080 | kích thước khi chạy transition |
| `scenes` | mảng số | — | chỉ số vào `scenes[]` của gốc; chỉ số sai là lỗi nạp |
| `program`, `preview` | số | 0, 1 | chỉ số **trong `scenes` của Channel**; giá trị sai được đưa về hợp lệ |

### 2.4 `outputs[]`
| Trường | Kiểu | Mặc định | Ghi chú |
|---|---|---|---|
| `name` | chuỗi | `"Output N"` | |
| `channel` | số nguyên | 0 | chỉ số `channels[]`; sai là lỗi nạp |
| `screen` | số nguyên | -1 | chỉ số màn hình hệ điều hành (khi toàn màn hình); chỉ dùng khi không có `screenName`; chỉ số vượt quá số màn = Output **không hiển thị** |
| `screenName` | chuỗi | (rỗng) | tên màn hình (`QScreen::name()`, ví dụ `\\.\DISPLAY2`), **ưu tiên hơn `screen`**; tự ghi nhớ khi Output khởi động toàn màn hình. Không còn màn đó = không hiển thị |
| `fullscreen` | bool | false | |
| `width`, `height` | số nguyên | 1920, 1080 | kích thước khi chạy ở chế độ cửa sổ |
| `scale` | chuỗi | `fit` | `fit` · `fill` · `stretch` · `pixel` |
| `rotation` | số | 0 | được đưa về bội số của 90 (0/90/180/270), chiều kim đồng hồ |
| `crop` | `[x, y, w, h]` | `[0,0,1,1]` | vùng của Channel, chuẩn hoá, gốc trên-trái; `w`,`h` phải > 0 |
| `calibration` | đối tượng | mặc định = không đổi | xem dưới |

`calibration`: `brightness` (1), `contrast` (1), `gamma` (1), `gain` = `[r, g, b]` (mỗi giá trị 1).
Công thức: `out = pow(clamp(((c − 0.5)·contrast + 0.5)·brightness·gain, 0, 1), 1/gamma)`.

## 3. Quy tắc kiểm tra khi nạp
Nạp thất bại (trả thông báo lỗi, không crash) nếu: JSON hỏng · `app` sai · `version` ≠ 1 · `fps` ngoài 1–240 ·
`id` nguồn rỗng hoặc trùng · nguồn `video`/`image` không mở được · Layer tham chiếu nguồn không tồn tại ·
kích thước Scene < 1 · chỉ số Scene của Channel sai · Output trỏ Channel không tồn tại · `crop` có `w` hoặc `h` ≤ 0 ·
không có Channel nào · không có Output nào.

## 4. Ví dụ tối giản
```json
{
  "app": "LumaCast", "version": 1, "fps": 60,
  "sources": [ { "id": "src1", "type": "testpattern", "width": 1920, "height": 1080, "label": "Test" } ],
  "scenes":  [ { "name": "Test", "width": 1920, "height": 1080,
                 "layers": [ { "source": "src1", "fit": "stretch" } ] } ],
  "channels": [ { "name": "Channel 1", "width": 1920, "height": 1080, "scenes": [0], "program": 0, "preview": 0 } ],
  "outputs":  [ { "name": "LED 1", "channel": 0, "width": 1920, "height": 1080, "fullscreen": true, "screen": 1 } ]
}
```

## 5. Quy tắc phiên bản (chưa triển khai bộ di trú)
- Thay đổi **tương thích** (thêm trường tuỳ chọn có mặc định): giữ `version`, bản cũ vẫn mở được.
- Thay đổi **phá vỡ** (đổi nghĩa/xoá trường): tăng `version`; bản mới phải **di trú** được các phiên bản cũ; bản cũ gặp
  `version` mới hơn phải từ chối với thông báo rõ.
- Khi thêm tính năng cần lưu (Effect, keyframe, âm thanh, plug-in): cập nhật tài liệu này và thêm test vòng lưu–mở.
