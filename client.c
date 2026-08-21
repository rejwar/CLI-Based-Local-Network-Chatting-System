#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>

#define RESET   "\x1b[0m"
#define RED     "\x1b[31m"
#define GREEN   "\x1b[32m"
#define YELLOW  "\x1b[33m"
#define BLUE    "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN    "\x1b[36m"
#define WHITE   "\x1b[37m"
#define BOLD    "\x1b[1m"
#define DIM     "\x1b[2m"

#define BUFFER_SIZE 2048
#define MAX_MESSAGES 100

typedef struct {
    char sender[50];
    char content[512];
    char timestamp[20];
    int is_read;
} Message;

int sock;
char current_user[50];
Message messages[MAX_MESSAGES];
int message_count = 0;

void clear_screen() {
    printf("\033[2J\033[H");
}

void print_header() {
    printf("%s%s", BOLD, CYAN);
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  %s💬 CLI CHAT APP v2.0%s                          │\n", BOLD, CYAN);
    printf("║  %s%s (online) | Status: online | 🟢 Active%s              ║\n", 
           GREEN, current_user, CYAN);
    printf("╚════════════════════════════════════════════════════════════╝%s\n", RESET);
}

void print_separator() {
    printf("%s", CYAN);
    printf("────────────────────────────────────────────────────────────\n");
    printf("%s", RESET);
}

void print_message(const char* sender, const char* content, const char* time, int is_read) {
    char* color = (strcmp(sender, current_user) == 0) ? BLUE : GREEN;
    
    if (strcmp(sender, current_user) == 0) {
        printf("%s[%s %s]%s ", DIM, time, sender, RESET);
        printf("%s┌─────────────────────────────────┐%s\n", BLUE, RESET);
        printf("%s│ %s%s", BLUE, content, RESET);
        printf("%s%s │%s", BLUE, (strlen(content) < 25) ? "                " : "", RESET);
        if (is_read) {
            printf(" %s✓✓%s", GREEN, RESET);
        } else {
            printf(" %s✓%s", CYAN, RESET);
        }
        printf("\n");
        printf("%s└─────────────────────────────────┘%s\n", BLUE, RESET);
    } else {
        printf("%s🟢 %s [%s]%s\n", GREEN, sender, time, RESET);
        printf("%s┌─────────────────────────────────┐%s\n", GREEN, RESET);
        printf("%s│ %s%s", GREEN, content, RESET);
        printf("%s%s │%s\n", GREEN, (strlen(content) < 25) ? "                " : "", RESET);
        printf("%s└─────────────────────────────────┘%s\n", GREEN, RESET);
    }
}

void print_input_area() {
    printf("\n%s", CYAN);
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║ %s> Type message...%s", YELLOW, RESET);
    printf("%s                              │\n", CYAN);
    printf("║ %s[/help] [/list] [/pm user msg] [/search word]%s   │\n", YELLOW, CYAN);
    printf("╚════════════════════════════════════════════════════════════╝%s\n", RESET);
}

void print_online_users(const char* user_list) {
    printf("\n%s%s", BOLD, CYAN);
    printf("📋 ONLINE USERS:\n");
    printf("%s", RESET);
    
    if (strcmp(user_list, "") == 0) {
        printf("No other users online\n");
    } else {
        char* copy = malloc(strlen(user_list) + 1);
        strcpy(copy, user_list);
        char* token = strtok(copy, ",");
        
        while (token != NULL) {
            if (strcmp(token, current_user) != 0) {
                printf("%s  🟢 %s%s\n", GREEN, token, RESET);
            }
            token = strtok(NULL, ",");
        }
        free(copy);
    }
}

void print_notification(const char* title, const char* message) {
    printf("\n%s%s", BOLD, YELLOW);
    printf("╔════════════════════════════════╗\n");
    printf("║ %s %s%s", "📬", title, RESET);
    printf("%s                    ║\n", YELLOW);
    printf("║ %s%s", message, RESET);
    printf("%s                              ║\n", YELLOW);
    printf("╚════════════════════════════════╝%s\n", RESET);
}

void display_chat() {
    clear_screen();
    print_header();
    print_separator();
    
    if (message_count > 0) {
        for (int i = 0; i < message_count; i++) {
            print_message(messages[i].sender, messages[i].content, 
                        messages[i].timestamp, messages[i].is_read);
        }
    } else {
        printf("%s No messages yet. Start chatting!%s\n", DIM, RESET);
    }
    
    print_separator();
    print_input_area();
}

void* receive_messages(void* arg) {
    char buffer[BUFFER_SIZE];
    
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes > 0) {
            buffer[bytes] = '\0';
            
            // Parse message format: [HH:MM:SS] sender: content
            char timestamp[20], sender[50], content[512];
            sscanf(buffer, "[%[^]]] %[^:]: %[^\n]", timestamp, sender, content);
            
            if (strlen(sender) > 0 && strlen(content) > 0) {
                strcpy(messages[message_count].sender, sender);
                strcpy(messages[message_count].content, content);
                strcpy(messages[message_count].timestamp, timestamp);
                messages[message_count].is_read = 0;
                message_count++;
                
                if (message_count >= MAX_MESSAGES) {
                    for (int i = 0; i < MAX_MESSAGES - 1; i++) {
                        messages[i] = messages[i + 1];
                    }
                    message_count = MAX_MESSAGES - 1;
                }
                
                display_chat();
            }
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }
    
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char input[BUFFER_SIZE];
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("%sError: Could not create socket%s\n", RED, RESET);
        return 1;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("%sError: Could not connect to server%s\n", RED, RESET);
        return 1;
    }
    
    // Get username
    clear_screen();
    printf("%s%s", BOLD, CYAN);
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Welcome to CLI CHAT APP v2.0                              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝%s\n", RESET);
    printf("%sEnter username: %s", YELLOW, RESET);
    fgets(current_user, sizeof(current_user), stdin);
    current_user[strcspn(current_user, "\n")] = 0;
    
    // Send username
    send(sock, current_user, strlen(current_user), 0);
    
    // Print welcome message
    print_notification("Connected!", "You are now in the chat!");
    sleep(1);
    
    // Start receive thread
    pthread_t receive_thread;
    pthread_create(&receive_thread, NULL, receive_messages, NULL);
    
    display_chat();
    
    // Main input loop
    while (1) {
        printf("%s%s> %s", YELLOW, current_user, RESET);
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = 0;
        
        if (strlen(input) > 0) {
            send(sock, input, strlen(input), 0);
            
            // Add user's message to display
            strcpy(messages[message_count].sender, current_user);
            strcpy(messages[message_count].content, input);
            time_t now = time(NULL);
            strftime(messages[message_count].timestamp, 20, "%H:%M:%S", localtime(&now));
            messages[message_count].is_read = 1;
            message_count++;
            
            if (message_count >= MAX_MESSAGES) {
                for (int i = 0; i < MAX_MESSAGES - 1; i++) {
                    messages[i] = messages[i + 1];
                }
                message_count = MAX_MESSAGES - 1;
            }
            
            display_chat();
            
            if (strcmp(input, "/quit") == 0) {
                print_notification("Goodbye!", "See you later!");
                sleep(1);
                break;
            }
        }
    }
    
    close(sock);
    return 0;
}