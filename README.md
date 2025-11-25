# WB-MGE
Wirenboard Multi-protocol gateway

## Project Description

WB-MGE is designed to connect devices with RS-485 interface and WBIO I/O side modules to an automation server via Ethernet or Wi-Fi.

Two modes are available for each port:

 - Modbus TCP — for Modbus devices only
 - Transparent gateway — suitable for any protocols running over RS-485.

## Manual Build Instructions

### Prerequisites

1. **Node.js 20.x** (for frontend build)
2. **Python 3.8+**
3. **Git**

### 0. Install Node.js (if not installed)

```bash
# For Ubuntu/Debian
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt-get install -y nodejs
```
```bash
# For macOS using Homebrew
brew install node
```

### 1. Install ESP-IDF

```bash
# Clone ESP-IDF repository
git clone --branch v5.4 --single-branch --depth 1 --recurse-submodules --shallow-submodules https://github.com/espressif/esp-idf.git

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

Remove Docker image:
```bash
docker rmi wb-mge-builder
```
