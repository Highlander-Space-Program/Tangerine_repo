// #include <SPI.h>
// #include <Ethernet.h>
// #include <PubSubClient.h>
// #include <Servo.h>

// // Pins
// #define PIN_SCK 14
// #define PIN_MOSI 15
// #define PIN_MISO 8
// #define W5500_CS 10
// #define W5500_INT 9   
// #define CAN_CS 19
// #define CAN_STANDBY 16
// #define CAN_RESET 18
// #define SERVO_PIN 24
// #define BREAKWIRE_SENSE 20
// #define BREAKWIRE_DRIVE 21
// #define FIRED_ANGLE 45 // Replace with whatever actually is open
// #define DEBOUNCE_MS 20

// bool fired = false;
// uint32_t high_since = 0;

// // Network
// byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
// IPAddress mqttBroker(192, 168, 0, 5); 
// const uint16_t mqttPort = 1883;
// const char* mqttClientId = "rp2040-servo-01";
// const char* subTopic     = "servo/angle"; 

// EthernetClient ethClient;
// PubSubClient mqtt(ethClient);
// Servo servo;

// void mqttCallback(char* topic, byte* payload, unsigned int len) {
//   if (strcmp(topic, subTopic) != 0) {
//     return;
//   }

//   char buf[8] = {0};
//   if (len >= sizeof(buf)) {
//     len = sizeof(buf) - 1;
//   }
//   memcpy(buf, payload, len);

//   int angle = constrain(atoi(buf), 0, 180);
//   servo.write(angle);
//   Serial.printf("Servo -> %d deg\n", angle);
// }

// void reconnectMqtt() {
//   while (!mqtt.connected()) {
//     Serial.print("MQTT connect... ");

//     if (mqtt.connect(mqttClientId)) {
//       Serial.println("ok");
//       mqtt.subscribe(subTopic);
//     } else {
//       Serial.printf("rc=%d, retry in 2s\n", mqtt.state());
//       delay(2000);
//     }
//   }
// }

// void checkBreakwire() {
//   if (fired) {
//     return;
//   }

//   if (digitalRead(BREAKWIRE_SENSE) == HIGH) {
//     if (high_since == 0) {
//       high_since = millis();
//     } else if (millis() - high_since > DEBOUNCE_MS) {
//       fired = true;
//       servo.write(FIRED_ANGLE);
//     } else {
//       high_since = 0;
//     }
//   }
// }

// void setup() {
//   Serial.begin(115200);

//   pinMode(CAN_RESET, OUTPUT);
//   digitalWrite(CAN_RESET, LOW);

//   pinMode(CAN_CS, OUTPUT);
//   digitalWrite(CAN_CS, HIGH);

//   pinMode(CAN_STANDBY, OUTPUT);
//   digitalWrite(CAN_STANDBY, HIGH);

//   pinMode(BREAKWIRE_DRIVE, OUTPUT);
//   digitalWrite(BREAKWIRE_DRIVE, LOW);

//   pinMode(BREAKWIRE_SENSE, INPUT_PULLUP);

//   SPI.setSCK(PIN_SCK);
//   SPI.setTX(PIN_MOSI);
//   SPI.setRX(PIN_MISO);
//   SPI.begin();

//   servo.attach(SERVO_PIN, 500, 2400);
//   servo.write(90);
  
//   Ethernet.init(W5500_CS);
//   Serial.print("DHCP... ");

//   IPAddress myIP(192, 168, 0, 50);
//   IPAddress myDNS(192, 168, 0, 1);
//   IPAddress myGateway(192, 168, 0, 1);
//   IPAddress mySubnet(255, 255, 255, 0);

//   Ethernet.init(W5500_CS);
//   Ethernet.begin(mac, myIP, myDNS, myGateway, mySubnet);
//   Serial.print("Static IP: ");
//   Serial.println(Ethernet.localIP());

//   mqtt.setServer(mqttBroker, mqttPort);
//   mqtt.setCallback(mqttCallback);
// }

// void loop() {
//   if (!mqtt.connected()) {
//     reconnectMqtt();
//   }

//   mqtt.loop();
//   Ethernet.maintain();
//   checkBreakwire();
// }