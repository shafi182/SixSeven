#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <vector>

// WiFi Configuration
#define WIFI_AP_SSID "SixSeven"
#define WIFI_AP_IP "192.168.4.1"
#define WIFI_CONNECT_TIMEOUT 15000  // 15 seconds
#define WIFI_CONFIG_PORT 80

// Single credential struct (support WPA2-Personal and WPA2-Enterprise)
struct WiFiCredential {
    String ssid;
    String password;
    String username;  // Untuk WPA2-Enterprise (kosong untuk WPA2-Personal)
};

// Legacy credentials struct (for backward compatibility)
struct WiFiCredentials {
    String ssid;
    String password;
    bool saved;
};

class WiFiManager {
private:
    bool apStarted;
    bool serverStarted;
    WebServer* webServer;
    DNSServer* dnsServer;

    // Connection state
    bool connecting;
    unsigned long connectStartTime;
    String targetSSID;
    String targetPassword;
    String targetUsername;  // Untuk WPA2-Enterprise
    bool credentialsReceived;  // Flag: user submitted credentials via web

    // Scan results
    int foundNetworks;
    String scannedSSIDs[20];
    int scannedRSSI[20];

    // Internal methods
    void handleRoot();
    void handleConnect();
    void startWebServer();
    void stopWebServer();

public:
    WiFiManager();
    ~WiFiManager();

    // Start AP mode for configuration
    bool startConfigMode();

    // Stop AP mode
    void stopConfigMode();

    // Process web server (non-blocking)
    void process();

    // Check if currently connecting
    bool isConnecting();

    // Start connection to WiFi network (support WPA2-Personal and WPA2-Enterprise)
    void startConnection(const String& ssid, const String& password, const String& username = "");

    // Check connection status
    bool isConnected();

    // Get connection result (call after isConnected returns true/false)
    wl_status_t getConnectionStatus();

    // Cancel ongoing connection
    void cancelConnection();

    // Save credentials to SD (Multi-Credential - support WPA2-Enterprise)
    bool saveCredentials(const String& ssid, const String& password, const String& username = "");

    // Load all credentials from SD to vector
    std::vector<WiFiCredential> loadAllCredentials();

    // Load first credential (legacy compatibility)
    WiFiCredentials loadCredentials();

    // Smart auto-connect at startup
    bool smartAutoConnect();

    // Get current SSID
    String getCurrentSSID();

    // Get target password (for saving after connection)
    String getTargetPassword();

    // Get target username (for WPA2-Enterprise)
    String getTargetUsername();

    // Check if credentials were received from web form
    bool hasCredentialsReceived();

    // Clear credentials received flag
    void clearCredentialsReceived();

    // Get signal icon based on RSSI
    String getSignalIcon(int rssi);

    // Connect to a saved network by SSID (reads password from wifi.csv)
    bool connectToSavedNetwork(const String& ssid);
};

// Global instance
extern WiFiManager wifiManager;