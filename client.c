/*
 * enhanced_client.c — Professional CLI chat client
 * Build: gcc enhanced_client.c -o client -lpthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    typedef SOCKET sock_t;
    #define CLOSESOCK(s) closesocket(s)
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <pthread.h>
    typedef int sock_t;
    #define INVALID_SOCKET (-1)
    #define CLOSESOCK(s) close(s)
#endif

#define PORT 8080
#define BUF 2048

sock_t sock;
volatile int running = 1;

/* Color codes */
#define GREEN "\x1b[32m"
#define BLUE "\x1b[34m"
#define YELLOW "\x1b[33m"
#define RED "\x1b[31m"
#define RESET "\x1b[0m"
#define BOLD "\x1b[1m"

#ifdef _WIN32
DWORD WINAPI reader(LPVOID arg) {
#else
void *reader(void *arg) {
#endif
    char buffer[BUF];
    while (running) {
        memset(buffer, 0, BUF);
        int n = recv(sock, buffer, BUF - 1, 0);
        if (n <= 0) {
            printf("\n%s[!] Disconnected%s\n", RED, RESET);
            running = 0;
            break;
        }
        buffer[n] = '\0';
        printf("%s", buffer);
        fflush(stdout);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("%sUsage: %s <server_ip>%s\n", BOLD, argv[0], RESET);
        printf("Example: %s 127.0.0.1\n", argv[0]);
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        printf("%s[ERROR] Socket failed%s\n", RED, RESET);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) <= 0) {
        printf("%s[ERROR] Invalid address%s\n", RED, RESET);
        return 1;
    }

    printf("%s[*] Connecting to %s:%d...%s\n", YELLOW, argv[1], PORT, RESET);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("%s[ERROR] Connection refused%s\n", RED, RESET);
        return 1;
    }
    printf("%s[✓] Connected!%s\n\n", GREEN, RESET);

#ifdef _WIN32
    HANDLE th = CreateThread(NULL, 0, reader, NULL, 0, NULL);
#else
    pthread_t th;
    pthread_create(&th, NULL, reader, NULL);
#endif

    char line[BUF];
    while (running) {
        printf("%s> %s", BLUE, RESET);
        if (fgets(line, BUF, stdin) == NULL) break;
        if (!running) break;

        if (send(sock, line, (int)strlen(line), 0) < 0) {
            printf("%s[ERROR] Send failed%s\n", RED, RESET);
            break;
        }

        if (strncmp(line, "/quit", 5) == 0) {
            printf("%s[*] Goodbye!%s\n", YELLOW, RESET);
            running = 0;
            break;
        }
    }

    running = 0;
    CLOSESOCK(sock);

#ifdef _WIN32
    WaitForSingleObject(th, 1000);
    WSACleanup();
#else
    pthread_join(th, NULL);
#endif

    return 0;
}