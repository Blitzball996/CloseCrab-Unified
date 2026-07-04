#!/usr/bin/env bash
# =============================================================================
# CloseCrab — one-line Linux installer
#
#   curl -fsSL https://raw.githubusercontent.com/Blitzball996/CloseCrab-Unified/main/scripts/install-linux.sh | bash
#
# Downloads the latest x86_64 build, installs it as the `closecrab` command,
# and adds it to PATH. No sudo required (installs to ~/.local/bin by default).
# =============================================================================
set -euo pipefail

REPO="Blitzball996/CloseCrab-Unified"
ASSET="CloseCrab-Linux-x86_64.tar.gz"
URL="https://github.com/${REPO}/releases/latest/download/${ASSET}"
BIN_DIR="${HOME}/.local/bin"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

echo "==> Downloading ${ASSET} ..."
if command -v curl >/dev/null 2>&1; then
  curl -fL --progress-bar -o "${TMP}/${ASSET}" "${URL}"
elif command -v wget >/dev/null 2>&1; then
  wget -q --show-progress -O "${TMP}/${ASSET}" "${URL}"
else
  echo "ERROR: curl or wget is required." >&2
  exit 1
fi

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
  BIN_DIR="/usr/local/bin"
  echo "    (falling back to ${BIN_DIR}, may prompt for password)"
  sudo install -m 0755 "${BIN_SRC}" "${BIN_DIR}/closecrab"
}

# Add to PATH if needed (writes to the login-shell's rc file).
case ":${PATH}:" in
  *":${BIN_DIR}:"*) ;;
  *)
    LOGIN_SHELL="$(basename "${SHELL:-/bin/bash}")"
    case "${LOGIN_SHELL}" in
      zsh)  SHELL_RC="${HOME}/.zshrc" ;;
      bash) SHELL_RC="${HOME}/.bashrc" ;;
      fish) SHELL_RC="${HOME}/.config/fish/config.fish" ;;
      *)    SHELL_RC="${HOME}/.profile" ;;
    esac
    # Also write ~/.profile as a universal fallback.
    for rc in "${SHELL_RC}" "${HOME}/.profile"; do
      if [ -f "${rc}" ] || [ "${rc}" = "${SHELL_RC}" ]; then
        if ! grep -qs "${BIN_DIR}" "${rc}" 2>/dev/null; then
          echo "export PATH=\"${BIN_DIR}:\$PATH\"" >> "${rc}"
          echo "    Added ${BIN_DIR} to PATH in ${rc}"
        fi
      fi
    done
    ;;
esac

echo ""
echo "============================================================"
echo " CloseCrab installed!  ->  ${BIN_DIR}/closecrab"
echo ""
echo " Run it with:           closecrab"
echo " Switch model:          closecrab --set-model claude-sonnet-5"
echo " (config auto-creates at ~/.crab/config.yaml on first run)"
echo ""
echo " If 'closecrab: command not found', open a new terminal or:"
echo "   export PATH=\"${BIN_DIR}:\$PATH\""
echo "============================================================"
