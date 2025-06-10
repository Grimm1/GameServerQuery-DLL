# GameServerQuery DLL

## Overview

The `GameServerQuery` DLL is a C++ library designed to query and manage game servers for *Medal of Honor* and *Call of Duty* using UDP-based protocols. It supports commands such as `getstatus`, `getinfo` (Call of Duty only), and `rcon` commands (e.g., `rcon status`, `rcon map`), with responses available in JSON or raw formats. The library includes DNS caching for efficient hostname resolution and robust error handling for invalid inputs and network issues. It is built as a Dynamic Link Library (DLL) for Windows, making it reusable across applications.

The repository includes:
- `GameServerQuery.h`: Header file defining the library's public interface.
- `GameServerQuery.cpp`: Implementation of the library's functionality.
- `test.cpp`: A test harness to validate the library with comprehensive test cases.

## Features

- **Supported Protocols**:
  - Medal of Honor (protocol ID: 1)
  - Call of Duty (protocol ID: 2)
- **Commands**:
  - `getstatus`: Retrieves server status and player information.
  - `getinfo` (Call of Duty only): Fetches server information.
  - `rcon` commands (e.g., `rcon status`, `rcon map`): Executes remote console commands with password authentication.
- **Output Formats**:
  - JSON: Structured output for easy parsing.
  - Raw: Unprocessed server response for debugging or custom handling.
- **DNS Caching**: Caches hostname-to-IP mappings with a 5-minute TTL to reduce DNS lookup overhead.
- **Error Handling**: Comprehensive checks for invalid inputs, network failures, and unsupported commands.
- **Thread Safety**: DNS cache is protected by a mutex for safe concurrent access.

## Prerequisites

To build and use the `GameServerQuery` DLL, ensure the following prerequisites are met:

- **Operating System**: Windows (due to Winsock and DLL-specific code).
- **Compiler**: Visual Studio 2019 or later (C++17 or higher required for `std::unique_ptr` and other features).
- **Dependencies**:
  - Windows SDK (included with Visual Studio) for Winsock2 (`winsock2.h`, `ws2tcpip.h`).
  - C++ Standard Library (included with Visual Studio) for `std::string`, `std::map`, `std::chrono`, etc.
- **Game Servers**: Access to *Medal of Honor* or *Call of Duty* servers for testing, with valid IP addresses/ports and RCON passwords (if applicable).

## Dependencies

The DLL relies on the following:
- **Winsock2**: Windows Sockets API for UDP communication (linked via `Ws2_32.lib`).
  - Included in the Windows SDK, no external installation required.
  - Linked automatically via `#pragma comment(lib, "Ws2_32.lib")` in `GameServerQuery.cpp`.
- **C++ Standard Library**: Used for string manipulation, containers, and threading utilities.
  - No external installation needed; provided by Visual Studio's C++ toolchain.

No additional third-party libraries are required.

## Visual Studio Setup

Follow these steps to set up and build the `GameServerQuery` DLL in Visual Studio:

1. **Create a New Project**:
   - Open Visual Studio (2019 or later).
   - Create a new project: `File > New > Project`.
   - Select **Dynamic-Link Library (DLL)** under **C++ > Windows > Desktop**.
   - Name the project (e.g., `GameServerQuery`) and choose a location.

2. **Add Source Files**:
   - Copy `GameServerQuery.h`, `GameServerQuery.cpp`, and `test.cpp` into the project directory.
   - In Solution Explorer, right-click the project and select `Add > Existing Item`.
   - Add `GameServerQuery.h` to the **Header Files** folder.
   - Add `GameServerQuery.cpp` and `test.cpp` to the **Source Files** folder.

3. **Configure Project Properties**:
   - Right-click the project in Solution Explorer and select **Properties**.
   - **General**:
     - Set **C++ Language Standard** to **ISO C++17 Standard** or higher.
   - **Advanced**:
     - Ensure **Character Set** is set to **Use Multi-Byte Character Set** (due to `std::string` and Winsock usage).
   - **Linker > Input**:
     - Verify that `Ws2_32.lib` is included in **Additional Dependencies** (handled by `#pragma comment` in the code, but confirm if needed).
   - **Preprocessor**:
     - Add `GAMESERVERQUERY_EXPORTS` to **Preprocessor Definitions** for the DLL project to enable export macros.

4. **Build the DLL**:
   - Set the solution configuration to **Release** or **Debug** as needed.
   - Select **Build > Build Solution** (or press `F7`).
   - The DLL (`GameServerQuery.dll`) and associated files (e.g., `.lib`) will be generated in the output directory (e.g., `Release` or `Debug`).

5. **Set Up the Test Project** (Optional):
   - To use `test.cpp`, create a separate **Console Application** project in the same solution.
   - Add `test.cpp` to the project.
   - In **Project Properties**:
     - **Linker > General**: Add the path to the DLL’s `.lib` file (e.g., `path/to/GameServerQuery.lib`) to **Additional Library Directories**.
     - **Linker > Input**: Add `GameServerQuery.lib` to **Additional Dependencies**.
     - **General**: Ensure the **C++ Language Standard** is set to **ISO C++17 Standard** or higher.
   - Copy `GameServerQuery.dll` to the test project’s output directory (e.g., `Debug` or `Release`) or ensure it’s in the system PATH.
   - Build and run the test project to execute the test suite.

6. **Verify Dependencies**:
   - Ensure the Windows SDK is installed (typically included with Visual Studio).
   - No additional runtime dependencies are required beyond the C++ runtime and `Ws2_32.dll` (part of Windows).

## Usage

### Library Integration

To use the `GameServerQuery` DLL in another project:

1. Include `GameServerQuery.h` in your source code.
2. Link against `GameServerQuery.lib` and ensure `GameServerQuery.dll` is accessible at runtime.
3. Call `ProcessGameServerCommand` to query a game server, and always call `FreeGameServerResponse` to free the allocated memory.

Example:

```cpp
#include "GameServerQuery.h"
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>

int main() {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Error: Winsock initialization failed" << std::endl;
        return 1;
    }

    // Query Call of Duty server for status
    const char* result = ProcessGameServerCommand(2, false, "myserver.com", 28960, "getstatus", nullptr);
    if (result) {
        if (std::strstr(result, "error=")) {
            std::cerr << "Error: " << result << std::endl;
        } else {
            std::cout << "Success: " << result << std::endl;
        }
        FreeGameServerResponse(result);
    } else {
        std::cerr << "Error: Null result returned" << std::endl;
    }

    // Clean up Winsock
    WSACleanup();
    return 0;
}
```
**Note**: Always call `FreeGameServerResponse` to free the memory allocated for the response. Ensure Winsock is initialized before calling `ProcessGameServerCommand` and cleaned up afterward.

### Test Harness

The `test.cpp` file provides a comprehensive test suite:

- Prompts for RCON passwords for *Medal of Honor* and *Call of Duty* servers.
- Runs 18 test cases covering valid commands, error cases, raw/JSON outputs, and edge cases (e.g., invalid ports, null inputs).
- Outputs results with `PASSED` or `FAILED` indicators, separated by two newlines for readability.

To run the tests:

1. Build and run the test project as described in the Visual Studio setup.
2. Enter valid RCON passwords when prompted (or empty strings for non-RCON tests).
3. Review the console output for test results.

### Supported Commands

- **Medal of Honor (protocolId = 1)**:
  - `getstatus`: Returns server status and player list.
  - `rcon status`: Returns player list (requires RCON password).
  - `rcon <command>`: Executes other RCON commands (e.g., `rcon map <mapname>`).
- **Call of Duty (protocolId = 2)**:
  - `getstatus`: Returns server status and player list.
  - `getinfo`: Returns player information.
  - `rcon status`: Returns player list (requires RCON password).
  - `rcon <command>`: Executes other RCON commands (e.g., `rcon map mp_harbor`).

### Example Output

Running `test.cpp` might produce:

``` plaintext
Enter RCON password for Medal of Honor server: <password>
Enter RCON password for Call of Duty server: <password>
Test 1: FAILED: error=Invalid protocol ID

Test 2: PASSED: {"server":{...},"players":[...]}

Test 3: PASSED: {"server":{...},"players":[...]}

...

### Building and Running Notes

- **Server Configuration**: Replace `myserver.com` and `127.0.0.1` with actual server IP addresses or hostnames, and use correct ports.
- **RCON Passwords**: Ensure valid RCON passwords are provided for `rcon` commands; invalid passwords will result in errors.
- **Network Access**: Ensure the machine has network access to the game servers and that firewalls allow UDP traffic on the specified ports.
- **Error Handling**: The library returns errors prefixed with `error=` (e.g., `error=Invalid port`) for easy identification.

### Limitations

- Windows-only due to Winsock and DLL-specific code.
- Supports only *Medal of Honor* and *Call of Duty* protocols; additional protocols require new `ProtocolHandler` implementations.
- Test suite assumes servers are accessible; results depend on server availability and configuration.

### Contributing

To contribute:

1. Fork the repository.
2. Create a new branch for your changes.
3. Submit a pull request with a clear description of the changes and test results.

### License

This project is provided as-is, with no specific license defined. Contact the repository owner for licensing details.

### Contact

For issues or questions, please file an issue on the repository or contact the maintainer.

