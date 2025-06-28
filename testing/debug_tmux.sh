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
        echo "8. Reserved for future use."
        echo "9. Reserved for future use."
        echo "10. Scenario 10: (Re-sync) Server moved files."
        echo "11. Scenario 11: (Re-sync) Client moved files."
        echo "12. Scenario 12: (Re-sync) Server edited files."
        echo "13. Scenario 13: (Re-sync) Client edited files."
        echo "14. Scenario 14: (Re-sync) Server deleted files."
        echo "15. Scenario 15: (Re-sync) Client deleted files."
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

    case $scenario in
        1)
            scenario_name="Initial sync: Client files empty, Server files populated"

            create_file "$SERVER_ROOT" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" 10
            create_file "$SERVER_ROOT" "./100MBfile.bin" 100

            create_folder "$SERVER_ROOT" "./folder1"
            create_file "$SERVER_ROOT" "./folder1/file4.txt" 1
            create_file "$SERVER_ROOT" "./folder1/file5.txt" 1
            create_folder "$SERVER_ROOT" "./folder2"
            ;;
        
        2)
            scenario_name="Initial sync: Server files are empty, Client files are populated."

            create_file "$CLIENT_ROOT" "./file1.txt" 1
            create_file "$CLIENT_ROOT" "./file2.txt" 1
            create_file "$CLIENT_ROOT" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$CLIENT_ROOT" "./1MBfile.bin" 1
            create_file "$CLIENT_ROOT" "./10MBfile.bin" 10
            create_file "$CLIENT_ROOT" "./100MBfile.bin" 100

            create_folder "$CLIENT_ROOT" "./folder1"
            create_file "$CLIENT_ROOT" "./folder1/file4.txt" 1
            create_file "$CLIENT_ROOT" "./folder1/file5.txt" 1
            create_folder "$CLIENT_ROOT" "./folder2"
            ;;
        3)
            scenario_name="Initial sync: Nested files and folders on client and server."

            create_file "$SERVER_ROOT" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" 1
            # Create large file for testing chunk transfers
            create_file "$SERVER_ROOT" "./100MBfile.bin" 100
            create_folder "$SERVER_ROOT" "./folder1"
            create_folder "$SERVER_ROOT" "./folder1/subfolder1"
            create_file "$SERVER_ROOT" "./folder1/subfolder1/file4.txt" 1
            create_file "$SERVER_ROOT" "./folder1/subfolder1/file5.txt" 1
            create_folder "$SERVER_ROOT" "./folder2"
            create_folder "$SERVER_ROOT" "./folder2/subfolder2"
            create_folder "$SERVER_ROOT" "./folder2/subfolder2/subsubfolder2"
            create_file "$SERVER_ROOT" "./folder2/subfolder2/subsubfolder2/file6.txt" 1
            create_folder "$SERVER_ROOT" "./folder3"
            create_folder "$SERVER_ROOT" "./folder3/subfolder3"
            create_folder "$SERVER_ROOT" "./folder3/subfolder3/subsubfolder3"
            create_folder "$SERVER_ROOT" "./folder3/subfolder3/subsubfolder3/subsubsubfolder3"

            create_file "$CLIENT_ROOT" "./file7.txt" 1
            create_file "$CLIENT_ROOT" "./file8.txt" 1
            create_file "$CLIENT_ROOT" "./file9.txt" 1
            create_folder "$CLIENT_ROOT" "./folder4"
            create_folder "$CLIENT_ROOT" "./folder4/subfolder4"
            create_file "$CLIENT_ROOT" "./folder4/subfolder4/file10.txt" 1
            create_file "$CLIENT_ROOT" "./folder4/subfolder4/file11.txt" 1
            create_folder "$CLIENT_ROOT" "./folder5"
            create_file "$CLIENT_ROOT" "./folder5/100MBfile.bin" 100
            create_folder "$CLIENT_ROOT" "./folder5/subfolder5"
            create_folder "$CLIENT_ROOT" "./folder5/subfolder5/subsubfolder5"
            create_file "$CLIENT_ROOT" "./folder5/subfolder5/subsubfolder5/file12.txt" 1
            ;;
        4)
            scenario_name="Initial sync: 20ms latency, files on server only."
            apply_latency=20
            # File setup similar to Scenario 1

            create_file "$SERVER_ROOT" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" 10
            create_file "$SERVER_ROOT" "./100MBfile.bin" 100
            ;;
        5)
            scenario_name="Initial sync: 100ms latency, files on server only."
            apply_latency=100

            create_file "$SERVER_ROOT" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" 10
            create_file "$SERVER_ROOT" "./100MBfile.bin" 100
            # Add your scenario 5 setup here
            ;;
        6)
            scenario_name="Initial sync: 20ms latency, files on client only."
            apply_latency=20

            create_file "$CLIENT_ROOT" "./file1.txt" 1
            create_file "$CLIENT_ROOT" "./file2.txt" 1
            create_file "$CLIENT_ROOT" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$CLIENT_ROOT" "./1MBfile.bin" 1
            create_file "$CLIENT_ROOT" "./10MBfile.bin" 10
            create_file "$CLIENT_ROOT" "./100MBfile.bin" 100
            ;;
        7)
            scenario_name="Initial sync: 100ms latency, files on client only."
            apply_latency=100

            create_file "$CLIENT_ROOT" "./file1.txt" 1
            create_file "$CLIENT_ROOT" "./file2.txt" 1
            create_file "$CLIENT_ROOT" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$CLIENT_ROOT" "./1MBfile.bin" 1
            create_file "$CLIENT_ROOT" "./10MBfile.bin" 10
            create_file "$CLIENT_ROOT" "./100MBfile.bin" 100
            ;;
        8)
            scenario_name="File created on both sides with identical content."
            
            create_file "$SERVER_ROOT" "./file1.txt" 1
            cp "$SERVER_ROOT/file1.txt" "$CLIENT_ROOT/file1.txt"
            ;;
        9)
            scenario_name="File created on both sides with different content."
            create_file "$SERVER_ROOT" "./file1.txt" 1
            if [ "$VERBOSE" == "1" ]; then
                echo "SERVER FILE HASH IS $(hash_file "$SERVER_ROOT" "./file1.txt")" >> "$SCRIPT_DIR/test_report.txt"
            fi
            create_file "$CLIENT_ROOT" "./file1.txt" 2
            if [ "$VERBOSE" == "1" ]; then
                echo "CLIENT FILE HASH IS $(hash_file "$CLIENT_ROOT" "./file1.txt")" >> "$SCRIPT_DIR/test_report.txt"
            fi
            ;;
        10)
            scenario_name="Re-sync: Server moved files."

            create_file "$SERVER_ROOT" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" 1
            create_folder "$SERVER_ROOT" "./folder1"  # Folder1 will be moved to folder3
            create_file "$SERVER_ROOT" "./folder1/file4.txt" 1  # File4 will be moved to folder3
            create_file "$SERVER_ROOT" "./folder1/file5.txt" 1  # File5 will be moved to folder3
            create_folder "$SERVER_ROOT" "./folder2"
            # need to run a sync here to ensure the client has the initial files
            echo "Running initial sync to ensure client has the initial files."
            $SERVER_CMD_LINE &
            sleep 0.25  # Give the server time to start
            $CLIENT_CMD_LINE &
            wait
            echo "Moving folder1 to folder3 on server."
            #mv "$SERVER_ROOT/folder1" "$SERVER_ROOT/folder3"
            move_path "$SERVER_ROOT" "./folder1" "./folder3"
            ;;
        11)
            scenario_name="Re-sync: Client moved files."

            create_file "$CLIENT_ROOT" "./file1.txt" 1
            create_file "$CLIENT_ROOT" "./file2.txt" 1
            create_file "$CLIENT_ROOT" "./file3.txt" 1

            create_folder "$CLIENT_ROOT" "./folder1"  # Folder1 will be moved to folder3
            create_file "$CLIENT_ROOT" "./folder1/file4.txt" 1  # File4 will be moved to folder3
            create_file "$CLIENT_ROOT" "./folder1/file5.txt" 1  # File5 will be moved to folder3
            create_folder "$CLIENT_ROOT" "./folder2"

            # need to run a sync here to ensure the server has the initial files
            echo "Running initial sync to ensure server has the initial files."
            $SERVER_CMD_LINE &
            sleep 0.25  # Give the server time to start
            $CLIENT_CMD_LINE &
            wait
            echo "Moving folder1 to folder3 on client."
            move_path "$CLIENT_ROOT" "./folder1" "./folder3"
            ;;
        12)
            scenario_name="Re-sync: Server edited files."

            create_file "$SERVER_ROOT" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" 10
            create_file "$SERVER_ROOT" "./100MBfile.bin" 100

            create_folder "$SERVER_ROOT" "./folder1"
            create_file "$SERVER_ROOT" "./folder1/file4.txt" 2                       # File4 will be edited, store its hash later
            create_file "$SERVER_ROOT" "./folder1/file5.txt" 2                       # File5 will be edited, store its hash later
            create_folder "$SERVER_ROOT" "./folder2"

            # need to run a sync here to ensure the client has the initial files
            echo "Running initial sync to ensure client has the initial files."
            $SERVER_CMD_LINE &
            sleep 0.25  # Give the server time to start
            $CLIENT_CMD_LINE &
            wait
            echo "Editing file4 and file5 on server."
            edit_file "$SERVER_ROOT" "./folder1/file4.txt"
            edit_file "$SERVER_ROOT" "./folder1/file5.txt"
            ;;
        13)
            scenario_name="Re-sync: Client edited files."

            create_file "$SERVER_ROOT" "./file1.txt" 1
            create_file "$SERVER_ROOT" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" 10
            create_file "$SERVER_ROOT" "./100MBfile.bin" 100

            create_folder "$SERVER_ROOT" "./folder1"
            create_file "$SERVER_ROOT" "./folder1/file4.txt" 2                       # File4 will be edited, store its hash later
            create_file "$SERVER_ROOT" "./folder1/file5.txt" 2                       # File5 will be edited, store its hash later
            create_folder "$SERVER_ROOT" "./folder2"
            # need to run a sync here to ensure the server has the initial files
            echo "Running initial sync to ensure server has the initial files."
            $SERVER_CMD_LINE &
            sleep 0.25  # Give the server time to start
            $CLIENT_CMD_LINE &
            wait
            echo "Editing file4 and file5 on client."
            edit_file "$CLIENT_ROOT" "./folder1/file4.txt"
            edit_file "$CLIENT_ROOT" "./folder1/file5.txt"
            ;;
        14)
            scenario_name="Re-sync: Server deleted files."

            create_file "$SERVER_ROOT" "./file1.txt" 1                               # File1 will be deleted
            create_file "$SERVER_ROOT" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" 10                           # 10MBfile will be deleted
            create_file "$SERVER_ROOT" "./100MBfile.bin" 100

            # need to run a sync here to ensure the client has the initial files
            echo "Running initial sync to ensure client has the initial files."
            $SERVER_CMD_LINE &
            sleep 0.25  # Give the server time to start
            $CLIENT_CMD_LINE &
            wait
            echo "Deleting file1 and 10MBfile on server."
            remove_path "$SERVER_ROOT" "./file1.txt"
            remove_path "$SERVER_ROOT" "./10MBfile.bin"
            ;;
        15)
            scenario_name="Re-sync: Client deleted files."

            create_file "$SERVER_ROOT" "./file1.txt" 1                               # File1 will be deleted
            create_file "$SERVER_ROOT" "./file2.txt" 1
            create_file "$SERVER_ROOT" "./file3.txt" 1
            # Create large files for testing chunk transfers
            create_file "$SERVER_ROOT" "./1MBfile.bin" 1
            create_file "$SERVER_ROOT" "./10MBfile.bin" 10                           # 10MBfile will be deleted
            create_file "$SERVER_ROOT" "./100MBfile.bin" 100

            # need to run a sync here to ensure the server has the initial files
            echo "Running initial sync to ensure server has the initial files."
            $SERVER_CMD_LINE &
            sleep 0.25  # Give the server time to start
            $CLIENT_CMD_LINE &
            wait
            echo "Deleting file1 and 10MBfile on client."
            remove_path "$CLIENT_ROOT" "./file1.txt"
            remove_path "$CLIENT_ROOT" "./10MBfile.bin"
            ;;
        16) 
            scenario_name="File deleted on both sides."

            create_file "$SERVER_ROOT" "./file1.txt" 1

            echo "Running initial sync to ensure client has the initial files."
            $SERVER_CMD_LINE &
            sleep 0.25  # Give the server time to start
            $CLIENT_CMD_LINE &
            wait

            echo "Deleting file1 on both sides."
            remove_path "$SERVER_ROOT" "./file1.txt"
            remove_path "$CLIENT_ROOT" "./file1.txt"
            ;;
        17)

            scenario_name="File modified differently on both sides."

            create_file "$SERVER_ROOT" "./file1.txt" 1

            echo "Running initial sync to ensure client has the initial files."
            $SERVER_CMD_LINE &
            sleep 0.25  # Give the server time to start
            $CLIENT_CMD_LINE &
            wait

            echo "Modifying file1 on both sides."
            edit_file "$SERVER_ROOT" "./file1.txt"
            edit_file "$CLIENT_ROOT" "./file1.txt"

            if [ "$VERBOSE" == "1" ]; then
                echo "SERVER FILE HASH IS $(hash_file "$SERVER_ROOT" "./file1.txt")" >> "$SCRIPT_DIR/test_report.txt"
            fi
            if [ "$VERBOSE" == "1" ]; then
                echo "CLIENT FILE HASH IS $(hash_file "$CLIENT_ROOT" "./file1.txt")" >> "$SCRIPT_DIR/test_report.txt"
            fi
            ;;
        18)
            scenario_name="File moved to another folder on server"

            create_file "$SERVER_ROOT" "./file1.txt" 1
            create_folder "$SERVER_ROOT" "./folder1"

            echo "Running initial sync to ensure client has the initial files."
            $SERVER_CMD_LINE &
            sleep 0.25  # Give the server time to start
            $CLIENT_CMD_LINE &
            wait

            echo "Moving file1 to another folder on server."
            move_path "$SERVER_ROOT" "./file1.txt" "./folder1/file1.txt"
            ;;

        19)
            scenario_name="File moved to another folder on client"

            create_file "$CLIENT_ROOT" "./file1.txt" 1
            create_folder "$CLIENT_ROOT" "./folder1"

            echo "Running initial sync to ensure server has the initial files."
            $SERVER_CMD_LINE &
            sleep 0.25  # Give the server time to start
            $CLIENT_CMD_LINE &
            wait

            echo "Moving file1 to another folder on client."
            move_path "$CLIENT_ROOT" "./file1.txt" "./folder1/file1.txt"
            ;;

        20)
            scenario_name="File moved and modified simultaneously on one side"
            create_file "$SERVER_ROOT" "./file1.txt" 1
            create_folder "$SERVER_ROOT" "./folder1"

            echo "Running initial sync to ensure client has the initial files."
            $SERVER_CMD_LINE &
            sleep 0.25  # Give the server time to start
            $CLIENT_CMD_LINE &
            wait

            echo "Moving file1 to another folder on server."
            move_path "$SERVER_ROOT" "./file1.txt" "./folder1/file1.txt"
            echo "Modifying file1 on server."
            edit_file "$SERVER_ROOT" "./folder1/file1.txt"
            ;;
        21)
            scenario_name="File renamed on both sides but with different names"
            create_file "$SERVER_ROOT" "./file1.txt" 1

            echo "Running initial sync to ensure client has the initial files."
            $SERVER_CMD_LINE &
            sleep 0.25  # Give the server time to start
            $CLIENT_CMD_LINE &
            wait

            echo "Renaming file1 on server."
            move_path "$SERVER_ROOT" "./file1.txt" "./file1_server_renamed.txt"
            echo "Renaming file1 on client."
            move_path "$CLIENT_ROOT" "./file1.txt" "./file1_client_renamed.txt"
            ;;
        99)
        #Too large for now
            scenario_name="Large and complex file system"
            echo "Building large file system on both client and server..."

            # Create deeply nested folders and files on server (at least 10 levels, overall 1000+ items)
            for toplevel in $(seq 1 10); do
                server_folder="./folder${toplevel}"
                create_folder "$SERVER_ROOT" "$server_folder"
                for sublevel in $(seq 1 10); do
                    server_subfolder="./folder${toplevel}/sub${sublevel}"
                    create_folder "$SERVER_ROOT" "$server_subfolder"
                    for f in $(seq 1 10); do
                        create_file "$SERVER_ROOT" "$server_subfolder/file_${f}.bin" 1
                    done
                    # Nest deeper for the next iteration
                    SERVER_ROOT="$SERVER_ROOT/$server_subfolder"
                    create_folder "${SERVER_ROOT}/folder${toplevel}"
                done
                # Reset SERVER_ROOT to base for the next level
                SERVER_ROOT="$(canonical "$TEST_FOLDER/server")"
            done

            # Create deeply nested folders and files on client (same approach)
            for toplevel in $(seq 1 10); do
                client_folder="./folder${toplevel}"
                create_folder "$CLIENT_ROOT" "$client_folder"
                for sublevel in $(seq 1 10); do
                    client_subfolder="./folder${toplevel}/sub${sublevel}"
                    create_folder "$CLIENT_ROOT" "$client_subfolder"
                    for f in $(seq 1 10); do
                        create_file "$CLIENT_ROOT" "$client_subfolder/file_${f}.bin" 1
                    done
                    CLIENT_ROOT="$CLIENT_ROOT/$client_subfolder"
                    create_folder "${CLIENT_ROOT}/folder${toplevel}"
                done
                CLIENT_ROOT="$(canonical "$TEST_FOLDER/client")"
            done

            # Initial sync so both sides have matching large trees
            echo "Running initial sync for scenario 99..."
            $SERVER_CMD_LINE &
            sleep 0.25
            $CLIENT_CMD_LINE &
            wait

            echo "performing a shit bunch of actions on the files"
            
            echo "1: Client and server moved the same folder, but to different locations,"
            move_path "$SERVER_ROOT" "./folder1/sub1" "./folder1/sub1_server_renamed"
            move_path "$CLIENT_ROOT" "./folder1/sub1" "./folder1/sub1_client_renamed"

            echo "2: Client and server edited the same file"
            edit_file "$SERVER_ROOT" "./folder1/sub2/file_1.bin"
            edit_file "$CLIENT_ROOT" "./folder1/sub2/file_1.bin"

            echo "3: Client and server removed the same file"
            remove_path "$SERVER_ROOT" "./folder2/sub1"
            remove_path "$CLIENT_ROOT" "./folder2/sub1"

            echo "4: Client and server added a new file of different name"
            create_file "$SERVER_ROOT" "./new_server_file1.bin" 1
            create_file "$CLIENT_ROOT" "./new_client_file1.bin" 1

            echo "5: Client and server added different files of same name"
            create_file "$SERVER_ROOT" "./level1_sub2/file_1.bin_added" 1
            create_file "$CLIENT_ROOT" "./level1_sub2/file_1.bin_added" 1

            ;;
        16)
            scenario_name="Stress test: deeply nested 1000+ files with conflicts"

            # Build a 3-level nested tree (10x10x10 = 1000 files) on server
            for i in {1..10}; do
                for j in {1..10}; do
                    # Create a folder for each level
                    folder="nested/level${i}/sub${j}"
                    create_folder "$SERVER_ROOT" "./$folder" "./$folder"

                    for k in {1..10}; do
                        folder="nested/level${i}/sub${j}/subsub${k}"
                        if [ $folder == "nested/level1/sub1/subsub1" ]; then
                            # Create a folder that will be deleted later
                            create_folder "$SERVER_ROOT" "./$folder" ""
                            create_file "$SERVER_ROOT" "./$folder/file${k}.txt" "" 0.01
                        else
                            create_folder "$SERVER_ROOT" "./$folder" "./$folder"
                            create_file "$SERVER_ROOT" "./$folder/file${k}.txt" "$folder/file${k}.txt" 0.01
                        fi
                    done
                done
            done

            # Build overlapping subset on client (to create initial differences)
            for i in {5..15}; do
                for j in {5..15}; do
                    for k in {5..15}; do
                        folder="nested/level${i}/sub${j}/subsub${k}"
                        create_folder "$CLIENT_ROOT" "./$folder" "$folder"  
                        create_file "$CLIENT_ROOT" "./$folder/file${k}.txt" "$folder/file${k}.txt" 0.01
                    done
                done
            done

            # Run initial sync
            echo "Running initial sync for stress-test scenario"
            $SERVER_CMD_LINE &
            sleep 1
            $CLIENT_CMD_LINE &
            wait

            # Server modifications: delete some files and move a subtree
            echo "Server: deleting subtree level1/sub1/subsub1 and file edits"
            rm -r "$SERVER_ROOT/nested/level1/sub1/subsub1"
            echo "server edit" >"$SERVER_ROOT/nested/level2/sub2/subsub2/file2.txt"
            move_path $SERVER_ROOT "./nested/level3/sub3" "./nested/moved3"

            # Client modifications: add new files, copy, conflict edits
            echo "Client: creating new branch and editing conflicts"
            folder_new="nested/newbranch/subA"
            create_folder "$CLIENT_ROOT" "./$folder_new" "$folder_new"
            create_file "$CLIENT_ROOT" "./$folder_new/new1.txt" "$folder_new/new1.txt" 0
            cp "$CLIENT_ROOT/nested/level4/sub4/subsub4/file4.txt" "$CLIENT_ROOT/nested/level4/sub4/subsub4/file4_copy.txt"
            echo "client conflict" >"$CLIENT_ROOT/nested/level2/sub2/subsub2/file2.txt"

            ;;
        *)
            echo "Invalid scenario number. Please run the script again and select a valid scenario."
            exit 1
            ;;
    esac

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






