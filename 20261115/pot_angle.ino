#include <ESP32Servo.h>

#define POT_PIN 39
#define SERVO_PIN 14

Servo myServo;

void setup() {
    Serial.begin(115200);
}

void loop() {
    int value = analogRead(POT_PIN);
    Serial.println(value);
    //4095 -> 180
    int angel = map(value, 0, 4095, 0, 180);
    analogWrite(SERVO_PIN, angel);
    delay(50);
}
