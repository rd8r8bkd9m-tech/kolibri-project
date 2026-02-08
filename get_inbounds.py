import pexpect

def run_remote(command):
    child = pexpect.spawn(f'ssh -o StrictHostKeyChecking=no root@217.60.7.164 "{command}"', timeout=60)
    child.expect('password:')
    child.sendline('NB1HLtGC9Kb_C2k8A4')
    child.expect(pexpect.EOF)
    return child.before.decode()

query = "SELECT port, protocol, remark FROM inbounds;"
result = run_remote(f"sqlite3 /root/3x-ui/db/x-ui.db '{query}'")
print(result)
