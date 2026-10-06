# Sambhav V3 Firmware README

## Overview
Sambhav is an STM32G0-based environmental and soil monitoring node designed for field deployment. On every RTC alarm the firmware reads a wide set of sensors and stores each measurement as a binary record in external flash memory. At the scheduled post hour it publishes the stored records as JSON over MQTT through an always-on GSM modem. Between cycles it listens on MQTT for motor, command and OTA messages from the backend.

The system is intended for agriculture and environmental monitoring use cases such as:
- soil moisture and soil temperature monitoring
- nutrient sensing (NPK)
- pH and leaf wetness monitoring
- weather and climate observation
- battery and solar radiation monitoring

---

## Hardware Platform
- MCU: STM32G0B1RETX
- Core peripherals used:
  - ADC1 for analog voltage sensing
  - I2C3 for the CO2 and temperature/humidity sensors
  - I2C2 for the OLED display
  - SPI2 for external flash access
  - USART3 for Modbus RTU/RS485 communication (9600 8N1)
  - USART4 for GSM module communication (115200)
  - USART6 for the debug log (115200 8N1)
  - RTC for periodic wake-up and timekeeping
  - TIM1 in encoder mode for the rotary user interface

---

## Sensor and Measurement Details

| Function | Sensor / Interface | Details |
|---|---|---|
| CO2 | SCD41 / SCD40 compatible I2C sensor | Read over I2C at address 0x62 using single-shot measurement commands. Value is stored in ppm. |
| Temperature / Humidity | SHT45 I2C sensor | Read over I2C at address 0x44. The firmware takes multiple samples and uses the median value for stability. |
| Soil nutrient sensing | Modbus RTU NPK sensors (slaves 0x01, 0x07) | Two NPK sensors, one per soil zone, each giving N, P and K. |
| pH | Modbus RTU pH sensor (slave 0x0B) | Raw value ÷100. |
| Leaf wetness | Modbus RTU leaf wetness sensors (slaves 0x04, 0x08) | Two sensors, raw value ÷10. |
| Soil moisture / temperature / EC | Modbus RTU soil sensors (slaves 0x02, 0x06) | Two soil channels: moisture ÷100, temperature ÷100, EC as read. |
| Weather station | SMKB weather station, Modbus RTU (slave 0x05) | One 14-register read of signed 16-bit values: air temperature, relative humidity, pressure, rain intensity, accumulated rainfall, wind speed, and wind direction from both the vane and the ultrasonic sensor. `0x7FFF` means "no reading" and is reported as 0. A daily write to register `0x00F2` clears the station's errors. |
| Solar radiation | Dedicated Modbus RTU solar sensor (slave 0x03) | Signed 16-bit value in W/m², sent as the weather `SR` value. |
| PAR | Modbus RTU PAR sensor (slave 0x0A) | Raw value ×3.33. |
| Battery voltage | ADC input | ADC reading converted to battery volts. |
| Solar radiation (analog) | ADC input | A second solar sensor whose voltage is converted to a radiation estimate (0–1500 W/m² over 0.2–3.3 V), sent as the canopy `SR` value. |
| Time and wake timing | RTC | Keeps time in GMT, synced from the GSM network, and schedules sampling and posting. |

> Note: Some driver code contains references to SHT21/SHT85-compatible logic and older sensor variants. The current firmware path in the active application uses the SHT45-based acquisition flow. 

---

## Peripheral Summary

### 1. GSM Connectivity
- Module: EC200U
- Communication: UART via USART4
- The modem stays powered and connected between cycles. After an MCU reset it is only powered up if it doesn't answer `AT`.
- Functions:
  - time synchronization (`AT+QLTS`, with an HTTP fallback)
  - MQTT connection to the broker set in `GSM_EC200U.h`, using `SAMBHAV_<uid>` as the client ID
  - publishes logged sensor records on the `data` topic (QoS 1)
  - publishes the motor state on `motor_status` (QoS 1, retained)
  - subscribes to `motor_cmd`, `cmd` and `ota`; messages addressed to a different `Device_ID` are ignored
- The MQTT topics and payloads are defined by the external Python backend (`device_simulator.py` / `onem2m_bridge.py`, which bridges to a oneM2M Mobius CSE).
- There is no motor hardware yet. "Motor" is a software timer started by `motor_cmd` and run for `tti` × `MQTT_TTI_UNIT_SECONDS`; no GPIO is switched. OTA messages are only logged.
- The earlier HTTP upload path is kept in the source, commented out.

### 2. Display
- Display: SSD1306 OLED
- Interface: I2C2
- Purpose:
  - show boot and cycle status (`Booting....`, `Data Post`, `Sleep`)
  - step through the measured values on the boot cycle
  - show configuration and error messages

### 3. Data Logging
- Flash memory: W25Q32JV (32 Mbit)
- Interface: SPI2
- Purpose:
  - non-volatile log of 70-byte sensor records
  - unsent records stay in flash until they are published successfully, including across resets
  - circular log with persisted write/read pointers and intervals (address map)

### 4. Communication Bus
- Modbus RTU over RS485
- Physical interface: USART3
- Purpose:
  - polling external soil/weather sensors
  - CRC-checked requests and responses

### 5. User Interface
- Rotary encoder with push button
- Purpose:
  - allow local configuration of sampling interval and GSM posting interval
  - navigate simple menu-based settings
- The menu blocks normal operation, including MQTT handling, and ends with an MCU reset after both values are saved.

---

## System Architecture
The firmware follows a layered architecture:
1. Hardware abstraction layer via STM32 HAL
2. Sensor drivers for I2C, Modbus, ADC, flash, and GSM
3. Application logic for sensing, logging, display, and communication
4. Scheduling through RTC alarms. Between cycles the MCU stays running and services MQTT; standby entry is currently disabled.

### Block Diagram
```mermaid
graph LR
    subgraph MCU[STM32G0B1RE MCU]
        I2C3[I2C3]
        I2C2[I2C2]
        SPI2[SPI2]
        USART3["USART3<br>RS485, 9600 8N1"]
        USART4["USART4<br>115200"]
        USART6["USART6<br>115200 8N1"]
        ADC[ADC1]
        RTC["RTC<br>Alarm A"]
        TIM1["TIM1 encoder<br>+ button EXTI"]
        GPIO["GPIO power cuts<br>I2C_Cut, RS485_Cut,<br>FLASH_CS, GSM_KEY"]
    end

    SCD41["SCD41 CO2<br>0x62"] --- I2C3
    SHT45["SHT45 temp/RH<br>0x44"] --- I2C3
    I2C2 --- OLED[SSD1306 OLED]
    SPI2 --- FLASH["W25Q32JV flash<br>70-byte record ring log<br>+ address map"]
    USART3 --- MB["Modbus RTU slaves<br>0x01 NPK1 · 0x07 NPK2<br>0x02 soil 1 · 0x06 soil 2<br>0x04 leaf wetness 1 · 0x08 leaf wetness 2<br>0x03 solar radiation · 0x0A PAR · 0x0B pH<br>0x05 SMKB weather station"]
    USART4 --- GSM["Quectel EC200U<br>always on"]
    GSM -.MQTT.- BROKER["MQTT broker<br>MQTT_HOST:MQTT_PORT"]
    BROKER -.- BACKEND["Python backend<br>onem2m_bridge.py → Mobius CSE"]
    USART6 --> LOG[USB-serial debug log]
    BAT[Battery voltage] --> ADC
    SOLAR[Solar sensor voltage] --> ADC
    ENC[Rotary encoder + button] --> TIM1
```

---

## Firmware Overview
On startup, the firmware:
- initializes all MCU peripherals
- initializes the GSM driver and flash interface
- initializes the OLED and encoder UI
- runs a boot cycle, which also powers the modem if needed, syncs time and connects to MQTT

During each sampling cycle (boot, or RTC alarm), the firmware:
- reads the stored address map from flash
- reads environmental, soil, and weather data, once the RTC has a valid time
- stores the measurement record in flash
- shows the values on the OLED (boot cycle only)
- publishes all unsent records over MQTT on boot and at the scheduled post hour
- sets the next RTC alarm

Between cycles the firmware stays in its main loop. It handles incoming MQTT messages, runs the motor timer, and reconnects if the MQTT link drops. Every day at 03:15 GMT (08:45 IST) it clears the weather station's errors and restarts.

---

## Firmware Flow Diagrams

### Main loop (`main()`)
The MCU does not enter standby: it stays in `while(1)`, and the RTC alarm interrupt only sets `RTC_alarm_flag`. Between cycles the loop keeps servicing MQTT.

```mermaid
flowchart TD
    A([Power-on / MCU reset]) --> B["HAL + MX_*_Init peripherals"]
    B --> C["GSM_init, flash init,<br>SSD1306 init, rotary encoder init"]
    C --> D["bootup_flag = 1"]
    D --> L{"bootup_flag or<br>RTC_alarm_flag set?"}
    L -->|yes| APP[["App_operation()"]]
    APP --> CLR["RTC_alarm_flag = 0"]
    CLR --> P
    L -->|no| P[["MQTT_app_poll()"]]
    P --> R{"Button long-pressed<br>for more than 3 s?"}
    R -->|no| L
    R -->|yes| MENU["Blocking menu mode<br>ROT_ENC_handler / printHandler"]
    MENU --> S["Enter time_interval, then GSM_interval;<br>each saved to the flash address map"]
    S --> RST([HAL_NVIC_SystemReset])
    IRQ[["RTC Alarm A interrupt"]] -. sets RTC_alarm_flag .-> L
```

### Per-cycle sequence (`App_operation()`)
```mermaid
flowchart TD
    A["Power RS485 bus"] --> B{"Flash device ID = 0x16?"}
    B -->|yes| C["flash_ready_flag = 1<br>read_address_map()"]
    B -->|no| D
    C --> D{"Boot cycle?"}
    D -->|yes| E{"Modem answers AT?"}
    E -->|no| F["GSM_power_up()"]
    E -->|yes| G
    F --> G["GSM_time_sync()<br>AT+QLTS, HTTP fallback"]
    G --> H["MQTT_app_connect()<br>drop old session, connect, subscribe,<br>publish retained motor_status"]
    D -->|no| I
    H --> I["Read RTC time (GMT)"]
    I --> J{"03:15 GMT and<br>not a boot cycle?"}
    J -->|yes| K["Write 0x0000 to weather station<br>register 0x00F2 (clear errors)"]
    K --> RST([HAL_NVIC_SystemReset])
    J -->|no| M{"RTC year ≥ 2024?<br>(time synced)"}
    M -->|no| Z
    M -->|yes| N[["Read_data()"]]
    N --> O["Oled_Data_display()<br>(boot cycle only)"]
    O --> Q[["GSM_post_sequence()"]]
    Q --> Z["rtc_set_alarm()<br>next wake after time_interval<br>(standby entry is disabled)"]
```

### Sensor acquisition and logging (`Read_data()`)
Raw sensor bytes are packed in this order into the 70-byte record; scaling is applied later, when the record is turned into JSON.

```mermaid
flowchart TD
    A["Header: 0xAA + 4-byte GMT timestamp"] --> B["I2C3: SCD41 CO2"]
    B --> C["I2C3: SHT45 temp/RH<br>median of 5 samples"]
    C --> D["Modbus: NPK1, NPK2, pH,<br>leaf wetness 1 and 2"]
    D --> E["Modbus: soil 1 and 2<br>moisture / temp / EC"]
    E --> F["Modbus: solar radiation (0x03)"]
    F --> G["Modbus: SMKB weather station (0x05)<br>14 registers, 0x7FFF = no reading"]
    G --> H["Modbus: PAR (0x0A)"]
    H --> I["ADC: battery, solar voltage"]
    I --> J["Trailer: 0xEE"]
    J --> K{"Flash ready and<br>write address in log area?"}
    K -->|no| END([return])
    K -->|yes| L["write_node_data()<br>(erases sector when 4 KB aligned)"]
    L --> M["Advance write address;<br>skip to next 256-byte page if the<br>next record won't fit; save address map"]
    M --> END
```

### Data upload (`GSM_post_sequence()`)
```mermaid
flowchart TD
    A["Shift time to IST (+19800 s)<br>compute next post hour from GSM_interval"] --> B{"Boot cycle, or alarm at<br>the post hour, minute 00?"}
    B -->|neither| END([return])
    B -->|boot| C["bootup_flag = 0"]
    B -->|scheduled| D{"MQTT link up?"}
    D -->|no| E["MQTT_app_connect()"]
    D -->|yes| F
    E --> F{"IST hour = 0?"}
    F -->|yes| G["GSM_time_sync()"]
    F -->|no| H
    G --> H
    C --> H{"Flash ready, MQTT connected,<br>read address < write address?"}
    H -->|yes| I["read_flash_create_json()<br>decode + scale one record"]
    I --> J{"Publish on 'data' topic, QoS 1"}
    J -->|ok| K["read address += 70, page skip, wrap;<br>save address map; reset fail count"]
    K --> H
    J -->|failed| L{"5th consecutive failure?"}
    L -->|no| W["Wait 2 s"] --> H
    L -->|yes| X
    H -->|no| X{"read address > write address?"}
    X -->|yes| Y["Log corrupt: reset both<br>pointers to 0, save map"] --> END
    X -->|no| END
```

### MQTT commands and motor timer (`MQTT_app_poll()`)
There is no motor hardware; the motor is a software timer that runs for `tti` × `MQTT_TTI_UNIT_SECONDS`, with `tti` clamped to 0–255.

```mermaid
flowchart TD
    A{"Motor running and<br>timer expired?"} -->|yes| B["motor_stop()<br>publish motor_status 'off' (retained)"]
    A -->|no| C
    B --> C{"URC in aRxBuffer?<br>+QMTRECV / +QMTSTAT"}
    C -->|no| D{"30 s since last check?"}
    D -->|no| END([return])
    D -->|yes| E
    C -->|yes| E{"Not connected, +QMTSTAT,<br>or periodic check?"}
    E -->|yes| F{"AT+QMTCONN? reports<br>connected?"}
    F -->|no| G["MQTT_app_connect()"]
    F -->|yes| H["mqtt_connected = 1"]
    E -->|no| I
    G --> I
    H --> I{"mqtt_connected?"}
    I -->|no| CLR
    I -->|yes| J["Read next buffered message<br>AT+QMTRECV (up to 5)"]
    J --> K{"Message read and Device_ID =<br>SAMBHAV_uid?"}
    K -->|no message| CLR["Clear aRxBuffer<br>so only new URCs count"] --> END
    K -->|other device| J
    K -->|yes| T{"Topic"}
    T -->|"motor_cmd: start + tti"| S1["Start timer<br>publish motor_status 'on'"] --> J
    T -->|"motor_cmd: stop"| S2["motor_stop()"] --> J
    T -->|"cmd"| S3["Log command;<br>'reset' stops the motor"] --> J
    T -->|"ota"| S4["Log only, no update"] --> J
```

---

## Data Handling and Storage
- Each measurement cycle creates a binary record containing:
  - timestamp
  - CO2 data
  - temperature and humidity
  - NPK values
  - pH and leaf wetness
  - soil moisture / temperature / EC values
  - solar radiation and weather station values
  - PAR
  - battery and analog solar ADC values
- Each record is 70 bytes and starts with `0xAA` and ends with `0xEE`. Values are stored as raw sensor counts; scaling is applied when the record is converted to JSON.
- At post time, each unsent record is converted into a JSON payload and published on the MQTT `data` topic.
- The read pointer only advances after a successful publish, so unsent data stays in flash and is retried later. A pass gives up after 5 consecutive failures.
- Changing the record layout makes records already in flash unreadable, so erase the log when deploying a layout change.

---

## Configuration Notes
The firmware exposes configurable runtime values such as:
- sampling interval (default 15 minutes, configurable in the menu)
- GSM post interval (default 1 hour, configurable in the menu)

Long-press the rotary encoder button for about 3 seconds to enter configuration mode.

| Setting | Variable | Values |
|---|---|---|
| Data collection interval | `time_interval` | 5, 10, 15, 30, 60 minutes |
| GSM upload interval | `GSM_interval` | 1, 2, 3, 4, 6, 12, 24 hours |

These values are stored and restored by the flash-backed configuration logic.

---

## Notes for Development
- The project is organized around STM32 HAL and peripheral-specific drivers in the Core sources.
- The main application loop is centered in the main firmware entry and the sensor acquisition routines.
- Sampling is driven by RTC alarms. Standby entry (`HAL_PWR_EnterSTANDBYMode()` in `rtc_sync.c`) is commented out so the modem link and MQTT polling stay active between cycles.
- To trace a cycle, connect a USB-serial adapter to USART6 (115200 8N1). Every stage, sensor value, flash address and outgoing JSON payload is logged there.
- See [CLAUDE.md](CLAUDE.md) for the byte-level record layout, the Modbus map, and the constraints on changing them.
