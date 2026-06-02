#!/usr/bin/env bash
# =============================================================================
# CloseCrab-Unified macOS Build Script
# Builds universal binary (arm64 + x86_64), creates .app bundle and .dmg
# Version: 0.2.0 (Team Mode release)
#
# Requirements:
#   - Xcode Command Line Tools (xcode-select --install)
#   - CMake 3.20+ (brew install cmake)
#   - create-dmg (brew install create-dmg) OR hdiutil (built-in)
#
# Usage:
#   ./scripts/build-macos.sh [--release|--debug] [--no-dmg] [--no-sign]
# =============================================================================

set -euo pipefail

# --- Configuration ---
APP_NAME="CloseCrab-Unified"
APP_VERSION="0.2.6"
BUNDLE_ID="com.blitzball996.closecrab-unified"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-macos"
APP_BUNDLE="${BUILD_DIR}/${APP_NAME}.app"
DMG_OUTPUT="${BUILD_DIR}/${APP_NAME}-${APP_VERSION}-macOS-universal.dmg"

# --- Parse arguments ---
BUILD_TYPE="Release"
CREATE_DMG=true
SIGN_APP=true

for arg in "$@"; do
    case "$arg" in
        --debug)    BUILD_TYPE="Debug" ;;
        --release)  BUILD_TYPE="Release" ;;
        --no-dmg)   CREATE_DMG=false ;;
        --no-sign)  SIGN_APP=false ;;
        --help|-h)
            echo "Usage: $0 [--release|--debug] [--no-dmg] [--no-sign]"
            exit 0
            ;;
    esac
done

echo "============================================"
echo " ${APP_NAME} macOS Build"
echo " Version: ${APP_VERSION}"
echo " Type: ${BUILD_TYPE}"
echo "============================================"

# --- Step 1: CMake configure for universal binary ---
echo ""
echo "[1/5] Configuring CMake (universal binary: arm64 + x86_64)..."

# macOS does not use CUDA; build CPU-only
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0" \
    -DUSE_CUDA=OFF \
    -DUSE_ONNX_GPU=OFF \
    -DBUILD_TESTS=OFF

# --- Step 2: Build ---
echo ""
echo "[2/5] Building..."

cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j "$(sysctl -n hw.ncpu)"

echo "Build complete."

# --- Step 3: Create .app bundle ---
echo ""
echo "[3/5] Creating .app bundle..."

# Clean previous bundle
rm -rf "${APP_BUNDLE}"

# Create bundle directory structure
mkdir -p "${APP_BUNDLE}/Contents/MacOS"
mkdir -p "${APP_BUNDLE}/Contents/Resources"
mkdir -p "${APP_BUNDLE}/Contents/Resources/config"
mkdir -p "${APP_BUNDLE}/Contents/Resources/docs"

# Copy executable
cp "${BUILD_DIR}/closecrab" "${APP_BUNDLE}/Contents/MacOS/${APP_NAME}"

# Copy shared libraries if any
find "${BUILD_DIR}" -name "*.dylib" -exec cp {} "${APP_BUNDLE}/Contents/MacOS/" \; 2>/dev/null || true

# Copy config
if [ -d "${PROJECT_ROOT}/config" ]; then
    cp -r "${PROJECT_ROOT}/config/"* "${APP_BUNDLE}/Contents/Resources/config/"
fi

# Copy docs
if [ -d "${PROJECT_ROOT}/docs" ]; then
    cp -r "${PROJECT_ROOT}/docs/"* "${APP_BUNDLE}/Contents/Resources/docs/"
fi

# Copy icon (convert .ico to .icns if needed, or use placeholder)
if [ -f "${PROJECT_ROOT}/icons/closecrab.icns" ]; then
    cp "${PROJECT_ROOT}/icons/closecrab.icns" "${APP_BUNDLE}/Contents/Resources/AppIcon.icns"
elif [ -f "${PROJECT_ROOT}/icons/closecrab.ico" ]; then
    # Use sips to create a basic icon from the .ico if possible
    echo "  Note: .icns not found, using .ico (convert with iconutil for production)"
    cp "${PROJECT_ROOT}/icons/closecrab.ico" "${APP_BUNDLE}/Contents/Resources/AppIcon.ico"
fi

# Create Info.plist
cat > "${APP_BUNDLE}/Contents/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleDisplayName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>${BUNDLE_ID}</string>
    <key>CFBundleVersion</key>
    <string>${APP_VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${APP_VERSION}</string>
    <key>CFBundleExecutable</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleSignature</key>
    <string>CCRB</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.developer-tools</string>
    <key>NSHumanReadableCopyright</key>
    <string>Copyright 2024-2025 Blitzball996. MIT License.</string>
</dict>
</plist>
PLIST

# Create PkgInfo
echo -n "APPLCCRB" > "${APP_BUNDLE}/Contents/PkgInfo"

echo "  Bundle created: ${APP_BUNDLE}"

# --- Step 4: Ad-hoc code signing ---
if [ "${SIGN_APP}" = true ]; then
    echo ""
    echo "[4/5] Signing with ad-hoc signature..."

    # Sign all dylibs first, then the main executable
    find "${APP_BUNDLE}/Contents/MacOS" -name "*.dylib" -exec \
        codesign --force --sign - --timestamp=none {} \; 2>/dev/null || true

    codesign --force --deep --sign - --timestamp=none "${APP_BUNDLE}"
    echo "  Signed (ad-hoc, no Apple Developer account required)"
else
    echo ""
    echo "[4/5] Skipping code signing (--no-sign)"
fi

# --- Step 5: Create DMG ---
if [ "${CREATE_DMG}" = true ]; then
    echo ""
    echo "[5/5] Creating DMG installer..."

    # Remove old DMG if exists
    rm -f "${DMG_OUTPUT}"

    # Try create-dmg first (prettier result), fall back to hdiutil
    if command -v create-dmg &>/dev/null; then
        create-dmg \
            --volname "${APP_NAME} ${APP_VERSION}" \
            --volicon "${APP_BUNDLE}/Contents/Resources/AppIcon.icns" \
            --window-pos 200 120 \
            --window-size 600 400 \
            --icon-size 100 \
            --icon "${APP_NAME}.app" 150 185 \
            --app-drop-link 450 185 \
            --hide-extension "${APP_NAME}.app" \
            "${DMG_OUTPUT}" \
            "${APP_BUNDLE}" \
        || {
            echo "  create-dmg failed, falling back to hdiutil..."
            # Fallback: use hdiutil directly
            STAGING_DIR="${BUILD_DIR}/dmg-staging"
            rm -rf "${STAGING_DIR}"
            mkdir -p "${STAGING_DIR}"
            cp -r "${APP_BUNDLE}" "${STAGING_DIR}/"
            ln -s /Applications "${STAGING_DIR}/Applications"

            hdiutil create -volname "${APP_NAME} ${APP_VERSION}" \
                -srcfolder "${STAGING_DIR}" \
                -ov -format UDZO \
                "${DMG_OUTPUT}"

            rm -rf "${STAGING_DIR}"
        }
    else
        # Use hdiutil (always available on macOS)
        STAGING_DIR="${BUILD_DIR}/dmg-staging"
        rm -rf "${STAGING_DIR}"
        mkdir -p "${STAGING_DIR}"
        cp -r "${APP_BUNDLE}" "${STAGING_DIR}/"
        ln -s /Applications "${STAGING_DIR}/Applications"

        hdiutil create -volname "${APP_NAME} ${APP_VERSION}" \
            -srcfolder "${STAGING_DIR}" \
            -ov -format UDZO \
            "${DMG_OUTPUT}"

        rm -rf "${STAGING_DIR}"
    fi

    echo "  DMG created: ${DMG_OUTPUT}"
else
    echo ""
    echo "[5/5] Skipping DMG creation (--no-dmg)"
fi

# --- Done ---
echo ""
echo "============================================"
echo " Build complete!"
echo " App:  ${APP_BUNDLE}"
if [ "${CREATE_DMG}" = true ]; then
    echo " DMG:  ${DMG_OUTPUT}"
fi
echo "============================================"
