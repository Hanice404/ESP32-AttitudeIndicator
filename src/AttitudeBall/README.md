# AttitudeBall —— ESP32-S3-Touch-LCD-1.85 姿态水平仪 / 3D 姿态球

> 目标板：Waveshare **ESP32-S3-Touch-LCD-1.85**（ESP32-S3R8 / 16 MB Flash / 8 MB PSRAM / 360×360 ST77916 / CST816 触摸 / QMI8658 IMU）
> 状态：**已在实机烧录运行并逐项验证通过**

---

## 1. 功能清单

| # | 功能 | 状态 |
| --- | --- | --- |
| 1 | ~~水平仪界面~~ | **已移除**（按需求，只保留姿态仪） |
| 2 | ~~3D 线框姿态球~~ | **已移除**（按需求） |
| 3 | **短按 BOOT = 对齐地平线**（姿态仪 cage）：把当前姿态记为"水平基准"，之后地平线以它为参考 | ✅ 实机验证 |
| 4 | ~~左右滑动切换界面~~ | 只剩一个界面，滑动不再触发切换 |
| 5 | **双击复位峰值**：最大倾斜角 / 最大角速度 | ✅ |
| 5.1 | **BOOT 键长按切换屏幕方向**：**0° ↔ 180° 两态切换**（USB 口朝下 / 朝上），自动存 NVS | ✅ 已验证 |
| 5.2 | **崩溃循环保护**：连续异常复位 ≥2 次自动进 safe mode（只跑串口+WiFi），防止变砖 | ✅ 已实现 |
| 6 | **电池显示**：ADC(GPIO8) → 百分比 | ✅ 实测 4.18 V |
| 7 | **PWR 长按关机** | ✅ |
| 8 | ~~自动降亮度 / 熄屏~~ | **已关闭**（`IDLE_DIM_MS`/`IDLE_OFF_MS` 均为 0，屏幕常亮） |
| 8.1 | **上下拖动调亮度**：边拖边调，每 18 px 一档（10%），底部黄色亮度条提示 0.45 s | ✅ 实机验证 |
| 9 | **WiFi OTA** | ✅ 实机验证（含 A/B 双槽交替） |
| 10 | **串口调试 + 调参命令** | ✅ 21 条命令（含 `info`/`calclear`/`reboot`） |
| 11 | ~~转屏 toast 提示~~ | 已按需移除（转屏时不再显示任何文字） |
| 12 | **姿态仪（人工地平仪）**：蓝天/棕地 + 地平线 + 俯仰刻度梯（带数字）+ 滚转刻度 + 飞机符号 | ✅ 实机验证 |
| 13 | **开机 Logo + 0.8 s 渐隐**：显示内置 Logo 700 ms，再用 0.8 s 交叉渐隐到姿态仪画面 | ✅ 实机验证 |

---

## 2. 上手三步

```powershell
cd E:\DeepSeekRuntime\APP\esp32s3-lcd185
.\build.ps1 -Upload -Monitor      # 编译 + 烧录 + 开监视器
```

| 操作 | 效果 |
| --- | --- |
| **短按 BOOT（40 ms ~ 1 s）** | **对齐地平线**：把当前姿态记为水平基准，地平线立刻居中水平（存 NVS，掉电不丢） |
| **双击屏幕** | 复位峰值记录 |
| **上下拖动** | 调亮度：上滑加亮、下滑减亮，每 18 px 一档（10%），底部显示亮度条 |
| **BOOT 键长按 ~2 s** | 屏幕方向 0°↔180° 切换（自动保存） |
| **PWR 键长按** | 关机 |

> 装机后第一件事：把设备摆到你要的"水平"姿态，**短按一次 BOOT**，地平线就对齐了。
> （开机时也会**自动归零一次**，所以平时不用手动按；换姿态后想重设再按一下。）

### 2.1 开机过程

```
上电 → LCD 初始化（背光灭）→ 预热 IMU（120 ms）
     → 自动归零（把当前姿态记为水平，等价短按一次 BOOT）
     → 渲染姿态仪首帧（在内存里）
     → 推 Logo（仍灭屏）→ 点亮背光 → Logo 停留 700 ms
     → 0.8 s 交叉渐隐 ──→ 姿态仪画面
```

| 想改什么 | 改哪里 |
| --- | --- |
| 开关开机动画 | `Config.h` → `SPLASH_ENABLE 1/0` |
| Logo 停留时间 | `Config.h` → `SPLASH_HOLD_MS 700` |
| 渐隐时长 | `Config.h` → `SPLASH_FADE_MS 800` |
| 换 Logo 图 | `.\_tools\make-logo.ps1 图片`（**支持 SVG** / PNG / JPG） |

> 关键点：**背光必须在首帧刷完之后才点亮**，否则会露出未初始化的显存（现象：开机红绿蓝闪屏）。
> 渐隐是"Logo 与姿态仪逐像素混合"（交叉渐隐），不是单纯调背光，所以过渡连贯不跳变。

### 2.2 更换开机 Logo（含 SVG）

```powershell
.\_tools\make-logo.ps1 我的Logo.svg                  # 默认写入 AttitudeBall\Boot_Logo.h
.\_tools\make-logo.ps1 我的Logo.png -Out 别的.h
```

| 说明 | 内容 |
| --- | --- |
| **支持格式** | **SVG**、PNG、JPG、BMP、GIF、WebP |
| **SVG 为什么能行** | 设备端跑不动矢量渲染，所以用**无头 Chrome/Edge 在电脑上光栅化**（720×720 二倍图），再缩到 360×360 —— 等于离线抗锯齿，边缘比 1:1 直接渲染更平滑 |
| **输出** | 360×360 RGB565 + RLE，平色 Logo 一般 10~40 KB flash |
| **示意图建议** | 黑底白字/单色块压缩率最好；渐变、照片类会显著变大（RLE 对它们不友好） |
| **手动转换** | `python _tools\gen_logo.py 图片.png Boot_Logo.h`（位图专用） |

> 踩过的坑：`make-logo.ps1` 必须**纯 ASCII**（PS 5.1 读无 BOM 的 UTF-8 中文会解析失败）；
> 无头浏览器必须给独立的 `--user-data-dir`，且只能传**一个**目标 URL，否则报
> `Multiple targets are not supported in headless mode`。

---

## 3. 实测确认的硬件事实

| 项 | 值 | 依据 |
| --- | --- | --- |
| 芯片 | ESP32-S3 QFN56 rev v0.2，8 MB PSRAM | esptool 输出 |
| MAC | `30:ed:a0:ad:98:90` | esptool |
| **IMU 地址** | **0x6B** | 官方示例 `Device_addr = QMI8658_L_SLAVE_ADDRESS`；I²C 扫描确认 |
| I²C 在线设备 | `0x20`(TCA9554) `0x51`(PCF85063) `0x6B`(QMI8658) `0x7E`(未知，疑似虚假 ACK) | 开机扫描 |
| QMI8658 Device ID | `0x7c` | 读寄存器 |
| 触摸 | `ChipID 0xb5 / ProjID 0x71 / FwVersion 0x04` | 驱动打印 |
| LCD | ST77916，QSPI 80 MHz，`vendor init case 2` | 驱动打印 |
| **GPIO6 / 7 / 8** | **PWR 键输入 / PWR 控制（SYS_EN）/ 电池 ADC** | `PWR_Key.h`、`BAT_Driver.h` |
| 背光 | GPIO5，LEDC 20 kHz 10 bit，`Set_Backlight(0~100)` | 驱动 |
| 空闲内存 | 内部 RAM 196 KB，PSRAM 8189 KB | 启动日志 |

---

## 3.0 ★ 当前已锁定的工作配置（2026-11-13 用户确认"画面正确"）

> **不要随意改动这组参数。** 这是经过多轮实测、由用户亲自确认画面正确的组合。
> `info` 应当打印出下面这一行，否则说明配置被改过或 NVS 被重置。

```
[info] srot=0 sdir=0 rot=1 invz=1 yflip=0 xflip=1 bfix=0 hflip=0 hpf=1
```

| 参数 | 值 | 含义 |
| --- | --- | --- |
| `srot` | 0 | 屏幕不旋转（USB-C 朝下竖拿时画面正立） |
| `sdir` | 0 | 屏幕旋转补偿方向 |
| `rot` | **1** | IMU 绕 Z 转 90°：本板 IMU 相对屏幕实际转了 90°（竖直时"世界上方"落在 +X） |
| `invz` | 1 | Z 取反 |
| `yflip` | **0** | Y 不单独取反（配合 rot=1） |
| `xflip` | **1** | **只镜像投影输出 axc**（水平仪气泡/show 数值左右），**不影响姿态仪画面** |
| `bfix` | 0 | 输出不额外旋转 |
| `hflip` | 0 | 姿态仪画面不做 180° 翻转 |
| `hpf` | **1** | 姿态仪**俯仰方向翻转**（抬头时地平线向下移）；本板实测需要 1 |
| `hrf` | **1** | 姿态仪**滚转方向翻转**（左右倾斜时地平线转向）；本板实测需要 1 |
| `hcal` | 1 | 地平线基准已设置（短按 BOOT / `cal` 写入；`hgx/hgy/hgz` 存物理坐标系方向） |

**万一 NVS 被清空**，按顺序发这些命令即可恢复（每行一条）：

```
bootrot 1
srot 0
sdir 0
rot 1
invz 1
yflip 0
xflip 1
bfix 0
hflip 0
hpf 1
hrf 1
save
```

然后平放设备、短按 BOOT 校准零位（姿态仪画面不受校准影响，但水平仪需要）。

---

## 3.1 IMU 轴向映射（本项目最关键的一条）

> ⚠️ 这一条我们绕了**很多轮**才定下来。历史结论（"`ax'=ay, ay'=ax, az'=-az`"）是**错的**，
> 它只在屏幕 180° 时因为双重取反碰巧抵消而"看起来对"。下面是实测最终结论。

**正确组合：`imuRot=0` + `imuInvZ=1` + `yflip=1`**

```
(ax, ay, az) = (+raw_ax, −raw_ay, −raw_az)      行列式 = +1（真旋转）
```

**物理含义：这颗 QMI8658 相对屏幕坐标系是「绕 X 轴翻转 180°」安装的。**

> 这也解释了为什么当初怎么调 `rot` 都不对：`imuRot` 只能做**绕 Z 轴**的 0/90/180/270° 旋转，
> 而绕 Z 轴的旋转**在数学上永远无法表达绕 X 轴的 180° 翻转** —— 必须再加一个 `yflip`（Y 单独取反）。
> 这是整件事绕了十几轮的根本原因。

**推导过程（可复现的依据）**

| 观测（屏幕 0°、正常拿、USB 口朝下） | 推出的原始数据 |
| --- | --- |
| 抬**右侧** → 气泡往右（正确） | 与 `axc = −raw_ax` 一致 |
| 抬**右侧** → 当 `rot=2/invz=1/yflip=0` 时气泡往**下** | 需要把 Y 单独取反 → `yflip=1` |

**最终有效参数**

| 参数 | 值 | 作用 |
| --- | --- | --- |
| `imuRot` | **0** | 不绕 Z 转 |
| `imuInvZ` | **1** | Z 取反（平放时 `azc=+1`） |
| `yflip` | **1** | **Y 单独取反 —— 关键，缺它上下必反** |
| `screenRot` | **0** | USB 口朝下、正常竖拿（字正面） |
| `bfix` | 0 | 90° 步进微调（本板不需要） |

以上已写入 `Store.h` 作为**出厂默认**，随时可 `info` 复核。

> 历史坑：早期用 `rot=4`（`ax'=ay, ay'=ax, az'=-az`）在 **180° 屏幕上**验证"正确"，
> 实际是屏幕旋转与坐标轴错误**互相抵消**。所以——**标定方向时，屏幕方向必须是最终使用方向**，
> 否则永远是在补偿错误的前提下求值（详见 §7 第 22 条）。

### 3.2 电池与电源（实机验证）

| 项 | 实测结论 |
| --- | --- |
| 电池接入 | MX1.25 3.7 V 锂电池；接上后系统自动接管，无需额外设置 |
| ADC 链路 | `analogReadMilliVolts(GPIO8)` → **1380 mV** × 3.0 ÷ 0.990476 = **4.18 V** |
| 满电读数 | USB 插着时读 4.18~4.19 V（充电器 CV 阶段把电池托在 4.2 V 附近） |
| **拔掉 USB** | ✅ **屏幕持续亮、不闪不重启** —— 电池无缝接管，无掉电复位 |
| **长按 PWR 2 s** | ✅ 屏幕熄灭、切断电池供电（**仅在电池供电时有效**；USB 插着时供电不会断） |
| 重新开机 | 按一下 PWR 键即可；固件启动后会拉高 `SYS_EN` 维持供电 |
| 电量曲线 | 九点分段插值：4.20/4.10/4.00/3.92/3.85/3.75/3.65/3.50/3.40 V → 100/90/78/65/50/30/15/5/0 % |

> ⚠️ **`SYS_EN` 是这块板的关键设计**：硬件只负责"按下 PWR 后上电"，之后**必须由固件持续拉高 SYS_EN 才能维持供电**。官方示例只在开机时按着 PWR 的情况下才拉高，导致接了电池也退化成 USB 供电、线一抖就重启。本项目已改为无条件拉高。

---

### 3.3 屏幕方向（BOOT 键长按切换）

**实现**：改 MADCTL（0x36）的 MV(bit5) / MX(bit6) / MY(bit7) 组合。驱动 `esp_lcd_st77916.c`
已实现 `mirror()` 与 `swap_xy()`，`Display_ST77916.cpp` 里的 `panel_handle` 是全局的，可直接操作。

| srot | 角度 | swap_xy | mirror_x | mirror_y |
| --- | --- | --- | --- | --- |
| 0 | 0° | false | false | false |
| 1 | 90° | true | true | false |
| 2 | 180° | false | true | true |
| 3 | 270° | true | false | true |

**操作**：长按 **BOOT**（GPIO0）≥ 800 ms，在 **0° ↔ 180°** 两态间切换（本项目只用竖装/倒装两种），自动保存到 NVS。
熄屏/暗屏时按会**先唤醒再旋转**（否则转了你根本看不见）。

> ⚠️ **该功能默认关闭**（`bootRotEnable = 0`）。实测拿板子、插拔线时极易误触，
> 而误触把屏幕转 180° 后，画面整体颠倒 —— 看起来就是"气泡上下左右全反"，
> 会让人误以为 IMU 映射错了（我们为此白折腾了两轮）。需要时串口发 `bootrot 1` 开启。
>
> 另有上电保护：必须先观察到一次"松开"状态，长按才开始计时，避免上电瞬间误判。

> 90°/270° 需要配合横拿，极易被误认为"方向算错"，因此**不再放进按键循环**；
> 确需横装时用串口 `srot 1` / `srot 3`。
> 转屏后屏幕上**不显示任何提示**（按需求已移除）。判断方法：180° 模式下把板子倒过来拿，画面才是正的。

#### ⚠️ 关键点：重力向量也必须跟着转（"画面坐标系"补偿）

屏幕转 90° 后，"画面里的右边"已经不等于物理的右边。**只转显示不转重力**，
气泡和姿态球就会与眼睛看到的画面对不上 —— 实测表现正是"上下左右都反了"。

所以 `handleImu()` 在 IMU 轴向映射之后，还要按 `contentRotSteps()` 再转一次：

```
crot = sdir ? (4 - srot) : srot          // 本板实测 sdir = 0 正确
```

| srot | 0° | 90° | 180° | 270° |
| --- | --- | --- | --- | --- |
| crot（sdir=0） | 0 | 1 | 2 | 3 |

> 180° 时 `crot=2`，正转反转等价，所以 **180° 是最无歧义的验证点** —— 我们就是先用它确认补偿量，再验证 90° 定下 `sdir=0`。

#### ⚠️⚠️ 最容易误会的一条：转屏后必须"跟着转板子拿"

**这不是 bug，是物理必然。** 屏幕转了 90°，画面就横过来了；如果你还竖着拿，
"抬右侧气泡往上"这类现象就会让人以为方向算错了 —— 但在**画面坐标系**里，
气泡每次都精准地朝着被抬高的那一侧移动，完全正确。

| 显示方向 | 正确拿法 |
| --- | --- |
| **0°** | 正常竖拿 |
| **90°** | 把板子顺时针转 90°（横拿） |
| **180°** | 倒着拿 |
| **270°** | 逆时针转 90°（横拿） |

实测证据（90° 显示 + 板子竖拿时）：
`抬右侧→气泡往上`、`抬前侧→气泡往左`，四组对应关系恰好是一个标准 **+90° 旋转**。

> 转屏后屏幕上**不会出现任何提示文字**（该 toast 已按需求移除）。

**本项目实际只用 0° 和 180°（竖装/倒装）**，两者都已被验证正确；
90°/270° 若将来要用，请按上表拿法复验，方向反了就用 `sdir 1` 翻转补偿符号。

**触摸坐标与手势方向必须使用同一个 `crot`**。用错会埋两个坑：将来加按钮会"点东亮西"；
90° 下"左右滑动切模式"失效。这两处都已修正。

## 4. 串口调参命令
波特率 115200（`CDCOnBoot=cdc`，走 Type-C 原生 USB）。命令以换行结尾：

| 命令 | 作用 |
| --- | --- |
| `show` | 打印原始/映射后加速度、投影分量、tilt/pitch/roll、当前 rot/invz/bflip/srot/sdir |
| `rot N` | 轴向映射：`0..3` = 绕 Z 转 0/90/180/270°，`4` = **XY 互换 + Z 取反（本板正确值）** |
| `invz 0\|1` | Z 取反开关（仅 rot=0..3 生效） |
| `bflip N` | 姿态球方向微调：`0` 正常，`1` 翻 pitch，`2` 翻 roll，`3` 都翻 |
| `srot N` | **屏幕方向** 0/1/2/3 = 0°/90°/180°/270°（等同 BOOT 长按，立即保存） |
| `sdir 0\|1` | **屏幕旋转方向补偿**：0=正转(+90°)，1=反转（本板实测 0 正确） |
| `btn` | 诊断：打印 BOOT(GPIO0) / PWR(GPIO6) 当前电平与 srot |
| `bat` | 打印电池 ADC 原始毫伏与换算电压 |
| `save` | 把当前 rot/invz/bflip 写入 NVS |
| `cal` | **对齐地平线**（同短按 BOOT）：把当前姿态记为水平基准，存 NVS |
| `calclear` | **清除校准**（换固件版本/坐标系后建议清一次，清完等 2 秒再 `cal`） |
| `yflip 0\|1` | **Y 轴单独取反**（X 对、上下反时用；本板必须为 1） |
| `bootrot 0\|1` | **BOOT 长按转屏开关**（持久化）；需要转屏时发 `bootrot 1` |
| `hflip 0\|1` | **姿态仪画面 180° 翻转**（持久化；本板保持 0） |
| `xflip 0\|1` | **只镜像投影输出 axc**（水平仪气泡左右），不影响姿态仪画面（持久化） |
| `ytau 0..60000` | 姿态球偏航回正时间（ms），0 = 关闭偏航积分 |
| `yawreset` | 偏航归零 |
| `bfix 0..3` | 气泡/姿态整体再转 0/90/180/270°（最后的"眼校"微调，持久化；作用在投影之后） |
| `fullscale 10..90` | **水平仪满偏角度**：倾斜到该角度气泡贴边。平放精测用 30，**竖直使用可设 90** |
| `tau 20..1000` | 低通滤波时间常数（ms）。越大越平滑、越滞后；默认 **300** |
| `swipe 20..250` | 已无实际作用（横滑切界面已移除；竖向调光阈值固定为 18 px） |
| `info` | 打印 NVS 原始设置 + 运行时状态（含 yflip/bfix，排查"设置没生效"必备） |
| `reboot` | 软重启 |
| `peak` | 复位峰值 |
| `mode l\|b` | 切换水平仪 / 姿态球 |

调试行格式（每 500 ms 一条）：

```
up=12s tilt=4.9 P=4.2 R=2.6 raw=(+0.07,+0.04,-0.94) rate=1 bat=4.19V 98% rot=4 mode=L
```

字段含义：运行秒数、合倾斜角、pitch、roll、**原始**（未映射）加速度、合角速度、电池电压与百分比、映射模式、当前界面。

---

## 5. 编译与烧录

```powershell
$root = 'E:\DeepSeekRuntime\APP\esp32s3-lcd185'
$exe  = "$root\_tools\arduino-cli\arduino-cli.exe"
$fqbn = 'esp32:esp32:waveshare_esp32_s3_touch_lcd_185:PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600'

& $exe compile --fqbn $fqbn --libraries "$root\_libs" --build-path "$root\_build" "$root\AttitudeBall"
& $exe upload  -p COM5 --fqbn $fqbn --input-dir "$root\_build" "$root\AttitudeBall"
```

板级设置说明：

- 用核心内置整板定义 `Waveshare ESP32-S3-Touch-LCD-1.85`（已固定 16 MB Flash、OPI PSRAM、VID/PID `0x303a:0x8290`）
- `PartitionScheme=app3M_fat9M_16MB`：3 MB APP，实测占用 **1.30 MB（41%）**
- `CDCOnBoot=cdc`：**让 `Serial` 走 Type-C 原生 USB**，否则串口监视器一片空白
- 编译警告数：**0**

### 5.1 WiFi 配置与无线升级（OTA）

`Config.h` 里已填好：

```cpp
#define WIFI_SSID "Hanice@Wi-Fi"
#define WIFI_PASS "1231231234"
#define OTA_HOSTNAME "attitude-ball"    // 局域网主机名 attitude-ball.local
#define OTA_PASSWORD "attball"          // 与上传时 --upload-field password 对应
```

开机日志确认连接：

```
[wifi] connecting to "Hanice@Wi-Fi" ...
[wifi] online, ip=192.168.50.59, OTA host=attitude-ball.local
```

**一键升级**（不用插线）：

```powershell
cd E:\DeepSeekRuntime\APP\esp32s3-lcd185
.\ota.ps1              # 自动发现 IP 并上传
.\ota.ps1 -Build       # 先编译再上传
.\ota.ps1 -List        # 只列出可发现的设备
.\ota.ps1 -Ip 192.168.50.59
```

等价的原始命令：

```powershell
& $exe upload --protocol network -p 192.168.50.59 --fqbn $fqbn `
    --upload-field password=attball --input-dir "$root\_build" "$root\AttitudeBall"
```

**A/B 双槽验证**（成功 OTA 后会交替）：

```
[ota] running: app1 (sub=17, 0x310000, 3072 KB)   ← 刚写入的槽
[ota] next   : app0 (sub=16, 0x010000, 3072 KB)   ← 下次写回另一个槽
```

#### ⚠️ OTA 踩过的三个坑

**① Windows 防火墙的 Block 规则优先级高于 Allow（本次真凶）**

系统里存在两条 `espota.exe` 的 **Inbound Block** 规则（`Query User` 类型，即以前在弹窗里点过"阻止"）。
表现为：**认证成功**（`Authenticating...OK`）但随后 `No response from device`。

根因在 arduino-esp32 的 OTA 是**两步握手**：先走 UDP 认证，然后**设备反向 TCP 连回电脑**取固件
（`ArduinoOTA.cpp` 里的 `client.connect(_ota_ip, _ota_port)`）。这一步被防火墙拦掉 →
设备报 `error[2]`（= `OTA_CONNECT_ERROR`）、宿主报 `No response`。

修法（需管理员）：

```powershell
Get-NetFirewallRule -DisplayName 'espota.exe' | Where-Object { $_.Action -eq 'Block' } | Remove-NetFirewallRule
New-NetFirewallRule -DisplayName 'espota.exe (OTA upload)' -Direction Inbound `
  -Program 'C:\Users\<你>\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.1.1\tools\espota.exe' `
  -Action Allow -Profile Private,Domain
```

> 本机原有的两条 Block 规则已备份到 `_tools\espota-fw-backup.txt` 后删除。

**② `arduino-cli` 的网络端口必须先被发现**

`-p 192.168.50.59:3232` 会报 `port not found`；正确写法是 **`-p 192.168.50.59`**（不带端口）。
前提是 `arduino-cli board list --discovery-timeout 5s` 能看到 `192.168.50.59 network`（mDNS 发现）。

**③ `[ota] error[N]` 的编号从 0 开始**

| N | 含义 |
| --- | --- |
| 0 | `OTA_AUTH_ERROR` 密码错 |
| 1 | `OTA_BEGIN_ERROR` `Update.begin()` 失败（空间/分区问题） |
| 2 | **`OTA_CONNECT_ERROR` 设备连不回电脑（防火墙）** |
| 3 | `OTA_RECEIVE_ERROR` 收数据失败 |
| 4 | `OTA_END_ERROR` 校验/收尾失败 |

> 电源安全：OTA 写到一半断电只会损坏**另一个槽**，当前运行槽不受影响 —— 这正是 A/B 双槽的价值（本次实测中途掐断过一次传输，设备照常运行）。


---

### 5.2 显示稳定性与崩溃保护

**① 撕裂（画面交错）的真正根因**

LVGL 的绘制缓冲只有 **1/10 屏**（`LVGL_BUF_LEN = 360*360/10`），`full_refresh = 1`。
整屏重绘时 LVGL 会**分 10 段**画进同一块缓冲，每段调一次 flush。
而 SPI 面板 IO 是异步队列（`trans_queue_depth = 10`），`esp_lcd_panel_draw_bitmap()` 只是**排队**就返回，
旧代码紧接着 `lv_disp_flush_ready()` → LVGL 立刻用下一段覆盖缓冲 → **正在 DMA 的那段数据被打断**。

平时只重绘局部（气泡/数字），一段就发完，看不出问题；**一切换界面就要整屏重绘**，于是明显交错。

修法（`Display_ST77916.cpp` + `LVGL_Driver.cpp`）：

```c
// 驱动侧：传输完成回调 + 二进制信号量
.on_color_trans_done = lcd_on_color_trans_done,
void LCD_WaitFlush(void) { xSemaphoreTake(s_color_done_sem, pdMS_TO_TICKS(100)); }

// LVGL 侧：等真发完再宣告完成
LCD_addWindow(...);
LCD_WaitFlush();          // ← 关键
lv_disp_flush_ready(disp_drv);
```

> ⚠️ **不要试图把 `trans_queue_depth` 设成 0 来"变同步"**：IDF 内部会用它创建 FreeRTOS 队列，
> 长度 0 直接 `assert failed: xQueueGenericCreate` 崩溃（本次真实事故，见 §7 第 15/16 条）。

**② 界面切换改为"创建一次、显隐切换"**

原来每次切换都 `lv_obj_del` + 重建，问题有三：指针悬空风险、每次重新 malloc 253 KB PSRAM 帧缓冲、
整屏重绘更易暴露撕裂。现在两个界面在 `setup()` 各创建一次，切换只做 `LV_OBJ_FLAG_HIDDEN` 显隐 +
`lv_refr_now(NULL)` 强制画完这一帧。

**③ 崩溃循环保护（safe mode）**

`RTC_NOINIT_ATTR` 变量在复位后保留，用来累计连续异常复位次数：

```
连续 PANIC / WDT 复位 ≥ 2 次  →  safe mode
safe mode 下：只初始化串口 + WiFi/OTA，跳过 LCD/LVGL/IMU
初始化全部成功才清零计数（所以正常重启不会误入）
```

串口会打印：

```
[guard] abnormalReset=0 failCount=0 safeMode=0     ← 正常
[guard] *** SAFE MODE *** skip LCD/LVGL/IMU        ← 已降级，仍可 OTA 救回
```

**这正好是本次事故缺少的保护**：当时因为没有它，崩溃循环让 USB-CDC 失稳，最后连烧录都进不去。

---

## 6. 参数速查

**`Config.h`**

| 宏 | 默认 | 作用 |
| --- | --- | --- |
| `FILTER_TAU_MS` | 80 | 低通时间常数，越大越稳越滞后 |
| `IMU_PERIOD_MS` | 10 | 采样周期（100 Hz） |
| `UI_PERIOD_MS` | 33 | 界面刷新上限（≈30 fps） |
| `LEVEL_FULLSCALE_DEG` | 30 | 气泡满偏角度 |
| `IDLE_DIM_MS` / `IDLE_OFF_MS` | 10000 / 30000 | 降亮度 / 熄屏 |
| `BAT_V_MIN` / `BAT_V_MAX` | 3.30 / 4.20 | 电量映射区间 |
| `WIFI_SSID` / `WIFI_PASS` | 已填写 | 见 §5.1 |
| `FILTER_TAU_MS` | 300 | 低通时间常数默认值（NVS 的 `tau` 优先） |

**NVS 中的运行时设置**（用串口命令改，`save` 落盘）：`imuRot`（默认 4）、`imuInvZ`（1）、`ballFlip`（0）、`mode`、`bright`、校准零位重力向量。

---

## 7. 开发中踩过的坑（都已在代码里修掉）

| # | 现象 | 根因 | 修法 |
| --- | --- | --- | --- |
| 1 | **板子每 2.5 秒自动重启**（120 s 内 22 次） | 调试 `printf` 的格式串写了 4 个 `%.1f` 但只给了 3 个对应参数，导致后面 `%s` 读到野指针 → LoadProhibited | 严格对齐格式串与参数；启动时打印 `esp_reset_reason()` 便于定位 |
| 2 | 平放时读数 180° | QMI8658 的 Z 轴指向板子背面 | Z 取反（`invz=1`） |
| 3 | 抬高右侧、气泡上下走 | IMU X/Y 与屏幕左右/上下不一致 | `rot=4`（XY 互换 + Z 取反，det=+1） |
| 4 | 姿态球反向（镜像） | 先用 `rot=1`（det=−1 的镜像） | 改用 det=+1 的 `rot=4` |
| 5 | 姿态球一个方向符号错 | `pitch/roll` 语义改过，`UI_Ball` 仍按旧语义旋转 | 改为 `p_dev = Ry(roll)·Rx(-pitch)·p_world` |
| 6 | 启动刷屏一行 `AAAA…` | 官方 `Display_ST77916.cpp` 第 287/320 行遗留调试打印 | 注释掉 |
| 7 | 串口中文变 `????` | 终端编码 | 日志统一改 ASCII |
| 8 | 电池抖动就可能掉电 | 官方 `PWR_Init()` 只在开机按着 PWR 时才拉高 `SYS_EN` | 无条件 `digitalWrite(PWR_Control_PIN, HIGH)` |
| 9 | 源码中文注释变乱码、**代码被并进注释行** | 用 PowerShell 默认 ANSI 编码读写 UTF-8 文件 | 三个文件按 UTF-8 重写；后续只用编辑工具改带中文的文件 |
| 10 | **转屏后气泡/姿态球方向全反** | 只旋转了显示，没把重力向量转到"画面坐标系" | `handleImu()` 里加 `contentRotSteps()` 二次旋转；用 180°（正反转等价）先定补偿量 |
| 11 | 触摸坐标/手势与重力用了**相反的旋转符号** | 两处各写了一套，符号不一致 | 统一到 `contentRotSteps()` |
| 12 | 熄屏时按 BOOT，转了屏却看不见 | 只改 MADCTL，没恢复背光 | 长按触发时先唤醒再旋转 |
| 13 | BOOT 长按像"没反应" | 按键只按下 0.15~0.2 s，低于 800 ms 阈值 | 打印 `[btn] boot down/up (xxx ms)` 后确认；按住约 1 s 即正常 |
| 14 | **切换界面时画面上下"交错/撕裂"** | LVGL 绘制缓冲只有 1/10 屏，整屏刷新分 10 段写**同一块**内存；SPI 是异步队列，`lv_disp_flush_ready()` 立即返回，下一段覆盖了还在 DMA 的数据 | 新增 `LCD_WaitFlush()`：用 `on_color_trans_done` 回调 + 二进制信号量，**等传输真完成**再让 LVGL 复用缓冲（含 100 ms 超时兜底） |
| 15 | **把 `trans_queue_depth` 设成 0 → 直接崩溃变砖** | IDF 的 `spi_bus_add_device` 用它创建 FreeRTOS 队列，长度 0 触发 `assert failed: xQueueGenericCreate` → PANIC → 崩溃重启循环 → USB-CDC 失稳，连烧录都进不去 | 队列保持官方值 10；撕裂改用 #14 的信号量方案。**教训：不要凭直觉改驱动层关键参数** |
| 16 | 板子卡在"能枚举但写入超时" | 崩溃循环把 USB-Serial-JTAG 外设搞挂，软件无法复位 | 必须物理动作：按 RESET / 拔插 USB / 断电冷启动（BOOT+断电）。宿主侧 `pnputil /remove-device` 只能改端口号，救不回来 |
| 17 | **滑动一次界面却"高速交替"，像两个画面重叠** | CST816 的手势寄存器在手指按住/滑动期间**持续**返回同一手势值；每 20 ms 轮询都执行一次切换 → 一次滑动触发十几次 | 手势去抖：**一次触摸只处理一个手势**（记 `g_handledGest`），手指完全离开才重新武装，另加 500 ms 冷却 |
| 18 | 设备管理器里出现两个 303A 串口，其中一个必然超时 | 历史残留的幽灵设备实例（非在线驱动） | `pnputil /remove-device "USB\VID_303A&PID_1001&MI_00\<实例>"`，或用 `esptool chip_id` 逐个验证哪个是真板子 |
| 19 | **换个屏幕方向后气泡上下左右全反（校准失效）** | 校准零位存的是**内容坐标系**（屏幕补偿在喂给解算器之前就做了），屏幕一转零位就整体跟着偏 | 校准零位一律存**物理板体坐标系**；用法时按 `contentRotSteps()` 换算：`applyCalibration()` |
| 20 | **烧新固件后旧校准/旧方向怎么都清不掉** | `Store::begin()` 版本号不匹配时只重写 `magic`、**没清旧键值**；下次启动 magic 就匹配了，旧 `calibOk`/`sRot` 全被读回来 | 不匹配时先 `s_prefs.clear()` 再写 `magic`；另有 `calclear` 命令可手动清校准 |
| 21 | 转屏后以为"方向算错了"，其实是画面转了 | 转屏后没有跟着转板子，"抬右侧气泡往上"是**画面坐标系**下的正确行为 | 见 §3.3 的"拿法对应表" |
| 22 | **方向怎么调都"反"，来回了十几轮** | ① 标定方向时屏幕处于 180°，坐标轴的错误与屏幕旋转**互相抵消**，导致"看起来对"的配置其实是错的；② 中途 BOOT 键把屏幕切回 180°，于是"反向"的反馈混进了两种状态 | **标定方向前，先固定屏幕方向与拿法**；每次改动后用 `info` 记录 `srot/rot/invz/yflip`，确保反馈对应同一个状态 |
| 22b | **`rot` 穷举 0..4 都调不出正确方向** | `imuRot` 只能做**绕 Z 轴**的旋转，而本板 IMU 是**绕 X 轴翻转 180°** 安装的 —— 绕 Z 的旋转在数学上永远表达不了它 | 增加 `yflip`（Y 单独取反）。最终解 `rot=0 + invz=1 + yflip=1`（行列式 +1，真旋转） |
| 22c | 烧录后屏幕方向莫名变 180° | BOOT 键在拿板子/插线时被误触（长按阈值只有 0.8 s） | ① `bootRotEnable` 默认关闭；② 阈值提到 2 s；③ 上电后必须先见到一次"松开"才认长按 |
| 25 | **每次烧录/连串口后 `srot` 都变成 180°，校准值也莫名变化** | **ESP32-S3 的 USB-Serial-JTAG 把主机 DTR 映射到 GPIO0(BOOT)**。电脑一打开串口监视器/烧录工具，GPIO0 就被拉低 → 固件误判为按住 BOOT：连接 >2 s 触发转屏，≈1 s 触发校准 | ① 上位机打开串口时**先置 DTR=false**（`$sp.DtrEnable = $false`）；② 用 Arduino IDE 监视器时注意它会拉 DTR，此时别按 BOOT 相关功能；③ 已验证：DTR=false 保持连接 7 s，`srot` 稳定不变 |
| 26 | **滑动完全无响应 / "只能切一次、点一下才行"** | 原厂 `Touch_Read_Data()` 是"非 0 才更新"：`if (buf[1] != 0) points = buf[1];`，**松手后没有任何地方把 points 清零** → 上层永远看不到"松开"边沿。此外手势寄存器**永远锁存上一次的值**，按下瞬间就被残值触发 | ① 驱动层改为每次如实写回（含 0）；② **左右滑动改用触摸坐标位移判定**（`\|dx\| ≥ 40 px` 且 `\|dx\| > \|dy\|×1.5`，抬起时结算一次），彻底不依赖手势寄存器；③ 手势寄存器只用于双击（并要求手指在屏上） |
| 27 | **`bfix` 改了完全没效果（0/1/2/3 看起来一样）** | 当时 `bfix` 同时旋转了"当前加速度"和"校准零点"→ 两者同转，**相对偏差不变**，等于空操作 | `bfix` 改为作用在**投影之后的结果**上（`att::Attitude::setOutputRot()`），不再碰输入与零点 |
| 28 | **方向"怎么调都不对"，实际是校准姿势错了** | 曾在设备**倾斜 13° 甚至侧立**时校准过（`g0=(0.217,...)`、`g0=(-0.998,...)`），零位本身是歪的 → 所有倾斜读数都被扭曲，后续所有方向判断全部失效 | **校准必须在真正水平的平面上做**；判断方向前先看 `info` 的 `g0`：平放时 `g0` 应≈`(0, 0, 1)`（本板约 `(0.04, 0.06, 0.998)`） |
| 29 | **姿态仪"左右倾斜变成了俯仰"** | 本板 IMU 相对屏幕**实际转了 90°**（竖直时世界上方落在 +X 轴），而映射用的是 `rot=0` | 改 `rot=1`（+ `yflip=0`）。**不要用屏幕方向 `srot` 去凑这个 90°** —— 那会把整个界面转歪 |
| 30 | **姿态仪天地/左右全反** | 渲染坐标系与传感器坐标系差 180° | 用 `hflip 1` 做整体 180° 翻转（本板最终确认**不需要**，保持 0） |
| 31 | **烧录重启后姿态仪突然上下颠倒** | `hflip` 当初只做运行时开关、**没有持久化**，重启回到代码默认值 `true` | `hflip` 已存入 NVS；代码默认值改为 `false`。**教训：所有"眼校"开关都必须持久化，否则每次烧录/重启都会跳回默认** |
| 23 | `calclear` 后立刻 `cal` → 校准存成 `(0,0,0)` | 滤波器刚被清空，`filtered` 还是零向量，归一化后基准正交基退化（现象：tilt 乱跳、方向随机） | `doCalibrate()` 增加保护：重力模长 < 0.5 g 直接拒绝并打印 `[calib] rejected` |
| 24 | `rot`/`invz` 组合穷举不出正确方向 | 这两个只是**旋转**，而本板实际还需要**一个轴的镜像**（`yflip`） | 增加 `yflip`（Y 取反）与 `bfix`（90° 步进微调）两个正交开关，方向问题最多两次就能对齐 |

> 坑 #1 最值得记：**断电式重启不一定是硬件问题**。加一句 `esp_reset_reason()` 就能立刻区分 `PANIC`（崩溃）/ `BROWNOUT`（掉电）/ `TASK_WDT`（看门狗）。

---

## 8. 已知限制 / 待办

| 项 | 说明 |
| --- | --- |
| 电池百分比 | 未接电池时读数为 USB 供电电压（约 4.18 V → 98%），**接了电池才准**；曲线是线性近似，可用 `BAT_V_MIN/MAX` 微调 |
| 充电状态 | 本板无独立充电状态引脚，`charging` 恒为 false |
| WiFi | 需自行填 `Config.h` 的 SSID/密码；不填不影响主功能（开机打印 `SSID not configured`） |
| 姿态球帧率 | 全屏 259 KB 经 QSPI 80 MHz 传输约 26 ms，理论上限 25~30 fps；实测未卡顿，如需更高可只重绘球体包围盒 |
| `0x7E` 设备 | I²C 扫描到这个地址，官方资料无对应芯片，疑似扫描时虚假 ACK，不影响功能 |
| 视角/配色 | 目前是深色 + 青绿线框，可按需改 `UI_Level.cpp` / `UI_Ball.cpp` 里的颜色常量 |

---

## 9. 与官方示例的差异

1. **禁用 LVGL 触摸设备**：官方 `LVGL_Driver.cpp` 注册的 indev 会与本项目 `Touch_Read_Data()` 抢数据（互相清零），导致芯片内置手势失效。已在 `Lvgl_Init()` 里注释掉 indev 注册。
2. **去掉示例 label**：`setup()` 里 `lv_obj_clean(lv_scr_act())`。
3. **同时读陀螺仪**：官方 `QMI8658_Loop()` 只调 `getAccelerometer()`，本项目额外调 `getGyroscope()`。
4. **LVGL 字体**：解锁 20/28/48 号（默认只开 14/16）。
5. **注释掉 AAAA 调试打印**（`Display_ST77916.cpp`）。
6. **无条件拉高 SYS_EN**（电源硬化）。
7. **不用的模块不引入**：SD / 音频 / RTC / MIC 的文件未拷入。

---

## 10. 文件结构

```
AttitudeBall/
├── AttitudeBall.ino        主程序：调度、触摸手势、省电、串口命令、复位原因
├── Config.h                ★ 需要改的文件（WiFi 账号、采样/省电/灵敏度参数）
├── App_Attitude.cpp/.h     姿态解算 + 校准（零位重力正交基投影）
├── App_Status.h            运行时状态（UI 与驱动解耦）
├── Store.cpp/.h            NVS 持久化（校准、模式、亮度、轴向映射）
├── UI_Horizon.cpp/.h       ★ 姿态仪（人工地平仪）：PSRAM 帧缓冲 + 自绘 天/地/刻度梯/飞机符号
├── Boot_Splash.cpp/.h      开机 Logo 推送 + 交叉渐隐
├── Boot_Logo.h             开机 Logo 数据（360×360 RGB565，RLE，约 39 KB）
│                           └ 由 `_tools/gen_logo.py` 从任意图片生成
├── WifiOta.cpp/.h          非阻塞 WiFi + ArduinoOTA
│  —— 以下为官方驱动（除注明外原样复用）——
├── Display_ST77916.cpp/.h  LCD + 触摸总入口（`LCD_Init()` 内部含 `Touch_Init()`；已注释 AAAA 打印）
├── esp_lcd_st77916.c/.h    ST77916 初始化序列
├── Touch_CST816.cpp/.h     CST816（独立 Wire1，GPIO1/3，0x15）
├── TCA9554PWR.cpp/.h       扩展 IO（LCD/触摸复位挂这里）
├── Gyro_QMI8658.cpp/.h      IMU（共享 Wire，GPIO10/11，0x6B）
├── I2C_Driver.cpp/.h       共享 I2C 封装
├── BAT_Driver.cpp/.h       电池 ADC（GPIO8）
├── PWR_Key.cpp/.h          电源键（GPIO6/7）
└── LVGL_Driver.cpp/.h      LVGL 移植层（双缓冲 1/10 屏；已禁用 indev）
```

> 已移除：`UI_Level.*`、`UI_Ball.*`（备份在 `_tools/removed_backup/`）。
> 移除后：Flash 1,237,788 → **1,210,012 B**；RAM 163,532 → **153,812 B**；PSRAM 帧缓冲 506 → **253 KB**。
