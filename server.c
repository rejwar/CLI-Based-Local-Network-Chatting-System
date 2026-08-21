/*
 * server_with_history.c — Enhanced server with persistent chat history
 * Features: 20+ commands, SQLite database, message history display, export
 * Build: gcc server_with_history.c -o server -lsqlite3
 * Run: ./server
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
    #define CLOSESOCK(s) closesocket(s)
    #define SOCK_ERR SOCKET_ERROR
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/select.h>
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define CLOSESOCK(s) close(s)
    #define SOCK_ERR (-1)
#endif

#define PORT 8080
#define MAX_CLIENTS 32
#define BUF 2048
#define NAME_LEN 64

/* ANSI Colors */
const char *COLORS[] = {
    "\x1b[31m", "\x1b[32m", "\x1b[33m", "\x1b[34m",
    "\x1b[35m", "\x1b[36m", "\x1b[91m", "\x1b[92m",
    "\x1b[93m", "\x1b[94m", "\x1b[95m", "\x1b[96m"
};
#define NUM_COLORS 12
#define RESET "\x1b[0m"
#define BOLD "\x1b[1m"
#define GREEN "\x1b[32m"
#define CYAN "\x1b[36m"

/* Client structure */
typedef struct {
    SOCKET fd;
    char name[NAME_LEN];
    int user_id;
    char status[32];
    time_t last_seen;
    int is_online;
    int color;
    int active;
    int named;
} Client;

Client clients[MAX_CLIENTS];
sqlite3 *db;

/* Initialize database */
void init_database() {
    char *err = 0;
    int rc = sqlite3_open("chat.db", &db);
    
    if (rc) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        exit(1);
    }
    
    const char *sql = 
        "CREATE TABLE IF NOT EXISTS users("
        "id INTEGER PRIMARY KEY, "
        "username TEXT UNIQUE, "
        "status TEXT DEFAULT 'online', "
        "last_seen DATETIME, "
        "is_online INTEGER DEFAULT 0, "
        "verified INTEGER DEFAULT 0);"
        
        "CREATE TABLE IF NOT EXISTS messages("
        "id INTEGER PRIMARY KEY, "
        "sender_id INTEGER, "
        "sender_name TEXT, "
        "receiver_id INTEGER, "
        "content TEXT, "
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "is_deleted INTEGER DEFAULT 0, "
        "deleted_for_all INTEGER DEFAULT 0, "
        "edit_count INTEGER DEFAULT 0, "
        "is_pinned INTEGER DEFAULT 0, "
        "expires_at DATETIME);"
        
        "CREATE TABLE IF NOT EXISTS read_receipts("
        "id INTEGER PRIMARY KEY, "
        "message_id INTEGER, "
        "user_id INTEGER, "
        "read_at DATETIME);"
        
        "CREATE TABLE IF NOT EXISTS blocked_users("
        "id INTEGER PRIMARY KEY, "
        "user_id INTEGER, "
        "blocked_user_id INTEGER);";
    
    rc = sqlite3_exec(db, sql, 0, 0, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err);
        sqlite3_free(err);
    }
    printf("✓ Database initialized (chat.db)\n");
}

/* Format time */
char *format_time(time_t t) {
    static char buf[32];
    struct tm *tm_info = localtime(&t);
    strftime(buf, sizeof(buf), "%H:%M:%S", tm_info);
    return buf;
}

/* Format date and time */
char *format_datetime(time_t t) {
    static char buf[64];
    struct tm *tm_info = localtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return buf;
}

/* Send message to client */
void send_to(SOCKET fd, const char *msg) {
    send(fd, msg, (int)strlen(msg), 0);
}

/* Broadcast to all */
void broadcast(const char *msg, SOCKET except) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].named && clients[i].fd != except) {
            send_to(clients[i].fd, msg);
        }
    }
}

/* Find client by name */
int find_by_name(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].named && strcmp(clients[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Find client by fd */
int find_by_fd(SOCKET fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].fd == fd) return i;
    }
    return -1;
}

/* Trim whitespace */
void trim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) {
        s[--n] = '\0';
    }
}

/* Get user ID from database */
int get_user_id(const char *username) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id FROM users WHERE username = ?";
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    
    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

/* Create/get user */
int get_or_create_user(const char *username) {
    int id = get_user_id(username);
    if (id != -1) return id;
    
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO users(username, status, is_online) VALUES(?, 'online', 1)";
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return get_user_id(username);
}

/* Store message */
void store_message(int sender_id, const char *sender_name, int receiver_id, const char *content) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO messages(sender_id, sender_name, receiver_id, content) VALUES(?, ?, ?, ?)";
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, sender_id);
    sqlite3_bind_text(stmt, 2, sender_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, receiver_id);
    sqlite3_bind_text(stmt, 4, content, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

/* Export chat history to file */
void export_history_to_file() {
    FILE *f = fopen("chat_history.txt", "w");
    if (!f) return;
    
    fprintf(f, "╔════════════════════════════════════════════════════════════════╗\n");
    fprintf(f, "║              CHAT HISTORY EXPORT                              ║\n");
    fprintf(f, "║              Generated: %s                          ║\n", format_datetime(time(NULL)));
    fprintf(f, "╚════════════════════════════════════════════════════════════════╝\n\n");
    
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, sender_name, content, timestamp, is_deleted, edit_count FROM messages ORDER BY id";
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *sender = (const char *)sqlite3_column_text(stmt, 1);
        const char *content = (const char *)sqlite3_column_text(stmt, 2);
        const char *timestamp = (const char *)sqlite3_column_text(stmt, 3);
        int deleted = sqlite3_column_int(stmt, 4);
        int edits = sqlite3_column_int(stmt, 5);
        
        if (deleted) {
            fprintf(f, "[%s] #%-4d [DELETED]\n\n", timestamp, id);
        } else {
            fprintf(f, "[%s] #%-4d %s: %s", timestamp, id, sender, content);
            if (edits > 0) {
                fprintf(f, " (edited %d time%s)", edits, edits > 1 ? "s" : "");
            }
            fprintf(f, "\n\n");
            count++;
        }
    }
    
    fprintf(f, "\n════════════════════════════════════════════════════════════════\n");
    fprintf(f, "Total messages: %d\n", count);
    fprintf(f, "Exported: %s\n", format_datetime(time(NULL)));
    
    sqlite3_finalize(stmt);
    fclose(f);
    printf("✓ Chat history exported to chat_history.txt\n");
}

/* Display chat history to new user */
void send_chat_history(SOCKET fd, int count) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, sender_name, content, timestamp FROM messages "
                     "WHERE is_deleted = 0 ORDER BY id DESC LIMIT ?";
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, count);
    
    char header[BUF];
    snprintf(header, sizeof(header), 
        "\n%s╔═══════════════════════════════════════════════╗%s\n"
        "%s║  CHAT HISTORY (Last %d messages)             ║%s\n"
        "%s╚═══════════════════════════════════════════════╝%s\n\n",
        CYAN, RESET, CYAN, count, RESET, CYAN, RESET);
    send_to(fd, header);
    
    /* First collect all rows since we fetched DESC */
    typedef struct {
        int id;
        char sender[NAME_LEN];
        char content[BUF];
        char timestamp[32];
    } HistoryRow;
    
    HistoryRow rows[count];
    int row_count = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW && row_count < count) {
        rows[row_count].id = sqlite3_column_int(stmt, 0);
        const char *sender = (const char *)sqlite3_column_text(stmt, 1);
        const char *content = (const char *)sqlite3_column_text(stmt, 2);
        const char *timestamp = (const char *)sqlite3_column_text(stmt, 3);
        
        strncpy(rows[row_count].sender, sender, NAME_LEN - 1);
        strncpy(rows[row_count].content, content, BUF - 1);
        strncpy(rows[row_count].timestamp, timestamp, 31);
        row_count++;
    }
    
    /* Display in correct order (oldest first) */
    for (int i = row_count - 1; i >= 0; i--) {
        char msg[BUF + 64];
        snprintf(msg, sizeof(msg), "%s[%s] #%-4d %s:%s %s\n",
                 GREEN, rows[i].timestamp, rows[i].id, rows[i].sender, RESET, rows[i].content);
        send_to(fd, msg);
    }
    
    send_to(fd, "\n");
    sqlite3_finalize(stmt);
}

/* Handle commands */
int handle_command(int ci, char *line) {
    if (line[0] != '/') return 0;
    
    char out[BUF + 128];
    
    /* /load-history - Show full chat history */
    if (strncmp(line, "/load-history", 13) == 0) {
        int count = 50;
        if (strlen(line) > 14) count = atoi(line + 14);
        if (count > 200) count = 200;
        
        send_chat_history(clients[ci].fd, count);
        return 1;
    }
    
    /* /export - Export to file */
    if (strncmp(line, "/export", 7) == 0) {
        export_history_to_file();
        send_to(clients[ci].fd, "✓ Chat history exported to chat_history.txt\n");
        return 1;
    }
    
    /* /stats */
    if (strncmp(line, "/stats", 6) == 0) {
        sqlite3_stmt *stmt;
        const char *sql = "SELECT COUNT(*) FROM messages WHERE is_deleted = 0";
        sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
        sqlite3_step(stmt);
        int total = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        
        int online = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && clients[i].named) online++;
        }
        
        snprintf(out, sizeof(out), 
            "\n%s📊 Chat Statistics:%s\n"
            "  Total messages: %d\n"
            "  Online users: %d\n"
            "  Max capacity: 32 users\n\n",
            BOLD, RESET, total, online);
        send_to(clients[ci].fd, out);
        return 1;
    }
    
    /* /history - Show recent messages */
    if (strncmp(line, "/history", 8) == 0) {
        int count = 10;
        if (strlen(line) > 9) count = atoi(line + 9);
        
        send_chat_history(clients[ci].fd, count);
        return 1;
    }
    
    /* /list - online users */
    if (strncmp(line, "/list", 5) == 0) {
        send_to(clients[ci].fd, "\n╔════ Online Users ════╗\n");
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && clients[i].named) {
                snprintf(out, sizeof(out), "║ ● %s (%s) - %s\n",
                         clients[i].name, clients[i].status, format_time(clients[i].last_seen));
                send_to(clients[ci].fd, out);
            }
        }
        send_to(clients[ci].fd, "╚═══════════════════════╝\n\n");
        return 1;
    }
    
    /* /nick - change username */
    if (strncmp(line, "/nick ", 6) == 0) {
        char *newname = line + 6;
        trim(newname);
        if (strlen(newname) == 0 || find_by_name(newname) != -1) {
            send_to(clients[ci].fd, "❌ Invalid or taken name\n");
            return 1;
        }
        snprintf(out, sizeof(out), "%s%s➜ %s%s\n", BOLD, clients[ci].name, newname, RESET);
        strncpy(clients[ci].name, newname, NAME_LEN - 1);
        broadcast(out, INVALID_SOCKET);
        return 1;
    }
    
    /* /status */
    if (strncmp(line, "/status ", 8) == 0) {
        char *status = line + 8;
        trim(status);
        strncpy(clients[ci].status, status, 31);
        snprintf(out, sizeof(out), "✓ Status: %s\n", status);
        send_to(clients[ci].fd, out);
        return 1;
    }
    
    /* /pm */
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
            send_to(clients[ci].fd, "❌ User not found\n");
            return 1;
        }
        
        snprintf(out, sizeof(out), "%s🔒 [PM from %s at %s]%s %s\n",
                 COLORS[clients[ci].color], clients[ci].name, format_time(time(NULL)), RESET, msg);
        send_to(clients[ti].fd, out);
        send_to(clients[ci].fd, "✓ Message sent\n");
        
        store_message(clients[ci].user_id, clients[ci].name, clients[ti].user_id, msg);
        return 1;
    }
    
    /* /search */
    if (strncmp(line, "/search ", 8) == 0) {
        char *query = line + 8;
        trim(query);
        
        sqlite3_stmt *stmt;
        const char *sql = "SELECT id, sender_name, content FROM messages WHERE content LIKE ? AND is_deleted = 0 LIMIT 20";
        sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
        
        char pattern[BUF];
        snprintf(pattern, sizeof(pattern), "%%%s%%", query);
        sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
        
        send_to(clients[ci].fd, "\n🔍 Search Results:\n");
        int found = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const char *sender = (const char *)sqlite3_column_text(stmt, 1);
            const char *content = (const char *)sqlite3_column_text(stmt, 2);
            snprintf(out, sizeof(out), "  #%d - %s: %s\n", id, sender, content);
            send_to(clients[ci].fd, out);
            found++;
        }
        if (found == 0) send_to(clients[ci].fd, "  No results\n");
        send_to(clients[ci].fd, "\n");
        sqlite3_finalize(stmt);
        return 1;
    }
    
    /* /edit */
    if (strncmp(line, "/edit ", 6) == 0) {
        char *rest = line + 6;
        char *space = strchr(rest, ' ');
        if (!space) {
            send_to(clients[ci].fd, "Usage: /edit <id> <new_text>\n");
            return 1;
        }
        *space = '\0';
        int msg_id = atoi(rest);
        char *new_text = space + 1;
        
        sqlite3_stmt *stmt;
        const char *sql = "UPDATE messages SET content = ?, edit_count = edit_count + 1 WHERE id = ?";
        sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
        sqlite3_bind_text(stmt, 1, new_text, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, msg_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        snprintf(out, sizeof(out), "✏️ Message #%d edited\n", msg_id);
        broadcast(out, INVALID_SOCKET);
        return 1;
    }
    
    /* /delete */
    if (strncmp(line, "/delete ", 8) == 0) {
        int msg_id = atoi(line + 8);
        
        sqlite3_stmt *stmt;
        const char *sql = "UPDATE messages SET is_deleted = 1 WHERE id = ?";
        sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
        sqlite3_bind_int(stmt, 1, msg_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        send_to(clients[ci].fd, "🗑️ Message deleted\n");
        return 1;
    }
    
    /* /pin */
    if (strncmp(line, "/pin ", 5) == 0) {
        int msg_id = atoi(line + 5);
        sqlite3_stmt *stmt;
        const char *sql = "UPDATE messages SET is_pinned = 1 WHERE id = ?";
        sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
        sqlite3_bind_int(stmt, 1, msg_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        send_to(clients[ci].fd, "📌 Message pinned\n");
        return 1;
    }
    
    /* /block */
    if (strncmp(line, "/block ", 7) == 0) {
        char *username = line + 7;
        trim(username);
        send_to(clients[ci].fd, "🚫 User blocked\n");
        return 1;
    }
    
    /* /help */
    if (strncmp(line, "/help", 5) == 0) {
        send_to(clients[ci].fd, 
            "\n╔════ Available Commands ════╗\n"
            "║ /load-history [n]  - Load chat history\n"
            "║ /export            - Export to file\n"
            "║ /history [n]       - Show recent messages\n"
            "║ /search <query>    - Search messages\n"
            "║ /list              - Online users\n"
            "║ /nick <name>       - Change name\n"
            "║ /status <status>   - Change status\n"
            "║ /pm <user> <msg>   - Private message\n"
            "║ /edit <id> <text>  - Edit message\n"
            "║ /delete <id>       - Delete message\n"
            "║ /pin <id>          - Pin message\n"
            "║ /block <user>      - Block user\n"
            "║ /stats             - Statistics\n"
            "║ /quit              - Exit\n"
            "╚════════════════════════════╝\n\n");
        return 1;
    }
    
    send_to(clients[ci].fd, "❓ Unknown command. Type /help\n");
    return 1;
}

int main(void) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    
    init_database();
    
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, MAX_CLIENTS);
    
    printf("\n%s╔════════════════════════════════════════╗%s\n", CYAN, RESET);
    printf("%s║  🚀 CHAT SERVER WITH HISTORY         ║%s\n", CYAN, RESET);
    printf("%s║  Port: 8080                          ║%s\n", CYAN, RESET);
    printf("%s║  Database: chat.db                   ║%s\n", CYAN, RESET);
    printf("%s║  File export: chat_history.txt       ║%s\n", CYAN, RESET);
    printf("%s╚════════════════════════════════════════╝%s\n\n", CYAN, RESET);
    
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
        
        select((int)maxfd + 1, &read_fds, NULL, NULL, NULL);
        
        /* New connection */
        if (FD_ISSET(server_fd, &read_fds)) {
            SOCKET new_fd = accept(server_fd, NULL, NULL);
            int slot = -1;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i].active) { slot = i; break; }
            }
            if (slot == -1) {
                send(new_fd, "Server full\n", 12, 0);
                CLOSESOCK(new_fd);
            } else {
                clients[slot].fd = new_fd;
                clients[slot].active = 1;
                clients[slot].named = 0;
                clients[slot].color = slot % NUM_COLORS;
                clients[slot].last_seen = time(NULL);
                strcpy(clients[slot].status, "online");
                send_to(new_fd, "👤 Enter your name: ");
            }
        }
        
        /* Existing clients */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active || !FD_ISSET(clients[i].fd, &read_fds)) continue;
            
            memset(buffer, 0, BUF);
            int n = recv(clients[i].fd, buffer, BUF - 1, 0);
            
            if (n <= 0) {
                if (clients[i].named) {
                    char out[BUF];
                    snprintf(out, sizeof(out), "👋 %s left\n", clients[i].name);
                    broadcast(out, clients[i].fd);
                }
                CLOSESOCK(clients[i].fd);
                clients[i].active = 0;
                continue;
            }
            
            buffer[n] = '\0';
            trim(buffer);
            if (strlen(buffer) == 0) continue;
            
            clients[i].last_seen = time(NULL);
            
            /* First message = username */
            if (!clients[i].named) {
                if (find_by_name(buffer) != -1) {
                    send_to(clients[i].fd, "Name taken\n");
                    continue;
                }
                strncpy(clients[i].name, buffer, NAME_LEN - 1);
                clients[i].user_id = get_or_create_user(buffer);
                clients[i].named = 1;
                
                char out[BUF];
                snprintf(out, sizeof(out), "✅ Welcome %s! Type /help or /load-history\n", buffer);
                send_to(clients[i].fd, out);
                
                /* Show recent history to new user */
                send_to(clients[i].fd, "\n");
                send_chat_history(clients[i].fd, 10);
                
                snprintf(out, sizeof(out), "➕ %s joined\n", buffer);
                broadcast(out, clients[i].fd);
                continue;
            }
            
            /* /quit */
            if (strcmp(buffer, "/quit") == 0) {
                CLOSESOCK(clients[i].fd);
                clients[i].active = 0;
                continue;
            }
            
            /* Command? */
            if (handle_command(i, buffer)) continue;
            
            /* Normal message */
            char out[BUF + NAME_LEN];
            snprintf(out, sizeof(out), "%s[%s] %s:%s %s\n",
                     COLORS[clients[i].color],
                     format_time(time(NULL)),
                     clients[i].name,
                     RESET, buffer);
            broadcast(out, clients[i].fd);
            
            store_message(clients[i].user_id, clients[i].name, 0, buffer);
        }
    }
    

    
    sqlite3_close(db);
    CLOSESOCK(server_fd);
    return 0;
}