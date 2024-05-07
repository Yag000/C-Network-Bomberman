#include "network_client.h"
#include "communication_client.h"
#include "utils.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *IP_SERVER = "::1";

static int sock_tcp = -1;
static int sock_udp = -1;
static int sock_diff = -1;

static uint16_t port_tcp = 0;
static uint16_t port_udp = 0;
static uint16_t port_diff = 0;

// TODO: fix this memory leak
static struct sockaddr_in6 *addr_udp;
static struct sockaddr_in6 *addr_diff;

static uint16_t adrmdiff[8];

static int id;
static int eq;

void close_socket(int sock) {
    if (sock != 0) {
        close(sock);
    }
}

void close_socket_tcp() {
    close(sock_tcp);
}

int init_socket(int *sock, bool is_tcp) {
    if (is_tcp) {
        *sock = socket(PF_INET6, SOCK_STREAM, 0);
    } else {
        *sock = socket(PF_INET6, SOCK_DGRAM, 0);
    }
    if (*sock < 0) {
        perror("socket creation");
        *sock = -1;
        return EXIT_FAILURE;
    }
    int option = 0; // Option value for setsockopt
    if (setsockopt(*sock, IPPROTO_IPV6, IPV6_V6ONLY, &option, sizeof(option)) < 0) {
        perror("setsockopt polymorphism");
        close_socket(*sock);
        *sock = -1;
        return EXIT_FAILURE;
    }
    option = 1;
    if (setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
        perror("setsockopt reuseaddr");
        close_socket(*sock);
        *sock = -1;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int init_tcp_socket() {
    return init_socket(&sock_tcp, true);
}

int init_udp_socket() {
    return init_socket(&sock_udp, false);
}

int init_diff_socket() {
    return init_socket(&sock_diff, false);
}

void set_tcp_port(uint16_t port) {
    port_tcp = htons(port);
}

struct sockaddr_in6 *prepare_address(int port) {
    struct sockaddr_in6 *addrsock = malloc(sizeof(struct sockaddr_in6));
    RETURN_NULL_IF_NULL_PERROR(addrsock, "malloc addrsock");
    memset(addrsock, 0, sizeof(struct sockaddr_in6));
    addrsock->sin6_family = AF_INET6;
    addrsock->sin6_port = port;
    inet_pton(AF_INET6, IP_SERVER, &addrsock->sin6_addr);
    return addrsock;
}

int try_to_connect_tcp() {
    struct sockaddr_in6 *addrsock = prepare_address(port_tcp);
    RETURN_FAILURE_IF_NULL(addrsock);
    return connect(sock_tcp, (struct sockaddr *)addrsock, sizeof(struct sockaddr_in6));
}

void set_server_informations(connection_information *head) {
    RETURN_IF_NULL(head);

    id = head->id;
    eq = head->eq;
    for (unsigned i = 0; i < 8; i++) {
        adrmdiff[i] = head->adrmdiff[i];
    }

    // TODO: fix this
    int dif_sock;

    /* créer la socket */
    if ((dif_sock = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
        perror("echec de socket");
        exit(1);
        return;
    }

    /* SO_REUSEADDR permet d'avoir plusieurs instances locales de cette application  */
    /* ecoutant sur le port multicast et recevant chacune les differents paquets       */
    int ok = 1;
    if (setsockopt(dif_sock, SOL_SOCKET, SO_REUSEADDR, &ok, sizeof(ok)) < 0) {
        perror("echec de SO_REUSEADDR");
        close(dif_sock);
        exit(1);
        return;
    }

    /* Initialisation de l'adresse de reception */
    struct sockaddr_in6 adr;
    memset(&adr, 0, sizeof(adr));
    adr.sin6_family = AF_INET6;
    adr.sin6_addr = in6addr_any;
    adr.sin6_port = htons(port_udp);

    if (bind(dif_sock, (struct sockaddr *)&adr, sizeof(adr))) {
        perror("echec de bind");
        close(dif_sock);
        exit(1);
        return;
    }

    /* initialisation de l'interface locale autorisant le multicast IPv6 */
    int ifindex = if_nametoindex("eth0");
    if (ifindex == 0) {
        perror("if_nametoindex");
        exit(1);
        return;
    }

    /* s'abonner au groupe multicast */
    struct ipv6_mreq group;
    char *addr_string = convert_adrmdif_into_string(adrmdiff);
    printf("Adresse multicast : %s\n", addr_string);
    inet_pton(AF_INET6, addr_string, &group.ipv6mr_multiaddr.s6_addr);
    free(addr_string);
    group.ipv6mr_interface = ifindex;

    if (setsockopt(dif_sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group, sizeof group) < 0) {
        perror("echec de abonnement groupe");
        close(dif_sock);
        exit(1);
        return;
    }

    port_udp = head->portudp;
    sock_udp = dif_sock;
    addr_udp = malloc(sizeof(struct sockaddr_in6));
    addr_udp->sin6_family = adr.sin6_family;
    addr_udp->sin6_port = adr.sin6_port;
    addr_udp->sin6_addr = adr.sin6_addr;

    sock_udp = dif_sock;

    RETURN_IF_NULL(addr_udp);

    port_diff = head->portmdiff;
    init_diff_socket();
    addr_diff = prepare_address(port_diff);
    RETURN_IF_NULL(addr_diff);
}

int start_initialisation_game(GAME_MODE mode) {
    RETURN_FAILURE_IF_ERROR(send_initial_connexion_information(sock_tcp, mode));
    printf("You have to wait for other players.\n");
    connection_information *head = recv_connexion_information(sock_tcp);
    RETURN_FAILURE_IF_NULL(head);
    set_server_informations(head);
    free(head);
    printf("The server is ready.\n");
    return EXIT_SUCCESS;
}

int send_ready_to_play(GAME_MODE mode) {
    return send_ready_connexion_information(sock_tcp, mode, id, eq);
}

char *recv_game_board_information(const udp_information *info, message_header *header) {
    uint16_t message_num;
    int res = recvfrom(info->sock, &message_num, sizeof(uint16_t), 0, (struct sockaddr *)info->addr, info->addr_len);
    RETURN_NULL_IF_NEG_PERROR(res, "recvfrom message_num");

    uint8_t height;
    res = recvfrom(info->sock, &height, sizeof(uint8_t), 0, (struct sockaddr *)info->addr, info->addr_len);
    RETURN_NULL_IF_NEG_PERROR(res, "recvfrom height");

    uint8_t width;
    res = recvfrom(info->sock, &width, sizeof(uint8_t), 0, (struct sockaddr *)info->addr, info->addr_len);
    RETURN_NULL_IF_NEG_PERROR(res, "recvfrom width");

    int message_size = height * width;
    int non_board_size = 2 + 2 + 1 + 1; // 2 for the header, 2 for the message_num, 1 for the height, 1 for the width

    char *message = malloc(message_size + non_board_size);
    RETURN_NULL_IF_NULL_PERROR(message, "malloc message");

    uint16_t header_serialized = serialize_message_header(header);
    memcpy(message, &header_serialized, 2);
    memcpy(message + 2, &message_num, 2);
    memcpy(message + 4, &height, 1);
    memcpy(message + 5, &width, 1);

    recvfrom_full(info, message + non_board_size, message_size);

    return message;
}

char *recv_game_update(const udp_information *info, message_header *header) {
    uint16_t message_num;
    int res = recvfrom(info->sock, &message_num, sizeof(uint16_t), 0, (struct sockaddr *)info->addr, info->addr_len);
    RETURN_NULL_IF_NEG_PERROR(res, "recvfrom message_num");

    uint8_t updated_tiles;
    res = recvfrom(info->sock, &updated_tiles, sizeof(uint8_t), 0, (struct sockaddr *)info->addr, info->addr_len);
    RETURN_NULL_IF_NEG_PERROR(res, "recvfrom updated_tiles");

    int message_size = updated_tiles * 3;
    int non_update_size = 2 + 2 + 1; // 2 for the header, 2 for the message_num, 1 for the number of updated tiles

    char *message = malloc(message_size + non_update_size);
    RETURN_NULL_IF_NULL_PERROR(message, "malloc message");

    uint16_t header_serialized = serialize_message_header(header);
    memcpy(message, &header_serialized, 2);
    memcpy(message + 2, &message_num, 2);
    memcpy(message + 4, &updated_tiles, 1);

    recvfrom_full(info, message + non_update_size, message_size);

    return message;
}

received_game_message *recv_game_message() {
    udp_information *info = malloc(sizeof(udp_information));
    RETURN_NULL_IF_NULL_PERROR(info, "malloc udp_information");

    info->sock = sock_udp;
    info->addr = (struct sockaddr *)addr_udp;
    info->addr_len = (socklen_t *)sizeof(struct sockaddr_in6);

    printf("Socket: %d\n", info->sock);
    char buffer[1024];
    inet_ntop(AF_INET6, &info->addr, buffer, 1024);
    printf("Address: %s\n", buffer);

    message_header *header = recv_header_multidiff(info);
    RETURN_NULL_IF_NULL(header);

    char *message = NULL;
    game_message_type type;

    switch (header->codereq) {
        case 11:
            message = recv_game_board_information(info, header);
            free(header);
            RETURN_NULL_IF_NULL(message);

            type = GAME_BOARD_INFORMATION;
            break;
        case 12:
            message = recv_game_update(info, header);
            free(header);
            RETURN_NULL_IF_NULL(message);

            type = GAME_BOARD_UPDATE;
            break;
        default:
            free(header);
            return NULL;
    }

    received_game_message *recieved = malloc(sizeof(received_game_message));
    RETURN_NULL_IF_NULL_PERROR(recieved, "malloc received_game_message");

    recieved->message = message;
    recieved->type = type;

    return recieved;
}

int send_game_action(game_action *action) {
    // TODO: fix this
    udp_information *info = malloc(sizeof(udp_information));
    RETURN_FAILURE_IF_NULL_PERROR(info, "malloc udp_information");

    info->sock = sock_diff;
    info->addr = (struct sockaddr *)addr_diff;
    info->addr_len = (socklen_t *)sizeof(struct sockaddr_in6);

    char *serialized = serialize_game_action(action);

    int sent = 0;

    while (sent < 4) { // 4 Bytes
        int res = sendto(info->sock, serialized + sent, 4 - sent, 0, (struct sockaddr *)info->addr, *info->addr_len);
        if (res < 0) {
            perror("sendto action");
            free(serialized);
            return EXIT_FAILURE;
        }
        sent += res;
    }

    return EXIT_SUCCESS;
}
