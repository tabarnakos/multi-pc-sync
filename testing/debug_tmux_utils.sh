send_line() {
  local pane="$1"
  shift
  local line="$*"
  tmux send-keys -t sync_debug:$pane "stdbuf -oL echo \"$line\"" C-m
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
    client_files=$(find "$1" | sed "s|$1/|./|")

    # Combine server and client files into a single list
    all_files=$(echo -e "$client_files" | sort | uniq)
    expected_files=$(echo "$1 $EXPECTED_FILES" | tr ' ' '\n' | sort | uniq)


    # Check for missing or extra files
    missing_files=""
    extra_files=""

    for file in $expected_files; do
        if ! echo "$all_files" | grep -q "^$file$"; then
            missing_files+="$file\n"
        fi
    done

    for file in $all_files; do
        if ! echo "$expected_files" | grep -Fxq "$file"; then
            extra_files+="$file\n"
        fi
    done

    # Display results
    if [ -n "$missing_files" ]; then
        echo "Missing files:" && echo -e "$missing_files"
    else
        echo "No missing files."
    fi

    if [ -n "$extra_files" ]; then
        echo "Extra files:" && echo -e "$extra_files"
    else
        echo "No extra files."
    fi
}