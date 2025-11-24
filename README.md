# WB-MGE
Wirenboard Modbus Gateway Ethernet firmware

[Русская версия README](README.ru.md)

## Project Description

WB-MGE is designed to connect devices with RS-485 interface and WBIO I/O side modules to an automation server via Ethernet or Wi-Fi.

Two modes are available for each port:

 - Modbus TCP — for Modbus devices only
 - Transparent gateway — suitable for any protocols running over RS-485.

## Required Tools

1. **ESP-IDF v5.4**
2. **Node.js and npm** (for frontend build)
3. **Python 3.8+**
4. **Git**

## Manual Build Instructions

### 1. Install ESP-IDF

```bash
# Clone ESP-IDF repository
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git

# Run the installation script
cd esp-idf
./install.sh

# Set up environment variables
source export.sh
```

### 2. Clone the Repository

```bash
git clone git@github.com:wirenboard/wb-mge.git
cd wb-mge
```

### 3. Generate Key File

Generate the key file according to copy_protection/readme.md.

### 4. Build the Project

For a complete build (unit tests + frontend + firmware):

```bash
make
```

If building components separately, first build the frontend:

```bash
make build-frontend
```

Then build the firmware:

```bash
make build-idf-project
```

**Important:** Using `idf.py build` directly will NOT embed version information into the binary. Always use `make build-idf-project` for production builds.

## Building with Docker

Docker allows you to build the project without installing ESP-IDF and Node.js on your host system.

### 1. Install Docker

Install Docker according to the official documentation for your OS:
https://docs.docker.com/get-started/get-docker/

### 2. Build Docker Image

```bash
# From the project root directory
docker build -t wb-mge-builder .
```

This will create a Docker image with:
- ESP-IDF v5.4
- Node.js 20.x
- All necessary build tools

### 3. Run Container

```bash
docker run --rm -it -v $(pwd):/root/esp/project wb-mge-builder
```

### 4. Build Inside Container

Building inside the container is the same as manual building (see "Manual Build Instructions" section, steps 3 and 4).

### Alternative: One-Command Build

You can build the project without entering the container:

```bash
docker run --rm -v $(pwd):/root/esp/project wb-mge-builder make
```

## Flashing the Device

```bash
# Linux
idf.py -p /dev/ttyUSB* flash

# macOS
idf.py -p /dev/cu.wchusbserial* flash
```

## Connecting to Device Console

```bash
idf.py monitor
```

To disconnect from the monitor, press `Ctrl+]`.

## Cleanup

```bash
make clean
```

Or inside container:

```bash
docker run --rm -v $(pwd):/root/esp/project wb-mge-builder make clean
```

Sometimes `make clean` gives an error, in that case first delete build manually, then perform cleanup:
```bash
rm -rf build/
make clean
```

Remove Docker image:
```bash
docker rmi wb-mge-builder
```

## License and Confidentiality

### Trade Secret
**CONFIDENTIAL**

All contents of this repository or archive are marked as "Trade Secret".
Trade Secret of Individual Entrepreneur Lesnichiy Yakov Vasilyevich.
Address: 352380, Krasnodar Region, Kropotkin, st. Timiryazeva, 55
TIN: 231302744108
Validity: 15 years.
Copy number: 1

Copyright (c) 2014-2017 Contactless Devices, LLC. CONFIDENTIAL
info@contactless.ru
Unpublished Copyright (c) 2014-2017 Contactless Devices, LLC, All Rights Reserved.

```
NOTICE: All information contained herein is, and remains the property of
Contactless Devices, LLC. The intellectual and technical concepts contained
herein are proprietary to Contactless Devices, LLC and may be covered by U.S.
and Foreign Patents, patents in process, and are protected by trade secret or
copyright law. Dissemination of this information or reproduction of this material
is strictly forbidden unless prior written permission is obtained from Contactless
Devices, LLC. Access to the source code contained herein is hereby forbidden to
anyone except current Contactless Devices, LLC employees, managers or contractors
who have executed Confidentiality and Non-disclosure agreements explicitly covering
such access.

The copyright notice above does not evidence any actual or intended publication or
disclosure of this source code, which includes information that is confidential
and/or proprietary, and is a trade secret, of Contactless Devices, LLC. ANY
REPRODUCTION, MODIFICATION, DISTRIBUTION, PUBLIC PERFORMANCE, OR PUBLIC DISPLAY OF
OR THROUGH USE OF THIS SOURCE CODE WITHOUT THE EXPRESS WRITTEN CONSENT OF
Contactless Devices, LLC IS STRICTLY PROHIBITED, AND IN VIOLATION OF APPLICABLE
LAWS AND INTERNATIONAL TREATIES. THE RECEIPT OR POSSESSION OF THIS SOURCE CODE
AND/OR RELATED INFORMATION DOES NOT CONVEY OR IMPLY ANY RIGHTS TO REPRODUCE,
DISCLOSE OR DISTRIBUTE ITS CONTENTS, OR TO MANUFACTURE, USE, OR SELL ANYTHING
THAT IT MAY DESCRIBE, IN WHOLE OR IN PART.
```
