#ifndef FAKE_ARDUINIO_DEFINES
#ifndef OUTPUT


// Arduino defines so compiler doesnt yell at me

#define OUTPUT 1 // Not sure this is the actual value
#define HIGH 1
#define LOW 0

void pinMode(int pin, int mode);
void digitalWrite(int pin, int state);
void delay(int ms);

#endif
#endif

