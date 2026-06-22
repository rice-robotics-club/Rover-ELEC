#ifndef SerialCommands
#define SerialCommands

#include <stdlib.h>

#ifdef VERBOSE_COMMANDS
#ifndef COMMAND_SERIAL
  #error "COMMAND_SERIAL must be defined to use VERBOSE_COMMANDS in serial_commands.h"
#endif
#define EXPAND(x) x
#define sc_print(...) EXPAND(COMMAND_SERIAL).print(__VA_ARGS__)
#define sc_println(...) EXPAND(COMMAND_SERIAL).println(__VA_ARGS__)
#else
#define sc_print(...) 
#define sc_println(...) 
#endif

#define MAX_COMMAND_LENGTH 50
#define MAX_COMMANDS 10
const int MAX_SCOMMAND_ARGUMENTS = 3;

#define ARGUMENT_SPACER ' '

typedef void (*Command) (void **args);

enum CommandArgType {
  NONE_ARG = 0, // unsigned types are none
  INT_ARG,
  FLOAT_ARG,
  CHAR_ARG
};

class CommandHandler
{
  public:
    int command_count = 0;
    int input_cursor = 0;
    char input_string[MAX_COMMAND_LENGTH];
    char command_ids[MAX_COMMANDS];
    Command commands[MAX_COMMANDS];
    CommandArgType command_arguments[MAX_COMMANDS][MAX_SCOMMAND_ARGUMENTS];

    // commands ids are single characters
    int addCommand(char command_id, Command command, CommandArgType types[MAX_SCOMMAND_ARGUMENTS]) 
    {
      command_ids[command_count] = command_id;
      commands[command_count] = command;
      
      // // Copying argument types
      for (char i = 0; i < MAX_SCOMMAND_ARGUMENTS; i++)
        command_arguments[command_count][i] = types[i];

      command_count++;
      sc_print(F("Added command: "));
      sc_println(command_id);
      return 0;
    }

    int runCommand(char* command_str)
    {
      char command_id = command_str[0];

      int command_index = -1;
      for (int i = 0; i < command_count; i++)
      {
        if (command_id == command_ids[i])
        {
          command_index = i;
          break;
        }
      } 

      // Checking if a command was found
      if (command_index == -1)
      {
        sc_print(F("ERROR: Invalud command id: \""));
        sc_print(command_id);
        sc_println(F("\""));
        return 0;
      }

      // Gathering arguments
      char *command_ptr = command_str+1;
      void **argument_stack = (void**) malloc(sizeof(void*)*4);
      int given_args = 0;
      int errored = 0;
      for (int i = 0; i < MAX_SCOMMAND_ARGUMENTS; i++)
      {
        const CommandArgType arg_type = command_arguments[command_index][i];
        char *end_ptr;
        void *arg;

        switch (arg_type) 
        {
          case INT_ARG:
            arg = new int((int) strtol(command_ptr, &end_ptr, 10));
            break;
          case FLOAT_ARG:
            arg = new float((float)strtod(command_ptr, &end_ptr));
            break;
          case NONE_ARG:
            break;
        }

        // Either invalid argument or no arguments left 
        if (end_ptr == command_ptr)
          break;

        command_ptr = end_ptr;

        given_args++;
        argument_stack[i] = arg;
      }

      
      if (!errored)
      {
        commands[command_index](argument_stack);
      } else {
        
      }
      
      
      // Freeing the stack
      for (int i = 0; i < given_args; i++)
      {
        const CommandArgType arg_type = command_arguments[command_index][i];
        switch (arg_type) 
        {
          case INT_ARG:
            delete (int*) argument_stack[i];
            break;
          case FLOAT_ARG:
            delete (float*) argument_stack[i];
            break;
          case NONE_ARG:
            break;
        }
      }

      free(argument_stack);

      return 1;
    }

  #ifdef INPUT_SERIAL
  int readSerial()
  {
    while (INPUT_SERIAL.available() > 0) {
      // read the incoming byte:
      int incomingByte = INPUT_SERIAL.read();
      input_string[input_cursor] = (char) incomingByte;
      input_cursor++;

      if (incomingByte == '&') { // end character

        // Replacing the end character with a string end to not mess things up
        input_string[input_cursor-1] = '\0'; 

        // print command 
        sc_print("I received command: \"");
        sc_print(input_string);
        sc_println("\"");

        runCommand(input_string);
    

        // We dont technically have to clear the string as it will get overwritten
        input_cursor = 0;
      } else if (incomingByte == '*') {
        input_cursor = 0;
      }
    }
    return 0;
  }
  #else
  int readSerial()
  {
    sc_println(F("INPUT_SERIAL must be defined to use readSerial"));
    return 0;
  }
  #endif
};
#endif