# Display results with color coding
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    NC='\033[0m' # No Color
else
    RED=''
    GREEN=''
    YELLOW=''
    NC=''
fi

# Helper: remove item from list
remove_item_from_list() {
    local item_to_remove="$2"
    local new_list=""
    for item in $1; do
        if [[ "$item" != "$item_to_remove" ]]; then
            new_list+="$item "
        fi
    done
    echo "${new_list%% }"
}

add_item_to_list() {
    local item_to_add="$2"
    local new_list="$1"
    if [[ ! " $new_list " =~ " $item_to_add " ]]; then
        new_list+=" $item_to_add"
    fi
    echo "${new_list%% }"
}
count_items_in_list() {
    local list="$1"
    local count=0
    for item in $list; do
        count=$((count + 1))
    done
    echo "$count"
}


hash_file(){
    local root="$1"
    local rel="$2"
    local abs="$root/${rel#./}"

    if [ ! -f "$abs" ]; then
        echo "hash_file: $abs does not exist or is not a file" >&2
        return 1
    fi

    # Return the hash of the file
    echo "$(md5sum "$abs" | awk '{print $1}')"
}

# remove_path <root> <relpath>
# Removes a file or directory, updates EXPECTED_FILES, and removes corresponding md5sums from EXPECTED_HASHES
remove_path() {
    local root="$1"
    local rel="$2"
    local abs="$root/${rel#./}"

    if [ -f "$abs" ]; then
        # Remove file from EXPECTED_FILES
        EXPECTED_FILES=$(remove_item_from_list "$EXPECTED_FILES" "$rel")
        # Remove hash from EXPECTED_HASHES
        local hash=$(md5sum "$abs" | awk '{print $1}')
        EXPECTED_HASHES=$(remove_item_from_list "$EXPECTED_HASHES" "$hash")
        rm "$abs"
    elif [ -d "$abs" ]; then
        # Directory: remove all matching paths in EXPECTED_FILES and hashes
        local updated_files=""
        # Find all files under the directory
        local files_to_remove=( )
        while IFS= read -r -d $'\0' file; do
            # Convert to relpath
            local relfile="./${file#$root/}"
            files_to_remove+=("$relfile")
        done < <(find "$abs" -type f -print0)
        # Remove hashes for all files
        for relfile in "${files_to_remove[@]}"; do
            local hash=$(hash_file "$root" "$relfile")
            EXPECTED_HASHES=$(remove_item_from_list "$EXPECTED_HASHES" "$hash")
        done
        # Remove all matching EXPECTED_FILES
        for f in $EXPECTED_FILES; do
            if [[ "$f" != $rel* ]]; then
                updated_files+="$f "
            fi
        done
        EXPECTED_FILES="${updated_files%% }"
        rm -rf "$abs"
    else
        echo "remove_path: Source path $abs does not exist or is not a file/folder" >&2
        return 1
    fi
}
# move_path <root> <src_relpath> <dst_relpath>
# Moves a file or directory and updates EXPECTED_FILES accordingly
move_path() {
    local root="$1"
    local src_rel="$2"
    local dst_rel="$3"
    local src_abs="$root/${src_rel#./}"
    local dst_abs="$root/${dst_rel#./}"

    if [ -f "$src_abs" ]; then
        # File: update EXPECTED_FILES (space-separated)
        EXPECTED_FILES=$(add_item_to_list "$EXPECTED_FILES" "$dst_rel")
        EXPECTED_FILES=$(remove_item_from_list "$EXPECTED_FILES" "$src_rel")
        mv "$src_abs" "$dst_abs"
    elif [ -d "$src_abs" ]; then
        # Directory: update all matching paths in EXPECTED_FILES (space-separated)
        local updated_files=""
        for f in $EXPECTED_FILES; do
            if [[ "$f" == $src_rel* ]]; then
                updated_files+="${dst_rel}${f#$src_rel} "
            else
                updated_files+="$f "
            fi
        done
        EXPECTED_FILES="${updated_files%% }" # Remove trailing space
        mv "$src_abs" "$dst_abs"
    else
        echo "move_path: Source path $src_abs does not exist or is not a file/folder" >&2
        return 1
    fi
}
canonical() {
    local path="$1"
    # Remove trailing slashes
    path="${path%/}"
    # Get absolute path
    echo "$(realpath "$path")"
}


run_server() {
    local gdbserver_port="$1"
    shift
    local server_cmd_line="$*"

    echo 'Do you want to run the server in gdb? (y/n)'
    read -r run_in_gdb
    if [ "$run_in_gdb" = "y" ]; then
        gdbserver :$gdbserver_port $server_cmd_line
        return
    else
        echo "$server_cmd_line"
        $server_cmd_line
    fi
}

run_client() {
    local gdbserver_port="$1"
    shift
    local client_cmd_line="$*"

    echo 'Do you want to run the client in gdb? (y/n)'
    read -r run_in_gdb
    if [ "$run_in_gdb" = "y" ]; then
        gdbserver :$gdbserver_port $client_cmd_line
        return
    else
        echo "$client_cmd_line"
        $client_cmd_line
    fi
}

log_to_report() {
    local report_file="$1"
    local color="$2"
    local message="$3"
    echo -e "${color}${message}${NC}" 
    echo "${message}" >> "$report_file"
}



compare_files() {
    local report_file="$2"
    # List files in folder
    local files_in_directory=$(find "$1" | sed "s|$1/|./|")

    # Combine server and client files into a single list
    local all_files=$(echo -e "$files_in_directory")
    local num_threads=$(lscpu | grep '^CPU(s):' | awk '{print $2}')
    local all_hashes=$(find "$1" -type f -print0 | xargs -0 -I{} -P $num_threads md5sum {} | awk '{print $1}')
    local expected_files=$(echo "$1 $EXPECTED_FILES" | tr ' ' '\n')
    local expected_hashes=$(echo "$EXPECTED_HASHES" | tr ' ' '\n')

    local files_to_ignore="./.folderindex ./.folderindex.last_run ./.remote.folderindex ./.remote.folderindex.last_run ./sync_commands.sh"

    log_to_report $report_file $NC "expected $(count_items_in_list "$expected_files") files, $(count_items_in_list "$expected_hashes") hashes"

    # Capture hashes of files to ignore
    local hashes_to_ignore=""
    for file in $files_to_ignore; do
        if [ -f "$1/$file" ]; then
            local hash=$(md5sum "$1/$file" | awk '{print $1}')
            hashes_to_ignore="$hashes_to_ignore $hash"
        fi
    done

    # Check for missing or extra files
    local missing_files=""
    local missing_hashes=""
    local extra_files=""
    local extra_hashes=""

    echo "searching for missing files..."
    for file in $expected_files; do
        if ! echo "$all_files" | grep -q "^$file$"; then
            if ! echo "$files_to_ignore" | grep -q "^$file$"; then
                missing_files+="$file\n"
            fi
        fi
    done

    echo "searching for missing hashes..."
    for hash in $expected_hashes; do
        if ! echo "$all_hashes" | grep -q "^$hash$"; then
            missing_hashes+="$hash\n"
        fi
    done

    echo "searching for extra files..."
    for file in $all_files; do
        if ! echo "$expected_files" | grep -Fxq "$file"; then
            if ! echo "$files_to_ignore" | grep -q "$file"; then
                extra_files+="$file\n"
            fi
        fi
    done

    echo "searching for extra hashes..."
    for hash in $all_hashes; do
        if ! echo "$expected_hashes" | grep -Fxq "$hash"; then
            if ! echo "$hashes_to_ignore" | grep -q "$hash"; then
                extra_hashes+="$hash\n"
            fi
        fi
    done

    # Display results with color coding
    if [ -n "$missing_files" ]; then
        log_to_report $report_file $RED "Missing files:"
        log_to_report $report_file $NC "$missing_files"
    else
        log_to_report $report_file $GREEN "No missing files."
    fi

    if [ -n "$extra_files" ]; then
        log_to_report $report_file $YELLOW "Extra files:"
        log_to_report $report_file $NC "$extra_files"
    else
        log_to_report $report_file $GREEN "No extra files."
    fi

    if [ -n "$missing_hashes" ]; then
        log_to_report $report_file $RED "Missing hashes:"
        log_to_report $report_file $NC "$missing_hashes"
    else
        log_to_report $report_file $GREEN "No missing hashes."
    fi

    if [ -n "$extra_hashes" ]; then
        log_to_report $report_file $YELLOW "Extra hashes:"
        log_to_report $report_file $NC "$extra_hashes"
    else
        log_to_report $report_file $GREEN "No extra hashes."
    fi
}

compare_file_times() {
    local folder1="$1"
    local folder2="$2"
    local report_file="$3"
    local pass="1"
    log_to_report $report_file $NC "Comparing file times..."
    local files_to_ignore=$(echo "./.folderindex ./.folderindex.last_run ./.remote.folderindex ./.remote.folderindex.last_run ./sync_commands.sh" | tr ' ' '\n')

    # Find all files in both folders
    local files1=$(find "$folder1" -type f)
    local files2=$(find "$folder2" -type f)

    # Compare file times
    for file1 in $files1; do
        # Skip ignored files
        local relpath="./${file1#$folder1/}"
        if echo "$files_to_ignore" | grep -q "^$relpath$"; then
            continue
        fi
        local file2="${folder2}${file1#$folder1}"
        if [ -f "$file2" ]; then
            local time1=$(stat -c %y "$file1")
            local time2=$(stat -c %y "$file2")
            if [ "$time1" != "$time2" ]; then
                log_to_report $report_file $YELLOW "File times differ: $relpath ($time1) vs ($time2)"
                pass="0"
            fi
        fi
    done

    if [[ "$pass" == "1" ]]; then
        log_to_report $report_file $GREEN "File times comparison passed."
    else
        log_to_report $report_file $RED "File times comparison failed."
    fi
}

compare_file_permissions() {
    local folder1="$1"
    local folder2="$2"
    local report_file="$3"
    local pass="1"
    log_to_report $report_file $NC "Comparing file permissions..."
    local files_to_ignore=$(echo "./.folderindex ./.folderindex.last_run ./.remote.folderindex ./.remote.folderindex.last_run ./sync_commands.sh" | tr ' ' '\n')

    # Find all files in both folders
    local files1=$(find "$folder1" -type f)
    local files2=$(find "$folder2" -type f)

    # Compare file permissions
    for file1 in $files1; do
        # Skip ignored files
        local relpath="./${file1#$folder1/}"
        if echo "$files_to_ignore" | grep -q "^$relpath$"; then
            continue
        fi
        local file2="${folder2}${file1#$folder1}"
        if [ -f "$file2" ]; then
            local perm1=$(stat -c %a "$file1")
            local perm2=$(stat -c %a "$file2")
            if [ "$perm1" != "$perm2" ]; then
                log_to_report $report_file $YELLOW "File permissions differ: $relpath ($perm1) vs ($perm2)"
                pass="0"
            fi
        fi
    done

    if [[ "$pass" == "1" ]]; then
        log_to_report $report_file $GREEN "File permissions comparison passed."
    else
        log_to_report $report_file $RED "File permissions comparison failed."
    fi
}

# Function to apply or remove latency on loopback interface in milliseconds (0 removes latency)
apply_latency() {
    local latency_ms="$1"
    if [ "$latency_ms" -gt 0 ]; then
        echo "Applying ${latency_ms}ms latency to loopback interface..."
        sudo tc qdisc add dev lo root netem delay "${latency_ms}ms"
        if [ $? -ne 0 ]; then
            echo "Error: Failed to apply latency with tc. Make sure you have sudo privileges and the 'lo' interface exists." >&2
            echo "You might need to run: sudo modprobe sch_netem" >&2
            exit 1
        fi
    else
        echo "Removing latency from loopback interface..."
        sudo tc qdisc del dev lo root netem 2>/dev/null
        if [ $? -ne 0 ]; then
            echo "Warning: Failed to remove latency with tc. Manual cleanup may be required: sudo tc qdisc del dev lo root netem" >&2
        fi
    fi
}

# create_file <root> <relative-path> <expected-relpath-or-empty> <size-in-MB>
create_file() {
    local root="$1"    # base directory where file is created
    local relpath="$2" # relative path inside root
    local exp="$2"     # expected path to add or empty string to skip
    local size_mb="$3" # file size in megabytes; 0 for empty file

    local fullpath="$root/${relpath#./}"
    # ensure directory exists
    mkdir -p "$(dirname "$fullpath")"

    if [ "$size_mb" -gt 0 ]; then
        # Use the largest possible block size up to 2GB for better performance
        local max_block_size_gb=2
        local max_block_size_mb=$((max_block_size_gb * 1024))
        
        if [ "$size_mb" -le "$max_block_size_mb" ]; then
            # File size is within 2GB limit, use the full size as block size
            dd if=/dev/urandom of="$fullpath" bs="${size_mb}M" count=1 status=none
        else
            # File size exceeds 2GB, use 2GB blocks and remaining smaller block
            local full_blocks=$((size_mb / max_block_size_mb))
            local remainder_mb=$((size_mb % max_block_size_mb))
            
            # Write full 2GB blocks
            if [ "$full_blocks" -gt 0 ]; then
                dd if=/dev/urandom of="$fullpath" bs="${max_block_size_mb}M" count="$full_blocks" status=none
            fi
            
            # Write remainder if any
            if [ "$remainder_mb" -gt 0 ]; then
                dd if=/dev/urandom of="$fullpath" bs="${remainder_mb}M" count=1 oflag=append conv=notrunc status=none
            fi
        fi
    else
        > "$fullpath"
    fi

    # on non-empty exp, register expected file and its md5 hash
    if [ -n "$exp" ]; then
        EXPECTED_FILES=$(add_item_to_list "$EXPECTED_FILES" "$exp")
        EXPECTED_HASHES=$(add_item_to_list "$EXPECTED_HASHES" "$(hash_file "$root" "$relpath")")
    fi
}
# create_virtual_file <root> <virtual_filename> <expected-relpath>
# Creates a virtual file reference by copying from the virtual filesystem mount
create_virtual_file() {
    local root="$1"           # base directory (server or client root)
    local relpath="$2"            # expected relative path (e.g., ".virtual/virtual_1gb.bin")
    local size_gb="$3"        # size in GB (e.g., 1)

    local dest_path="$root/${relpath#./}"

    # Create the virtual file it doesn't exist
    dd if=/dev/zero of="$dest_path" bs=1G count="$size_gb" status=none
    
    # Register in expected files and get the hash
    if [ -n "$relpath" ]; then
        # expected path needs to be relative to root

        EXPECTED_FILES=$(add_item_to_list "$EXPECTED_FILES" "$relpath")
        
        local hash=$(get_virtual_zero_hash "$size_gb")
        
        EXPECTED_HASHES=$(add_item_to_list "$EXPECTED_HASHES" "$hash")
    fi
}

# get_virtual_zero_hash <size-in-GB>
# Calculates the MD5 hash for a file of the specified size filled with zeros
get_virtual_zero_hash() {
    local size_gb="$1"
    
    # Use known hashes for common sizes to avoid recalculation
    case "$size_gb" in
        1)   echo "cd573cfaace07e7949bc0c46028904ff" ;;  # 1GB of zeros
        2)   echo "a981130cf2b7e09f4686dc273cf7187e" ;;  # 2GB of zeros
        10)  echo "2dd26c4d4799ebd29fa31e48d49e8e53" ;;  # 10GB of zeros
        50)  echo "e7f4706922e1edfdb43cd89eb1af606d" ;;  # 50GB of zeros
        100) echo "25c94031714c109c0592b90fcb468232" ;; # 100GB of zeros
        *)  echo "Calculating hash for $size_gb GB of zeros..."
            # For other sizes, calculate on demand
            dd if=/dev/zero bs=1G count="$size_gb" 2>/dev/null | md5sum | cut -d' ' -f1
            ;;
    esac
}

# create_folder <root> <relative-path> <expected-relpath-or-empty>
create_folder() {
    local root="$1"    # base directory where file is created
    local relpath="$2" # relative path inside root
    local exp="$2"     # expected path to add or empty string to skip

    local fullpath="$root/${relpath#./}"
    # ensure directory exists
    mkdir -p "$fullpath"

    # on non-empty exp, register expected file and its md5 hash
    if [ -n "$exp" ]; then
        EXPECTED_FILES="$EXPECTED_FILES $exp"
    fi
}

parse_range() {
    local input="$1"
    local part start end
    SCENARIOS=""

    if [[ -z "$input" ]]; then
        # no input → interactive mode
        SCENARIOS="0"
        return
    fi

    IFS=',' read -ra parts <<< "$input"
    for part in "${parts[@]}"; do
        if [[ "$part" =~ ^([0-9]+)-([0-9]+)$ ]]; then
            start=${BASH_REMATCH[1]}; end=${BASH_REMATCH[2]}
            for ((i=start; i<=end; i++)); do
                SCENARIOS+=" $i"
            done
        elif [[ "$part" =~ ^[0-9]+$ ]]; then
            SCENARIOS+=" $part"
        else
            echo "Invalid scenario spec: $part" >&2
            exit 1
        fi
    done
    SCENARIOS="${SCENARIOS## }"
}

# edit_file <root> <relpath>
# Overwrites half of the file with random data, updates EXPECTED_HASHES accordingly
edit_file() {
    local root="$1"
    local rel="$2"
    local abs="$root/${rel#./}"

    if [ ! -f "$abs" ]; then
        echo "edit_file: $abs does not exist or is not a file" >&2
        return 1
    fi

    # Remove old hash from EXPECTED_HASHES
    local old_hash
    old_hash=$(md5sum "$abs" | awk '{print $1}')
    echo "Removing old hash: $old_hash"
    EXPECTED_HASHES=$(remove_item_from_list "$EXPECTED_HASHES" "$old_hash")

    # Get file size in bytes
    local filesize
    filesize=$(stat -c%s "$abs")
    if [ "$filesize" -eq 0 ]; then
        echo "edit_file: $abs is empty, nothing to edit" >&2
        return 1
    fi

    # Calculate half size (rounded down)
    local halfsize=$((filesize / 2))

    # Overwrite half of the file with random data
    dd if=/dev/urandom of="$abs" bs="$halfsize" count=1 conv=notrunc status=none

    # Compute new hash and append to EXPECTED_HASHES
    local new_hash
    new_hash=$(md5sum "$abs" | awk '{print $1}')
    echo "Adding new hash: $new_hash"
    EXPECTED_HASHES="$EXPECTED_HASHES $new_hash"
}

verbose_log() {
    if [[ "$VERBOSE" == "1" ]]; then
        echo "[VERBOSE] $*"
    fi
}

wait_for_server_start() {
    # Wait for the server to start and bind to the port
    while ! sudo ss -tlnp 2>/dev/null | grep -q ":$MULTI_PC_SYNC_PORT.*multi_pc_sync"; do
        sleep 0.01
    done
}

# Virtual Filesystem Functions
# ============================
# These functions enable testing with large virtual files that don't consume disk space.
# 
# Usage Example:
# 1. Mount virtual filesystems for both server and client:
#    mount_test_virtual_fs "$SERVER_ROOT" "$CLIENT_ROOT"
# 
# 2. Create virtual file references in your scenario:
#    create_virtual_file "$SERVER_ROOT" "test_1gb.bin" "./my_1gb_file.bin"
#    create_virtual_file "$SERVER_ROOT" "test_10gb.bin" "./my_10gb_file.bin"
# 
# 3. Virtual filesystems are automatically cleaned up at scenario end
#
# Available virtual files in the config:
# - test_1gb.bin, test_10gb.bin, test_50gb.bin, test_100gb.bin (zeros pattern)
# - random_1gb.bin (random pattern), sequence_1gb.bin (sequence pattern)

# mount_virtual_fs <mount_point> <config_file>
# Mounts a virtual filesystem at the specified mount point using the given config
mount_virtual_fs() {
    local mount_point="$1"
    local vfs_binary="../third-party/virtual-filesystem/build/virtual-fs-mount"
    
    # Convert mount point to absolute path (FUSE requires absolute paths)
    mount_point="$(canonical "$mount_point")"
    
    if [ ! -f "$vfs_binary" ]; then
        echo "Error: Virtual filesystem binary not found at $vfs_binary" >&2
        echo "Please build the virtual filesystem first: cd ../third-party/virtual-filesystem && mkdir -p build && cd build && cmake .. && make" >&2
        return 1
    fi
    
    # Create mount point if it doesn't exist
    mkdir -p "$mount_point"
    
    # Check if already mounted
    if mountpoint -q "$mount_point" 2>/dev/null; then
        echo "Warning: $mount_point is already mounted, unmounting first..."
        unmount_virtual_fs "$mount_point"
    fi

    echo "Mounting virtual filesystem at $mount_point..."

    # Mount the virtual filesystem in background
    sudo "$vfs_binary" "$mount_point"
    
    echo "Virtual filesystem mounted successfully at $mount_point"
    return 0
}

# unmount_virtual_fs <mount_point>
# Unmounts the virtual filesystem at the specified mount point
unmount_virtual_fs() {
    local mount_point="$1"
    
    # Convert mount point to absolute path for consistency
    mount_point="$(canonical "$mount_point")"
    
    if [ ! -d "$mount_point" ]; then
        echo "Warning: Mount point $mount_point does not exist"
        return 0
    fi
    
    echo "Unmounting virtual filesystem at $mount_point..."
    
    # Try to unmount using fusermount
    if command -v fusermount3 >/dev/null 2>&1; then
        sudo fusermount3 -u "$mount_point" 2>/dev/null || true
    elif command -v fusermount >/dev/null 2>&1; then
        sudo fusermount -u "$mount_point" 2>/dev/null || true
    else
        echo "Warning: fusermount not found, trying umount..."
        sudo umount "$mount_point" 2>/dev/null || true
    fi
    
    # Kill the VFS process if PID file exists
    if [ -f "$mount_point/.vfs.pid" ]; then
        local vfs_pid
        vfs_pid=$(cat "$mount_point/.vfs.pid")
        if [ -n "$vfs_pid" ]; then
            kill "$vfs_pid" 2>/dev/null || true
            # Give it a moment to clean up
            sleep 1
            # Force kill if still running
            kill -9 "$vfs_pid" 2>/dev/null || true
        fi
        rm -f "$mount_point/.vfs.pid"
    fi
    
    # Verify unmount was successful
    if mountpoint -q "$mount_point" 2>/dev/null; then
        echo "Warning: Failed to unmount $mount_point, may require manual cleanup"
        return 1
    fi
    
    echo "Virtual filesystem unmounted successfully from $mount_point"
    return 0
}

# mount_test_virtual_fs <server_root> <client_root>
# Mounts virtual filesystems for both server and client test environments
mount_test_virtual_fs() {
    local server_root="$1"
    local client_root="$2"
    
    echo "Setting up virtual filesystems for testing..."
    
    # Create virtual subdirectories
    local server_vfs_dir="$server_root/virtual"
    local client_vfs_dir="$client_root/virtual"
    
    # Mount virtual filesystem for server
    create_folder "$server_root" "./virtual"
    mount_virtual_fs "$server_vfs_dir"
    
    # Mount virtual filesystem for client
    mkdir -p "$client_root/virtual" # ./virtual is already in EXPECTED_FILES
    mount_virtual_fs "$client_vfs_dir"
    
    # Add virtual files to expected files list with their known hashes
    echo "Registering virtual files in expected lists..."
    
    echo "Virtual filesystems mounted and registered successfully"
    return 0
}

# unmount_test_virtual_fs <server_root> <client_root>
# Unmounts virtual filesystems for both server and client test environments
unmount_test_virtual_fs() {
    local server_root="$1"
    local client_root="$2"
    
    echo "Cleaning up virtual filesystems..."
    
    # Unmount both virtual filesystems
    unmount_virtual_fs "$server_root/virtual"
    unmount_virtual_fs "$client_root/virtual"
    
    # Remove virtual directories if empty
    rmdir "$server_root/virtual" 2>/dev/null || true
    rmdir "$client_root/virtual" 2>/dev/null || true
    
    echo "Virtual filesystem cleanup completed"
}