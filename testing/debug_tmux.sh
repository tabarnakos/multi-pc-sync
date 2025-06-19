#!/bin/bash
# Script to start a tmux session for debugging multi-pc-sync

SCENARIO_RANGE="${1:-""}"
SERVER_GDBSERVER_PORT=12345
CLIENT_GDBSERVER_PORT=12346
PROGRAM="multi_pc_sync"
PROGRAM_PATH="/home/harvey/multi-pc-sync/build/$PROGRAM"
TEST_FOLDER="/home/harvey/multi-pc-sync/test_sync_env0"

CLIENT_ROOT="$TEST_FOLDER/client"
SERVER_ROOT="$TEST_FOLDER/server"
MULTI_PC_SYNC_PORT=5555
SERVER_IP=127.0.0.1
CLIENT_OPTS="-s $SERVER_IP:$MULTI_PC_SYNC_PORT -y $CLIENT_ROOT"
SERVER_OPTS="-d $MULTI_PC_SYNC_PORT --exit-after-sync $SERVER_ROOT"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/debug_tmux_utils.sh"

parse_range "$SCENARIO_RANGE"

apply_latency=0

# Check if tc command is available
if ! command -v tc &> /dev/null; then
    echo "Error: tc command not found. Please install it (e.g., sudo apt install iproute2)." >&2
    exit 1
fi

# Trap to clean up tc rules on exit if latency was applied
trap "if [ \\"$apply_latency\\" == \\"true\\" ]; then sudo tc qdisc del dev lo root netem 2>/dev/null; echo 'Cleaned up tc qdisc rules.'; fi" EXIT

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
trap "if [ $apply_latency -gt 0 ]; then sudo tc qdisc del dev lo root netem 2>/dev/null; echo 'Cleaned up tc qdisc rules.'; fi" EXIT

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
        echo "8. Reserved for future use."
        echo "9. Reserved for future use."
        echo "10. Scenario 10: (Re-sync) Server moved files."
        echo "11. Scenario 11: (Re-sync) Client moved files."
        echo "12. Scenario 12: (Re-sync) Server edited files."
        echo "13. Scenario 13: (Re-sync) Client edited files."
        echo "14. Scenario 14: (Re-sync) Server deleted files."
        echo "15. Scenario 15: (Re-sync) Client deleted files."
        read -p "Enter the scenario number (1-15): " scenario
    fi
    echo "Running scenario: $scenario"
    
    # Wipe the test folder and recreate it
    if [ -d "$TEST_FOLDER" ]; then
        echo "Test folder already exists: $TEST_FOLDER"
        rm -rf "$TEST_FOLDER"
        echo "Removed existing test folder: $TEST_FOLDER"
    fi
    mkdir -p "$TEST_FOLDER"
    echo "Recreated test folder: $TEST_FOLDER"

    mkdir -p "$CLIENT_ROOT"
    mkdir -p "$SERVER_ROOT"

    case $scenario in
        1)
            echo "Running Scenario 1: Client files are empty, server files are populated."

            create_file "$SERVER_ROOT" "./file1.txt" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" "./10MBfile.bin" 10
            create_file "$SERVER_ROOT" "./100MBfile.bin" "./100MBfile.bin" 100

            create_folder "$SERVER_ROOT" "./folder1" "./folder1"
            create_file "$SERVER_ROOT" "./folder1/file4.txt" "./folder1/file4.txt" 1
            create_file "$SERVER_ROOT" "./folder1/file5.txt" "./folder1/file5.txt" 1
            create_folder "$SERVER_ROOT" "./folder2" "./folder2"
            ;;
        
        2)
            echo "Running Scenario 2: Server files are empty, client files are populated."
            
            create_file "$CLIENT_ROOT" "./file1.txt" "./file1.txt" 1
            create_file "$CLIENT_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$CLIENT_ROOT" "./file3.txt" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$CLIENT_ROOT" "./1MBfile.bin" "./1MBfile.bin" 1
            create_file "$CLIENT_ROOT" "./10MBfile.bin" "./10MBfile.bin" 10
            create_file "$CLIENT_ROOT" "./100MBfile.bin" "./100MBfile.bin" 100

            create_folder "$CLIENT_ROOT" "./folder1" "./folder1"
            create_file "$CLIENT_ROOT" "./folder1/file4.txt" "./folder1/file4.txt" 1
            create_file "$CLIENT_ROOT" "./folder1/file5.txt" "./folder1/file5.txt" 1
            create_folder "$CLIENT_ROOT" "./folder2" "./folder2"
            ;;
        3)
            echo "Running Scenario 3: Nested files and folders on client and server."

            create_file "$SERVER_ROOT" "./file1.txt" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" "./file3.txt" 1
            # Create large file for testing chunk transfers
            create_file "$SERVER_ROOT" "./100MBfile.bin" "./100MBfile.bin" 100
            create_folder "$SERVER_ROOT" "./folder1" "./folder1"
            create_folder "$SERVER_ROOT" "./folder1/subfolder1" "./folder1/subfolder1"
            create_file "$SERVER_ROOT" "./folder1/subfolder1/file4.txt" "./folder1/subfolder1/file4.txt" 1
            create_file "$SERVER_ROOT" "./folder1/subfolder1/file5.txt" "./folder1/subfolder1/file5.txt" 1
            create_folder "$SERVER_ROOT" "./folder2" "./folder2"
            create_folder "$SERVER_ROOT" "./folder2/subfolder2" "./folder2/subfolder2"
            create_folder "$SERVER_ROOT" "./folder2/subfolder2/subsubfolder2" "./folder2/subfolder2/subsubfolder2"
            create_file "$SERVER_ROOT" "./folder2/subfolder2/subsubfolder2/file6.txt" "./folder2/subfolder2/subsubfolder2/file6.txt" 1
            create_folder "$SERVER_ROOT" "./folder3" "./folder3"
            create_folder "$SERVER_ROOT" "./folder3/subfolder3" "./folder3/subfolder3"
            create_folder "$SERVER_ROOT" "./folder3/subfolder3/subsubfolder3" "./folder3/subfolder3/subsubfolder3"
            create_folder "$SERVER_ROOT" "./folder3/subfolder3/subsubfolder3/subsubsubfolder3" "./folder3/subfolder3/subsubfolder3/subsubsubfolder3"

            create_file "$CLIENT_ROOT" "./file7.txt" "./file7.txt" 1
            create_file "$CLIENT_ROOT" "./file8.txt" "./file8.txt" 1
            create_file "$CLIENT_ROOT" "./file9.txt" "./file9.txt" 1
            create_folder "$CLIENT_ROOT" "./folder4" "./folder4"
            create_folder "$CLIENT_ROOT" "./folder4/subfolder4" "./folder4/subfolder4"
            create_file "$CLIENT_ROOT" "./folder4/subfolder4/file10.txt" "./folder4/subfolder4/file10.txt" 1
            create_file "$CLIENT_ROOT" "./folder4/subfolder4/file11.txt" "./folder4/subfolder4/file11.txt" 1
            create_folder "$CLIENT_ROOT" "./folder5" "./folder5"
            create_file "$CLIENT_ROOT" "./folder5/100MBfile.bin" "./folder5/100MBfile.bin" 100
            create_folder "$CLIENT_ROOT" "./folder5/subfolder5" "./folder5/subfolder5"
            create_folder "$CLIENT_ROOT" "./folder5/subfolder5/subsubfolder5" "./folder5/subfolder5/subsubfolder5"
            create_file "$CLIENT_ROOT" "./folder5/subfolder5/subsubfolder5/file12.txt" "./folder5/subfolder5/subsubfolder5/file12.txt" 1
            ;;
        4)
            echo "Running Scenario 4: (Initial sync) 20ms latency, files on server only."
            apply_latency=20
            # File setup similar to Scenario 1

            create_file "$SERVER_ROOT" "./file1.txt" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" "./10MBfile.bin" 10
            create_file "$SERVER_ROOT" "./100MBfile.bin" "./100MBfile.bin" 100
            ;;
        5)
            echo "Running Scenario 5: (Initial sync) 250ms latency, files on server only."
            apply_latency=250

            create_file "$SERVER_ROOT" "./file1.txt" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" "./10MBfile.bin" 10
            create_file "$SERVER_ROOT" "./100MBfile.bin" "./100MBfile.bin" 100
            # Add your scenario 5 setup here
            ;;
        6)
            echo "Running Scenario 6: (Initial sync) 20ms latency, files on client only."
            apply_latency=20

            create_file "$CLIENT_ROOT" "./file1.txt" "./file1.txt" 1
            create_file "$CLIENT_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$CLIENT_ROOT" "./file3.txt" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$CLIENT_ROOT" "./1MBfile.bin" "./1MBfile.bin" 1
            create_file "$CLIENT_ROOT" "./10MBfile.bin" "./10MBfile.bin" 10
            create_file "$CLIENT_ROOT" "./100MBfile.bin" "./100MBfile.bin" 100
            ;;
        7)
            echo "Running Scenario 7: (Initial sync) 250ms latency, files on client only."
            apply_latency=250

            create_file "$CLIENT_ROOT" "./file1.txt" "./file1.txt" 1
            create_file "$CLIENT_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$CLIENT_ROOT" "./file3.txt" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$CLIENT_ROOT" "./1MBfile.bin" "./1MBfile.bin" 1
            create_file "$CLIENT_ROOT" "./10MBfile.bin" "./10MBfile.bin" 10
            create_file "$CLIENT_ROOT" "./100MBfile.bin" "./100MBfile.bin" 100
            ;;
        8)
            echo "Running Scenario 8: Reserved for future use."
            # Add your scenario 8 setup here
            ;;
        9)
            echo "Running Scenario 9: Reserved for future use."
            # Add your scenario 9 setup here
            ;;
        10)
            echo "Running Scenario 10: Server moved files."

            create_file "$SERVER_ROOT" "./file1.txt" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" "./file3.txt" 1
            create_folder "$SERVER_ROOT" "./folder1" "./folder3"                        # Folder1 will be moved to folder3
            create_file "$SERVER_ROOT" "./folder1/file4.txt" "./folder3/file4.txt" 1    # File4 will be moved to folder3
            create_file "$SERVER_ROOT" "./folder1/file5.txt" "./folder3/file5.txt" 1    # File5 will be moved to folder3
            create_folder "$SERVER_ROOT" "./folder2" "./folder2"
            # need to run a sync here to ensure the client has the initial files
            echo "Running initial sync to ensure client has the initial files."
            $SERVER_CMD_LINE &
            $CLIENT_CMD_LINE
            mv "$SERVER_ROOT/folder1" "$SERVER_ROOT/folder3"
            ;;
        11)
            echo "Running Scenario 11: Client moved files."
            
            create_file "$CLIENT_ROOT" "./file1.txt" "./file1.txt" 1
            create_file "$CLIENT_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$CLIENT_ROOT" "./file3.txt" "./file3.txt" 1

            create_folder "$CLIENT_ROOT" "./folder1" "./folder3"                        # Folder1 will be moved to folder3
            create_file "$CLIENT_ROOT" "./folder1/file4.txt" "./folder3/file4.txt" 1    # File4 will be moved to folder3
            create_file "$CLIENT_ROOT" "./folder1/file5.txt" "./folder3/file5.txt" 1    # File5 will be moved to folder3
            create_folder "$CLIENT_ROOT" "./folder2" "./folder2"        

            # need to run a sync here to ensure the server has the initial files
            echo "Running initial sync to ensure server has the initial files."
            $SERVER_CMD_LINE &
            $CLIENT_CMD_LINE
            mv "$CLIENT_ROOT/folder1" "$CLIENT_ROOT/folder3"
            ;;
        12)
            echo "Running Scenario 12: Server edited files."

            create_file "$SERVER_ROOT" "./file1.txt" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" "./10MBfile.bin" 10
            create_file "$SERVER_ROOT" "./100MBfile.bin" "./100MBfile.bin" 100

            create_folder "$SERVER_ROOT" "./folder1" "./folder1"
            create_file "$SERVER_ROOT" "./folder1/file4.txt" "" 1                       # File4 will be edited, store its hash later
            create_file "$SERVER_ROOT" "./folder1/file5.txt" "" 1                       # File5 will be edited, store its hash later
            create_folder "$SERVER_ROOT" "./folder2" "./folder2"

            # need to run a sync here to ensure the client has the initial files
            echo "Running initial sync to ensure client has the initial files."
            $SERVER_CMD_LINE &
            $CLIENT_CMD_LINE
            echo "Editing file4 and file5 on server."
            create_file "$SERVER_ROOT" "./folder1/file4.txt" "./folder1/file4.txt" 1
            create_file "$SERVER_ROOT" "./folder1/file5.txt" "./folder1/file5.txt" 1
            ;;
        13)
            echo "Running Scenario 13: Client edited files."

            create_file "$SERVER_ROOT" "./file1.txt" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" "./10MBfile.bin" 10
            create_file "$SERVER_ROOT" "./100MBfile.bin" "./100MBfile.bin" 100

            create_folder "$SERVER_ROOT" "./folder1" "./folder1"
            create_file "$SERVER_ROOT" "./folder1/file4.txt" "" 1                       # File4 will be edited, store its hash later
            create_file "$SERVER_ROOT" "./folder1/file5.txt" "" 1                       # File5 will be edited, store its hash later
            create_folder "$SERVER_ROOT" "./folder2" "./folder2"
            # need to run a sync here to ensure the server has the initial files
            echo "Running initial sync to ensure server has the initial files."
            $SERVER_CMD_LINE &
            $CLIENT_CMD_LINE
            echo "Editing file4 and file5 on client."
            create_file "$CLIENT_ROOT" "./folder1/file4.txt" "./folder1/file4.txt" 1
            create_file "$CLIENT_ROOT" "./folder1/file5.txt" "./folder1/file5.txt" 1
            ;;
        14)
            echo "Running Scenario 14: Server deleted files."
            
            create_file "$SERVER_ROOT" "./file1.txt" "" 1                               # File1 will be deleted
            create_file "$SERVER_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" "" 10                           # 10MBfile will be deleted
            create_file "$SERVER_ROOT" "./100MBfile.bin" "./100MBfile.bin" 100

            # need to run a sync here to ensure the client has the initial files
            echo "Running initial sync to ensure client has the initial files."
            $SERVER_CMD_LINE &
            $CLIENT_CMD_LINE
            echo "Deleting file1 and 10MBfile on server."
            rm "$SERVER_ROOT/file1.txt" "$SERVER_ROOT/10MBfile.bin"
            ;;
        15)
            echo "Running Scenario 15: Client deleted files."
            
            create_file "$SERVER_ROOT" "./file1.txt" "" 1                               # File1 will be deleted
            create_file "$SERVER_ROOT" "./file2.txt" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" "" 10                           # 10MBfile will be deleted
            create_file "$SERVER_ROOT" "./100MBfile.bin" "./100MBfile.bin" 100

            # need to run a sync here to ensure the server has the initial files
            echo "Running initial sync to ensure server has the initial files."
            $SERVER_CMD_LINE &
            $CLIENT_CMD_LINE
            echo "Deleting file1 and 10MBfile on client."
            rm "$CLIENT_ROOT/file1.txt" "$CLIENT_ROOT/10MBfile.bin"
            ;;
        *)
            echo "Invalid scenario number. Please run the script again and select a valid scenario."
            exit 1
            ;;
    esac

    if [ "$apply_latency" -gt 0 ]; then
    apply_latency $apply_latency
    fi
    if [[ $SCENARIOS == "0 0" ]]; then
        echo "Running interactively in tmux"

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
        $CLIENT_CMD_LINE
    fi

    if [ "$apply_latency" -gt 0 ]; then
        echo "Removing latency from loopback interface..."
        sudo tc qdisc del dev lo root netem
        if [ $? -ne 0 ]; then
            echo "Warning: Failed to remove latency with tc. Manual cleanup may be required: sudo tc qdisc del dev lo root netem" >&2
        fi
    fi

    if [[ $SCENARIOS == "0 0" ]]; then
        # Perform file comparison after processes have exited
        echo "Checking if the expected files exist in the client root directory..."

        echo "Comparing files in CLIENT_ROOT with EXPECTED_FILES..."
        compare_files "$CLIENT_ROOT"
        echo "Comparing files in SERVER_ROOT with EXPECTED_FILES..."
        compare_files "$SERVER_ROOT"
    else
        echo "========== Scenario $scenario Test Report ==========" >> "$SCRIPT_DIR/test_report.txt"

        echo "Comparing files in CLIENT_ROOT with EXPECTED_FILES..." >> "$SCRIPT_DIR/test_report.txt"
        compare_files "$CLIENT_ROOT" >> "$SCRIPT_DIR/test_report.txt"
        echo "Comparing files in SERVER_ROOT with EXPECTED_FILES..." >> "$SCRIPT_DIR/test_report.txt"
        compare_files "$SERVER_ROOT" >> "$SCRIPT_DIR/test_report.txt"

        echo "========== Scenario $scenario /Test Report ==========" >> "$SCRIPT_DIR/test_report.txt"
    fi
done






