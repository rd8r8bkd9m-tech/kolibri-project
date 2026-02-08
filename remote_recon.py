import pexpect

def run_remote(ip, password, command):
    print(f"Running command: {command}")
    child = pexpect.spawn(f'ssh -o StrictHostKeyChecking=no root@{ip} "{command}"', timeout=60)
    try:
        i = child.expect(['[Pp]assword:', pexpect.EOF, pexpect.TIMEOUT])
        if i == 0:
            child.sendline(password)
            child.expect(pexpect.EOF)
            return child.before.decode()
        elif i == 1:
            return child.before.decode()
        else:
            return "TIMEOUT"
    except Exception as e:
        return str(e)

ip = "217.60.7.164"
password = "NB1HLtGC9Kb_C2k8A4"

print("--- RECON ---")
print("Uptime & OS:")
print(run_remote(ip, password, "uptime && cat /etc/os-release"))

print("\n--- TMUX CHECK ---")
print(run_remote(ip, password, "which tmux || echo 'tmux not found'"))

print("\n--- DOCKER CHECK ---")
print(run_remote(ip, password, "which docker || echo 'docker not found'"))

print("\n--- PROCESSES (kolibri) ---")
print(run_remote(ip, password, "ps aux | grep -i kolibri | grep -v grep"))

print("\n--- NETWORK PORTS ---")
print(run_remote(ip, password, "ss -tulpn"))
