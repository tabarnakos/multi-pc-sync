# multi-pc-sync

Multi-pc-sync is a file synchronization utility that is optimized to be used over a high-latency link, such as the internet. The single binary has 2 modes: server and client. The drastic speed benefit of this application over rsync, is that the remote folder is never traversed, which creates a long series of short blocking messages to list the directories. No, instead the remote folder file attributes, including MD5 Sum, are fully indexed and serialized using protocol buffers. Since hash computation is CPU-intensive, the index from a previous run of the sync is loaded first and only new files are hashed.

## Server mode

In server mode, the application launches with a path argument and starts a server thread that processes messages received via TCP on what to do. Most commands handle file or folder copy, move, save, read, and remove, however one special command kicks off the indexing of the folder.

## Client mode

In client mode, the application lanched with a path argument and starts a client thread that request the index from the server and waits for the response while indexing its path. It then compares the indexes and generates a series of sync commands, that it either executes locally or sends to the server.
