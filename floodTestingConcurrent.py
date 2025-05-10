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

def start_vmstat(concurrent_count):
    log_file = f"vmstat_log_{concurrent_count}_pings.txt"
    vmstat_command = f"vmstat 1 80 > {log_file}"
    
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(vm_ip, username=vm_username, password=vm_password)
    ssh.exec_command(f"nohup {vmstat_command} &")
    ssh.close()

def run_ping_instance(ip, index, log=False):
    cmd = build_ping_command(ip, None)
    if log:
        with open(f"ping_log_{index}.txt", "w") as f:
            subprocess.run(cmd, stdout=f)
    else:
        subprocess.run(cmd)

def start_concurrent_pings(ip, count):
    threads = []
    for i in range(count):
        thread = threading.Thread(target=run_ping_instance, args=(ip, i, True))
        thread.start()
        threads.append(thread)

    for thread in threads:
        thread.join()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run concurrent ping floods against VM.")
    parser.add_argument("count", type=int, help="Number of concurrent ping processes to run.")
    args = parser.parse_args()

    print(f"Starting vmstat on VM (log: vmstat_log_{args.count}_pings.txt)...")
    start_vmstat(args.count)
    time.sleep(5)
    print(f"Starting {args.count} concurrent ping floods...")
    start_concurrent_pings(vm_ip, args.count)
    print("Done.")

