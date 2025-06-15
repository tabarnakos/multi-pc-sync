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
import termios
import tty
import select
from dataclasses import dataclass
from typing import List, Dict, Any
from datetime import datetime, timedelta

from rich.console import Console
from rich.panel import Panel
from rich.layout import Layout
from rich.live import Live
from rich.text import Text
from rich.align import Align
from rich import box
from rich.prompt import Prompt
from rich.table import Table
from rich.progress import Progress, SpinnerColumn, TextColumn

console = Console()

GREEN = "green"
RED = "red"
YELLOW = "yellow"
CYAN = "cyan"
BLUE = "blue"
MAGENTA = "magenta"

def colored(msg, color):
    return Text(msg, style=color)

MULTI_PC_SYNC_BIN = "./build/multi_pc_sync"  # Change this if your binary is elsewhere
GDB_PORTS = [12345, 12346]

if not Path(MULTI_PC_SYNC_BIN).exists():
    console.print(f"[red]ERROR: {MULTI_PC_SYNC_BIN} not found![/red]")
    console.print(f"Current working directory: {os.getcwd()}")
    sys.exit(1)

# Test result tracking classes
@dataclass
class TestMetrics:
    """Test execution metrics"""
    start_time: datetime
    end_time: datetime = None
    server_errors: List[str] = None
    client_errors: List[str] = None
    sync_operations: int = 0
    files_transferred: int = 0
    bytes_transferred: int = 0
    network_connections: int = 0
    max_memory_usage: Dict[str, int] = None
    average_cpu_usage: Dict[str, float] = None
    
    def __post_init__(self):
        if self.server_errors is None:
            self.server_errors = []
        if self.client_errors is None:
            self.client_errors = []
        if self.max_memory_usage is None:
            self.max_memory_usage = {'server': 0, 'client': 0}
        if self.average_cpu_usage is None:
            self.average_cpu_usage = {'server': 0.0, 'client': 0.0}
    
    @property
    def duration(self) -> timedelta:
        """Get test duration"""
        if self.end_time:
            return self.end_time - self.start_time
        return datetime.now() - self.start_time
    
    def evaluate_success(self, test_case_name: str, server_dir: Path, client_dir: Path) -> bool:
        """Determine if test was successful based on test case and directory comparison"""
        # First check for errors
        if len(self.server_errors) > 0 or len(self.client_errors) > 0:
            return False
        
        # Then check test-specific success criteria
        return self._check_test_specific_success(test_case_name, server_dir, client_dir)
    
    def _check_test_specific_success(self, test_case_name: str, server_dir: Path, client_dir: Path) -> bool:
        """Check test-specific success criteria"""
        try:
            server_files = self._get_directory_files(server_dir)
            client_files = self._get_directory_files(client_dir)
            
            if test_case_name == "initial_sync":
                # Success: Client should have the same files as server after sync
                return len(server_files) > 0 and server_files == client_files
                
            elif test_case_name == "file_removal_server":
                # Success: File removed from server should also be removed from client
                return len(server_files) < 3 and server_files == client_files  # Assuming we started with 3 files
                
            elif test_case_name == "file_removal_client":
                # Success: File removed from client should also be removed from server
                return len(client_files) < 3 and server_files == client_files
                
            elif test_case_name in ["file_added_server", "file_added_client"]:
                # Success: New files should be synchronized
                return len(server_files) > 0 and server_files == client_files
                
            elif test_case_name in ["file_moved_server", "file_moved_client"]:
                # Success: Moved/renamed files should be synchronized
                return len(server_files) > 0 and server_files == client_files
                
            elif test_case_name == "file_conflict":
                # Success: Conflict should be resolved (both sides should have same resolution)
                return server_files == client_files
                
            elif test_case_name == "directory_operations":
                # Success: Directory operations should be synchronized
                server_dirs = self._get_directory_structure(server_dir)
                client_dirs = self._get_directory_structure(client_dir)
                return server_dirs == client_dirs
                
            elif test_case_name == "large_file_sync":
                # Success: Large files should be transferred completely
                return len(server_files) > 0 and server_files == client_files
                
            elif test_case_name in ["complex_scenario", "simultaneous_operations"]:
                # Success: All operations should result in synchronized state
                return len(server_files) > 0 and server_files == client_files
                
            else:
                # Default: Just check that directories are synchronized
                return server_files == client_files
                
        except Exception as e:
            console.print(f"[red]Error evaluating test success: {e}[/red]")
            return False
    
    def _get_directory_files(self, directory: Path) -> set:
        """Get set of all files in directory with their relative paths and sizes"""
        files = set()
        try:
            for item in directory.rglob('*'):
                if item.is_file():
                    rel_path = item.relative_to(directory)
                    file_size = item.stat().st_size
                    files.add((str(rel_path), file_size))
        except Exception:
            pass
        return files
    
    def _get_directory_structure(self, directory: Path) -> set:
        """Get set of all directories in directory tree"""
        dirs = set()
        try:
            for item in directory.rglob('*'):
                if item.is_dir():
                    rel_path = item.relative_to(directory)
                    dirs.add(str(rel_path))
        except Exception:
            pass
        return dirs
    
    @property
    def success(self) -> bool:
        """Legacy success property - now requires explicit evaluation"""
        return len(self.server_errors) == 0 and len(self.client_errors) == 0

class TestReportGenerator:
    """Generate colorful test reports"""
    
    def __init__(self, console: Console):
        self.console = console
    
    def generate_report(self, test_case: 'TestCase', metrics: TestMetrics, server_dir: Path, client_dir: Path):
        """Generate a comprehensive test report"""
        self.console.clear()
        
        # Evaluate test success properly
        test_success = metrics.evaluate_success(test_case.name, server_dir, client_dir)
        
        # Header
        self._print_header(test_case, metrics, test_success)
        
        # Summary
        self._print_summary(metrics, test_success)
        
        # File System Analysis
        self._print_filesystem_analysis(server_dir, client_dir)
        
        # Performance Metrics
        self._print_performance_metrics(metrics)
        
        # Error Analysis
        if not test_success:
            self._print_error_analysis(metrics)
        
        # Footer
        self._print_footer(metrics, test_success)
    
    def _print_header(self, test_case: 'TestCase', metrics: TestMetrics, test_success: bool):
        """Print colorful header"""
        status_color = "green" if test_success else "red"
        status_text = "✅ PASSED" if test_success else "❌ FAILED"
        
        header_text = f"[bold {status_color}]{status_text}[/bold {status_color}] - {test_case.name}"
        self.console.print(Panel(
            Align.center(header_text), 
            title="Test Report", 
            border_style=status_color,
            box=box.DOUBLE
        ))
        
        self.console.print(f"[dim]Description: {test_case.description}[/dim]")
        self.console.print(f"[dim]Duration: {metrics.duration}[/dim]\n")
    
    def _print_summary(self, metrics: TestMetrics, test_success: bool):
        """Print test summary"""
        table = Table(title="Test Summary", show_header=True, header_style="bold magenta")
        table.add_column("Metric", style="cyan", width=20)
        table.add_column("Value", style="yellow", width=15)
        table.add_column("Status", style="green", width=10)
        
        # Add summary rows
        table.add_row("Overall Result", "PASSED" if test_success else "FAILED", "✅" if test_success else "❌")
        table.add_row("Sync Operations", str(metrics.sync_operations), "✅" if metrics.sync_operations > 0 else "⚠️")
        table.add_row("Files Transferred", str(metrics.files_transferred), "✅" if metrics.files_transferred >= 0 else "⚠️")
        table.add_row("Data Transferred", f"{metrics.bytes_transferred} bytes", "✅" if metrics.bytes_transferred >= 0 else "⚠️")
        table.add_row("Network Connections", str(metrics.network_connections), "✅" if metrics.network_connections > 0 else "⚠️")
        table.add_row("Server Errors", str(len(metrics.server_errors)), "✅" if len(metrics.server_errors) == 0 else "❌")
        table.add_row("Client Errors", str(len(metrics.client_errors)), "✅" if len(metrics.client_errors) == 0 else "❌")
        
        self.console.print(table)
        self.console.print()
    
    def _print_filesystem_analysis(self, server_dir: Path, client_dir: Path):
        """Print filesystem analysis"""
        def analyze_directory(directory: Path) -> Dict[str, Any]:
            """Analyze directory contents"""
            if not directory.exists():
                return {"files": 0, "directories": 0, "total_size": 0, "items": []}
            
            items = []
            total_size = 0
            file_count = 0
            dir_count = 0
            
            for item in directory.rglob("*"):
                relative_path = item.relative_to(directory)
                if item.is_file():
                    size = item.stat().st_size
                    total_size += size
                    file_count += 1
                    items.append({"name": str(relative_path), "type": "file", "size": size})
                elif item.is_dir():
                    dir_count += 1
                    items.append({"name": str(relative_path), "type": "directory", "size": 0})
            
            return {
                "files": file_count,
                "directories": dir_count,
                "total_size": total_size,
                "items": items
            }
        
        server_analysis = analyze_directory(server_dir)
        client_analysis = analyze_directory(client_dir)
        
        # Create comparison table
        table = Table(title="File System Analysis", show_header=True, header_style="bold blue")
        table.add_column("Location", style="cyan", width=15)
        table.add_column("Files", style="green", width=8)
        table.add_column("Directories", style="yellow", width=12)
        table.add_column("Total Size", style="magenta", width=12)
        table.add_column("Status", style="white", width=10)
        
        def format_size(bytes_size: int) -> str:
            """Format byte size to human readable"""
            for unit in ['B', 'KB', 'MB', 'GB']:
                if bytes_size < 1024:
                    return f"{bytes_size:.1f} {unit}"
                bytes_size /= 1024
            return f"{bytes_size:.1f} TB"
        
        # Server row
        server_status = "✅ Sync" if server_analysis["files"] == client_analysis["files"] else "⚠️ Diff"
        table.add_row(
            "Server", 
            str(server_analysis["files"]), 
            str(server_analysis["directories"]),
            format_size(server_analysis["total_size"]),
            server_status
        )
        
        # Client row
        client_status = "✅ Sync" if server_analysis["files"] == client_analysis["files"] else "⚠️ Diff"
        table.add_row(
            "Client", 
            str(client_analysis["files"]), 
            str(client_analysis["directories"]),
            format_size(client_analysis["total_size"]),
            client_status
        )
        
        self.console.print(table)
        
        # Show file differences if any
        server_files = {item["name"] for item in server_analysis["items"] if item["type"] == "file"}
        client_files = {item["name"] for item in client_analysis["items"] if item["type"] == "file"}
        
        if server_files != client_files:
            self.console.print("\n[yellow]File Differences Detected:[/yellow]")
            
            only_server = server_files - client_files
            only_client = client_files - server_files
            
            if only_server:
                self.console.print(f"[red]Only on server:[/red] {', '.join(only_server)}")
            if only_client:
                self.console.print(f"[blue]Only on client:[/blue] {', '.join(only_client)}")
        
        self.console.print()
    
    def _print_performance_metrics(self, metrics: TestMetrics):
        """Print performance metrics"""
        table = Table(title="Performance Metrics", show_header=True, header_style="bold green")
        table.add_column("Component", style="cyan", width=15)
        table.add_column("Max Memory (KB)", style="yellow", width=15)
        table.add_column("Avg CPU (%)", style="magenta", width=12)
        table.add_column("Performance", style="green", width=12)
        
        # Server metrics
        server_mem = metrics.max_memory_usage["server"]
        server_cpu = metrics.average_cpu_usage["server"]
        server_perf = "🚀 Excellent" if server_mem < 10000 and server_cpu < 50 else "⚡ Good" if server_mem < 50000 and server_cpu < 80 else "⚠️ High"
        
        table.add_row("Server", f"{server_mem:,}", f"{server_cpu:.1f}", server_perf)
        
        # Client metrics
        client_mem = metrics.max_memory_usage["client"]
        client_cpu = metrics.average_cpu_usage["client"]
        client_perf = "🚀 Excellent" if client_mem < 10000 and client_cpu < 50 else "⚡ Good" if client_mem < 50000 and client_cpu < 80 else "⚠️ High"
        
        table.add_row("Client", f"{client_mem:,}", f"{client_cpu:.1f}", client_perf)
        
        self.console.print(table)
        self.console.print()
    
    def _print_error_analysis(self, metrics: TestMetrics):
        """Print error analysis"""
        if metrics.server_errors:
            self.console.print("[bold red]Server Errors:[/bold red]")
            for i, error in enumerate(metrics.server_errors[-5:], 1):  # Show last 5 errors
                self.console.print(f"  {i}. [red]{error}[/red]")
            if len(metrics.server_errors) > 5:
                self.console.print(f"  ... and {len(metrics.server_errors) - 5} more errors")
            self.console.print()
        
        if metrics.client_errors:
            self.console.print("[bold red]Client Errors:[/bold red]")
            for i, error in enumerate(metrics.client_errors[-5:], 1):  # Show last 5 errors
                self.console.print(f"  {i}. [red]{error}[/red]")
            if len(metrics.client_errors) > 5:
                self.console.print(f"  ... and {len(metrics.client_errors) - 5} more errors")
            self.console.print()
    
    def _print_footer(self, metrics: TestMetrics, test_success: bool):
        """Print report footer"""
        if test_success:
            footer_text = "🎉 Test completed successfully! All sync operations worked as expected."
            footer_color = "green"
        else:
            footer_text = "⚠️ Test completed with issues. Review errors above for debugging."
            footer_color = "red"
        
        self.console.print(Panel(
            Align.center(footer_text),
            title="Result",
            border_style=footer_color,
            box=box.ROUNDED
        ))
        
        self.console.print(f"\n[dim]Report generated at: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}[/dim]")
        self.console.print("[blue]Press any key to continue...[/blue]")

class TestCase:
    def __init__(self, name, description, setup_func):
        self.name = name
        self.description = description
        self.setup_func = setup_func

# Test case setup functions
def setup_initial_sync(server_dir, client_dir):
    """Initial sync: Clean directories, add files to server"""
    clean_directories(server_dir, client_dir)
    
    # Add files to server
    (server_dir / "file1.txt").write_text("Hello from server")
    (server_dir / "file2.txt").write_text("Another file")
    (server_dir / "subdir").mkdir()
    (server_dir / "subdir" / "nested.txt").write_text("Nested file")
    console.print(f"[green]Setup: Added files to server directory[/green]")

def setup_file_removal_server(server_dir, client_dir):
    """File removal on server: Start with synced files, remove one on server"""
    setup_initial_sync(server_dir, client_dir)
    # Simulate previous sync by copying files to client
    shutil.copytree(server_dir, client_dir, dirs_exist_ok=True)
    
    # Remove file on server
    (server_dir / "file2.txt").unlink()
    console.print(f"[red]Setup: Removed file2.txt from server[/red]")

def setup_file_removal_client(server_dir, client_dir):
    """File removal on client: Start with synced files, remove one on client"""
    setup_initial_sync(server_dir, client_dir)
    # Simulate previous sync by copying files to client
    shutil.copytree(server_dir, client_dir, dirs_exist_ok=True)
    
    # Remove file on client
    (client_dir / "file1.txt").unlink()
    console.print(f"[red]Setup: Removed file1.txt from client[/red]")

def setup_file_added_server(server_dir, client_dir):
    """File added on server: Start with synced files, add new file on server"""
    setup_initial_sync(server_dir, client_dir)
    # Simulate previous sync by copying files to client
    shutil.copytree(server_dir, client_dir, dirs_exist_ok=True)
    
    # Add new file on server
    (server_dir / "new_server_file.txt").write_text("New file from server")
    console.print(f"[green]Setup: Added new_server_file.txt to server[/green]")

def setup_file_added_client(server_dir, client_dir):
    """File added on client: Start with synced files, add new file on client"""
    setup_initial_sync(server_dir, client_dir)
    # Simulate previous sync by copying files to client
    shutil.copytree(server_dir, client_dir, dirs_exist_ok=True)
    
    # Add new file on client
    (client_dir / "new_client_file.txt").write_text("New file from client")
    console.print(f"[green]Setup: Added new_client_file.txt to client[/green]")

def setup_file_moved_server(server_dir, client_dir):
    """File moved on server: Start with synced files, move/rename file on server"""
    setup_initial_sync(server_dir, client_dir)
    # Simulate previous sync by copying files to client
    shutil.copytree(server_dir, client_dir, dirs_exist_ok=True)
    
    # Move file on server
    (server_dir / "file1.txt").rename(server_dir / "file1_moved.txt")
    console.print(f"[yellow]Setup: Moved file1.txt to file1_moved.txt on server[/yellow]")

def setup_file_moved_client(server_dir, client_dir):
    """File moved on client: Start with synced files, move/rename file on client"""
    setup_initial_sync(server_dir, client_dir)
    # Simulate previous sync by copying files to client
    shutil.copytree(server_dir, client_dir, dirs_exist_ok=True)
    
    # Move file on client
    (client_dir / "file2.txt").rename(client_dir / "file2_moved.txt")
    console.print(f"[yellow]Setup: Moved file2.txt to file2_moved.txt on client[/yellow]")

def setup_file_conflict(server_dir, client_dir):
    """File conflict: Both sides modify the same file"""
    setup_initial_sync(server_dir, client_dir)
    # Simulate previous sync by copying files to client
    shutil.copytree(server_dir, client_dir, dirs_exist_ok=True)
    
    # Modify same file on both sides
    (server_dir / "file1.txt").write_text("Modified by server")
    (client_dir / "file1.txt").write_text("Modified by client")
    console.print(f"[magenta]Setup: Modified file1.txt on both server and client (conflict)[/magenta]")

def setup_directory_operations(server_dir, client_dir):
    """Directory operations: Create/delete directories"""
    setup_initial_sync(server_dir, client_dir)
    # Simulate previous sync by copying files to client
    shutil.copytree(server_dir, client_dir, dirs_exist_ok=True)
    
    # Create new directory on server
    (server_dir / "new_dir").mkdir()
    (server_dir / "new_dir" / "file_in_new_dir.txt").write_text("File in new directory")
    
    # Remove directory on client
    shutil.rmtree(client_dir / "subdir")
    console.print(f"[blue]Setup: Added new_dir on server, removed subdir on client[/blue]")

def setup_large_file_sync(server_dir, client_dir):
    """Large file sync: Test with larger files"""
    setup_initial_sync(server_dir, client_dir)
    
    # Create a larger file on server
    large_content = "A" * (1024 * 100)  # 100KB file
    (server_dir / "large_file.txt").write_text(large_content)
    console.print(f"[cyan]Setup: Added 100KB large_file.txt to server[/cyan]")

def setup_complex_scenario(server_dir, client_dir):
    """Complex scenario from unit_test.py: Multiple simultaneous operations"""
    setup_initial_sync(server_dir, client_dir)
    # Simulate previous sync by copying files to client
    shutil.copytree(server_dir, client_dir, dirs_exist_ok=True)
    
    # Complex operations based on unit_test.py
    # 1. Modify file1.txt on client
    (client_dir / "file1.txt").write_text("Modified on client")
    
    # 2. Touch file2.txt on server
    os.utime(server_dir / "file2.txt", None)
    
    # 3. Delete nested.txt on client
    (client_dir / "subdir" / "nested.txt").unlink()
    
    # 4. Move file2.txt to file3.txt on server
    (server_dir / "file2.txt").rename(server_dir / "file3.txt")
    
    # 5. Simultaneous edit: both server and client modify file1.txt
    (server_dir / "file1.txt").write_text("Modified on server")
    
    # 6. Create new file on both sides with same name but different content
    (server_dir / "conflict.txt").write_text("Server version")
    (client_dir / "conflict.txt").write_text("Client version")
    
    console.print(f"[magenta]Setup: Complex scenario with multiple conflicts and operations[/magenta]")

def setup_simultaneous_operations(server_dir, client_dir):
    """Simultaneous operations: Multiple changes on both sides"""
    setup_initial_sync(server_dir, client_dir)
    # Simulate previous sync by copying files to client
    shutil.copytree(server_dir, client_dir, dirs_exist_ok=True)
    
    # Simultaneous operations
    # Server side
    (server_dir / "server_new.txt").write_text("New file from server")
    (server_dir / "file1.txt").write_text("Server modified file1")
    
    # Client side
    (client_dir / "client_new.txt").write_text("New file from client")
    (client_dir / "file2.txt").write_text("Client modified file2")
    
    # Both modify the same file
    (server_dir / "shared.txt").write_text("Server version of shared file")
    (client_dir / "shared.txt").write_text("Client version of shared file")
    
    console.print(f"[yellow]Setup: Simultaneous operations on both server and client[/yellow]")

# Test cases registry
TEST_CASES = [
    TestCase("initial_sync", "Initial sync: Clean start with files on server", setup_initial_sync),
    TestCase("file_removal_server", "File removal on server side", setup_file_removal_server),
    TestCase("file_removal_client", "File removal on client side", setup_file_removal_client),
    TestCase("file_added_server", "File added on server side", setup_file_added_server),
    TestCase("file_added_client", "File added on client side", setup_file_added_client),
    TestCase("file_moved_server", "File moved/renamed on server side", setup_file_moved_server),
    TestCase("file_moved_client", "File moved/renamed on client side", setup_file_moved_client),
    TestCase("file_conflict", "File conflict: Both sides modify same file", setup_file_conflict),
    TestCase("directory_operations", "Directory create/delete operations", setup_directory_operations),
    TestCase("large_file_sync", "Large file synchronization test", setup_large_file_sync),
    TestCase("complex_scenario", "Complex scenario: Multiple operations and conflicts", setup_complex_scenario),
    TestCase("simultaneous_operations", "Simultaneous operations on both sides", setup_simultaneous_operations),
]

# Utility functions
def clean_directories(server_dir, client_dir):
    """Clean both server and client directories"""
    for directory in [server_dir, client_dir]:
        for item in directory.iterdir():
            if item.is_dir():
                shutil.rmtree(item)
            else:
                item.unlink()

def show_test_menu():
    """Display test case selection menu and return selected test case"""
    table = Table(title="Debug Test Cases", show_header=True)
    table.add_column("ID", style="cyan", width=3)
    table.add_column("Name", style="green", width=25)
    table.add_column("Description", style="yellow")
    
    for i, test_case in enumerate(TEST_CASES):
        table.add_row(str(i), test_case.name, test_case.description)
    
    console.print(table)
    console.print("\n[blue]Use arrow keys to navigate, Enter to select, or type test ID:[/blue]")
    
    return interactive_menu_selection()

def interactive_menu_selection():
    """Interactive menu with arrow key navigation"""
    selected = 0
    max_selection = len(TEST_CASES) - 1
    
    def get_key():
        """Get a single keypress or arrow key sequence"""
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        try:
            tty.setcbreak(fd)
            key = sys.stdin.read(1)
            
            # Handle arrow keys (escape sequences)
            if key == '\x1b':  # ESC
                key += sys.stdin.read(1)  # [
                key += sys.stdin.read(1)  # A, B, C, or D
            
            return key
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    
    # Interactive selection with arrow keys
    try:
        while True:
            # Clear and redraw menu
            console.clear()
            table = Table(title="Debug Test Cases - Use ↑↓ arrows, Enter to select", show_header=True)
            table.add_column("ID", style="cyan", width=3)
            table.add_column("Name", style="green", width=25)
            table.add_column("Description", style="yellow")
            
            for i, test_case in enumerate(TEST_CASES):
                style = "reverse" if i == selected else None
                table.add_row(str(i), test_case.name, test_case.description, style=style)
            
            console.print(table)
            console.print(f"\n[blue]Selected: {selected} | Press Enter to confirm, ↑↓ to navigate, or 'q' to quit[/blue]")
            
            key = get_key()
            if key == '\x1b[A':  # Up arrow
                selected = max(0, selected - 1)
            elif key == '\x1b[B':  # Down arrow
                selected = min(max_selection, selected + 1)
            elif key == '\r' or key == '\n':  # Enter
                return selected
            elif key == 'q':
                console.print("[red]Quit selected[/red]")
                sys.exit(0)
            elif len(key) == 1 and key.isdigit():
                digit = int(key)
                if 0 <= digit <= max_selection:
                    return digit
    except (KeyboardInterrupt, EOFError):
        console.print("\n[red]Selection cancelled[/red]")
        sys.exit(0)
    except Exception:
        # Fallback to simple prompt
        while True:
            try:
                choice = Prompt.ask("Enter test case ID", choices=[str(i) for i in range(len(TEST_CASES))], default="0")
                return int(choice)
            except (KeyboardInterrupt, EOFError):
                console.print("\n[red]Selection cancelled[/red]")
                sys.exit(0)

def enqueue_output(pipe, queue, proc=None):
    """Enqueue output from a subprocess pipe"""
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

def make_test_env(test_id):
    """Create test environment directories"""
    base = Path(f"test_sync_env_{test_id}").absolute()
    server_dir = base / "server"
    client_dir = base / "client"
    if base.exists():
        shutil.rmtree(base)
    server_dir.mkdir(parents=True)
    client_dir.mkdir(parents=True)
    return server_dir, client_dir

def is_port_in_use(port):
    """Check if a port is in use"""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        return s.connect_ex(('localhost', port)) == 0

def get_available_port(start_port=5555):
    """Get an available port starting from start_port"""
    port = start_port
    while is_port_in_use(port):
        port += 1
        if port > start_port + 100:  # Avoid infinite loop
            raise RuntimeError("Could not find available port")
    return port

def get_gdbserver_status(proc, port):
    """Get the status of a gdbserver process"""
    if not proc or proc.poll() is not None:
        return "[red]Not Running[/red]"
    if is_port_in_use(port):
        return f"[green]GDB Ready (:{port})[/green]"
    return "[yellow]Starting...[/yellow]"

def get_proc_stats(proc):
    """Get process statistics"""
    if not proc or proc.poll() is not None:
        return 0, 0, None, None
    try:
        p = psutil.Process(proc.pid)
        return p.cpu_percent(), p.memory_info().rss // 1024, p.io_counters(), p.connections()
    except:
        return 0, 0, None, None

def run_debug_session(test_case, server_dir, client_dir):
    """Run a debug session for the selected test case"""
    console.print(f"\n[bold green]Starting debug session for: {test_case.name}[/bold green]")
    console.print(f"[yellow]Description: {test_case.description}[/yellow]")
    
    # Initialize test metrics
    test_metrics = TestMetrics(start_time=datetime.now())
    
    # Setup the test case
    test_case.setup_func(server_dir, client_dir)
    
    port = get_available_port()
    console.print(f"[blue]Using port: {port}[/blue]")
    
    # Lists to store output
    server_lines = []
    client_lines = []
    scroll_offsets = [0, 0]  # [server_scroll, client_scroll]
    
    # Performance tracking
    cpu_samples = {'server': [], 'client': []}
    memory_samples = {'server': [], 'client': []}
    
    proc_info = {
        'server_proc': None, 
        'client_proc': None, 
        'server_gdb_port': GDB_PORTS[0], 
        'client_gdb_port': GDB_PORTS[1]
    }

    def run_server():
        try:
            proc_info['server_proc'] = subprocess.Popen(
                ["gdbserver", f":{proc_info['server_gdb_port']}", MULTI_PC_SYNC_BIN, 
                 "-d", str(port), str(server_dir.resolve())],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, preexec_fn=os.setsid
            )
            server_queue = Queue()
            server_thread = threading.Thread(target=enqueue_output, 
                                            args=(proc_info['server_proc'].stdout, server_queue))
            server_thread.daemon = True
            server_thread.start()
            
            while proc_info['server_proc'].poll() is None:
                try:
                    line = server_queue.get(timeout=0.1)
                    server_lines.append(line.rstrip())
                    
                    # Track errors and metrics
                    if "error" in line.lower() or "failed" in line.lower():
                        test_metrics.server_errors.append(line.strip())
                    if "sync" in line.lower():
                        test_metrics.sync_operations += 1
                    if "transfer" in line.lower() and "bytes" in line.lower():
                        # Try to extract byte count
                        import re
                        byte_match = re.search(r'(\d+)\s*bytes', line.lower())
                        if byte_match:
                            test_metrics.bytes_transferred += int(byte_match.group(1))
                    
                    if len(server_lines) > 200:
                        del server_lines[0]
                except Empty:
                    pass
        except Exception as e:
            server_lines.append(f"ERROR: Failed to start server with gdbserver: {e}")

    def run_client():
        time.sleep(2)  # Give server time to start
        try:
            proc_info['client_proc'] = subprocess.Popen(
                ["gdbserver", f":{proc_info['client_gdb_port']}", MULTI_PC_SYNC_BIN, 
                 "-s", f"127.0.0.1:{port}", str(client_dir.resolve())],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, 
                stdin=subprocess.PIPE, preexec_fn=os.setsid
            )
            client_queue = Queue()
            client_thread = threading.Thread(target=enqueue_output, 
                                            args=(proc_info['client_proc'].stdout, client_queue, proc_info['client_proc']))
            client_thread.daemon = True
            client_thread.start()
            
            while proc_info['client_proc'].poll() is None:
                try:
                    line = client_queue.get(timeout=0.1)
                    client_lines.append(line.rstrip())
                    
                    # Track errors and metrics
                    if "error" in line.lower() or "failed" in line.lower():
                        test_metrics.client_errors.append(line.strip())
                    if "transfer" in line.lower() and "file" in line.lower():
                        test_metrics.files_transferred += 1
                    
                    if len(client_lines) > 200:
                        del client_lines[0]
                except Empty:
                    pass
        except Exception as e:
            client_lines.append(f"ERROR: Failed to start client with gdbserver: {e}")

    # Start server and client threads
    server_thread = threading.Thread(target=run_server)
    client_thread = threading.Thread(target=run_client)
    server_thread.start()
    client_thread.start()
    threads = [server_thread, client_thread]

    # Setup layout
    layout = Layout()
    layout.split_column(
        Layout(name="upper"),
        Layout(name="lower")
    )
    layout["upper"].split_row(
        Layout(name="server"),
        Layout(name="client")
    )

    def get_key():
        """Get keyboard input"""
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        try:
            tty.setcbreak(fd)
            rlist, _, _ = select.select([fd], [], [], 0.05)
            if rlist:
                return sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
        return None

    # Main display loop
    with Live(layout, refresh_per_second=10, screen=True, console=console):
        while any(t.is_alive() for t in threads):
            term_height, _ = shutil.get_terminal_size((80, 24))
            max_lines = max((term_height // 2) - 5, 5)
            
            # Handle scroll offsets
            server_log_len = len(server_lines)
            client_log_len = len(client_lines)
            
            key = get_key()
            if key is None:
                # Auto-scroll to bottom when no user input
                scroll_offsets[0] = max(0, server_log_len - max_lines)
                scroll_offsets[1] = max(0, client_log_len - max_lines)
            
            # Handle keyboard input for scrolling
            if key == "\x1b":  # Escape sequence
                next1 = get_key()
                next2 = get_key()
                if next1 == "[":
                    if next2 == "A":  # Up arrow - scroll server up
                        scroll_offsets[0] = max(scroll_offsets[0] - 1, 0)
                    elif next2 == "B":  # Down arrow - scroll server down
                        scroll_offsets[0] = min(scroll_offsets[0] + 1, max(0, server_log_len - max_lines))
                    elif next2 == "C":  # Right arrow - scroll client down
                        scroll_offsets[1] = min(scroll_offsets[1] + 1, max(0, client_log_len - max_lines))
                    elif next2 == "D":  # Left arrow - scroll client up
                        scroll_offsets[1] = max(scroll_offsets[1] - 1, 0)
            elif key == "q":
                break

            # Update display panels
            server_display_lines = server_lines[scroll_offsets[0]:scroll_offsets[0]+max_lines]
            client_display_lines = client_lines[scroll_offsets[1]:scroll_offsets[1]+max_lines]
            
            layout["server"].update(Panel("\n".join(server_display_lines), 
                                         title="Server (gdbserver)", border_style="cyan", box=box.ROUNDED))
            layout["client"].update(Panel("\n".join(client_display_lines), 
                                         title="Client (gdbserver)", border_style="green", box=box.ROUNDED))

            # Debug/monitoring info for lower panel
            server_status = get_gdbserver_status(proc_info['server_proc'], proc_info['server_gdb_port'])
            client_status = get_gdbserver_status(proc_info['client_proc'], proc_info['client_gdb_port'])
            server_cpu, server_mem, server_io, server_net = get_proc_stats(proc_info['server_proc'])
            client_cpu, client_mem, client_io, client_net = get_proc_stats(proc_info['client_proc'])
            
            # Track performance metrics
            if server_cpu > 0:
                cpu_samples['server'].append(server_cpu)
                memory_samples['server'].append(server_mem)
                test_metrics.max_memory_usage['server'] = max(test_metrics.max_memory_usage['server'], server_mem)
            
            if client_cpu > 0:
                cpu_samples['client'].append(client_cpu)
                memory_samples['client'].append(client_mem)
                test_metrics.max_memory_usage['client'] = max(test_metrics.max_memory_usage['client'], client_mem)
            
            # Track network connections
            if server_net:
                test_metrics.network_connections = max(test_metrics.network_connections, len(server_net))
            
            def io_summary(io):
                if not io: return "N/A"
                return f"R: {io.read_bytes//1024}K W: {io.write_bytes//1024}K"
            
            def net_summary(net):
                if not net: return "N/A"
                return f"Conns: {len(net)}"
            
            lower_info = f"[bold]Test Case:[/bold] {test_case.name}\n"
            lower_info += f"[bold]Server:[/bold] {server_status} | CPU: {server_cpu:.1f}% | Mem: {server_mem}K | Disk: {io_summary(server_io)} | Net: {net_summary(server_net)}\n"
            lower_info += f"[bold]Client:[/bold] {client_status} | CPU: {client_cpu:.1f}% | Mem: {client_mem}K | Disk: {io_summary(client_io)} | Net: {net_summary(client_net)}\n"
            lower_info += f"[dim]Controls: ↑↓ scroll server, ←→ scroll client, 'q' to quit | GDB Ports: Server={proc_info['server_gdb_port']}, Client={proc_info['client_gdb_port']}[/dim]"
            
            layout["lower"].update(Panel(lower_info, 
                                       title="Debug/Resource Monitor", border_style="magenta", box=box.ROUNDED))
            time.sleep(0.1)

    # Wait for threads to complete
    for t in threads:
        t.join(timeout=1)
    
    # Cleanup processes
    for proc in [proc_info['server_proc'], proc_info['client_proc']]:
        if proc and proc.poll() is None:
            proc.terminate()
            time.sleep(1)
            if proc.poll() is None:
                proc.kill()
    
    # Finalize test metrics
    test_metrics.end_time = datetime.now()
    
    # Calculate average CPU usage
    if cpu_samples['server']:
        test_metrics.average_cpu_usage['server'] = sum(cpu_samples['server']) / len(cpu_samples['server'])
    if cpu_samples['client']:
        test_metrics.average_cpu_usage['client'] = sum(cpu_samples['client']) / len(cpu_samples['client'])
    
    # Generate and display test report
    report_generator = TestReportGenerator(console)
    report_generator.generate_report(test_case, test_metrics, server_dir, client_dir)
    
    # Wait for user input to continue
    try:
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        try:
            tty.setcbreak(fd)
            sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    except:
        input("Press Enter to continue...")
    
    return test_metrics

def main():
    """Main function"""
    console.print("[bold blue]Multi-PC-Sync Interactive Debug Tool[/bold blue]")
    console.print("[yellow]This tool allows you to debug specific test scenarios with gdbserver integration[/yellow]")
    console.print("[cyan]Connect with GDB using: gdb -ex 'target remote :12345' (server) or gdb -ex 'target remote :12346' (client)[/cyan]\n")
    
    # Show test case selection menu
    selected_test_id = show_test_menu()
    selected_test = TEST_CASES[selected_test_id]
    
    # Create test environment
    test_id = 0  # Use 0 for debug session
    server_dir, client_dir = make_test_env(test_id)
    
    console.print(f"\n[green]Test environment created at:[/green]")
    console.print(f"  Server: {server_dir}")
    console.print(f"  Client: {client_dir}")
    
    try:
        test_metrics = run_debug_session(selected_test, server_dir, client_dir)
        final_success = test_metrics.evaluate_success(selected_test.name, server_dir, client_dir)
        console.print(f"\n[green]Debug session completed with {'SUCCESS' if final_success else 'ISSUES'}[/green]")
    except KeyboardInterrupt:
        console.print("\n[red]Debug session interrupted[/red]")
    finally:
        # Cleanup test environment
        base = Path(f"test_sync_env_{test_id}").absolute()
        if base.exists():
            shutil.rmtree(base)
        console.print("[green]Cleanup completed[/green]")

if __name__ == "__main__":
    main()
