import multiprocessing
import time
import sys

def stress():
    while True:
        _ = 3.14159 * 2.71828

if __name__ == "__main__":
    num_workers = 4
    if len(sys.argv) > 1:
        num_workers = int(sys.argv[1])
    
    print(f"Starting {num_workers} stress workers...")
    processes = []
    for _ in range(num_workers):
        p = multiprocessing.Process(target=stress)
        p.start()
        processes.append(p)
        
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("Stopping workers...")
        for p in processes:
            p.terminate()
