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

compare_files() {
    # List files in folder
    files_in_directory=$(find "$1" | sed "s|$1/|./|")

    # Combine server and client files into a single list
    all_files=$(echo -e "$files_in_directory")
    all_hashes=$(find "$1" -type f -exec md5sum {} + | awk '{print $1}')
    expected_files=$(echo "$1 $EXPECTED_FILES" | tr ' ' '\n')
    expected_hashes=$(echo "$EXPECTED_HASHES" | tr ' ' '\n')

    files_to_ignore="./.folderindex ./.folderindex.last_run ./.remote.folderindex ./.remote.folderindex.last_run ./sync_commands.sh"

    echo "expected $(count_items_in_list "$expected_files") files, $(count_items_in_list "$expected_hashes") hashes"

    # Capture hashes of files to ignore
    hashes_to_ignore=""
    for file in $files_to_ignore; do
        if [ -f "$1/$file" ]; then
            hash=$(md5sum "$1/$file" | awk '{print $1}')
            hashes_to_ignore="$hashes_to_ignore $hash"
        fi
    done

    # Check for missing or extra files
    missing_files=""
    missing_hashes=""
    extra_files=""
    extra_hashes=""

    for file in $expected_files; do
        if ! echo "$all_files" | grep -q "^$file$"; then
            if ! echo "$files_to_ignore" | grep -q "^$file$"; then
                missing_files+="$file\n"
            fi
        fi
    done

    for hash in $expected_hashes; do
        if ! echo "$all_hashes" | grep -q "^$hash$"; then
            missing_hashes+="$hash\n"
        fi
    done

    for file in $all_files; do
        if ! echo "$expected_files" | grep -Fxq "$file"; then
            if ! echo "$files_to_ignore" | grep -q "$file"; then
                extra_files+="$file\n"
            fi
        fi
    done

    for hash in $all_hashes; do
        if ! echo "$expected_hashes" | grep -Fxq "$hash"; then
            if ! echo "$hashes_to_ignore" | grep -q "$hash"; then
                extra_hashes+="$hash\n"
            fi
        fi
    done

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

    if [ -n "$missing_files" ]; then
        echo -e "${RED}Missing files:${NC}" && echo -e "$missing_files"
    else
        echo -e "${GREEN}No missing files.${NC}"
    fi

    if [ -n "$extra_files" ]; then
        echo -e "${YELLOW}Extra files:${NC}" && echo -e "$extra_files"
    else
        echo -e "${GREEN}No extra files.${NC}"
    fi

    if [ -n "$missing_hashes" ]; then
        echo -e "${RED}Missing hashes:${NC}" && echo -e "$missing_hashes"
    else
        echo -e "${GREEN}No missing hashes.${NC}"
    fi

    if [ -n "$extra_hashes" ]; then
        echo -e "${YELLOW}Extra hashes:${NC}" && echo -e "$extra_hashes"
    else
        echo -e "${GREEN}No extra hashes.${NC}"
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

    local fullpath="$root/$relpath"
    # ensure directory exists
    mkdir -p "$(dirname "$fullpath")"

    if [ "$size_mb" -gt 0 ]; then
        dd if=/dev/urandom of="$fullpath" bs=${size_mb}M count=1 status=none
    else
        > "$fullpath"
    fi

    # on non-empty exp, register expected file and its md5 hash
    if [ -n "$exp" ]; then
        EXPECTED_FILES=$(add_item_to_list "$EXPECTED_FILES" "$exp")
        EXPECTED_HASHES=$(add_item_to_list "$EXPECTED_HASHES" "$(hash_file "$root" "$relpath")")
    fi
}

# create_folder <root> <relative-path> <expected-relpath-or-empty>
create_folder() {
    local root="$1"    # base directory where file is created
    local relpath="$2" # relative path inside root
    local exp="$2"     # expected path to add or empty string to skip

    local fullpath="$root/$relpath"
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