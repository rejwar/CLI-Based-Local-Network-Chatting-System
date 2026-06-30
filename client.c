s/*
 * client.c — Cross-platform CLI group chat client
 * Compiles on macOS/Linux (POSIX) and Windows (Winsock).
 *
 * Why a thread? On Windows, select() does NOT work on stdin (keyboard).
 * So instead of select(), we run one thread that blocks on recv() (network)
 * and the main thread blocks on fgets() (keyboard). Clean on both OSes.
 *
 * Build:
 *   macOS/Linux : gcc client.c -o client -lpthread
 *   Windows     : gcc client.c -o client -lws2_32
 *
 * Run:
 *   ./client 127.0.0.1        (same machine)
 *   ./client 192.168.0.105    (other machine's LAN IP)
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
#define BUF  1024

sock_t sock;
volatile int running = 1;

/* Reader: continuously receive from server and print. Runs in its own thread. */
#ifdef _WIN32
DWORD WINAPI reader(LPVOID arg) {
#else
void *reader(void *arg) {
#endif
    (void)arg;
    char buffer[BUF];
    while (running) {
        memset(buffer, 0, BUF);
        int n = recv(sock, buffer, BUF - 1, 0);
        if (n <= 0) {
            printf("\nDisconnected from server.\n");
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
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }
#endif

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) { printf("socket() failed\n"); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) <= 0) {
        printf("Invalid address: %s\n", argv[1]);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("connect: Connection refused (is the server running? right IP?)\n");
        return 1;
    }

    /* Start the network reader thread */
#ifdef _WIN32
    HANDLE th = CreateThread(NULL, 0, reader, NULL, 0, NULL);
#else
    pthread_t th;
    pthread_create(&th, NULL, reader, NULL);
#endif

    /* Main thread: read keyboard, send to server */
    char line[BUF];
    while (running) {
        if (fgets(line, BUF, stdin) == NULL) break;
        if (!running) break;
        if (send(sock, line, (int)strlen(line), 0) < 0) {
            printf("Send failed.\n");
            break;
        }
        if (strncmp(line, "/quit", 5) == 0) {
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