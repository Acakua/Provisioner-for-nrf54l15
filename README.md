# Provisioner for nRF54L15 (Custom Mesh Provisioner)

This project is a Bluetooth Mesh Provisioner application running on Zephyr RTOS, optimized for the Nordic nRF54L15. The source code has been customized to support Fixed Keys, an Auto-Provisioning mode, and real-time LED status indication.

## 1. Key Features

- Fixed Keys: Uses pre-defined NetKey, AppKey, and DevKey to simplify synchronization with Unprovisioned Nodes during development.
- Auto-Provisioning Mode: Capable of automatically provisioning any detected unprovisioned beacon without manual intervention.
- LED Status: Utilizes LED0 to indicate whether Auto-Mode is active (On) or inactive (Off).
- Button Control: Uses SW0 to toggle between Manual and Auto-Provisioning modes.
- Persistent Storage: Utilizes NVS (Non-Volatile Storage) to save network states and CDB data across reboots.

## 2. Security Keys Specification

For development consistency, the following keys are hardcoded:

- NetKey: 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef
- AppKey: 0xfe, 0xdc, 0xba, 0x09, 0x87, 0x65, 0x43, 0x21, 0xfe, 0xdc, 0xba, 0x09, 0x87, 0x65, 0x43, 0x21
- DevKey: 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99

## 3. Hardware Configuration (DeviceTree Aliases)

- led0: Indicates Auto-Mode status.
- sw0: Input button to toggle modes and confirm manual provisioning.

## 4. Key Configuration (prj.conf)

This project requires specific Bluetooth Mesh stack settings:
- CONFIG_BT_MESH_PROVISIONER=y: Enables the provisioner role.
- CONFIG_BT_MESH_CDB=y: Enables the Configuration Database to manage nodes.
- CONFIG_BT_SETTINGS=y: Enables persistent storage for Mesh data.
- CONFIG_MAIN_THREAD_PRIORITY=-2: Sets a cooperative priority for Bluetooth stability.

## 5. How to Use

### Step 1: Build the project
Use the west tool to build for the nRF54L15 target:
west build -b nrf54l15dk_nrf54l15_cpuapp

### Step 2: Flash the firmware
west flash

### Step 3: Operation
- Initial State: Auto-Mode is OFF by default.
- Manual Mode: When a new device is detected, the UUID is printed to the Serial Monitor. Press SW0 to provision that specific device.
- Auto Mode: Press SW0 to toggle Auto-Mode ON (LED0 will light up). Any device in range sending unprovisioned beacons will be provisioned automatically.

## 6. Security Note
This project uses fixed cryptographic keys for testing and development purposes only. Do not use these keys in a production environment as they pose a significant security risk.