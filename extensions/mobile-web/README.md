# CloseCrab Mobile Remote Control

Control CloseCrab from your phone. See real-time progress, approve/deny permissions, send messages.

## Setup

### Local Network (same WiFi)
1. Start CloseCrab-Unified on your PC
2. Open `http://<PC-IP>:9001/mobile` on your phone
3. Done!

### Remote Access (from anywhere)
Option A: Cloudflare Tunnel (recommended, free)
```bash
# Install cloudflared on your PC
cloudflared tunnel --url http://localhost:9001
# Opens a public URL like https://xxx.trycloudflare.com
# Open that URL + /mobile on your phone
```

Option B: Tailscale VPN
```bash
# Install Tailscale on PC and phone
# Access via Tailscale IP: http://100.x.x.x:9001/mobile
```

Option C: ngrok
```bash
ngrok http 9001
# Use the ngrok URL on your phone
```

## Features
- Real-time streaming text display
- Tool execution status with timing
- Permission request buttons (Allow/Deny/Allow All)
- Phone vibration on permission requests
- Auto-reconnect on connection loss
- Dark theme, mobile-optimized
- PWA: "Add to Home Screen" for app-like experience

## Security
- Add authentication token in CloseCrab config
- Use HTTPS (Cloudflare Tunnel provides this automatically)
- Don't expose port 9001 directly to the internet without auth
