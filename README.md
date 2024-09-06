# Bluetooth Remote Service Application

This project is a Bluetooth remote service application using nRF Connect SDK and Zephyr RTOS. It measures the data throughput during a period of 30 seconds. It is currently configure to run on nRF52 Development Kits, but adding new build configurations for other Nordic Boards should work as well.

## Features

- **Bluetooth Advertising**: Advertises a custom BLE service.
- **Updates Connection Parameters**: Send a request for updating connection parameters to the BLE Central.
- **Data Transmission**: Sends as much data as possible using BLE notifications.
- **Log current throughout**: Current throughput is logged every second.

## Requirements

- nRF Connect SDK version v2.6.1
- Board supported by nRF, e.g., nRF52 DK

## Files

- `main.c`: Contains the main application logic.
- `cts.h`: Header file for custom services and characteristics.
- `cts.c`: Source code for custom services and characteristics

## Usage

1. **Build and Flash**: Build the application and flash it to your hardware.
2. **Run**: Power on the device. It will start advertising and be ready to connect.
3. **Connect**: Use a BLE-compatible device to connect to the application and subscribe to notifications of characteristic `e9ea0002-e19b-482d-9293-c7907585fc48`.
4. **Interact**: Press the Button 1 to start sending notifications.

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.