# LumaCast — API điều khiển ngoài (CTL-01)

Cho phép điện thoại, Bitfocus Companion, script hoặc bàn điều khiển OSC điều khiển LumaCast khi đang chạy. Có hai cách, **cùng quy tắc an toàn như giao diện**:

| Cách | Giao thức | Mặc định | Dùng cho |
|---|---|---|---|
| HTTP + SSE | TCP, JSON | **Tắt**; khi bật chỉ nghe `127.0.0.1:8088` | điện thoại (có sẵn trang web), Companion (module HTTP), script, `curl` |
| OSC | UDP | **Tắt**; khi bật chỉ nghe `127.0.0.1:9000` | bàn điều khiển/phần mềm OSC (TouchOSC, Companion OSC…) |

Bật/tắt, cổng, LAN và token chỉnh trong **Cài đặt → Cài đặt vận hành…** (lưu bằng `QSettings`, theo từng máy). Mỗi lần bật/tắt và mỗi lệnh bị từ chối đều ghi vào log (`%AppData%\LumaCast\logs\lumacast.log`).

> **Trạng thái kiểm chứng:** toàn bộ phần dưới đã có test tự động trên Linux (`lumacast_control_tests`, `lumacast_m5c_tests`) bằng socket thật ở `127.0.0.1`. **Chưa thử** trên Windows, qua mạng LAN thật, từ điện thoại hoặc Companion thật (xem `TEST-PLAN.md`: T-CTL, T-OSC).

## 1. Quy ước

- Mọi chỉ số đều **bắt đầu từ 1**, giống giao diện: Kênh 1, Output 1, Scene 01.
- Scene là chung cho cả project (cùng danh sách Scene của mọi kênh).
- Lệnh **không chờ** render thread: trả `200` nghĩa là *đã nhận lệnh hợp lệ và đã đưa vào hàng đợi `RenderEngine::post()`* (thực thi ở đầu khung hình kế tiếp, ≤ 1 khung). Muốn biết kết quả, đọc `GET /api/state` hoặc theo dõi luồng SSE.
- Không có header CORS: trang web ở nguồn khác không gọi được API (cố ý, xem mục 5).

## 2. HTTP

Phản hồi luôn là JSON: `{"ok":true,...}` hoặc `{"ok":false,"error":"<mã>","message":"<mô tả>"}`.
Thân yêu cầu (POST) là một **đối tượng JSON** hoặc rỗng (rỗng = `{}`).

| Phương thức & đường dẫn | Thân (JSON) | Tác dụng |
|---|---|---|
| `GET /` | — | Trang web điều khiển cho điện thoại (không cần token để tải trang; các lệnh gọi API thì cần) |
| `GET /api/ping` | — | `{"ok":true,"pong":true}` |
| `GET /api/state` | — | Toàn bộ trạng thái (xem dưới) |
| `GET /api/events` | — | Luồng **SSE** đẩy trạng thái (xem dưới) |
| `POST /api/channels/{n}/preview` | `{"scene":3}` | Đặt Scene 3 vào Preview của kênh n. **Không bao giờ đổi Program** |
| `POST /api/channels/{n}/cut` | — | CUT: Program ↔ Preview |
| `POST /api/channels/{n}/take` | `{"type":"fade","duration":1.0,"easing":"easeinout"}` | TAKE. Tất cả trường tuỳ chọn |
| `POST /api/channels/{n}/ftb` | `{"on":true,"duration":1.0}` | Fade-To-Black. Bỏ `on` = đảo trạng thái |
| `POST /api/channels/{n}/playlist/play` | `{"item":2}` | Chạy playlist của kênh (đang tạm dừng: tiếp tục). `item` (từ 1, tuỳ chọn): chạy từ mục đó. Lệnh **live** |
| `POST /api/channels/{n}/playlist/pause` · `stop` | — | Tạm dừng / Dừng (Program giữ nguyên). Lệnh **live** |
| `POST /api/channels/{n}/playlist/next` · `prev` | — | Mục sau / mục trước (409 `playlist_stopped` nếu đang dừng). Lệnh **live** |
| `POST /api/channels/{n}/mute` | `{"on":true}` | **Tắt tiếng** Channel n (hình không đổi). Bỏ `on` = đảo. Lệnh **live** |
| `POST /api/channels/{n}/volume` | `{"volume":0.8}` hoặc `{"db":-6}` | Âm lượng Channel: tuyến tính 0–2 (1 = nguyên gốc) hoặc dB −60…+6. Ngoài khoảng/sai kiểu → 400, không đổi. Lệnh **live** |
| `GET /api/audio` | — | Thiết bị đang phát, `underruns`, `clippedBlocks`, meter peak/RMS [trái, phải] của từng Channel và master (không nằm trong `/api/state`/SSE vì đổi liên tục) |
| `POST /api/outputs/{i}/black` | `{"on":true}` | Đen **chỉ Output i**. Bỏ `on` = đảo |
| `POST /api/outputs/{i}/freeze` | `{"on":true}` | Dừng hình Output i |
| `POST /api/outputs/{i}/pattern` | `{"on":true}` | Mẫu thử Output i |
| `POST /api/black-all` | `{"on":true}` | Đen **tất cả** Output. Bỏ `on` = đảo |
| `POST /api/scenes/{n}/play` | — | Chạy đồng hồ Scene n (timeline) |
| `POST /api/scenes/{n}/pause` | — | Tạm dừng |
| `POST /api/scenes/{n}/stop` | — | Dừng và về 0 |
| `POST /api/scenes/{n}/seek` | `{"time":2.5}` | Tua tới 2,5 s (kẹp về cuối nếu vượt thời lượng) |

**TAKE** — `type`: `cut`, `fade` (mặc định), `dip`, `wipeleft`, `wiperight`, `wipeup`, `wipedown`, `push`, `slide`, `zoom`, `luma`, hoặc `plugin` kèm `"plugin":"<id Transition>"` (và `"params":{...}`). `duration`: 0,1–10 s. `easing`: `linear`, `easeinout` (mặc định), `easein`, `easeout`. Stinger chưa hỗ trợ qua API.

**Mã lỗi**

| HTTP | `error` | Khi nào |
|---|---|---|
| 400 | `bad_request` | Chỉ số/trường sai kiểu hoặc ngoài phạm vi (vd. `duration` = 99, `on` không phải bool) |
| 400 | `invalid_json` | Thân không phải một đối tượng JSON hợp lệ |
| 400 | `unknown_plugin` | `plugin` không tồn tại hoặc không phải Transition |
| 400 | `unsupported` | Tính năng chưa hỗ trợ qua API (Stinger) |
| 401 | `unauthorized` / `token_required` | Thiếu/sai token (kèm `WWW-Authenticate: Bearer`) |
| 403 | `bad_host` / `bad_origin` | Host hoặc Origin không hợp lệ (mục 5) |
| 404 | `not_found`, `channel_not_found`, `output_not_found`, `scene_not_found`, `source_not_found` | Đường dẫn hoặc chỉ số không tồn tại |
| 405 | `method_not_allowed` | Sai phương thức (kèm `Allow`) |
| 408 | `request_timeout` | Gửi yêu cầu quá chậm/dở dang (> 5 s) |
| 409 | `transition_in_progress`, `no_preview` | Kênh đang chuyển cảnh / chưa có Scene ở Preview |
| 409 | `playlist_empty`, `playlist_stopped` | Playlist chưa có mục / đang dừng (`item_not_found` là 404) |
| 413 / 414 / 431 | `body_too_large`, `uri_too_long`, `header_too_large`, `too_many_headers` | Vượt giới hạn kích thước |
| 423 | `locked` | **Khoá vận hành** đang bật và lệnh là lệnh *sửa cấu hình* |
| 501 / 505 | `not_implemented`, `bad_version` | `Transfer-Encoding`, không phải HTTP/1.0–1.1 |
| 503 | `too_many_streams` hoặc đóng ngay | Quá nhiều kết nối / luồng SSE |

### Trạng thái (`GET /api/state`)

```json
{"ok":true,"version":1,"locked":false,
 "engine":{"fps":60.0,"droppedFrames":0,"frameP50":1.2,"frameP99":2.4,"stalled":false,
            "stages":{"upload":{"p50":0.3,"p99":0.6},"scene":{"p50":0.4,"p99":0.9},"transition":{"p50":0,"p99":0},"monitor":{"p50":0.1,"p99":1.2},"output":{"p50":0.5,"p99":1.1}}},
 "channels":[{"index":1,"name":"Channel 1","program":1,"preview":2,"programName":"Test","previewName":"Xanh",
              "inTransition":false,"ftb":false,"ftbAmount":0.0,"audio":{"volume":1.0,"mute":false},"scenes":["Test","Xanh"]}],
 "scenes":[{"index":1,"name":"Test","time":0.0,"playing":true}],
 "outputs":[{"index":1,"name":"Output 1","channel":1,"width":1920,"height":1080,
             "black":false,"freeze":false,"pattern":false,"health":"ok","fps":60}]}
```

`engine.stages` (M6, trường thêm — `version` vẫn 1): thời gian CPU mỗi giai đoạn của khung render, ms, p50/p99 trên ~300 khung gần nhất. GPU chạy bất đồng bộ nên đây là thời gian phát lệnh, không phải thời gian GPU (muốn GPU: `lumacast --benchmark ... sync=1`, xem `TEST-PLAN.md` mục 4).
**M5b:** `outputs[]` có thêm `kind` (`screen`/`ndi`) và với Output NDI một khối `ndi: {name, sending, connections, status}`; `sources` liệt kê **chỉ nguồn trực tiếp** (NDI/camera): `{index (1-based theo Media Pool), name, kind, state (connecting|live|lost|unavailable), status, width, height, fps, reconnects}`; nguồn NDI thêm `audio: {fillMs, driftPpm, resyncs}` (độ đầy ring âm thanh, độ lệch đồng hồ ước lượng, số lần mất đồng bộ) — dùng khi thử T-NDI-IN.

`program`/`preview` là `null` khi chưa có Scene. `health`: `ok`, `no_screen` (mất màn), `hidden`, `wrong_screen`.

`channels[].playlist`: `{"state":"stopped|playing|paused","items":3,"item":2,"name":"logo","remaining":7}` — `item` (từ 1) là mục đang phát hoặc `null`; `remaining` là số giây còn lại (làm tròn lên), chỉ có khi đang phát/tạm dừng. CUT/TAKE gửi qua API làm playlist đang chạy **tạm dừng** (như bấm tại máy); trong lúc playlist đang chuyển cảnh, `take` trả 409 `transition_in_progress`, còn `preview` vẫn nhận.

### Nguồn trực tiếp (M5b)
| Phương thức | Đường dẫn | Mô tả |
|---|---|---|
| GET | `/api/sources` | Danh sách + trạng thái nguồn NDI/camera (như `state.sources`) |
| POST | `/api/sources/{n}/reconnect` | Đóng và mở lại kết nối của nguồn thứ `n` (chỉ số 1-based theo Media Pool). **Lệnh sửa** → `423` khi Khoá vận hành. `404 source_not_found` nếu không có, `400 not_live` nếu nguồn không phải NDI/camera |
Đen/Dừng/Mẫu của Output NDI dùng chung `POST /api/outputs/{i}/black|freeze|pattern`. Chưa có OSC cho nguồn trực tiếp.

### SSE (`GET /api/events`)

Kết nối giữ mở, máy chủ đẩy `event: state` (dữ liệu giống `/api/state`, thời gian làm tròn 0,1 s) **mỗi khi trạng thái đổi** (kiểm tra 10 lần/giây) và dòng `: ping` mỗi 15 s. Tối đa 8 luồng. Trình duyệt dùng `fetch()` + đọc luồng (để gửi được header `Authorization`; `EventSource` gốc không gửi được header nên **không** nhận token qua URL).

**Vì sao SSE mà không phải WebSocket:** nhu cầu chỉ là *đẩy trạng thái một chiều*, còn lệnh đã có REST. SSE chỉ là HTTP thường (cùng bộ phân tích, cùng giới hạn, cùng xác thực với phần còn lại; không cần bắt tay nâng cấp, mask khung, ping/pong, phân mảnh — phần lớn lỗi bảo mật của WebSocket tự viết nằm ở đó). Qt WebSockets không có trên máy Windows của dự án (chỉ cài Qt Base) nên không dùng được; tự cài RFC 6455 là phần mã nhiều rủi ro nhất mà chưa mang lại gì thêm.

### Ví dụ `curl`

```bash
curl http://127.0.0.1:8088/api/state
curl -X POST http://127.0.0.1:8088/api/channels/1/preview -d '{"scene":3}'
curl -X POST http://127.0.0.1:8088/api/channels/1/take -d '{"type":"wipeleft","duration":0.8,"easing":"linear"}'
curl -X POST http://127.0.0.1:8088/api/channels/1/cut
curl -X POST http://127.0.0.1:8088/api/outputs/2/black -d '{"on":true}'
curl -X POST http://127.0.0.1:8088/api/channels/1/mute -d '{"on":true}'
curl -X POST http://127.0.0.1:8088/api/channels/1/volume -d '{"db":-6}'
curl http://127.0.0.1:8088/api/audio
curl -X POST http://127.0.0.1:8088/api/black-all -d '{"on":false}'
curl -X POST http://127.0.0.1:8088/api/scenes/3/seek -d '{"time":2.5}'
curl -N http://127.0.0.1:8088/api/events                 # SSE
# khi có token (bắt buộc nếu nghe LAN):
curl -H "Authorization: Bearer <token>" -X POST http://192.168.1.20:8088/api/channels/1/cut
```

## 3. OSC

UDP, OSC 1.0: thông điệp đơn hoặc bundle (lồng tối đa 4 cấp; timetag bị bỏ qua, thực thi ngay). Tham số `int32`, `float32`, `string`, `T`/`F`. Địa chỉ phản chiếu API:

| Địa chỉ | Tham số | Tương đương HTTP |
|---|---|---|
| `/lumacast/channel/{n}/preview` | `int` Scene | `…/preview` |
| `/lumacast/channel/{n}/cut` | — | `…/cut` |
| `/lumacast/channel/{n}/take` | tuỳ chọn: `string` kiểu, `float` giây, `string` easing (theo thứ tự xuất hiện) | `…/take` |
| `/lumacast/channel/{n}/ftb` | tuỳ chọn `int`/`T`/`F` | `…/ftb` |
| `/lumacast/channel/{n}/mute` | tuỳ chọn `int`/`T`/`F` (1/T tắt tiếng, 0/F bật, bỏ = đảo) | `…/mute` |
| `/lumacast/channel/{n}/volume` | `float`/`int` 0–2 | `…/volume` |
| `/lumacast/playlist/{n}/play` | tuỳ chọn `int` mục (từ 1) | `…/channels/{n}/playlist/play` |
| `/lumacast/playlist/{n}/pause` · `stop` · `next` · `prev` | — | `…/channels/{n}/playlist/…` |
| `/lumacast/output/{i}/black` · `freeze` · `pattern` | tuỳ chọn `int`/`T`/`F` (1/T bật, 0/F tắt, bỏ = đảo) | `…/outputs/{i}/…` |
| `/lumacast/blackall` | tuỳ chọn như trên | `/api/black-all` |
| `/lumacast/scene/{n}/play` · `pause` · `stop` | — | `…/scenes/{n}/…` |
| `/lumacast/scene/{n}/seek` | `float` giây | `…/seek` |

Ví dụ: `/lumacast/channel/1/preview 3` · `/lumacast/channel/1/cut` · `/lumacast/channel/1/take "wipeleft" 0.8` · `/lumacast/output/1/black 1`.

Gói hỏng, địa chỉ lạ, chỉ số sai đều **bị bỏ qua** (không phản hồi — UDP) và ghi log có giới hạn tần suất. Giới hạn: gói ≤ 4096 byte, ≤ 64 thông điệp/gói, tối đa 256 gói xử lý mỗi lượt vòng lặp sự kiện.

## 4. Khoá vận hành (UI-07) và API

Khi **Khoá vận hành** bật, API vẫn nhận mọi lệnh *live* (CUT/TAKE/FTB/Đen/Dừng/Mẫu/chọn Scene/phát-tua timeline/**tắt tiếng và âm lượng Channel**). Lệnh *sửa cấu hình* (nếu có) bị từ chối `423 locked` và ghi log. **Hiện chưa có endpoint sửa cấu hình nào** nên khoá chưa chặn gì ở API; cơ chế đã được cài và có test (`ControlApi::addRoute(..., edit=true, ...)`) để các endpoint sửa sau này mặc định tuân theo khoá. API **không thể mở khoá** (mở khoá cần người thao tác tại máy).

## 4b. MIDI

Đầu vào MIDI (Windows, `winmm`) cấu hình ở *Cài đặt → Phím tắt và MIDI*, mặc định tắt. Mỗi nút (Note On, CC vượt lên từ dưới 64, hoặc Program Change) được "học" cho một lệnh: CUT, TAKE, FTB, BLACK ALL, chọn Preview 1–9, Đen/Dừng/Mẫu từng Output. Lệnh đi qua cùng `ControlApi` với HTTP/OSC nên cùng quy tắc: chọn Preview không đổi Program, bị từ chối khi đang chuyển cảnh hoặc chỉ số sai (ghi log), vẫn chạy khi Khoá vận hành, **không khoá/mở khoá được**. Khác HTTP/OSC: không ghi số kênh mà dùng **kênh đang chọn** trên giao diện, và TAKE dùng **hiệu ứng, easing, thời lượng đang chọn** (kể cả Stinger).

## 5. Bảo mật

1. **Mặc định tắt**, bật khi cần. Mặc định chỉ nghe loopback.
2. **Nghe LAN phải bật rõ ràng và BẮT BUỘC có token** (≥ 16 ký tự; nút "Tạo token ngẫu nhiên" tạo 32 ký tự từ bộ sinh ngẫu nhiên của hệ điều hành). Thiếu token → **không mở cổng**, ghi log, hiện lỗi. Mọi yêu cầu `/api/*` phải có `Authorization: Bearer <token>`; so sánh **hằng thời gian**. Token cũng có thể đặt khi chỉ nghe loopback.
3. **Giới hạn:** header ≤ 8 KB (tối đa 64 header), thân ≤ 16 KB, URL ≤ 2 KB, 32 kết nối đồng thời, 8 luồng SSE, hết thời gian 5 s cho một yêu cầu (chống slowloris), mỗi kết nối chỉ một yêu cầu (`Connection: close`). Yêu cầu hỏng trả 4xx đúng mã; không có lỗi nào làm crash hoặc treo.
4. **Chống tấn công qua trình duyệt khi nghe loopback:** từ chối (403) nếu `Origin` có mặt mà khác `Host` (chặn trang web ngoài POST vào `127.0.0.1` — CSRF) và nếu `Host` không phải `127.0.0.1`/`localhost`/`[::1]` (chặn DNS rebinding). Không gửi header CORS. Trang `GET /` có `Content-Security-Policy` chặt, `X-Frame-Options: DENY`.
5. **HTTP không mã hoá.** Token đi rõ trên mạng: chỉ dùng trong mạng điều khiển riêng; muốn mã hoá hãy đặt sau một reverse proxy TLS hoặc VPN. Token lưu dạng chữ thường trong cấu hình người dùng (`QSettings`), không nằm trong file project.
6. **OSC không có xác thực.** Vì vậy mặc định chỉ nghe `127.0.0.1`; nếu bật "LAN" thì hộp thoại cảnh báo và log ghi rõ. Chỉ bật trên mạng riêng, tốt nhất kèm tường lửa. (Lệnh OSC vẫn bị giới hạn ở tập lệnh live ở mục 3.)
7. Thông điệp lỗi không lộ thông tin nội bộ (đường dẫn file, địa chỉ bộ nhớ); log ghi địa chỉ nguồn của lệnh bị từ chối, tối đa 20 dòng/10 s rồi gộp thành một dòng tóm tắt (không để kẻ xấu làm đầy file log).
