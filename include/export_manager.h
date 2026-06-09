#ifndef EXPORT_MANAGER_H
#define EXPORT_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SD.h>

class ExportManager {
private:
    WebServer* webServer;
    DNSServer* dnsServer;
    bool apActive;
    String dosenNIP;
    String dosenNama;

    // Web Server Handlers
    static void handleRootWrapper();
    static void handleDownloadWrapper();
    static void handleNotFoundWrapper();

    void handleRoot();
    void handleDownload();
    void handleNotFound();

    String buildHTMLPage();

    // Data Lookups (Optimized for RAM)
    String lookupNamaMahasiswa(const String& nim);
    String lookupStatusPresensi(const String& nim, const String& kodeKelas, const String& kelas, const String& pertemuanId);

    // Global instance pointer for static wrappers
    static ExportManager* instance;

public:
    ExportManager();
    ~ExportManager();

    void startExportMode(const String& nip, const String& nama);
    void stopExportMode();
    void process();
    bool isActive();
};

#endif // EXPORT_MANAGER_H
