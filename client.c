/*
 * ============================================================================
 * ADVANCED CHAT CLIENT v2.5 - FULL FEATURED
 * ============================================================================
 * 
 * FEATURES IMPLEMENTED:
 * - Real-time message receiving in separate thread
 * - Persistent message history (200 messages in memory)
 * - ANSI color terminal UI
 * - Support for all 28+ server commands
 * - Automatic message parsing and display
 * - User input handling with prompts
 * - Connection status tracking
 * - Graceful disconnection handling
 * 
 * BUILD: gcc client_full.c -o client -pthread
 * RUN: ./client <server_ip>
 * Example: ./client localhost
 *          ./client 192.168.1.100
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

/* ========================================================================
   CONFIGURATION AND CONSTANTS
   ======================================================================== */

#define SERVER_PORT 8080
#define BUFFER_SIZE 2048
#define MAX_USERNAME_LEN 64
#define MAX_MESSAGES 200  /* Store 200 messages in memory */
#define MESSAGE_LEN 512

/* ANSI Color Codes for terminal formatting */
#define RESET       "\x1b[0m"
#define BOLD        "\x1b[1m"
#define DIM         "\x1b[2m"
#define RED         "\x1b[31m"
#define GREEN       "\x1b[32m"
#define YELLOW      "\x1b[33m"
#define BLUE        "\x1b[34m"
#define MAGENTA     "\x1b[35m"
#define CYAN        "\x1b[36m"
#define BRIGHT_RED  "\x1b[91m"
#define BRIGHT_GREEN "\x1b[92m"

/* ========================================================================
   DATA STRUCTURES - MESSAGE STORAGE
   ======================================================================== */

/* Structure to store individual chat message in memory */
typedef struct {
    char sender[MAX_USERNAME_LEN];  /* Who sent the message */
    char content[MESSAGE_LEN];      /* Message content */
    char timestamp[20];             /* Message timestamp HH:MM:SS */
    int is_own_message;             /* Is this our message? 1=yes, 0=no */
} ChatMessage;

/* ========================================================================
   GLOBAL VARIABLES - CLIENT STATE
   ======================================================================== */

static int server_socket = -1;                              /* Connection to server */
static char current_username[MAX_USERNAME_LEN];             /* Our username */
static ChatMessage message_buffer[MAX_MESSAGES];            /* Message history */
static int message_count = 0;                               /* How many messages stored */
static pthread_mutex_t message_buffer_lock = PTHREAD_MUTEX_INITIALIZER;  /* Thread safety */

/* ========================================================================
   TERMINAL UI FUNCTIONS - DISPLAY AND FORMATTING
   ======================================================================== */

/* Clear terminal screen and move cursor to top */
void clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

/* Display header banner with user info */
void print_header(void) {
    printf("%s%s", BOLD, CYAN);
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║  💬 ADVANCED CHAT CLIENT v2.5                        ║\n");
    printf("║  28 Features • Full Command Support                 ║\n");
    printf("║  User: %-41s ║\n", current_username);
    printf("║  Type /help for commands                             ║\n");
    printf("╚═══════════════════════════════════════════════════════╝%s\n\n", RESET);
}

/* Draw horizontal separator line */
void print_separator(void) {
    printf("%s───────────────────────────────────────────────────────%s\n", CYAN, RESET);
}

/* Display all messages in message buffer */
void display_messages(void) {
    clear_screen();
    print_header();
    print_separator();
    
    pthread_mutex_lock(&message_buffer_lock);
    
    if (message_count == 0) {
        printf("%sNo messages yet. Start chatting!%s\n", DIM, RESET);
    } else {
        /* Display each message with color coding */
        for (int i = 0; i < message_count; i++) {
            if (message_buffer[i].is_own_message) {
                /* Our messages in blue */
                printf("%s[%s YOU]%s %s\n", 
                       BLUE, message_buffer[i].timestamp, RESET, 
                       message_buffer[i].content);
            } else {
                /* Other messages in green */
                printf("%s[%s %s]%s %s\n",
                       GREEN, message_buffer[i].timestamp, 
                       message_buffer[i].sender, RESET, 
                       message_buffer[i].content);
            }
        }
    }
    
    pthread_mutex_unlock(&message_buffer_lock);
    print_separator();
}

/* Display input prompt */
void show_prompt(void) {
    printf("%s%s> %s", YELLOW, current_username, RESET);
    fflush(stdout);
}

/* ========================================================================
   MESSAGE BUFFER MANAGEMENT
   ======================================================================== */

/* Add message to in-memory buffer */
void add_message_to_buffer(const char *sender, const char *content, 
                          const char *timestamp, int is_own) {
    pthread_mutex_lock(&message_buffer_lock);
    
    if (message_count < MAX_MESSAGES) {
        /* Buffer not full - add new message */
        strncpy(message_buffer[message_count].sender, sender, MAX_USERNAME_LEN - 1);
        strncpy(message_buffer[message_count].content, content, MESSAGE_LEN - 1);
        strncpy(message_buffer[message_count].timestamp, timestamp, 19);
        message_buffer[message_count].is_own_message = is_own;
        message_count++;
    } else {
        /* Buffer full - shift messages and add new one at end */
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {
            message_buffer[i] = message_buffer[i + 1];
        }
        strncpy(message_buffer[MAX_MESSAGES - 1].sender, sender, MAX_USERNAME_LEN - 1);
        strncpy(message_buffer[MAX_MESSAGES - 1].content, content, MESSAGE_LEN - 1);
        strncpy(message_buffer[MAX_MESSAGES - 1].timestamp, timestamp, 19);
        message_buffer[MAX_MESSAGES - 1].is_own_message = is_own;
    }
    
    pthread_mutex_unlock(&message_buffer_lock);
}

/* ========================================================================
   NETWORK COMMUNICATION FUNCTIONS
   ======================================================================== */

/* Send message to server */
void send_to_server(const char *message) {
    if (message && strlen(message) > 0 && server_socket != -1) {
        send(server_socket, message, strlen(message), 0);
    }
}

/* ========================================================================
   RECEIVING THREAD - Runs in background to receive messages
   ======================================================================== */

/* Thread function: Receive messages from server continuously */
void *receive_thread_function(void *argument) {
    (void)argument;  /* Unused parameter */
    
    char receive_buffer[BUFFER_SIZE];
    
    while (server_socket != -1) {
        memset(receive_buffer, 0, BUFFER_SIZE);
        int bytes = recv(server_socket, receive_buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes > 0) {
            receive_buffer[bytes] = '\0';
            
            /* Try to parse message in format [HH:MM:SS] sender: content */
            char timestamp[20], sender[MAX_USERNAME_LEN], content[MESSAGE_LEN];
            int parsed = sscanf(receive_buffer, "[%[^]]] %[^:]: %[^\n]", 
                               timestamp, sender, content);
            
            if (parsed == 3 && strlen(sender) > 0 && strlen(content) > 0) {
                /* Successfully parsed as formatted message */
                int is_own = (strcmp(sender, current_username) == 0);
                add_message_to_buffer(sender, content, timestamp, is_own);
                display_messages();
                show_prompt();
            } else {
                /* Server notification or other message */
                printf("\n%s%s%s\n", YELLOW, receive_buffer, RESET);
                show_prompt();
            }
        } else if (bytes == 0) {
            /* Server disconnected */
            printf("%sServer disconnected%s\n", RED, RESET);
            break;
        }
    }
    
    return NULL;
}

/* ========================================================================
   HELP AND COMMAND INFORMATION
   ======================================================================== */

void display_help(void) {
    clear_screen();
    printf("%s%s", BOLD, CYAN);
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║  AVAILABLE COMMANDS (28 Features)                   ║\n");
    printf("╚═══════════════════════════════════════════════════════╝%s\n\n", RESET);
    
    printf("%s--- USER PROFILE ---%s\n", YELLOW, RESET);
    printf("  /profile          Display your profile\n");
    printf("  /setbio <text>    Set your biography\n");
    printf("  /setstatus <text> Update status (online/away/offline)\n");
    printf("  /setcolor <0-7>   Change message color\n");
    printf("  /userinfo <name>  Get info about a user\n\n");
    
    printf("%s--- MESSAGING ---%s\n", YELLOW, RESET);
    printf("  /pm <user> <msg>  Send private message\n");
    printf("  /mention @user    Mention specific user\n");
    printf("  /react <id> <e>   Add emoji reaction\n");
    printf("  /edit <id> <text> Edit message\n");
    printf("  /delete <id>      Delete message\n");
    printf("  /pin <id>         Pin important message\n\n");
    
    printf("%s--- HISTORY & SEARCH ---%s\n", YELLOW, RESET);
    printf("  /history [n]      Show last N messages (default 10)\n");
    printf("  /search <text>    Search messages\n");
    printf("  /export           Save chat to file\n");
    printf("  /clear            Clear screen\n\n");
    
    printf("%s--- GROUPS ---%s\n", YELLOW, RESET);
    printf("  /group create <n> Create new group\n");
    printf("  /group join <n>   Join existing group\n\n");
    
    printf("%s--- ADMINISTRATION ---%s\n", YELLOW, RESET);
    printf("  /list             List all online users\n");
    printf("  /stats            Show statistics\n");
    printf("  /broadcast <msg>  Send announcement\n");
    printf("  /mute <user>      Mute user\n");
    printf("  /unmute <user>    Unmute user\n\n");
    
    printf("%s--- INFORMATION ---%s\n", YELLOW, RESET);
    printf("  /count            Count online users\n");
    printf("  /time             Show server time\n");
    printf("  /whoami           Show your username\n");
    printf("  /commands         List all commands\n\n");
    
    printf("%s--- CONNECTION ---%s\n", YELLOW, RESET);
    printf("  /help             Show this help\n");
    printf("  /quit             Exit chat\n\n");
    
    printf("%sPress ENTER to continue...%s", YELLOW, RESET);
    getchar();
}

/* ========================================================================
   MAIN PROGRAM - Initialization and Input Loop
   ======================================================================== */

int main(int argc, char *argv[]) {
    /* Check command line arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        fprintf(stderr, "Example: %s localhost\n", argv[0]);
        fprintf(stderr, "Example: %s 192.168.1.100\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    /* Create socket for server connection */
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        fprintf(stderr, "%sError: Could not create socket%s\n", RED, RESET);
        return EXIT_FAILURE;
    }
    
    /* Set up server address structure */
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    
    /* Convert IP address string to binary form */
    if (inet_pton(AF_INET, argv[1], &server_address.sin_addr) <= 0) {
        fprintf(stderr, "%sError: Invalid server address%s\n", RED, RESET);
        close(server_socket);
        return EXIT_FAILURE;
    }
    
    /* Connect to server */
    if (connect(server_socket, (struct sockaddr *)&server_address, 
               sizeof(server_address)) < 0) {
        fprintf(stderr, "%sError: Could not connect to server at %s:%d%s\n", 
               RED, argv[1], SERVER_PORT, RESET);
        close(server_socket);
        return EXIT_FAILURE;
    }
    
    /* Display welcome screen */
    clear_screen();
    printf("%s%s", BOLD, CYAN);
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║  🚀 ADVANCED CHAT CLIENT v2.5                        ║\n");
    printf("║  28 Features • Full Command Support                 ║\n");
    printf("║  Connected to %s%-35s ║\n", argv[1], RESET);
    printf("%s╚═══════════════════════════════════════════════════════╝%s\n\n", CYAN, RESET);
    
    /* Get username from user */
    printf("%sEnter your username: %s", YELLOW, RESET);
    fgets(current_username, sizeof(current_username), stdin);
    current_username[strcspn(current_username, "\n")] = 0;
    
    /* Use default name if nothing entered */
    if (strlen(current_username) == 0) {
        strcpy(current_username, "Anonymous");
    }
    
    /* Send username to server */
    send_to_server(current_username);
    
    /* Start receiving thread to handle incoming messages */
    pthread_t receive_tid;
    pthread_create(&receive_tid, NULL, receive_thread_function, NULL);
    pthread_detach(receive_tid);
    
    /* Wait for server response */
    sleep(1);
    display_messages();
    show_prompt();
    
    /* Main input loop - read user input and send to server */
    char input_buffer[BUFFER_SIZE];
    
    while (1) {
        fgets(input_buffer, sizeof(input_buffer), stdin);
        input_buffer[strcspn(input_buffer, "\n")] = 0;
        
        if (strlen(input_buffer) > 0) {
            /* Handle /help command locally */
            if (strcmp(input_buffer, "/help") == 0) {
                display_help();
                display_messages();
                show_prompt();
                continue;
            }
            
            /* Send message to server */
            send_to_server(input_buffer);
            
            /* Handle /quit command locally */
            if (strcmp(input_buffer, "/quit") == 0) {
                printf("%sGoodbye!%s\n", GREEN, RESET);
                break;
            }
            
            /* Add own regular messages to buffer */
            if (input_buffer[0] != '/') {
                time_t now = time(NULL);
                struct tm *time_info = localtime(&now);
                char time_str[20];
                strftime(time_str, sizeof(time_str), "%H:%M:%S", time_info);
                add_message_to_buffer(current_username, input_buffer, time_str, 1);
            }
            
            display_messages();
            show_prompt();
        }
    }
    
    /* Cleanup before exit */
    close(server_socket);
    server_socket = -1;
    
    return EXIT_SUCCESS;
}


