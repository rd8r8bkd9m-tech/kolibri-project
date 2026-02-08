import pexpect

files_to_transfer = [
    ("/workspaces/kolibri-project/build/kolibri_node", "/root/kolibri_node"),
    ("/workspaces/kolibri-project/kolibri.genome", "/root/kolibri.genome"),
    ("/workspaces/kolibri-project/knowledge_bootstrap.ks", "/root/knowledge_bootstrap.ks")
]

password = "NB1HLtGC9Kb_C2k8A4"
ip = "217.60.7.164"

for local, remote in files_to_transfer:
    print(f"Transferring {local} to {remote}...")
    child = pexpect.spawn(f'scp -o StrictHostKeyChecking=no {local} root@{ip}:{remote}', timeout=300)
    child.expect('password:')
    child.sendline(password)
    child.expect(pexpect.EOF)
    print(f"Finished {remote}")

# Make executable and setup service
child = pexpect.spawn(f'ssh -o StrictHostKeyChecking=no root@{ip} "chmod +x /root/kolibri_node"', timeout=60)
child.expect('password:')
child.sendline(password)
child.expect(pexpect.EOF)

print("All files transferred and permissions set.")
