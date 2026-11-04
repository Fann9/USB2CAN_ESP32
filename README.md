# USB2CAN

ESP-IDF 6.0 project for the ESP32 target. The current starter firmware prints
`Hello World!` once on boot. CAN communication has not been added yet.

## Build

Open this directory in VS Code with the ESP-IDF extension and run **ESP-IDF:
Build your project**. The copied `.vscode/settings.json` points to the local
ESP-IDF installation and Python environment.

From a terminal:

```bash
source ./export_idf.sh
idf.py build
```

The configured serial port is inherited from the source project. Select the
actual ESP32 port in VS Code before flashing or monitoring.
