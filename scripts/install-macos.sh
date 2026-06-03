#!/usr/bin/env bash
# =============================================================================
# CloseCrab — one-line macOS installer
#
#   curl -fsSL https://raw.githubusercontent.com/Blitzball996/CloseCrab-Unified/main/scripts/install-macos.sh | bash
#
# Downloads the latest universal build, installs it as the `closecrab` command,
# strips the quarantine flag, and prints next steps. No `sudo installer`, no
# manual xattr, no multi-step dance.
# =============================================================================
set -euo pipefail

REPO="Blitzball996/CloseCrab-Unified"
ASSET="CloseCrab-macOS-universal.tar.gz"
URL="https://github.com/${REPO}/releases/latest/download/${ASSET}"
# Prefer a no-sudo location; fall back to /usr/local/bin with sudo if needed.
BIN_DIR="${HOME}/.local/bin"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

echo "==> Downloading ${ASSET} ..."
curl -fL --progress-bar -o "${TMP}/${ASSET}" "${URL}"

echo "==> Extracting ..."
tar -xzf "${TMP}/${ASSET}" -C "${TMP}"
BIN_SRC="${TMP}/CloseCrab/closecrab"
if [ ! -f "${BIN_SRC}" ]; then
  echo "ERROR: 'closecrab' binary not found in the archive." >&2
  exit 1
fi

echo "==> Installing to ${BIN_DIR}/closecrab ..."
mkdir -p "${BIN_DIR}"
install -m 0755 "${BIN_SRC}" "${BIN_DIR}/closecrab" 2>/dev/null || {
  # If ~/.local/bin is not writable for some reason, fall back to /usr/local/bin.
  BIN_DIR="/usr/local/bin"
  echo "    (falling back to ${BIN_DIR}, may prompt for password)"
  sudo install -m 0755 "${BIN_SRC}" "${BIN_DIR}/closecrab"
}

echo "==> Removing quarantine flag ..."
xattr -dr com.apple.quarantine "${BIN_DIR}/closecrab" 2>/dev/null || true

# Ensure the install dir is on PATH (only needed for ~/.local/bin).
case ":${PATH}:" in
  *":${BIN_DIR}:"*) ;;
  *)
    # Pick the RC file by the user's LOGIN shell ($SHELL), NOT the shell running
    # this script. When invoked as `curl ... | bash`, BASH_VERSION is always set
    # even though the user's interactive shell is zsh — writing to ~/.bashrc then
    # never takes effect (the bug that broke `closecrab: command not found`).
    LOGIN_SHELL="$(basename "${SHELL:-/bin/zsh}")"
    case "${LOGIN_SHELL}" in
      zsh)  SHELL_RC="${HOME}/.zshrc" ;;
      bash) SHELL_RC="${HOME}/.bash_profile" ;;  # macOS login shell reads .bash_profile
      *)    SHELL_RC="${HOME}/.profile" ;;
    esac
    # Also write ~/.zshrc as a safety net on macOS (default shell is zsh since Catalina).
    for rc in "${SHELL_RC}" "${HOME}/.zshrc"; do
      if ! grep -qs "${BIN_DIR}" "${rc}" 2>/dev/null; then
        echo "export PATH=\"${BIN_DIR}:\$PATH\"" >> "${rc}"
        echo "    Added ${BIN_DIR} to PATH in ${rc}"
      fi
    done
    ;;
esac

echo ""
echo "============================================================"
echo " CloseCrab installed!  ->  ${BIN_DIR}/closecrab"
echo ""
echo " Run it with:           closecrab"
echo " (config auto-creates at ~/.crab/config.yaml on first run)"
echo ""
echo " If 'closecrab: command not found', open a new terminal or run:"
echo "   export PATH=\"${BIN_DIR}:\$PATH\""
echo "============================================================"
