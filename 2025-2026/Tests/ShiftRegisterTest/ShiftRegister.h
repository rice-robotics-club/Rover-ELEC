#ifndef SHIFT_REGISTER_H
#define SHIFT_REGISTER_H


#include "FakeArduinoDefines.h"

void soft_pin_setup(int pin, int mod)
{
  if (pin != -1)
    pinMode(pin, mod);
}

class ShiftRegister
{
  public: 
    int OE_PIN    = -1;
    int SER_PIN   = -1;
    int SRCLK_PIN = -1;
    int RCLK_PIN  = -1;
    int SRCLR_PIN = -1;

    int pins = 8;
	int clock_delay = 1; // Delay (micro seconds) before flipping bits in the Shift Register

    ShiftRegister(int pins)
	{
		this->pins = pins;
	}

	void setup()
	{
		soft_pin_setup(OE_PIN, OUTPUT);
		soft_pin_setup(SER_PIN, OUTPUT);
		soft_pin_setup(SRCLK_PIN, OUTPUT);
		soft_pin_setup(RCLK_PIN, OUTPUT);
		soft_pin_setup(SRCLR_PIN, OUTPUT);
	}

	void setPins(char *pin_value)
	{
		digitalWrite(SRCLR_PIN, HIGH);

		for (int i = pins-1; i >= 0; i--)
		{
			digitalWrite(SER_PIN, pin_value[i]);	

			delayMicroseconds(clock_delay);

			digitalWrite(SRCLK_PIN, HIGH);
			delayMicroseconds(clock_delay);
			digitalWrite(SRCLK_PIN, LOW);
			delayMicroseconds(clock_delay);
		}

		// Moves the values out the shift register
		digitalWrite(RCLK_PIN, HIGH);
		delayMicroseconds(clock_delay);
		digitalWrite(RCLK_PIN, LOW);
		delayMicroseconds(clock_delay);

		digitalWrite(OE_PIN, LOW);

	}


};


#endif
