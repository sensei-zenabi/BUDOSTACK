#!/data/data/com.termux/files/usr/bin/bash
# =============================================================================
# Script Name: setup_termux.sh
# Description: Termux installer for BUDOSTACK dependencies. It uses Termux's
#              pkg package manager and builds the Termux target, which excludes
#              apps/terminal.
#
# Usage:
#   chmod +x setup_termux.sh
#   ./setup_termux.sh
#
# Requirements:
#   - Run inside Termux on Android.
# =============================================================================

PACKAGE_MANAGER="pkg install -y"

PACKAGES=(
    "clang"      # C compiler for Termux builds
    "make"       # Required to run the BUDOSTACK makefile
    "pkg-config" # Required for optional dependency detection
    "git"        # Mandatory: general requirement
    "curl"       # Optional: apps that fetch remote content
    "zip"        # Mandatory: commands: unpack, pack
)

prompt_user() {
    echo "This script will install the following Termux packages:"
    for pkg in "${PACKAGES[@]}"; do
        echo "  - $pkg"
    done
    echo
    read -r -p "Do you want to proceed? (y/n): " response
    case "$response" in
        [yY][eE][sS]|[yY])
            echo "Proceeding with installation..."
            ;;
        *)
            echo "Installation aborted by user."
            exit 1
            ;;
    esac
}

install_package() {
    local package="$1"
    echo "Installing $package..."
    if ! $PACKAGE_MANAGER "$package"; then
        echo "Error: Installation of $package failed."
    else
        echo "$package installed successfully."
    fi
}

install_all_packages() {
    for pkg in "${PACKAGES[@]}"; do
        install_package "$pkg"
    done
}

main() {
    if [ -z "${TERMUX_VERSION:-}" ] && [ "${PREFIX:-}" != "/data/data/com.termux/files/usr" ]; then
        echo "Warning: This installer is intended for Termux."
    fi

    prompt_user
    install_all_packages

    echo "All requested packages have been processed."
    echo ""
    echo "Building BUDOSTACK for Termux..."
    make clean termux CC=clang
    echo "BUDOSTACK Termux setup finished successfully!"
}

main
