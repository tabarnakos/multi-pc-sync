#!/bin/bash
# Script to start a tmux session for debugging multi-pc-sync

VERBOSE=0
if [[ "$1" == "--verbose" ]]; then
    VERBOSE=1
    shift
fi

verbose_log() {
    if [[ "$VERBOSE" == "1" ]]; then
        echo "[VERBOSE] $*"
    fi
}

SCENARIO_RANGE="${1:-""}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/debug_tmux_utils.sh"
source "$SCRIPT_DIR/scenarios.sh"

SERVER_GDBSERVER_PORT=12345
CLIENT_GDBSERVER_PORT=12346
PROGRAM="multi_pc_sync"
PROGRAM_PATH="$(canonical "$SCRIPT_DIR/../build/$PROGRAM")"
TEST_FOLDER="$(canonical "$SCRIPT_DIR/../test_sync_env0")"

CLIENT_ROOT="$TEST_FOLDER/client"
SERVER_ROOT="$TEST_FOLDER/server"
MULTI_PC_SYNC_PORT=5555
SERVER_IP=127.0.0.1
CLIENT_OPTS="-s $SERVER_IP:$MULTI_PC_SYNC_PORT -y $CLIENT_ROOT"
SERVER_OPTS="-d $MULTI_PC_SYNC_PORT --exit-after-sync $SERVER_ROOT"

parse_range "$SCENARIO_RANGE"

apply_latency=0

# Check if tc command is available
if ! command -v tc &> /dev/null; then
    echo "Error: tc command not found. Please install it (e.g., sudo apt install iproute2)." >&2
    exit 1
fi

# Trap to clean up tc rules on exit if latency was applied
trap "if [ \\"$apply_latency\\" == \\"true\\" ]; then sudo tc qdisc del dev lo root netem 2>/dev/null; verbose_log 'Cleaned up tc qdisc rules.'; fi" EXIT

CLIENT_CMD_LINE="$PROGRAM_PATH $CLIENT_OPTS"
SERVER_CMD_LINE="$PROGRAM_PATH $SERVER_OPTS"
# Check if the program exists
if [ ! -f "$PROGRAM_PATH" ]; then
    echo "Error: $PROGRAM not found at $PROGRAM_PATH"
    exit 1
fi
# Check if tmux is installed
if ! command -v tmux &> /dev/null; then
    echo "Error: tmux is not installed. Please install it and try again."
    exit 1
fi
# Check if gnome-terminal is installed
if ! command -v gnome-terminal &> /dev/null; then
    echo "Error: gnome-terminal is not installed. Please install it and try again."
    exit 1
fi

# Check if tc is installed
if ! command -v tc &> /dev/null; then
    echo "Error: tc command not found. Please install it (e.g., sudo apt install iproute2) and try again."
    exit 1
fi

# Trap to clean up tc rules on exit
trap "if [ $apply_latency -gt 0 ]; then sudo tc qdisc del dev lo root netem 2>/dev/null; verbose_log 'Cleaned up tc qdisc rules.'; fi" EXIT

read start end <<< "$SCENARIOS"

if [[ $SCENARIOS != "0 0" ]]; then
    rm -f "$SCRIPT_DIR/test_report.txt"
fi

for scenario in $(seq "$start" "$end"); do
    # Declare variables
    EXPECTED_FILES=""
    EXPECTED_HASHES=""
    apply_latency="0" # Flag to indicate if latency should be applied
    if [[ $scenario == "0" ]]; then
        # Ask the user to select the scenario
        echo "Please select the scenario to run:"
        echo "1. Scenario 1: (Initial sync) client files are empty, server files are populated."
        echo "2. Scenario 2: (Initial sync) server files are empty, client files are populated."
        echo "3. Scenario 3: (Initial sync) Nested files and folders on client and server."
        echo "4. Scenario 4: (Initial sync) 20ms latency, files on server only."
        echo "5. Scenario 5: (Initial sync) 250ms latency, files on server only."
        echo "6. Scenario 6: (Initial sync) 20ms latency, files on client only."
        echo "7. Scenario 7: (Initial sync) 250ms latency, files on client only."
        echo "8. Scenario 8: File created on both sides with identical content."
        echo "9. Scenario 9: File created on both sides with different content."
        echo "10. Scenario 10: (Re-sync) Server moved files."
        echo "11. Scenario 11: (Re-sync) Client moved files."
        echo "12. Scenario 12: (Re-sync) Server edited files."
        echo "13. Scenario 13: (Re-sync) Client edited files."
        echo "14. Scenario 14: (Re-sync) Server deleted files."
        echo "15. Scenario 15: (Re-sync) Client deleted files."
        echo "16. Scenario 16: (Re-sync) File deleted on both sides."
        echo "17. Scenario 17: (Re-sync) File modified differently on both sides."
        echo "18. Scenario 18: (Re-sync) File moved to another folder on server."
        echo "19. Scenario 19: (Re-sync) File moved to another folder on client."
        echo "20. Scenario 20: (Re-sync) File moved and modified simultaneously on one side."
        echo "21. Scenario 21: (Re-sync) File renamed on both sides but with different names."
        echo "22. Scenario 22: Multiple operations on same file - modify then rename."
        echo "23. Scenario 23: Operations on files within renamed directories. Test file rename, edit and delete."
        echo "24. Scenario 24: Operations on files within moved directories. Test file rename, edit and delete."
        echo "25. Scenario 25: Circular rename (A→B, B→A across sides)."
        echo "26. Scenario 26: File deleted on the server, modified on the client."
        echo "27. Scenario 27: File deleted on the client, modified on the server."
        echo "28. Scenario 28: File moved on the server, renamed on the client."
        echo "29. Scenario 29: File moved on the client, renamed on the server."
        echo "30. Scenario 30: Filename case changes."
        echo "31. Scenario 31: Very large file (10GB)."
        echo "32. Scenario 32: File with 0 bytes."
        echo "99. Scenario 99: (Large and complex file system) Build a large file system on both client and server."
        read -p "Enter the scenario number (1-15): " scenario
    fi
    echo "Running scenario: $scenario"
    
    # Wipe the test folder and recreate it
    if [ -d "$TEST_FOLDER" ]; then
        verbose_log "Test folder already exists: $TEST_FOLDER"
        rm -rf "$TEST_FOLDER"
        verbose_log "Removed existing test folder: $TEST_FOLDER"
    fi
    mkdir -p "$TEST_FOLDER"
    verbose_log "Recreated test folder: $TEST_FOLDER"

    mkdir -p "$CLIENT_ROOT"
    mkdir -p "$SERVER_ROOT"

    # Delegate scenario setup to scenarios.sh
    run_scenario "$scenario"

    echo "========== Scenario $scenario =========="
    echo "$scenario_name"

    if [ "$apply_latency" -gt 0 ]; then
        apply_latency $apply_latency
    fi
    if [[ $SCENARIOS == "0 0" ]]; then
        verbose_log "Running interactively in tmux"

        # Start a new tmux session named 'sync_debug'
        tmux kill-session -t sync_debug 2>/dev/null
        tmux new-session -d -s sync_debug 'bash'
        tmux set-option -t sync_debug -g mouse on

        #tmux attach-session -t sync_debug

        # Rename the first window to 'server'
        tmux rename-window -t sync_debug:0 'server'
        # Send commands to the first pane
        tmux send-keys -t sync_debug:0 "source $SCRIPT_DIR/debug_tmux_utils.sh" C-m
        tmux send-keys -t sync_debug:0 "run_server $SERVER_GDBSERVER_PORT $SERVER_CMD_LINE" C-m

        # Split the tmux window into two panes
        tmux split-window -v -l 67 -t sync_debug:0.0

        # Rename the second pane to 'client'
        tmux rename-window -t sync_debug:0.1 'client'
        # Send commands to the second pane
        tmux send-keys -t sync_debug:0.1 "source $SCRIPT_DIR/debug_tmux_utils.sh" C-m
        tmux send-keys -t sync_debug:0.1 "run_client $CLIENT_GDBSERVER_PORT $CLIENT_CMD_LINE" C-m
        # Wait for the server to start

        tmux attach-session -t sync_debug
        # Wait for the server and client processes to exit
        while tmux list-panes -t sync_debug -F '#{pane_active} #{pane_pid}' | grep -q '1'; do
            sleep 0.1
        done

    else
        $SERVER_CMD_LINE &
        sleep 0.25  # Give the server time to start
        $CLIENT_CMD_LINE &
        wait
    fi

    if [ "$apply_latency" -gt 0 ]; then
        verbose_log "Removing latency from loopback interface..."
        sudo tc qdisc del dev lo root netem
        if [ $? -ne 0 ]; then
            echo "Warning: Failed to remove latency with tc. Manual cleanup may be required: sudo tc qdisc del dev lo root netem" >&2
        fi
    fi

    if [[ $SCENARIOS == "0 0" ]]; then
        # Perform file comparison after processes have exited
        echo "========== Scenario $scenario - $scenario_name Test Report =========="
        if ["$VERBOSE" == "1" ]; then
            echo "EXPECTED_FILES content: " >> "$SCRIPT_DIR/test_report.txt"
            echo $(echo "$EXPECTED_FILES" | tr ' ' '\n') >> "$SCRIPT_DIR/test_report.txt"
            echo "EXPECTED_HASHES content: " >> "$SCRIPT_DIR/test_report.txt"
            echo $(echo "$EXPECTED_HASHES" | tr ' ' '\n') >> "$SCRIPT_DIR/test_report.txt"
        fi

        echo "Comparing files in CLIENT_ROOT with EXPECTED_FILES..."
        compare_files "$CLIENT_ROOT"
        echo "Comparing files in SERVER_ROOT with EXPECTED_FILES..."
        compare_files "$SERVER_ROOT"

        echo "========== Scenario $scenario - $scenario_name /Test Report =========="
    else
        echo "========== Scenario $scenario - $scenario_name Test Report ==========" >> "$SCRIPT_DIR/test_report.txt"
        if [ "$VERBOSE" == "1" ]; then
            echo "EXPECTED_FILES content: " >> "$SCRIPT_DIR/test_report.txt"
            echo $(echo "$EXPECTED_FILES" | tr ' ' '\n') >> "$SCRIPT_DIR/test_report.txt"
            echo "EXPECTED_HASHES content: " >> "$SCRIPT_DIR/test_report.txt"
            echo $(echo "$EXPECTED_HASHES" | tr ' ' '\n') >> "$SCRIPT_DIR/test_report.txt"
        fi
        echo "Comparing files in CLIENT_ROOT with EXPECTED_FILES..." >> "$SCRIPT_DIR/test_report.txt"
        compare_files "$CLIENT_ROOT" >> "$SCRIPT_DIR/test_report.txt"
        echo "Comparing files in SERVER_ROOT with EXPECTED_FILES..." >> "$SCRIPT_DIR/test_report.txt"
        compare_files "$SERVER_ROOT" >> "$SCRIPT_DIR/test_report.txt"

        echo "========== Scenario $scenario - $scenario_name /Test Report ==========" >> "$SCRIPT_DIR/test_report.txt"
    fi
done






