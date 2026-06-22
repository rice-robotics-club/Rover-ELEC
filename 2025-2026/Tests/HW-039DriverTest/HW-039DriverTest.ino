void setup() {
  // put your setup code here, to run once:
  pinMode(10, OUTPUT); // PWM Right
  pinMode(11, OUTPUT); // Left PWM

  pinMode(6, OUTPUT); // Enable LEFT EN
  pinMode(7, OUTPUT); // Enable RIght EN

  Serial.begin(9600);

  digitalWrite(6, HIGH);
  digitalWrite(7, HIGH);
  analogWrite(10, 0);
  analogWrite(11, 128);
  
}

void loop() {
  // put your main code here, to run repeatedly:

  delay(10);

  

  Serial.println("RUnn");
} 
