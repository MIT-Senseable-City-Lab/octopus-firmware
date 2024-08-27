# Project Setup Guide

This guide will help you set up your environment by downloading the required Arduino libraries and configuring additional files.

## Required Libraries

To successfully run this project, you will need to download and install the following Arduino libraries:

1. **Adafruit_DotStar**
2. **sensirion-sps**
3. **Arduino_HS300x**
4. **ArduinoBLE**
5. **SD**
6. **SparkFun_u-blox_GNSS_Arduino_Library**

## Installation Instructions

### Install via Arduino Library Manager (Recommended)

1. Open the Arduino IDE.
2. Navigate to `Sketch` > `Include Library` > `Manage Libraries...`.
3. In the Library Manager, search for each of the required libraries listed above.
4. Click the `Install` button next to each library.

## Adding `octopus.cpp` and `octopus.h` Files

To ensure your custom code is recognized by the Arduino IDE, you need to manually add the `octopus.cpp` and `octopus.h` files to the appropriate library folder.

1. **Locate the Arduino Libraries Folder:**
   - This is typically located in your documents under `Arduino/libraries`.
   - Alternatively, it may be in the installation directory of the Arduino IDE under `libraries`.

2. **Add Files to the Library:**
   - Navigate to the folder of the library where you want to add these files, or create a new folder within the `libraries` directory named `Octopus`.
   - Copy `octopus.cpp` and `octopus.h` into this folder.

3. **Restart the Arduino IDE:**
   - After adding the files, restart the Arduino IDE to ensure the changes take effect.

## Verifying Installation

To ensure that all libraries and files have been installed correctly, follow these steps:

1. Open the Arduino IDE.
2. Go to `Sketch` > `Include Library` and check if the libraries appear in the list of installed libraries.
3. Make sure the `octopus.cpp` and `octopus.h` files are accessible in your sketches.

## Additional Information

- For more details on how to use these libraries and files, refer to their official documentation or example sketches available within the Arduino IDE.
- Make sure to use the latest version of the Arduino IDE for compatibility.

