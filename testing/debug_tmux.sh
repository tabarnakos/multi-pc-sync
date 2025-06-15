#!/bin/bash
# Script to start a tmux session for debugging multi-pc-sync

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

EXPECTED_FILES=""

# Ask the user to select the scenario
echo "Please select the scenario to run:"
echo "1. Scenario 1: (Initial sync) client files are empty, server files are populated."
echo "2. Scenario 2: (Initial sync) server files are empty, client files are populated."
echo "3. Scenario 3: (Initial sync) Nested files and folders on client and server."
echo "4. Reserved for future use."
echo "5. Reserved for future use."
echo "6. Reserved for future use."
echo "7. Reserved for future use."
echo "8. Reserved for future use."
echo "9. Reserved for future use."
echo "10. Scenario 10: (Re-sync) Server moved files."
echo "11. Scenario 11: (Re-sync) Client moved files."
echo "12. Scenario 12: (Re-sync) Server edited files."
echo "13. Scenario 13: (Re-sync) Client edited files."
echo "14. Scenario 14: (Re-sync) Server deleted files."
echo "15. Scenario 15: (Re-sync) Client deleted files."
read -p "Enter the scenario number (1-15): " scenario
case $scenario in
    1)
        echo "Running Scenario 1: Client files are empty, server files are populated."
        echo "file 1 content" > "$SERVER_ROOT/file1.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file1.txt"
        echo "file 2 content" > "$SERVER_ROOT/file2.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file2.txt"
        echo "file 3 content" > "$SERVER_ROOT/file3.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file3.txt"
        mkdir -p "$SERVER_ROOT/folder1"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1"
        echo "file 4 content" > "$SERVER_ROOT/folder1/file4.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/file4.txt"
        echo "file 5 content" > "$SERVER_ROOT/folder1/file5.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/file5.txt"
        mkdir -p "$SERVER_ROOT/folder2"
        EXPECTED_FILES="$EXPECTED_FILES ./folder2"
        ;;
    
    2)
        echo "Running Scenario 2: Server files are empty, client files are populated."
        echo "file 1 content" > "$CLIENT_ROOT/file1.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file1.txt"
        echo "file 2 content" > "$CLIENT_ROOT/file2.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file2.txt"
        echo "file 3 content" > "$CLIENT_ROOT/file3.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file3.txt"
        mkdir -p "$CLIENT_ROOT/folder1"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1"
        echo "file 4 content" > "$CLIENT_ROOT/folder1/file4.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/file4.txt"
        echo "file 5 content" > "$CLIENT_ROOT/folder1/file5.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/file5.txt"
        mkdir -p "$CLIENT_ROOT/folder2"
        EXPECTED_FILES="$EXPECTED_FILES ./folder2"
        ;;
    3)
        echo "Running Scenario 3: Nested files and folders on client and server."
        echo "file 1 content" > "$SERVER_ROOT/file1.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file1.txt"
        echo "file 2 content" > "$SERVER_ROOT/file2.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file2.txt"
        echo "file 3 content" > "$SERVER_ROOT/file3.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file3.txt"
        mkdir -p "$SERVER_ROOT/folder1/subfolder1"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1 ./folder1/subfolder1"
        echo "file 4 content" > "$SERVER_ROOT/folder1/subfolder1/file4.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/subfolder1/file4.txt"
        echo "file 5 content" > "$SERVER_ROOT/folder1/subfolder1/file5.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/subfolder1/file5.txt"
        mkdir -p "$SERVER_ROOT/folder4/subfolder3/subsubfolder3"
        EXPECTED_FILES="$EXPECTED_FILES ./folder4 ./folder4/subfolder3 ./folder4/subfolder3/subsubfolder3"
        mkdir -p "$SERVER_ROOT/folder4/subfolder3/subsubfolder4"
        EXPECTED_FILES="$EXPECTED_FILES ./folder4/subfolder3/subsubfolder4"
        echo "file 7 content" > "$SERVER_ROOT/folder4/subfolder3/subsubfolder4/file7.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder4/subfolder3/subsubfolder4/file7.txt"
        mkdir -p "$CLIENT_ROOT/folder2/subfolder2"
        EXPECTED_FILES="$EXPECTED_FILES ./folder2 ./folder2/subfolder2"
        echo "file 6 content" > "$CLIENT_ROOT/folder2/subfolder2/file6.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder2/subfolder2/file6.txt"
        mkdir -p "$CLIENT_ROOT/folder3/subfolder3/subsubfolder3"
        EXPECTED_FILES="$EXPECTED_FILES ./folder3 ./folder3/subfolder3 ./folder3/subfolder3/subsubfolder3"
        mkdir -p "$CLIENT_ROOT/folder3/subfolder3/subsubfolder4"
        EXPECTED_FILES="$EXPECTED_FILES ./folder3/subfolder3/subsubfolder4"
        echo "file 7 content" > "$CLIENT_ROOT/folder3/subfolder3/subsubfolder4/file7.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder3/subfolder3/subsubfolder4/file7.txt"
        ;;
    4)
        echo "Running Scenario 4: Reserved for future use."
        # Add your scenario 4 setup here
        ;;
    5)
        echo "Running Scenario 5: Reserved for future use."
        # Add your scenario 5 setup here
        ;;
    6)
        echo "Running Scenario 6: Reserved for future use."
        # Add your scenario 6 setup here
        ;;
    7)
        echo "Running Scenario 7: Reserved for future use."
        # Add your scenario 7 setup here
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
        echo "file 1 content" > "$SERVER_ROOT/file1.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file1.txt"
        echo "file 2 content" > "$SERVER_ROOT/file2.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file2.txt"
        echo "file 3 content" > "$SERVER_ROOT/file3.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file3.txt"
        mkdir -p "$SERVER_ROOT/folder1"
        echo "file 4 content" > "$SERVER_ROOT/folder1/file4.txt"
        echo "file 5 content" > "$SERVER_ROOT/folder1/file5.txt"
        mkdir -p "$SERVER_ROOT/folder2"
        EXPECTED_FILES="$EXPECTED_FILES ./folder2"
        # need to run a sync here to ensure the client has the initial files
        echo "Running initial sync to ensure client has the initial files."
        $SERVER_CMD_LINE &
        $CLIENT_CMD_LINE
        mv "$SERVER_ROOT/folder1" "$SERVER_ROOT/folder3"
        EXPECTED_FILES="$EXPECTED_FILES ./folder3"
        EXPECTED_FILES="$EXPECTED_FILES ./folder3/file4.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder3/file5.txt"
        ;;
    11)
        echo "Running Scenario 11: Client moved files."
        echo "file 1 content" > "$CLIENT_ROOT/file1.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file1.txt"
        echo "file 2 content" > "$CLIENT_ROOT/file2.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file2.txt"
        echo "file 3 content" > "$CLIENT_ROOT/file3.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file3.txt"
        mkdir -p "$CLIENT_ROOT/folder1"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1"
        echo "file 4 content" > "$CLIENT_ROOT/folder1/file4.txt"
        echo "file 5 content" > "$CLIENT_ROOT/folder1/file5.txt"
        mkdir -p "$CLIENT_ROOT/folder2"
        EXPECTED_FILES="$EXPECTED_FILES ./folder2"
        # need to run a sync here to ensure the server has the initial files
        echo "Running initial sync to ensure server has the initial files."
        $SERVER_CMD_LINE &
        $CLIENT_CMD_LINE
        mv "$CLIENT_ROOT/folder1" "$CLIENT_ROOT/folder3"
        EXPECTED_FILES="$EXPECTED_FILES ./folder3"
        EXPECTED_FILES="$EXPECTED_FILES ./folder3/file4.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder3/file5.txt"
        ;;
    12)
        echo "Running Scenario 12: Server edited files."
        echo "file 1 content" > "$SERVER_ROOT/file1.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file1.txt"
        echo "file 2 content" > "$SERVER_ROOT/file2.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file2.txt"
        echo "file 3 content" > "$SERVER_ROOT/file3.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file3.txt"
        mkdir -p "$SERVER_ROOT/folder1"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1"
        echo "file 4 content" > "$SERVER_ROOT/folder1/file4.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/file4.txt"
        echo "file 5 content" > "$SERVER_ROOT/folder1/file5.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/file5.txt"
        mkdir -p "$SERVER_ROOT/folder2"
        EXPECTED_FILES="$EXPECTED_FILES ./folder2"
        # need to run a sync here to ensure the client has the initial files
        echo "Running initial sync to ensure client has the initial files."
        $SERVER_CMD_LINE &
        $CLIENT_CMD_LINE
        echo "Editing file1 on server."
        echo "edited file 1 content" > "$SERVER_ROOT/file1.txt"
        ;;
    13)
        echo "Running Scenario 13: Client edited files."
        echo "file 1 content" > "$CLIENT_ROOT/file1.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file1.txt"
        echo "file 2 content" > "$CLIENT_ROOT/file2.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file2.txt"
        echo "file 3 content" > "$CLIENT_ROOT/file3.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file3.txt"
        mkdir -p "$CLIENT_ROOT/folder1"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1"
        echo "file 4 content" > "$CLIENT_ROOT/folder1/file4.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/file4.txt"
        echo "file 5 content" > "$CLIENT_ROOT/folder1/file5.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/file5.txt"
        mkdir -p "$CLIENT_ROOT/folder2"
        EXPECTED_FILES="$EXPECTED_FILES ./folder2"
        # need to run a sync here to ensure the server has the initial files
        echo "Running initial sync to ensure server has the initial files."
        $SERVER_CMD_LINE &
        $CLIENT_CMD_LINE
        echo "Editing file1 on client."
        echo "edited file 1 content" > "$CLIENT_ROOT/file1.txt"
        ;;
    14)
        echo "Running Scenario 14: Server deleted files."
        echo "file 1 content" > "$SERVER_ROOT/file1.txt"
        #EXPECTED_FILES="$EXPECTED_FILES ./file1.txt"
        echo "file 2 content" > "$SERVER_ROOT/file2.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file2.txt"
        echo "file 3 content" > "$SERVER_ROOT/file3.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file3.txt"
        mkdir -p "$SERVER_ROOT/folder1"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1"
        echo "file 4 content" > "$SERVER_ROOT/folder1/file4.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/file4.txt"
        echo "file 5 content" > "$SERVER_ROOT/folder1/file5.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/file5.txt"
        mkdir -p "$SERVER_ROOT/folder2"
        EXPECTED_FILES="$EXPECTED_FILES ./folder2"
        # need to run a sync here to ensure the client has the initial files
        echo "Running initial sync to ensure client has the initial files."
        $SERVER_CMD_LINE &
        $CLIENT_CMD_LINE
        echo "Deleting file1 on server."
        rm "$SERVER_ROOT/file1.txt"
        ;;
    15)
        echo "Running Scenario 15: Client deleted files."
        echo "file 1 content" > "$CLIENT_ROOT/file1.txt"
        #EXPECTED_FILES="$EXPECTED_FILES ./file1.txt"
        echo "file 2 content" > "$CLIENT_ROOT/file2.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file2.txt"
        echo "file 3 content" > "$CLIENT_ROOT/file3.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./file3.txt"
        mkdir -p "$CLIENT_ROOT/folder1"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1"
        echo "file 4 content" > "$CLIENT_ROOT/folder1/file4.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/file4.txt"
        echo "file 5 content" > "$CLIENT_ROOT/folder1/file5.txt"
        EXPECTED_FILES="$EXPECTED_FILES ./folder1/file5.txt"
        mkdir -p "$CLIENT_ROOT/folder2"
        # need to run a sync here to ensure the server has the initial files
        echo "Running initial sync to ensure server has the initial files."
        $SERVER_CMD_LINE &
        $CLIENT_CMD_LINE
        echo "Deleting file1 on client."
        rm "$CLIENT_ROOT/file1.txt"
        ;;
    *)
        echo "Invalid scenario number. Please run the script again and select a valid scenario."
        exit 1
        ;;
esac

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
tmux split-window -v -l 50 -t sync_debug:0.0

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

# Perform file comparison after processes have exited
echo "Checking if the expected files exist in the client root directory..."

echo "Comparing files in CLIENT_ROOT with EXPECTED_FILES..."
compare_files "$CLIENT_ROOT"
echo "Comparing files in SERVER_ROOT with EXPECTED_FILES..."
compare_files "$SERVER_ROOT"