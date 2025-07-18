#!/bin/bash

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'  # Used for partial pass
ORANGE='\033[0;33m'  # Another option for partial pass
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
NC='\033[0m' # No Color

# Function to check if a test passed (no missing/extra files or hashes)
check_test_result() {
    local section="$1"
    local has_files_hash_failure=false
    local has_time_failure=false
    
    # Look for actual missing files (lines that start with ./ after "Missing files:")
    local missing_files_section=$(echo "$section" | sed -n '/Missing files:/,/^$/p')
    if echo "$missing_files_section" | grep -q "^\./"; then
        has_files_hash_failure=true
    fi
    
    # Look for actual extra files (lines that start with ./ after "Extra files:")
    local extra_files_section=$(echo "$section" | sed -n '/Extra files:/,/^$/p')
    if echo "$extra_files_section" | grep -q "^\./"; then
        has_files_hash_failure=true
    fi
    
    # Look for actual missing hashes (lines with hex after "Missing hashes:")
    local missing_hashes_section=$(echo "$section" | sed -n '/Missing hashes:/,/^$/p')
    if echo "$missing_hashes_section" | grep -q "^[a-f0-9]"; then
        has_files_hash_failure=true
    fi
    
    # Look for actual extra hashes (lines with hex after "Extra hashes:")
    local extra_hashes_section=$(echo "$section" | sed -n '/Extra hashes:/,/^$/p')
    if echo "$extra_hashes_section" | grep -q "^[a-f0-9]"; then
        has_files_hash_failure=true
    fi
    
    # Check for file times comparison failure
    if echo "$section" | grep -q "File times comparison failed."; then
        has_time_failure=true
    fi
    
    # Check for file permissions comparison failure
    if echo "$section" | grep -q "File permissions comparison failed."; then
        has_time_failure=true
    fi
    
    # Determine result based on failures
    if [ "$has_files_hash_failure" = true ]; then
        echo "FAIL"
    elif [ "$has_time_failure" = true ]; then
        echo "PART"
    else
        echo "PASS"
    fi
}

# Function to draw a test result box
draw_test_box() {
    local scenario_num="$1"
    local client_result="$2"
    local server_result="$3"
    local scenario_title="$4"
    
    # Color codes for results
    local client_color=""
    local server_color=""
    
    if [ "$client_result" = "PASS" ]; then
        client_color="$GREEN"
    elif [ "$client_result" = "PART" ]; then
        client_color="$YELLOW"
    else
        client_color="$RED"
    fi
    
    if [ "$server_result" = "PASS" ]; then
        server_color="$GREEN"
    elif [ "$server_result" = "PART" ]; then
        server_color="$YELLOW"
    else
        server_color="$RED"
    fi
    
    # Draw the box (each scenario is a 12x6 character box)
    echo -e "┌──────────┐"
    echo -e "│${WHITE}Scenario $scenario_num${NC}│"
    echo -e "├─────┬────┤"
    echo -e "│${CYAN}CLI${NC} │${CYAN}SRV${NC} │"
    echo -e "│${client_color}$client_result${NC} │${server_color}$server_result${NC} │"
    echo -e "└─────┴────┘"
}

# Function to extract scenario info
parse_test_report() {
    local file="$1"
    local current_scenario=""
    local scenario_title=""
    local client_section=""
    local server_section=""
    local in_client_section=false
    local in_server_section=false
    
    while IFS= read -r line; do
        # Check for scenario start
        if [[ "$line" =~ ^=.*Scenario\ ([0-9]+).*Test\ Report.*=$ ]]; then
            # Process previous scenario if exists
            if [ -n "$current_scenario" ]; then
                local client_result=$(check_test_result "$client_section")
                local server_result=$(check_test_result "$server_section")
                draw_test_box "$current_scenario" "$client_result" "$server_result"
            fi
            
            # Start new scenario
            current_scenario=$(echo "$line" | sed -n 's/.*Scenario \([0-9]*\).*/\1/p')
            client_section=""
            server_section=""
            in_client_section=false
            in_server_section=false
            
        # Check for client section start
        elif [[ "$line" =~ ^Comparing\ files\ in\ CLIENT_ROOT ]]; then
            in_client_section=true
            in_server_section=false
            client_section="$line"
            
        # Check for server section start
        elif [[ "$line" =~ ^Comparing\ files\ in\ SERVER_ROOT ]]; then
            in_client_section=false
            in_server_section=true
            server_section="$line"
            
        # Check for scenario end
        elif [[ "$line" =~ ^=.*Scenario\ [0-9]+.*\/Test\ Report.*=$ ]]; then
            # Continue accumulating until we hit the next scenario
            continue
            
        # Accumulate section content
        elif [ "$in_client_section" = true ]; then
            client_section="$client_section"$'\n'"$line"
        elif [ "$in_server_section" = true ]; then
            server_section="$server_section"$'\n'"$line"
        fi
        
    done < "$file"
    
    # Process the last scenario
    if [ -n "$current_scenario" ]; then
        local client_result=$(check_test_result "$client_section")
        local server_result=$(check_test_result "$server_section")
        draw_test_box "$current_scenario" "$client_result" "$server_result"
    fi
}

# Function to create a grid layout
create_grid_layout() {
    local file="$1"
    local scenarios=()
    local scenario_titles=()
    local client_results=()
    local server_results=()
    
    # Parse all scenarios first
    local current_scenario=""
    local current_title=""
    local client_section=""
    local server_section=""
    local file_times_section=""
    local file_permissions_section=""
    local in_client_section=false
    local in_server_section=false
    local in_file_times_section=false
    local in_file_permissions_section=false
    local scenario_complete=false
    
    while IFS= read -r line; do
        # Check for scenario start (but not end)
        if [[ "$line" =~ ^=.*Scenario\ ([0-9]+).*Test\ Report.*=$ ]] && [[ ! "$line" =~ /Test\ Report.*=$ ]]; then
            # Process previous scenario if exists and complete
            if [ -n "$current_scenario" ] && [ "$scenario_complete" = true ]; then
                scenarios+=("$current_scenario")
                scenario_titles+=("$current_title")
                # Combine all sections for comprehensive checking
                local combined_section="$client_section"$'\n'"$server_section"$'\n'"$file_times_section"$'\n'"$file_permissions_section"
                client_results+=($(check_test_result "$combined_section"))
                server_results+=($(check_test_result "$combined_section"))
            fi
            
            current_scenario=$(echo "$line" | sed -n 's/.*Scenario \([0-9]*\).*/\1/p')
            # Extract scenario title - handle format "Scenario X - Title Test Report"
            current_title=$(echo "$line" | sed -n 's/^=*\s*Scenario [0-9]\+\s*-\s*\(.*\)\s*Test Report.*$/\1/p' | sed 's/[[:space:]]*$//')
            client_section=""
            server_section=""
            file_times_section=""
            file_permissions_section=""
            in_client_section=false
            in_server_section=false
            in_file_times_section=false
            in_file_permissions_section=false
            scenario_complete=false
            
        # Check for scenario end
        elif [[ "$line" =~ ^=.*Scenario\ [0-9]+.*\/Test\ Report.*=$ ]]; then
            scenario_complete=true
            
        elif [[ "$line" =~ ^Comparing\ files\ in\ CLIENT_ROOT ]]; then
            in_client_section=true
            in_server_section=false
            in_file_times_section=false
            in_file_permissions_section=false
            client_section="$line"
            
        elif [[ "$line" =~ ^Comparing\ files\ in\ SERVER_ROOT ]]; then
            in_client_section=false
            in_server_section=true
            in_file_times_section=false
            in_file_permissions_section=false
            server_section="$line"
            
        elif [[ "$line" =~ ^Comparing\ file\ times ]]; then
            in_client_section=false
            in_server_section=false
            in_file_times_section=true
            in_file_permissions_section=false
            file_times_section="$line"
            
        elif [[ "$line" =~ ^Comparing\ file\ permissions ]]; then
            in_client_section=false
            in_server_section=false
            in_file_times_section=false
            in_file_permissions_section=true
            file_permissions_section="$line"
            
        elif [ "$in_client_section" = true ]; then
            client_section="$client_section"$'\n'"$line"
        elif [ "$in_server_section" = true ]; then
            server_section="$server_section"$'\n'"$line"
        elif [ "$in_file_times_section" = true ]; then
            file_times_section="$file_times_section"$'\n'"$line"
        elif [ "$in_file_permissions_section" = true ]; then
            file_permissions_section="$file_permissions_section"$'\n'"$line"
        fi
    done < "$file"
    
    # Process the last scenario if complete
    if [ -n "$current_scenario" ] && [ "$scenario_complete" = true ]; then
        scenarios+=("$current_scenario")
        scenario_titles+=("$current_title")
        # Combine all sections for comprehensive checking
        local combined_section="$client_section"$'\n'"$server_section"$'\n'"$file_times_section"$'\n'"$file_permissions_section"
        client_results+=($(check_test_result "$combined_section"))
        server_results+=($(check_test_result "$combined_section"))
    fi
    
    # Display header
    echo -e "${WHITE}═══════════════════════════════════════════════════════════════════════════════${NC}"
    echo -e "${WHITE}                           MULTI-PC-SYNC TEST REPORT VISUALIZATION            ${NC}"
    echo -e "${WHITE}═══════════════════════════════════════════════════════════════════════════════${NC}"
    echo ""
    echo -e "${CYAN}Legend: ${GREEN}PASS${NC} = All checks passed  ${YELLOW}PARTIAL${NC} = File times/permissions failed  ${RED}FAIL${NC} = Missing/extra files or hashes"
    echo -e "${CYAN}        CLI = Client Test Result, SRV = Server Test Result${NC}"
    echo ""
    
    # Display scenarios in a dynamic grid with 5 columns
    local cols=5
    local total=${#scenarios[@]}
    local rows=$(( (total + cols - 1) / cols ))

    for ((row=0; row<rows; row++)); do
        # Top border line
        for ((col=0; col<cols; col++)); do
            local idx=$((row * cols + col))
            if [ $idx -lt $total ]; then
                printf "┌──────────┐"
                if [ $col -lt $((cols-1)) ]; then
                    printf " "
                fi
            fi
        done
        echo ""

        # Scenario number line
        for ((col=0; col<cols; col++)); do
            local idx=$((row * cols + col))
            if [ $idx -lt $total ]; then
                printf "│${WHITE}%-10s${NC}│" "Scenario${scenarios[$idx]}"
                if [ $col -lt $((cols-1)) ]; then
                    printf " "
                fi
            fi
        done
        echo ""

        # Middle separator line
        for ((col=0; col<cols; col++)); do
            local idx=$((row * cols + col))
            if [ $idx -lt $total ]; then
                printf "├─────┬────┤"
                if [ $col -lt $((cols-1)) ]; then
                    printf " "
                fi
            fi
        done
        echo ""

        # Header line
        for ((col=0; col<cols; col++)); do
            local idx=$((row * cols + col))
            if [ $idx -lt $total ]; then
                printf "│${CYAN}%-5s${NC}│${CYAN}%-4s${NC}│" "CLI" "SRV"
                if [ $col -lt $((cols-1)) ]; then
                    printf " "
                fi
            fi
        done
        echo ""

        # Result line
        for ((col=0; col<cols; col++)); do
            local idx=$((row * cols + col))
            if [ $idx -lt $total ]; then
                local client_color=""
                local server_color=""

                if [ "${client_results[$idx]}" = "PASS" ]; then
                    client_color="$GREEN"
                elif [ "${client_results[$idx]}" = "PART" ]; then
                    client_color="$YELLOW"
                else
                    client_color="$RED"
                fi

                if [ "${server_results[$idx]}" = "PASS" ]; then
                    server_color="$GREEN"
                elif [ "${server_results[$idx]}" = "PART" ]; then
                    server_color="$YELLOW"
                else
                    server_color="$RED"
                fi

                printf "│${client_color}%-5s${NC}│${server_color}%-4s${NC}│" "${client_results[$idx]}" "${server_results[$idx]}"
                if [ $col -lt $((cols-1)) ]; then
                    printf " "
                fi
            fi
        done
        echo ""

        # Bottom border line
        for ((col=0; col<cols; col++)); do
            local idx=$((row * cols + col))
            if [ $idx -lt $total ]; then
                printf "└─────┴────┘"
                if [ $col -lt $((cols-1)) ]; then
                    printf " "
                fi
            fi
        done
        echo ""
        echo ""
    done
    
    # Summary
    local total_tests=$((${#scenarios[@]} * 2))  # Each scenario has client + server tests
    local passed_tests=0
    local partial_tests=0
    
    for result in "${client_results[@]}" "${server_results[@]}"; do
        if [ "$result" = "PASS" ]; then
            ((passed_tests++))
        elif [ "$result" = "PART" ]; then
            ((partial_tests++))
        fi
    done
    
    local failed_tests=$((total_tests - passed_tests - partial_tests))
    
    echo -e "${WHITE}═══════════════════════════════════════════════════════════════════════════════${NC}"
    echo -e "${WHITE}SUMMARY:${NC} Total Tests: $total_tests | ${GREEN}Passed: $passed_tests${NC} | ${YELLOW}Partial: $partial_tests${NC} | ${RED}Failed: $failed_tests${NC}"
    echo -e "${WHITE}═══════════════════════════════════════════════════════════════════════════════${NC}"
    
    # Display detailed list of scenarios with titles and status
    echo ""
    echo -e "${WHITE}DETAILED TEST RESULTS:${NC}"
    echo -e "${WHITE}─────────────────────────────────────────────────────────────────────────────────${NC}"
    echo ""
    
    for ((i=0; i<${#scenarios[@]}; i++)); do
        local scenario_num="${scenarios[$i]}"
        local title="${scenario_titles[$i]}"
        local client_result="${client_results[$i]}"
        local server_result="${server_results[$i]}"
        local status_color=""
        
        # Determine status color based on client and server results
        if [ "$client_result" = "PASS" ] && [ "$server_result" = "PASS" ]; then
            status_color="$GREEN"
            status="PASS"
        elif [ "$client_result" = "FAIL" ] || [ "$server_result" = "FAIL" ]; then
            status_color="$RED"
            status="FAIL"
        elif [ "$client_result" = "PART" ] || [ "$server_result" = "PART" ]; then
            status_color="$YELLOW"
            status="PARTIAL"
        else
            status_color="$ORANGE"
            status="MIXED"
        fi
        
        # Add padding to scenario number for alignment
        printf "${WHITE}%-3s${NC} ${status_color}%-8s${NC} %s\n" "$scenario_num" "[$status]" "$title"
    done
    
    echo ""
    echo -e "${WHITE}═══════════════════════════════════════════════════════════════════════════════${NC}"
}

# Main function
main() {
    local report_file="${1:-test_report.txt}"
    
    if [ ! -f "$report_file" ]; then
        echo -e "${RED}Error: Test report file '$report_file' not found!${NC}"
        echo "Usage: $0 [test_report_file]"
        exit 1
    fi
    
    echo -e "${BLUE}Processing test report: $report_file${NC}"
    echo ""
    
    create_grid_layout "$report_file"
}

# Run main function with arguments
main "$@"
