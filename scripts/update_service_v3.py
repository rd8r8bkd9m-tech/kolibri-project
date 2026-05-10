import pexpect

def run_remote(command):
    child = pexpect.spawn(f'ssh -o StrictHostKeyChecking=no root@217.60.7.164 "{command}"', timeout=60)
    child.expect('password:')
    child.sendline('NB1HLtGC9Kb_C2k8A4')
    child.expect(pexpect.EOF)
    return child.before.decode()

service_content = """[Unit]
Description=Kolibri AI Node
After=network.target

[Service]
ExecStart=/root/kolibri_node --daemon --genome /root/kolibri.genome --bootstrap /root/knowledge_bootstrap.ks --listen 4050
Restart=always
User=root
WorkingDirectory=/root

[Install]
WantedBy=multi-user.target
"""

setup_script = f"""
cat <<EOF > /etc/systemd/system/kolibri-node.service
{service_content}
EOF
systemctl daemon-reload
systemctl restart kolibri-node
"""

print("Updating systemd service with --daemon flag...")
print(run_remote(setup_script))
print("Done.")
