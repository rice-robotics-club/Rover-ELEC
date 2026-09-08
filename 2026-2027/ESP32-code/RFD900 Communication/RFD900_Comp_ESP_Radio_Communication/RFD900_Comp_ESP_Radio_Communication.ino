// ESP32 <-> RFD900 Serial Bridge

#define RFD_RX 16   // ESP32 receives from RFD900 TX
#define RFD_TX 17   // ESP32 transmits to RFD900 RX

HardwareSerial RFDSerial(2);

void setup() {
  // USB serial connection to laptop
  Serial.begin(115200);

  // UART connection to RFD900
  RFDSerial.begin(
    57600,
    SERIAL_8N1,
    RFD_RX,
    RFD_TX
  );

  delay(1000);

  Serial.println();
  Serial.println("ESP32 RFD900 Bridge Ready");
}

void loop()
  // Laptop -> RFD900
  while (Serial.available()) {
    char c = Serial.read();
    RFDSerial.write(c);
  }

  // RFD900 -> Laptop
  while (RFDSerial.available()) {
    char c = RFDSerial.read();
    Serial.write(c);
  }
}