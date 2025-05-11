import paramiko
import subprocess
import time
import argparse
import threading

# Remote (Ubuntu VM) details
vm_ip = "192.168.122.34"     # Replace with actual IP
vm_username = "testing"
vm_password = "password"

# Build ping command
def build_ping_command(ip, log_file):
    return ["sudo", "./enhanced_ping", "-s", "65515", "-i", "0", "-c", "65536", ip]

def start_vmstat(label):
    log_file = f"vmstat_log_step_test_{label}.txt"
    vmstat_command = f"vmstat 1 80 > {log_file}"
    
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(vm_ip, username=vm_username, password=vm_password)
    ssh.exec_command(f"nohup {vmstat_command} &")
    ssh.close()

def run_ping_instance(ip, index):
    cmd = build_ping_command(ip, None)
    with open(f"ping_log_step_{index}.txt", "w") as f:
        subprocess.run(cmd, stdout=f)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run stepwise concurrent ping floods.")
    parser.add_argument("--max_threads", type=int, default=5, help="Max concurrent ping threads (default: 5)")
    args = parser.parse_args()

    print(f"Starting vmstat logging (log: vmstat_log_step_test_{args.max_threads}.txt)...")
    start_vmstat(args.max_threads)
    
    time.sleep(5)
    print("Starting stepwise ping floods...")

    threads = []
    for i in range(args.max_threads):
        print(f"Starting thread {i + 1}")
        thread = threading.Thread(target=run_ping_instance, args=(vm_ip, i))
        thread.start()
        threads.append(thread)
        
        if i < args.max_threads - 1:
            time.sleep(10)  # Wait before launching next

    print("All threads started. Waiting 10 more seconds before finishing...")
    time.sleep(10)

    print("Waiting for all threads to complete...")
    for thread in threads:
        thread.join()
    
    print("Done.")

