cmake_minimum_required(VERSION 3.22)
set(CMAKE_GENERATOR Ninja)
project(multi_pc_sync VERSION 0.9)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# protobuf
find_package(Protobuf REQUIRED CONFIG)
include_directories(${PROTOBUF_INCLUDE_DIR})
include_directories(${CMAKE_SOURCE_DIR})
include_directories(tcp_command)
include_directories(hash)
include(hash/hash.mk)
include(tcp_command/tcp_command.mk)

# Generate protobuf files at configuration time
file(GLOB PROTO_FILES "${CMAKE_SOURCE_DIR}/*.proto")
set(PROTO_GENERATED_FILES)

foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    get_filename_component(PROTO_FILENAME ${PROTO_FILE} NAME)
    set(PROTO_HEADER "${CMAKE_SOURCE_DIR}/${PROTO_NAME}.pb.h")
    set(PROTO_SOURCE "${CMAKE_SOURCE_DIR}/${PROTO_NAME}.pb.cc")
    
    # Check if generated files are older than proto files or don't exist
    set(NEED_REGENERATE FALSE)
    if(NOT EXISTS ${PROTO_HEADER} OR NOT EXISTS ${PROTO_SOURCE})
        set(NEED_REGENERATE TRUE)
    else()
        file(TIMESTAMP ${PROTO_FILE} PROTO_TIMESTAMP)
        file(TIMESTAMP ${PROTO_HEADER} HEADER_TIMESTAMP)
        if(PROTO_TIMESTAMP IS_NEWER_THAN HEADER_TIMESTAMP)
            set(NEED_REGENERATE TRUE)
        endif()
    endif()
    
    if(NEED_REGENERATE)
        message(STATUS "Generating protobuf files for ${PROTO_FILENAME}")
        execute_process(
            COMMAND protoc
                    --proto_path=${CMAKE_SOURCE_DIR}
                    --cpp_out=${CMAKE_SOURCE_DIR}
                    ${PROTO_FILENAME}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE protoc_result
            ERROR_VARIABLE protoc_error
        )
        
        if(NOT protoc_result EQUAL 0)
            message(FATAL_ERROR "protoc failed for ${PROTO_FILENAME}: ${protoc_error}")
        endif()
    endif()
    
    list(APPEND PROTO_GENERATED_FILES ${PROTO_HEADER} ${PROTO_SOURCE})
endforeach()

set ( multi_pc_sync_src
	${hash_src}
	${tcp_command_src}
	client.cpp
	directory_indexer.cpp
	${PROTO_GENERATED_FILES}
	growing_buffer.cpp
	main.cpp
	program_options.cpp
	server.cpp
	socket_helpers.cpp
	sync_command.cpp
	)

set ( multi_pc_sync_hdr
	${hash_hdr}
	${tcp_command_hdr}
	directory_indexer.h
	growing_buffer.h
	human_readable.h
	network_thread.h
	program_options.h
	socket_helpers.h
	sync_command.h
	)

add_executable( multi_pc_sync ${multi_pc_sync_src} ${multi_pc_sync_hdr} )
target_compile_options(multi_pc_sync PRIVATE -Wall -Wextra -Wno-sign-compare -Wpedantic $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Release>:-O3>)
target_compile_definitions(multi_pc_sync PRIVATE $<$<CONFIG:Debug>:DEBUG> $<$<CONFIG:Release>:RELEASE>)

target_include_directories(multi_pc_sync PRIVATE ${CMAKE_SOURCE_DIR}/third-party/termcolor/include)

target_link_libraries( multi_pc_sync PRIVATE md )
target_link_libraries( multi_pc_sync PRIVATE protobuf::libprotobuf )
