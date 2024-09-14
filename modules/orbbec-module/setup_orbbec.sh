#!/bin/bash

set -e

check_command git
check_command cmake
check_command sudo

sudo -v || error_exit "This script requires sudo privileges. Please run as a user with sudo access."

if [ -d "OrbbecSDK" ]; then
  echo "Directory 'OrbbecSDK' already exists. Skipping git clone."
else
  git clone https://github.com/orbbec/OrbbecSDK.git || error_exit "Failed to clone OrbbecSDK repository."
fi

# Navigate to the OrbbecSDK directory
cd OrbbecSDK

# Create a build directory and navigate into it
mkdir build
cd build

# Run cmake and build the project in Release configuration
cmake ..
cmake --build . --config Release

# Navigate to the scripts directory
cd ../misc/scripts

# Make the install_udev_rules.sh script executable
sudo chmod +x ./install_udev_rules.sh

# Run the udev rules installation script
sudo ./install_udev_rules.sh

# Reload udev rules and trigger changes
sudo udevadm control --reload
sudo udevadm trigger

echo "Orbbec SDK installation and udev rules setup completed."
