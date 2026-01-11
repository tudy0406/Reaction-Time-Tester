#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>
#include <string.h>

#include "HD44780.hpp"
#include "TimerOne.hpp"
#include "uart_buffer.hpp"

#define SLAVE_ADDRESS 0x08 // 7-bit address

// difficulties
#define EASY 110
#define MEDIUM 111
#define HARD 112

#define NO_TRIES 10

volatile uint8_t received_difficulty = 0;
uint8_t data_ready = 0;
uint8_t points = 0;
uint8_t point_per_difficulty[3] = {10, 15, 25}; // EASY, MEDIUM, HARD
uint8_t penalty_points = 5;                     //-5 points if you press the wrong button

unsigned long timer_period = 0;
volatile uint8_t current_led = 0xFF, last_led = 0xFF;
volatile bool waiting_for_input = false;
volatile bool button_pressed = false;

int leds[3] = {PORTB0, PORTB1, PORTB2};
int buttons[3] = {PORTB3, PORTB4, PORTB5};
uint8_t button_history[3] = {0, 0, 0};

void i2c_slave_init(void)
{
    TWAR = (SLAVE_ADDRESS << 1);
    TWCR = (1 << TWEN) | (1 << TWEA) | (1 << TWINT) | (1 << TWIE);

    PORTC |= (1 << PC4) | (1 << PC5);
}

ISR(TWI_vect)
{
    uint8_t status = TWSR & 0xF8;

    if (status == 0x80)
    { // Data received
        received_difficulty = TWDR;
        data_ready = 1;
    }

    TWCR |= (1 << TWINT) | (1 << TWEN) | (1 << TWEA) | (1 << TWIE); // Clear flag, keep interrupt enabled
}

void timerISR(void)
{

    if (current_led != 0xFF)
        PORTB &= ~(1 << leds[current_led]);

    last_led = current_led;
    while (current_led == last_led)
        current_led = rand() % 3;

    PORTB |= (1 << leds[current_led]);

    waiting_for_input = false;
    button_pressed = false;
}

void seed_random(void)
{
    ADMUX = (1 << REFS0);      // AVcc reference
    ADCSRA = (1 << ADEN) | 7;  // enable ADC, prescaler 128
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    srand(ADC);
}

/*uint8_t i2c_slave_receive(void)
{
    // Wait for address or data
    while (!(TWCR & (1 << TWINT)));

    uint8_t status = TWSR & 0xF8;

    // SLA+W received
    if (status == 0x60 || status == 0x68) {
        TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
        while (!(TWCR & (1 << TWINT)));
        status = TWSR & 0xF8;
    }

    // Data received
    if (status == 0x80) {
        uint8_t data = TWDR;
        TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
        return data;
    }

    // Default fallback
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
    return 0;
}*/

void setup_game();
void start_game();

/*int main(void)
{
    i2c_slave_init();
    sei();
    LCD_Initalize();
    LCD_Clear();

    set_sleep_mode(SLEEP_MODE_IDLE);

    for (int i = 0; i < 3; i++)
    {
        DDRB |= (1 << leds[i]);
        PORTB &= ~(1 << leds[i]);
        DDRB &= ~(1 << buttons[i]);
    }

    while (1)
    {
        PORTB &= ~((1 << leds[0]) | (1 << leds[1]) | (1 << leds[2]));
        sleep_enable();
        sleep_cpu();
        sleep_disable();

        if (data_ready)
        {
            timer_period = 1000000 * (1 + (2 - (received_difficulty - EASY))); // for: EASY 1.5 seconds, MEDIUM 1 second, HARD 0.5 seconds

            start_game();
            data_ready = 0; // clear flag
        }
    }
}*/

int main(void)
{

    sei();
    uart_init(9600, 0);

    for (int i = 0; i < 3; i++)
    {
        DDRB |= (1 << leds[i]);
        PORTB &= ~(1 << leds[i]);
        DDRB &= ~(1 << buttons[i]);
    }
    for (int i = 3; i > 0; i--)
    {
        uart_send_byte((char)(i + 48));
        _delay_ms(1000);
    }

    seed_random();
    received_difficulty = HARD;
    start_game();

    char buff[4];
    sprintf(buff, "%u", points);
    uart_send_string((unsigned char *)buff);
    return 0;
}

void start_game()
{
    setup_game();   
    for (int i = 0; i < NO_TRIES; i++)
    {
        waiting_for_input = true;
        while (waiting_for_input)
        {
            if (!button_pressed)
            {
                for (int i = 0; i < 3; i++)
                {
                    if (PINB & (1 << buttons[i]))
                    {
                        button_pressed = true;
                        if (i != current_led)
                        {
                            if (points >= penalty_points)
                                points -= penalty_points;
                            else
                                points = 0;
                        }
                        else
                            points += point_per_difficulty[received_difficulty - EASY];
                    }
                }
            }
            _delay_ms(50);
        }
    }
}

void setup_game(){
    timer_period = 1500000 * (1 + (2 - (received_difficulty - EASY))); // for: EASY 2.25 seconds, MEDIUM 1.5 second, HARD 0.75 seconds
    Timer1.initialize(timer_period);
    Timer1.attachInterrupt(timerISR);
    current_led = rand()%3;
}