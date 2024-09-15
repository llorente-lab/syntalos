#!/bin/bash

set -e

# check for required commands
command -v git >/dev/null 2>&1 || { echo >&2 "Error: git is not installed."; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo >&2 "Error: cmake is not installed."; exit 1; }

# Clone OrbbecSDK repository if it doesn't exist
if [ -d "OrbbecSDK" ]; then
  echo "Directory 'OrbbecSDK' already exists. Skipping git clone."
else
  git clone https://github.com/orbbec/OrbbecSDK.git || { echo "Failed to clone OrbbecSDK repository."; exit 1; }
fi

# Navigate to the OrbbecSDK directory
cd OrbbecSDK

# Create a build directory and navigate into it
mkdir -p build
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
