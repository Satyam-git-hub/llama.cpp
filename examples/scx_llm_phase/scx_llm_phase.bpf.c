/*
 * scx_llm_phase.bpf.c - Phase-Aware Scheduler for LLM Inference
 *
 * This BPF scheduler implements the logic described in "Temporal Dissonance in Generative AI".
 * It uses Task Local Storage to track the phase of the LLM inference (Prefill vs Decode)
 * and applies strict priority scheduling for the Decode phase.
 */

#include "scx/common.bpf.h"

char _license[] SEC("license") = "GPL";

// Phase enum (must match userspace)
enum llm_phase {
    PHASE_UNKNOWN = 0,
    PHASE_PREFILL = 1,
    PHASE_DECODE  = 2
};

// Task Local Storage Map
struct {
    __uint(type, BPF_MAP_TYPE_TASK_STORAGE);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, int);
    __type(value, enum llm_phase);
} task_phase_map SEC(".maps");

// Custom DSQ ID for high priority decode tasks
#define DSQ_DECODE 0

// Enqueue callback
void BPF_STRUCT_OPS(llm_enqueue, struct task_struct *p, u64 enq_flags)
{
    enum llm_phase *phase;
    
    // Retrieve the phase from task local storage
    phase = bpf_task_storage_get(&task_phase_map, p, 0, 0);

    if (phase && *phase == PHASE_DECODE) {
        // Strict Priority: Dispatch to Custom DSQ 0 (High Priority)
        // We use a large slice to avoid unnecessary ticks, though preemption is handled via kick_cpu
        scx_bpf_dsq_insert(p, DSQ_DECODE, SCX_SLICE_DFL, enq_flags);
        return;
    }
    
    // Default: Dispatch to Global DSQ
    scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
}

// Dispatch callback
void BPF_STRUCT_OPS(llm_dispatch, s32 cpu, struct task_struct *prev)
{
    // 1. Try to consume from the High Priority Decode Queue (DSQ 0)
    if (scx_bpf_dsq_move_to_local(DSQ_DECODE)) {
        return; 
    }

    // 2. If no high priority work, consume from Global Queue
    scx_bpf_dsq_move_to_local(SCX_DSQ_GLOBAL);
}

// Select CPU callback - Critical for preemption
s32 BPF_STRUCT_OPS(llm_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
    enum llm_phase *phase = bpf_task_storage_get(&task_phase_map, p, 0, 0);

    if (phase && *phase == PHASE_DECODE) {
        s32 target_cpu = prev_cpu; 
        
        // Simple logic: Try to stay on previous CPU for cache locality.
        // In a real implementation, we would search for idle CPUs in the same LLC domain.
        // For this reference implementation, we stick to prev_cpu.
        
        // Check if the target CPU is idle. If not, we might need to preempt.
        // scx_bpf_test_and_clear_cpu_idle() is one way, but here we just want to kick.
        
        // CRITICAL: If we picked a CPU that is currently busy, KICK it.
        // This forces the current running task on target_cpu to yield.
        // We unconditionally kick to ensure immediate service.
        scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT);
        
        return target_cpu;
    }
    
    // Default selection logic
    bool is_idle = false;
    return scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
}

// Initialize the scheduler
void BPF_STRUCT_OPS_SLEEPABLE(llm_init)
{
    // Create the custom DSQ
    scx_bpf_create_dsq(DSQ_DECODE, -1);
}

// Define the ops structure
SEC(".struct_ops.link")
struct sched_ext_ops llm_ops = {
    .enqueue    = (void *)llm_enqueue,
    .dispatch   = (void *)llm_dispatch,
    .select_cpu = (void *)llm_select_cpu,
    .init       = (void *)llm_init,
    .name       = "llm_phase",
};
