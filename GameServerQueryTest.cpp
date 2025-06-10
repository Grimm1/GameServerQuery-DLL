#include "GameServerQuery.h"
#include <iostream>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

// Executes a single game server query test and prints the result
void RunTest(int testId, int protocolId, bool raw, const char* ipOrHostname, int port, const char* command, const char* rconPassword) {
    std::cout << "Test " << testId << ": ";
    // Call the game server command processing function
    const char* result = ProcessGameServerCommand(protocolId, raw, ipOrHostname, port, command, rconPassword);
    if (result) {
        // Check if result contains an error and print appropriate status
        std::cout << (strstr(result, "error=") ? "FAILED: " : "PASSED: ") << result << std::endl;
        // Free the allocated result memory
        FreeGameServerResponse(result);
    }
    else {
        std::cout << "FAILED: Null result" << std::endl;
    }
    // Add two newlines for separation between test results
    std::cout << std::endl << std::endl;
    // Delay to prevent server overload
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// Main function to run a comprehensive suite of game server query tests for the dll
int main() {
    // Prompt for RCON passwords
    std::string mohRconPassword, codRconPassword;
    std::cout << "Enter RCON password for Medal of Honor server: ";
    std::getline(std::cin, mohRconPassword);
    std::cout << "Enter RCON password for Call of Duty server: ";
    std::getline(std::cin, codRconPassword);

    // Test 1: Invalid protocol ID
    RunTest(1, 999, false, "127.0.0.1", 28960, "getstatus", nullptr);

    // Test 2: Medal of Honor getstatus (JSON format)
    RunTest(2, 1, false, "127.0.0.1", 12203, "getstatus", nullptr);

    // Test 3: Call of Duty getstatus (JSON format)
    RunTest(3, 2, false, "myserver.com", 28960, "getstatus", nullptr);

    // Test 4: Medal of Honor getstatus (raw format)
    RunTest(4, 1, true, "127.0.0.1", 12203, "getstatus", nullptr);

    // Test 5: Medal of Honor rcon status with password
    RunTest(5, 1, false, "127.0.0.1", 12203, "rcon status", mohRconPassword.c_str());

    // Test 6: Medal of Honor rcon status (raw format)
    RunTest(6, 1, true, "127.0.0.1", 12203, "rcon status", mohRconPassword.c_str());

    // Test 7: Call of Duty rcon status with password
    RunTest(7, 2, false, "myserver.com", 28960, "rcon status", codRconPassword.c_str());

    // Test 8: Call of Duty rcon status (raw format)
    RunTest(8, 2, true, "myserver.com", 28960, "rcon status", codRconPassword.c_str());

    // Test 9: Call of Duty getinfo (JSON format)
    RunTest(9, 2, false, "myserver.com", 28960, "getinfo", nullptr);

    // Test 10: Medal of Honor invalid command
    RunTest(10, 1, false, "127.0.0.1", 12203, "invalid_command", nullptr);

    // Test 11: Call of Duty rcon map change
    RunTest(11, 2, false, "myserver.com", 28960, "rcon map mp_harbor", codRconPassword.c_str());

    // Test 12: Invalid port number (out of range)
    RunTest(12, 1, false, "127.0.0.1", 0, "getstatus", nullptr);

    // Test 13: Null IP/hostname
    RunTest(13, 1, false, nullptr, 12203, "getstatus", nullptr);

    // Test 14: Null command
    RunTest(14, 1, false, "127.0.0.1", 12203, nullptr, nullptr);

    // Test 15: Medal of Honor rcon with empty password
    RunTest(15, 1, false, "127.0.0.1", 12203, "rcon status", "");

    // Test 16: Call of Duty rcon with invalid password
    RunTest(16, 2, false, "myserver.com", 28960, "rcon status", "wrongpassword");

    // Test 17: Medal of Honor hostname resolution
    RunTest(17, 1, false, "myserver.com", 12203, "getstatus", nullptr);

    // Test 18: Call of Duty with high port number
    RunTest(18, 2, false, "myserver.com", 65535, "getstatus", nullptr);

    return 0;
}