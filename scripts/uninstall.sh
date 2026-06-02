#!/usr/bin/env bash
# =============================================================================
# CloseCrab — one-line macOS / Linux uninstaller
#
#   curl -fsSL https://raw.githubusercontent.com/Blitzball996/CloseCrab-Unified/main/scripts/uninstall.sh | bash
#
# Removes the `closecrab` binary from every location the installer/pkg may have
# put it, plus the package receipt. By default it KEEPS your data & config
# (~/.crab). Pass --purge to also delete ~/.crab (config, db, logs, sessions).
# =============================================================================
set -uo pipefail

PURGE=false
for arg in "$@"; do
  case "$arg" in
    --purge) PURGE=true ;;
    -h|--help) echo "Usage: uninstall.sh [--purge]"; echo "  --purge  also delete ~/.crab (config, data, logs)"; exit 0 ;;
  esac
done

echo "==> Uninstalling CloseCrab ..."

# Candidate binary locations (tar.gz install, pkg install, manual symlink).
BINS=(
  "${HOME}/.local/bin/closecrab"
  "/usr/local/bin/closecrab"
  "/usr/local/bin/closecrab-unified"   # legacy name (pre-0.2.2)
)
removed_any=false
for b in "${BINS[@]}"; do
  if [ -e "$b" ] || [ -L "$b" ]; then
    if rm -f "$b" 2>/dev/null; then
      echo "    removed $b"
    elif sudo rm -f "$b" 2>/dev/null; then
      echo "    removed $b (sudo)"
    else
      echo "    WARN: could not remove $b"
    fi
    removed_any=true
  fi
done
$removed_any || echo "    (no closecrab binary found in standard locations)"

# Forget the macOS package receipt so a future pkg reinstall is clean.
if command -v pkgutil >/dev/null 2>&1; then
  if pkgutil --pkgs | grep -q '^com.blitzball.closecrab$'; then
    sudo pkgutil --forget com.blitzball.closecrab >/dev/null 2>&1 \
      && echo "    forgot pkg receipt com.blitzball.closecrab"
  fi
fi

# Data & config.
if [ "$PURGE" = true ]; then
  if [ -d "${HOME}/.crab" ]; then
    rm -rf "${HOME}/.crab" && echo "    purged ${HOME}/.crab (config, data, logs)"
  fi
else
  if [ -d "${HOME}/.crab" ]; then
    echo "    kept your data at ${HOME}/.crab (run with --purge to delete it)"
  fi
fi

echo ""
echo "============================================================"
echo " CloseCrab uninstalled."
$PURGE || echo " Your config/data remain in ~/.crab (use --purge to remove)."
echo "============================================================"
