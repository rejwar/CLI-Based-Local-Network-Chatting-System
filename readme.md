# CLI Group Chat (C)

A terminal-based group chat application written in pure C, using TCP sockets.
Multiple users connect to a central server and chat in real time. Works
**cross-platform** — the same source compiles on macOS, Linux, and Windows.

## Features

- **Group chat** — multiple clients connect, messages broadcast to everyone
- **Usernames** — each user picks a name on join (duplicates rejected)
- **Color-coded users** — every user shows in a distinct ANSI color
- **Join / leave notifications** — see who comes and goes
- **Commands:**
  - `/list` — show all online users
  - `/nick <name>` — change your username
  - `/pm <user> <message>` — send a private message
  - `/quit` — leave the chat
  - `/help` — list commands

## How it works

- **Server** uses `select()` to handle many clients on one thread —
  no per-client threads, fully event-driven (scales to 16 clients here).
- **Client** uses a small two-thread model: one thread blocks on the network
  (`recv`), the main thread blocks on the keyboard (`fgets`). This is done
  because Windows `select()` cannot watch keyboard input, so a thread keeps
  the design identical across operating systems.
- A thin platform layer (`#ifdef _WIN32`) swaps Winsock for POSIX sockets,
  so a single codebase builds everywhere.

## Build

### macOS / Linux
```bash
gcc server.c -o server
gcc client.c -o client -lpthread
```

### Windows (MinGW)
```bash
gcc server.c -o server -lws2_32
gcc client.c -o client -lws2_32
```

## Run

1. Start the server on one machine:
   ```bash
   ./server
   ```

2. Find that machine's LAN IP:
   - macOS:   `ipconfig getifaddr en0`
   - Linux:   `ip addr`
   - Windows: `ipconfig`

3. Connect clients (on the same Wi-Fi/LAN):
   ```bash
   ./client 127.0.0.1        # same machine (testing)
   ./client 192.168.0.105    # another machine — use the server's LAN IP
   ```

All machines must be on the same local network. The default port is 8080.

## Troubleshooting

### `bind() failed` when starting the server
Port 8080 is still held by a previous server that didn't shut down cleanly.
Free the port, then start again:

```bash
# macOS / Linux
lsof -ti:8080 | xargs kill -9
./server
```

To avoid this, always stop the server with **Ctrl+C** rather than just
closing the terminal window.

### `Connection refused` on the client
- Make sure the server is actually running first.
- Check the IP. Same machine → use `127.0.0.1`. Another machine → use the
  server's LAN IP (`ipconfig getifaddr en0` on macOS).
- Both machines must be on the same Wi-Fi / LAN.

### Windows: `client.exe is not recognized`
PowerShell needs `.\` before a local program:

```powershell
.\client.exe 192.168.0.102
```

### Client connects but no messages get through
The server machine's firewall may be blocking incoming connections.
On macOS: System Settings → Network → Firewall → turn off for testing.