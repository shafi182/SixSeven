#include "export_manager.h"

// DNS port for Captive Portal
const byte DNS_PORT = 53;

ExportManager* ExportManager::instance = nullptr;

ExportManager::ExportManager() {
    webServer = nullptr;
    dnsServer = nullptr;
    apActive = false;
    dosenNIP = "";
    dosenNama = "";
    instance = this;
}

ExportManager::~ExportManager() {
    stopExportMode();
}

void ExportManager::startExportMode(const String& nip, const String& nama) {
    if (apActive) return;

    dosenNIP = nip;
    dosenNama = nama;

    Serial.println(F("[EXPORT] Starting AP & Captive Portal..."));
    
    // Disconnect station mode if active
    WiFi.disconnect(true);
    delay(100);

    // Setup AP Mode
    WiFi.mode(WIFI_AP);
    
    // Configure AP IP
    IPAddress apIP(192, 168, 4, 1);
    IPAddress netMsk(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apIP, netMsk);
    
    // Start AP with SSID
    WiFi.softAP("SixSeven");

    // Setup DNS Server for Captive Portal (redirect all to apIP)
    dnsServer = new DNSServer();
    dnsServer->start(DNS_PORT, "*", apIP);

    // Setup Web Server
    webServer = new WebServer(80);
    
    // Register handlers using static wrappers
    webServer->on("/", handleRootWrapper);
    webServer->on("/download", handleDownloadWrapper);
    webServer->onNotFound(handleNotFoundWrapper);
    
    webServer->begin();
    
    apActive = true;
    Serial.println(F("[EXPORT] AP Mode Active."));
}

void ExportManager::stopExportMode() {
    if (!apActive) return;

    Serial.println(F("[EXPORT] Stopping AP..."));

    if (webServer) {
        webServer->stop();
        delete webServer;
        webServer = nullptr;
    }

    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA); // Return to STA mode
    
    apActive = false;
    dosenNIP = "";
    dosenNama = "";
}

void ExportManager::process() {
    if (apActive) {
        if (dnsServer) dnsServer->processNextRequest();
        if (webServer) webServer->handleClient();
    }
}

bool ExportManager::isActive() {
    return apActive;
}

// Static Wrappers
void ExportManager::handleRootWrapper() {
    if (instance) instance->handleRoot();
}

void ExportManager::handleDownloadWrapper() {
    if (instance) instance->handleDownload();
}

void ExportManager::handleNotFoundWrapper() {
    if (instance) instance->handleNotFound();
}

// Handlers
void ExportManager::handleRoot() {
    Serial.println(F("[EXPORT] Serving Root Page"));
    String html = buildHTMLPage();
    webServer->send(200, "text/html", html);
}

void ExportManager::handleNotFound() {
    // Captive portal redirect to root
    webServer->sendHeader("Location", String("http://192.168.4.1/"), true);
    webServer->send(302, "text/plain", "");
}

String ExportManager::buildHTMLPage() {
    String html = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<title>Ekspor Presensi</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background-color: #f4f4f9; color: #333; }";
    html += "h1 { font-size: 1.5em; color: #4CAF50; }";
    html += ".card { background: white; padding: 15px; margin-bottom: 15px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
    html += "a.btn { display: inline-block; background-color: #007bff; color: white; text-decoration: none; padding: 10px 15px; border-radius: 4px; margin-top: 10px; cursor: pointer; transition: all 0.3s ease; }";
    html += "a.btn:hover { background-color: #0056b3; }";
    html += "a.btn.loading { background-color: #6c757d; cursor: not-allowed; }";
    html += "</style>";
    html += "<script>";
    html += "function startDownload(btn, url) {";
    html += "  if(btn.classList.contains('loading')) return;";
    html += "  var originalText = btn.innerHTML;";
    html += "  btn.innerHTML = '&#8987; Downloading...';";
    html += "  btn.classList.add('loading');";
    html += "  var iframe = document.createElement('iframe');";
    html += "  iframe.style.display = 'none';";
    html += "  iframe.src = url;";
    html += "  document.body.appendChild(iframe);";
    html += "  setTimeout(function() {";
    html += "    btn.innerHTML = originalText;";
    html += "    btn.classList.remove('loading');";
    html += "    document.body.removeChild(iframe);";
    html += "  }, 3500);"; // Reset button after 3.5 seconds
    html += "}";
    html += "</script>";
    html += "</head><body>";
    
    html += "<h1>Portal Ekspor Presensi</h1>";
    html += "<p>Dosen: <b>" + dosenNama + "</b> (NIP: " + dosenNIP + ")</p>";
    html += "<hr>";
    html += "<h3>Daftar Kelas:</h3>";

    // Read dosen_kelas.csv
    File file = SD.open("/dosen_kelas.csv", FILE_READ);
    if (!file) {
        html += "<p>Gagal membaca data kelas.</p></body></html>";
        return html;
    }

    if (file.available()) file.readStringUntil('\n'); // Skip header
    
    int count = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        line.replace("\r", "");
        if (line.length() < 5) continue;
        
        // id,nip,kode_kelas,kelas
        int p1 = line.indexOf(',');
        int p2 = line.indexOf(',', p1 + 1);
        int p3 = line.indexOf(',', p2 + 1);
        int p4 = line.indexOf(',', p3 + 1);
        
        if (p1 > 0 && p2 > 0 && p3 > 0) {
            String csvNip = line.substring(p1 + 1, p2);
            csvNip.trim();
            
            if (csvNip == dosenNIP) {
                String kodeMk = line.substring(p2 + 1, p3);
                kodeMk.trim();
                
                String kelas = "";
                if (p4 > 0 && p4 > p3) {
                    kelas = line.substring(p3 + 1, p4);
                } else {
                    kelas = line.substring(p3 + 1);
                }
                kelas.trim();

                // Get nama_mk from kelas.csv
                String namaMk = kodeMk; // fallback
                File kFile = SD.open("/kelas.csv", FILE_READ);
                if (kFile) {
                    if (kFile.available()) kFile.readStringUntil('\n');
                    while (kFile.available()) {
                        String kLine = kFile.readStringUntil('\n');
                        kLine.trim();
                        // kode_mk,kelas,nama_mk,sks or kode_kelas,nama_kelas
                        int kp1 = kLine.indexOf(',');
                        if (kp1 > 0) {
                            String kKode = kLine.substring(0, kp1);
                            if (kKode == kodeMk) {
                                // Extract nama_mk based on commas
                                int kp2 = kLine.indexOf(',', kp1 + 1);
                                if (kp2 > 0) {
                                    // Could be 4 cols format
                                    String kKls = kLine.substring(kp1 + 1, kp2);
                                    if (kKls == kelas || kKls.length() == 0) { // If matches or we just take first match
                                        int kp3 = kLine.indexOf(',', kp2 + 1);
                                        if (kp3 > 0) namaMk = kLine.substring(kp2 + 1, kp3);
                                        else namaMk = kLine.substring(kp2 + 1);
                                    }
                                } else {
                                    // 2 cols format
                                    namaMk = kLine.substring(kp1 + 1);
                                }
                                break;
                            }
                        }
                    }
                    kFile.close();
                }

                html += "<div class='card'>";
                html += "<h4>" + kodeMk + " - " + namaMk + " (Kelas " + kelas + ")</h4>";
                html += "<a class='btn' onclick=\"startDownload(this, '/download?kode_mk=" + kodeMk + "&kelas=" + kelas + "')\">Unduh CSV</a>";
                html += "</div>";
                count++;
            }
        }
    }
    file.close();

    if (count == 0) {
        html += "<p>Belum ada data kelas untuk dosen ini.</p>";
    }

    html += "</body></html>";
    return html;
}

void ExportManager::handleDownload() {
    if (!webServer->hasArg("kode_mk") || !webServer->hasArg("kelas")) {
        webServer->send(400, "text/plain", "Missing parameters");
        return;
    }

    String reqKodeMk = webServer->arg("kode_mk");
    String reqKelas = webServer->arg("kelas");

    Serial.printf("[EXPORT] Generating CSV for %s Kelas %s\n", reqKodeMk.c_str(), reqKelas.c_str());

    // Prepare headers
    String fileName = "Presensi_" + reqKodeMk + "_" + reqKelas + ".csv";
    webServer->sendHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
    
    // Chunked transfer for CSV
    webServer->setContentLength(CONTENT_LENGTH_UNKNOWN);
    webServer->send(200, "text/csv", "");

    // 1. Get all pertemuan (sessions) for this class
    String pertemuanIds[20];
    String pertemuanTgls[20];
    int pCount = 0;
    
    File pFile = SD.open("/pertemuan.csv", FILE_READ);
    if (pFile) {
        if (pFile.available()) pFile.readStringUntil('\n'); // Skip header
        while (pFile.available() && pCount < 20) {
            String line = pFile.readStringUntil('\n');
            line.trim();
            if (line.length() < 5) continue;
            
            // id,server_jadwal_id,kode_kelas,kelas,pertemuan_ke,tanggal
            int c1 = line.indexOf(',');
            int c2 = line.indexOf(',', c1 + 1);
            int c3 = line.indexOf(',', c2 + 1);
            int c4 = line.indexOf(',', c3 + 1);
            int c5 = line.indexOf(',', c4 + 1);
            
            if (c1 > 0 && c2 > 0 && c3 > 0 && c4 > 0 && c5 > 0) {
                String pServerId = line.substring(c1 + 1, c2); // Use server_jadwal_id
                String pKode = line.substring(c2 + 1, c3);
                String pKelas = line.substring(c3 + 1, c4);
                String pTgl = line.substring(c5 + 1);
                
                if (pKode == reqKodeMk && pKelas == reqKelas) {
                    pertemuanIds[pCount] = pServerId;
                    pertemuanTgls[pCount] = pTgl;
                    pCount++;
                }
            }
        }
        pFile.close();
    }

    // 2. Build and send CSV header
    String csvHeader = "No,NIM,Nama";
    for (int i = 0; i < pCount; i++) {
        csvHeader += "," + pertemuanTgls[i];
    }
    csvHeader += "\n";
    webServer->sendContent(csvHeader);

    // 3. Process mahasiswa for this class
    File mFile = SD.open("/kelas_mahasiswa.csv", FILE_READ);
    if (!mFile) {
        webServer->sendContent(""); // End chunk
        return;
    }

    if (mFile.available()) mFile.readStringUntil('\n'); // Skip header
    
    int no = 1;
    while (mFile.available()) {
        String line = mFile.readStringUntil('\n');
        line.trim();
        line.replace("\r", "");
        if (line.length() < 5) continue;
        
        // id,kode_kelas,kelas,nim,sit_in,status_fingerprint
        int c1 = line.indexOf(',');
        int c2 = line.indexOf(',', c1 + 1);
        int c3 = line.indexOf(',', c2 + 1);
        int c4 = line.indexOf(',', c3 + 1);
        
        if (c1 > 0 && c2 > 0 && c3 > 0 && c4 > 0) {
            String mKode = line.substring(c1 + 1, c2);
            String mKelas = line.substring(c2 + 1, c3);
            String mNim = line.substring(c3 + 1, c4);
            
            if (mKode == reqKodeMk && mKelas == reqKelas) {
                String mNama = lookupNamaMahasiswa(mNim);
                
                String row = String(no++) + "," + mNim + "," + mNama;
                
                // Get attendance for each session
                for (int i = 0; i < pCount; i++) {
                    String status = lookupStatusPresensi(mNim, reqKodeMk, reqKelas, pertemuanIds[i]);
                    if (status.length() == 0) status = "-"; // Default if not found
                    row += "," + status;
                }
                row += "\n";
                
                webServer->sendContent(row);
            }
        }
    }
    mFile.close();

    // End chunked transfer
    webServer->sendContent("");
}

String ExportManager::lookupNamaMahasiswa(const String& nim) {
    String nama = "Unknown";
    File f = SD.open("/mahasiswa.csv", FILE_READ);
    if (f) {
        if (f.available()) f.readStringUntil('\n'); // Skip header
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() < 5) continue;
            
            // nim,nama_lengkap,created_at
            int p1 = line.indexOf(',');
            if (p1 > 0) {
                String csvNim = line.substring(0, p1);
                if (csvNim == nim) {
                    int p2 = line.indexOf(',', p1 + 1);
                    if (p2 > 0) nama = line.substring(p1 + 1, p2);
                    else nama = line.substring(p1 + 1);
                    break;
                }
            }
        }
        f.close();
    }
    return nama;
}

String ExportManager::lookupStatusPresensi(const String& nim, const String& kodeKelas, const String& kelas, const String& pertemuanId) {
    String status = "";
    File f = SD.open("/presensi.csv", FILE_READ);
    if (f) {
        if (f.available()) f.readStringUntil('\n'); // Skip header
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() < 5) continue;
            
            // id,nim,kode_kelas,pertemuan_id,waktu,status
            int c1 = line.indexOf(',');
            int c2 = line.indexOf(',', c1 + 1);
            int c3 = line.indexOf(',', c2 + 1);
            int c4 = line.indexOf(',', c3 + 1);
            int c5 = line.indexOf(',', c4 + 1);
            
            if (c1 > 0 && c2 > 0 && c3 > 0 && c4 > 0 && c5 > 0) {
                String csvNim = line.substring(c1 + 1, c2);
                String csvKode = line.substring(c2 + 1, c3);
                String csvPertemuan = line.substring(c3 + 1, c4);
                
                // presensi.csv saves kode_kelas field as "kode_mk-kelas" (e.g. "EL2222-1")
                if (csvNim == nim && csvKode == (kodeKelas + "-" + kelas) && csvPertemuan == pertemuanId) {
                    status = line.substring(c5 + 1);
                    break; // Found
                }
            }
        }
        f.close();
    }
    return status;
}
