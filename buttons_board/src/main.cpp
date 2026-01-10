#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "HD44780.hpp"
#include <string.h>
#define SLAVE_ADDRESS 0x08   // 7-bit address
#define LED_DDR DDRD
#define LED_PORT PORTD
#define LED_PIN PD2

volatile uint8_t received_byte;

void i2c_slave_init(void)
{
    TWAR = (SLAVE_ADDRESS << 1);
    TWCR = (1 << TWEN) | (1 << TWEA) | (1 << TWINT);

    PORTC |= (1 << PC4) | (1 << PC5);
}

uint8_t i2c_slave_receive(void)
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
}

int main(void)
{
    i2c_slave_init();
    
    LCD_Initalize();
    LCD_Clear();
    LED_DDR |= (1 << LED_PIN);  
    char data[3];
    strcpy(data, "sh");
    while (1)
    {
        data[0] = (char)i2c_slave_receive();
        
        _delay_ms(1000);
    }
}