#include <Wire.h>
#include <SerialWombat.h>

//make all of the move functions here to be called by main program


//-------------starting definitions:
  //for rotary encoder stall tracking:
  //position in this array corresponds to number of motor
  int CLKs[] = {0, 0, 0, 0, 0};

  //sw pins setup:
  int RIS = 0;
  int REN = 1;
  int RPWM = 2;
  int LPWM = 3;
  int LEN = 4;
  int LIS = 5;
  int CHA = 6;
  int CHB = 7;

  //initialize sw chips
  SerialWombatChip motor0;
  SerialWombatChip motor1;
  SerialWombatChip motor2;
  SerialWombatChip motor3;
  SerialWombatChip motor4;

  //initialize REs
  SerialWombatQuadEnc RE0(motor0);
  SerialWombatQuadEnc RE1(motor1);
  SerialWombatQuadEnc RE2(motor2);
  SerialWombatQuadEnc RE3(motor3);
  SerialWombatQuadEnc RE4(motor4);

  //initialize PWMs
  SerialWombatPWM RPWM0(motor0);
  SerialWombatPWM LPWM0(motor0);
  SerialWombatPWM RPWM1(motor1);
  SerialWombatPWM LPWM1(motor1);
  SerialWombatPWM RPWM2(motor2);
  SerialWombatPWM LPWM2(motor2);
  SerialWombatPWM RPWM3(motor3);
  SerialWombatPWM LPWM3(motor3);
  SerialWombatPWM RPWM4(motor4);
  SerialWombatPWM LPWM4(motor4);

  //create arrays of addresses for PWMs, REs, motors
  SerialWombatPWM* RPWMs[] = { &RPWM0, &RPWM1, &RPWM2, &RPWM3, &RPWM4 };
  SerialWombatPWM* LPWMs[] = { &LPWM0, &LPWM1, &LPWM2, &LPWM3, &LPWM4 };
  SerialWombatQuadEnc* REs[] = { &RE0, &RE1, &RE2, &RE3, &RE4 };
  SerialWombatChip* motors[] = { &motor0, &motor1, &motor2, &motor3, &motor4 };

  //create array of i2c addresses
  uint8_t i2cAddresses[] = {0x60, 0x61, 0x62, 0x63, 0x64};


//-------------setup: must be called at the beginning of main program in order for the rest of the commands to work 
  // void initializeSW(){
  //   //initialize sw chip and pins for each motor (motor numbers printed on PCB)

  //   //start i2C
  //   Wire.begin(21, 22); // Initialize I2C with custom pin
  //   Wire.setTimeOut(5000);
  //   delay(500);

  //   //initialize & set up everything for each motor
  //   for(int i = 0; i<5; i++){
  //     //begin SW chips & communication with them
  //     motors[i]->begin(Wire, i2cAddresses[i]);

  //     //begin PWM pins
  //     RPWMs[i]->begin(RPWM, 0, false);
  //     LPWMs[i]->begin(LPWM, 0, false);

  //     //set up enable pins
  //     motors[i]->pinMode(LEN, OUTPUT);
  //     motors[i]->pinMode(REN, OUTPUT);

  //     //set up current alarms
  //     motors[i]->pinMode(LIS, INPUT);
  //     motors[i]->pinMode(RIS, INPUT);
    
  //     //initialize rotary encoders
  //     REs[i]->begin(CHA, CHB, 0.01, true, QE_ONHIGH_INT);

  //     //set motors to not be enabled
  //     motors[i]->digitalWrite(LEN, LOW);
  //     motors[i]->digitalWrite(REN, LOW);

  //   }
  // }

  void initializeSW(){
  Wire.begin(21, 22);
  Wire.setTimeOut(1000); // Lower timeout so one ghost doesn't hang the boot for 5 seconds
  delay(500);

  for(int i = 0; i < 5; i++){
    Serial.print("Initializing Motor "); Serial.print(i);
    Serial.print(" at 0x"); Serial.println(i2cAddresses[i], HEX);

    // Try to start the chip. If it returns false, skip it.
    if (motors[i]->begin(Wire, i2cAddresses[i])) {
      
      // Successfully found chip, now configure pins
      RPWMs[i]->begin(RPWM, 0, false);
      LPWMs[i]->begin(LPWM, 0, false);
      motors[i]->pinMode(LEN, OUTPUT);
      motors[i]->pinMode(REN, OUTPUT);
      
      // Set to safe state
      motors[i]->digitalWrite(LEN, LOW);
      motors[i]->digitalWrite(REN, LOW);
      
      Serial.println(" -> Success");
    } else {
      Serial.println(" -> FAILED (Ghost or Disconnected)");
    }
  }
}

//------------subcommands: will not be called in main program, but used to build commands

  //turns any motor right
  void r(int motornum, int dutyCycle){
    //make sure lpwm is set to 0
    LPWMs[motornum]->writeDutyCycle(0);
    
    //make sure motor is enabled
    motors[motornum]->digitalWrite(LEN, HIGH);
    motors[motornum]->digitalWrite(REN, HIGH);

    //move motor
    RPWMs[motornum]->writeDutyCycle(dutyCycle);
  }

  //turns any motor left
  void l(int motornum, int dutyCycle){
    //make sure RPWM is set to 0
    RPWMs[motornum]->writeDutyCycle(0);

    //make sure motor is enabled
    motors[motornum]->digitalWrite(LEN, HIGH);
    motors[motornum]->digitalWrite(REN, HIGH);

    //move motor
    LPWMs[motornum]->writeDutyCycle(dutyCycle);
  }

  //stops motor
  void s(int motornum){
    //unenable motor
    motors[motornum]->digitalWrite(LEN, LOW);
    motors[motornum]->digitalWrite(REN, LOW);

    //set pwms to 0
    RPWMs[motornum]->writeDutyCycle(0);
    LPWMs[motornum]->writeDutyCycle(0);
  }


//------------commands: will be called in main program 

  //stops all drivetrain motors
  void stopAll(){
    s(1);
    s(2);
    s(3);
    s(4);
  }

  //makes rover drive forward. Can swap "r" with "l" if rover goes wrong direction
  void df(int dutyCycle){
    //stopAll()
    r(1, dutyCycle);
    r(2, dutyCycle);
    l(3, dutyCycle);
    l(4, dutyCycle);
  }

  //makes rover drive backward. Can swap "r" with "l" if rover goes wrong direction
  void db(int dutyCycle){
    //stopAll()
    l(1, dutyCycle);
    l(2, dutyCycle);
    r(3, dutyCycle);
    r(4, dutyCycle);
  }

  //makes rover turn right. Can swap "r" with "l" if rover goes wrong direction
  void tl(int dutyCycle){
    //stopAll()
    l(1, dutyCycle);
    l(2, dutyCycle);
    l(3, dutyCycle);
    l(4, dutyCycle);
  }

  //makes rover turn left. Can swap "r" with "l" if rover goes wrong direction
  void tr(int dutyCycle){
    //stopAll()
    r(1, dutyCycle);
    r(2, dutyCycle);
    r(3, dutyCycle);
    r(4, dutyCycle);
  }



//------------checkStall and subcommands: a class all of its own 
  // bool checkStall(bool newcommand, int currentcommand){
  //   bool stall;
  //   if(newcommand){
  //     for(int i = 0; i<5; i++){
  //       CLKs[i] = REs[i]->read();
  //     }
  //     return false;
  //   }
  //   else if(currentcommand == 0){
  //       return false;
  //     }
  //   else if((currentcommand >0)&&(currentcommand<4)){
  //     bool stall1 = getStall(1);
  //     bool stall2 = getStall(2);
  //     bool stall3 = getStall(3);
  //     bool stall4 = getStall(4);
  //     if(((stall1 == true)||(stall2==true))||((stall3==true)||(stall4==true))){
  //       return true;
  //     }
  //     else{
  //       return false;
  //     }
  //   }
  //   else if((currentcommand >3)&&(currentcommand<8)){
  //     stall = getStall(0);
  //     return stall;
  //   }
  //   else if((currentcommand >7)&&(currentcommand<11)){
  //     stall = getStall(1);
  //     return stall;
  //   }
  //   else if((currentcommand >10)&&(currentcommand<14)){
  //     stall = getStall(2);
  //     return stall;
  //   }
  //   else if((currentcommand >13)&&(currentcommand<17)){
  //     stall = getStall(3);
  //     return stall;
  //   }
  //   else if((currentcommand >16)&&(currentcommand<20)){
  //     stall = getStall(4);
  //     return stall;
  //   }
  //   else{
  //     return true;
  //   }
  // }

  // bool getStall(int motornum){

  //   int newCLK = REs[motornum]->read();
        
  //   int oldCLK = CLKs[motornum];

  //   if((oldCLK-newCLK != 0)){
  //     CLKs[motornum] = newCLK;
  //     return false;
  //   }
  //   else{
  //     return true;
  //   }
  //}






