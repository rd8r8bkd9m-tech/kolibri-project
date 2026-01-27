#!/bin/bash
# Helper script to start the desktop manually

# Clean up any locks
rm -f /tmp/.X1-lock
rm -f /tmp/.X11-unix/X1
vncserver -kill :1 2>/dev/null

# Start VNC
echo "Starting VNC Server..."
chmod +x ~/.vnc/xstartup
vncserver :1 -geometry 1280x800 -depth 24

# Start noVNC
echo "Starting noVNC Bridge..."
pkill -f websockify
websockify --web=/usr/share/novnc/ 6080 localhost:5901 > /tmp/novnc.log 2>&1 &

echo "Desktop is running!"
echo "Connect via Browser: Port 6080"
echo "Connect via VNC Client: Port 5901"
echo "Password: 12345678"
