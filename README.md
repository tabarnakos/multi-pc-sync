# multi-pc-sync

Multi-pc-sync is a file synchronization utility that is optimized to be used over a high-latency link, such as the internet. The single binary has 2 modes: server and client. The drastic speed benefit of this application over rsync, is that the remote folder is never traversed, which creates a long series of short blocking messages to list the directories. No, instead the remote folder file attributes, including MD5 Sum, are fully indexed and serialized using protocol buffers. Since hash computation is CPU-intensive, the index from a previous run of the sync is loaded first and only new files are hashed.

## Server mode

In server mode, the application launches with a path argument and starts a server thread that processes messages received via TCP on what to do. Most commands handle file or folder copy, move, save, read, and remove, however one special command kicks off the indexing of the folder.


In server mode, `multi-pc-sync` runs as a daemon, listening for client connections and handling file synchronization requests.

### Usage

```sh
./multi_pc_sync -d <port> [options] <path>
```

- `-d <port>`: Start the server (daemon) on the specified TCP port.
- `<path>`: The directory to synchronize. At the time of writing only canonical paths are supported.
- **Debugging Options:**
  - `-r <rate>`: Limit TCP command rate (Hz). `0` means unlimited (default: 0).
  - `--exit-after-sync`: Exit server after sending SyncDoneCmd (for unit testing).

**Example:**
```sh
./multi_pc_sync -d 9000 /srv/shared_folder
```

## Client mode

In client mode, the application launches with a path argument and starts a client thread that request the index from the server and waits for the response while indexing its path. It then compares the indexes and generates a series of sync commands, that it either executes locally or sends to the server.

### Usage

```sh
./multi_pc_sync -s <serverip:port> [options] <path>
```

- `-s <serverip:port>`: Connect to the server at `<serverip>` and `<port>`.
- `<path>`: The local directory to synchronize.
- **Options:**
  - `-r <rate>`: Limit TCP command rate (Hz). `0` means unlimited (default: 0).
  - `-y`: Skip Y/N prompt and automatically sync.
  - `--dry-run`: Print commands but don't execute them.

**Example:**
```sh
./multi_pc_sync -s 192.168.1.10:9000 /home/user/Documents
```

## Debugging multi-pc-sync

For advanced debugging, including GDB, see the scripts and tools in the `testing/` directory.  
You can use `gdbserver` to debug either the server or client process, or both. Example:

```sh
gdbserver :12345 ./multi-pc-sync -d 9000 /srv/data/server_folder
gdbserver :12346 ./multi-pc-sync -s 127.0.0.1:9000 /home/user/client_folder
```
