#!/bin/bash

# Clone the Orbbec SDK repository
git clone https://github.com/orbbec/OrbbecSDK.git

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
