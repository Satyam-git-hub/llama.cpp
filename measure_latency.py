import subprocess
import time
import sys
import math

def measure_latency(command):
    print(f"Running: {' '.join(command)}")
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, # Capture stderr to avoid polluting output
        text=True,
        bufsize=0 # Unbuffered
    )

    latencies = []
    last_time = None
    
    # Simple state machine to detect start of generation
    generating = False

    while True:
        # Read one character at a time to get precise timing
        char = process.stdout.read(1)
        if not char and process.poll() is not None:
            break
        if not char:
            continue
            
        current_time = time.time()
        
        # We assume generation starts when we see the first output
        if not generating:
            generating = True
            last_time = current_time
            sys.stdout.write(char)
            sys.stdout.flush()
            continue

        if generating:
            latency = (current_time - last_time) * 1000 # ms
            latencies.append(latency)
            last_time = current_time
            sys.stdout.write(char)
            sys.stdout.flush()

    process.wait()
    return latencies

def percentile(data, p):
    if not data:
        return 0
    k = (len(data) - 1) * (p / 100.0)
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return data[int(k)]
    d0 = data[int(f)]
    d1 = data[int(c)]
    return d0 + (d1 - d0) * (k - f)

def print_stats(latencies):
    if not latencies:
        print("\nNo tokens generated.")
        return

    latencies.sort()
    count = len(latencies)
    avg = sum(latencies) / count
    variance = sum([((x - avg) ** 2) for x in latencies]) / count
    std = math.sqrt(variance)
    
    p50 = percentile(latencies, 50)
    p95 = percentile(latencies, 95)
    p99 = percentile(latencies, 99)

    print(f"\n\n--- Statistics (ms) ---")
    print(f"Count: {count}")
    print(f"Avg:   {avg:.2f}")
    print(f"Std:   {std:.2f}")
    print(f"p50:   {p50:.2f}")
    print(f"p95:   {p95:.2f}")
    print(f"p99:   {p99:.2f}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 measure_latency.py <path_to_llama_cli> <args...>")
        sys.exit(1)

    cmd = sys.argv[1:]
    latencies = measure_latency(cmd)
    print_stats(latencies)
