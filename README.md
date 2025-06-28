# multi-pc-sync

Multi-pc-sync is a file synchronization utility that is optimized to be used over a high-latency link, such as the internet. The single binary has 2 modes: server and client. The drastic speed benefit of this application over rsync, is that the remote folder is never traversed, which creates a long series of short blocking messages to list the directories. No, instead the remote folder file attributes, including MD5 Sum, are fully indexed and serialized using protocol buffers. Since hash computation is CPU-intensive, the index from a previous run of the sync is loaded first and only new files are hashed.

## Server mode

In server mode, the application launches with a path argument and starts a server thread that processes messages received via TCP on what to do. Most commands handle file or folder copy, move, save, read, and remove, however one special command kicks off the indexing of the folder.

## Client mode

In client mode, the application lanched with a path argument and starts a client thread that request the index from the server and waits for the response while indexing its path. It then compares the indexes and generates a series of sync commands, that it either executes locally or sends to the server.

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
- [ ] File modified differently on both sides (conflict)

## 3. File Renaming and Moving
- [✅] File renamed on source only
- [✅] File renamed on destination only
- [ ] File moved to another folder on source
- [ ] File moved to another folder on destination
- [ ] File moved and modified simultaneously on one side
- [ ] File renamed on both sides but with different names

## 4. Directory Operations
- [✅] New directory created on source only
- [✅] New directory created on destination only
- [✅] Directory renamed on source or destination
- [✅] Directory deleted on source or destination
- [✅] Directory with content moved
- [✅] Directory with nested changes inside (combination)

## 5. Combination Scenarios
- [ ] Multiple operations on same file (e.g., modify then rename)
- [ ] Operations on files within renamed directories
- [ ] Operations on files within moved directories
- [ ] Circular rename (A→B, B→A across sides)

## 6. Conflict Scenarios
- [ ] Same file modified on both sides without sync
- [ ] File deleted on one side, modified on the other
- [ ] File moved on one side, renamed on the other
- [ ] Same file created independently on both sides with different content

## 7. Permissions and Metadata (if applicable)
- [ ] Permissions changed on one side
- [ ] Timestamps changed without content change
- [ ] Ownership or extended attributes changed

## 8. Edge Cases
- [ ] File with special characters in the name
- [ ] Very large file
- [ ] File with 0 bytes
- [ ] Filename case changes (case sensitivity issues)
- [ ] Long path names
- [ ] Files with identical hashes but different content
- [ ] Clock skew between source and destination

## 9. Repeatability and Idempotency
- [✅] Running sync twice with no changes (should be a no-op)
- [✅] Running sync after a full sync (should be fast)
- [ ] Interrupt/resume sync mid-operation
