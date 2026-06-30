/*
 * server.c — Cross-platform CLI group chat server
 * Compiles on macOS/Linux (POSIX) and Windows (Winsock).
 *
 * Build:
 *   macOS/Linux : gcc server.c -o server
 *   Windows     : gcc server.c -o server -lws2_32
 *
 * Run:
 *   ./server            (listens on port 8080)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Platform abstraction layer ---- */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
    #define CLOSESOCK(s) closesocket(s)
    #define SOCK_ERR    SOCKET_ERROR
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/select.h>
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define CLOSESOCK(s) close(s)
    #define SOCK_ERR    (-1)
#endif

#define PORT        8080
#define MAX_CLIENTS 16
#define BUF         1024
#define NAME_LEN    32

/* ANSI colors assigned per client so each user shows in a distinct color */
const char *COLORS[] = {
    "\x1b[31m", "\x1b[32m", "\x1b[33m", "\x1b[34m",
    "\x1b[35m", "\x1b[36m", "\x1b[91m", "\x1b[92m"
};
#define NUM_COLORS 8
#define RESET "\x1b[0m"

typedef struct {
    SOCKET fd;
    char   name[NAME_LEN];
    int    color;       /* index into COLORS */
    int    active;
    int    named;       /* has the user sent their name yet? */
} Client;

Client clients[MAX_CLIENTS];

/* Send a message to a single client */
void send_to(SOCKET fd, const char *msg) {
    send(fd, msg, (int)strlen(msg), 0);
}

/* Broadcast to everyone except `except` (pass INVALID_SOCKET to send to all) */
void broadcast(const char *msg, SOCKET except) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].named && clients[i].fd != except) {
            send_to(clients[i].fd, msg);
        }
    }
}

/* Find a client slot by name; returns index or -1 */
int find_by_name(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].named &&
            strcmp(clients[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Find client index by socket fd */
int find_by_fd(SOCKET fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].fd == fd) return i;
    }
    return -1;
}

/* Trim trailing newline/carriage-return */
void trim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) {
        s[--n] = '\0';
    }
}

/* Handle a slash command. Returns 1 if the input was a command. */
int handle_command(int ci, char *line) {
    if (line[0] != '/') return 0;

    char out[BUF + NAME_LEN];

    /* /list — show online users */
    if (strncmp(line, "/list", 5) == 0) {
        char list[BUF] = "Online users: ";
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && clients[i].named) {
                strncat(list, clients[i].name, sizeof(list) - strlen(list) - 1);
                strncat(list, " ", sizeof(list) - strlen(list) - 1);
            }
        }
        strncat(list, "\n", sizeof(list) - strlen(list) - 1);
        send_to(clients[ci].fd, list);
        return 1;
    }

    /* /nick <newname> — change username */
    if (strncmp(line, "/nick ", 6) == 0) {
        char *newname = line + 6;
        trim(newname);
        if (strlen(newname) == 0 || strlen(newname) >= NAME_LEN) {
            send_to(clients[ci].fd, "Invalid name.\n");
            return 1;
        }
        if (find_by_name(newname) != -1) {
            send_to(clients[ci].fd, "Name already taken.\n");
            return 1;
        }
        snprintf(out, sizeof(out), "* %s is now known as %s\n",
                 clients[ci].name, newname);
        strncpy(clients[ci].name, newname, NAME_LEN - 1);
        clients[ci].name[NAME_LEN - 1] = '\0';
        broadcast(out, INVALID_SOCKET);
        return 1;
    }

    /* /pm <user> <message> — private message */
    if (strncmp(line, "/pm ", 4) == 0) {
        char *rest = line + 4;
        char *space = strchr(rest, ' ');
        if (!space) {
            send_to(clients[ci].fd, "Usage: /pm <user> <message>\n");
            return 1;
        }
        *space = '\0';
        char *target = rest;
        char *msg = space + 1;
        int ti = find_by_name(target);
        if (ti == -1) {
            send_to(clients[ci].fd, "User not found.\n");
            return 1;
        }
        snprintf(out, sizeof(out), "%s[PM from %s]%s %s\n",
                 COLORS[clients[ci].color], clients[ci].name, RESET, msg);
        send_to(clients[ti].fd, out);
        snprintf(out, sizeof(out), "%s[PM to %s]%s %s\n",
                 COLORS[clients[ci].color], clients[ti].name, RESET, msg);
        send_to(clients[ci].fd, out);
        return 1;
    }

    /* /help */
    if (strncmp(line, "/help", 5) == 0) {
        send_to(clients[ci].fd,
            "Commands: /list  /nick <name>  /pm <user> <msg>  /quit\n");
        return 1;
    }

    send_to(clients[ci].fd, "Unknown command. Try /help\n");
    return 1;
}

int main(void) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }
#endif

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) { printf("socket() failed\n"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCK_ERR) {
        printf("bind() failed\n"); return 1;
    }
    if (listen(server_fd, MAX_CLIENTS) == SOCK_ERR) {
        printf("listen() failed\n"); return 1;
    }

    printf("Server listening on port %d...\n", PORT);

    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].active = 0;

    fd_set read_fds;
    char buffer[BUF];

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        SOCKET maxfd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                FD_SET(clients[i].fd, &read_fds);
                if (clients[i].fd > maxfd) maxfd = clients[i].fd;
            }
        }

        if (select((int)maxfd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            printf("select() error\n");
            break;
        }

        /* New connection */
        if (FD_ISSET(server_fd, &read_fds)) {
            SOCKET new_fd = accept(server_fd, NULL, NULL);
            if (new_fd != INVALID_SOCKET) {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (!clients[i].active) { slot = i; break; }
                }
                if (slot == -1) {
                    send_to(new_fd, "Server full.\n");
                    CLOSESOCK(new_fd);
                } else {
                    clients[slot].fd = new_fd;
                    clients[slot].active = 1;
                    clients[slot].named = 0;
                    clients[slot].color = slot % NUM_COLORS;
                    send_to(new_fd, "Enter your name: ");
                }
            }
        }

        /* Existing clients */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) continue;
            if (!FD_ISSET(clients[i].fd, &read_fds)) continue;

            memset(buffer, 0, BUF);
            int n = recv(clients[i].fd, buffer, BUF - 1, 0);

            if (n <= 0) {
                /* Disconnect */
                if (clients[i].named) {
                    char out[BUF];
                    snprintf(out, sizeof(out), "* %s left the chat\n",
                             clients[i].name);
                    broadcast(out, clients[i].fd);
                    printf("%s disconnected.\n", clients[i].name);
                }
                CLOSESOCK(clients[i].fd);
                clients[i].active = 0;
                continue;
            }

            buffer[n] = '\0';
            trim(buffer);
            if (strlen(buffer) == 0) continue;

            /* First message = the chosen username */
            if (!clients[i].named) {
                if (find_by_name(buffer) != -1) {
                    send_to(clients[i].fd,
                            "Name taken. Enter your name: ");
                    continue;
                }
                strncpy(clients[i].name, buffer, NAME_LEN - 1);
                clients[i].name[NAME_LEN - 1] = '\0';
                clients[i].named = 1;

                char out[BUF];
                snprintf(out, sizeof(out),
                    "Welcome %s! Type /help for commands.\n",
                    clients[i].name);
                send_to(clients[i].fd, out);

                snprintf(out, sizeof(out), "* %s joined the chat\n",
                         clients[i].name);
                broadcast(out, clients[i].fd);
                printf("%s joined.\n", clients[i].name);
                continue;
            }

            /* /quit */
            if (strcmp(buffer, "/quit") == 0) {
                char out[BUF];
                snprintf(out, sizeof(out), "* %s left the chat\n",
                         clients[i].name);
                broadcast(out, clients[i].fd);
                printf("%s quit.\n", clients[i].name);
                CLOSESOCK(clients[i].fd);
                clients[i].active = 0;
                continue;
            }

            /* Command? */
            if (handle_command(i, buffer)) continue;

            /* Normal message — broadcast with colored username */
            char out[BUF + NAME_LEN + 16];
            snprintf(out, sizeof(out), "%s%s:%s %s\n",
                     COLORS[clients[i].color], clients[i].name,
                     RESET, buffer);
            broadcast(out, clients[i].fd);
        }
    }

    CLOSESOCK(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}