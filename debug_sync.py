#!/usr/bin/env python3

import os
import shutil
import subprocess
import time
import threading
from pathlib import Path
from queue import Queue, Empty
import sys
import re
import psutil
import socket

from rich.console import Console
from rich.panel import Panel
from rich.layout import Layout
from rich.live import Live
from rich.text import Text

console = Console()

GREEN = "green"
RED = "red"
YELLOW = "yellow"
CYAN = "cyan"

def colored(msg, color):
    return Text(msg, style=color)

MULTI_PC_SYNC_BIN = "./build/multi_pc_sync"  # Change this if your binary is elsewhere
GDB_PORTS = [12345, 12346]

if not Path(MULTI_PC_SYNC_BIN).exists():
    console.print(f"[red]ERROR: {MULTI_PC_SYNC_BIN} not found![/red]")
    console.print(f"Current working directory: {os.getcwd()}")
    sys.exit(1)

def enqueue_output(pipe, queue, proc=None):
    for line in iter(pipe.readline, b''):
        decoded = line.decode(errors='replace')
        queue.put(decoded)
        # Auto-answer y/n prompts for client
        if proc and re.search(r"\by/n\b", decoded, re.IGNORECASE):
            try:
                proc.stdin.write(b"y\n")
                proc.stdin.flush()
            except Exception:
                pass
    pipe.close()

# Store process and port info for monitoring
proc_info = {
    'server_proc': None,
    'client_proc': None,
    'server_gdb_port': None,
    'client_gdb_port': None
}

def run_debug_sync_pair(test_id, server_dir, client_dir, port, server_lines, client_lines, max_lines=30):
    server_gdb_port = GDB_PORTS[0] + test_id * 2
    client_gdb_port = GDB_PORTS[1] + test_id * 2
    server_proc = subprocess.Popen(
        ["gdbserver", f":{server_gdb_port}", MULTI_PC_SYNC_BIN, "-d", str(port), "-r", "1", str(server_dir.resolve())],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    client_proc = subprocess.Popen(
        ["gdbserver", f":{client_gdb_port}", MULTI_PC_SYNC_BIN, "-s", f"127.0.0.1:{port}", "-r", "1", str(client_dir.resolve())],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        stdin=subprocess.PIPE  # Needed for auto-answer
    )
    # Save for monitoring
    proc_info['server_proc'] = server_proc
    proc_info['client_proc'] = client_proc
    proc_info['server_gdb_port'] = server_gdb_port
    proc_info['client_gdb_port'] = client_gdb_port

    server_queue = Queue()
    client_queue = Queue()
    server_thread = threading.Thread(target=enqueue_output, args=(server_proc.stdout, server_queue))
    client_thread = threading.Thread(target=enqueue_output, args=(client_proc.stdout, client_queue, client_proc))
    server_thread.daemon = True
    client_thread.daemon = True
    server_thread.start()
    client_thread.start()

    last_activity = time.time()
    timeout = 0  # seconds, longer for debugging

    while True:
        activity = False
        try:
            line = server_queue.get(timeout=0.1)
            server_lines.append(line.rstrip())
            if len(server_lines) > max_lines:
                del server_lines[0]
            activity = True
        except Empty:
            pass
        try:
            line = client_queue.get(timeout=0.1)
            client_lines.append(line.rstrip())
            if len(client_lines) > max_lines:
                del client_lines[0]
            activity = True
        except Empty:
            pass

        if activity:
            last_activity = time.time()
        if (server_proc.poll() is not None and client_proc.poll() is not None):
            break
        if timeout > 0 and time.time() - last_activity > timeout:
            server_proc.kill()
            client_proc.kill()
            break

    server_thread.join(timeout=1)
    client_thread.join(timeout=1)

# Helper to get gdbserver/debug status for a process
def get_gdbserver_status(proc, gdb_port):
    if proc is None:
        return "[grey]Not started[/grey]"
    if proc.poll() is not None:
        return "[red]Exited[/red]"
    try:
        for conn in psutil.net_connections(kind='tcp'):
            if conn.laddr.port == gdb_port:
                if conn.status == psutil.CONN_LISTEN:
                    return "[yellow]gdbserver started, waiting for debugger[/yellow]"
                elif conn.status == psutil.CONN_ESTABLISHED:
                    return "[green]Remote debugging session started[/green]"
        return "[cyan]Running[/cyan]"
    except Exception:
        return "[grey]Unknown[/grey]"

def get_proc_stats(proc):
    try:
        p = psutil.Process(proc.pid)
        cpu = p.cpu_percent(interval=0.1)
        mem = p.memory_info().rss // 1024
        io = p.io_counters()
        net = p.connections(kind='inet')
        return cpu, mem, io, net
    except Exception:
        return 0, 0, None, []

def main():
    num_tests = 1  # Only one debug pair for simplicity
    ports = [5555 + i for i in range(num_tests)]
    server_lines = [[] for _ in range(num_tests)]
    client_lines = [[] for _ in range(num_tests)]
    threads = []

    def make_test_env(test_id):
        base = Path(f"test_sync_env_{test_id}").absolute()
        server_dir = base / "server"
        client_dir = base / "client"
        if base.exists():
            shutil.rmtree(base)
        server_dir.mkdir(parents=True)
        client_dir.mkdir(parents=True)
        (server_dir / "file1.txt").write_text("Hello from server")
        (server_dir / "file2.txt").write_text("Another file")
        (server_dir / "subdir").mkdir()
        (server_dir / "subdir" / "nested.txt").write_text("Nested file")
        return server_dir, client_dir

    envs = [make_test_env(i) for i in range(num_tests)]

    def run_test(idx):
        run_debug_sync_pair(idx, envs[idx][0], envs[idx][1], ports[idx], server_lines[idx], client_lines[idx])

    for i in range(num_tests):
        t = threading.Thread(target=run_test, args=(i,))
        t.start()
        threads.append(t)

    layout = Layout()
    layout.split_column(
        Layout(name="upper"),
        Layout(name="lower")
    )
    layout["upper"].split_row(
        Layout(name="server0"),
        Layout(name="client0")
    )

    max_lines = 30  # or set to your preferred value
    with Live(layout, refresh_per_second=10, screen=True, console=console):
        while any(t.is_alive() for t in threads):
            # Dynamically determine max_lines based on terminal height
            term_height, _ = shutil.get_terminal_size((80, 24))
            # Reserve some lines for UI, set at least 5 lines per panel
            max_lines = max((term_height // 2) - 5, 5)
            server0_lines = server_lines[0][-max_lines:]
            client0_lines = client_lines[0][-max_lines:]
            layout["server0"].update(Panel("\n".join(server0_lines), title="Server 0 (gdbserver)", border_style="cyan"))
            layout["client0"].update(Panel("\n".join(client0_lines), title="Client 0 (gdbserver)", border_style="green"))

            # --- Debug/monitoring info for lower window ---
            server_status = get_gdbserver_status(proc_info['server_proc'], proc_info['server_gdb_port'])
            client_status = get_gdbserver_status(proc_info['client_proc'], proc_info['client_gdb_port'])
            server_cpu, server_mem, server_io, server_net = get_proc_stats(proc_info['server_proc'])
            client_cpu, client_mem, client_io, client_net = get_proc_stats(proc_info['client_proc'])
            def io_summary(io):
                if not io: return "?"
                return f"R: {io.read_bytes//1024}K W: {io.write_bytes//1024}K"
            def net_summary(net):
                if not net: return "?"
                return f"Conns: {len(net)}"
            lower_info = f"[b]Server:[/b] {server_status} | CPU: {server_cpu:.1f}% | Mem: {server_mem}K | Disk: {io_summary(server_io)} | Net: {net_summary(server_net)}\n"
            lower_info += f"[b]Client:[/b] {client_status} | CPU: {client_cpu:.1f}% | Mem: {client_mem}K | Disk: {io_summary(client_io)} | Net: {net_summary(client_net)}\n"
            layout["lower"].update(Panel(lower_info, title="Debug/Resource Monitor", border_style="magenta"))
            time.sleep(0.1)

    for t in threads:
        t.join()

if __name__ == "__main__":
    main()
