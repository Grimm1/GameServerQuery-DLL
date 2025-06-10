#pragma once

#include <string>
#include <memory>
#include <map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

// Defines export/import macro for DLL linkage on Windows
#ifdef GAMESERVERQUERY_EXPORTS
#define GAMESERVERQUERY_API __declspec(dllexport)
#else
#define GAMESERVERQUERY_API __declspec(dllimport)
#endif

// Abstract base class for handling game server protocol commands
class ProtocolHandler {
public:
    virtual ~ProtocolHandler() = default; // Virtual destructor for proper cleanup
    // Processes a command for a specific game server protocol
    virtual std::string ProcessCommand(
        bool raw,                           // If true, returns raw response
        const std::string& ip,              // Server IP address
        int port,                           // Server port
        const std::string& command,         // Command to execute
        const std::string& rconPassword     // RCON password for authentication
    ) = 0;
};

// Resolves a hostname to an IP address with DNS caching
std::string ResolveHostname(const std::string& hostname);

// Sends a UDP query to a game server and returns the response
std::string SendUDPQuery(
    const std::string& ip,      // Server IP address
    int port,                   // Server port
    const std::string& query,   // Query to send
    int timeoutMs = 5000        // Timeout in milliseconds (default: 5000)
);

// Processes a game server command and returns the response as a C-string
extern "C" GAMESERVERQUERY_API const char* ProcessGameServerCommand(
    int protocolId,             // Protocol ID (e.g., 1 for Medal of Honor, 2 for Call of Duty)
    bool raw,                   // If true, returns raw response
    const char* ipOrHostname,   // Server IP or hostname
    int port,                   // Server port
    const char* command,        // Command to execute
    const char* rconPassword    // RCON password for authentication
);

// Frees memory allocated for the game server response
extern "C" GAMESERVERQUERY_API void FreeGameServerResponse(const char* response);