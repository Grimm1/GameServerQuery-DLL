#include "GameServerQuery.h"
#include <chrono>
#include <mutex>
#include <sstream>
#include <vector>
#include <algorithm>
#include <windows.h>

#pragma comment(lib, "Ws2_32.lib")

namespace {
    // DNS cache entry structure with 5-minute TTL
    struct DnsCacheEntry {
        std::string ip;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::map<std::string, DnsCacheEntry> dnsCache;
    std::mutex dnsMutex;

    // Registry for protocol handlers
    std::map<int, std::unique_ptr<ProtocolHandler>> protocolRegistry;

    // Sanitizes command by removing semicolons and newlines
    std::string SanitizeCommand(const std::string& command) {
        std::string sanitized = command;
        sanitized.erase(std::remove_if(sanitized.begin(), sanitized.end(),
            [](char c) { return c == ';' || c == '\n' || c == '\r'; }), sanitized.end());
        return sanitized;
    }

    // Escapes special characters in a string for JSON output
    std::string EscapeJson(const std::string& input) {
        std::string result;
        for (char c : input) {
            if (c == '"') result += "\\\"";
            else if (c == '\\') result += "\\\\";
            else if (c == '\n') result += "\\n";
            else if (c == '\r') continue;
            else result += c;
        }
        return result;
    }

    // Parses key-value pairs from server response
    std::map<std::string, std::string> ParseKeyValues(const std::string& response) {
        std::map<std::string, std::string> result;
        std::string key;
        size_t pos = 0;

        while (pos < response.size() && response[pos] == '\n') {
            ++pos;
        }

        while (pos < response.size()) {
            if (response[pos] != '\\') {
                break;
            }
            ++pos;
            size_t next = response.find('\\', pos);
            if (next == std::string::npos) {
                break;
            }
            key = response.substr(pos, next - pos);
            pos = next + 1;
            next = response.find('\\', pos);
            if (next == std::string::npos) {
                next = response.find('\n', pos);
                if (next == std::string::npos) {
                    next = response.size();
                }
            }
            if (!key.empty()) {
                result[key] = response.substr(pos, next - pos);
            }
            pos = next;
        }
        return result;
    }

    // Parses player data from getstatus response
    std::vector<std::map<std::string, std::string>> ParseGetStatusPlayers(const std::string& response, int protocolId) {
        std::vector<std::map<std::string, std::string>> players;
        std::stringstream ss(response);
        std::string line;
        bool inKeyValues = true;

        while (std::getline(ss, line)) {
            if (line.empty()) {
                inKeyValues = false;
                continue;
            }
            if (inKeyValues && line[0] == '\\') {
                continue;
            }
            inKeyValues = false;
            // Trim whitespace
            line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](char c) { return !std::isspace(c); }));
            line.erase(std::find_if(line.rbegin(), line.rend(), [](char c) { return !std::isspace(c); }).base(), line.end());
            if (line.empty()) {
                continue;
            }
            std::stringstream playerStream(line);
            std::vector<std::string> tokens;
            std::string token;
            bool inQuotes = false;
            for (char c : line) {
                if (c == '"') {
                    inQuotes = !inQuotes;
                    token += c;
                    continue;
                }
                if (c == ' ' && !inQuotes && !token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                    continue;
                }
                token += c;
            }
            if (!token.empty()) {
                tokens.push_back(token);
            }
            if (protocolId == 1 && tokens.size() >= 2) { // Medal of Honor: slot "name"
                std::map<std::string, std::string> player;
                player["slot"] = tokens[0];
                std::string name = tokens[1];
                if (name.size() >= 2 && name[0] == '"' && name.back() == '"') {
                    player["name"] = name.substr(1, name.size() - 2);
                    player["score"] = "0";
                    player["ping"] = "0";
                    if (std::all_of(player["slot"].begin(), player["slot"].end(), ::isdigit)) {
                        players.push_back(player);
                    }
                }
            }
            else if (protocolId == 2 && tokens.size() >= 3) { // Call of Duty: score ping "name"
                std::map<std::string, std::string> player;
                player["score"] = tokens[0];
                player["ping"] = tokens[1];
                std::string name = tokens[2];
                if (name.size() >= 2 && name[0] == '"' && name.back() == '"') {
                    player["name"] = name.substr(1, name.size() - 2);
                    player["slot"] = "0";
                    if ((player["score"].empty() || std::all_of(player["score"].begin(), player["score"].end(), ::isdigit) ||
                        (player["score"].size() > 1 && player["score"][0] == '-' &&
                            std::all_of(player["score"].begin() + 1, player["score"].end(), ::isdigit))) &&
                        (player["ping"].empty() || std::all_of(player["ping"].begin(), player["ping"].end(), ::isdigit))) {
                        players.push_back(player);
                    }
                }
            }
        }
        return players;
    }

    // Parses player data from rcon status response
    std::vector<std::map<std::string, std::string>> ParseRconStatusPlayers(const std::string& response, int protocolId) {
        std::vector<std::map<std::string, std::string>> players;
        std::stringstream ss(response);
        std::string line;
        bool isSteam = false;

        // Determine if server is Steam-based by checking for hostname or map
        while (std::getline(ss, line)) {
            line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](char c) { return !std::isspace(c); }));
            line.erase(std::find_if(line.rbegin(), line.rend(), [](char c) { return !std::isspace(c); }).base(), line.end());
            if (line.empty()) {
                continue;
            }
            std::string lowerLine = line;
            std::transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), ::tolower);
            if (lowerLine.find("hostname:") == 0) {
                isSteam = true;
                break;
            }
            else if (lowerLine.find("map:") == 0) {
                isSteam = false;
                break;
            }
            else if (lowerLine.find("num score ping playerid steamid name") != std::string::npos) {
                isSteam = true;
                break;
            }
            else if (lowerLine.find("num score ping guid name") != std::string::npos) {
                isSteam = false;
                break;
            }
        }
        ss.clear();
        ss.seekg(0, std::ios::beg);

        while (std::getline(ss, line)) {
            // Trim whitespace
            line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](char c) { return !std::isspace(c); }));
            line.erase(std::find_if(line.rbegin(), line.rend(), [](char c) { return !std::isspace(c); }).base(), line.end());
            if (line.empty()) {
                continue;
            }
            // Skip headers
            if (line.find("map:") != std::string::npos ||
                line.find("num score ping") != std::string::npos ||
                line.find("----") != std::string::npos ||
                line.find("hostname:") != std::string::npos ||
                line.find("version :") != std::string::npos ||
                line.find("udp/ip  :") != std::string::npos ||
                line.find("os      :") != std::string::npos ||
                line.find("type    :") != std::string::npos) {
                continue;
            }
            // Tokenize space-separated fields
            std::vector<std::string> tokens;
            std::string token;
            size_t i = 0;
            int fieldCount = 0;
            int nameField = (protocolId == 1) ? 3 : (isSteam ? 5 : 4);
            bool inName = false;

            while (i < line.size()) {
                // Handle name field
                if (fieldCount == nameField) {
                    inName = true;
                    size_t nextField = i;
                    bool foundLastmsg = false;
                    size_t lastCaret7 = std::string::npos;
                    // Find last ^7 before numeric lastmsg
                    while (nextField < line.size() && !foundLastmsg) {
                        size_t numStart = nextField;
                        while (numStart < line.size() && line[numStart] == ' ') numStart++;
                        size_t numEnd = numStart;
                        while (numEnd < line.size() && line[numEnd] != ' ') numEnd++;
                        std::string maybeNumber = line.substr(numStart, numEnd - numStart);
                        if (!maybeNumber.empty() && std::all_of(maybeNumber.begin(), maybeNumber.end(), ::isdigit)) {
                            foundLastmsg = true;
                            break;
                        }
                        if (protocolId == 2 && numStart >= 2 && line[numStart - 2] == '^' && line[numStart - 1] == '7') {
                            lastCaret7 = numStart - 2;
                        }
                        nextField = numEnd + 1;
                    }
                    if (protocolId == 2 && lastCaret7 != std::string::npos && lastCaret7 >= i) {
                        nextField = lastCaret7 + 2;
                        while (nextField < line.size() && line[nextField] == ' ') nextField++;
                    }
                    token = line.substr(i, nextField - i);
                    while (!token.empty() && token.back() == ' ') token.pop_back();
                    if (token.empty()) token = "";
                    tokens.push_back(token);
                    token.clear();
                    fieldCount++;
                    i = nextField;
                    inName = false;
                    while (i < line.size() && line[i] == ' ') i++;
                    continue;
                }
                // Handle other fields
                if (line[i] == ' ' && !token.empty() && !inName) {
                    tokens.push_back(token);
                    token.clear();
                    fieldCount++;
                    i++;
                    while (i < line.size() && line[i] == ' ') i++;
                    continue;
                }
                // Capture last field
                if ((protocolId == 1 && fieldCount == 7) || (protocolId == 2 && fieldCount == (isSteam ? 9 : 8))) {
                    token = line.substr(i);
                    while (!token.empty() && token.back() == ' ') token.pop_back();
                    if (!token.empty()) {
                        tokens.push_back(token);
                    }
                    break;
                }
                token += line[i];
                i++;
            }
            if (!token.empty()) {
                tokens.push_back(token);
            }
            // Process tokens based on protocol
            if (protocolId == 1 && tokens.size() >= 8) { // Medal of Honor
                std::map<std::string, std::string> player;
                player["slot"] = tokens[0];
                player["score"] = tokens[1];
                player["ping"] = tokens[2];
                player["name"] = tokens[3];
                player["lastmsg"] = tokens[4];
                player["address"] = tokens[5];
                player["qport"] = tokens[6];
                player["rate"] = tokens[7];
                if (std::all_of(player["slot"].begin(), player["slot"].end(), ::isdigit) &&
                    (player["score"].empty() || std::all_of(player["score"].begin(), player["score"].end(), ::isdigit) ||
                        (player["score"].size() > 1 && player["score"][0] == '-' &&
                            std::all_of(player["score"].begin() + 1, player["score"].end(), ::isdigit))) &&
                    (player["ping"].empty() || std::all_of(player["ping"].begin(), player["ping"].end(), ::isdigit))) {
                    players.push_back(player);
                }
            }
            else if (protocolId == 2 && tokens.size() >= (isSteam ? 10 : 9)) { // Call of Duty
                std::map<std::string, std::string> player;
                player["slot"] = tokens[0];
                player["score"] = tokens[1];
                player["ping"] = tokens[2];
                if (isSteam) { // Steam
                    player["playerid"] = tokens[3];
                    player["steamid"] = tokens[4];
                    player["name"] = tokens[5];
                    player["lastmsg"] = tokens[6];
                    player["address"] = tokens[7];
                    player["qport"] = tokens[8];
                    player["rate"] = tokens[9];
                }
                else { // Non-Steam
                    player["guid"] = tokens[3];
                    player["name"] = tokens[4];
                    player["lastmsg"] = tokens[5];
                    player["address"] = tokens[6];
                    player["qport"] = tokens[7];
                    player["rate"] = tokens[8];
                }
                if (std::all_of(player["slot"].begin(), player["slot"].end(), ::isdigit) &&
                    (player["score"].empty() || std::all_of(player["score"].begin(), player["score"].end(), ::isdigit) ||
                        (player["score"].size() > 1 && player["score"][0] == '-' &&
                            std::all_of(player["score"].begin() + 1, player["score"].end(), ::isdigit))) &&
                    (player["ping"].empty() || std::all_of(player["ping"].begin(), player["ping"].end(), ::isdigit))) {
                    players.push_back(player);
                }
            }
        }
        return players;
    }

    // Converts key-value pairs and player data to JSON format
    std::string ToJson(const std::map<std::string, std::string>& kv, const std::vector<std::map<std::string, std::string>>& players) {
        std::string result = "{";
        result += "\"server\":{";
        bool first = true;
        for (const auto& pair : kv) {
            if (!first) result += ",";
            result += "\"" + EscapeJson(pair.first) + "\":\"" + EscapeJson(pair.second) + "\"";
            first = false;
        }
        result += "},\"players\":[";
        first = true;
        for (const auto& player : players) {
            if (!first) result += ",";
            result += "{";
            result += "\"slot\":\"" + EscapeJson(player.at("slot")) + "\",";
            result += "\"score\":\"" + EscapeJson(player.at("score")) + "\",";
            result += "\"ping\":\"" + EscapeJson(player.at("ping")) + "\",";
            result += "\"name\":\"" + EscapeJson(player.at("name")) + "\"";
            if (player.count("lastmsg")) result += ",\"lastmsg\":\"" + EscapeJson(player.at("lastmsg")) + "\"";
            if (player.count("address")) result += ",\"address\":\"" + EscapeJson(player.at("address")) + "\"";
            if (player.count("qport")) result += ",\"qport\":\"" + EscapeJson(player.at("qport")) + "\"";
            if (player.count("rate")) result += ",\"rate\":\"" + EscapeJson(player.at("rate")) + "\"";
            if (player.count("guid")) result += ",\"guid\":\"" + EscapeJson(player.at("guid")) + "\"";
            if (player.count("playerid")) result += ",\"playerid\":\"" + EscapeJson(player.at("playerid")) + "\"";
            if (player.count("steamid")) result += ",\"steamid\":\"" + EscapeJson(player.at("steamid")) + "\"";
            result += "}";
            first = false;
        }
        result += "]}";
        return result;
    }

    // Handler for Medal of Honor server commands
    class MedalOfHonorHandler : public ProtocolHandler {
    public:
        std::string ProcessCommand(bool raw, const std::string& ip, int port, const std::string& command, const std::string& rconPassword) override {
            std::string query;
            std::string cmd = SanitizeCommand(command);
            if (cmd == "getstatus") {
                query = "\xFF\xFF\xFF\xFF\x02getstatus";
            }
            else if (cmd.find("rcon ") == 0) {
                query = "\xFF\xFF\xFF\xFF\x02rcon \"" + rconPassword + "\" " + cmd.substr(5);
            }
            else {
                return "error=Invalid command";
            }

            std::string response = SendUDPQuery(ip, port, query, cmd.find("rcon map ") == 0 ? 2000 : 1000);
            if (cmd.find("rcon map ") == 0) {
                std::string map = cmd.substr(9);
                return "{\"status\":\"success\",\"message\":\"Map changed to " + EscapeJson(map) + "\"}";
            }
            if (response.find("error=") == 0 || response.empty()) {
                return response.empty() ? "error=Empty response from server" : response;
            }

            if (cmd == "getstatus") {
                size_t pos = response.find("statusResponse");
                if (pos == std::string::npos) {
                    return "error=Invalid server response;raw=" + response;
                }
                response = response.substr(pos + 14);
                if (response.empty()) {
                    return "error=Empty response after header removal;raw=" + response;
                }
                if (raw) {
                    return response;
                }
                auto kv = ParseKeyValues(response);
                auto players = ParseGetStatusPlayers(response, 1);
                return ToJson(kv, players);
            }
            else if (cmd.find("rcon ") == 0) {
                size_t pos = response.find("print");
                if (pos == std::string::npos) {
                    return "error=Invalid server response;raw=" + response;
                }
                response = response.substr(pos + 5);
                if (response.empty()) {
                    return "error=Empty response after header removal;raw=" + response;
                }
                if (cmd == "rcon status") {
                    if (raw) {
                        return response;
                    }
                    auto players = ParseRconStatusPlayers(response, 1);
                    std::string result = "{\"players\":[";
                    bool first = true;
                    for (const auto& player : players) {
                        if (!first) result += ",";
                        result += "{";
                        result += "\"slot\":\"" + EscapeJson(player.at("slot")) + "\",";
                        result += "\"score\":\"" + EscapeJson(player.at("score")) + "\",";
                        result += "\"ping\":\"" + EscapeJson(player.at("ping")) + "\",";
                        result += "\"name\":\"" + EscapeJson(player.at("name")) + "\",";
                        result += "\"lastmsg\":\"" + EscapeJson(player.at("lastmsg")) + "\",";
                        result += "\"address\":\"" + EscapeJson(player.at("address")) + "\",";
                        result += "\"qport\":\"" + EscapeJson(player.at("qport")) + "\",";
                        result += "\"rate\":\"" + EscapeJson(player.at("rate")) + "\"";
                        result += "}";
                        first = false;
                    }
                    result += "]}";
                    return result;
                }
                else {
                    response.erase(0, response.find_first_not_of("\n")); // Trim leading newlines
                    return raw ? response : "{\"response\":\"" + EscapeJson(response) + "\"}";
                }
            }
            return "error=Unsupported command";
        }
    };

    // Handler for Call of Duty server commands
    class CallOfDutyHandler : public ProtocolHandler {
    public:
        std::string ProcessCommand(bool raw, const std::string& ip, int port, const std::string& command, const std::string& rconPassword) override {
            std::string query;
            std::string cmd = SanitizeCommand(command);
            if (cmd == "getinfo") {
                query = "\xFF\xFF\xFF\xFFgetinfo";
            }
            else if (cmd == "getstatus") {
                query = "\xFF\xFF\xFF\xFFgetstatus";
            }
            else if (cmd.find("rcon ") == 0) {
                query = "\xFF\xFF\xFF\xFFrcon \"" + rconPassword + "\" " + cmd.substr(5);
            }
            else {
                return "error=Invalid command";
            }

            std::string response = SendUDPQuery(ip, port, query, cmd.find("rcon map ") == 0 ? 2000 : 1000);
            if (cmd.find("rcon map ") == 0) {
                std::string map = cmd.substr(9);
                return "{\"status\":\"success\",\"message\":\"Map changed to " + EscapeJson(map) + "\"}";
            }
            if (response.find("error=") == 0 || response.empty()) {
                return response.empty() ? "error=Empty response from server" : response;
            }

            if (cmd == "getinfo" || cmd == "getstatus") {
                size_t pos = response.find(cmd == "getinfo" ? "infoResponse" : "statusResponse");
                if (pos == std::string::npos) {
                    return "error=Invalid server response;raw=" + response;
                }
                response = response.substr(pos + (cmd == "getinfo" ? 12 : 14));
                if (response.empty()) {
                    return "error=Empty response after header removal;raw=" + response;
                }
                if (raw) {
                    return response;
                }
                auto kv = ParseKeyValues(response);
                auto players = ParseGetStatusPlayers(response, 2);
                return ToJson(kv, players);
            }
            else if (cmd.find("rcon ") == 0) {
                size_t pos = response.find("print");
                if (pos == std::string::npos) {
                    return "error=Invalid server response;raw=" + response;
                }
                response = response.substr(pos + 5);
                if (response.empty()) {
                    return "error=Empty response after header removal;raw=" + response;
                }
                if (cmd == "rcon status") {
                    if (raw) {
                        return response;
                    }
                    auto players = ParseRconStatusPlayers(response, 2);
                    std::string result = "{\"players\":[";
                    bool first = true;
                    for (const auto& player : players) {
                        if (!first) result += ",";
                        result += "{";
                        result += "\"slot\":\"" + EscapeJson(player.at("slot")) + "\",";
                        result += "\"score\":\"" + EscapeJson(player.at("score")) + "\",";
                        result += "\"ping\":\"" + EscapeJson(player.at("ping")) + "\",";
                        result += "\"name\":\"" + EscapeJson(player.at("name")) + "\",";
                        result += "\"lastmsg\":\"" + EscapeJson(player.at("lastmsg")) + "\",";
                        result += "\"address\":\"" + EscapeJson(player.at("address")) + "\",";
                        result += "\"qport\":\"" + EscapeJson(player.at("qport")) + "\",";
                        result += "\"rate\":\"" + EscapeJson(player.at("rate")) + "\"";
                        if (player.count("guid")) result += ",\"guid\":\"" + EscapeJson(player.at("guid")) + "\"";
                        if (player.count("playerid")) result += ",\"playerid\":\"" + EscapeJson(player.at("playerid")) + "\"";
                        if (player.count("steamid")) result += ",\"steamid\":\"" + EscapeJson(player.at("steamid")) + "\"";
                        result += "}";
                        first = false;
                    }
                    result += "]}";
                    return result;
                }
                else {
                    response.erase(0, response.find_first_not_of("\n")); // Trim leading newlines
                    return raw ? response : "{\"response\":\"" + EscapeJson(response) + "\"}";
                }
            }
            return "error=Unsupported command";
        }
    };

    // Initializes protocol registry with handlers
    struct ProtocolInitializer {
        ProtocolInitializer() {
            protocolRegistry[1] = std::make_unique<MedalOfHonorHandler>();
            protocolRegistry[2] = std::make_unique<CallOfDutyHandler>();
        }
    } initializer;
}

// Resolves hostname to IP address with DNS caching
std::string ResolveHostname(const std::string& hostname) {
    std::lock_guard<std::mutex> lock(dnsMutex);
    auto now = std::chrono::steady_clock::now();
    auto it = dnsCache.find(hostname);
    if (it != dnsCache.end()) {
        auto age = std::chrono::duration_cast<std::chrono::minutes>(now - it->second.timestamp).count();
        if (age < 5) return it->second.ip;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return "error=Winsock initialization failed";
    }

    addrinfo hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(hostname.c_str(), nullptr, &hints, &result) != 0) {
        WSACleanup();
        return "error=Failed to resolve hostname";
    }

    std::string ip;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        char ipStr[INET_ADDRSTRLEN];
        auto* sa = (sockaddr_in*)ptr->ai_addr;
        inet_ntop(AF_INET, &sa->sin_addr, ipStr, sizeof(ipStr));
        ip = ipStr;
        break;
    }
    freeaddrinfo(result);
    WSACleanup();

    if (!ip.empty()) {
        dnsCache[hostname] = { ip, now };
    }
    return ip.empty() ? "error=Failed to resolve hostname" : ip;
}

// Sends UDP query to game server and returns response
std::string SendUDPQuery(const std::string& ip, int port, const std::string& query, int timeoutMs) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return "error=Winsock initialization failed";
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return "error=Socket creation failed";
    }

    // Set receive timeout
    DWORD timeout = timeoutMs;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, ip.c_str(), &server.sin_addr) <= 0) {
        closesocket(sock);
        WSACleanup();
        return "error=Invalid IP address";
    }

    if (sendto(sock, query.c_str(), static_cast<int>(query.size()), 0, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return "error=Send failed";
    }

    char buffer[4096];
    int addrLen = sizeof(server);
    int bytesReceived = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&server, &addrLen);
    if (bytesReceived == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return "error=Receive failed";
    }

    buffer[bytesReceived] = '\0';
    std::string response(buffer);
    closesocket(sock);
    WSACleanup();
    return response;
}

// Processes game server command and returns response
extern "C" const char* ProcessGameServerCommand(int protocolId, bool raw, const char* ipOrHostname, int port, const char* command, const char* rconPassword) {
    try {
        if (!ipOrHostname || !command) {
            return _strdup("error=Null input parameters");
        }
        if (port < 1 || port > 65535) {
            return _strdup("error=Invalid port");
        }
        auto it = protocolRegistry.find(protocolId);
        if (it == protocolRegistry.end()) {
            return _strdup("error=Invalid protocol ID");
        }

        std::string ip = ResolveHostname(ipOrHostname);
        if (ip.find("error=") == 0) {
            return _strdup(ip.c_str());
        }

        std::string cmd = SanitizeCommand(command);
        if (cmd.empty()) {
            return _strdup("error=Empty command");
        }

        std::string rcon = rconPassword ? rconPassword : "";
        std::string result = it->second->ProcessCommand(raw, ip, port, cmd, rcon);
        return _strdup(result.c_str());
    }
    catch (...) {
        return _strdup("error=Unexpected exception");
    }
}

// Frees memory allocated for game server response
extern "C" void FreeGameServerResponse(const char* response) {
    if (response) {
        free((void*)response);
    }
}