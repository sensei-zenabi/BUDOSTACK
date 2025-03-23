#!/bin/bash
# =============================================================================
# Script Name: install-apps.sh
# Description: Template for installing applications using sudo and a package
#              manager. This script prompts the user once at the beginning to 
#              confirm proceeding, and then sequentially installs the listed 
#              programs.
#
# Design Principles:
# - User Prompt: Only one prompt for user confirmation before any changes.
# - Modularization: Each installation step is encapsulated in its own function.
# - Extendability: New packages/programs can be added by creating additional
#                  functions or adding entries to the packages list.
# - Portability: Uses plain Bash and standard utilities (assumes apt as package
#                manager; change the package manager command if needed).
#
# Usage: 
#   chmod +x install-apps.sh
#   ./install-apps.sh
#
# Requirements:
#   - The script must be run on a system with sudo privileges.
#   - Modify PACKAGE_MANAGER if you use a different package manager (e.g. yum, pacman).
# =============================================================================

# Set the package manager command (modify if needed, e.g., "yum install -y")
PACKAGE_MANAGER="apt-get install -y"

# Array of packages to install. Extend this list as needed.
PACKAGES=(
    "curl"
    "git"
    "tmux"
    "zip"
)

# Function: prompt_user
# Description: Prompts the user for a yes/no confirmation.
prompt_user() {
    echo "This script will install the following packages:"
    for pkg in "${PACKAGES[@]}"; do
        echo "  - $pkg"
    done
    echo
    read -p "Do you want to proceed? (y/n): " response
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

# Function: install_package
# Description: Installs a single package using the specified package manager.
# Parameters:
#   $1 - Name of the package to install.
install_package() {
    local package="$1"
    echo "Installing $package..."
    sudo $PACKAGE_MANAGER "$package"
    if [ $? -ne 0 ]; then
        echo "Error: Installation of $package failed."
    else
        echo "$package installed successfully."
    fi
}

# Function: install_all_packages
# Description: Iterates over the PACKAGES array and installs each package.
install_all_packages() {
    for pkg in "${PACKAGES[@]}"; do
        install_package "$pkg"
    done
}

# Main function
main() {
    # Prompt user once before starting installation
    prompt_user

    # Call the function to install all packages
    install_all_packages

    echo "All requested packages have been processed."
	echo ""
	echo "Building AALTO..."
	make clean
	make
	echo "AALTO setup finished successfully!"
}

# Execute main if the script is run directly.
main
