#!/bin/bash
# Scenario setup functions for multi-pc-sync testing

# per-scenario name helpers:
set_scenario_01_name() { scenario_name="Initial sync: Client files empty, Server files populated"; }
set_scenario_02_name() { scenario_name="Initial sync: Server files are empty, Client files are populated."; }
set_scenario_03_name() { scenario_name="Initial sync: Nested files and folders on client and server."; }
set_scenario_04_name() { scenario_name="Initial sync: 20ms latency, files on server only."; }
set_scenario_05_name() { scenario_name="Initial sync: 250ms latency, files on server only."; }
set_scenario_06_name() { scenario_name="Initial sync: 20ms latency, files on client only."; }
set_scenario_07_name() { scenario_name="Initial sync: 250ms latency, files on client only."; }
set_scenario_08_name() { scenario_name="File created on both sides with identical content."; }
set_scenario_09_name() { scenario_name="File created on both sides with different content."; }
set_scenario_10_name() { scenario_name="Re-sync: Server moved files."; }
set_scenario_11_name() { scenario_name="Re-sync: Client moved files."; }
set_scenario_12_name() { scenario_name="Re-sync: Server edited files."; }
set_scenario_13_name() { scenario_name="Re-sync: Client edited files."; }
set_scenario_14_name() { scenario_name="Re-sync: Server deleted files."; }
set_scenario_15_name() { scenario_name="Re-sync: Client deleted files."; }
set_scenario_16_name() { scenario_name="Re-sync: File deleted on both sides."; }
set_scenario_17_name() { scenario_name="Re-sync: File modified differently on both sides."; }
set_scenario_18_name() { scenario_name="Re-sync: File moved to another folder on server"; }
set_scenario_19_name() { scenario_name="Re-sync: File moved to another folder on client"; }
set_scenario_20_name() { scenario_name="Re-sync: File moved and modified simultaneously on one side"; }
set_scenario_21_name() { scenario_name="Re-sync: File renamed on both sides but with different names"; }
set_scenario_22_name() { scenario_name="Re-sync: Multiple operations on same file - modify then rename"; }
set_scenario_23_name() { scenario_name="Re-sync: Operations on files within renamed directories"; }
set_scenario_24_name() { scenario_name="Re-sync: Operations on files within moved directories"; }
set_scenario_25_name() { scenario_name="Re-sync: Circular rename (A→B, B→A across sides)"; }
set_scenario_26_name() { scenario_name="Re-sync: File deleted on the server, modified on the client"; }
set_scenario_27_name() { scenario_name="Re-sync: File deleted on the client, modified on the server"; }
set_scenario_28_name() { scenario_name="Re-sync: File moved on the server, renamed on the client"; }
set_scenario_29_name() { scenario_name="Re-sync: File moved on the client, renamed on the server"; }
set_scenario_30_name() { scenario_name="Re-sync: Filename case changes"; }
set_scenario_31_name() { scenario_name="Very large file (10GB)"; }
set_scenario_32_name() { scenario_name="File with 0 bytes"; }
set_scenario_33_name() { scenario_name="File with special characters in the name"; }
set_scenario_34_name() { scenario_name="Long path names"; }
set_scenario_35_name() { scenario_name="Long file names"; }

# This scenario is for building a large and complex file system for testing, not yet supported
set_scenario_99_name() { scenario_name="Large and complex file system"; }


scenario_01() {
    set_scenario_01_name # Initial sync: Client files empty, Server files populated
    create_file "$SERVER_ROOT" "./file1.txt" 1
    create_file "$SERVER_ROOT" "./file2.txt" 1
    create_file "$SERVER_ROOT" "./file3.txt" 1
    create_file "$SERVER_ROOT" "./1MBfile.bin" 1
    create_file "$SERVER_ROOT" "./10MBfile.bin" 10
    create_file "$SERVER_ROOT" "./100MBfile.bin" 100
    create_folder "$SERVER_ROOT" "./folder1"
    create_file "$SERVER_ROOT" "./folder1/file4.txt" 1
    create_file "$SERVER_ROOT" "./folder1/file5.txt" 1
    create_folder "$SERVER_ROOT" "./folder2"
}

scenario_02() {
    set_scenario_02_name # Initial sync: Server files are empty, Client files are populated.
    create_file "$CLIENT_ROOT" "./file1.txt" 1
    create_file "$CLIENT_ROOT" "./file2.txt" 1
    create_file "$CLIENT_ROOT" "./file3.txt" 1
    create_file "$CLIENT_ROOT" "./1MBfile.bin" 1
    create_file "$CLIENT_ROOT" "./10MBfile.bin" 10
    create_file "$CLIENT_ROOT" "./100MBfile.bin" 100
    create_folder "$CLIENT_ROOT" "./folder1"
    create_file "$CLIENT_ROOT" "./folder1/file4.txt" 1
    create_file "$CLIENT_ROOT" "./folder1/file5.txt" 1
    create_folder "$CLIENT_ROOT" "./folder2"
}


scenario_03() {
    set_scenario_03_name # Initial sync: Nested files and folders on client and server.
    create_file "$SERVER_ROOT" "./file1.txt" 1
    create_file "$SERVER_ROOT" "./file2.txt" 1
    create_file "$SERVER_ROOT" "./file3.txt" 1
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
}

scenario_04() {
    set_scenario_04_name # Initial sync: 20ms latency, files on server only.
    apply_latency=20
    # File setup similar to Scenario 1

    create_file "$SERVER_ROOT" "./file1.txt" 1
    create_file "$SERVER_ROOT" "./file2.txt" 1
    create_file "$SERVER_ROOT" "./file3.txt" 1
    # Create large files for testing chunk transfers
    create_file "$SERVER_ROOT" "./1MBfile.bin" 1
    create_file "$SERVER_ROOT" "./10MBfile.bin" 10
    create_file "$SERVER_ROOT" "./100MBfile.bin" 100
}

scenario_05() {
    set_scenario_05_name # Initial sync: 250ms latency, files on server only.
    apply_latency=250
    # File setup similar to Scenario 1

    create_file "$SERVER_ROOT" "./file1.txt" 1
    create_file "$SERVER_ROOT" "./file2.txt" 1
    create_file "$SERVER_ROOT" "./file3.txt" 1
    # Create large files for testing chunk transfers
    create_file "$SERVER_ROOT" "./1MBfile.bin" 1
    create_file "$SERVER_ROOT" "./10MBfile.bin" 10
    create_file "$SERVER_ROOT" "./100MBfile.bin" 100
}

scenario_06() {
    set_scenario_06_name # Initial sync: 20ms latency, files on client only.
    apply_latency=20
    # File setup similar to Scenario 2

    create_file "$CLIENT_ROOT" "./file1.txt" 1
    create_file "$CLIENT_ROOT" "./file2.txt" 1
    create_file "$CLIENT_ROOT" "./file3.txt" 1
    # Create large files for testing chunk transfers
    create_file "$CLIENT_ROOT" "./1MBfile.bin" 1
    create_file "$CLIENT_ROOT" "./10MBfile.bin" 10
    create_file "$CLIENT_ROOT" "./100MBfile.bin" 100
}

scenario_07() {
    set_scenario_07_name # Initial sync: 250ms latency, files on client only.
    apply_latency=250
    # File setup similar to Scenario 2

    create_file "$CLIENT_ROOT" "./file1.txt" 1
    create_file "$CLIENT_ROOT" "./file2.txt" 1
    create_file "$CLIENT_ROOT" "./file3.txt" 1
    # Create large files for testing chunk transfers
    create_file "$CLIENT_ROOT" "./1MBfile.bin" 1
    create_file "$CLIENT_ROOT" "./10MBfile.bin" 10
    create_file "$CLIENT_ROOT" "./100MBfile.bin" 100
}

scenario_08() {
    set_scenario_08_name # File created on both sides with identical content.
            
    create_file "$SERVER_ROOT" "./file1.txt" 1
    cp "$SERVER_ROOT/file1.txt" "$CLIENT_ROOT/file1.txt"
}

scenario_09() {
    set_scenario_09_name # File created on both sides with different content.
    create_file "$SERVER_ROOT" "./file1.txt" 1
    if [ "$VERBOSE" == "1" ]; then
        echo "SERVER FILE HASH IS $(hash_file "$SERVER_ROOT" "./file1.txt")" >> "$SCRIPT_DIR/test_report.txt"
    fi
    create_file "$CLIENT_ROOT" "./file1.txt" 2
    if [ "$VERBOSE" == "1" ]; then
        echo "CLIENT FILE HASH IS $(hash_file "$CLIENT_ROOT" "./file1.txt")" >> "$SCRIPT_DIR/test_report.txt"
    fi

    # Scenario 9 creates a conflict, both versions of file1.txt need to be kept
    EXPECTED_FILES=$(add_item_to_list "$EXPECTED_FILES" "./file1.txt.server")
    EXPECTED_FILES=$(add_item_to_list "$EXPECTED_FILES" "./file1.txt.client")
}

scenario_10() {
    set_scenario_10_name # Re-sync: Server moved files.

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
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Moving folder1 to folder3 on server."
    #mv "$SERVER_ROOT/folder1" "$SERVER_ROOT/folder3"
    move_path "$SERVER_ROOT" "./folder1" "./folder3"
    
}

scenario_11() {
    set_scenario_11_name # Re-sync: Client moved files.

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
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Moving folder1 to folder3 on client."
    move_path "$CLIENT_ROOT" "./folder1" "./folder3"
}

scenario_12() {
    set_scenario_12_name # Re-sync: Server edited files.

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
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Editing file4 and file5 on server."
    edit_file "$SERVER_ROOT" "./folder1/file4.txt"
    edit_file "$SERVER_ROOT" "./folder1/file5.txt"
}

scenario_13() {
    set_scenario_13_name # Re-sync: Client edited files.

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
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Editing file4 and file5 on client."
    edit_file "$CLIENT_ROOT" "./folder1/file4.txt"
    edit_file "$CLIENT_ROOT" "./folder1/file5.txt"
}

scenario_14() {
    set_scenario_14_name # Re-sync: Server deleted files.
    create_file "$SERVER_ROOT" "./file1.txt" 1
    create_file "$SERVER_ROOT" "./file2.txt" 1
    create_file "$SERVER_ROOT" "./file3.txt" 1
    create_file "$SERVER_ROOT" "./1MBfile.bin" 1
    create_file "$SERVER_ROOT" "./10MBfile.bin" 10
    create_file "$SERVER_ROOT" "./100MBfile.bin" 100
    echo "Running initial sync to ensure client has the initial files."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Deleting file1 and 10MBfile on server."
    remove_path "$SERVER_ROOT" "./file1.txt"
    remove_path "$SERVER_ROOT" "./10MBfile.bin"
}

scenario_15() {
    set_scenario_15_name # Re-sync: Client deleted files.
    create_file "$SERVER_ROOT" "./file1.txt" 1
    create_file "$SERVER_ROOT" "./file2.txt" 1
    create_file "$SERVER_ROOT" "./file3.txt" 1
    create_file "$SERVER_ROOT" "./1MBfile.bin" 1
    create_file "$SERVER_ROOT" "./10MBfile.bin" 10
    create_file "$SERVER_ROOT" "./100MBfile.bin" 100
    echo "Running initial sync to ensure server has the initial files."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Deleting file1 and 10MBfile on client."
    remove_path "$CLIENT_ROOT" "./file1.txt"
    remove_path "$CLIENT_ROOT" "./10MBfile.bin"
}

scenario_16() {
    set_scenario_16_name # Re-sync: File deleted on both sides.
    create_file "$SERVER_ROOT" "./file1.txt" 1
    echo "Running initial sync to ensure client has the initial files."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Deleting file1 on both sides."
    remove_path "$SERVER_ROOT" "./file1.txt"
    remove_path "$CLIENT_ROOT" "./file1.txt"
}

scenario_17() {
    set_scenario_17_name # Re-sync: File modified differently on both sides.
    create_file "$SERVER_ROOT" "./file1.txt" 1
    echo "Running initial sync to ensure client has the initial files."
    $SERVER_CMD_LINE &
    wait_for_server_start
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
    # Scenario 17 creates a conflict, both versions of file1.txt need to be kept
    EXPECTED_FILES=$(add_item_to_list "$EXPECTED_FILES" "./file1.txt.server")
    EXPECTED_FILES=$(add_item_to_list "$EXPECTED_FILES" "./file1.txt.client")
}

scenario_18() {
    set_scenario_18_name # Re-sync: File moved to another folder on server.
    create_file "$SERVER_ROOT" "./file1.txt" 1
    create_folder "$SERVER_ROOT" "./folder1"
    echo "Running initial sync to ensure client has the initial files."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Moving file1 to another folder on server."
    move_path "$SERVER_ROOT" "./file1.txt" "./folder1/file1.txt"
}

scenario_19() {
    set_scenario_19_name # Re-sync: File moved to another folder on client.
    create_file "$CLIENT_ROOT" "./file1.txt" 1
    create_folder "$CLIENT_ROOT" "./folder1"
    echo "Running initial sync to ensure server has the initial files."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Moving file1 to another folder on client."
    move_path "$CLIENT_ROOT" "./file1.txt" "./folder1/file1.txt"
}

scenario_20() {
    set_scenario_20_name # Re-sync: File moved and modified simultaneously on one side.
    create_file "$SERVER_ROOT" "./file1.txt" 1
    create_folder "$SERVER_ROOT" "./folder1"
    echo "Running initial sync to ensure client has the initial files."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Moving file1 to another folder on server."
    move_path "$SERVER_ROOT" "./file1.txt" "./folder1/file1.txt"
    echo "Modifying file1 on server."
    edit_file "$SERVER_ROOT" "./folder1/file1.txt"
}

scenario_21() {
    set_scenario_21_name # Re-sync: File renamed on both sides but with different names.
    create_file "$SERVER_ROOT" "./file1.txt" 1
    echo "Running initial sync to ensure client has the initial files."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Renaming file1 on server."
    move_path "$SERVER_ROOT" "./file1.txt" "./file1_server_renamed.txt"
    echo "Renaming file1 on client."
    move_path "$CLIENT_ROOT" "./file1.txt" "./file1_client_renamed.txt"
}

scenario_22() {
    set_scenario_22_name # Re-sync: Multiple operations on same file - modify then rename.
    create_file "$SERVER_ROOT" "./file1.txt" 1
    echo "Running initial sync to ensure client has the initial files."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Editing file1 on server."
    edit_file "$SERVER_ROOT" "./file1.txt"
    echo "Renaming file1 to file1_renamed.txt on server."
    move_path "$SERVER_ROOT" "./file1.txt" "./file1_renamed.txt"
}

scenario_23() {
    set_scenario_23_name # Re-sync: Operations on files within renamed directories.
    create_folder "$SERVER_ROOT" "./dirA"
    create_file "$SERVER_ROOT" "./dirA/file1.txt" 1
    create_file "$SERVER_ROOT" "./dirA/file2.txt" 1
    create_file "$SERVER_ROOT" "./dirA/file3.txt" 1
    echo "Running initial sync to ensure client has the initial files."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Renaming dirA to dirB on server."
    move_path "$SERVER_ROOT" "./dirA" "./dirB"
    echo "Renaming file1.txt to file1_renamed.txt inside dirB on server."
    move_path "$SERVER_ROOT" "./dirB/file1.txt" "./dirB/file1_renamed.txt"
    echo "Editing file2.txt inside dirB on server."
    edit_file "$SERVER_ROOT" "./dirB/file2.txt"
    echo "Deleting file3.txt inside dirB on server."
    remove_path "$SERVER_ROOT" "./dirB/file3.txt"
}

scenario_24() {
    set_scenario_24_name # Re-sync: Operations on files within moved directories.
    create_folder "$SERVER_ROOT" "./parentA"
    create_folder "$SERVER_ROOT" "./parentA/childA"
    create_file "$SERVER_ROOT" "./parentA/childA/file1.txt" 1
    create_file "$SERVER_ROOT" "./parentA/childA/file2.txt" 1
    create_file "$SERVER_ROOT" "./parentA/childA/file3.txt" 1
    echo "Running initial sync to ensure client has the initial files."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Moving childA from parentA to parentB on server."
    create_folder "$SERVER_ROOT" "./parentB"
    move_path "$SERVER_ROOT" "./parentA/childA" "./parentB/childA"
    echo "Renaming file1.txt to file1_renamed.txt inside parentB/childA on server."
    move_path "$SERVER_ROOT" "./parentB/childA/file1.txt" "./parentB/childA/file1_renamed.txt"
    echo "Editing file2.txt inside parentB/childA on server."
    edit_file "$SERVER_ROOT" "./parentB/childA/file2.txt"
    echo "Deleting file3.txt inside parentB/childA on server."
    remove_path "$SERVER_ROOT" "./parentB/childA/file3.txt"
}

scenario_25() {
    set_scenario_25_name # Re-sync: Circular rename (A→B, B→A across sides).
    create_file "$SERVER_ROOT" "./fileA.txt" 1
    create_file "$SERVER_ROOT" "./fileB.txt" 1
    echo "Running initial sync to ensure both sides have fileA.txt and fileB.txt."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait

    if [ "$VERBOSE" == "1" ]; then
        echo "FileA FILE HASH IS $(hash_file "$SERVER_ROOT" "./fileA.txt")" >> "$SCRIPT_DIR/test_report.txt"
    fi
    if [ "$VERBOSE" == "1" ]; then
        echo "FileB FILE HASH IS $(hash_file "$CLIENT_ROOT" "./fileB.txt")" >> "$SCRIPT_DIR/test_report.txt"
    fi

    echo "Renaming fileA.txt to fileB.txt on server (should overwrite/replace)."

    mv "$SERVER_ROOT/fileA.txt" "$SERVER_ROOT/fileB.txt"
    mv "$CLIENT_ROOT/fileB.txt" "$CLIENT_ROOT/fileA.txt"
}

scenario_26() {
    set_scenario_26_name # Re-sync: File deleted on the server, modified on the client.
    create_file "$SERVER_ROOT" "./file26.txt" 1
    echo "Running initial sync..."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Deleting file26 on server, modifying on client..."
    rm "${SERVER_ROOT}/file26.txt"
    edit_file "$CLIENT_ROOT" "./file26.txt"
}

scenario_27() {
    set_scenario_27_name # Re-sync: File deleted on the client, modified on the server.
    create_file "$CLIENT_ROOT" "./file27.txt" 1
    echo "Running initial sync..."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Deleting file27 on client, modifying on server..."
    rm "${CLIENT_ROOT}/file27.txt"
    edit_file "$SERVER_ROOT" "./file27.txt"
}

scenario_28() {
    set_scenario_28_name # Re-sync: File moved on the server, renamed on the client.
    create_file "$SERVER_ROOT" "./file28.txt" 1
    echo "Running initial sync..."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Moving file28 on server, renaming on client..."
    create_folder "$SERVER_ROOT" "./folder_move"
    move_path "$SERVER_ROOT" "./file28.txt" "./folder_move/file28.txt"
    move_path "$CLIENT_ROOT" "./file28.txt" "./file28_renamed.txt"
    EXPECTED_FILES="$EXPECTED_FILES ./file28_renamed.txt"
}

scenario_29() {
    set_scenario_29_name # Re-sync: File moved on the client, renamed on the server.
    create_file "$CLIENT_ROOT" "./file29.txt" 1
    echo "Running initial sync..."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Moving file29 on client, renaming on server..."
    create_folder "$CLIENT_ROOT" "./folder_move"
    move_path "$CLIENT_ROOT" "./file29.txt" "./folder_move/file29.txt"
    move_path "$SERVER_ROOT" "./file29.txt" "./file29_renamed.txt"
    EXPECTED_FILES="$EXPECTED_FILES ./file29_renamed.txt"
}

scenario_30() {
    set_scenario_30_name # Re-sync: Filename case changes.
    create_file "$SERVER_ROOT" "./CaseTest.txt" 1
    echo "Running initial sync..."
    $SERVER_CMD_LINE &
    wait_for_server_start
    $CLIENT_CMD_LINE &
    wait
    echo "Renaming CaseTest.txt to casetest.txt on server."
    move_path "$SERVER_ROOT" "./CaseTest.txt" "./casetest.txt"
}

scenario_31() {
    set_scenario_31_name # Very large file (10GB)
    echo "Creating a 10GB file on server..."
    create_file "$SERVER_ROOT" "./hugefile_10GB.bin" 10240
    echo "Running sync for very large file..."
}

scenario_32() {
    set_scenario_32_name # File with 0 bytes
    echo "Creating a 0-byte file on server..."
    create_file "$SERVER_ROOT" "./emptyfile.txt" 0
    echo "Running sync for 0-byte file..."
}

# Helper function to create a file with a path of exact specified length using nested folders
create_long_path() {
    local base_path="$1"  # SERVER_ROOT or CLIENT_ROOT
    local target_length="$2"

    if [ -z "$target_length" ] || [ -z "$base_path" ]; then
        echo "Error: create_long_path requires base_path and target_length arguments"
        return 1
    fi
    
    local root_path_length=${#base_path}
    local filename="file.txt"
    local filename_length=${#filename}
    
    # We need to create: ${base_path}/target_length/folder2/.../foldern/file.txt = target_length
    # Available length for folder structure: target_length - root_path_length - 1 (/) - filename_length
    local available_length=$((target_length - root_path_length - 1 - filename_length))
    
    if [ $available_length -le 0 ]; then
        echo "Error: Target path length ($target_length) is too short for base path ($root_path_length chars) and filename '$filename'"
        return 1
    fi
    
    # First folder is named with the target length for easy analysis
    local first_folder="$target_length"
    local first_folder_length=${#first_folder}
    
    # Build nested folder structure starting with the target length folder
    local folder_path="/${first_folder}"
    local remaining_length=$((available_length - first_folder_length - 1))  # -1 for the slash
    local folder_num=2
    
    while [ $remaining_length -gt 0 ]; do
        # Use longer folder names (up to 100 chars) to reduce nesting levels
        local max_folder_length=100
        local base_name="folder_${folder_num}_"
        local base_length=${#base_name}
        
        # Calculate how much padding we can add (up to max_folder_length total)
        local max_padding=$((max_folder_length - base_length))
        local available_for_folder=$((remaining_length - 1))  # -1 for the slash
        
        if [ $available_for_folder -le 0 ]; then
            break
        fi
        
        # Use the smaller of: available space or max folder length
        local folder_content_length=$available_for_folder
        if [ $folder_content_length -gt $max_folder_length ]; then
            folder_content_length=$max_folder_length
        fi
        
        # Calculate padding needed
        local padding_length=$((folder_content_length - base_length))
        if [ $padding_length -lt 0 ]; then
            padding_length=0
        fi
        
        # Generate folder name with padding
        local padding=""
        if [ $padding_length -gt 0 ]; then
            padding=$(printf "%.${padding_length}s" "$(yes 'x' | head -n ${padding_length} | tr -d '\n')")
        fi
        
        local folder_name="${base_name}${padding}"
        local actual_folder_length=${#folder_name}
        
        folder_path="${folder_path}/${folder_name}"
        remaining_length=$((remaining_length - actual_folder_length - 1))  # -1 for the slash
        folder_num=$((folder_num + 1))
        
        # Safety check to prevent infinite loop
        if [ $actual_folder_length -eq 0 ]; then
            break
        fi
    done
    
    # Calculate actual final path length for verification
    local final_path="${base_path}${folder_path}/${filename}"
    local actual_length=${#final_path}
    
    if [ $actual_length -ne $target_length ]; then
        echo "Warning: Actual length ($actual_length) differs from target ($target_length)"
    fi
    
    # Create the nested folder structure step by step to properly track in EXPECTED_FILES
    local current_path=""
    IFS='/' read -ra folders <<< "${folder_path#/}"  # Remove leading slash and split
    for folder in "${folders[@]}"; do
        current_path="${current_path}/${folder}"
        local rel_current_path=".${current_path}"
        create_folder "$base_path" "$rel_current_path"
    done
    
    # Create the file
    local rel_folder_path=".${folder_path}"
    create_file "$base_path" "${rel_folder_path}/${filename}" 1
    
    return 0
}

# Helper function to create a file with a filename of exact specified length
create_long_filename() {
    local base_path="$1"  # SERVER_ROOT or CLIENT_ROOT
    local target_length="$2"

    if [ -z "$target_length" ] || [ -z "$base_path" ]; then
        echo "Error: create_long_filename requires base_path and target_length arguments"
        return 1
    fi
    
    # Create filename: ${target_length}_<padding>.txt
    local prefix="${target_length}_"
    local suffix=".txt"
    local prefix_length=${#prefix}
    local suffix_length=${#suffix}
    
    # Calculate padding needed
    local padding_length=$((target_length - prefix_length - suffix_length))
    
    if [ $padding_length -lt 0 ]; then
        echo "Error: Target filename length ($target_length) is too short for prefix '$prefix' and suffix '$suffix'"
        return 1
    fi
    
    # Generate padding
    local padding=""
    if [ $padding_length -gt 0 ]; then
        padding=$(printf "%.${padding_length}s" "$(yes 'a' | head -n ${padding_length} | tr -d '\n')")
    fi
    
    local filename="${prefix}${padding}${suffix}"
    local actual_length=${#filename}
    
    if [ $actual_length -ne $target_length ]; then
        echo "Warning: Actual filename length ($actual_length) differs from target ($target_length)"
    fi
    
    # Create the file in the root directory
    create_file "$base_path" "./${filename}" 1
    
    return 0
}

scenario_33() {
    set_scenario_33_name # File with special characters in the name
    echo "Creating a file with special characters in the name on server..."
    create_file "$SERVER_ROOT" "./file_with_special_#@!\"%❤.txt" 1
    echo "Running sync for file with special characters in the name..."
}

scenario_34() {
    set_scenario_34_name # Long path names
    echo "Creating files with long path names on server..."

    local lengths=(127 128 255 256 511 512 1023 1024 2047 2048 4095)

    for length in "${lengths[@]}"; do
        echo "Creating path with length $length"
        create_long_path "$SERVER_ROOT" "$length"
    done
    
    echo "Running sync for long path names..."
}

scenario_35() {
    set_scenario_35_name # Long file names
    echo "Creating files with long file names on server..."

    # Test common filename length limits and powers of 2
    # 255 is the typical filesystem filename limit
    # Also test some powers of 2 around that limit
    local lengths=(127 128 254 255)
    
    for length in "${lengths[@]}"; do
        echo "Creating filename with length $length"
        create_long_filename "$SERVER_ROOT" "$length"
    done
    
    echo "Running sync for long file names..."
}

scenario_99() {
    set_scenerio_99_name # Large and complex file system
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
    wait_for_server_start
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

}

# At the end, a dispatcher function for debug_tmux.sh:
run_scenario() {
    local scenario_num="$1"
    local func="scenario_$(printf '%02d' "$scenario_num")"
    if declare -f "$func" > /dev/null; then
        $func
    else
        echo "Scenario function $func not found" >&2
        exit 1
    fi
}

set_scenario_name() {
    local scenario_num="$1"
    local func="set_scenario_$(printf '%02d' "$scenario_num")_name"
    if declare -f "$func" > /dev/null; then
        $func
    else
        scenario_name=""    # no scenario defined
    fi
}

# Function to list all available scenarios
list_scenarios() {
    # insert a for loop to call set_scenario_name for each scenario
    for i in {1..99}; do
        set_scenario_name "$i"
        if [[ -n "$scenario_name" ]]; then
            echo "$i. Scenario: $i: $scenario_name"
        fi
    done
}