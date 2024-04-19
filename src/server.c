#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "model.h"
#include "network_server.h"
#include "utils.h"

typedef struct tcp_thread_data {
    unsigned id;
    pthread_t thread_id;
    bool finished_flag;
} tcp_thread_data;

static tcp_thread_data *tcp_threads_data_players[PLAYER_NUM];
static pthread_t game_threads[3];

pthread_mutex_t lock_waiting_all_players_join;
pthread_mutex_t lock_all_players_ready;
pthread_mutex_t lock_waiting_the_game_finish;

pthread_cond_t cond_lock_waiting_all_players_join;
pthread_cond_t cond_lock_all_players_ready;
pthread_cond_t cond_lock_waiting_the_game_finish;

static unsigned connected_player_number = 0;
static unsigned ready_player_number = 0;

#define LIMIT_LAST_NUM_MESSAGE_UDP 8192   // 2^13
#define LIMIT_LAST_NUM_MESSAGE_MULT 65536 // 2^16

static unsigned last_num_received_messages[PLAYER_NUM];
static unsigned last_num_freq_message = 0;
static unsigned last_num_sec_message = 0;

void *serve_client(void *);

void free_tcp_threads_data() {
    for (unsigned i = 0; i < connected_player_number; i++) {
        if (tcp_threads_data_players[i] != NULL) {
            free(tcp_threads_data_players[i]);
            tcp_threads_data_players[i] = NULL;
        }
    }
}

int init_server_network() {
    RETURN_FAILURE_IF_ERROR(init_socket_tcp());
    RETURN_FAILURE_IF_ERROR(init_socket_udp());
    RETURN_FAILURE_IF_ERROR(init_socket_mult());

    if (try_to_bind_random_port_on_socket_tcp() != EXIT_SUCCESS) {
        goto exit_closing_sockets;
    }
    if (try_to_bind_random_port_on_socket_udp() != EXIT_SUCCESS) {
        goto exit_closing_sockets;
    }
    if (init_random_port_on_socket_mult() != EXIT_SUCCESS) {
        goto exit_closing_sockets;
    }
    if (init_random_adrmdiff() == EXIT_FAILURE) {
        goto exit_closing_sockets;
    }
    return EXIT_SUCCESS;

exit_closing_sockets:
    close_socket_tcp();
    close_socket_udp();
    close_socket_mult();
    return EXIT_FAILURE;
}

int init_tcp_threads_data() {
    for (unsigned i = 0; i < PLAYER_NUM; i++) {
        tcp_threads_data_players[i] = malloc(sizeof(tcp_thread_data));

        if (tcp_threads_data_players[i] == NULL) {
            perror("malloc tcp_thread_data");
            for (unsigned j = 0; j < i; j++) {
                free(tcp_threads_data_players[j]);
            }
            return EXIT_FAILURE;
        }
        memset(&tcp_threads_data_players[i], 0, sizeof(tcp_thread_data));
    }
    return EXIT_SUCCESS;
}

void lock_mutex(pthread_mutex_t *mutex, pthread_cond_t *cond) {
    pthread_mutex_lock(mutex);
    pthread_cond_wait(cond, mutex);
    pthread_mutex_unlock(mutex);
}

void unlock_mutex_for_everyone(pthread_mutex_t *mutex, pthread_cond_t *cond) {
    pthread_mutex_lock(mutex);
    pthread_cond_broadcast(cond);
    pthread_mutex_unlock(mutex);
}

void wait_all_clients_not_ready() {
    if (ready_player_number < PLAYER_NUM) {
        pthread_cond_wait(&cond_lock_all_players_ready, &lock_all_players_ready);
    } else {
        pthread_cond_broadcast(&cond_lock_all_players_ready);
    }
    pthread_mutex_unlock(&lock_all_players_ready);
}

void init_connexion_with_client(tcp_thread_data *tcp_data) {
    // TODO separate solo and eq client
    initial_connection_header *head = recv_initial_connection_header_of_client(tcp_data->id);
    free(head);
    lock_mutex(&lock_waiting_all_players_join, &cond_lock_waiting_all_players_join);
    // TODO verify well send
    send_connexion_information_of_client(tcp_data->id, 0);
}

void print_ready_player(ready_connection_header *ready_informations) {
    printf("Player with Id : %d, eq : %d is ready.\n", ready_informations->id, ready_informations->eq);
}

void *serve_client(void *arg_tcp_thread_data) {
    tcp_thread_data *tcp_data = (tcp_thread_data *)arg_tcp_thread_data;

    init_connexion_with_client(tcp_data);

    // TODO verify ready_informations
    ready_connection_header *ready_informations = recv_ready_connexion_header_of_client(tcp_data->id);
    print_ready_player(ready_informations);
    pthread_mutex_lock(&lock_all_players_ready);
    ready_player_number++;

    wait_all_clients_not_ready();

    // TODO end of the game
    lock_mutex(&lock_waiting_the_game_finish, &cond_lock_waiting_the_game_finish);

    printf("Player %d left the game.\n", ready_informations->id);
    free(ready_informations);
    close_socket_client(tcp_data->id);
    // TODO keep free(tcp_data) if we don't use join
    // TO CONTINUE
    return NULL;
}

int connect_one_player_to_game(int id) {
    tcp_threads_data_players[id] = malloc(sizeof(tcp_threads_data_players));
    tcp_threads_data_players[id]->id = id;

    int res = try_to_init_socket_of_client(id);

    if (res < 0) {
        return EXIT_FAILURE;
    }

    if (pthread_create(&tcp_threads_data_players[id]->thread_id, NULL, serve_client, tcp_threads_data_players[id]) <
        0) {
        perror("thread creation");
        return EXIT_FAILURE;
    }
    connected_player_number++;
    return EXIT_SUCCESS;
}

int connect_player_to_game() {
    listen_players();
    printf("Waiting players on %u port.\n", get_port_tcp());
    while (connected_player_number < PLAYER_NUM) {
        connect_one_player_to_game(connected_player_number);
    }
    printf("All players are connected.\n");
    return EXIT_SUCCESS;
}

void join_tcp_threads() {
    for (unsigned i = 0; i < PLAYER_NUM; i++) {
        pthread_join(tcp_threads_data_players[i]->thread_id, NULL);
    }
    free_tcp_threads_data();
}

void increment_last_num_freq_message() {
    last_num_freq_message = last_num_freq_message + 1 % LIMIT_LAST_NUM_MESSAGE_MULT;
}

void increment_last_num_sec_message() {
    last_num_sec_message = last_num_sec_message + 1 % LIMIT_LAST_NUM_MESSAGE_MULT;
}

void increment_last_num_received_messages(unsigned id) {
    if (id >= PLAYER_NUM) {
        return;
    }
    last_num_received_messages[id] = last_num_received_messages[id] + 1 % LIMIT_LAST_NUM_MESSAGE_UDP;
}

int main() {
    srandom(time(NULL));
    int return_value = EXIT_SUCCESS;
    RETURN_FAILURE_IF_ERROR(init_server_network());
    if (connect_player_to_game() != EXIT_SUCCESS) {
        return_value = EXIT_FAILURE;
        goto exit_closing_sockets;
    }
    sleep(1); // Wait all clients for the join mutex
    unlock_mutex_for_everyone(&lock_waiting_all_players_join, &cond_lock_waiting_all_players_join);
    join_tcp_threads();
    goto exit_closing_sockets;

exit_closing_sockets:
    close_socket_tcp();
    close_socket_udp();
    close_socket_mult();
    return return_value;
}
