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

// i2c clock speed definition
#define SCL_CLOCK 100000L

// command sent to slave to start the game
#define CMD_START_GAME 0xA0

// command reserved for game over (not used here)
#define CMD_GAME_OVER 0xA1

// adc threshold values for keypad buttons
#define SELECT_TR 100
#define UP_TR 250
#define DOWN_TR 350
#define LEFT_TR 500

// keypad button identifiers
#define SELECT 40
#define UP 41
#define DOWN 42
#define LEFT 43

// current keypad state
int keypad_state = UP;

// number of rounds in one game
#define NO_TRIES 10

// menu states
#define IN_GAME -1
#define MENU 0
#define CHARACTER 10
#define NEW_USER 100
#define GAME_OVER 2

// difficulty menu state
#define DIFFICULTY 11

// difficulty values sent to slave
#define EASY 110
#define MEDIUM 111
#define HARD 112

// leaderboard menu state
#define LEADERBOARD 3

// current menu and option selection
int menu_select = MENU;
int options_select = 0;

// text for main menu
char main_menu[2][12] = {
    {'N','E','W',' ','G','A','M','E','\0'},
    {'L','E','A','D','E','R','B','O','A','R','D','\0'}
};

// text for difficulty menu
char difficulties[3][7] = {
    {'E','A','S','Y','\0'},
    {'M','E','D','I','U','M','\0'},
    {'H','A','R','D','\0'}
};

// selected difficulty level
uint8_t selected_difficulty = EASY;

// last score received from slave
uint8_t last_score = 0;

// player structure holding username and high score
typedef struct
{
    char username[10];
    uint8_t high_score;
} Player;

// dynamic list of registered players
Player **registered_players;

// number of registered players
uint8_t no_registered_players;

// index of current player
uint8_t current_player;

// initialize i2c as master
void i2c_init(void)
{
    TWSR = 0x00;
    TWBR = ((F_CPU / SCL_CLOCK) - 16) / 2;
    PORTC |= (1 << PC4) | (1 << PC5);
}

// send start condition on i2c bus
void i2c_start(void)
{
    TWCR = (1 << TWSTA) | (1 << TWEN) | (1 << TWINT);
    while (!(TWCR & (1 << TWINT)));
}

// send stop condition on i2c bus
void i2c_stop(void)
{
    TWCR = (1 << TWSTO) | (1 << TWEN) | (1 << TWINT);
}

// write one byte to i2c bus
void i2c_write(uint8_t data)
{
    TWDR = data;
    TWCR = (1 << TWEN) | (1 << TWINT);
    while (!(TWCR & (1 << TWINT)));
}

// read one byte from i2c and send ack
uint8_t i2c_read_ack(void)
{
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
    while (!(TWCR & (1 << TWINT)));
    return TWDR;
}

// read one byte from i2c and send nack
uint8_t i2c_read_nack(void)
{
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
    return TWDR;
}

// custom delay function used to wait for game completion
void delay_ms(uint16_t ms)
{
    char a[10000];
    sprintf(a, "%u", ms);
    uart_send_string((uint8_t*)a);
    while (ms--)
        _delay_ms(1);
}

// function prototypes
void serialize();
void deserialize();
int create_user();
void update_screen();

// main program entry point
int main(void)
{
    // initialize uart for debugging
    uart_init(9600, 0);

    // initialize i2c, adc and lcd
    i2c_init();
    ADC_Init();
    LCD_Initalize();
    sei();

    // load players from eeprom
    deserialize();

    // clear lcd and show menu
    LCD_Clear();
    update_screen();

    uint16_t raw, rawOld = 0;

    while (1)
    {
        // handle menu navigation
        while (menu_select != IN_GAME)
        {
            raw = ADC_conversion();

            // debounce adc reading
            if ((raw - rawOld) < 50)
            {
                rawOld = raw;
            }
            else
            {
                // move up in menu
                if (raw < 250)
                {
                    keypad_state = UP;
                    if (options_select == 0)
                        continue;
                    options_select--;
                    update_screen();
                }
                // move down in menu
                else if (raw < 350)
                {
                    keypad_state = DOWN;
                    if (menu_select == MENU && options_select == 1)
                        continue;
                    if ((menu_select == CHARACTER) && options_select == no_registered_players)
                        continue;
                    if ((menu_select == LEADERBOARD) && options_select == no_registered_players - 1)
                        continue;
                    if (menu_select == DIFFICULTY && options_select == 2)
                        continue;
                    options_select++;
                    update_screen();
                }
                // go back in menu
                else if (raw < 500)
                {
                    keypad_state = LEFT;
                    if (menu_select == MENU)
                        continue;
                    options_select = 0;
                    update_screen();
                }
                // select menu option
                else if (raw < 800)
                {
                    keypad_state = SELECT;
                    if (menu_select == LEADERBOARD)
                        continue;
                    update_screen();
                }
            }
            _delay_ms(100);
        }

        // start game state
        if (menu_select == IN_GAME)
        {
            last_score = 0;

            // send start command and difficulty to slave
            i2c_start();
            i2c_write((0x08 << 1) | 0);
            i2c_write(CMD_START_GAME);
            i2c_write(selected_difficulty);
            i2c_stop();

            // wait estimated game duration
            delay_ms(((NO_TRIES+1)*(1+(2-(selected_difficulty-EASY))))*750);

            // read score from slave
            i2c_start();
            i2c_write((0x08 << 1) | 1);
            last_score = i2c_read_nack();
            i2c_stop();

            // go to game over menu
            menu_select = GAME_OVER;
            keypad_state = UP;
            update_screen();
        }
    }
}

int create_user()
{
    char buff[10];
    buff[0] = '\0';
    int k = 0;
    uint8_t data = 0;
    LCD_Clear();
    LCD_GoTo(0, 0);
    LCD_WriteText((char *)"max 9 chars");

    while (1)
    {
        if (uart_read_count() > 0)
        {
            data = uart_read();
            if (k < 9)
            {
                if (data >= 'a' && data <= 'z')
                    buff[k++] = data;
                else if (data >= 'A' && data <= 'Z')
                    buff[k++] = data;
                else if (data >= '0' && data <= '9')
                {
                    if (k == 0)
                        continue;
                    else
                        buff[k++] = data;
                }
                else if (data == '-' || data == '_')
                {
                    if (k == 0)
                        continue;
                    else
                        buff[k++] = data;
                }
                if ((data == 0x08 || data == 0x7F) && k > 0)
                {
                    k--;
                    uart_send_byte('\b');
                    uart_send_byte(' ');
                    uart_send_byte('\b');

                    LCD_Clear();
                    LCD_GoTo(0, 0);
                    LCD_WriteText((char *)"max 9 chars");
                }
                if (data == '\n' && k > 0)
                    break;
                buff[k] = '\0';
                if (data != '\n' && (data != 0x08 && data != 0x7F))
                    uart_send_byte(data);
                LCD_GoTo(0, 1);
                LCD_WriteText(buff);
            }
            else
            {
                if ((data == 0x08 || data == 0x7F) && k > 0)
                {
                    k--;
                    buff[k] = '\0';
                    uart_send_byte('\b');
                    uart_send_byte(' ');
                    uart_send_byte('\b');

                    LCD_Clear();
                    LCD_GoTo(0, 0);
                    LCD_WriteText((char *)"max 9 chars");
                    LCD_GoTo(0, 1);
                    LCD_WriteText(buff);
                }
                if (data == '\n' && k > 0)
                    break;
            }
            if (data == 0x1B)
                return CHARACTER;
        }
        _delay_ms(200);
    }
    no_registered_players++;
    registered_players = (Player **)realloc(registered_players, no_registered_players * sizeof(Player *));
    registered_players[no_registered_players - 1] = (Player *)malloc(sizeof(Player));
    strcpy(registered_players[no_registered_players - 1]->username, buff);
    registered_players[no_registered_players - 1]->high_score = 0;
    serialize();
    return DIFFICULTY;
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

    case GAME_OVER:
        if (keypad_state != UP)
        {
            menu_select = MENU;
            keypad_state = UP;
            options_select = 0;
            update_screen();
        }
        else
        {
            sprintf(buf, "%u", last_score);
            if (registered_players[current_player]->high_score < last_score)
            {
                LCD_WriteText((char *)"NEW HIGHSCORE:");
                registered_players[current_player]->high_score = last_score;
                serialize();
            }
            else
            {
                LCD_WriteText((char *)"SCORE:");
            }
            LCD_GoTo(0, 1);
            LCD_WriteText(buf);
        }
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
        else if (keypad_state == SELECT)
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
            keypad_state = UP;
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
        else if (keypad_state == SELECT)
        {
            if (options_select < no_registered_players)
                menu_select = DIFFICULTY;
            else if (options_select == no_registered_players)
                menu_select = NEW_USER;
            current_player = options_select;
            options_select = 0;
            keypad_state = UP;
            update_screen();
        }
        else if (keypad_state == LEFT)
        {
            menu_select = MENU;
            options_select = 0;
            keypad_state = UP;
            update_screen();
        }
        break;

    case NEW_USER:
        menu_select = create_user();
        options_select = 0;
        keypad_state = UP;
        update_screen();
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
        else if (keypad_state == SELECT)
        {
            menu_select = IN_GAME;
            selected_difficulty = EASY + options_select;
            options_select = 0;
            keypad_state = UP;
            update_screen();
        }
        else if (keypad_state == LEFT)
        {
            menu_select = MENU;
            options_select = 0;
            keypad_state = UP;
            update_screen();
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
            if (options_select < no_registered_players - 1)
            {
                LCD_GoTo(0, 1);
                sprintf(buf, "%s %u", registered_players[options_select + 1]->username, registered_players[options_select + 1]->high_score);
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
        else if (keypad_state == SELECT)
        {
            break;
        }
        break;
    }
    return;
}

void serialize()
{
    int addr = 0, k;

    EEPROM.write(addr, no_registered_players);
    addr += sizeof(no_registered_players);

    for (int i = 0; i < no_registered_players; i++)
    {
        k = 0;
        while (registered_players[i]->username[k] != '\0')
            EEPROM.write(addr++, registered_players[i]->username[k++]);

        EEPROM.write(addr, registered_players[i]->username[k]);
        addr += sizeof(registered_players[i]->username[k]);

        EEPROM.write(addr, registered_players[i]->high_score);
        addr += sizeof(registered_players[i]->high_score);
    }
    return;
}

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