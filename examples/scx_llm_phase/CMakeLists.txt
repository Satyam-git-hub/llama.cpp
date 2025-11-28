cmake_minimum_required(VERSION 3.14)
project(scx_llm_phase)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBBPF REQUIRED libbpf)

find_program(CLANG clang)
find_program(BPFTOOL bpftool)

if (NOT CLANG OR NOT BPFTOOL)
    message(WARNING "clang or bpftool not found. Skipping scx_llm_phase build.")
    return()
endif()

# Generate vmlinux.h
add_custom_command(OUTPUT vmlinux.h
    COMMAND ${BPFTOOL} btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
)

# Build BPF object
add_custom_command(OUTPUT scx_llm_phase.bpf.o
    COMMAND ${CLANG} -g -O2 -target bpf -D__TARGET_ARCH_x86 -I/usr/include/x86_64-linux-gnu -I${CMAKE_CURRENT_SOURCE_DIR}/scx/scheds/include -I${CMAKE_CURRENT_BINARY_DIR} -c ${CMAKE_CURRENT_SOURCE_DIR}/scx_llm_phase.bpf.c -o scx_llm_phase.bpf.o
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scx_llm_phase.bpf.c vmlinux.h
)

# Generate Skeleton
add_custom_command(OUTPUT scx_llm_phase.skel.h
    COMMAND ${BPFTOOL} gen skeleton scx_llm_phase.bpf.o > scx_llm_phase.skel.h
    COMMAND sed -i "/map->link =/d" scx_llm_phase.skel.h
    DEPENDS scx_llm_phase.bpf.o
    VERBATIM
)

# Build Loader
add_executable(scx_llm_phase_loader loader.c scx_llm_phase.skel.h)
target_link_libraries(scx_llm_phase_loader PkgConfig::LIBBPF)
target_include_directories(scx_llm_phase_loader PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/scx/scheds/include)
