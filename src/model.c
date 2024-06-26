#include "./model.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "./utils.h"

typedef struct player {
    coord *pos;
    bool dead;
} player;

typedef struct bomb {
    coord pos;
    time_t placement_time;
} bomb;

typedef struct bomb_collection {
    bomb *arr;
    int total_count;
    int max_capacity;
} bomb_collection;

typedef struct game {
    board *game_board;
    bomb_collection all_bombs;
    player *players[PLAYER_NUM];
    GAME_MODE game_mode;
    chat *chat;
} game;

static game **games = NULL;
size_t games_size = 0;
size_t games_capacity = 10;
#define GROWTH_FACTOR 2

TILE get_player(int);

void free_model(unsigned int game_id);

int add_game(game *g) {
    if (games == NULL) {
        games = malloc(games_capacity * sizeof(game *));
        if (games == NULL) {
            perror("malloc");
            return -1;
        }
        for (size_t i = 0; i < games_capacity; i++) {
            games[i] = NULL;
        }
    }

    if (games_size == games_capacity) {
        size_t new_capacity = games_capacity * GROWTH_FACTOR;
        game **new_games = realloc(games, new_capacity * sizeof(game *));
        if (new_games == NULL) {
            perror("realloc");
            return -1;
        }
        games = new_games;
        games_capacity = new_capacity;

        for (size_t i = games_size; i < games_capacity; i++) {
            games[i] = NULL;
        }
    }

    if (games[games_size] == NULL) {
        games[games_size] = g;
        games_size++;
        return games_size - 1;
    }

    for (size_t i = 0; i < games_capacity; i++) {
        if (games[i] == NULL) {
            games[i] = g;
            games_size++;
            return i;
        }
    }

    return -1;
}

game *get_game(unsigned int game_id) {
    if (game_id >= games_size) {
        return NULL;
    }
    return games[game_id];
}

void remove_game(unsigned int game_id) {
    if (game_id >= games_capacity || games[game_id] == NULL) {
        return;
    }

    free_model(game_id);
    games[game_id] = NULL;
    games_size--;
    return;
}

void reset_games() {
    for (size_t i = 0; i < games_capacity; i++) {
        remove_game(i);
    }

    free(games);
    games = NULL;
}

static game *init_game_struct() {
    game *g = malloc(sizeof(game));
    if (g == NULL) {
        perror("malloc");
        return NULL;
    }

    g->game_board = NULL;
    g->all_bombs.arr = NULL;
    g->all_bombs.total_count = 0;
    g->all_bombs.max_capacity = 0;

    for (int i = 0; i < PLAYER_NUM; i++) {
        g->players[i] = NULL;
    }

    g->game_mode = SOLO;

    g->chat = NULL;

    return g;
}

TILE get_probably_destructible_wall() {
    if (random() % DESTRUCTIBLE_WALL_CHANCE == 0) {
        return EMPTY;
    }
    return DESTRUCTIBLE_WALL;
}

int init_game_board_content(unsigned int game_id) {
    RETURN_FAILURE_IF_NULL(games[game_id]);

    board *game_board = games[game_id]->game_board;
    RETURN_FAILURE_IF_NULL(game_board);

    srandom(time(NULL));

    // Indestructible wall part
    for (int c = 1; c < game_board->dim.width - 1; c += 2) {
        for (int l = 1; l < game_board->dim.height - 1; l += 2) {
            game_board->grid[coord_to_int(c, l, game_id)] = INDESTRUCTIBLE_WALL;
        }
    }

    // Destructible wall part
    for (int c = 3; c < game_board->dim.width - 3; c++) { // Fill the first and last line
        game_board->grid[coord_to_int(c, 0, game_id)] = get_probably_destructible_wall();
        game_board->grid[coord_to_int(c, game_board->dim.height - 1, game_id)] = get_probably_destructible_wall();
    }

    for (int c = 2; c < game_board->dim.width - 2; c += 2) { // Fill the second and the second last line
        game_board->grid[coord_to_int(c, 1, game_id)] = get_probably_destructible_wall();
        game_board->grid[coord_to_int(c, game_board->dim.height - 2, game_id)] = get_probably_destructible_wall();
    }

    for (int c = 1; c < game_board->dim.width - 1; c++) { // Fill the third and the third last line
        game_board->grid[coord_to_int(c, 2, game_id)] = get_probably_destructible_wall();
        game_board->grid[coord_to_int(c, game_board->dim.height - 3, game_id)] = get_probably_destructible_wall();
    }

    for (int l = 3; l < game_board->dim.height - 3; l++) { // Fill the other lines
        if (l % 2 == 0) { // There are no indestructible walls between destructible walls on this line
            for (int c = 0; c < game_board->dim.width; c++) {
                game_board->grid[coord_to_int(c, l, game_id)] = get_probably_destructible_wall();
            }
        } else { // There are indestructible walls between destructible walls on this line
            for (int c = 0; c < game_board->dim.width; c += 2) {
                game_board->grid[coord_to_int(c, l, game_id)] = get_probably_destructible_wall();
            }
        }
    }
    return EXIT_SUCCESS;
}

int init_game_board(dimension dim, unsigned int game_id) {
    RETURN_FAILURE_IF_NULL(games[game_id]);

    if (dim.width % 2 == 0) { // The game_board width has to be odd to fill it with content
        dim.width--;
    }
    if (dim.height % 2 == 0) { // The game board height has to be odd to fill it with content
        dim.height--;
    }

    if (dim.width < MIN_GAMEBOARD_WIDTH || dim.height < MIN_GAMEBOARD_HEIGHT) {
        return EXIT_FAILURE;
    }

    if (games[game_id]->game_board == NULL) {
        board *game_board = malloc(sizeof(board));
        RETURN_FAILURE_IF_NULL_PERROR(game_board, "malloc");

        game_board->dim.height = dim.height - 2; // 2 rows reserved for border
        game_board->dim.width = dim.width - 2;   // 2 columns reserved for border

        game_board->grid = calloc((game_board->dim.width) * (game_board->dim.height), sizeof(char));
        RETURN_FAILURE_IF_NULL_PERROR(game_board->grid, "calloc");

        games[game_id]->game_board = game_board;
    }

    if (init_game_board_content(game_id) == EXIT_FAILURE) {
        free_board(games[game_id]->game_board);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int init_player_positions(unsigned int game_id) {
    RETURN_FAILURE_IF_NULL(games[game_id]);

    player **players = games[game_id]->players;
    board *game_board = games[game_id]->game_board;

    for (int i = 0; i < 4; i++) {
        players[i] = malloc(sizeof(player));
        RETURN_FAILURE_IF_NULL_PERROR(players[i], "malloc");

        players[i]->pos = malloc(sizeof(coord));
        RETURN_FAILURE_IF_NULL_PERROR(players[i]->pos, "malloc");

        if (i < 2) {
            players[i]->pos->y = 0;
        } else {
            players[i]->pos->y = game_board->dim.height - 1;
        }
        players[i]->pos->x = (game_board->dim.width - (i % 2)) % game_board->dim.width;

        players[i]->dead = false;

        set_grid(players[i]->pos->x, players[i]->pos->y, get_player(i), game_id);
    }
    return EXIT_SUCCESS;
}

int init_game_chat(unsigned int game_id) {
    RETURN_FAILURE_IF_NULL(games[game_id]);

    if (games[game_id]->chat == NULL) {
        games[game_id]->chat = create_chat();
        RETURN_FAILURE_IF_NULL(games[game_id]->chat);
    }

    return EXIT_SUCCESS;
}

int init_model(dimension dim, GAME_MODE game_mode_) {
    game *g = init_game_struct();
    if (g == NULL) {
        return -1;
    }

    g->game_mode = game_mode_;

    int game_id = add_game(g);

    if (game_id == -1) {
        return -1;
    }

    if (init_game_board(dim, game_id) == EXIT_FAILURE) {
        return -1;
    }

    if (init_player_positions(game_id) == EXIT_FAILURE) {
        return -1;
    }
    if (init_game_chat(game_id) == EXIT_FAILURE) {
        return -1;
    }

    return game_id;
}

void free_board(board *game_board) {
    if (game_board != NULL) {
        if (game_board->grid != NULL) {
            free(game_board->grid);
            game_board->grid = NULL;
        }
        free(game_board);
        game_board = NULL;
    }
}

void free_game_board(unsigned int game_id) {
    if (games[game_id] == NULL) {
        return;
    }
    board *game_board = games[game_id]->game_board;
    free_board(game_board);
}

void free_player_positions(unsigned int game_id) {
    if (games[game_id] == NULL) {
        return;
    }
    for (int i = 0; i < PLAYER_NUM; i++) {
        if (games[game_id]->players[i] != NULL) {
            if (games[game_id]->players[i]->pos != NULL) {
                free(games[game_id]->players[i]->pos);
                games[game_id]->players[i]->pos = NULL;
            }
            free(games[game_id]->players[i]);
            games[game_id]->players[i] = NULL;
        }
    }
}

void free_model(unsigned int game_id) {
    free_game_board(game_id);
    free_chat(games[game_id]->chat);
    games[game_id]->chat = NULL;
    free_player_positions(game_id);
    free(games[game_id]);
}

char tile_to_char(TILE t) {
    char c;
    switch (t) {
        case EMPTY:
            c = ' ';
            break;
        case INDESTRUCTIBLE_WALL:
            c = '#';
            break;
        case DESTRUCTIBLE_WALL:
            c = '@';
            break;
        case BOMB:
            c = 'o';
            break;
        case EXPLOSION:
            c = '+';
            break;
        case PLAYER_1:
            c = '1';
            break;
        case PLAYER_2:
            c = '2';
            break;
        case PLAYER_3:
            c = '3';
            break;
        case PLAYER_4:
            c = '4';
            break;
        case VERTICAL_BORDER:
            c = '|';
            break;
        case HORIZONTAL_BORDER:
            c = '-';
            break;
    }
    return c;
}

bool is_outside_board(int x, int y, unsigned int game_id) {
    if (games[game_id] == NULL) {
        return true;
    }
    board *game_board = games[game_id]->game_board;
    return x < 0 || x >= game_board->dim.width || y < 0 || y >= game_board->dim.height;
}

bool can_move_to_position(int x, int y, unsigned int game_id) {
    if (is_outside_board(x, y, game_id)) {
        return false;
    }
    TILE t = get_grid(x, y, game_id);
    return t != BOMB && t != INDESTRUCTIBLE_WALL && t != DESTRUCTIBLE_WALL && t != PLAYER_1 && t != PLAYER_2 &&
           t != PLAYER_3 && t != PLAYER_4;
}

coord int_to_coord(int n, unsigned int game_id) {
    board *game_board = games[game_id]->game_board;
    coord c;
    c.y = n / game_board->dim.width;
    c.x = n % game_board->dim.width;
    return c;
}

int coord_to_int_dim(int x, int y, dimension dim) {
    return y * dim.width + x;
}

int coord_to_int(int x, int y, unsigned int game_id) {
    board *game_board = games[game_id]->game_board;
    return coord_to_int_dim(x, y, game_board->dim);
}

TILE get_grid(int x, int y, unsigned int game_id) {
    RETURN_FAILURE_IF_NULL(games[game_id]);

    board *game_board = games[game_id]->game_board;
    if (game_board != NULL) {
        return game_board->grid[coord_to_int(x, y, game_id)];
    }
    return EXIT_FAILURE;
}

void set_grid(int x, int y, TILE v, unsigned int game_id) {
    RETURN_IF_NULL(games[game_id]);

    board *game_board = games[game_id]->game_board;
    if (game_board != NULL) {
        game_board->grid[coord_to_int(x, y, game_id)] = v;
    }
}

TILE get_player(int player_id) {
    switch (player_id) {
        case 0:
            return PLAYER_1;
        case 1:
            return PLAYER_2;
        case 2:
            return PLAYER_3;
        case 3:
            return PLAYER_4;
        default:
            return EMPTY;
    }
}

int get_player_id(TILE t) {
    switch (t) {
        case PLAYER_1:
            return 0;
        case PLAYER_2:
            return 1;
        case PLAYER_3:
            return 2;
        case PLAYER_4:
            return 3;
        default:
            return -1;
    }
}

bool is_move(GAME_ACTION action) {
    return action == GAME_LEFT || action == GAME_RIGHT || action == GAME_UP || action == GAME_DOWN ||
           action == GAME_NONE;
}

coord get_next_position(GAME_ACTION a, const coord *pos) {
    coord c;
    c.x = pos->x;
    c.y = pos->y;
    switch (a) {
        case GAME_LEFT:
            c.x--;
            break;
        case GAME_RIGHT:
            c.x++;
            break;
        case GAME_UP:
            c.y--;
            break;
        case GAME_DOWN:
            c.y++;
            break;
        default:
            break;
    }
    return c;
}

void perform_move(GAME_ACTION a, int player_id, unsigned int game_id) {
    RETURN_IF_NULL(games[game_id]);

    player **players = games[game_id]->players;

    if (players[player_id]->dead) {
        return;
    }

    coord *current_pos = players[player_id]->pos;
    coord old_pos = *current_pos;

    coord c = get_next_position(a, current_pos);
    if (!can_move_to_position(c.x, c.y, game_id)) {
        return;
    }
    current_pos->x = c.x;
    current_pos->y = c.y;
    set_grid(current_pos->x, current_pos->y, get_player(player_id), game_id);
    if (get_grid(old_pos.x, old_pos.y, game_id) != BOMB) {
        set_grid(old_pos.x, old_pos.y, EMPTY, game_id);
    }
}
void place_bomb(int player_id, unsigned int game_id) {

    RETURN_IF_NULL(games[game_id]);

    game *g = games[game_id];

    if (g->all_bombs.total_count == g->all_bombs.max_capacity) {
        int new_capacity = (g->all_bombs.max_capacity == 0) ? 4 : g->all_bombs.max_capacity * 2;
        bomb *new_list = realloc(g->all_bombs.arr, new_capacity * sizeof(bomb));
        RETURN_IF_NULL_PERROR(new_list, "realloc");

        g->all_bombs.arr = new_list;
        g->all_bombs.max_capacity = new_capacity;
    }

    player **players = games[game_id]->players;

    coord current_pos = *players[player_id]->pos;

    TILE t = get_grid(current_pos.x, current_pos.y, game_id);
    if (t == BOMB) { // Shouldn't be able to place a bomb on top of an another
        return;
    }

    bomb new_bomb;
    new_bomb.pos.x = current_pos.x;
    new_bomb.pos.y = current_pos.y;
    new_bomb.placement_time = time(NULL);

    g->all_bombs.arr[g->all_bombs.total_count] = new_bomb;
    g->all_bombs.total_count++;

    set_grid(current_pos.x, current_pos.y, BOMB, game_id);
}

board *get_game_board(unsigned int game_id) {
    RETURN_NULL_IF_NULL(games[game_id]);

    board *copy = malloc(sizeof(board));
    RETURN_NULL_IF_NULL_PERROR(copy, "malloc");

    board *game_board = games[game_id]->game_board;

    copy->dim.width = game_board->dim.width;
    copy->dim.height = game_board->dim.height;
    copy->grid = malloc(sizeof(char) * copy->dim.width * copy->dim.height);
    if (copy->grid == NULL) {
        perror("malloc");
        free(copy);
        return NULL;
    }

    for (int i = 0; i < copy->dim.width * copy->dim.height; i++) {
        copy->grid[i] = game_board->grid[i];
    }

    return copy;
}

GAME_MODE get_game_mode(unsigned int game_id) {
    return games[game_id]->game_mode;
}

bool is_player_dead(int id, unsigned int game_id) {
    if (games[game_id] == NULL) {
        return true;
    }

    return games[game_id]->players[id]->dead;
}

void set_player_dead(unsigned int game_id, int player_id) {
    if (games[game_id] == NULL) {
        return;
    }
    for (int i = 0; i < games[game_id]->game_board->dim.width * games[game_id]->game_board->dim.height; i++) {
        coord c = int_to_coord(i, game_id);
        if (get_grid(c.x, c.y, game_id) == get_player(player_id)) {
            set_grid(c.x, c.y, EMPTY, game_id);
            break;
        }
    }
    games[game_id]->players[player_id]->dead = true;
}

bool apply_explosion_effect(int x, int y, unsigned int game_id) {
    if (is_outside_board(x, y, game_id)) {
        return false;
    }

    bool impact_happened = false;

    player **players = games[game_id]->players;

    TILE t = get_grid(x, y, game_id);
    int id;
    switch (t) {
        case DESTRUCTIBLE_WALL:
            set_grid(x, y, EMPTY, game_id);
            impact_happened = true;
            break;
        case PLAYER_1:
        case PLAYER_2:
        case PLAYER_3:
        case PLAYER_4:
            id = get_player_id(t);
            players[id]->dead = true;
            set_grid(x, y, EMPTY, game_id);
            break;
        case INDESTRUCTIBLE_WALL:
            impact_happened = true;
            break;
        default:
            break;
    }

    // Check if player standing on a bomb is impacted by an another explosion
    if (!impact_happened) {
        coord *player_pos;
        for (int i = 0; i < PLAYER_NUM; ++i) {
            player_pos = players[i]->pos;
            if (player_pos->x == x && player_pos->y == y) {
                players[i]->dead = true;
            }
        }
    }

    return impact_happened;
}

void update_explosion(bomb b, unsigned int game_id) {
    RETURN_IF_NULL(games[game_id]);

    player **players = games[game_id]->players;

    int x, y;

    x = b.pos.x;
    y = b.pos.y;
    for (int i = 0; i < PLAYER_NUM; ++i) {
        if (players[i]->pos->x == x && players[i]->pos->y == y) {
            players[i]->dead = true;
        }
    }

    // Vertical center
    x = b.pos.x;
    for (int k = 0; k <= 2; ++k) {
        y = b.pos.y + k;
        if (apply_explosion_effect(x, y, game_id)) {
            break;
        }
    }

    x = b.pos.x;
    for (int k = 0; k >= -2; --k) {
        y = b.pos.y + k;
        if (apply_explosion_effect(x, y, game_id)) {
            break;
        }
    }

    // Horizontal center
    y = b.pos.y;
    for (int k = 0; k <= 2; ++k) {
        x = b.pos.x + k;
        if (apply_explosion_effect(x, y, game_id)) {
            break;
        }
    }

    x = b.pos.x;
    for (int k = 0; k >= -2; --k) {
        x = b.pos.x + k;
        if (apply_explosion_effect(x, y, game_id)) {
            break;
        }
    }

    // Diagonals
    x = b.pos.x;
    y = b.pos.y;
    apply_explosion_effect(x + 1, y + 1, game_id);
    apply_explosion_effect(x + 1, y - 1, game_id);
    apply_explosion_effect(x - 1, y + 1, game_id);
    apply_explosion_effect(x - 1, y - 1, game_id);
}

void update_bombs(unsigned int game_id) {
    RETURN_IF_NULL(games[game_id]);

    game *g = games[game_id];

    time_t current_time = time(NULL);

    for (int i = 0; i < g->all_bombs.total_count; ++i) {
        bomb b = g->all_bombs.arr[i];
        if (difftime(current_time, b.placement_time) >= BOMB_LIFETIME) {
            update_explosion(b, game_id);
            set_grid(b.pos.x, b.pos.y, EMPTY, game_id);

            // Get rid of the exploded bomb
            g->all_bombs.total_count -= 1;
            g->all_bombs.arr[i] = g->all_bombs.arr[g->all_bombs.total_count]; // no need to free memory
        }
    }
}

tile_diff *get_diff_with_board(unsigned game_id, board *different_board, unsigned *size_tile_diff) {
    board *current_board = games[game_id]->game_board;
    if (current_board->dim.height != different_board->dim.height ||
        current_board->dim.width != different_board->dim.width || size_tile_diff == NULL) {
        return NULL;
    }
    unsigned cmpt = 0;
    tile_diff diffs[current_board->dim.width * current_board->dim.height];
    for (int i = 0; i < current_board->dim.height * current_board->dim.width; i++) {
        if (current_board->grid[i] != different_board->grid[i]) {
            coord c = int_to_coord(i, game_id);

            tile_diff diff;
            diff.x = c.x;
            diff.y = c.y;
            diff.tile = current_board->grid[i];

            diffs[cmpt] = diff;
            cmpt++;
        }
    }
    tile_diff *res_diffs = malloc(sizeof(tile_diff) * cmpt);
    RETURN_NULL_IF_NULL(res_diffs);
    memmove(res_diffs, diffs, sizeof(tile_diff) * cmpt);
    *size_tile_diff = cmpt;
    return res_diffs;
}

tile_diff *update_game_board(unsigned game_id, player_action *actions, size_t nb_game_actions,
                             unsigned *size_tile_diff) {
    RETURN_NULL_IF_NULL(size_tile_diff);

    board *current_board = get_game_board(game_id);
    RETURN_NULL_IF_NULL(current_board);

    for (unsigned i = 0; i < nb_game_actions; i++) {
        if (actions[i].action == GAME_PLACE_BOMB) {
            place_bomb(actions[i].id, game_id);
        } else {
            perform_move(actions[i].action, actions[i].id, game_id);
        }
    }
    update_bombs(game_id);
    tile_diff *diffs = get_diff_with_board(game_id, current_board, size_tile_diff);
    free(current_board);
    return diffs;
}

bool is_game_over(unsigned int game_id) {
    if (games[game_id] == NULL) {
        return true;
    }

    player **players = games[game_id]->players;
    GAME_MODE game_mode = get_game_mode(game_id);

    if (game_mode == SOLO) {
        int alive_count = 0;
        for (int i = 0; i < PLAYER_NUM; ++i) {
            if (!players[i]->dead) {
                alive_count++;
            }
        }
        return alive_count <= 1;
    }

    // In team mode, the game is over when all players of a team are dead
    // The teams are always 0-3 and 1-2

    bool team1_dead = players[0]->dead && players[3]->dead;
    bool team2_dead = players[1]->dead && players[2]->dead;

    return team1_dead || team2_dead;
}

int get_winner_solo(unsigned int game_id) {
    if (games[game_id] == NULL) {
        return -1;
    }

    player **players = games[game_id]->players;

    for (int i = 0; i < PLAYER_NUM; i++) {
        for (int j = 0; j < PLAYER_NUM; j++) {
            if (i != j && !players[i]->dead && players[j]->dead) {
                return i;
            }
        }
    }

    return -1;
}

int get_winner_team(unsigned int game_id) {
    if (games[game_id] == NULL) {
        return -1;
    }

    player **players = games[game_id]->players;

    if (players[0]->dead && players[3]->dead) {
        return 1;
    }

    if (players[1]->dead && players[2]->dead) {
        return 0;
    }

    return -1;
}

chat *get_chat(unsigned int game_id) {
    RETURN_NULL_IF_NULL(games[game_id]);

    return games[game_id]->chat;
}
