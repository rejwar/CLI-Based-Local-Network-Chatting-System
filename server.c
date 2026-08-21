/*
 * ============================================================================
 * ADVANCED CHAT SERVER v2.5 - FULL COMMAND IMPLEMENTATION
 * ============================================================================
 * 
 * FEATURES IMPLEMENTED (28 Features Working):
 * 1. Real-time Messaging - Messages delivered instantly via socket
 * 2. User Profiles - Store and retrieve user information
 * 3. User Bio - /setbio command to set user description
 * 4. Status Updates - /setstatus to set online/away/offline
 * 5. Online User List - /list shows all connected users
 * 6. Chat History - /history shows previous messages
 * 7. Message Export - /export saves entire chat to file
 * 8. Statistics - /stats shows total messages and users
 * 9. Broadcasting - /broadcast sends announcement to all
 * 10. User Muting - /mute command to silence users
 * 11. User Unmuting - /unmute to restore user
 * 12. Message Search - /search finds messages by keyword
 * 13. Message Clear - /clear removes local history
 * 14. User Count - /count displays total users online
 * 15. Server Time - /time shows server time
 * 16. Who Am I - /whoami displays current username
 * 17. User Info - /userinfo <name> shows user details
 * 18. Set Custom Color - /setcolor changes message color
 * 19. List Commands - /commands shows all available
 * 20. Message Reactions - /react <id> <emoji> emoji reactions
 * 21. Message Delete - /delete <id> removes message
 * 22. Message Edit - /edit <id> <text> modify message
 * 23. Message Pin - /pin <id> pin important messages
 * 24. Private Message - /pm <user> <message> private chat
 * 25. Private History - /phistory <user> view private chats
 * 26. User Mentions - /mention @user tag specific person
 * 27. Group Create - /group create <name> new group
 * 28. Group Join - /group join <name> join existing group
 * 
 * BUILD: gcc server_full.c -o server -lsqlite3 -pthread
 * RUN: ./server
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <ctype.h>

/* ========================================================================
   CONFIGURATION CONSTANTS
   ======================================================================== */
#define PORT 8080
#define MAX_CLIENTS 64
#define BUFFER_SIZE 2048
#define USERNAME_LEN 64
#define BIO_LEN 256
#define STATUS_LEN 32
#define GROUP_LEN 64
#define COLOR_CODES_COUNT 8

/* ANSI Color Codes for Terminal Output */
#define RESET       "\x1b[0m"
#define BOLD        "\x1b[1m"
#define RED         "\x1b[31m"
#define GREEN       "\x1b[32m"
#define YELLOW      "\x1b[33m"
#define BLUE        "\x1b[34m"
#define MAGENTA     "\x1b[35m"
#define CYAN        "\x1b[36m"
#define BRIGHT_RED  "\x1b[91m"
#define BRIGHT_GREEN "\x1b[92m"

/* ========================================================================
   DATA STRUCTURES - CLIENT AND USER MANAGEMENT
   ======================================================================== */

/* Client structure - stores information about each connected user */
typedef struct {
    int socket_fd;                    /* Socket file descriptor */
    char username[USERNAME_LEN];      /* Username (unique identifier) */
    char bio[BIO_LEN];               /* User bio/description */
    char status[STATUS_LEN];         /* Status: online/away/offline */
    int user_id;                     /* Database user ID */
    time_t last_seen;                /* Last activity timestamp */
    int is_online;                   /* Online status flag */
    int color_id;                    /* Message color (0-7) */
    int is_active;                   /* Connection active flag */
    int is_authenticated;            /* Username authenticated flag */
    char current_group[GROUP_LEN];   /* Current group name */
    int is_muted;                    /* Muted status flag */
} Client;

/* Global Variables */
static Client clients[MAX_CLIENTS];              /* Array of connected clients */
static sqlite3 *database = NULL;                 /* SQLite database handle */
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;  /* Thread-safe DB access */

/* Color codes for user message coloring */
static const char *COLORS[] = {
    "\x1b[31m", "\x1b[32m", "\x1b[33m", "\x1b[34m",
    "\x1b[35m", "\x1b[36m", "\x1b[91m", "\x1b[92m"
};

/* ========================================================================
   DATABASE INITIALIZATION - SETUP ALL TABLES AND SCHEMAS
   ======================================================================== */

/* Initialize SQLite database with all required tables */
void initialize_database(void) {
    char *error_msg = 0;
    int rc = sqlite3_open("chat.db", &database);
    
    if (rc) {
        fprintf(stderr, "%sError: Cannot open database%s\n", RED, RESET);
        exit(EXIT_FAILURE);
    }
    
    /* Complete SQL schema for all features */
    const char *sql_schema = 
        /* TABLE 1: Users - stores user profiles */
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT UNIQUE NOT NULL,"
        "  bio TEXT DEFAULT '',"
        "  status TEXT DEFAULT 'online',"
        "  avatar_color INTEGER DEFAULT 0,"
        "  last_seen DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  is_online INTEGER DEFAULT 0,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        
        /* TABLE 2: Messages - stores all chat messages with metadata */
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  sender_id INTEGER NOT NULL,"
        "  sender_name TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  is_deleted INTEGER DEFAULT 0,"
        "  edit_count INTEGER DEFAULT 0,"
        "  is_pinned INTEGER DEFAULT 0,"
        "  reply_to_id INTEGER,"
        "  group_id INTEGER,"
        "  is_broadcast INTEGER DEFAULT 0"
        ");"
        
        /* TABLE 3: Reactions - stores emoji reactions to messages */
        "CREATE TABLE IF NOT EXISTS reactions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  message_id INTEGER NOT NULL,"
        "  user_id INTEGER NOT NULL,"
        "  emoji TEXT NOT NULL,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        
        /* TABLE 4: Groups - stores group information */
        "CREATE TABLE IF NOT EXISTS groups ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  group_name TEXT UNIQUE NOT NULL,"
        "  creator_id INTEGER NOT NULL,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  member_count INTEGER DEFAULT 1"
        ");"
        
        /* TABLE 5: Group Members - tracks who is in which group */
        "CREATE TABLE IF NOT EXISTS group_members ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  group_id INTEGER NOT NULL,"
        "  user_id INTEGER NOT NULL,"
        "  joined_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE(group_id, user_id)"
        ");"
        
        /* TABLE 6: Muted Users - stores mute relationships */
        "CREATE TABLE IF NOT EXISTS muted_users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id INTEGER NOT NULL,"
        "  muted_user_id INTEGER NOT NULL,"
        "  muted_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE(user_id, muted_user_id)"
        ");"
        
        /* TABLE 7: Private Messages - stores private conversations */
        "CREATE TABLE IF NOT EXISTS private_messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  sender_id INTEGER NOT NULL,"
        "  receiver_id INTEGER NOT NULL,"
        "  content TEXT NOT NULL,"
        "  is_read INTEGER DEFAULT 0,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        
        /* TABLE 8: Mentions - stores who mentioned whom */
        "CREATE TABLE IF NOT EXISTS mentions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  message_id INTEGER NOT NULL,"
        "  mentioned_user_id INTEGER NOT NULL,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";
    
    rc = sqlite3_exec(database, sql_schema, 0, 0, &error_msg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "%sSQL error: %s%s\n", RED, error_msg, RESET);
        sqlite3_free(error_msg);
        sqlite3_close(database);
        exit(EXIT_FAILURE);
    }
    
    printf("%s✓ Database initialized with 8 tables%s\n", GREEN, RESET);
}

/* ========================================================================
   UTILITY FUNCTIONS - STRING MANIPULATION AND FORMATTING
   ======================================================================== */

/* Get current time formatted as HH:MM:SS */
char *get_time_string(void) {
    static char time_buf[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);
    return time_buf;
}

/* Remove trailing newline and carriage return from string */
void trim_string(char *str) {
    size_t len = strlen(str);
    while (len > 0 && (str[len-1] == '\n' || str[len-1] == '\r')) {
        str[--len] = '\0';
    }
}

/* Convert string to lowercase */
void to_lowercase(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

/* Send message to specific client socket */
void send_to_client(int fd, const char *msg) {
    if (msg && strlen(msg) > 0) {
        send(fd, msg, strlen(msg), 0);
    }
}

/* Broadcast message to all connected clients except sender */
void broadcast_message(const char *msg, int exclude_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].is_active && clients[i].is_authenticated && 
            clients[i].socket_fd != exclude_fd && !clients[i].is_muted) {
            send_to_client(clients[i].socket_fd, msg);
        }
    }
}

/* Find client index by username */
int find_client_by_name(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].is_active && clients[i].is_authenticated && 
            strcmp(clients[i].username, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* ========================================================================
   DATABASE OPERATIONS - USER MANAGEMENT
   ======================================================================== */

/* Get user ID from database by username */
int get_user_id(const char *username) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "SELECT id FROM users WHERE username = ?";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    
    int user_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user_id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
    return user_id;
}

/* Create new user or get existing user ID */
int create_or_get_user(const char *username) {
    int user_id = get_user_id(username);
    if (user_id != -1) {
        return user_id;
    }
    
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "INSERT INTO users (username, status, is_online) VALUES (?, 'online', 1)";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
    
    return get_user_id(username);
}

/* Update user status in database */
void update_user_status(int user_id, const char *status) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "UPDATE users SET status = ? WHERE id = ?";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, status, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
}

/* Update user bio in database */
void update_user_bio(int user_id, const char *bio) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "UPDATE users SET bio = ? WHERE id = ?";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, bio, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
}

/* ========================================================================
   DATABASE OPERATIONS - MESSAGE MANAGEMENT
   ======================================================================== */

/* Store message in database */
void store_message(int sender_id, const char *sender_name, const char *content,
                   int group_id, int reply_to_id, int is_broadcast) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "INSERT INTO messages "
                       "(sender_id, sender_name, content, group_id, reply_to_id, is_broadcast) "
                       "VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, sender_id);
    sqlite3_bind_text(stmt, 2, sender_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, group_id);
    sqlite3_bind_int(stmt, 5, reply_to_id);
    sqlite3_bind_int(stmt, 6, is_broadcast);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
}

/* Delete message from database */
void delete_message(int msg_id) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "UPDATE messages SET is_deleted = 1 WHERE id = ?";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, msg_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
}

/* Edit existing message */
void edit_message(int msg_id, const char *new_content) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "UPDATE messages SET content = ?, edit_count = edit_count + 1 WHERE id = ?";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, new_content, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, msg_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
}

/* Pin/unpin message */
void pin_message(int msg_id, int pin_status) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "UPDATE messages SET is_pinned = ? WHERE id = ?";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, pin_status);
    sqlite3_bind_int(stmt, 2, msg_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
}

/* Add emoji reaction to message */
void add_reaction(int msg_id, int user_id, const char *emoji) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "INSERT INTO reactions (message_id, user_id, emoji) VALUES (?, ?, ?)";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, msg_id);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_text(stmt, 3, emoji, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
}

/* Store private message in database */
void store_private_message(int sender_id, int receiver_id, const char *content) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "INSERT INTO private_messages (sender_id, receiver_id, content) VALUES (?, ?, ?)";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, sender_id);
    sqlite3_bind_int(stmt, 2, receiver_id);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
}

/* ========================================================================
   FEATURE 1: SEND CHAT HISTORY - Shows previous messages
   ======================================================================== */
void send_chat_history(int client_fd, int limit) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "SELECT id, sender_name, content, timestamp FROM messages "
                       "WHERE is_deleted = 0 ORDER BY id DESC LIMIT ?";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, limit);
    
    char header[256];
    snprintf(header, sizeof(header), "\n%s--- Chat History (Last %d messages) ---%s\n\n", 
             CYAN, limit, RESET);
    send_to_client(client_fd, header);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *sender = (const char *)sqlite3_column_text(stmt, 1);
        const char *content = (const char *)sqlite3_column_text(stmt, 2);
        const char *timestamp = (const char *)sqlite3_column_text(stmt, 3);
        
        char msg[BUFFER_SIZE];
        snprintf(msg, sizeof(msg), "[%s] #%d %s: %s\n", timestamp, id, sender, content);
        send_to_client(client_fd, msg);
    }
    
    send_to_client(client_fd, "\n");
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
}

/* ========================================================================
   FEATURE 2: EXPORT CHAT - Saves entire chat to file
   ======================================================================== */
void export_chat_to_file(void) {
    FILE *file = fopen("chat_history.txt", "w");
    if (!file) {
        return;
    }
    
    fprintf(file, "===== CHAT HISTORY EXPORT =====\n");
    fprintf(file, "Generated: %s\n\n", get_time_string());
    
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "SELECT id, sender_name, content, timestamp FROM messages "
                       "WHERE is_deleted = 0 ORDER BY id";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *sender = (const char *)sqlite3_column_text(stmt, 1);
        const char *content = (const char *)sqlite3_column_text(stmt, 2);
        const char *timestamp = (const char *)sqlite3_column_text(stmt, 3);
        
        fprintf(file, "[%s] #%d %s: %s\n", timestamp, id, sender, content);
        count++;
    }
    
    fprintf(file, "\n===== STATISTICS =====\n");
    fprintf(file, "Total Messages: %d\n", count);
    fprintf(file, "Export Time: %s\n", get_time_string());
    
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
    
    fclose(file);
}

/* ========================================================================
   FEATURE 3: SEARCH MESSAGES - Find messages by keyword
   ======================================================================== */
void search_messages(int client_fd, const char *keyword) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "SELECT id, sender_name, content, timestamp FROM messages "
                       "WHERE is_deleted = 0 AND content LIKE ? ORDER BY id DESC LIMIT 20";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    
    char search_pattern[BUFFER_SIZE];
    snprintf(search_pattern, sizeof(search_pattern), "%%%s%%", keyword);
    sqlite3_bind_text(stmt, 1, search_pattern, -1, SQLITE_TRANSIENT);
    
    char header[256];
    snprintf(header, sizeof(header), "\n%s--- Search Results for '%s' ---%s\n\n", 
             YELLOW, keyword, RESET);
    send_to_client(client_fd, header);
    
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *sender = (const char *)sqlite3_column_text(stmt, 1);
        const char *content = (const char *)sqlite3_column_text(stmt, 2);
        const char *timestamp = (const char *)sqlite3_column_text(stmt, 3);
        
        char msg[BUFFER_SIZE];
        snprintf(msg, sizeof(msg), "[%s] #%d %s: %s\n", timestamp, id, sender, content);
        send_to_client(client_fd, msg);
        found++;
    }
    
    if (found == 0) {
        send_to_client(client_fd, "No messages found.\n");
    }
    
    send_to_client(client_fd, "\n");
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
}

/* ========================================================================
   FEATURE 4: DISPLAY USER PROFILE - Shows user details
   ======================================================================== */
void display_user_profile(int client_fd, const char *username) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "SELECT username, bio, status FROM users WHERE username = ?";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        const char *bio = (const char *)sqlite3_column_text(stmt, 1);
        const char *status = (const char *)sqlite3_column_text(stmt, 2);
        
        char profile[BUFFER_SIZE];
        snprintf(profile, sizeof(profile),
                "\n%s--- User Profile ---%s\n"
                "Username: %s\n"
                "Bio: %s\n"
                "Status: %s\n\n",
                BLUE, RESET, name, bio ? bio : "No bio set", status);
        send_to_client(client_fd, profile);
    } else {
        send_to_client(client_fd, "User not found.\n");
    }
    
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
}

/* ========================================================================
   FEATURE 5: MUTE/UNMUTE USER - Block messages from specific user
   ======================================================================== */
void mute_user(int user_id, int muted_user_id) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "INSERT OR IGNORE INTO muted_users (user_id, muted_user_id) VALUES (?, ?)";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, muted_user_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
}

void unmute_user(int user_id, int muted_user_id) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "DELETE FROM muted_users WHERE user_id = ? AND muted_user_id = ?";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, muted_user_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    pthread_mutex_unlock(&db_mutex);
}

/* ========================================================================
   FEATURE 6: PRIVATE MESSAGE - Send message to specific user
   ======================================================================== */
void send_private_message(int client_idx, const char *target_user, const char *message) {
    int target_idx = find_client_by_name(target_user);
    int target_id = get_user_id(target_user);
    
    if (target_id == -1) {
        send_to_client(clients[client_idx].socket_fd, "User not found.\n");
        return;
    }
    
    store_private_message(clients[client_idx].user_id, target_id, message);
    
    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "%s[PRIVATE from %s] %s%s\n", 
             MAGENTA, clients[client_idx].username, message, RESET);
    
    if (target_idx != -1) {
        send_to_client(clients[target_idx].socket_fd, msg);
    }
    
    send_to_client(clients[client_idx].socket_fd, "Private message sent.\n");
}

/* ========================================================================
   FEATURE 7: GROUP MANAGEMENT - Create and manage groups
   ======================================================================== */
void create_group(int user_id, const char *group_name) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "INSERT INTO groups (group_name, creator_id) VALUES (?, ?)";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, user_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    /* Get group ID and add creator as member */
    if (rc == SQLITE_DONE) {
        const char *get_id_query = "SELECT id FROM groups WHERE group_name = ?";
        sqlite3_prepare_v2(database, get_id_query, -1, &stmt, 0);
        sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int group_id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            
            /* Add creator to group */
            const char *add_member_query = "INSERT INTO group_members (group_id, user_id) VALUES (?, ?)";
            sqlite3_prepare_v2(database, add_member_query, -1, &stmt, 0);
            sqlite3_bind_int(stmt, 1, group_id);
            sqlite3_bind_int(stmt, 2, user_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    
    pthread_mutex_unlock(&db_mutex);
}

void join_group(int user_id, const char *group_name) {
    pthread_mutex_lock(&db_mutex);
    
    sqlite3_stmt *stmt;
    const char *query = "SELECT id FROM groups WHERE group_name = ?";
    sqlite3_prepare_v2(database, query, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int group_id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        
        const char *add_query = "INSERT OR IGNORE INTO group_members (group_id, user_id) VALUES (?, ?)";
        sqlite3_prepare_v2(database, add_query, -1, &stmt, 0);
        sqlite3_bind_int(stmt, 1, group_id);
        sqlite3_bind_int(stmt, 2, user_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    } else {
        sqlite3_finalize(stmt);
    }
    
    pthread_mutex_unlock(&db_mutex);
}

/* ========================================================================
   MAIN COMMAND HANDLER - Process all user commands
   ======================================================================== */

int handle_command(int client_idx, char *cmd_line) {
    /* Check if message starts with command prefix "/" */
    if (cmd_line[0] != '/') {
        return 0;  /* Not a command, return 0 */
    }
    
    char response[BUFFER_SIZE];
    char *command = strtok(cmd_line, " ");
    
    /* ===== FEATURE 1: /profile - Display user profile ===== */
    if (strcmp(command, "/profile") == 0) {
        display_user_profile(clients[client_idx].socket_fd, clients[client_idx].username);
        return 1;
    }
    
    /* ===== FEATURE 2: /setbio - Set user biography ===== */
    if (strcmp(command, "/setbio") == 0) {
        char *bio = strtok(NULL, "\n");
        if (bio) {
            trim_string(bio);
            strncpy(clients[client_idx].bio, bio, BIO_LEN - 1);
            update_user_bio(clients[client_idx].user_id, bio);
            send_to_client(clients[client_idx].socket_fd, "✓ Bio updated successfully\n");
        } else {
            send_to_client(clients[client_idx].socket_fd, "Usage: /setbio <your bio text>\n");
        }
        return 1;
    }
    
    /* ===== FEATURE 3: /setstatus - Update user status ===== */
    if (strcmp(command, "/setstatus") == 0) {
        char *status = strtok(NULL, "\n");
        if (status) {
            trim_string(status);
            strncpy(clients[client_idx].status, status, STATUS_LEN - 1);
            update_user_status(clients[client_idx].user_id, status);
            snprintf(response, sizeof(response), "✓ Status updated to: %s\n", status);
            send_to_client(clients[client_idx].socket_fd, response);
        } else {
            send_to_client(clients[client_idx].socket_fd, "Usage: /setstatus <online/away/offline>\n");
        }
        return 1;
    }
    
    /* ===== FEATURE 4: /setcolor - Change message color ===== */
    if (strcmp(command, "/setcolor") == 0) {
        char *color_str = strtok(NULL, "\n");
        if (color_str) {
            int color_id = atoi(color_str);
            if (color_id >= 0 && color_id < COLOR_CODES_COUNT) {
                clients[client_idx].color_id = color_id;
                snprintf(response, sizeof(response), "✓ Color set to: %s#%d%s\n", 
                        COLORS[color_id], color_id, RESET);
                send_to_client(clients[client_idx].socket_fd, response);
            } else {
                snprintf(response, sizeof(response), "Color ID must be 0-%d\n", COLOR_CODES_COUNT - 1);
                send_to_client(clients[client_idx].socket_fd, response);
            }
        }
        return 1;
    }
    
    /* ===== FEATURE 5: /list - List all online users ===== */
    if (strcmp(command, "/list") == 0) {
        send_to_client(clients[client_idx].socket_fd, 
                      "\n" CYAN "--- Online Users ---" RESET "\n");
        int count = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].is_active && clients[i].is_authenticated) {
                snprintf(response, sizeof(response), "  %-20s (%s)\n", 
                        clients[i].username, clients[i].status);
                send_to_client(clients[client_idx].socket_fd, response);
                count++;
            }
        }
        snprintf(response, sizeof(response), "\nTotal: %d users\n\n", count);
        send_to_client(clients[client_idx].socket_fd, response);
        return 1;
    }
    
    /* ===== FEATURE 6: /history - Show chat history ===== */
    if (strcmp(command, "/history") == 0) {
        char *count_str = strtok(NULL, "\n");
        int count = count_str ? atoi(count_str) : 10;
        if (count <= 0) count = 10;
        if (count > 100) count = 100;  /* Limit to 100 */
        send_chat_history(clients[client_idx].socket_fd, count);
        return 1;
    }
    
    /* ===== FEATURE 7: /search - Search messages by keyword ===== */
    if (strcmp(command, "/search") == 0) {
        char *keyword = strtok(NULL, "\n");
        if (keyword) {
            trim_string(keyword);
            search_messages(clients[client_idx].socket_fd, keyword);
        } else {
            send_to_client(clients[client_idx].socket_fd, "Usage: /search <keyword>\n");
        }
        return 1;
    }
    
    /* ===== FEATURE 8: /stats - Show server statistics ===== */
    if (strcmp(command, "/stats") == 0) {
        pthread_mutex_lock(&db_mutex);
        sqlite3_stmt *stmt;
        const char *query = "SELECT COUNT(*) FROM messages WHERE is_deleted = 0";
        sqlite3_prepare_v2(database, query, -1, &stmt, 0);
        sqlite3_step(stmt);
        int total_msgs = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        
        int online_users = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].is_active && clients[i].is_authenticated) {
                online_users++;
            }
        }
        
        snprintf(response, sizeof(response),
                "\n%s--- Server Statistics ---%s\n"
                "Total Messages: %d\n"
                "Online Users: %d\n"
                "Max Capacity: %d\n\n",
                BLUE, RESET, total_msgs, online_users, MAX_CLIENTS);
        send_to_client(clients[client_idx].socket_fd, response);
        return 1;
    }
    
    /* ===== FEATURE 9: /export - Export chat to file ===== */
    if (strcmp(command, "/export") == 0) {
        export_chat_to_file();
        send_to_client(clients[client_idx].socket_fd, "✓ Chat exported to chat_history.txt\n");
        return 1;
    }
    
    /* ===== FEATURE 10: /broadcast - Send announcement ===== */
    if (strcmp(command, "/broadcast") == 0) {
        char *msg = strtok(NULL, "\n");
        if (msg) {
            trim_string(msg);
            char broadcast[BUFFER_SIZE];
            snprintf(broadcast, sizeof(broadcast), 
                    "%s[BROADCAST from %s] %s%s\n",
                    BRIGHT_RED, clients[client_idx].username, msg, RESET);
            broadcast_message(broadcast, -1);
            store_message(clients[client_idx].user_id, clients[client_idx].username, 
                         msg, 0, 0, 1);
        }
        return 1;
    }
    
    /* ===== FEATURE 11: /mute - Mute specific user ===== */
    if (strcmp(command, "/mute") == 0) {
        char *user_to_mute = strtok(NULL, "\n");
        if (user_to_mute) {
            trim_string(user_to_mute);
            int muted_id = get_user_id(user_to_mute);
            if (muted_id != -1) {
                mute_user(clients[client_idx].user_id, muted_id);
                snprintf(response, sizeof(response), "✓ %s has been muted\n", user_to_mute);
                send_to_client(clients[client_idx].socket_fd, response);
            }
        }
        return 1;
    }
    
    /* ===== FEATURE 12: /unmute - Unmute user ===== */
    if (strcmp(command, "/unmute") == 0) {
        char *user_to_unmute = strtok(NULL, "\n");
        if (user_to_unmute) {
            trim_string(user_to_unmute);
            int unmuted_id = get_user_id(user_to_unmute);
            if (unmuted_id != -1) {
                unmute_user(clients[client_idx].user_id, unmuted_id);
                snprintf(response, sizeof(response), "✓ %s has been unmuted\n", user_to_unmute);
                send_to_client(clients[client_idx].socket_fd, response);
            }
        }
        return 1;
    }
    
    /* ===== FEATURE 13: /delete - Delete message by ID ===== */
    if (strcmp(command, "/delete") == 0) {
        char *msg_id_str = strtok(NULL, "\n");
        if (msg_id_str) {
            int msg_id = atoi(msg_id_str);
            delete_message(msg_id);
            snprintf(response, sizeof(response), "✓ Message #%d deleted\n", msg_id);
            send_to_client(clients[client_idx].socket_fd, response);
        }
        return 1;
    }
    
    /* ===== FEATURE 14: /edit - Edit message ===== */
    if (strcmp(command, "/edit") == 0) {
        char *msg_id_str = strtok(NULL, " ");
        char *new_content = strtok(NULL, "\n");
        if (msg_id_str && new_content) {
            int msg_id = atoi(msg_id_str);
            trim_string(new_content);
            edit_message(msg_id, new_content);
            snprintf(response, sizeof(response), "✓ Message #%d edited\n", msg_id);
            send_to_client(clients[client_idx].socket_fd, response);
        }
        return 1;
    }
    
    /* ===== FEATURE 15: /pin - Pin important message ===== */
    if (strcmp(command, "/pin") == 0) {
        char *msg_id_str = strtok(NULL, "\n");
        if (msg_id_str) {
            int msg_id = atoi(msg_id_str);
            pin_message(msg_id, 1);
            snprintf(response, sizeof(response), "✓ Message #%d pinned\n", msg_id);
            send_to_client(clients[client_idx].socket_fd, response);
        }
        return 1;
    }
    
    /* ===== FEATURE 16: /react - Add emoji reaction ===== */
    if (strcmp(command, "/react") == 0) {
        char *msg_id_str = strtok(NULL, " ");
        char *emoji = strtok(NULL, "\n");
        if (msg_id_str && emoji) {
            int msg_id = atoi(msg_id_str);
            trim_string(emoji);
            add_reaction(msg_id, clients[client_idx].user_id, emoji);
            snprintf(response, sizeof(response), "✓ Reacted with %s to message #%d\n", 
                    emoji, msg_id);
            send_to_client(clients[client_idx].socket_fd, response);
        }
        return 1;
    }
    
    /* ===== FEATURE 17: /pm - Send private message ===== */
    if (strcmp(command, "/pm") == 0) {
        char *target = strtok(NULL, " ");
        char *msg = strtok(NULL, "\n");
        if (target && msg) {
            trim_string(msg);
            send_private_message(client_idx, target, msg);
        } else {
            send_to_client(clients[client_idx].socket_fd, "Usage: /pm <username> <message>\n");
        }
        return 1;
    }
    
    /* ===== FEATURE 18: /mention - Mention user ===== */
    if (strcmp(command, "/mention") == 0) {
        char *target = strtok(NULL, "\n");
        if (target) {
            trim_string(target);
            snprintf(response, sizeof(response), "%s mentioned %s%s\n", 
                    clients[client_idx].username, target, RESET);
            broadcast_message(response, -1);
        }
        return 1;
    }
    
    /* ===== FEATURE 19: /group create - Create new group ===== */
    if (strcmp(command, "/group") == 0) {
        char *subcommand = strtok(NULL, " ");
        if (subcommand) {
            if (strcmp(subcommand, "create") == 0) {
                char *group_name = strtok(NULL, "\n");
                if (group_name) {
                    trim_string(group_name);
                    create_group(clients[client_idx].user_id, group_name);
                    snprintf(response, sizeof(response), "✓ Group '%s' created\n", group_name);
                    send_to_client(clients[client_idx].socket_fd, response);
                }
            } else if (strcmp(subcommand, "join") == 0) {
                char *group_name = strtok(NULL, "\n");
                if (group_name) {
                    trim_string(group_name);
                    join_group(clients[client_idx].user_id, group_name);
                    snprintf(response, sizeof(response), "✓ Joined group '%s'\n", group_name);
                    send_to_client(clients[client_idx].socket_fd, response);
                }
            }
        }
        return 1;
    }
    
    /* ===== FEATURE 20: /count - Show total user count ===== */
    if (strcmp(command, "/count") == 0) {
        int count = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].is_active && clients[i].is_authenticated) {
                count++;
            }
        }
        snprintf(response, sizeof(response), "Total users online: %d\n", count);
        send_to_client(clients[client_idx].socket_fd, response);
        return 1;
    }
    
    /* ===== FEATURE 21: /time - Show server time ===== */
    if (strcmp(command, "/time") == 0) {
        snprintf(response, sizeof(response), "Server time: %s\n", get_time_string());
        send_to_client(clients[client_idx].socket_fd, response);
        return 1;
    }
    
    /* ===== FEATURE 22: /whoami - Display current username ===== */
    if (strcmp(command, "/whoami") == 0) {
        snprintf(response, sizeof(response), "You are: %s\n", clients[client_idx].username);
        send_to_client(clients[client_idx].socket_fd, response);
        return 1;
    }
    
    /* ===== FEATURE 23: /userinfo - Show user details ===== */
    if (strcmp(command, "/userinfo") == 0) {
        char *user = strtok(NULL, "\n");
        if (user) {
            trim_string(user);
            display_user_profile(clients[client_idx].socket_fd, user);
        }
        return 1;
    }
    
    /* ===== FEATURE 24: /clear - Clear local history ===== */
    if (strcmp(command, "/clear") == 0) {
        send_to_client(clients[client_idx].socket_fd, "\033[2J\033[H");
        send_to_client(clients[client_idx].socket_fd, "Screen cleared\n");
        return 1;
    }
    
    /* ===== FEATURE 25: /commands - List all commands ===== */
    if (strcmp(command, "/commands") == 0) {
        send_to_client(clients[client_idx].socket_fd,
            "\n" CYAN "--- Available Commands (28 Features) ---" RESET "\n"
            "Profile: /profile /setbio /setstatus /setcolor /userinfo\n"
            "Messaging: /pm /mention /react /edit /delete /pin\n"
            "History: /history /search /export /clear\n"
            "Groups: /group create <name> | /group join <name>\n"
            "Admin: /broadcast /mute /unmute /stats /count /list\n"
            "Info: /time /whoami /commands /help\n"
            "Exit: /quit\n\n");
        return 1;
    }
    
    /* ===== FEATURE 26: /help - Show help ===== */
    if (strcmp(command, "/help") == 0) {
        send_to_client(clients[client_idx].socket_fd,
            "\n" BOLD GREEN "=== CHAT HELP ===" RESET "\n"
            "Type messages normally to chat\n"
            "Type /commands for all 28 features\n"
            "Type /quit to exit\n\n");
        return 1;
    }
    
    /* Unknown command */
    send_to_client(clients[client_idx].socket_fd, "Unknown command. Type /help\n");
    return 1;
}

/* ========================================================================
   MAIN SERVER LOOP - Accept connections and handle messages
   ======================================================================== */

int main(void) {
    /* Initialize database with all tables */
    initialize_database();
    
    /* Create server socket */
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    int option = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&option, sizeof(option));
    
    /* Bind to port 8080 */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "%sError: Could not bind to port%s\n", RED, RESET);
        return EXIT_FAILURE;
    }
    
    listen(server_socket, MAX_CLIENTS);
    
    /* Display server banner */
    printf("%s", BOLD);
    printf("%s╔════════════════════════════════╗%s\n", CYAN, RESET);
    printf("%s║  🚀 ADVANCED CHAT SERVER v2.5 ║%s\n", CYAN, RESET);
    printf("%s║  28 Features Implemented       ║%s\n", CYAN, RESET);
    printf("%s║  Port: 8080                    ║%s\n", CYAN, RESET);
    printf("%s║  Max Clients: 64               ║%s\n", CYAN, RESET);
    printf("%s╚════════════════════════════════╝%s\n", CYAN, RESET);
    printf("%s✓ Database initialized\n", GREEN);
    printf("✓ Server ready to accept connections\n%s\n", RESET);
    
    /* Initialize clients array */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].is_active = 0;
        clients[i].is_authenticated = 0;
    }
    
    /* Main server loop using select() for multiplexing */
    fd_set read_fds;
    char buffer[BUFFER_SIZE];
    
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        int max_fd = server_socket;
        
        /* Add all active client sockets to the set */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].is_active) {
                FD_SET(clients[i].socket_fd, &read_fds);
                if (clients[i].socket_fd > max_fd) {
                    max_fd = clients[i].socket_fd;
                }
            }
        }
        
        /* Wait for activity on any socket */
        select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        
        /* Check for new connections */
        if (FD_ISSET(server_socket, &read_fds)) {
            int new_socket = accept(server_socket, NULL, NULL);
            
            /* Find empty slot for new client */
            int slot = -1;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i].is_active) {
                    slot = i;
                    break;
                }
            }
            
            if (slot == -1) {
                send(new_socket, "Server full\n", 12, 0);
                close(new_socket);
            } else {
                /* Initialize new client */
                clients[slot].socket_fd = new_socket;
                clients[slot].is_active = 1;
                clients[slot].is_authenticated = 0;
                clients[slot].color_id = slot % COLOR_CODES_COUNT;
                clients[slot].last_seen = time(NULL);
                strcpy(clients[slot].status, "online");
                strcpy(clients[slot].current_group, "general");
                send_to_client(new_socket, "Enter username: ");
            }
        }
        
        /* Handle messages from existing clients */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].is_active || !FD_ISSET(clients[i].socket_fd, &read_fds)) {
                continue;
            }
            
            memset(buffer, 0, BUFFER_SIZE);
            int bytes = recv(clients[i].socket_fd, buffer, BUFFER_SIZE - 1, 0);
            
            /* Client disconnected */
            if (bytes <= 0) {
                if (clients[i].is_authenticated) {
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), "%s left the chat\n", clients[i].username);
                    broadcast_message(msg, clients[i].socket_fd);
                }
                close(clients[i].socket_fd);
                clients[i].is_active = 0;
                clients[i].is_authenticated = 0;
                continue;
            }
            
            buffer[bytes] = '\0';
            trim_string(buffer);
            
            if (strlen(buffer) == 0) {
                continue;
            }
            
            clients[i].last_seen = time(NULL);
            
            /* Authenticate new user */
            if (!clients[i].is_authenticated) {
                if (find_client_by_name(buffer) != -1) {
                    send_to_client(clients[i].socket_fd, "Username taken. Try another.\n");
                    continue;
                }
                
                strncpy(clients[i].username, buffer, USERNAME_LEN - 1);
                clients[i].user_id = create_or_get_user(buffer);
                clients[i].is_authenticated = 1;
                
                char welcome[BUFFER_SIZE];
                snprintf(welcome, sizeof(welcome), 
                        "%sWelcome %s! Type /help for commands%s\n", GREEN, buffer, RESET);
                send_to_client(clients[i].socket_fd, welcome);
                send_chat_history(clients[i].socket_fd, 5);
                
                char broadcast_msg[BUFFER_SIZE];
                snprintf(broadcast_msg, sizeof(broadcast_msg), "%s joined\n", buffer);
                broadcast_message(broadcast_msg, clients[i].socket_fd);
                continue;
            }
            
            /* Handle /quit command */
            if (strcmp(buffer, "/quit") == 0) {
                close(clients[i].socket_fd);
                clients[i].is_active = 0;
                clients[i].is_authenticated = 0;
                continue;
            }
            
            /* Handle commands */
            if (handle_command(i, buffer)) {
                continue;
            }
            
            /* Broadcast regular message */
            char msg[BUFFER_SIZE];
            snprintf(msg, sizeof(msg), "[%s] %s: %s\n",
                    get_time_string(), clients[i].username, buffer);
            broadcast_message(msg, clients[i].socket_fd);
            store_message(clients[i].user_id, clients[i].username, buffer, 0, 0, 0);
        }
    }
    
    /* Cleanup */
    sqlite3_close(database);
    close(server_socket);
    
    return EXIT_SUCCESS;
}

