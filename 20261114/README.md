# 2026.11.14 실습 — 로봇 구동 시스템 개요 & Tinkercad LED Blink

## 1. 실습 개요

| 항목 | 내용 |
|---|---|
| 날짜 | 2026-11-14 |
| 주제 | 로봇 구동 시스템 구조(High/Low Level), Linux CLI 기초, 임베디드 기초 용어, LED 점멸 실습 |
| 실습 환경 | Tinkercad Circuits (Arduino Uno 시뮬레이션) |
| 결과물 | `code.txt` (LED Blink 코드), `Screenshot from 2026-11-14 17-28-56.png` (시뮬레이션 결과) |

---

## 2. 이론적 배경

### 2.1 과정 로드맵
1. Embedded 시스템 기반 저수준 제어
2. ROS2 기초 및 고수준 판단
3. SLAM & Navigation
4. RTOS 기반 고수준–저수준 연동

### 2.2 로봇 구동 시스템 (Robot Drive System)
로봇 구동은 단순한 모터 제어가 아니라 **센서 데이터 처리, 위치 추정, 경로 계획, 모터 제어가 동시에 수행**되는 시스템이다. 각 기능은 요구되는 **연산량**과 **제어 주기**가 다르기 때문에 시스템을 High Level / Low Level로 나눈다.

- **High Level** : 복잡한 연산, 로봇의 판단과 명령
- **Low Level** : 하드웨어를 빠르고 일정한 주기로 제어

#### High Level — Decision Making (SBC / ROS2)
- 센서 데이터 처리 및 주변 환경 인식 (Perception)
- Localization, Mapping, SLAM
- Path Planning, Navigation
- 카메라·LiDAR 등 대용량 센서 처리
- 하드웨어: SBC (Jetson, Raspberry Pi), 소프트웨어: ROS2

#### Low Level — Control (MCU / RTOS)
- 모터 PWM 출력
- Encoder 데이터 처리
- PID 제어
- 센서 데이터 수집 및 전처리 (IMU, 초음파 등)
- 직접적인 하드웨어 제어 (GPIO, ADC/DAC, UART, I2C, SPI)
- 짧고 일정한 주기의 제어 수행
- 하드웨어: MCU (Micro Controller Unit, 예: ESP32)

#### 레벨을 분리하는 이유
- High Level은 **많은 연산량**이 필요하다.
- Low Level은 **빠르고 일정한 제어 주기**가 필요하다.
- SBC는 Linux 기반이라 다양한 소프트웨어와 고성능 연산 환경을 제공하지만, 여러 프로세스가 동시에 실행되어 **제어 주기가 흔들릴 수 있다**.
- MCU + RTOS는 모터 제어 같은 **주기적 작업을 안정적으로** 수행한다.

| 구분 | High Level | Low Level |
|---|---|---|
| 역할 | 인식·판단·계획 | 하드웨어 직접 제어 |
| 주요 기능 | SLAM, Navigation, 센서 퓨전 | PWM, Encoder, PID, 센서 수집 |
| 하드웨어 | SBC (Jetson, Raspberry Pi) | MCU (ESP32 등) |
| OS / 프레임워크 | Linux + ROS2 | RTOS / 펌웨어 |
| 요구 사항 | 높은 연산 성능 | 빠르고 일정한 제어 주기 |

### 2.3 RTOS와 Linux 비교
레벨을 나누는 핵심 이유는 **실시간성(Real-time)** 이다. 실시간 시스템은 "빠른" 시스템이 아니라 **정해진 시간 안에 반드시 끝나는(deterministic)** 시스템을 말한다.

| 구분 | 의미 | 예시 |
|---|---|---|
| Hard Real-time | 마감 시간을 한 번이라도 놓치면 시스템 실패 | 모터 제어, 에어백, 제동 장치 |
| Soft Real-time | 가끔 늦어도 품질만 떨어지고 동작은 유지 | 영상 스트리밍, 경로 재계획 |

**Linux에서 제어 주기가 흔들리는 이유**
- Linux 스케줄러(CFS)는 여러 프로세스에 CPU 시간을 **공평하게** 나누는 것이 목표라서, 특정 작업이 정확한 시점에 실행된다는 보장이 없다.
- 인터럽트 처리, 메모리 스왑, 디스크 I/O, 커널 작업 등이 끼어들면 실행이 지연된다.
- 이렇게 실행 시점이 들쭉날쭉한 것을 **지터(Jitter)** 라고 한다. PID 제어처럼 일정한 주기(dt)를 가정하는 알고리즘은 지터가 커지면 제어 성능이 떨어진다.

**RTOS가 주기를 지키는 방법**
- **우선순위 기반 선점형 스케줄링** : 우선순위가 높은 작업이 준비되면 즉시 CPU를 빼앗아 실행한다.
- 커널이 작고 단순해서 작업 전환 시간이 짧고 예측 가능하다.
- 하드웨어 타이머 인터럽트로 정확한 주기를 만든다.

```
제어 주기 1ms 작업의 실행 시점 (|: 실행)
기준        :  |    |    |    |    |    |
MCU + RTOS  :  |    |    |    |    |    |     → 간격 일정 (jitter ≈ 0)
SBC + Linux :  |     |   |  ▒▒▒▒  |  |      |  → 다른 프로세스(▒) 때문에 밀리거나 당겨짐
```

| 항목 | Linux (SBC) | RTOS (MCU) |
|---|---|---|
| 스케줄링 목표 | 공평성, 전체 처리량 | 마감 시간 준수 |
| 스케줄링 방식 | CFS (시분할) | 우선순위 기반 선점형 |
| 응답 시간 | 수 ms ~ 수십 ms, 가변적 | 수 µs, 예측 가능 |
| 메모리 | 수 GB, 가상 메모리 | 수백 KB, 가상 메모리 없음 |
| 장점 | 풍부한 라이브러리, 멀티태스킹, 네트워크·GUI | 정확한 타이밍, 저전력, 빠른 부팅 |
| 대표 예 | Ubuntu (+ ROS2) | FreeRTOS, Zephyr |

> 참고: ESP32용 Arduino 코어는 내부적으로 **FreeRTOS** 위에서 동작한다. `setup()`/`loop()`도 FreeRTOS의 태스크 하나로 실행된다.
> Linux도 `PREEMPT_RT` 패치로 실시간성을 높일 수 있지만, 마이크로초 단위의 하드웨어 제어는 여전히 MCU에 맡기는 것이 일반적이다.

### 2.4 Linux와 CLI
- **Linux를 쓰는 이유** : 높은 개발 자유도, 강력한 패키지 관리, ROS2 중심 개발 생태계, 광범위한 활용 분야
- **GUI vs CLI**
  - GUI : 아이콘·창·마우스로 조작
  - CLI : 사용자가 명령어를 입력해 컴퓨터와 상호작용
- **터미널 프롬프트**
  - `~` 는 `$HOME`, 즉 `/home/<사용자명>` 을 의미
  - `echo $변수명` 으로 변수·환경변수 값을 확인 (예: `echo $HOME`)

### 2.5 MCU와 임베디드 시스템
- **MCU** : 프로세서, 메모리, 입출력 주변장치를 하나의 칩에 담은 소형 컴퓨터. 특정 하드웨어 제어에 특화되어 있다.
- **ESP32 구분**
  - 칩(SoC) : 실제 반도체 칩
  - 모듈 : 칩 + 플래시 메모리 + 안테나 등을 묶은 부품
  - 개발 보드 : 모듈 + USB, 전원부, 핀 헤더 등을 붙여 바로 실습할 수 있게 만든 보드
- **임베디드 시스템** : 특정 목적을 위해 기기 안에 내장된 컴퓨터 시스템. 제한된 자원, 실시간성, 하드웨어와의 밀접한 연동이 특징이다.

### 2.6 임베디드 기초 용어
| 용어 | 설명 |
|---|---|
| 옴의 법칙 | `V = I × R` |
| 전압 (V) | 전기를 밀어주는 힘(압력) |
| 전류 (I) | 전기의 흐름 |
| 저항 (R) | 전류의 흐름을 방해하는 정도. 저항이 있어야 전압 강하가 생긴다 |
| VCC / GND | 전류는 VCC(전원)에서 GND 방향으로 흐른다 |
| **Common GND** | 서로 연결된 모든 장치의 GND는 하나로 통일해야 한다 |
| GPIO | 센서·액추에이터와 신호를 주고받는 범용 입출력 핀 |
| INPUT / OUTPUT | INPUT: 외부에서 들어오는 정보 / OUTPUT: 외부로 내보내는 신호(동작) |
| HIGH / LOW | 논리 전압. ESP32는 HIGH = 3.3V, LOW = 0V (Arduino Uno는 HIGH = 5V) |
| ADC / DAC | 아날로그 ↔ 디지털 변환. 해상도 8bit = 0~255, 10bit = 0~1023, 12bit = 0~4095 |

### 2.7 통신 인터페이스 비교 (UART / I2C / SPI)
Low Level에서 MCU가 센서·SBC와 데이터를 주고받는 대표적인 직렬 통신 방식이다.

| 항목 | UART | I2C | SPI |
|---|---|---|---|
| 신호선 | TX, RX (+GND) | SDA(데이터), SCL(클럭) | MOSI, MISO, SCK, CS |
| 선 개수 | 2 | 2 | 4 (+장치마다 CS 1개) |
| 동기 방식 | **비동기** (클럭선 없음, baud rate를 미리 맞춤) | 동기 (마스터가 클럭 제공) | 동기 (마스터가 클럭 제공) |
| 통신 방향 | 전이중 (동시에 송수신) | 반이중 (한 번에 한 방향) | 전이중 |
| 연결 구조 | 1 : 1 | 1 : N (**주소**로 장치 구분, 7bit 주소) | 1 : N (**CS 핀**으로 장치 선택) |
| 속도 | 보통 9600 ~ 115200bps, 수 Mbps까지 | 100kHz / 400kHz / 1MHz | 수 MHz ~ 수십 MHz |
| 특징 | 구조가 가장 단순, 장거리에 비교적 강함 | 선 2개로 여러 장치 연결, **풀업 저항 필요** | 가장 빠름, 대신 선이 많음 |
| 로봇 활용 예 | SBC ↔ MCU 통신, GPS, LiDAR | IMU(MPU6050 등), OLED, 각종 센서 | 디스플레이, SD 카드, 고속 IMU |

```
[UART]  1:1, TX↔RX 교차
  MCU TX ───→ RX Device
  MCU RX ←─── TX Device

[I2C]   선 2개를 여러 장치가 공유, 주소로 구분
  VCC ─ R(풀업) ─┐
  MCU SDA ───────┼──── Dev1 (주소 0x68)
                 └──── Dev2 (주소 0x3C)      * SCL도 같은 구조

[SPI]   SCK/MOSI/MISO 공유, CS로 장치 선택
  MCU SCK/MOSI/MISO ──┬── Dev1
                      └── Dev2
  MCU CS1 ─────────────── Dev1
  MCU CS2 ─────────────── Dev2
```

- UART는 TX와 RX를 **서로 교차**해서 연결한다.
- 세 방식 모두 장치끼리 **GND를 공통으로** 연결해야 신호 기준 전압이 맞는다 (Common GND).

---

## 3. 핵심 기술 — LED Blink

GPIO 디지털 출력으로 LED를 1초 간격으로 켜고 끄는 가장 기본적인 저수준 제어 예제이다.

```cpp
const int ledPin = 7;

void setup()
{
    pinMode(ledPin, OUTPUT);     // 7번 핀을 출력 모드로 설정
}

void loop()
{
    digitalWrite(ledPin, HIGH);  // LED ON
    delay(1000);                 // 1초 대기
    digitalWrite(ledPin, LOW);   // LED OFF
    delay(1000);                 // 1초 대기
}
```

- `pinMode()` : GPIO 핀의 입출력 방향 설정
- `digitalWrite()` : 핀에 HIGH/LOW 논리 전압 출력
- `delay()` : ms 단위 대기 → 점멸 주기 2초(ON 1초 + OFF 1초)

---

## 4. 수행 과정

1. Tinkercad Circuits에서 새 회로 생성
2. Arduino Uno, LED, 저항, 브레드보드 배치
3. 배선
   - Arduino **5V** → 브레드보드 전원 레일(+), Arduino **GND** → 브레드보드 GND 레일(−)
   - Arduino **D7** → 저항 → LED **애노드(+, 긴 다리)**
   - LED **캐소드(−, 짧은 다리)** → GND 레일
4. 코드 편집기를 '텍스트' 모드로 바꾸고 위 코드 입력
5. '시뮬레이션 시작' 실행 후 LED 점멸 확인

> 저항은 LED에 과도한 전류가 흐르는 것을 막기 위한 전류 제한용이다 (`R = (V_HIGH − V_LED) / I`).

---

## 5. 결과

- LED가 1초 켜짐 → 1초 꺼짐을 반복하는 것을 시뮬레이션으로 확인했다.
- 결과 화면: `Screenshot from 2026-11-14 17-28-56.png`

![Tinkercad LED Blink](./Screenshot%20from%202026-11-14%2017-28-56.png)

---

## 6. 실행 방법

### Tinkercad (시뮬레이션)
1. https://www.tinkercad.com → Circuits → 새 회로 만들기
2. 4장의 배선대로 회로 구성
3. 코드 → 텍스트 모드 → `code.txt` 내용 붙여넣기
4. 시뮬레이션 시작

### 실제 보드 (Arduino IDE)
1. Arduino IDE 설치
2. 새 스케치에 `code.txt` 내용 붙여넣기 (또는 `.ino`로 저장)
3. 보드: Arduino Uno, 포트 선택
4. 업로드 후 D7에 연결된 LED 확인

---

## 7. 필수 의존성

| 항목 | 내용 |
|---|---|
| 시뮬레이터 | Tinkercad Circuits (웹 브라우저, 별도 설치 없음) |
| IDE (실보드 사용 시) | Arduino IDE |
| 보드 | Arduino Uno |
| 부품 | LED 1개, 저항 1개(220Ω 내외), 점퍼 와이어, 브레드보드 |
| 외부 라이브러리 | 없음 |

---

## 8. 파일 구성

```
20261114/
├── README.md                              # 본 문서
├── code.txt                               # LED Blink 소스 코드
└── Screenshot from 2026-11-14 17-28-56.png  # 시뮬레이션 결과 화면
```

## 9. 참고 사항
- ESP32로 옮길 경우 논리 전압이 3.3V이므로 핀 번호와 저항값을 보드에 맞게 조정해야 한다.
- 여러 장치를 연결할 때는 반드시 **Common GND**를 유지한다.
