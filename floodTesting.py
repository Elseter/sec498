import paramiko
import subprocess
import time

# Remote (Ubuntu VM) details
vm_ip = "192.168.122.34"     # Replace with actual IP
vm_username = "testing" # Replace with actual username
vm_password = "password" # Or use SSH keys instead
remote_log_file = "vmstat_log.txt"
vmstat_command = f"vmstat 1 100 > {remote_log_file}"  # 60 seconds of recording

# Local ping sweep command (customize as needed)
ping_command = ["sudo", "./enhanced_ping", "-s", "65515", "-i", "0", "-c", "65536", vm_ip]

# Start SSH session and execute remote command
def start_vmstat():
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(vm_ip, username=vm_username, password=vm_password)
    # Run vmstat in background
    ssh.exec_command(f"nohup {vmstat_command} &")
    ssh.close()

def start_ping():
    subprocess.run(ping_command)

if __name__ == "__main__":
    print("Starting vmstat on VM...")
    start_vmstat()
    time.sleep(1)  # small buffer to ensure vmstat starts
    print("Starting ping flood from host...")
    start_ping()
    print("Done.")

