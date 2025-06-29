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
- **Options:**
  - `--cfg=<path>`: Path to the config file. Configures behavior on file conflicts.
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
  - `--cfg=<path>`: Path to the config file. Configures behavior on file conflicts.

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

# Features / Test results

Call it a feature or a bug, I don't know if the other sync programs have such extensive testing, but this one does. My goal is not necessarily to have everything green, rather is that whenever there is data being overwritten, there should be an option somewhere.

## ✅ File Sync Test Scenarios

## 1. File Creation and Deletion
- [✅] File created on source only
- [✅] File created on destination only
- [✅] File created on both sides with identical content
- [❗] File created on both sides with different content (currently, the client-side wins over and overwrites the server side. I plan to make this a configurable option in the future)
- [✅] File deleted on source only
- [✅] File deleted on destination only
- [✅] File deleted on both sides

## 2. File Modification
- [✅] File modified on source only
- [✅] File modified on destination only
- [❗] File modified differently on both sides (conflict) (currently, the client-side wins over and overwrites the server side. I plan to make this a configurable option in the future)

## 3. File Renaming and Moving
- [✅] File renamed on source only
- [✅] File renamed on destination only
- [✅] File moved to another folder on source
- [✅] File moved to another folder on destination
- [✅] File moved and modified simultaneously on one side
- [✅] File renamed on both sides but with different names

## 4. Directory Operations
- [✅] New directory created on source only
- [✅] New directory created on destination only
- [✅] Directory renamed on source or destination
- [✅] Directory deleted on source or destination
- [✅] Directory with content moved
- [✅] Directory with nested changes inside (combination)

## 5. Combination Scenarios
- [✅] Multiple operations on same file - modify then rename
- [✅] Operations on files within renamed directories
- [✅] Operations on files within moved directories
- [❗] Circular rename (A→B, B→A across sides)  (This use-case is not yet supported, and difficult to detect)

## 6. Conflict Scenarios
- [❗] Same file modified on both sides without sync (currently, the client-side wins over and overwrites the server side. I plan to make this a configurable option in the future)
- [❗] File deleted on the server, modified on the client (currently causes data loss)
- [❗] File deleted on the client, modified on the server (currently causes data loss)
- [❗] File moved on the server, renamed on the client    (currently causes data loss)
- [❗] File moved on the client, renamed on the server    (currently causes data loss)

## 7. Permissions and Metadata (if applicable)
- [ ] Permissions changed on one side
- [ ] Timestamps changed without content change
- [ ] Ownership or extended attributes changed

## 8. Edge Cases
- [ ] File with special characters in the name
- [✅] Very large file (10GB)
- [✅] File with 0 bytes
- [✅] Filename case changes (case sensitivity issues)
- [ ] Long path names
- [ ] Files with identical hashes but different content
- [ ] Clock skew between source and destination

## 9. Repeatability and Idempotency
- [✅] Running sync twice with no changes (should be a no-op)
- [✅] Running sync after a full sync (should be fast)
- [ ] Interrupt/resume sync mid-operation
