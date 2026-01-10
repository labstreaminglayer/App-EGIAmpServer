#!/bin/bash
# =============================================================================
# Apple Code Signing and Notarization Script
# =============================================================================
# This script handles identity-based signing and notarization for macOS apps.
# It's designed to be called from CI (GitHub Actions) after the build.
#
# Usage:
#   ./scripts/sign_and_notarize.sh <path_to_app_or_binary> [--notarize]
#
# Environment Variables (set in CI):
#   APPLE_CODE_SIGN_IDENTITY_APP  - Developer ID Application certificate name
#   APPLE_NOTARIZE_KEYCHAIN_PROFILE - notarytool credential profile name
#   ENTITLEMENTS_FILE             - Path to entitlements file (optional)
#
# Examples:
#   ./scripts/sign_and_notarize.sh build/install/EGIAmpServer.app
#   ./scripts/sign_and_notarize.sh build/install/EGIAmpServer.app --notarize
# =============================================================================

set -e

APP_PATH="$1"
DO_NOTARIZE=false

if [[ "$2" == "--notarize" ]]; then
    DO_NOTARIZE=true
fi

if [[ -z "$APP_PATH" ]]; then
    echo "Usage: $0 <path_to_app_or_binary> [--notarize]"
    exit 1
fi

if [[ ! -e "$APP_PATH" ]]; then
    echo "Error: $APP_PATH does not exist"
    exit 1
fi

SIGN_IDENTITY="${APPLE_CODE_SIGN_IDENTITY_APP:--}"
ENTITLEMENTS_ARG=""

if [[ -n "${ENTITLEMENTS_FILE}" && -f "${ENTITLEMENTS_FILE}" ]]; then
    ENTITLEMENTS_ARG="--entitlements ${ENTITLEMENTS_FILE}"
elif [[ -f "$(dirname "$0")/../app.entitlements" ]]; then
    ENTITLEMENTS_ARG="--entitlements $(dirname "$0")/../app.entitlements"
fi

echo "=== Code Signing ==="
echo "Target: $APP_PATH"
echo "Identity: $SIGN_IDENTITY"
echo "Entitlements: ${ENTITLEMENTS_ARG:-none}"

if [[ -d "$APP_PATH" ]]; then
    codesign --force --deep --sign "$SIGN_IDENTITY" \
        --options runtime \
        $ENTITLEMENTS_ARG \
        "$APP_PATH"
else
    codesign --force --sign "$SIGN_IDENTITY" \
        --options runtime \
        $ENTITLEMENTS_ARG \
        "$APP_PATH"
fi

echo "Verifying signature..."
codesign --verify --verbose "$APP_PATH"

if [[ "$DO_NOTARIZE" == true ]]; then
    if [[ "$SIGN_IDENTITY" == "-" ]]; then
        echo "Warning: Cannot notarize with ad-hoc signature. Skipping notarization."
        exit 0
    fi

    if [[ -z "$APPLE_NOTARIZE_KEYCHAIN_PROFILE" ]]; then
        echo "Error: APPLE_NOTARIZE_KEYCHAIN_PROFILE not set"
        exit 1
    fi

    echo ""
    echo "=== Notarizing ==="

    BASENAME=$(basename "$APP_PATH")
    ZIP_PATH="/tmp/${BASENAME%.*}_notarize.zip"

    echo "Creating zip for submission: $ZIP_PATH"
    ditto -c -k --keepParent "$APP_PATH" "$ZIP_PATH"

    echo "Submitting to Apple notarization service..."
    xcrun notarytool submit "$ZIP_PATH" \
        --keychain-profile "$APPLE_NOTARIZE_KEYCHAIN_PROFILE" \
        --wait

    if [[ -d "$APP_PATH" ]]; then
        echo "Stapling notarization ticket..."
        xcrun stapler staple "$APP_PATH"
        xcrun stapler validate "$APP_PATH"
    fi

    rm -f "$ZIP_PATH"

    echo ""
    echo "=== Notarization Complete ==="
fi

echo ""
echo "Done!"
