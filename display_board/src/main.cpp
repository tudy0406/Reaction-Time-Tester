#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <EEPROM.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "HD44780.hpp"
#include "libADC.hpp"
#include "uart_buffer.hpp"

#define F_CPU 16000000UL
#define SCL_CLOCK 100000L
#define LED_DDR DDRD
#define LED_PORT PORTD
#define LED_PIN PD2

// keypad states treshold / expected value
#define RIGHT_TR 100 // 0
#define UP_TR 250    // 131
#define DOWN_TR 350  // 306
#define LEFT_TR 500  // 480
// keypad states
#define RIGHT 40
#define UP 41
#define DOWN 42
#define LEFT 43
int keypad_state = UP;

#define IN_GAME -1
#define MENU 0
#define CHARACTER 10
#define DIFFICULTY 11
#define LEADERBOARD 2

int menu_select = MENU;
int options_select = 0;

char main_menu[2][12] = {{'N', 'E', 'W', ' ', 'G', 'A', 'M', 'E', '\0'}, {'L', 'E', 'A', 'D', 'E', 'R', 'B', 'O', 'A', 'R', 'D', '\0'}};
char difficulties[3][7] = {{'E', 'A', 'S', 'Y', '\0'}, {'M', 'E', 'D', 'I', 'U', 'M', '\0'}, {'H', 'A', 'R', 'D', '\0'}};

typedef struct
{
    char username[6];
    int high_score;
} Player;

Player **registered_players;
uint8_t no_registered_players;

void i2c_init(void)
{
    TWSR = 0x00; // prescaler
    TWBR = ((F_CPU / SCL_CLOCK) - 16) / 2;

    PORTC |= (1 << PC4) | (1 << PC5);
}

void i2c_start(void)
{
    TWCR = (1 << TWSTA) | (1 << TWEN) | (1 << TWINT);
    while (!(TWCR & (1 << TWINT)))
        ;
}

void i2c_stop(void)
{
    TWCR = (1 << TWSTO) | (1 << TWEN) | (1 << TWINT);
}

void i2c_write(uint8_t data)
{
    TWDR = data;
    TWCR = (1 << TWEN) | (1 << TWINT);
    while (!(TWCR & (1 << TWINT)))
        ;
}

void update_screen()
{
    LCD_Clear();
    LCD_GoTo(0, 0);
    char buf[20];
    switch (menu_select)
    {
    case IN_GAME:
        for (uint8_t i = 3; i > 0; i--)
        {
            LCD_Clear();
            LCD_GoTo(0, 0);
            LCD_WriteData((char)(i + 48));
            _delay_ms(1000);
        }
        LCD_Clear();
        LCD_GoTo(0, 0);
        LCD_WriteText((char *)"START!");
        break;

    case MENU:
        if (keypad_state == UP || keypad_state == DOWN)
        {
            sprintf(buf, "[%s]", main_menu[options_select]);
            LCD_WriteText(buf);
            _delay_ms(100);
            if (options_select < 1)
            {
                LCD_GoTo(1, 1);
                sprintf(buf, "%s", main_menu[options_select + 1]);
                LCD_WriteText(buf);
                _delay_ms(100);
            }
        }
        else if (keypad_state == LEFT)
            break;
        else if (keypad_state == RIGHT)
        {
            switch (options_select)
            {
            case 0:
                menu_select = CHARACTER;
                break;
            case 1:
                menu_select = LEADERBOARD;
                break;
            }
            options_select = 0;
            update_screen();
        }
        break;

    case CHARACTER:
        if (keypad_state == UP || keypad_state == DOWN)
        {
            if (options_select < no_registered_players)
            {
                sprintf(buf, "[%s]", registered_players[options_select]->username);
                LCD_WriteText(buf);
                _delay_ms(100);
            }
            if (options_select < no_registered_players - 1)
            {
                LCD_GoTo(1, 1);
                sprintf(buf, "%s", registered_players[options_select + 1]->username);
                LCD_WriteText(buf);
                _delay_ms(100);
            }
            else
            {
                if (options_select == no_registered_players - 1)
                {
                    LCD_GoTo(1, 1);
                    sprintf(buf, "new user");
                    LCD_WriteText(buf);
                    _delay_ms(100);
                }
                else if (options_select == no_registered_players)
                {
                    sprintf(buf, "[new user]");
                    LCD_WriteText(buf);
                    _delay_ms(100);
                }
            }
        }
        else if (keypad_state == LEFT)
        {
            menu_select = MENU;
            options_select = 0;
            update_screen();
            keypad_state = UP;
        }
        break;

    case DIFFICULTY:
        if (keypad_state == UP || keypad_state == DOWN)
        {
            sprintf(buf, "[%s]", difficulties[options_select]);
            LCD_WriteText(buf);
            _delay_ms(100);
            if (options_select < 2)
            {
                LCD_GoTo(1, 1);
                sprintf(buf, "%s", difficulties[options_select + 1]);
                LCD_WriteText(buf);
                _delay_ms(100);
            }
        }
        else if (keypad_state == LEFT)
        {
            menu_select = MENU;
            options_select = 0;
            keypad_state = UP;
            update_screen();
        }
        else if (keypad_state == RIGHT)
        {
            if (options_select < no_registered_players)
                menu_select = IN_GAME;
        }
        break;

    case LEADERBOARD:
        if (keypad_state == UP || keypad_state == DOWN)
        {
            if (options_select < no_registered_players)
            {
                sprintf(buf, "%s %u", registered_players[options_select]->username, registered_players[options_select]->high_score);
                LCD_WriteText(buf);
                _delay_ms(100);
            }
            if (options_select < no_registered_players)
            {
                LCD_GoTo(1, 1);
                sprintf(buf, "%s %u", registered_players[options_select + 1]->username, registered_players[options_select]->high_score);
                LCD_WriteText(buf);
                _delay_ms(100);
            }
        }
        else if (keypad_state == LEFT)
        {
            menu_select = MENU;
            options_select = 0;
            keypad_state = UP;
            update_screen();
        }
        else if (keypad_state == RIGHT)
        {
            break;
        }
        break;
    }
    return;
}

void serialize() {}

void deserialize()
{
    int addr = 0;
    no_registered_players = EEPROM.read(addr++);
    if (no_registered_players == 0)
        return;
    registered_players = (Player **)malloc(no_registered_players * sizeof(Player *));
    for (int i = 0; i < no_registered_players; i++)
        registered_players[i] = (Player *)malloc(sizeof(Player));
    int k;
    uint8_t high_score;
    char buff[20];
    for (int i = 0; i < no_registered_players; i++)
    {
        k = 0;
        while ((buff[k++] = (char)EEPROM.read(addr++)) != '\0')
        {
        }
        high_score = (char)EEPROM.read(addr++);
        strcpy(registered_players[i]->username, buff);
        registered_players[i]->high_score = high_score;
    }
    return;
}

int main(void)
{
    i2c_init();

    LCD_Initalize();
    LCD_Clear();

    ADC_Init();

    uint16_t raw, rawOld = 0;
    update_screen();

    deserialize();

    while (1)
    {

        while (menu_select != IN_GAME)
        {
            raw = ADC_conversion();
            if ((raw - rawOld) < 50)
            {
                rawOld = raw;
            }
            else
            {

                if (raw < 100)
                {
                    keypad_state = RIGHT;
                    update_screen();
                }

                else if (raw < 250)
                {
                    keypad_state = UP;
                    if (options_select == 0)
                        continue;
                    options_select--;
                    update_screen();
                }

                else if (raw < 350)
                {
                    keypad_state = DOWN;
                    if (menu_select == MENU && options_select == 1)
                        continue;
                    if ((menu_select == CHARACTER || menu_select == LEADERBOARD) && options_select == no_registered_players)
                        continue;
                    if (menu_select == DIFFICULTY && options_select == 2)
                        continue;
                    options_select++;
                    update_screen();
                }

                else if (raw < 500)
                {
                    keypad_state = LEFT;
                    options_select = 0;
                    update_screen();
                }
            }
        }

        // i2c_start();
        // i2c_write((0x08 << 1) | 0); // slave address + write
        // i2c_write(1); // data
        // i2c_stop();
        // _delay_ms(1000);
    }
}
