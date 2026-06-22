#include <SoftwareSerial.h>

SoftwareSerial soft_serial(7, 8);

#define VERBOSE_COMMANDS
#define COMMAND_SERIAL soft_serial
#define INPUT_SERIAL soft_serial

#include "serial_commands.h"

CommandHandler cHandler;

void ping(void ** arg_stack)
{
	int value = *((int*) (arg_stack[0]));
	soft_serial.print("Pinged: ");
	soft_serial.println(value);
}

void setup()
{
	Serial.begin(9600);
	soft_serial.begin(9600);

	CommandArgType ping_cargs[MAX_SCOMMAND_ARGUMENTS] = {INT_ARG};
	cHandler.addCommand('P', ping, ping_cargs);

	delay(1000);
	soft_serial.println("Printing");
}

void loop()
{
	cHandler.readSerial();

}