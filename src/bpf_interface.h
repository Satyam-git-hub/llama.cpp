#pragma once

#ifdef LLAMA_SCHED_EXT

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <fcntl.h>

#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif

// Phase enum must match the BPF program
enum llm_phase {
    PHASE_UNKNOWN = 0,
    PHASE_PREFILL = 1,
    PHASE_DECODE  = 2
};

class PhaseHinter {
    int map_fd;

public:
    PhaseHinter() {
        // Open the pinned map created by the scheduler loader
        // We use bpf_obj_get to get the FD of the pinned map
        map_fd = bpf_obj_get("/sys/fs/bpf/scx_llm_phase_map");
        if (map_fd < 0) {
            fprintf(stderr, "PhaseHinter: Failed to open BPF map: %m\n");
        } else {
            fprintf(stderr, "PhaseHinter: Successfully opened BPF map (fd=%d)\n", map_fd);
        }
    }

    ~PhaseHinter() {
        if (map_fd >= 0) {
            close(map_fd);
        }
    }

    int last_phase = -1;

    void set_phase(int phase) {
        if (map_fd < 0) return;
        if (phase == last_phase) return;

        // For TASK_STORAGE, we need a pidfd to the task (thread)
        // We target the process leader (TGID) so that all threads can share the hint
        pid_t tgid = getpid();
        int task_fd = syscall(SYS_pidfd_open, tgid, 0);
        
        if (task_fd < 0) {
             static bool warned_open = false;
             if (!warned_open) {
                 fprintf(stderr, "PhaseHinter: Failed to get pidfd for TGID %d: %m\n", tgid);
                 warned_open = true;
             }
             return;
        } else {
             static bool logged_open = false;
             if (!logged_open) {
                 fprintf(stderr, "PhaseHinter: Successfully got pidfd %d for TGID %d\n", task_fd, tgid);
                 logged_open = true;
             }
        }
        
        if (bpf_map_update_elem(map_fd, &task_fd, &phase, BPF_ANY) < 0) {
             static bool warned_update = false;
             if (!warned_update) {
                 fprintf(stderr, "PhaseHinter: Failed to update map for task_fd %d: %m\n", task_fd);
                 warned_update = true;
             }
        } else {
             // Log the update to prove it happened
             fprintf(stderr, "PhaseHinter: Updated map for TGID %d to phase %d\n", tgid, phase);
             last_phase = phase;
        }
        
        close(task_fd);
    }
};

#else

// Dummy implementation when LLAMA_SCHED_EXT is OFF
class PhaseHinter {
public:
    PhaseHinter() {}
    void set_phase(int) {}
};

enum llm_phase {
    PHASE_UNKNOWN = 0,
    PHASE_PREFILL = 1,
    PHASE_DECODE  = 2
};

#endif
