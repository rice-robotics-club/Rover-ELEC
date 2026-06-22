#include <stdio.h>
#include <math.h>

// Example pins
const int motor_puls[] = {0x1u, 0x2u, 0x4u, 0x8u};
const int motor_dirs[] = {0x16u, 0x32u, 0x64u, 0x128u};
float motor_speeds[] = {100000, 100000, 100000, 100000}; // steps per s
int motor_steps[] = {100, 200, 300, 400};
const int motors = 4;

#define PACKET_SIZE 1000
// Micro seconds 10^-6 seconds
#define MICROSECOND 0.000001

#define PULSE_DELAY 500 // In microseconds 
#define STEP_DELAY (PULSE_DELAY * 2)  // In microseconds 


#define MIN_MOTOR_SPEED (1 / (MICROSECOND * PACKET_SIZE / STEP_DELAY))
#define MAX_MOTOR_SPEED (1 / (STEP_DELAY * MICROSECOND))

int reorder_array[PACKET_SIZE];

// Only done once 
void generate_reorder_array()
{
    // fill with -1s
    for (int i = 0; i < reorder_array; i++)
    { reorder_array[i] = -1; }

    for (int i = 0; i < PACKET_SIZE; i++) {
        int index = (i * PACKET_SIZE/2) % PACKET_SIZE;  // Even redistribution

        // Find the next available spot (linear probing)
        while (reorder_array[index] != -1) {
            index = (index + 1) % PACKET_SIZE;
        }

        reorder_array[index] = i;
    }
}

void fill_packet(char *packet)
{
    // Fill with 0s
    for (int i = 0; i < PACKET_SIZE; i++)
    { packet[i] = 0; }


    for (int motor = 0; motor < motors; motor++)
    {
        // Should round down
        const int expected_movements = motor_speeds[motor] * MICROSECOND * PACKET_SIZE / STEP_DELAY;
        printf("Expected movements %d", expected_movements);
        
        for (int i = 0; i < expected_movements; i++)
        {
            packet[reorder_array[i]] |= 0x1u << motor;
        }

        if (expected_movements > motor_steps[motor])
        {
            int iters = 0;
            char *packet_head = &(packet[PACKET_SIZE-1]);
            while (motor_steps[motor] > 0 && iters < PACKET_SIZE)
            {
                motor_steps[motor] -= (*packet_head >> motor) & 0x1u;
                packet_head--;
                iters++;
            }
        } else {
            motor_steps[motor] -= expected_movements;
        }
    }
}

int main()
{
    printf("MIN_MOTOR_SPEED %d, MAX_MOTOR_SPEED %d", MIN_MOTOR_SPEED, MAX_MOTOR_SPEED);
    generate_reorder_array();
    char packet[PACKET_SIZE];
    fill_packet(packet);

    return 0;
}