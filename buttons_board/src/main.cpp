#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>
#include <string.h>
#include <stdlib.h>

#include "HD44780.hpp"
#include "TimerOne.hpp"
#include "uart_buffer.hpp"

// i2c slave address
#define SLAVE_ADDRESS 0x08

// command received from master
#define CMD_START_GAME 0xA0

// difficulty values
#define EASY 110
#define MEDIUM 111
#define HARD 112

// number of rounds
#define NO_TRIES 10

// variables received via i2c
volatile uint8_t received_difficulty = 0;
volatile uint8_t data_ready = 0;
volatile uint8_t rx_index = 0;

// game scoring variables
uint8_t points = 0;
uint8_t point_per_difficulty[3] = {10, 15, 25};
uint8_t penalty_points = 5;

// timer and game state variables
unsigned long timer_period = 0;
volatile uint8_t current_led = 0xFF, last_led = 0xFF;
volatile uint8_t waiting_for_input = 0;
volatile uint8_t button_pressed = 0;

// led and button pins
int leds[3] = {PORTB0, PORTB1, PORTB2};
int buttons[3] = {PORTB3, PORTB4, PORTB5};

// score to be sent to master
volatile uint8_t tx_score = 0;

// initialize i2c as slave
void i2c_slave_init(void)
{
    TWAR = (SLAVE_ADDRESS << 1);
    TWCR = (1 << TWEN) | (1 << TWEA) | (1 << TWIE);
    PORTC |= (1 << PC4) | (1 << PC5);
}

// i2c interrupt routine
ISR(TWI_vect)
{
    uint8_t status = TWSR & 0xF8;
    static uint8_t rx_cmd = 0;

    switch (status)
    {
        // slave write address received
        case 0x60:
        case 0x68:
            rx_index = 0;
            break;

        // data received from master
        case 0x80:
            if (rx_index == 0)
            {
                rx_cmd = TWDR;
            }
            else if (rx_index == 1)
            {
                if (rx_cmd == CMD_START_GAME)
                {
                    received_difficulty = TWDR;
                    data_ready = 1;
                }
            }
            rx_index++;
            break;

        // stop or repeated start received
        case 0xA0:
            rx_index = 0;
            break;

        // slave transmit mode
        case 0xA8:
        case 0xB8:
            TWDR = tx_score;
            break;

        case 0xC0:
        case 0xC8:
            break;
    }

    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA) | (1 << TWIE);
}

// timer interrupt used for reaction timeout
void timerISR(void)
{
    if (current_led != 0xFF)
        PORTB &= ~(1 << leds[current_led]);

    last_led = current_led;
    while (current_led == last_led)
        current_led = rand() % 3;

    PORTB |= (1 << leds[current_led]);

    waiting_for_input = 0;
    button_pressed = 0;
    Timer1.detachInterrupt();
}

// initialize random generator
void seed_random(void)
{
    ADMUX = (1 << REFS0);
    ADCSRA = (1 << ADEN) | 7;
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC))
        ;
    srand(ADC);
}

// prepare game settings
void setup_game(void)
{
    timer_period = 750000UL * (1 + (2 - (received_difficulty - EASY)));
    points = 0;
    current_led = rand() % 3;
    PORTB ^= (1 << leds[current_led]);
    return;
}

// main game execution
void start_game(void)
{
    setup_game();

    for (int t = 0; t < NO_TRIES; t++)
    {
        waiting_for_input = 1;
        button_pressed = 0;
        Timer1.initialize(timer_period);
        Timer1.attachInterrupt(timerISR);

        while (waiting_for_input)
        {
            if (!button_pressed)
            {
                for (int i = 0; i < 3; i++)
                {
                    if (PINB & (1 << buttons[i]))
                    {
                        button_pressed = 1;

                        if (i != current_led)
                        {
                            if (points >= penalty_points)
                                points -= penalty_points;
                            else
                                points = 0;
                        }
                        else
                        {
                            points += point_per_difficulty[received_difficulty - EASY];
                        }
                    }
                }
            }
            _delay_ms(20);
        }
    }
    Timer1.stop();
}

// stop game and clear outputs
void stop_game(){
    PORTB &= ~((1 << leds[0]) | (1 << leds[1]) | (1 << leds[2]));
    points = 0;
}

// main loop
int main(void)
{
    i2c_slave_init();
    sei();

    set_sleep_mode(SLEEP_MODE_IDLE);
    seed_random();

    for (int i = 0; i < 3; i++)
    {
        DDRB |= (1 << leds[i]);
        PORTB &= ~(1 << leds[i]);
        DDRB &= ~(1 << buttons[i]);
    }

    while (1)
    {
        sleep_enable();
        sleep_cpu();
        sleep_disable();

        if (data_ready)
        {
            data_ready = 0;
            
            start_game();
            tx_score = points;
            stop_game();
        }
    }
}
