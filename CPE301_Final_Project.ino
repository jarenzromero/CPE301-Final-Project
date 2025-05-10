#include <DHT.h>
#include <LiquidCrystal.h>
#include <RTClib.h>
#include <Stepper.h>

#define DHTPIN 6
#define DHTTYPE DHT11
#define TEMP_THRESHOLD 22
#define WATER_THRESHOLD 300

#define RED_LED_BIT PB5 // Pin 13
#define BLUE_LED_BIT PB2 // Pin 10
#define GREEN_LED_BIT PB1 // Pin 9
#define YELLOW_LED_BIT PB0 // Pin 8
#define FAN_BIT PH4 // Pin 7

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);
RTC_DS1307 rtc;
DHT dht(DHTPIN, DHTTYPE);
Stepper stepper(2048, A1, A3, A2, A4);

enum SystemState { DISABLED, IDLE, RUNNING, ERROR };
SystemState state = DISABLED, nextState = DISABLED;

bool lastVentButtonState = false;
bool ventMoved = false;
unsigned long lastLog = 0;

void adc_init() {
ADMUX = (1 << REFS0);
ADCSRA = (1 << ADEN) | (1 << ADPS2);
}

int adc_read(uint8_t ch) {
ADMUX = (ADMUX & 0xF0) | (ch & 0x0F);
ADCSRA |= (1 << ADSC);
while (ADCSRA & (1 << ADSC));
return ADC;
}

void setup() {
DDRB |= (1 << RED_LED_BIT) | (1 << BLUE_LED_BIT) | (1 << GREEN_LED_BIT) | (1 << YELLOW_LED_BIT);
DDRH |= (1 << FAN_BIT);
DDRK &= ~(1 << 0);

lcd.begin(16, 2);
rtc.begin();
dht.begin();
stepper.setSpeed(10);
Serial.begin(9600);
adc_init();

attachInterrupt(digitalPinToInterrupt(18), startStopISR, RISING);
attachInterrupt(digitalPinToInterrupt(19), resetISR, RISING);

nextState = DISABLED;
}

void loop() {
if (state != nextState) {
logTransition(state, nextState);
state = nextState;
}

int waterLevel = adc_read(0);
float temp = dht.readTemperature();
float hum = dht.readHumidity();

switch (state) {
case DISABLED:
updateLEDs(0, 0, 0, 1);
PORTH &= ~(1 << FAN_BIT);
lcdStatus("System Off");
break;

go
Copy
Edit
case IDLE:
  displayTempHumidity(temp, hum);
  handleVent();
  if (millis() - lastLog >= 60000) {
    logTempHumidity(temp, hum);
    logWaterLevel(waterLevel);
    lastLog = millis();
  }
  if (waterLevel < WATER_THRESHOLD) { logWaterLowEvent(waterLevel); nextState = ERROR; break; }
  if (!isnan(temp) && temp > TEMP_THRESHOLD) nextState = RUNNING;
  updateLEDs(0, 0, 1, 0);
  PORTH &= ~(1 << FAN_BIT);
  break;

case RUNNING:
  displayTempHumidity(temp, hum);
  handleVent();
  if (millis() - lastLog >= 60000) {
    logTempHumidity(temp, hum);
    logWaterLevel(waterLevel);
    lastLog = millis();
  }
  if (waterLevel < WATER_THRESHOLD) { logWaterLowEvent(waterLevel); logFanOff(); nextState = ERROR; break; }
  if (!isnan(temp) && temp <= TEMP_THRESHOLD) {
    PORTH &= ~(1 << FAN_BIT);
    logFanOff();
    nextState = IDLE;
  } else {
    PORTH |= (1 << FAN_BIT);
    logFanOn();
  }
  updateLEDs(0, 1, 0, 0);
  break;

case ERROR:
  updateLEDs(1, 0, 0, 0);
  PORTH &= ~(1 << FAN_BIT);
  logFanOff();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("ERROR: Water Low");
  lcd.setCursor(0, 1); lcd.print("Level: "); lcd.print(waterLevel);
  handleVent();
  break;
}

if (ventMoved) {
logVentMove();
ventMoved = false;
}

delay(1000);
}

void displayTempHumidity(float t, float h) {
lcd.clear();
if (isnan(t) || isnan(h)) {
lcd.setCursor(0, 0); lcd.print("DHT Error");
} else {
lcd.setCursor(0, 0); lcd.print("Temp: "); lcd.print(t, 1); lcd.print(" C");
lcd.setCursor(0, 1); lcd.print("Hum: "); lcd.print(h, 1); lcd.print("%");
}
}

void handleVent() {
static unsigned long lastPress = 0;
bool pressed = !digitalRead(15);
if (pressed && !lastVentButtonState && millis() - lastPress > 300) {
noInterrupts(); stepper.step(512); interrupts();
delay(10); ventMoved = true; lastPress = millis();
}
lastVentButtonState = pressed;
}

void updateLEDs(bool r, bool b, bool g, bool y) {
if (r) PORTB |= (1 << RED_LED_BIT); else PORTB &= ~(1 << RED_LED_BIT);
if (b) PORTB |= (1 << BLUE_LED_BIT); else PORTB &= ~(1 << BLUE_LED_BIT);
if (g) PORTB |= (1 << GREEN_LED_BIT); else PORTB &= ~(1 << GREEN_LED_BIT);
if (y) PORTB |= (1 << YELLOW_LED_BIT); else PORTB &= ~(1 << YELLOW_LED_BIT);
}

void lcdStatus(const char* msg) {
lcd.clear(); lcd.setCursor(0, 0); lcd.print(msg);
}

void printTimestamp() {
DateTime now = rtc.now();
Serial.print(now.hour()); Serial.print(":");
Serial.print(now.minute()); Serial.print(":");
Serial.print(now.second());
}

const char* stateName(SystemState s) {
switch (s) {
case DISABLED: return "DISABLED";
case IDLE: return "IDLE";
case RUNNING: return "RUNNING";
case ERROR: return "ERROR";
default: return "UNKNOWN";
}
}

void logTransition(SystemState f, SystemState t) {
Serial.print("Transition: "); Serial.print(stateName(f));
Serial.print(" -> "); Serial.print(stateName(t));
Serial.print(" at "); printTimestamp(); Serial.println();
}

void logFanOn() { Serial.print("Fan ON at "); printTimestamp(); Serial.println(); }
void logFanOff() { Serial.print("Fan OFF at "); printTimestamp(); Serial.println(); }
void logVentMove() { Serial.print("Vent 90° at "); printTimestamp(); Serial.println(); }

void logTempHumidity(float t, float h) {
Serial.print("Temp: "); Serial.print(t, 1);
Serial.print(" C, Hum: "); Serial.print(h, 1);
Serial.print("% at "); printTimestamp(); Serial.println();
}

void logWaterLevel(int l) {
Serial.print("Water Level: "); Serial.print(l);
Serial.print(" at "); printTimestamp(); Serial.println();
}

void logWaterLowEvent(int l) {
Serial.print("Water LOW ("); Serial.print(l);
Serial.print(") at "); printTimestamp(); Serial.println();
}

void logResetPressed() {
Serial.print("Reset pressed at "); printTimestamp(); Serial.println();
}

void logSystemDisabled() {
Serial.print("System DISABLED at "); printTimestamp(); Serial.println();
}

void startStopISR() {
logSystemDisabled();
nextState = (state == DISABLED) ? IDLE : DISABLED;
}

void resetISR() {
int lvl = adc_read(0);
if (state == ERROR && lvl > WATER_THRESHOLD) {
logResetPressed();
nextState = IDLE;
}
}

