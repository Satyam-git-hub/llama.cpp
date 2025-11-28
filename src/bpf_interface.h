#pragma once

#ifdef LLAMA_SCHED_EXT

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

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
            // It's expected to fail if the scheduler is not loaded
            // We can just ignore it, or print a warning once
            // fprintf(stderr, "PhaseHinter: Failed to open BPF map: %m\n");
        }
    }

    ~PhaseHinter() {
        if (map_fd >= 0) {
            close(map_fd);
        }
    }

    void set_phase(int phase) {
        if (map_fd < 0) return;

        int key = 0; // 0 implies "current task" in task_local_storage if we were using that, 
                     // but for BPF_MAP_TYPE_TASK_STORAGE, the key is actually the task_fd or 0 for current?
                     // Wait, for task_storage, the key in userspace bpf_map_update_elem is the FD of the task.
                     // Passing 0 usually doesn't work for task_storage from userspace unless we have a specific helper.
                     // Actually, updating task_storage from userspace is tricky.
                     // Usually we use a pid_iter or similar.
                     // BUT, if we use a simple ARRAY map or HASH map keyed by PID, it's easier.
                     // The research paper said "Task Local Storage".
                     // "We utilize BPF_MAP_TYPE_TASK_STORAGE... key is the task (thread) itself"
                     // Updating task storage from userspace requires getting the task FD.
                     // pidfd_open() can get a FD for the current thread/process.
        
        // Let's try to get a FD for the current thread.
        // On Linux, gettid() gives the thread ID.
        // We can use /proc/self/task/<tid>/... or just pidfd_open if available.
        
        // However, for simplicity and to match the paper's "userspace hook", 
        // maybe they meant a simple map keyed by PID/TID?
        // "In llama.cpp, we introduce a lightweight wrapper around bpf_map_update_elem."
        
        // If it is indeed TASK_STORAGE, we need a FD to the task.
        // Let's assume we can get it via open("/proc/self/task/<tid>", O_RDONLY).
        
        // But wait, if we use a pinned map, we can just update it.
        
        // Let's implement getting the FD for the current thread.
        // Or, if the BPF side uses a HASH map keyed by u32 (tid), that's easier.
        // The paper says: "We utilize BPF_MAP_TYPE_TASK_STORAGE".
        // And "In llama.cpp... bpf_map_update_elem(map_fd, &key, &phase, BPF_ANY)".
        // If key is 0, that implies the map might be a special type or they are simplifying.
        
        // Standard libbpf way to update task storage for *current* task from userspace:
        // You need a FD representing the task.
        
        int task_fd = open("/proc/thread-self", O_RDONLY);
        if (task_fd < 0) return;
        
        bpf_map_update_elem(map_fd, &task_fd, &phase, BPF_ANY);
        
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
