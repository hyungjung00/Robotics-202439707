# 2026.11.15 실습 — GPIO 입출력, ADC, PWM, Servo 제어

## 1. 실습 개요

| 항목 | 내용 |
|---|---|
| 날짜 | 2026-11-15 |
| 주제 | 버튼 입력(Pull-Down), 가변저항 ADC 읽기, PWM으로 LED 밝기 제어, `map()` 함수, Servo 모터 제어 |
| 실습 환경 | ① Tinkercad Circuits (Arduino Uno 시뮬레이션) ② Arduino IDE + ESP32 실보드 |
| 진행 순서 | Tinkercad로 회로·코드 검증 → ESP32 실보드로 가변저항·서보 제어 |

---

## 2. 이론적 배경

### 2.1 핀 정의와 GPIO 레지스터
코드에서 핀 번호는 `const int` 또는 `#define`으로 상수로 정의한다.

```cpp
const int ledPin = 7;   // 타입이 있는 상수 (컴파일러가 타입 검사)
#define SERVO_PIN 14    // 전처리기 치환 (타입 없음)
```

MCU 내부에서 각 GPIO 핀은 **레지스터의 1비트**로 관리된다. `pinMode()`, `digitalWrite()`, `digitalRead()`는 결국 이 레지스터의 비트를 0 또는 1로 바꾸거나 읽는 함수다.

| 역할 | Arduino Uno (AVR) | ESP32 | 비트 의미 |
|---|---|---|---|
| 방향 설정 (`pinMode`) | `DDRx` | `GPIO_ENABLE_REG` | 1 = 출력, 0 = 입력 |
| 출력 값 (`digitalWrite`) | `PORTx` | `GPIO_OUT_REG` (`W1TS`/`W1TC`로 set/clear) | 1 = HIGH, 0 = LOW |
| 입력 값 (`digitalRead`) | `PINx` | `GPIO_IN_REG` | 핀의 현재 논리 상태 |

```
예) Uno의 D7 = PORTD의 7번 비트
pinMode(7, OUTPUT)    →  DDRD  |=  (1 << 7)
digitalWrite(7, HIGH) →  PORTD |=  (1 << 7)
digitalWrite(7, LOW)  →  PORTD &= ~(1 << 7)
```

### 2.2 과전류와 전류 제한
GPIO 핀이 흘릴 수 있는 전류에는 한계가 있다 (Uno 핀당 최대 40mA, 권장 20mA 이하 / ESP32는 그보다 작음). LED처럼 저항이 거의 없는 부품을 바로 연결하면 과전류로 핀이나 부품이 손상될 수 있으므로 **직렬 저항으로 전류를 제한**한다.

```
R = (V_HIGH − V_LED) / I
예) Uno 5V, 빨간 LED 약 2V, 목표 15mA → R = (5 − 2) / 0.015 = 200Ω → 220Ω 사용
```

### 2.3 Floating과 Pull-Up / Pull-Down 저항
입력 핀이 아무 데도 연결되지 않은 상태를 **Floating**이라 한다. 이때 핀 전압이 주변 노이즈에 따라 흔들려 HIGH/LOW가 무작위로 읽힌다. 이를 막기 위해 저항으로 기본 상태를 고정한다.

```
   [Pull-Down]                     [Pull-Up]
      VCC                             VCC
       |                               |
     버튼                            10kΩ
       |                               |
       +──── GPIO                      +──── GPIO
       |                               |
     10kΩ                            버튼
       |                               |
      GND                             GND
 안 누름 LOW / 누름 HIGH         안 누름 HIGH / 누름 LOW
```

| 방식 | 연결 | 안 눌렀을 때 | 눌렀을 때 |
|---|---|---|---|
| **Pull-Down** | 입력 핀 ↔ 저항 ↔ GND, 버튼은 VCC 쪽 | LOW | HIGH |
| **Pull-Up** | 입력 핀 ↔ 저항 ↔ VCC, 버튼은 GND 쪽 | HIGH | LOW |

- 저항이 없으면 버튼을 눌렀을 때 VCC–GND가 직결(단락)된다. 저항(보통 10kΩ)이 있으면 `V = IR`에 따라 흐르는 전류가 작아진다 (5V / 10kΩ = 0.5mA).
- MCU 내부에도 풀업 저항이 있어 `pinMode(pin, INPUT_PULLUP)`으로 외부 저항 없이 사용할 수 있다. ESP32는 `INPUT_PULLDOWN`도 지원한다.
- 단, **ESP32의 GPIO 34~39는 입력 전용이고 내부 풀업/풀다운이 없다.**

### 2.4 Serial 통신
- 데이터를 한 줄로 1비트씩 순서대로 보내는 **직렬 통신**이다 (예: USB = Universal Serial Bus).
- **Baud rate** = 초당 전송 비트 수(bit/s). 이번 실습에서는 `115200`을 사용했다.
- 보드의 `Serial.begin()` 값과 시리얼 모니터의 baud rate가 다르면 수신 측이 비트를 잘못 끊어 읽어서 **문자가 깨진다**.

### 2.5 Edge 검출과 Bouncing
- **Rising edge** : LOW → HIGH로 바뀌는 순간 / **Falling edge** : HIGH → LOW로 바뀌는 순간
- 버튼을 "누르고 있는 동안" 켜지는 게 아니라 "한 번 딸깍할 때마다" 상태를 바꾸려면 상태(레벨)가 아니라 **변화(엣지)**를 검출해야 한다.
- **Bouncing(채터링)** : 기계식 스위치는 접점이 붙거나 떨어질 때 수 ms 동안 전압이 튄다. MCU는 이를 여러 번 누른 것으로 인식한다.

```
HIGH        ┌┐┌┐┌──────────────┐┌┐┌┐
            ││││││              ││││││
LOW  ───────┘└┘└┘              └┘└┘└┘───────
            ↑ 누를 때 튐        ↑ 뗄 때 튐 (수 ms)
          Rising edge         Falling edge
```

| 해결 방법 | 내용 |
|---|---|
| 하드웨어 | RC 필터(저항 + 커패시터)로 신호를 완만하게 만듦, 슈미트 트리거 사용 |
| 소프트웨어 | 상태가 바뀐 뒤 일정 시간(예: 50ms) 동안 유지될 때만 인정 (`millis()` 이용) |

**[추가 조사] 엣지 검출 + 디바운싱 예제 코드** — 버튼을 한 번 누를 때마다 LED가 토글된다.

```cpp
const int buttonPin = 7;
const int ledPin = 2;
const unsigned long DEBOUNCE_MS = 50;

int stableState = LOW;        // 디바운싱된 버튼 상태
int lastReading = LOW;        // 직전 루프에서 읽은 값
unsigned long lastChange = 0; // 마지막으로 값이 바뀐 시각
bool ledOn = false;

void setup() {
  pinMode(buttonPin, INPUT);  // 외부 Pull-Down 저항 사용
  pinMode(ledPin, OUTPUT);
}

void loop() {
  int reading = digitalRead(buttonPin);

  if (reading != lastReading) {        // 값이 튀면 타이머 리셋
    lastChange = millis();
  }

  if (millis() - lastChange > DEBOUNCE_MS && reading != stableState) {
    stableState = reading;
    if (stableState == HIGH) {         // Rising edge (눌린 순간)
      ledOn = !ledOn;
      digitalWrite(ledPin, ledOn ? HIGH : LOW);
    }
  }

  lastReading = reading;
}
```

**[추가 조사] 폴링(Polling) vs 인터럽트(Interrupt)**

위 예제와 이번 실습 코드는 `loop()`가 계속 돌면서 버튼 상태를 확인하는 **폴링** 방식이다. 다른 방법으로, 핀 변화가 생기는 순간 하드웨어가 CPU에 알려 주는 **인터럽트** 방식이 있다.

| 항목 | 폴링 | 인터럽트 |
|---|---|---|
| 동작 | 주기적으로 직접 읽어서 확인 | 엣지 발생 시 하드웨어가 ISR(인터럽트 서비스 루틴) 호출 |
| 반응 속도 | `loop()` 주기와 `delay()`에 따라 늦어지거나 놓칠 수 있음 | 즉시 반응 |
| CPU 사용 | 확인하느라 계속 사용 | 이벤트가 있을 때만 사용 |
| 구현 난이도 | 쉬움 | ISR은 짧게, 공유 변수는 `volatile` 등 주의 필요 |

```cpp
const int buttonPin = 7;
volatile bool pressed = false;          // ISR과 loop()가 같이 쓰는 변수
volatile unsigned long lastIsr = 0;

void IRAM_ATTR onButton() {             // ESP32: ISR은 IRAM에 올려 빠르게 실행
  unsigned long now = millis();
  if (now - lastIsr > 50) {             // 50ms 이내 재진입은 바운싱으로 보고 무시
    pressed = true;
    lastIsr = now;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(buttonPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(buttonPin), onButton, RISING); // Rising edge에서 호출
}

void loop() {
  if (pressed) {
    pressed = false;
    Serial.println("Button pressed");   // 무거운 작업은 ISR 밖에서 처리
  }
}
```
- 트리거 종류: `RISING`(LOW→HIGH), `FALLING`(HIGH→LOW), `CHANGE`(둘 다)
- ISR 안에서는 `delay()`, `Serial.print()` 같은 오래 걸리는 함수를 쓰지 않는다.

### 2.6 ADC와 가변저항
- 가변저항(Potentiometer)은 노브 위치에 따라 출력 전압이 **연속적으로** 변하는 아날로그 신호를 낸다 → **ADC**로 디지털 값으로 바꿔 읽는다.
- 해상도: Arduino Uno는 **10bit (0~1023)**, ESP32는 **12bit (0~4095)**.
- 가변저항의 양 끝 다리는 VCC와 GND, 가운데 다리(wiper)는 아날로그 입력 핀에 연결한다.

**ADC 값 → 전압 변환**
```
V = (ADC 값 / 최대값) × 기준 전압
Uno   : V = value / 1023 × 5.0      예) 512  → 약 2.5V
ESP32 : V = value / 4095 × 3.3      예) 2048 → 약 1.65V
```

**ESP32에서 GPIO39를 사용한 이유**
- ESP32의 ADC는 **ADC1**(GPIO 32~39)과 **ADC2**(GPIO 0, 2, 4, 12~15, 25~27) 두 개가 있다.
- **ADC2는 Wi-Fi를 켜면 사용할 수 없다** (Wi-Fi 드라이버가 ADC2를 점유). 나중에 무선 통신을 붙일 것을 생각하면 아날로그 입력은 ADC1 핀을 쓰는 것이 안전하다.
- GPIO39(ADC1_CH3)는 **입력 전용 핀**이라 출력과 충돌할 일이 없다. 대신 내부 풀업/풀다운이 없지만, 가변저항은 항상 전압을 출력하므로 상관없다.

**ESP32 ADC 주의사항**
- 입력 전압 범위는 **감쇠(Attenuation)** 설정으로 정해진다. Arduino 코어 기본값은 11dB로 약 0 ~ 3.1V까지 측정한다 (`analogSetAttenuation()`으로 변경 가능).
- ESP32 ADC는 **비선형성**이 있어서 0V 근처와 최대 전압 근처에서 값이 정확하지 않다. 노브 끝부분에서 값이 0이나 4095로 일찍 붙는 현상이 이 때문이다.
- 정확한 전압이 필요하면 칩에 저장된 보정값을 사용하는 `analogReadMilliVolts()`를 쓸 수 있다.

### 2.7 PWM (Pulse Width Modulation)
- 디지털 핀은 HIGH/LOW만 낼 수 있으므로, 아주 빠르게 켜고 끄면서 **HIGH 유지 비율(듀티비)**을 조절해 평균 전압을 바꾼다.
- MCU 내부의 **타이머(system clock)**가 일정한 주기를 만들어 준다.
- Arduino Uno는 `~` 표시가 있는 핀(3, 5, 6, 9, 10, 11)에서만 `analogWrite()`가 가능하다. 이번 실습에서는 **9번 핀**을 사용했다. 값의 범위는 0~255(8bit).
- ESP32는 LEDC 주변장치로 대부분의 출력 핀에서 PWM을 쓸 수 있다.

```
        |← 1주기 →|
25%  ┌─┐       ┌─┐       ┌─┐        analogWrite 64
     ┘ └───────┘ └───────┘ └──
50%  ┌────┐    ┌────┐    ┌────┐     analogWrite 128
     ┘    └────┘    └────┘    └──
75%  ┌──────┐  ┌──────┐  ┌──────┐   analogWrite 191
     ┘      └──┘      └──┘      └─
평균 전압 ≈ VCC × 듀티비
```

### 2.8 `map()` 함수
한 범위의 값을 다른 범위로 비례 변환한다.

```cpp
map(value, fromLow, fromHigh, toLow, toHigh)
map(value, 0, 1023, 0, 255);  // Uno ADC(10bit) → PWM(8bit)
map(value, 0, 4095, 0, 180);  // ESP32 ADC(12bit) → 서보 각도
```
정수 연산이라 소수점 이하는 버려진다.

### 2.9 Servo 모터
- 0~180° 범위에서 **지정한 각도로 회전**하는 모터다.
- 약 50Hz(20ms 주기) PWM 신호를 받고, HIGH 펄스 폭(대략 0.5~2.5ms)으로 각도가 정해진다.
- ESP32에서는 `ESP32Servo` 라이브러리의 `attach()`로 핀을 연결하고 `write(각도)`로 제어한다.

**각도 ↔ 펄스 폭 계산**
```
주기 = 1 / 50Hz = 20ms
펄스 폭 = 최소 펄스 + (각도 / 180) × (최대 펄스 − 최소 펄스)

ESP32Servo 기본값 (최소 544µs, 최대 2400µs) 기준
  0°   → 544µs   (듀티비 약 2.7%)
  90°  → 1472µs  (듀티비 약 7.4%)
  180° → 2400µs  (듀티비 12%)
```
- 서보 신호는 결국 **PWM**이다. 다만 LED 밝기 제어처럼 듀티비 0~100%를 다 쓰는 게 아니라, 20ms 중 약 0.5~2.5ms 구간만 사용한다.
- 그래서 `analogWrite()`(기본 주파수가 수 kHz, 값 0~255)로는 서보를 제대로 제어할 수 없고, 50Hz와 µs 단위 펄스를 만들어 주는 Servo 라이브러리를 사용한다.
- 서보 모델마다 실제 펄스 범위가 조금씩 다르므로 끝까지 안 돌거나 떨리면 `attach(pin, min, max)`로 범위를 조정한다.

### 2.10 그 밖의 수업 내용
- **망간 배터리** : 이번 수업에서 언급된 전원
- **IMU(관성 측정 장치)** : 자이로스코프(회전 속도), 가속도계(선가속도), 지자기 센서(나침반)를 조합해 자세와 움직임을 측정한다. 로봇의 위치 추정에 사용된다.
- **Arduino IDE의 업로드** : 작성한 코드를 컴파일해 MCU의 플래시 메모리에 기록(flash)한다.

---

## 3. 수행 과정

### Part A. Tinkercad (Arduino Uno)

| # | 실습 | 코드 | 회로 | 배선 |
|---|---|---|---|---|
| A-1 | 버튼 상태 시리얼 출력 | `button.txt` | `button.png` | 5V → 버튼 → **D2**, D2 → 저항 → GND (**Pull-Down**) |
| A-2 | 버튼 누르는 동안 LED 켜기 | `button_linked_LED.txt` | `button_linked_LED.png` | 버튼 입력 **D7** (Pull-Down), LED 출력 **D2** (직렬 저항) |
| A-3 | 가변저항 값 시리얼 출력 | `Potentiometer.txt` | `Potentiometer.png` | 가변저항 양 끝 → 5V / GND, 가운데 → **A2** |
| A-4 | 가변저항으로 LED 밝기 조절 | `Potentiometer_linked_LED.txt` | `Potentiometer_linked_LED.png` | 가변저항 → **A2**, LED → **D9 (PWM)** + 직렬 저항 |

**A-4 핵심 코드**
```cpp
int value = analogRead(potPin);                // 0 ~ 1023
int brightness = map(value, 0, 1023, 0, 255);  // 0 ~ 255
analogWrite(ledPin, brightness);               // PWM 출력
```

### Part B. ESP32 실보드 (Arduino IDE)

| # | 실습 | 코드 | 연결 |
|---|---|---|---|
| B-1 | 가변저항 값 시리얼 출력 | `pot_serial.ino` | 가변저항 → **GPIO39**, 3V3, GND |
| B-2 | 서보 0° ↔ 100° 반복 | `servo.ino` | 서보 신호선 → **GPIO14** |
| B-3 | 서보 0° → 180° → 0° 스윕 | `step_angle.ino` | 서보 신호선 → **GPIO14** |
| B-4 | 가변저항으로 서보 각도 제어 | `pot_angle.ino` (`sketch_nov15a.ino`는 같은 코드) | 가변저항 → GPIO39, 서보 → GPIO14 |

- GPIO39는 ADC1 채널의 **입력 전용 핀**이라 가변저항 입력에 적합하다.
- ESP32 ADC는 12bit이므로 B-4에서 `map(value, 0, 4095, 0, 180)`으로 각도로 변환했다.

---

## 4. 결과

| # | 결과 |
|---|---|
| A-1 | 버튼을 누르면 `Pressed`, 떼면 `Not Pressed`가 시리얼 모니터에 출력됨 |
| A-2 | 버튼을 누르는 동안만 LED가 켜짐 |
| A-3 | 노브를 돌리면 시리얼 값이 0 ~ 1023 사이에서 변함 |
| A-4 | 노브 위치에 따라 LED 밝기가 연속적으로 변함 |
| B-1 | 노브를 돌리면 시리얼 값이 0 ~ 4095 사이에서 변함 (Uno보다 해상도가 4배) |
| B-2 | 서보가 1초 간격으로 0°와 100°를 왕복 |
| B-3 | 서보가 15ms마다 1°씩 움직이며 부드럽게 0°↔180° 스윕 |
| B-4 | 노브 값이 시리얼에 출력되고 서보 각도로 변환됨 |

**분석**
- 같은 가변저항이라도 ADC 해상도에 따라 값의 범위가 다르므로(1023 vs 4095), `map()`의 입력 범위를 보드에 맞춰 바꿔야 한다.
- A-2는 **레벨(상태)** 기반이라 누르는 동안만 켜진다. 한 번 딸깍으로 켜고 끄려면 2.5절의 **엣지 검출 + 디바운싱**이 필요하다.

---

## 5. 실행 방법

### Tinkercad (`.txt` 파일)
1. https://www.tinkercad.com → Circuits → 새 회로 만들기
2. 같은 이름의 `.png` 회로도를 보고 부품 배치·배선
3. 코드 → 텍스트 모드 → `.txt` 내용 붙여넣기
4. 시뮬레이션 시작 → 시리얼 모니터에서 출력 확인 (baud 115200)

### ESP32 (`.ino` 파일)
1. Arduino IDE → 보드 매니저에서 **esp32 (Espressif Systems)** 설치
2. 라이브러리 매니저에서 **ESP32Servo** 설치 (서보 예제에 필요)
3. 보드를 ESP32 Dev Module 등 사용하는 보드로, 포트를 연결된 COM/tty로 선택
4. `.ino` 파일 열기 → 업로드
5. 시리얼 모니터 baud rate를 **115200**으로 설정

---

## 6. 필수 의존성

| 항목 | 내용 |
|---|---|
| 시뮬레이터 | Tinkercad Circuits (웹) |
| IDE | Arduino IDE |
| 보드 패키지 | esp32 by Espressif Systems |
| 라이브러리 | `ESP32Servo` (servo / step_angle / pot_angle / sketch_nov15a) |
| 하드웨어 | Arduino Uno(시뮬레이션), ESP32 개발 보드, 택트 스위치, LED, 저항(220Ω, 10kΩ), 가변저항, 서보 모터(SG90 등), 브레드보드, 점퍼선 |

---

## 7. 파일 구성

```
20261115/
├── README.md
├── button.txt / button.png                                   # A-1
├── button_linked_LED.txt / button_linked_LED.png             # A-2
├── Potentiometer.txt / Potentiometer.png                     # A-3
├── Potentiometer_linked_LED.txt / Potentiometer_linked_LED.png # A-4
├── pot_serial.ino      # B-1
├── servo.ino           # B-2
├── step_angle.ino      # B-3
├── pot_angle.ino       # B-4
└── sketch_nov15a.ino   # B-4와 동일
```

## 8. 코드 검토 및 개선점

**`pot_angle.ino` / `sketch_nov15a.ino`**
- `myServo.attach()`를 호출하지 않았고, 각도를 `analogWrite()`로 출력하고 있다. 2.9절에서 설명했듯이 `analogWrite()`는 서보가 요구하는 50Hz 펄스를 만들지 않으므로, 서보 라이브러리 방식으로 바꾸는 것이 맞다.
- 변수명 `angel`은 `angle`의 오타다.
- 두 파일의 내용이 같으므로 하나로 정리하는 것이 좋다.

```cpp
#include <ESP32Servo.h>

#define POT_PIN 39
#define SERVO_PIN 14

Servo myServo;

void setup() {
    Serial.begin(115200);
    myServo.attach(SERVO_PIN);                 // 추가: 서보 핀 연결 (50Hz PWM 설정)
}

void loop() {
    int value = analogRead(POT_PIN);           // 0 ~ 4095
    int angle = map(value, 0, 4095, 0, 180);   // 0 ~ 180도
    myServo.write(angle);                      // 변경: analogWrite → write
    Serial.println(angle);
    delay(20);                                 // 서보 신호 주기(20ms)에 맞춤
}
```

**`button.txt`**
- `if (...)( ... ); else ( ... );` 처럼 중괄호 대신 소괄호를 썼다. 식 하나짜리 문장으로 해석돼서 동작은 하지만, 문장이 두 개 이상이 되면 바로 오류가 나므로 `{ }`로 쓰는 것이 맞다.

## 9. 참고 사항
- 여러 전원을 함께 쓸 때(예: 서보를 외부 전원으로 구동) 반드시 **Common GND**를 유지한다.
- 서보는 순간 전류가 커서 보드의 3.3V 핀에서 직접 전원을 받으면 보드가 리셋될 수 있다.
- ESP32 GPIO는 3.3V 로직이므로 5V 신호를 직접 입력하면 안 된다.
