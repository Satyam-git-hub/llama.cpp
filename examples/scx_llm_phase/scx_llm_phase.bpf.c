/*
 * scx_llm_phase.bpf.c - Phase-Aware Scheduler for LLM Inference
 *
 * This BPF scheduler implements the logic described in "Temporal Dissonance in Generative AI".
 * It uses Task Local Storage to track the phase of the LLM inference (Prefill vs Decode)
 * and applies strict priority scheduling for the Decode phase.
 */

// #include "scx/common.bpf.h"

// char _license[] SEC("license") = "GPL";

// // Phase enum (must match userspace)
// enum llm_phase {
//     PHASE_UNKNOWN = 0,
//     PHASE_PREFILL = 1,
//     PHASE_DECODE  = 2
// };

// // Task Local Storage Map
// struct {
//     __uint(type, BPF_MAP_TYPE_TASK_STORAGE);
//     __uint(map_flags, BPF_F_NO_PREALLOC);
//     __type(key, int);
//     __type(value, enum llm_phase);
// } task_phase_map SEC(".maps");

// // Custom DSQ ID for high priority decode tasks
// #define DSQ_DECODE 1024
// // Custom DSQ ID for prefill/default tasks (fallback since built-ins failed)
// #define DSQ_PREFILL 2048

// // Enqueue callback
// void BPF_STRUCT_OPS(llm_enqueue, struct task_struct *p, u64 enq_flags)
// {
//     enum llm_phase *phase;
    
//     // Retrieve the phase from task local storage
//     // Retrieve the phase from task local storage of the group leader
//     phase = bpf_task_storage_get(&task_phase_map, p->group_leader, 0, 0);

//     u32 pid = p->pid;
//     u32 tgid = p->tgid;
//     u32 leader_pid = p->group_leader->pid;

//     if (phase) {
//         if (*phase == PHASE_DECODE) {
//             u64 dsq_id = DSQ_DECODE;
//             // bpf_printk("pid=%d phase=DECODE DSQ=%llu", pid, dsq_id);
//             scx_bpf_dsq_insert(p, dsq_id, SCX_SLICE_DFL, enq_flags);
//             return;
//         } else if (*phase == PHASE_PREFILL) {
//             u64 dsq_id = DSQ_PREFILL;
//             // bpf_printk("pid=%d phase=PREFILL DSQ=%llu", pid, dsq_id);
//             scx_bpf_dsq_insert(p, dsq_id, SCX_SLICE_DFL, enq_flags);
//             return;
//         }
//     }
    
//     // Default
//     // Log everything to debug why the filter was failing
//     char comm[16];
//     bpf_probe_read_kernel_str(comm, sizeof(comm), p->comm);
    
//     bpf_printk("pid=%d tgid=%d leader=%d phase=DEFAULT (NULL) comm=%s", pid, tgid, leader_pid, comm);
    
//     // Use DSQ_PREFILL for default tasks as well
//     scx_bpf_dsq_insert(p, DSQ_PREFILL, SCX_SLICE_DFL, enq_flags);
// }

// // Dispatch callback
// void BPF_STRUCT_OPS(llm_dispatch, s32 cpu, struct task_struct *prev)
// {
//     // 1. Try to consume from custom DSQ (Decode Phase - High Priority)
//     if (scx_bpf_dsq_move_to_local(DSQ_DECODE)) {
//         return;
//     }
    
//     // 2. Consume from Prefill/Default DSQ
//     scx_bpf_dsq_move_to_local(DSQ_PREFILL);
// }

// // Select CPU callback - Critical for preemption
// s32 BPF_STRUCT_OPS(llm_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags)
// {
//     enum llm_phase *phase = bpf_task_storage_get(&task_phase_map, p->group_leader, 0, 0);

//     if (phase && *phase == PHASE_DECODE) {
//         s32 target_cpu = prev_cpu; 
        
//         // Simple logic: Try to stay on previous CPU for cache locality.
//         // In a real implementation, we would search for idle CPUs in the same LLC domain.
//         // For this reference implementation, we stick to prev_cpu.
        
//         // Check if the target CPU is idle. If not, we might need to preempt.
//         // scx_bpf_test_and_clear_cpu_idle() is one way, but here we just want to kick.
        
//         // CRITICAL: If we picked a CPU that is currently busy, KICK it.
//         // This forces the current running task on target_cpu to yield.
//         // We unconditionally kick to ensure immediate service.
//         scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT);
        
//         return target_cpu;
//     }
    
//     // Default selection logic
//     bool is_idle = false;
//     return scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
// }

// // Initialize the scheduler
// void BPF_STRUCT_OPS_SLEEPABLE(llm_init)
// {
//     // Create the custom DSQs
//     bpf_printk("DEBUG: llm_init creating DSQ_DECODE %d", DSQ_DECODE);
//     scx_bpf_create_dsq(DSQ_DECODE, -1);
    
//     bpf_printk("DEBUG: llm_init creating DSQ_PREFILL %d", DSQ_PREFILL);
//     scx_bpf_create_dsq(DSQ_PREFILL, -1);
// }

// // Define the ops structure
// SEC(".struct_ops.link")
// struct sched_ext_ops llm_ops = {
//     .enqueue    = (void *)llm_enqueue,
//     .dispatch   = (void *)llm_dispatch,
//     .select_cpu = (void *)llm_select_cpu,
//     .init       = (void *)llm_init,
//     .name       = "llm_phase",
// };


/* End of scx_llm_phase.bpf.c , start of demo code*/
//go:build ignore
#include "vmlinux.h"
#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

// -------------------------------------------------------------------
//  MANUAL KFUNC DECLARATIONS
// -------------------------------------------------------------------
extern struct task_struct *bpf_task_from_cpu(int cpu) __ksym;
extern void bpf_task_release(struct task_struct *p) __ksym;


/* * CONSTANTS */

// Slices (Batching Logic)
// DECODE: 20ms (Enough to process a token, but responsive)
#define SLICE_DECODE  20000000ULL 
// PREFILL: 30ms (Longer slice = Better cache usage = Higher Throughput)
#define SLICE_PREFILL 30000000ULL 

/* ENUMS & STRUCTS */
enum llm_phase {
    PHASE_UNKNOWN = 0,
    PHASE_PREFILL = 1,
    PHASE_DECODE  = 2
};

/* MAPS */

// 1. Task Phase Map
struct {
    __uint(type, BPF_MAP_TYPE_TASK_STORAGE);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, int);
    __type(value, enum llm_phase);
} task_phase_map SEC(".maps");

// 2. CPU Phase Map (Tracks what is running where)
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1024);
    __type(key, u32);
    __type(value, enum llm_phase);
} cpu_phase_map SEC(".maps");

/* HELPERS */
static __always_inline enum llm_phase get_phase(struct task_struct *p)
{
    enum llm_phase *phase = bpf_task_storage_get(&task_phase_map, p->group_leader, 0, 0);
    if (!phase) return PHASE_UNKNOWN;
    return *phase;
}

/* CALLBACKS */

// 1. SELECT CPU (Affinity + Peace Treaty)
s32 BPF_STRUCT_OPS(llm_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
    enum llm_phase my_phase = get_phase(p);

    // CRITICAL FIX: Always default to prev_cpu for CACHE LOCALITY.
    // Only deviate if we absolutely must.
    s32 target_cpu = prev_cpu;

    // If we are Decode (VIP), we check if we need to bully someone
    if (my_phase == PHASE_DECODE) {
        u32 cpu_key = (u32)target_cpu;
        enum llm_phase *curr_phase_ptr = bpf_map_lookup_elem(&cpu_phase_map, &cpu_key);
        enum llm_phase curr_phase = PHASE_UNKNOWN;
        
        if (curr_phase_ptr) curr_phase = *curr_phase_ptr;

        // If the CPU is busy with "Trash" (Not Decode), KICK it.
        // If it's busy with Decode, we wait (don't fight).
        if (curr_phase != PHASE_DECODE) {
            scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT);
        }
    }
    
    return target_cpu;
}

// 2. ENQUEUE (The "Go Local" Logic)
void BPF_STRUCT_OPS(llm_enqueue, struct task_struct *p, u64 enq_flags)
{
    enum llm_phase phase = get_phase(p);
    u64 slice = SLICE_PREFILL; // Default to 30ms

    if (phase == PHASE_DECODE) {
        slice = SLICE_DECODE; // 20ms
    }

    // CRITICAL FIX: Use SCX_DSQ_LOCAL instead of SHARED_DSQ_ID.
    // This puts the task directly into the runqueue of the CPU selected 
    // by select_cpu(). This guarantees Affinity.
    scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, slice, enq_flags);
}

// 3. DISPATCH (Fallback only)
// Since we are using SCX_DSQ_LOCAL, this is rarely called unless a CPU is idle 
// and trying to steal work (which we don't implement here for simplicity).
void BPF_STRUCT_OPS(llm_dispatch, s32 cpu, struct task_struct *prev)
{
    // We could implement work-stealing here, but for llama.cpp benchmark
    // we want strict affinity, so we do nothing or return.
    // But sched_ext requires we return *something* if we have a global queue.
    // Since we don't use global anymore, this can be empty or just specific return.
    return; 
}

// 4. RUNNING (Update Map)
void BPF_STRUCT_OPS(llm_running, struct task_struct *p)
{
    u32 cpu = bpf_get_smp_processor_id();
    enum llm_phase phase = get_phase(p);
    bpf_map_update_elem(&cpu_phase_map, &cpu, &phase, BPF_ANY);
}

// 5. STOPPING (Update Map)
void BPF_STRUCT_OPS(llm_stopping, struct task_struct *p, bool runnable)
{
    u32 cpu = bpf_get_smp_processor_id();
    enum llm_phase phase = PHASE_UNKNOWN;
    bpf_map_update_elem(&cpu_phase_map, &cpu, &phase, BPF_ANY);
}

// 6. INIT
s32 BPF_STRUCT_OPS_SLEEPABLE(llm_init)
{
    // We don't strictly need a custom DSQ anymore, but good to have.
    return scx_bpf_create_dsq(SCX_DSQ_GLOBAL, -1);
}

SEC(".struct_ops.link")
struct sched_ext_ops llm_ops = {
    .select_cpu = (void *)llm_select_cpu,
    .enqueue    = (void *)llm_enqueue,
    .dispatch   = (void *)llm_dispatch,
    .running    = (void *)llm_running,
    .stopping   = (void *)llm_stopping,
    .init       = (void *)llm_init,
    .name       = "llm_local",
};