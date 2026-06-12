#include <WiFi.h>
#include <PubSubClient.h>

const char* WIFI_SSID     = "EdNet";
const char* WIFI_PASSWORD = "Huawei@123";

const char* MQTT_SERVER    = "157.173.101.159";
const uint16_t MQTT_PORT   = 1883;
const char* MQTT_TOPIC     = "vision/team213_ange/movement";
const char* MQTT_CLIENT_ID = "esp32_team213_ange";

#define SERVO_PIN  18
#define LEDC_CHAN  0
#define LEDC_FREQ  50
#define LEDC_RES   16

const int SERVO_MIN    = 0;
const int SERVO_MAX    = 180;
const int SERVO_CENTER = 90;
const int TRACK_STEP   = 2;
const int SEARCH_STEP  = 2;

const unsigned long TRACK_INTERVAL  = 55;
const unsigned long SEARCH_INTERVAL = 70;
const unsigned long CMD_TIMEOUT     = 800;

uint32_t angleToDuty(int angle) {
  angle = constrain(angle, SERVO_MIN, SERVO_MAX);

  long pulseUs = map(angle, 0, 180, 500, 2400);

  return (uint32_t)((pulseUs * 65535UL) / 20000UL);
}

void servoBegin() {

#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3,0,0)

  ledcAttach(SERVO_PIN, LEDC_FREQ, LEDC_RES);

#else

  ledcSetup(LEDC_CHAN, LEDC_FREQ, LEDC_RES);
  ledcAttachPin(SERVO_PIN, LEDC_CHAN);

#endif
}

void servoWrite(int angle) {

  uint32_t duty = angleToDuty(angle);

#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3,0,0)

  ledcWrite(SERVO_PIN, duty);

#else

  ledcWrite(LEDC_CHAN, duty);

#endif
}

enum Cmd {
  IDLE,
  CMD_LEFT,
  CMD_RIGHT,
  CMD_CENTER,
  CMD_SEARCH
};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

Cmd currentCmd = CMD_SEARCH;

int servoAngle = SERVO_CENTER;
int sweepDir   = 1;

unsigned long lastMoveAt   = 0;
unsigned long lastReconnAt = 0;
unsigned long lastCmdAt    = 0;

void mqttCallback(char* topic, byte* payload, unsigned int length) {

  String msg = "";

  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.print("[MQTT] ");
  Serial.println(msg.substring(0, 80));

  if (msg.indexOf("MOVE_LEFT") >= 0) {
    currentCmd = CMD_LEFT;
  }
  else if (msg.indexOf("MOVE_RIGHT") >= 0) {
    currentCmd = CMD_RIGHT;
  }
  else if (msg.indexOf("CENTERED") >= 0) {
    currentCmd = CMD_CENTER;
  }
  else if (msg.indexOf("NO_FACE") >= 0) {
    currentCmd = CMD_SEARCH;
  }
  else {
    currentCmd = IDLE;
  }

  lastCmdAt = millis();
}

void connectWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.print("[WiFi] Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long t = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected!");
  } else {
    Serial.println("\n[WiFi] FAILED");
  }
}

bool connectMqtt() {

  if (mqttClient.connected()) {
    return true;
  }

  if (millis() - lastReconnAt < 5000) {
    return false;
  }

  lastReconnAt = millis();

  Serial.print("[MQTT] Connecting...");

  if (mqttClient.connect(MQTT_CLIENT_ID)) {

    Serial.println("OK");

    mqttClient.subscribe(MQTT_TOPIC);

    Serial.print("[MQTT] Subscribed: ");
    Serial.println(MQTT_TOPIC);

    return true;
  }

  Serial.print("FAILED rc=");
  Serial.println(mqttClient.state());

  return false;
}

void handleServo() {

  unsigned long now = millis();

  if ((now - lastCmdAt) > CMD_TIMEOUT) {
    currentCmd = CMD_SEARCH;
  }

  if (currentCmd == CMD_CENTER) {

    servoAngle = SERVO_CENTER;

    servoWrite(servoAngle);

    currentCmd = IDLE;

    Serial.println("[SERVO] Centered");

    return;
  }

  if (currentCmd == CMD_SEARCH) {

    if (now - lastMoveAt < SEARCH_INTERVAL) {
      return;
    }

    lastMoveAt = now;

    servoAngle = constrain(
      servoAngle + sweepDir * SEARCH_STEP,
      SERVO_MIN,
      SERVO_MAX
    );

    servoWrite(servoAngle);

    if (servoAngle >= SERVO_MAX) {
      sweepDir = -1;
    }

    if (servoAngle <= SERVO_MIN) {
      sweepDir = 1;
    }

    return;
  }

  if (now - lastMoveAt < TRACK_INTERVAL) {
    return;
  }

  lastMoveAt = now;

  if (currentCmd == CMD_LEFT) {

    servoAngle = constrain(
      servoAngle - TRACK_STEP,
      SERVO_MIN,
      SERVO_MAX
    );

    servoWrite(servoAngle);

    Serial.print("[SERVO] Left ");
    Serial.println(servoAngle);
  }

  else if (currentCmd == CMD_RIGHT) {

    servoAngle = constrain(
      servoAngle + TRACK_STEP,
      SERVO_MIN,
      SERVO_MAX
    );

    servoWrite(servoAngle);

    Serial.print("[SERVO] Right ");
    Serial.println(servoAngle);
  }
}

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println("\n[SYS] BENAX ESP32 — team213_ange");

  servoBegin();

  servoWrite(SERVO_CENTER);

  Serial.println("[SERVO] GPIO 18, centered at 90");

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

  mqttClient.setCallback(mqttCallback);

  connectWiFi();

  lastCmdAt = millis();
}

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (!mqttClient.connected()) {
    connectMqtt();
  }

  mqttClient.loop();

  handleServo();

  delay(1);
}