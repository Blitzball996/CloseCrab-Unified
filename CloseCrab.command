#!/bin/bash
# CloseCrab Launcher for macOS/Linux
# Double-click this file or run: ./CloseCrab.command

clear
echo ""
echo "  ╔══════════════════════════════════════╗"
echo "  ║        CloseCrab Launcher            ║"
echo "  ║   AI Coding Assistant Control Panel  ║"
echo "  ╚══════════════════════════════════════╝"
echo ""

# Check Node.js
if ! command -v node &> /dev/null; then
    echo "  [ERROR] Node.js not found!"
    echo "  Install: brew install node (macOS) or apt install nodejs (Linux)"
    echo ""
    read -p "  Press Enter to exit..."
    exit 1
fi

# Find CloseCrab-Web
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WEB_DIR="$SCRIPT_DIR/../CloseCrab-Web"
if [ ! -f "$WEB_DIR/bin/cli.js" ]; then
    WEB_DIR="$SCRIPT_DIR/CloseCrab-Web"
fi
if [ ! -f "$WEB_DIR/bin/cli.js" ]; then
    echo "  [ERROR] CloseCrab-Web not found!"
    echo "  Expected at: $WEB_DIR"
    read -p "  Press Enter to exit..."
    exit 1
fi

# Start server
echo "  Starting CloseCrab-Web server..."
cd "$WEB_DIR"
node bin/cli.js --port 3000 &
SERVER_PID=$!

sleep 2

# Open browser
echo "  Opening Dashboard..."
if [[ "$OSTYPE" == "darwin"* ]]; then
    open http://localhost:3000
else
    xdg-open http://localhost:3000 2>/dev/null || echo "  Open http://localhost:3000 in your browser"
fi

echo ""
echo "  CloseCrab is running!"
echo "  Dashboard: http://localhost:3000"
echo ""
echo "  Press Ctrl+C or close this window to stop."
echo ""

# Wait for user to stop
trap "kill $SERVER_PID 2>/dev/null; echo '  Stopped.'; exit 0" INT TERM
wait $SERVER_PID
