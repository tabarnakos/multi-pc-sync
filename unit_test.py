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

def run_sync_pair(test_id, server_dir, client_dir, port, server_lines, client_lines, max_lines=30):
    server_proc = subprocess.Popen(
        [MULTI_PC_SYNC_BIN, "-d", str(port), str(server_dir.resolve())],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    client_proc = subprocess.Popen(
        [MULTI_PC_SYNC_BIN, "-s", f"127.0.0.1:{port}", str(client_dir.resolve())],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        stdin=subprocess.PIPE  # Needed for auto-answer
    )

    server_queue = Queue()
    client_queue = Queue()
    server_thread = threading.Thread(target=enqueue_output, args=(server_proc.stdout, server_queue))
    client_thread = threading.Thread(target=enqueue_output, args=(client_proc.stdout, client_queue, client_proc))
    server_thread.daemon = True
    client_thread.daemon = True
    server_thread.start()
    client_thread.start()

    last_activity = time.time()
    timeout = 3.0  # seconds

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
        if time.time() - last_activity > timeout:
            server_proc.kill()
            client_proc.kill()
            break

    server_thread.join(timeout=1)
    client_thread.join(timeout=1)

def assert_file_exists(path, should_exist=True):
    result = Path(path).exists() == should_exist
    msg = f"{path} {'exists' if should_exist else 'does not exist'}"
    return (result, msg)

def assertion_report(assertions, test_name):
    console.print(colored(f"\nAssertion Report for {test_name}:", CYAN))
    passed = 0
    for result, msg in assertions:
        if result:
            console.print(colored(f"PASS: {msg}", GREEN))
            passed += 1
        else:
            console.print(colored(f"FAIL: {msg}", RED))
    console.print(colored(f"Passed {passed}/{len(assertions)} assertions.", YELLOW))
    return passed, len(assertions)

def wait_for_files(files, timeout=5.0):
    """Wait until all files exist, or timeout."""
    start = time.time()
    while time.time() - start < timeout:
        if all(f.exists() for f in files):
            return True
        time.sleep(0.1)
    return False

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

def test_scenario(test_id, global_results):
    base = Path(f"test_sync_env_{test_id}").absolute()
    server_dir = base / "server"
    client_dir = base / "client"
    port = 5555 + test_id

    if base.exists():
        shutil.rmtree(base)
    server_dir.mkdir(parents=True)
    client_dir.mkdir(parents=True)

    # Initial files
    (server_dir / "file1.txt").write_text("Hello from server")
    (server_dir / "file2.txt").write_text("Another file")
    (server_dir / "subdir").mkdir()
    (server_dir / "subdir" / "nested.txt").write_text("Nested file")

    # Initial sync
    console.print(colored(f"\n[{test_id}] Running initial sync...", CYAN))
    run_sync_pair(test_id, server_dir, client_dir, port, [], [])
    # Wait until files exist before proceeding
    expected_files = [
        client_dir / "file1.txt",
        client_dir / "file2.txt",
        client_dir / "subdir" / "nested.txt"
    ]
    if not wait_for_files(expected_files):
        console.print(colored(f"[{test_id}] ERROR: Initial sync did not create expected files.", RED))

    assertions = []
    assertions.append(assert_file_exists(client_dir / "file1.txt"))
    assertions.append(assert_file_exists(client_dir / "file2.txt"))
    assertions.append(assert_file_exists(client_dir / "subdir" / "nested.txt"))

    # Simulate file actions
    # 1. Modify file1.txt on client
    if (client_dir / "file1.txt").exists():
        (client_dir / "file1.txt").write_text("Modified on client")
    # 2. Touch file2.txt on server
    if (server_dir / "file2.txt").exists():
        os.utime(server_dir / "file2.txt", None)
    # 3. Delete nested.txt on client
    if (client_dir / "subdir" / "nested.txt").exists():
        os.remove((client_dir / "subdir" / "nested.txt").as_posix())
    # 4. Move file2.txt to file3.txt on server
    if (server_dir / "file2.txt").exists():
        shutil.move(server_dir / "file2.txt", server_dir / "file3.txt")
    # 5. Simultaneous edit: both server and client modify file1.txt
    if (server_dir / "file1.txt").exists():
        (server_dir / "file1.txt").write_text("Modified on server")

    # 6. Simultaneous delete: both server and client delete file3.txt (if exists)
    if (client_dir / "file3.txt").exists():
        os.remove(client_dir / "file3.txt")
    if (server_dir / "file3.txt").exists():
        os.remove(server_dir / "file3.txt")

    # 7. Simultaneous move: move file1.txt to file1_renamed.txt on both
    if (server_dir / "file1.txt").exists():
        shutil.move(server_dir / "file1.txt", server_dir / "file1_renamed.txt")
    if (client_dir / "file1.txt").exists():
        shutil.move(client_dir / "file1.txt", client_dir / "file1_renamed.txt")

    # 8. Create new file on both sides with same name but different content
    (server_dir / "conflict.txt").write_text("Server version")
    (client_dir / "conflict.txt").write_text("Client version")

    console.print(colored(f"\n[{test_id}] Running sync after modifications...", CYAN))
    run_sync_pair(test_id, server_dir, client_dir, port, [], [])
    time.sleep(0.5)

    # Assertions after sync
    assertions.append(assert_file_exists(client_dir / "file1_renamed.txt"))
    assertions.append(assert_file_exists(client_dir / "file3.txt", should_exist=False))
    assertions.append(assert_file_exists(client_dir / "subdir" / "nested.txt", should_exist=False))
    assertions.append(assert_file_exists(client_dir / "conflict.txt"))

    # Check file contents for conflict resolution (optional, depends on your sync logic)
    # Here we just print the contents
    conflict_path = client_dir / "conflict.txt"
    if conflict_path.exists():
        console.print(colored(f"[{test_id}] conflict.txt content: {conflict_path.read_text()}", YELLOW))

    passed, total = assertion_report(assertions, f"Test {test_id}")
    global_results.append((passed, total))

    # Cleanup: delete the test environment directory after assertions
    if base.exists():
        shutil.rmtree(base)

def main():
    num_tests = 2  # 2 simultaneous tests for 4 panels
    ports = [5555 + i for i in range(num_tests)]
    server_lines = [[] for _ in range(num_tests)]
    client_lines = [[] for _ in range(num_tests)]
    threads = []

    envs = [make_test_env(i) for i in range(num_tests)]

    def run_test(idx):
        run_sync_pair(idx, envs[idx][0], envs[idx][1], ports[idx], server_lines[idx], client_lines[idx])

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
    layout["lower"].split_row(
        Layout(name="server1"),
        Layout(name="client1")
    )

    with Live(layout, refresh_per_second=10, screen=True, console=console):
        while any(t.is_alive() for t in threads):
            layout["server0"].update(Panel("\n".join(server_lines[0]), title="Server 0", border_style="cyan"))
            layout["client0"].update(Panel("\n".join(client_lines[0]), title="Client 0", border_style="green"))
            layout["server1"].update(Panel("\n".join(server_lines[1]), title="Server 1", border_style="cyan"))
            layout["client1"].update(Panel("\n".join(client_lines[1]), title="Client 1", border_style="green"))
            time.sleep(0.1)

    for t in threads:
        t.join()

    # Optionally, run assertions after all tests
    global_results = []
    for i in range(num_tests):
        test_scenario(i, global_results)

    # Global assertion report
    total_passed = sum(p for p, t in global_results)
    total_asserts = sum(t for p, t in global_results)
    console.print(colored(f"\nGlobal assertion report: Passed {total_passed}/{total_asserts} assertions.", CYAN if total_passed == total_asserts else RED))
    input("Press Enter to exit...")

if __name__ == "__main__":
    main()