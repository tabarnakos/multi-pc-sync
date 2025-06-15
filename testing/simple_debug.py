#!/usr/bin/env python3

import os
import shutil
import subprocess
import threading
from pathlib import Path
from queue import Queue, Empty
import time
from rich.console import Console
from rich.text import Text

console = Console()

# Colors for server and client logs
SERVER_COLOR = "cyan"
CLIENT_COLOR = "green"

MULTI_PC_SYNC_BIN = "./build/multi_pc_sync"  # Path to the binary

def setup_dummy_file_system():
    """Create dummy file system for server and client."""
    base_dir = Path("dummy_sync_env").absolute()
    server_dir = base_dir / "server"
    client_dir = base_dir / "client"

    # Clean up any existing environment
    if base_dir.exists():
        shutil.rmtree(base_dir)

    # Create directories
    server_dir.mkdir(parents=True)
    client_dir.mkdir(parents=True)

    # Add dummy files to the server
    (server_dir / "file1.txt").write_text("Hello from server")
    (server_dir / "file2.txt").write_text("Another file")
    (server_dir / "subdir").mkdir()
    (server_dir / "subdir" / "nested.txt").write_text("Nested file")

    return server_dir, client_dir

def enqueue_output(pipe, queue):
    """Read output from a process and put it into a queue."""
    for line in iter(pipe.readline, b""):
        queue.put(line.strip())
    pipe.close()

def log_output(queue, label, color):
    """Log output from a queue with a label and color."""
    while True:
        try:
            line = queue.get(timeout=0.1)
            console.print(Text(f"[{label}] {line}", style=color))
        except Empty:
            break

def main():
    server_dir, client_dir = setup_dummy_file_system()
    port = 5555

    # Start the server
    server_proc = subprocess.Popen(
        [MULTI_PC_SYNC_BIN, "-d", str(port), "-r", "1", str(server_dir)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    server_queue = Queue()
    server_thread = threading.Thread(target=enqueue_output, args=(server_proc.stdout, server_queue))
    server_thread.daemon = True
    server_thread.start()

    # Start the client
    client_proc = subprocess.Popen(
        [MULTI_PC_SYNC_BIN, "-s", f"127.0.0.1:{port}", "-r", "1", str(client_dir)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    client_queue = Queue()
    client_thread = threading.Thread(target=enqueue_output, args=(client_proc.stdout, client_queue))
    client_thread.daemon = True
    client_thread.start()

    try:
        # Log output from both server and client
        while server_proc.poll() is None or client_proc.poll() is None:
            log_output(server_queue, "SERVER", SERVER_COLOR)
            log_output(client_queue, "CLIENT", CLIENT_COLOR)
            time.sleep(0.1)
    finally:
        # Clean up processes
        server_proc.terminate()
        client_proc.terminate()
        server_proc.wait()
        client_proc.wait()

        # Clean up file system
        shutil.rmtree(server_dir.parent)

if __name__ == "__main__":
    main()