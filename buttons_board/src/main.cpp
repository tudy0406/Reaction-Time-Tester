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

/* ================= DEFINES ================= */

#define SLAVE_ADDRESS 0x08
#define CMD_START_GAME 0xA0

#define EASY 110
#define MEDIUM 111
#define HARD 112

#define NO_TRIES 10

/* ================= GLOBALS ================= */

volatile uint8_t received_difficulty = 0;
volatile uint8_t data_ready = 0;
volatile uint8_t rx_index = 0;

uint8_t points = 0;
uint8_t point_per_difficulty[3] = {10, 15, 25};
uint8_t penalty_points = 5;

unsigned long timer_period = 0;
volatile uint8_t current_led = 0xFF, last_led = 0xFF;
volatile uint8_t waiting_for_input = 0;
volatile uint8_t button_pressed = 0;

int leds[3] = {PORTB0, PORTB1, PORTB2};
int buttons[3] = {PORTB3, PORTB4, PORTB5};

/* ================= I2C TX ================= */

volatile uint8_t tx_score = 0;

/* ================= I2C INIT ================= */

void i2c_slave_init(void)
{
    TWAR = (SLAVE_ADDRESS << 1);
    TWCR = (1 << TWEN) | (1 << TWEA) | (1 << TWIE);
    PORTC |= (1 << PC4) | (1 << PC5);
}

/* ================= I2C ISR ================= */

ISR(TWI_vect)
{
    uint8_t status = TWSR & 0xF8;
    static uint8_t rx_cmd = 0;

    switch (status)
    {
        /* -------- SLAVE RECEIVER -------- */

        case 0x60: // Own SLA+W received
        case 0x68: // Arbitration lost, SLA+W received
            rx_index = 0;
            break;

        case 0x80: // Data received, ACK returned
            if (rx_index == 0)
            {
                rx_cmd = TWDR;  // command byte
            }
            else if (rx_index == 1)
            {
                if (rx_cmd == CMD_START_GAME)
                {
                    received_difficulty = TWDR;
                    data_ready = 1;   // ✅ exactly one place
                }
            }
            rx_index++;
            break;

        case 0xA0: // STOP or repeated START
            rx_index = 0;
            break;

        /* -------- SLAVE TRANSMITTER -------- */

        case 0xA8: // SLA+R received
        case 0xB8: // Data transmitted, ACK received
            TWDR = tx_score;   // send score only
            break;

        case 0xC0: // NACK received
        case 0xC8: // Last data byte transmitted
            break;

    }

    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA) | (1 << TWIE);
}


/* ================= TIMER ISR ================= */

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

/* ================= RANDOM ================= */

void seed_random(void)
{
    ADMUX = (1 << REFS0);
    ADCSRA = (1 << ADEN) | 7;
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC))
        ;
    srand(ADC);
}

/* ================= GAME ================= */

void setup_game(void)
{
    timer_period = 750000UL * (1 + (2 - (received_difficulty - EASY)));
    points = 0;
    current_led = rand() % 3;
    PORTB ^= (1 << leds[current_led]);
    return;
}

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

void stop_game(){
    PORTB &= ~((1 << leds[0]) | (1 << leds[1]) | (1 << leds[2]));
    points = 0;
}

/* ================= MAIN ================= */

int main(void)
{
    i2c_slave_init();
    sei();

    LCD_Initalize();
    LCD_Clear();
    uart_init(9600, 0);

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
            char c[3];
            sprintf(c,"%u", received_difficulty);
            uart_send_string((uint8_t *)c);
            data_ready = 0;
            
            start_game();
            tx_score = points;
            stop_game();
        }
    }
}
