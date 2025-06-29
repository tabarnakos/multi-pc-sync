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

set ( multi_pc_sync_src
	${hash_src}
	${tcp_command_src}
	client.cpp
	directory_indexer.cpp
	file.proto
	folder.proto
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

protobuf_generate(TARGET multi_pc_sync PROTOC_OUT_DIR "..")

target_link_libraries( multi_pc_sync PRIVATE md )
target_link_libraries( multi_pc_sync PRIVATE protobuf::libprotobuf )
