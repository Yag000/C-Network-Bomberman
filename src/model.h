#ifndef SRC_MODEL_H_
#define SRC_MODEL_H_

#include <stdbool.h>

#define TEXT_SIZE 255
#define PLAYER_NUM 4

typedef enum ACTION {
    UP = 0,
    RIGHT = 1,
    DOWN = 2,
    LEFT = 3,
    PLACE_BOMB = 4,
    NONE = 5,
    CHAT_WRITE = 6,
    CHAT_ERASE = 7,
    QUIT = 8,
    SWITCH_PLAYER = 9
} ACTION;

typedef enum TILE {
    EMPTY = 0,
    INDESTRUCTIBLE_WALL = 1,
    DESTRUCTIBLE_WALL = 2,
    BOMB = 3,
    EXPLOSION = 4,
    PLAYER_1 = 5,
    PLAYER_2 = 6,
    PLAYER_3 = 7,
    PLAYER_4 = 8,
    VERTICAL_BORDER = 9,
    HORIZONTAL_BORDER = 10
} TILE;

typedef struct board {
    char *grid;
    int width;
    int height;
} board;

typedef struct line {
    char data[TEXT_SIZE];
    int cursor;
} line;

typedef struct coord {
    int x;
    int y;
} coord;

extern line *chat_line; // line of text that can be filled in with chat

/** Initializes - The game board with the width and the height
 *              - The chat line
 *              - The current position of the player
 */
int init_model(int, int);

/** Frees - The game board with the width and the height
 *              - The chat line
 *              - The current position of the players
 */
void free_model();

/** Frees the board
 */
void free_board(board *);

/** Returns the corresponding char of a tile
 */
char tile_to_char(TILE);

/** Returns the corresponding coordinate of an int in flatten list
 */
coord int_to_coord(int);

/** Returns the corresponding int of coordinate in flatten list
 */
int coord_to_int(int, int);

/** Returns the tile at the position (x, y) of game_board
 */
TILE get_grid(int, int);

/** Sets the tile at the position (x, y) of game_board to the last argument
 */
void set_grid(int, int, TILE);

/** Decrements the line cursor
 */
void decrement_line();

/** Adds the character at the end of chat_line if it does not exceed TEXT_SIZE and increment the cursor
 */
void add_to_line(char);

/** Depending on the action, changes the player's position in the table if the argument is a move.
 */
void perform_move(ACTION, int player_id);

/** Returns a copy of the game board
 */
board *get_game_board();

#endif // SRC_MODEL_H_
