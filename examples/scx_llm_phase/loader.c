/*
 * loader.c - Userspace loader for scx_llm_phase
 *
 * This program loads the BPF scheduler and pins the task_phase_map
 * to /sys/fs/bpf/scx_llm_phase_map so that llama.cpp can access it.
 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_llm_phase.skel.h"

static volatile int exiting = 0;

static void sig_handler(int sig)
{
    exiting = 1;
}

int main(int argc, char **argv)
{
    struct scx_llm_phase_bpf *skel;
    int err;

    // Setup signal handler
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Open BPF skeleton
    skel = scx_llm_phase_bpf__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    // Load BPF skeleton
    err = scx_llm_phase_bpf__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF skeleton: %d\n", err);
        goto cleanup;
    }

    // Pin the map
    // We unpin first just in case it was left over
    unlink("/sys/fs/bpf/scx_llm_phase_map");
    err = bpf_map__pin(skel->maps.task_phase_map, "/sys/fs/bpf/scx_llm_phase_map");
    if (err) {
        fprintf(stderr, "Failed to pin map: %d\n", err);
        goto cleanup;
    }
    printf("Pinned map to /sys/fs/bpf/scx_llm_phase_map\n");
    
    // Allow non-root users (like the one running llama.cpp) to update the map
    if (chmod("/sys/fs/bpf/scx_llm_phase_map", 0666) < 0) {
        fprintf(stderr, "Failed to chmod map: %m\n");
        // Don't exit, just warn
    }

    // Attach the scheduler
    struct bpf_link *link = bpf_map__attach_struct_ops(skel->maps.llm_ops);
    if (!link) {
        fprintf(stderr, "Failed to attach scheduler: %m\n");
        goto cleanup;
    }
    printf("Scheduler attached. Press Ctrl-C to exit.\n");

    // Keep running until signal
    while (!exiting) {
        sleep(1);
    }

cleanup:
    // Cleanup
    unlink("/sys/fs/bpf/scx_llm_phase_map");
    scx_llm_phase_bpf__destroy(skel);
    return 0;
}
