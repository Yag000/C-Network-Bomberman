#include "./view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "./utils.h"
#include "model.h"

// Define the minimum size of the terminal window
#define MIN_WINDOW_WIDTH 150
#define MIN_WINDOW_HEIGHT 30

// Define the padding for the screen (between the terminal window and the game/chat windows)
#define PADDING_SCREEN_TOP 1
#define PADDING_SCREEN_LEFT 2

// Define the padding for the playable area
#define PADDING_PLAYABLE_TOP 2
#define PADDING_PLAYABLE_LEFT 4

typedef struct window_context {
    dimension dim;
    int start_y;
    int start_x;
    WINDOW *win;
} window_context;

typedef struct padding {
    int top;
    int left;
} padding; // Padding right and bottom are not needed (because of inferring)

static window_context *game_wc;
static window_context *chat_wc;
static window_context *chat_history_wc;
static window_context *chat_input_wc;

static const padding SCREEN_PADDING = {PADDING_SCREEN_TOP, PADDING_SCREEN_LEFT};

static void get_height_width_terminal(dimension *);
static bool is_valid_terminal_size();

// Helper functions for managing windows
static int init_windows();
static void del_window(window_context *);
static void del_all_windows();

// Helper functions for splitting the terminal window
static int split_terminal_window(window_context *, window_context *);
static int split_chat_window(window_context *, window_context *, window_context *);

// Helper functions for refreshing the game and chat windows
static void print_game(board *, window_context *);
static void print_chat(GAME_MODE, chat *, int, window_context *, window_context *);

static void toggle_focus(chat *, window_context *, window_context *, window_context *);

int init_colors() {
    if (has_colors() == FALSE) {
        endwin();
        printf("Your terminal does not support color\n");
        return EXIT_FAILURE;
    }

    start_color();                           // Enable colors
    init_pair(1, COLOR_WHITE, COLOR_BLACK);  // For the unfocused window
    init_pair(2, COLOR_YELLOW, COLOR_BLACK); // For the focused window
    init_pair(3, COLOR_WHITE, COLOR_BLACK);  // For chat text

    // Initialize the colors for the players
    init_pair(4, COLOR_BLUE, COLOR_BLACK);
    init_pair(5, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(6, COLOR_YELLOW, COLOR_BLACK);
    init_pair(7, COLOR_GREEN, COLOR_BLACK);

    // Initialize the color for the borders
    init_pair(8, COLOR_WHITE, COLOR_BLACK);

    // Initialize the color for indestructible walls
    init_pair(9, COLOR_WHITE, COLOR_BLACK);

    // Initialize the color for destructible walls
    init_pair(10, COLOR_CYAN, COLOR_BLACK);

    // Initialize the color for the bomb
    init_pair(11, COLOR_RED, COLOR_BLACK);

    init_pair(12, COLOR_RED, COLOR_BLACK);

    return EXIT_SUCCESS;
}

int init_view() {
    initscr();   /* Start curses mode */
    raw();       /* Disable line buffering */
    noecho();    /* Don't echo() while we do getch (we will manually print characters when relevant) */
    curs_set(0); // Set the cursor to invisible

    RETURN_FAILURE_IF_ERROR(init_colors()); // Initialize the colors

    if (!is_valid_terminal_size()) { // Check if the terminal is big enough
        dimension dim;
        get_height_width_terminal(&dim);
        end_view();
        printf(
            "Please resize your terminal to at least %d rows and %d columns (now %d rows, %d columns) and restart the "
            "game.\n",
            MIN_WINDOW_HEIGHT, MIN_WINDOW_WIDTH, dim.height, dim.width);

        return EXIT_FAILURE;
    }

    RETURN_FAILURE_IF_ERROR(init_windows()); // Initialize the windows

    return EXIT_SUCCESS;
}

void end_view() {
    del_all_windows(); // Delete all the windows and free the memory
    curs_set(1);       // Set the cursor to visible again
    endwin();          /* End curses mode */
}

void get_height_width_terminal(dimension *dim) {
    if (dim != NULL) {
        getmaxyx(stdscr, dim->height, dim->width);
    }
}

void get_height_width_playable(dimension *dim, dimension scr_dim) {
    if (dim != NULL) {
        dim->height = min(scr_dim.height, scr_dim.width); // 2/3 of the terminal height is customizable
        dim->width = GAMEBOARD_WIDTH + 5;                 // 2:1 aspect ratio (ncurses characters are not square)
    }
}

void add_padding(dimension *dim, padding pad) {
    if (dim != NULL) {
        dim->height -= 2 * pad.top;
        dim->width -= 2 * pad.left;
    }
}

void get_computed_board_dimension(dimension *dim) {
    if (dim != NULL) {
        dimension scr_dim;
        get_height_width_terminal(&scr_dim);
        get_height_width_playable(dim, scr_dim);
        padding pad;
        pad.left = (dim->width - GAMEBOARD_WIDTH) / 2;
        pad.top = (dim->height - GAMEBOARD_HEIGHT) / 2;
        add_padding(dim, pad);
        // add_padding(dim, PLAYABLE_PADDING);
        add_padding(dim, SCREEN_PADDING);
    }
}

void refresh_game(GAME_MODE game_mode, board *b, chat *c, int player_id) {
    toggle_focus(c, game_wc, chat_history_wc, chat_input_wc);
    print_game(b, game_wc);
    wrefresh(game_wc->win); // Refresh the game window

    print_chat(game_mode, c, player_id, chat_history_wc, chat_input_wc);
    wrefresh(chat_input_wc->win);   // Refresh the chat input window (Before chat_win refresh)
    wrefresh(chat_history_wc->win); // Refresh the chat history window
    wrefresh(chat_wc->win);         // Refresh the chat window

    refresh(); // Apply the changes to the terminal
}

bool is_valid_terminal_size() {
    dimension dim;
    get_height_width_terminal(&dim);

    if (dim.height < MIN_WINDOW_HEIGHT || dim.width < MIN_WINDOW_WIDTH) {
        return false;
    }

    return true;
}

int init_windows() {
    game_wc = malloc(sizeof(window_context));
    chat_wc = malloc(sizeof(window_context));
    chat_history_wc = malloc(sizeof(window_context));
    chat_input_wc = malloc(sizeof(window_context));

    RETURN_FAILURE_IF_ERROR(split_terminal_window(game_wc, chat_wc));
    RETURN_FAILURE_IF_ERROR(split_chat_window(chat_wc, chat_history_wc, chat_input_wc));

    return EXIT_SUCCESS;
}

void del_all_windows() {
    del_window(game_wc);
    del_window(chat_history_wc);
    del_window(chat_input_wc);
    del_window(chat_wc);
}

void del_window(window_context *wc) {
    if (wc == NULL || wc->win == NULL) {
        return;
    }
    delwin(wc->win);
    wc->win = NULL;
    free(wc);
}

int split_terminal_window(window_context *game_wc, window_context *chat_wc) {
    dimension scr_dim;
    get_height_width_terminal(&scr_dim);
    padding pad = {PADDING_SCREEN_TOP, PADDING_SCREEN_LEFT};
    add_padding(&scr_dim, pad);
    // Game window is a square occupying maximum right side of the terminal
    dimension game_dim;
    get_height_width_playable(&game_dim, scr_dim);
    game_wc->dim.height = game_dim.height;
    game_wc->dim.width = game_dim.width;
    game_wc->start_y = pad.top + (scr_dim.height - game_wc->dim.height) / 2; // Center the window vertically
    game_wc->start_x = pad.left;
    game_wc->win = newwin(game_wc->dim.height, game_wc->dim.width, game_wc->start_y, game_wc->start_x);

    if (game_wc->win == NULL) {
        end_view();
        printf("Error creating game window\n");
        return EXIT_FAILURE;
    }

    chat_wc->dim.height = scr_dim.height;
    chat_wc->dim.width = scr_dim.width - game_wc->dim.width;
    chat_wc->start_y = pad.top;
    chat_wc->start_x = game_wc->dim.width + pad.left;
    chat_wc->win = newwin(chat_wc->dim.height, chat_wc->dim.width, chat_wc->start_y, chat_wc->start_x);

    if (chat_wc->win == NULL) {
        end_view();
        printf("Error creating chat window\n");
        return EXIT_FAILURE;
    }

    // Border around the windows
    box(game_wc->win, 0, 0);
    box(chat_wc->win, 0, 0);

    // Apply color to the borders
    wbkgd(game_wc->win, COLOR_PAIR(2));
    wbkgd(chat_wc->win, COLOR_PAIR(1));

    return EXIT_SUCCESS;
}

int split_chat_window(window_context *chat_wc, window_context *chat_history_wc, window_context *chat_input_wc) {

    chat_history_wc->dim.height = chat_wc->dim.height - 3;
    chat_history_wc->dim.width = chat_wc->dim.width;
    chat_history_wc->start_y = chat_wc->start_y;
    chat_history_wc->start_x = chat_wc->start_x;
    chat_history_wc->win = subwin(chat_wc->win, chat_history_wc->dim.height, chat_history_wc->dim.width,
                                  chat_history_wc->start_y, chat_history_wc->start_x);

    if (chat_history_wc->win == NULL) {
        end_view();
        printf("Error creating sub chat history window\n");
        return EXIT_FAILURE;
    }

    chat_input_wc->dim.height = 3;
    chat_input_wc->dim.width = chat_wc->dim.width;
    chat_input_wc->start_y = chat_wc->start_y + chat_wc->dim.height - 3;
    chat_input_wc->start_x = chat_wc->start_x;
    chat_input_wc->win = subwin(chat_wc->win, chat_input_wc->dim.height, chat_input_wc->dim.width,
                                chat_input_wc->start_y, chat_input_wc->start_x);

    if (chat_input_wc->win == NULL) {
        end_view();
        printf("Error creating sub chat input window\n");
        return EXIT_FAILURE;
    }

    scrollok(chat_history_wc->win, TRUE); // Enable scrolling in the chat history window

    box(chat_history_wc->win, 0, 0);
    box(chat_input_wc->win, 0, 0);

    wbkgd(chat_history_wc->win, COLOR_PAIR(2));
    wbkgd(chat_input_wc->win, COLOR_PAIR(2));

    return EXIT_SUCCESS;
}

void activate_color_for_tile(window_context *wc, TILE tile) {
    switch (tile) {
        case PLAYER_1:
            wattron(wc->win, COLOR_PAIR(4));
            break;
        case PLAYER_2:
            wattron(wc->win, COLOR_PAIR(5));
            break;
        case PLAYER_3:
            wattron(wc->win, COLOR_PAIR(6));
            break;
        case PLAYER_4:
            wattron(wc->win, COLOR_PAIR(7));
            break;
        case VERTICAL_BORDER:
        case HORIZONTAL_BORDER:
            wattron(wc->win, COLOR_PAIR(8));
            break;
        case INDESTRUCTIBLE_WALL:
            wattron(wc->win, COLOR_PAIR(9));
            break;
        case DESTRUCTIBLE_WALL:
            wattron(wc->win, COLOR_PAIR(10));
            break;
        case EMPTY:
            break;
        case BOMB:
            wattron(wc->win, COLOR_PAIR(11));
            break;
        case EXPLOSION:
            break;
    }
}

void deactivate_color_for_tile(window_context *wc, TILE tile) {
    switch (tile) {
        case PLAYER_1:
            wattroff(wc->win, COLOR_PAIR(4));
            break;
        case PLAYER_2:
            wattroff(wc->win, COLOR_PAIR(5));
            break;
        case PLAYER_3:
            wattroff(wc->win, COLOR_PAIR(6));
            break;
        case PLAYER_4:
            wattroff(wc->win, COLOR_PAIR(7));
            break;
        case VERTICAL_BORDER:
        case HORIZONTAL_BORDER:
            wattroff(wc->win, COLOR_PAIR(8));
            break;
        case INDESTRUCTIBLE_WALL:
            wattroff(wc->win, COLOR_PAIR(9));
            break;
        case DESTRUCTIBLE_WALL:
            wattroff(wc->win, COLOR_PAIR(10));
            break;
        case EMPTY:
            break;
        case BOMB:
            wattroff(wc->win, COLOR_PAIR(11));
            break;
        case EXPLOSION:
            break;
    }
}

void print_game(board *b, window_context *game_wc) {
    // Update grid
    int x, y;
    dimension dim = b->dim;
    int pad_top = (game_wc->dim.height - dim.height - 2) / 2; // We can substract 2 or 1 but the first enable a left
                                                              // align whether 1 is for a right align
    int pad_left = (game_wc->dim.width - dim.width - 2) / 2;
    padding pad = {pad_top, pad_left};
    char vb = tile_to_char(VERTICAL_BORDER);
    char hb = tile_to_char(HORIZONTAL_BORDER);
    for (y = 0; y < b->dim.height; y++) {
        for (x = 0; x < b->dim.width; x++) {
            int pos = coord_to_int_dim(x, y, dim);
            TILE t = b->grid[pos];
            char c = tile_to_char(t);
            activate_color_for_tile(game_wc, t);
            mvwaddch(game_wc->win, y + 1 + pad.top, x + 1 + pad.left, c);
            deactivate_color_for_tile(game_wc, t);
        }
    }

    activate_color_for_tile(game_wc, HORIZONTAL_BORDER);
    for (x = 0; x < b->dim.width + 2; x++) {
        mvwaddch(game_wc->win, pad.top, x + pad.left, hb);
        mvwaddch(game_wc->win, b->dim.height + 1 + pad.top, x + pad.left, hb);
    }
    deactivate_color_for_tile(game_wc, HORIZONTAL_BORDER);

    activate_color_for_tile(game_wc, VERTICAL_BORDER);
    for (y = 0; y < b->dim.height + 2; y++) {
        mvwaddch(game_wc->win, y + pad.top, pad.left, vb);
        mvwaddch(game_wc->win, y + pad.top, b->dim.width + 1 + pad.left, vb);
    }
    deactivate_color_for_tile(game_wc, VERTICAL_BORDER);
}

void activate_color_for_player(window_context *wc, int player_id) {
    switch (player_id) {
        case 1:
            wattron(wc->win, COLOR_PAIR(4));
            break;
        case 2:
            wattron(wc->win, COLOR_PAIR(5));
            break;
        case 3:
            wattron(wc->win, COLOR_PAIR(6));
            break;
        case 4:
            wattron(wc->win, COLOR_PAIR(7));
            break;
        default:
            break;
    }
}

void deactivate_color_for_player(window_context *wc, int player_id) {
    switch (player_id) {
        case 1:
            wattroff(wc->win, COLOR_PAIR(4));
            break;
        case 2:
            wattroff(wc->win, COLOR_PAIR(5));
            break;
        case 3:
            wattroff(wc->win, COLOR_PAIR(6));
            break;
        case 4:
            wattroff(wc->win, COLOR_PAIR(7));
            break;
        default:
            break;
    }
}

void clear_view_line(window_context *wc, int offset_y, int offset_x) {
    int x;
    char e = tile_to_char(EMPTY);
    for (x = 0; x < wc->dim.width - offset_x - 1; x++) {
        mvwaddch(wc->win, offset_y, x + offset_x, e);
    }
}

int print_player_tag_chat(GAME_MODE game_mode, bool whispering, int sender, window_context *wc, int offset_y) {
    // Update chat text
    activate_color_for_player(wc, sender + 1);
    wattron(wc->win, A_BOLD); // Enable bold

    char buf[30];
    int len = 0;
    if (whispering && game_mode == TEAM) {
        len = snprintf(buf, sizeof(buf), " Player%d ", sender + 1);
    } else {
        len = snprintf(buf, sizeof(buf), " Player%d : ", sender + 1);
    }

    clear_view_line(wc, 1 + offset_y, 1);
    mvwprintw(wc->win, 1 + offset_y, 1, "%s", buf);

    wattroff(wc->win, A_BOLD); // Disable bold
    deactivate_color_for_player(wc, sender + 1);

    return len;
}

int print_whispering_tag_chat(int player_tag_len, int sender, window_context *wc, int offset_y) {
    char buf[30];
    int len = snprintf(buf, sizeof(buf), "(whispering)");

    wattron(wc->win, COLOR_PAIR(12));
    mvwprintw(wc->win, 1 + offset_y, 1 + player_tag_len, "%s", buf);
    wattroff(wc->win, COLOR_PAIR(12));

    memset(buf, 0, 30);
    int tmp = snprintf(buf, sizeof(buf), " : ");

    activate_color_for_player(wc, sender + 1);
    clear_view_line(wc, 1 + offset_y, 1 + player_tag_len + len);
    mvwprintw(wc->win, 1 + offset_y, 1 + player_tag_len + len, "%s", buf);
    deactivate_color_for_player(wc, sender + 1);

    len += tmp;

    return len;
}

void print_tag_chat(GAME_MODE game_mode, int *player_tag_len, int *whispering_tag_len, int sender, bool whispering,
                    window_context *wc, int offset_y) {
    *player_tag_len = print_player_tag_chat(game_mode, whispering, sender, wc, offset_y);
    *whispering_tag_len = 0;
    if (whispering && game_mode == TEAM) {
        *whispering_tag_len = print_whispering_tag_chat(*player_tag_len, sender, wc, offset_y);
    }
}

void print_chat_input(chat *c, int player_tag_len, int whispering_tag_len, window_context *chat_input_wc) {
    wattron(chat_input_wc->win, COLOR_PAIR(3)); // Enable custom color 2
    int x;
    char e = tile_to_char(EMPTY);
    for (x = 0; x < chat_input_wc->dim.width - 2 - player_tag_len - whispering_tag_len; x++) {
        if (x >= TEXT_SIZE || x >= c->line->cursor) {
            mvwaddch(chat_input_wc->win, 1, x + 1 + player_tag_len + whispering_tag_len, e);
        } else {
            mvwaddch(chat_input_wc->win, 1, x + 1 + player_tag_len + whispering_tag_len, c->line->data[x]);
        }
    }
    wattroff(chat_input_wc->win, COLOR_PAIR(3)); // Disable custom color 3
}

void print_chat_history(GAME_MODE game_mode, chat *c, window_context *chat_history_wc) {
    wattron(chat_history_wc->win, COLOR_PAIR(3)); // Enable custom color 3
    chat_node *cnode = c->history->head;
    int i = 0;
    while (i < c->history->count) {
        int player_tag_len = 0;
        int whispering_tag_len = 0;
        print_tag_chat(game_mode, &player_tag_len, &whispering_tag_len, cnode->sender, cnode->whispered,
                       chat_history_wc, i);

        activate_color_for_player(chat_history_wc, cnode->sender + 1);
        clear_view_line(chat_history_wc, i + 1, 1 + player_tag_len + whispering_tag_len);
        mvwprintw(chat_history_wc->win, i + 1, 1 + player_tag_len + whispering_tag_len, "%s", cnode->message);
        deactivate_color_for_player(chat_history_wc, cnode->sender + 1);
        cnode = cnode->next;
        ++i;
    }
    wattroff(chat_history_wc->win, COLOR_PAIR(3)); // Disable custom color 3
}

void print_chat(GAME_MODE game_mode, chat *c, int player_id, window_context *chat_history_wc,
                window_context *chat_input_wc) {
    // Add tag
    int player_tag_len = 0;
    int whispering_tag_len = 0;
    print_tag_chat(game_mode, &player_tag_len, &whispering_tag_len, player_id, c->whispering, chat_input_wc, 0);

    // Update chat text
    print_chat_input(c, player_tag_len, whispering_tag_len, chat_input_wc);

    // Update chat history
    print_chat_history(game_mode, c, chat_history_wc);
}

void toggle_focus(chat *c, window_context *game_wc, window_context *chat_history_wc, window_context *chat_input_wc) {
    if (!c->on_focus) {
        wbkgd(game_wc->win, COLOR_PAIR(2));
        wbkgd(chat_history_wc->win, COLOR_PAIR(1));
        wbkgd(chat_input_wc->win, COLOR_PAIR(1));
    } else {
        wbkgd(game_wc->win, COLOR_PAIR(1));
        wbkgd(chat_history_wc->win, COLOR_PAIR(2));
        wbkgd(chat_input_wc->win, COLOR_PAIR(2));
    }
}
