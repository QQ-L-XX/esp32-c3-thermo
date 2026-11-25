# ESP32-C3 温湿度计 · LAN Thermometer

USB-C 供电的竖屏温湿度计。OLED 每 2.5 秒更新一次，手机和其他 ESP32 可通过家庭 Wi-Fi 读取同一份采样。

A USB-powered DHT11 thermometer with a portrait OLED, a local web dashboard and a JSON endpoint. It works on a local network without internet access.

## 接线 / Wiring

| 模块 / Module | 信号 / Signal | ESP32-C3 SuperMini |
|---|---|---|
| DHT11 module | DATA | GPIO3 |
| I²C OLED | SDA | GPIO4 |
| I²C OLED | SCL | GPIO1 |
| Both modules | VCC / GND | 3V3 / GND |

![单侧接线 / Single-side wiring](docs/wiring.png)

## 编译与配网 / Build and connect

1. 用 PlatformIO 打开仓库根目录，执行 `pio run -e c3`。通过 USB 连接目标 C3 后执行 `pio run -e c3 -t upload`。
2. 打开 115200 波特率串口监视器。首次启动时，连接串口列出的 `Thermo-Setup-xxxx` 热点，使用同一处显示的设备专属密码。
3. 手机打开 `http://192.168.4.1/`，输入家里的 2.4 GHz Wi-Fi 信息。连接成功后，切回家里 Wi-Fi，打开串口显示的局域网地址。

Build and upload with the PlatformIO commands above. Open the serial monitor at 115200 baud for the device-specific setup SSID and password. Join the hotspot, visit `http://192.168.4.1/` and enter the credentials of a 2.4 GHz network. Join that network to open the device at its printed IP address or mDNS name.

## 局域网读取 / Local API

Browse to `http://<device-ip>/` for the dashboard. Other devices can request `GET /api/latest` over HTTP. Any client on the same reachable LAN can read the measurements.

示例响应 / Example response

```json
{"temperature_c":23.8,"humidity_pct":38.0,"online":true,"status":"ok","age_ms":869}
```

`age_ms` 表示距离采样经过的毫秒数。仅在 `online` 为真时使用读数。等待首笔数据、传感器报错或数据过期时，温湿度字段返回 `null`。

Use readings only when `online` is true. The device reports `waiting`, `sensor_error` or `stale` with null measurements when a current sample is unavailable.

## 外壳 / Enclosure

[主壳](enclosure/MainCase.stl) · [滑动后盖](enclosure/RearCover.stl) · [组合支架](enclosure/ComboCarrier.stl)

R10 主壳靠后盖的侧壁和滑轨向后延长 2 mm，为焊线留空间。打印前请按实际模块检查开孔和夹持尺寸。

The R10 enclosure adds 2 mm of wiring clearance behind the boards. Check fit against your modules before printing.

## 验证 / Verification

原型上验证了传感器、局域网页面和 JSON 接口，并将接口读数与串口对照。此仓库的配网密码和主机名按设备生成。

The prototype was checked over the LAN against its serial readings. This repository generates a device-specific setup password and derives the host name from the board address.
