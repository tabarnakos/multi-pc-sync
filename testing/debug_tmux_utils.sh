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
    local exp="$3"     # expected path to add or empty string to skip
    local size_mb="$4" # file size in megabytes; 0 for empty file

    local fullpath="$root/$relpath"
    # ensure directory exists
    mkdir -p "$(dirname "$fullpath")"

    if [ "$size_mb" -gt 0 ]; then
        dd if=/dev/urandom of="$fullpath" bs=1M count="$size_mb" status=none
    else
        > "$fullpath"
    fi

    # on non-empty exp, register expected file and its md5 hash
    if [ -n "$exp" ]; then
        EXPECTED_FILES="$EXPECTED_FILES $exp"
        EXPECTED_HASHES="$EXPECTED_HASHES $(md5sum "$fullpath" | awk '{print $1}')"
    fi
}

# create_folder <root> <relative-path> <expected-relpath-or-empty>
create_folder() {
    local root="$1"    # base directory where file is created
    local relpath="$2" # relative path inside root
    local exp="$3"     # expected path to add or empty string to skip

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
    local start end

    if [[ -z "$input" ]]; then
        echo "No input provided. Defaulting to interactive mode"
        start="0" # No start
        end="0"   # No end
    elif [[ "$input" =~ ^[0-9]+$ ]]; then
        echo "Single number provided: $input"
        start="$input"
        end="$input"
    elif [[ "$input" =~ ^([0-9]+)-([0-9]+)$ ]]; then
        start="${BASH_REMATCH[1]}"
        end="${BASH_REMATCH[2]}"
        echo "Range provided: Start=$start, End=$end"
    else
        echo "Invalid input format. Please use # or #-#." >&2
        exit 1
    fi

    # Return the parsed values
    SCENARIOS="$start $end"
}