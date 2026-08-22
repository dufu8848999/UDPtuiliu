# Udp-streamVedio

A low-latency real-time screen transmission system based on the UDP protocol, supporting the real-time transmission of screen content to another device for display.

## Project Introduction

This project implements an efficient real-time screen transmission solution. It captures and compresses screen frames via the UDP protocol for transmission to the receiver. The project adopts the LZ4 fast compression algorithm combined with a custom UDP packet protocol, capable of achieving a smooth screen sharing experience within a LAN environment.

### Core Features

- **Low Latency Transmission**: Uses the UDP protocol for fast data transmission, minimizing transmission delay to the greatest extent.
- **High-Efficiency Compression**: Integrates the LZ4 compression algorithm to reduce network bandwidth usage while ensuring compression speed.
- **Flexible Configuration**: Supports customization of transmission resolution, frame rate, and other parameters.
- **Multiple Operating Modes**: Supports independent operation of the sender and receiver to meet different scenario requirements.
- **DLL Injection Support**: Provides remote injection functionality to perform screen capture and transmission within a target process.

### Technical Architecture

```
Sender Workflow:
  Screen Capture (DXGI) → Frame Compression (LZ4) → Data Fragmentation → UDP Send

Receiver Workflow:
  UDP Receive → Data Reassembly → Decompression (LZ4) → Window Display
```

### Core Modules

| Module | Description |
|------|------|
| `protocol.h` | Defines packet format and protocol-related constants |
| `compressor.h/lz4.*` | LZ4 compression/decompression implementation |
| `dxgi_capture.h` | DXGI-based screen capture module |
| `display.h` | Receiver window display module |
| `udp_sender.h` | UDP data sending module |
| `udp_receiver.h` | UDP data receiving and reassembly module |
| `screen_capture.h` | Unified screen capture interface |
| `dll_main.cpp` | Main entry point for the remote injection module |
| `gui_injector_imgui.cpp` | Injection tool with a graphical interface |

### Main Files

| File | Function Description |
|------|----------|
| `Demo/Demo/main.cpp` | Main program entry, supports both sender/receiver modes |
| `Demo/dll_main.cpp` | Entry point for the DLL injection module |
| `Demo/gui_injector_imgui.cpp` | Source file for the GUI injection tool |
| `Demo/injector.cpp` | Command-line injection tool |

## Compilation Instructions

### Environment Requirements

- Windows 10/11 Operating System
- Visual Studio 2022 or higher
- CMake 3.15 or higher (Optional)
- DirectX 11 Development Environment

### Compilation Steps

1. Use Visual Studio to open the `Demo/Demo.slnx` solution file.
2. Select the `x64` platform and `Release` configuration.
3. Build the solution.

After successful compilation, you can find the generated executable file in the `Demo/Demo/x64/Release/` directory.

## Usage

### Command-Line Arguments

The program supports the following command-line arguments:

```
Demo.exe [Mode] [Arguments]

Modes:
  --sender <IP> <PORT>    Start the sender, specifying the target IP and port
  --receiver <PORT>       Start the receiver, specifying the listening port

Examples:
  Demo.exe --sender 192.168.1.100 5000    // Send screen to 192.168.1.100:5000
  Demo.exe --receiver 5000                // Listen on port 5000 to receive screen stream
```

### Sender Usage

1. Run the program and specify the target IP address and port.
2. The program will automatically capture the main screen content.
3. Captured frames will be compressed and sent via UDP.

### Receiver Usage

1. Run the program and specify the listening port.
2. The program will create a display window.
3. Receive and display screen content from the sender.

### DLL Injection Method

For scenarios requiring remote screen capture, the injection function can be used:

1. Ensure the target process is running.
2. Use the injection tool to inject the DLL into the target process.
3. Configure target IP, port, and other parameters.

## Configuration Instructions

### Default Parameters

| Parameter | Default Value | Description |
|------|--------|------|
| CAPTURE_W | 1920 | Capture Width |
| CAPTURE_H | 1080 | Capture Height |
| UDP_PORT | 5000 | Default UDP Port |
| MTU_PAYLOAD | 1400 | Maximum Payload Per Single Packet |
| Target Framerate | 60 FPS | Transmission Frame Rate |

### Custom Configuration

The following settings can be modified via configuration files or command-line arguments:

- Capture Resolution (Width/Height)
- Target Framerate
- Compression Level
- Network Port

## Project Structure

```
Udp-streamVedio/
├── Demo/
│   ├── Demo/                    # Main Program Source Code
│   │   ├── main.cpp             # Program Entry
│   │   ├── compressor.h         # Compression Interface
│   │   ├── lz4.c/h              # LZ4 Implementation
│   │   ├── protocol.h           # Protocol Definition
│   │   ├── display.h            # Display Module
│   │   ├── dxgi_capture.h       # DXGI Capture
│   │   ├── screen_capture.h     # Capture Interface
│   │   ├── udp_sender.h         # Sending Module
│   │   ├── udp_receiver.h       # Receiving Module
│   │   ├── dll_main.cpp         # DLL Entry
│   │   └── gui_injector_imgui.cpp # GUI Injection Tool
│   ├── imgui/                   # ImGui Library
│   ├── x64/Release/             # Build Output
│   └── DllInject/               # Injection Related Project
│       ├── SC256_DLL/           # DLL to be Injected
│       └── Injector/            # Injector
├── Demo_backup_20260816/        # Backup Version
└── DllInject/                   # Injection Module
```

## Technical Details

### Packet Structure

Each UDP packet contains the following:

- **Header**: Contains Frame ID, Total Fragments, Fragment ID, Image Size, etc.
- **Data**: LZ4-compressed image data.

### Fragmentation Strategy

To adapt to MTU limitations, large frames will be split into multiple small packets for transmission:

```
Single Packet Size = HEADER_SIZE + DATA_PER_PKT
Total Fragments = ceil(Compressed Data Size / DATA_PER_PKT)
```

### Time Synchronization

The program uses microsecond-level timestamps for synchronization to ensure the receiver can play frames correctly according to the frame rate.

## Notes

1. This program runs only on the Windows platform.
2. Ensure the firewall allows the program through the specified port before use.
3. It is recommended to use in a LAN environment for the best results.
4. High-resolution transmission will occupy significant network bandwidth.

## License

This project is for learning and research purposes only.

## Contribution Guide

Welcome to participate in contributions via the following methods:

- Submit Bug Reports
- Send Feature Suggestions
- Contribute Code Improvements
- Improve Project Documentation