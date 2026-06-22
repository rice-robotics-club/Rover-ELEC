#include <SerialWombat.h>


//for ESP32 RE reading:
#define DT GPIO_NUM_33
#define CLK GPIO_NUM_32
float RPM;
int currentStateCLK;
int lastStateCLK;
float counter;
float lastCounter;
float lastPrint;
int clicks_per_rotation = 5264;
//------------------------

SerialWombatChip sw;    //Declare a Serial Wombat chip

int newval;
int oldval;

//we're using a quadrature encoder with built-in pullup resistors
SerialWombatQuadEnc qeWithPullUps(sw);

//PWM at frequency of 31250 Hz.  Can change this probably if needed but we haven't figured out how yet
SerialWombatPWM pwm(sw);



int PWM = 2;
int QE1 = 6;
int QE2 = 7;
int REN = 1;
int LEN = 4;
//int checkStall = 5;

void setup() {
  // put your setup code here, to run once:

  Serial.begin(115200);
  delay(2000);
  Serial.print("Hello");
  Wire.begin(21, 22); // Initialize I2C with custom pin
  Wire.setTimeOut(5000);
	delay(500);
	uint8_t i2cAddress = 0x62; //sw.find();
    sw.begin(Wire,i2cAddress);  //Initialize the Serial Wombat library to use the primary I2C port
	//sw.registerErrorHandler(SerialWombatSerialErrorHandlerBrief); //Register an error handler that will print communication errors to Serial

  Serial.println("here");

  sw.queryVersion();

  Serial.println();
  Serial.print("Version "); Serial.println((char*)sw.fwVersion);
  Serial.println("SW Found.");

  //begin(pin, starting duty cycle, inverted true/false)
  pwm.begin(PWM, 0, false);
  qeWithPullUps.begin(QE1, QE2, 0.01, true, QE_ONHIGH_INT);  // Initialize a QE on pins 2 and 3


  //digital enable pins
  sw.pinMode(REN, OUTPUT);
  sw.pinMode(LEN, OUTPUT);
  //sw.pinMode(checkStall, INPUT);

  //enable motor
  sw.digitalWrite(REN, HIGH);
  sw.digitalWrite(LEN, HIGH);
  Serial.print("enabled motor");
  //spin motor
  pwm.writeDutyCycle(60000);
  Serial.print("Started motor");

  //for using esp32:
  pinMode(CLK, INPUT);
  pinMode(DT, INPUT);

  // Read the initial state of CLK
  lastStateCLK = gpio_get_level(CLK);


  //analogWrite(33, 100);
  oldval = 0;
}

void loop() {
  
  //return;

  newval = qeWithPullUps.read();
  if(newval!=oldval){
    Serial.println(newval);
    oldval = newval;
  }

//  if(sw.digitalRead(checkStall) == HIGH){
//    Serial.println("Stalling");
 // }
  delay(50);





  //---------------------------------------------------------- Reading REs with ESP32:
  /*

   // Read the current state of CLK
  currentStateCLK = gpio_get_level(CLK);

  // If last and current state of CLK are different, then pulse occurred
  // React to only 1 state change to avoid double count
  if (currentStateCLK != lastStateCLK) {


    // If the DT state is different than the CLK state then
    // the encoder is rotating CCW so decrement
    if (gpio_get_level(DT) != currentStateCLK) {
      counter--;
    } else {
      // Encoder is rotating CW so increment
      counter++;
    }
    
  }
  
  if ((millis() - lastPrint) > 1000) {
        //RPM = [# of clicks]/[change in time, ms] * 1 rotation/[clicks per rotation] * 1000ms/1s * 60s/1min
        //currently, this is actually rotation per second
        RPM = 1000 * (counter - lastCounter) / (clicks_per_rotation * (millis() - lastPrint));
        Serial.print(" | Counter: ");
        Serial.print(counter);
        Serial.print(" | RPS: ");
        Serial.println(RPM);
        lastPrint = millis();
        lastCounter = counter;
      }

  // Remember last CLK state
  lastStateCLK = currentStateCLK;

  // Put in a slight delay to help debounce the reading

  //------------------------------------------------------------------------------------------------------------------------------
*/
}