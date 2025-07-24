# multi-pc-sync

Multi-pc-sync is a file synchronization utility that is optimized to be used over a high-latency link, such as the internet. The single binary has 2 modes: server and client. The drastic speed benefit of this application over rsync, is that the remote folder is never traversed, which creates a long series of short blocking messages to list the directories. No, instead the remote folder file attributes, including MD5 Sum, are fully indexed and serialized using protocol buffers. Since hash computation is CPU-intensive, the index from a previous run of the sync is loaded first and only new files are hashed.

## Installation Instructions

### Prerequisites

Multi-pc-sync requires the following dependencies:

- **C++ Compiler**: Supporting C++23 standard (GCC 12+ or Clang 15+)
- **Protocol Buffers**: Version 29.2 or newer
- **CMake**: Version 3.22 or newer
- **Ninja Build System**: For faster compilation
- **Git**: For cloning the repository and managing submodules

### Step 1: Verify Current Versions

Check if you already have the required tools installed:

```bash
# Check C++ compiler version
g++ --version
# or
clang++ --version

# Check Protocol Buffers version
protoc --version

# Check CMake version
cmake --version

# Check Ninja build system
ninja --version

# Check Git version
git --version
```

### Step 2: Install Prerequisites

#### On Ubuntu/Debian:

```bash
# Update package list
sudo apt update

# Install essential build tools and C++23 capable compiler
sudo apt install build-essential gcc-12 g++-12

# Install Protocol Buffers
sudo apt install protobuf-compiler libprotobuf-dev

# Install CMake and Ninja
sudo apt install cmake ninja-build

# Install Git
sudo apt install git

# Set GCC 12 as default (if multiple versions installed)
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 60
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 60
```

#### On Red Hat/CentOS/Fedora:

```bash
# Install development tools
sudo dnf groupinstall "Development Tools"
sudo dnf install gcc-c++

# Install Protocol Buffers
sudo dnf install protobuf-compiler protobuf-devel

# Install CMake and Ninja
sudo dnf install cmake ninja-build

# Install Git
sudo dnf install git
```

#### On macOS:

```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install prerequisites
brew install gcc protobuf cmake ninja git

# Ensure you're using a modern GCC
export CC=gcc-13
export CXX=g++-13
```

### Step 3: Clone the Repository

```bash
# Clone the repository
git clone https://github.com/tabarnakos/multi-pc-sync.git
cd multi-pc-sync

# Initialize and update git submodules (for termcolor dependency)
git submodule update --init --recursive
```

### Step 4: Compilation

```bash
# Create and enter build directory
mkdir build
cd build

# Configure the project with CMake
cmake -G Ninja ..

# Build the project
ninja

# Alternative: Build with make (if Ninja is not available)
# cmake ..
# make -j$(nproc)
```

The compiled binary will be located at `build/multi_pc_sync`.

### Step 5: Running the Program

#### Test the Installation

```bash
# From the build directory, test the help output
./multi_pc_sync --help
```

#### Basic Usage Examples

```bash
# Start server on port 9000 for directory /srv/shared
./multi_pc_sync -d 9000 /srv/shared

# Connect client to synchronize local directory /home/user/docs
./multi_pc_sync -s 192.168.1.10:9000 /home/user/docs
```

### Step 6: Installation (System-wide)

To install the binary system-wide:

```bash
# From the build directory
sudo ninja install

# Alternative: Manual installation
sudo cp multi_pc_sync /usr/local/bin/
sudo chmod +x /usr/local/bin/multi_pc_sync
```

After installation, you can run `multi_pc_sync` from anywhere:

```bash
# Test system-wide installation
multi_pc_sync --help
```

### Troubleshooting

#### Common Issues:

1. **C++23 Support**: If you get compiler errors, ensure you have GCC 12+ or Clang 15+
2. **Protocol Buffers**: If protobuf is not found, install development headers (`libprotobuf-dev` on Ubuntu)
3. **CMake Version**: If CMake version is too old, consider installing from the official CMake website
4. **Missing Submodules**: If compilation fails with termcolor errors, run `git submodule update --init --recursive`

#### Manual Protocol Buffers Installation (if package version is too old):

```bash
# Download and compile Protocol Buffers from source
wget https://github.com/protocolbuffers/protobuf/releases/download/v29.2/protobuf-29.2.tar.gz
tar -xzf protobuf-29.2.tar.gz
cd protobuf-29.2
mkdir build && cd build
cmake -G Ninja ..
ninja
sudo ninja install
sudo ldconfig  # Update library cache
```

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
  - `--print-before-sync`: Print commands before executing them (equivalent to `--dry-run -y`).
  - `--cfg=<path>`: Path to the config file. Configures behavior on file conflicts.

**Examples:**
```sh
# Basic synchronization
./multi_pc_sync -s 192.168.1.10:9000 /home/user/Documents

# Print commands before executing them
./multi_pc_sync -s 192.168.1.10:9000 --print-before-sync /home/user/Documents
```

## Debugging multi-pc-sync

The project includes a comprehensive testing and debugging framework in the `testing/` directory. These tools allow you to run predefined test scenarios, debug with gdbserver, and validate the application behavior against expected results.

### Testing Framework Scripts

- `debug_tmux.sh`: Main script for running tests and interactive debugging sessions
- `debug_tmux_utils.sh`: Utility functions for file operations, hash calculations, and test verification
- `scenarios.sh`: Defines test scenarios that exercise different synchronization behaviors
- `visualize_test_report.sh`: Generates human-readable test reports

### Running Test Scenarios

You can run specific test scenarios or ranges:

```sh
# List available test scenarios
./testing/debug_tmux.sh --list

# Run a specific scenario (e.g., #9: File created on both sides with different content)
./testing/debug_tmux.sh 9

# Run multiple scenarios
./testing/debug_tmux.sh 1,3,9

# Run ranges of scenarios
./testing/debug_tmux.sh 1-5,8-10,31

# Run interactive mode (select scenario at runtime)
./testing/debug_tmux.sh
```

### Interactive Debugging

For interactive debugging with a tmux split view:

```sh
# Run in verbose mode with interactive debugging
./testing/debug_tmux.sh --verbose
```

This launches:
1. A tmux session with two panes - server (top) and client (bottom)
2. Both panes prompt if you want to run with gdbserver
3. You can attach VS Code or another debugger to gdbserver ports (12345 for server, 12346 for client)
4. At the end of the debugging session, kill both applications and type "exit" in both panes of tmux. The script will detect it and generate the test report.

The recommended way of debugging is to use Visual Studio Code or a similar code editor that can attach to a remote host and attach to both server and client in the same IDE.

### Simulating Network Conditions

Some scenarios automatically simulate network latency to test performance over slow connections:

```sh
# Run scenarios that test performance with different latencies
./testing/debug_tmux.sh 4-7
```

Scenarios 4-7 test synchronization with 20ms and 250ms network latencies.

### Understanding Test Results

After running tests:
1. The script reports missing/extra files/folders/hashes for each scenario
2. Results are saved to `testing/test_report.txt`
3. `visualize_test_report.sh` is automatically run to convert the detailed test report into a pass/fail human-readable, colorized summary.

### Advanced Debugging Tips

#### Verbose Mode

Run with `--verbose` to see detailed output including file hashes and commands:

```sh
./testing/debug_tmux.sh --verbose 9
```

In verbose mode, `--print-before-sync` replaces `-y` flag, showing all commands before they execute.

#### Manual GDB Debugging

You can also manually debug using gdbserver:

```sh
# Server debugging
gdbserver :12345 ./multi-pc-sync -d 9000 /srv/data/server_folder

# Client debugging
gdbserver :12346 ./multi-pc-sync -s 127.0.0.1:9000 /home/user/client_folder
```

# Features / Test results

Call it a feature or a bug, I don't know if the other sync programs have such extensive testing, but this one does. My goal is not necessarily to have everything green, rather is that whenever there is data being overwritten, there should be an option somewhere.

## ✅ File Sync Test Scenarios

## 1. File Creation and Deletion
- [✅] File created on source only
- [✅] File created on destination only
- [✅] File created on both sides with identical content
- [✅] File deleted on source only
- [✅] File deleted on destination only
- [✅] File deleted on both sides

## 2. File Modification
- [✅] File modified on source only
- [✅] File modified on destination only

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
- [✅] Circular rename (A→B, B→A across sides)  

## 6. Conflict Scenarios
- [✅] File created on both sides with different content (in case of conflict, original files are renamed and the file location is replaced by a symlink that points to your own copy of the file)
- [✅] File modified differently on both sides (in case of conflict, original files are renamed and the file location is replaced by a symlink that points to your own copy of the file)
- [✅] File deleted on the server, modified on the client
- [✅] File deleted on the client, modified on the server
- [✅] File moved on the server, renamed on the client    (currently causes data loss)
- [✅] File moved on the client, renamed on the server    (currently causes data loss)

## 7. Permissions and Metadata (if applicable)
- [✅] Permissions changed on one side
- [✅] Permissions changed on server moved file
- [✅] Permissions changed on client moved file
- [✅] Timestamps changed without content change
- [ ] Ownership or extended attributes changed

## 8. Edge Cases
- [✅] File with special characters in the name
- [✅] Very large file (The default max size is 64GB, configurable via config file)
- [✅] File with 0 bytes
- [✅] Filename case changes (case sensitivity issues)
- [✅] Long path names (4095 characters is the limit set by Ubuntu 24.04 used in test)
- [✅] Long file names (255 characters is the limit for the EXT4 filesystem used in test)
- [ ] Files with identical hashes but different content

## 9. Repeatability and Idempotency
- [✅] Running sync twice with no changes (should be a no-op)
- [✅] Running sync after a full sync (should be fast)
- [ ] Interrupt/resume sync mid-operation
