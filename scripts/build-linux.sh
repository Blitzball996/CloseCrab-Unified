#!/usr/bin/env bash
# =============================================================================
# CloseCrab-Unified Linux Build Script
# Builds native binary, creates AppImage and .deb package
# Version: 0.2.0 (Team Mode release)
#
# Requirements:
#   - GCC 9+ or Clang 10+
#   - CMake 3.20+ (apt install cmake)
#   - For AppImage: linuxdeploy (downloaded automatically)
#   - For .deb: dpkg-deb (apt install dpkg)
#
# Usage:
#   ./scripts/build-linux.sh [--release|--debug] [--no-appimage] [--no-deb]
# =============================================================================

set -euo pipefail

# --- Configuration ---
APP_NAME="closecrab"
APP_DISPLAY_NAME="CloseCrab-Unified"
APP_VERSION="0.2.3"
APP_DESCRIPTION="AI-powered coding assistant with Team Mode, multi-agent coordination, and local LLM support"
MAINTAINER="Blitzball996 <blitzball996@users.noreply.github.com>"
HOMEPAGE="https://github.com/Blitzball996/CloseCrab-Unified"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-linux"
PACKAGE_DIR="${BUILD_DIR}/package"
ARCH="$(uname -m)"

# --- Parse arguments ---
BUILD_TYPE="Release"
CREATE_APPIMAGE=true
CREATE_DEB=true

for arg in "$@"; do
    case "$arg" in
        --debug)         BUILD_TYPE="Debug" ;;
        --release)       BUILD_TYPE="Release" ;;
        --no-appimage)   CREATE_APPIMAGE=false ;;
        --no-deb)        CREATE_DEB=false ;;
        --help|-h)
            echo "Usage: $0 [--release|--debug] [--no-appimage] [--no-deb]"
            exit 0
            ;;
    esac
done

echo "============================================"
echo " ${APP_DISPLAY_NAME} Linux Build"
echo " Version: ${APP_VERSION}"
echo " Type: ${BUILD_TYPE}"
echo " Arch: ${ARCH}"
echo "============================================"

# --- Step 1: CMake configure ---
echo ""
echo "[1/5] Configuring CMake..."

# Linux does not use CUDA; build CPU-only
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="/usr" \
    -DUSE_CUDA=OFF \
    -DUSE_ONNX_GPU=OFF \
    -DBUILD_TESTS=OFF

# --- Step 2: Build ---
echo ""
echo "[2/5] Building..."

cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j "$(nproc)"

echo "  Build complete."

# --- Step 3: Create desktop file and icon ---
echo ""
echo "[3/5] Preparing packaging assets..."

# Create .desktop file
mkdir -p "${BUILD_DIR}/assets"
cat > "${BUILD_DIR}/assets/${APP_NAME}.desktop" << DESKTOP
[Desktop Entry]
Type=Application
Name=${APP_DISPLAY_NAME}
Comment=${APP_DESCRIPTION}
Exec=${APP_NAME}
Icon=${APP_NAME}
Terminal=true
Categories=Development;IDE;
Keywords=AI;coding;assistant;LLM;
StartupNotify=false
DESKTOP

# Copy icon (use PNG for Linux; convert from .ico if needed)
if [ -f "${PROJECT_ROOT}/icons/closecrab.png" ]; then
    cp "${PROJECT_ROOT}/icons/closecrab.png" "${BUILD_DIR}/assets/${APP_NAME}.png"
elif [ -f "${PROJECT_ROOT}/icons/closecrab.ico" ]; then
    # Try to convert .ico to .png using ImageMagick if available
    if command -v convert &>/dev/null; then
        convert "${PROJECT_ROOT}/icons/closecrab.ico[0]" -resize 256x256 \
            "${BUILD_DIR}/assets/${APP_NAME}.png"
    else
        echo "  Warning: No .png icon found and ImageMagick not available."
        echo "  Using placeholder icon. Install imagemagick to convert .ico"
        # Create a minimal 1x1 PNG as placeholder
        printf '\x89PNG\r\n\x1a\n' > "${BUILD_DIR}/assets/${APP_NAME}.png"
    fi
fi

# --- Step 4: Create AppImage ---
if [ "${CREATE_APPIMAGE}" = true ]; then
    echo ""
    echo "[4/5] Creating AppImage..."

    APPIMAGE_DIR="${BUILD_DIR}/AppDir"
    rm -rf "${APPIMAGE_DIR}"
    mkdir -p "${APPIMAGE_DIR}/usr/bin"
    mkdir -p "${APPIMAGE_DIR}/usr/share/applications"
    mkdir -p "${APPIMAGE_DIR}/usr/share/icons/hicolor/256x256/apps"
    mkdir -p "${APPIMAGE_DIR}/usr/share/${APP_NAME}/config"
    mkdir -p "${APPIMAGE_DIR}/usr/share/${APP_NAME}/docs"

    # Copy binary
    cp "${BUILD_DIR}/${APP_NAME}" "${APPIMAGE_DIR}/usr/bin/"
    chmod +x "${APPIMAGE_DIR}/usr/bin/${APP_NAME}"

    # Copy shared libraries
    find "${BUILD_DIR}" -name "*.so*" -exec cp {} "${APPIMAGE_DIR}/usr/bin/" \; 2>/dev/null || true

    # Copy desktop file and icon
    cp "${BUILD_DIR}/assets/${APP_NAME}.desktop" "${APPIMAGE_DIR}/usr/share/applications/"
    cp "${BUILD_DIR}/assets/${APP_NAME}.desktop" "${APPIMAGE_DIR}/"
    if [ -f "${BUILD_DIR}/assets/${APP_NAME}.png" ]; then
        cp "${BUILD_DIR}/assets/${APP_NAME}.png" \
            "${APPIMAGE_DIR}/usr/share/icons/hicolor/256x256/apps/"
        cp "${BUILD_DIR}/assets/${APP_NAME}.png" "${APPIMAGE_DIR}/"
    fi

    # Copy config and docs
    if [ -d "${PROJECT_ROOT}/config" ]; then
        cp -r "${PROJECT_ROOT}/config/"* "${APPIMAGE_DIR}/usr/share/${APP_NAME}/config/"
    fi
    if [ -d "${PROJECT_ROOT}/docs" ]; then
        cp -r "${PROJECT_ROOT}/docs/"* "${APPIMAGE_DIR}/usr/share/${APP_NAME}/docs/"
    fi

    # Download linuxdeploy if not present
    LINUXDEPLOY="${BUILD_DIR}/linuxdeploy-${ARCH}.AppImage"
    if [ ! -f "${LINUXDEPLOY}" ]; then
        echo "  Downloading linuxdeploy..."
        curl -fsSL -o "${LINUXDEPLOY}" \
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"
        chmod +x "${LINUXDEPLOY}"
    fi

    # Build AppImage
    APPIMAGE_OUTPUT="${BUILD_DIR}/${APP_DISPLAY_NAME}-${APP_VERSION}-${ARCH}.AppImage"
    OUTPUT="${APPIMAGE_OUTPUT}" "${LINUXDEPLOY}" \
        --appdir "${APPIMAGE_DIR}" \
        --desktop-file "${BUILD_DIR}/assets/${APP_NAME}.desktop" \
        --icon-file "${BUILD_DIR}/assets/${APP_NAME}.png" \
        --output appimage \
    || {
        echo "  linuxdeploy failed. Trying manual AppImage creation..."
        # Fallback: create AppRun and package manually
        cat > "${APPIMAGE_DIR}/AppRun" << 'APPRUN'
#!/bin/bash
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/bin:${LD_LIBRARY_PATH}"
exec "${HERE}/usr/bin/closecrab" "$@"
APPRUN
        chmod +x "${APPIMAGE_DIR}/AppRun"
        echo "  Manual AppDir created at: ${APPIMAGE_DIR}"
        echo "  Use appimagetool to finalize: appimagetool ${APPIMAGE_DIR} ${APPIMAGE_OUTPUT}"
    }

    echo "  AppImage: ${APPIMAGE_OUTPUT}"
else
    echo ""
    echo "[4/5] Skipping AppImage (--no-appimage)"
fi

# --- Step 5: Create .deb package ---
if [ "${CREATE_DEB}" = true ]; then
    echo ""
    echo "[5/5] Creating .deb package..."

    # Map architecture
    case "${ARCH}" in
        x86_64)  DEB_ARCH="amd64" ;;
        aarch64) DEB_ARCH="arm64" ;;
        armv7l)  DEB_ARCH="armhf" ;;
        *)       DEB_ARCH="${ARCH}" ;;
    esac

    DEB_DIR="${BUILD_DIR}/deb-package"
    DEB_OUTPUT="${BUILD_DIR}/${APP_NAME}_${APP_VERSION}_${DEB_ARCH}.deb"
    rm -rf "${DEB_DIR}"

    # Create directory structure
    mkdir -p "${DEB_DIR}/DEBIAN"
    mkdir -p "${DEB_DIR}/usr/bin"
    mkdir -p "${DEB_DIR}/usr/share/applications"
    mkdir -p "${DEB_DIR}/usr/share/icons/hicolor/256x256/apps"
    mkdir -p "${DEB_DIR}/usr/share/${APP_NAME}/config"
    mkdir -p "${DEB_DIR}/usr/share/${APP_NAME}/docs"
    mkdir -p "${DEB_DIR}/usr/share/doc/${APP_NAME}"

    # Copy binary
    cp "${BUILD_DIR}/${APP_NAME}" "${DEB_DIR}/usr/bin/"
    chmod 755 "${DEB_DIR}/usr/bin/${APP_NAME}"

    # Copy shared libraries to /usr/lib
    if find "${BUILD_DIR}" -name "*.so*" -print -quit 2>/dev/null | grep -q .; then
        mkdir -p "${DEB_DIR}/usr/lib/${APP_NAME}"
        find "${BUILD_DIR}" -maxdepth 1 -name "*.so*" -exec cp {} "${DEB_DIR}/usr/lib/${APP_NAME}/" \;
    fi

    # Copy desktop file and icon
    cp "${BUILD_DIR}/assets/${APP_NAME}.desktop" "${DEB_DIR}/usr/share/applications/"
    if [ -f "${BUILD_DIR}/assets/${APP_NAME}.png" ]; then
        cp "${BUILD_DIR}/assets/${APP_NAME}.png" \
            "${DEB_DIR}/usr/share/icons/hicolor/256x256/apps/"
    fi

    # Copy config and docs
    if [ -d "${PROJECT_ROOT}/config" ]; then
        cp -r "${PROJECT_ROOT}/config/"* "${DEB_DIR}/usr/share/${APP_NAME}/config/"
    fi
    if [ -d "${PROJECT_ROOT}/docs" ]; then
        cp -r "${PROJECT_ROOT}/docs/"* "${DEB_DIR}/usr/share/${APP_NAME}/docs/"
    fi

    # Copy license
    if [ -f "${PROJECT_ROOT}/LICENSE" ]; then
        cp "${PROJECT_ROOT}/LICENSE" "${DEB_DIR}/usr/share/doc/${APP_NAME}/copyright"
    fi

    # Calculate installed size (in KB)
    INSTALLED_SIZE=$(du -sk "${DEB_DIR}" | cut -f1)

    # Create control file
    cat > "${DEB_DIR}/DEBIAN/control" << CONTROL
Package: ${APP_NAME}
Version: ${APP_VERSION}
Section: devel
Priority: optional
Architecture: ${DEB_ARCH}
Installed-Size: ${INSTALLED_SIZE}
Maintainer: ${MAINTAINER}
Homepage: ${HOMEPAGE}
Description: ${APP_DESCRIPTION}
 CloseCrab-Unified is an AI-powered coding assistant that supports
 local LLM inference, Anthropic API, and OpenAI-compatible endpoints.
 Features Team Mode for multi-agent coordination, shared knowledge,
 51 tools, 83 commands, and CUDA acceleration (Windows only).
Depends: libc6 (>= 2.31), libstdc++6 (>= 10), libcurl4 (>= 7.68), zlib1g (>= 1.2.11)
CONTROL

    # Create postinst script
    cat > "${DEB_DIR}/DEBIAN/postinst" << 'POSTINST'
#!/bin/sh
set -e
# Update icon cache
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor 2>/dev/null || true
fi
# Update desktop database
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications 2>/dev/null || true
fi
POSTINST
    chmod 755 "${DEB_DIR}/DEBIAN/postinst"

    # Build .deb
    dpkg-deb --build --root-owner-group "${DEB_DIR}" "${DEB_OUTPUT}"

    echo "  .deb package: ${DEB_OUTPUT}"
else
    echo ""
    echo "[5/5] Skipping .deb package (--no-deb)"
fi

# --- Create install.sh for manual installation ---
echo ""
echo "Creating install.sh..."

cat > "${BUILD_DIR}/install.sh" << 'INSTALL_SCRIPT'
#!/usr/bin/env bash
# =============================================================================
# CloseCrab-Unified Manual Installation Script
# Run as root or with sudo for system-wide install
# =============================================================================

set -euo pipefail

APP_NAME="closecrab"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"

# Check if running as root for system install
if [ "$(id -u)" -ne 0 ] && [ "${INSTALL_PREFIX}" = "/usr/local" ]; then
    echo "This script installs to ${INSTALL_PREFIX}. Run with sudo or set INSTALL_PREFIX."
    echo "  Example: INSTALL_PREFIX=~/.local ./install.sh"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Installing ${APP_NAME} to ${INSTALL_PREFIX}..."

# Install binary
install -Dm755 "${SCRIPT_DIR}/${APP_NAME}" "${INSTALL_PREFIX}/bin/${APP_NAME}"

# Install config
mkdir -p "${INSTALL_PREFIX}/share/${APP_NAME}/config"
if [ -d "${SCRIPT_DIR}/config" ]; then
    cp -r "${SCRIPT_DIR}/config/"* "${INSTALL_PREFIX}/share/${APP_NAME}/config/"
fi

# Install docs
mkdir -p "${INSTALL_PREFIX}/share/${APP_NAME}/docs"
if [ -d "${SCRIPT_DIR}/docs" ]; then
    cp -r "${SCRIPT_DIR}/docs/"* "${INSTALL_PREFIX}/share/${APP_NAME}/docs/"
fi

# Install desktop file (system-wide only)
if [ "${INSTALL_PREFIX}" = "/usr/local" ] || [ "${INSTALL_PREFIX}" = "/usr" ]; then
    if [ -f "${SCRIPT_DIR}/${APP_NAME}.desktop" ]; then
        install -Dm644 "${SCRIPT_DIR}/${APP_NAME}.desktop" \
            "/usr/share/applications/${APP_NAME}.desktop"
    fi
    if [ -f "${SCRIPT_DIR}/${APP_NAME}.png" ]; then
        install -Dm644 "${SCRIPT_DIR}/${APP_NAME}.png" \
            "/usr/share/icons/hicolor/256x256/apps/${APP_NAME}.png"
    fi
fi

echo "Installation complete!"
echo "  Binary: ${INSTALL_PREFIX}/bin/${APP_NAME}"
echo "  Config: ${INSTALL_PREFIX}/share/${APP_NAME}/config/"
echo ""
echo "Run '${APP_NAME}' to start."
INSTALL_SCRIPT

chmod +x "${BUILD_DIR}/install.sh"
echo "  install.sh created at: ${BUILD_DIR}/install.sh"

# --- Done ---
echo ""
echo "============================================"
echo " Build complete!"
echo " Binary: ${BUILD_DIR}/${APP_NAME}"
if [ "${CREATE_APPIMAGE}" = true ]; then
    echo " AppImage: ${BUILD_DIR}/${APP_DISPLAY_NAME}-${APP_VERSION}-${ARCH}.AppImage"
fi
if [ "${CREATE_DEB}" = true ]; then
    echo " .deb: ${BUILD_DIR}/${APP_NAME}_${APP_VERSION}_*.deb"
fi
echo " install.sh: ${BUILD_DIR}/install.sh"
echo "============================================"
