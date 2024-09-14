# Orbbec SDK Setup Guide

This module requires you to build the Orbbec SDK. The SDK can be found in the official Orbbec GitHub repository: https://github.com/orbbec/OrbbecSDK/tree/main.

To simplify things, there is a script (setup_orbbec.sh) that automates much of the installation for you.

### Step 1: Run the Setup Script

Execute the provided setup_orbbec.sh script to install and configure the Orbbec SDK:

```bash
sudo ./setup_orbbec.sh
```
This script will:
1. Clone the Orbbec SDK repository.
2. Build the SDK.
3. Install the necessary udev rules.

### Step 2: Update the Meson Build File

After running the script:
1. Locate the Orbbec SDK path in your home directory. The setup_orbbec.sh script will have placed it there.
2. Open the meson.build file located in this repository.
3. At the top of the meson.build file, replace the orbbec_sdk_path variable with the location of your local copy of the Orbbec SDK. It should look something like this:

```bash
orbbec_sdk_path = '/path/to/your/local/copy'
```

