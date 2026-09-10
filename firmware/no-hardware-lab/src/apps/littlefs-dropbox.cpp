#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>

#include "app_support.h"
#include "lab_logic.h"

namespace {
constexpr size_t kMaximumFileBytes = 128U * 1024U;
LocalPortal portal;
File upload_file;
String target_path;
String temporary_path;
String backup_path;
size_t upload_bytes = 0;
bool upload_failed = false;
bool filesystem_ready = false;

bool removeIfPresent(const String& path) {
    return path.isEmpty() || !LittleFS.exists(path) || LittleFS.remove(path);
}

bool restoreInterruptedReplacement() {
    if (backup_path.isEmpty() || !LittleFS.exists(backup_path)) return true;
    if (LittleFS.exists(target_path)) return LittleFS.remove(backup_path);
    return LittleFS.rename(backup_path, target_path);
}

bool prepareTemporaryUpload() {
    if (!restoreInterruptedReplacement()) return false;
    return removeIfPresent(temporary_path);
}

bool discardTemporaryUpload() {
    if (upload_file) upload_file.close();
    const bool removed = removeIfPresent(temporary_path);
    if (!removed) Serial.println("Failed to remove temporary upload");
    return removed;
}

bool commitTemporaryUpload() {
    const bool had_target = LittleFS.exists(target_path);
    if (had_target && !LittleFS.rename(target_path, backup_path)) return false;
    if (!LittleFS.rename(temporary_path, target_path)) {
        if (had_target && !LittleFS.rename(backup_path, target_path)) {
            Serial.println("Critical: failed to restore previous file");
        }
        return false;
    }
    if (had_target && !removeIfPresent(backup_path)) {
        Serial.println("Uploaded file committed but backup cleanup failed");
    }
    return true;
}

String requestedFilename() {
    const String candidate = portal.server.arg("name");
    return isSafeFilename(candidate.c_str()) ? candidate : String();
}

void serveHome() {
    String files;
    if (filesystem_ready) {
        File root = LittleFS.open("/");
        File file = root.openNextFile();
        while (file) {
            const String path = file.name();
            const String name = path.substring(path.lastIndexOf('/') + 1);
            if (isSafeFilename(name.c_str())) {
                files += "<div class=card><strong>" + htmlEscape(name) + "</strong><br><small>" + String(file.size()) + " bytes</small><br>";
                files += "<a href='./download?name=" + name + "'>Download</a> · <form method=post action=./delete style='display:inline'><input type=hidden name=name value='" + name + "'><button type=submit>Delete</button></form></div>";
            }
            file = root.openNextFile();
        }
        root.close();
    }
    if (files.isEmpty()) files = filesystem_ready ? "<p class=muted>No files stored yet.</p>" : "<p>Filesystem unavailable. Existing data was not formatted.</p>";
    String body = "<p class=muted>Join <code>ESP32-Pocket-Drop</code>. Anyone nearby on this open network can download, replace, or delete files.</p>";
    body += "<div class=card><form method=post action=./upload enctype=multipart/form-data><input type=file name=file required style='width:100%;padding-top:10px'><button class=primary type=submit>Upload file</button></form><small>Maximum 128 KiB. Names may contain letters, numbers, dots, dashes, and underscores.</small></div><h2>Stored files</h2>";
    body += files;
    portal.server.send(200, "text/html; charset=utf-8", pageShell("Pocket File Drop", body));
}

void handleUploadData() {
    HTTPUpload& upload = portal.server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        if (upload_file) upload_file.close();
        upload_bytes = 0;
        upload_failed = !filesystem_ready || !isSafeFilename(upload.filename.c_str());
        if (!upload_failed) {
            target_path = "/" + upload.filename;
            temporary_path = "/." + upload.filename + ".tmp";
            backup_path = "/." + upload.filename + ".bak";
            upload_failed = !prepareTemporaryUpload();
        }
        if (!upload_failed) {
            upload_file = LittleFS.open(temporary_path, FILE_WRITE);
            upload_failed = !upload_file;
        }
    } else if (upload.status == UPLOAD_FILE_WRITE && !upload_failed) {
        if (upload_bytes + upload.currentSize > kMaximumFileBytes || upload_file.write(upload.buf, upload.currentSize) != upload.currentSize) {
            upload_failed = true;
            discardTemporaryUpload();
        } else {
            upload_bytes += upload.currentSize;
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (upload_file) upload_file.close();
        if (upload_failed || upload_bytes != upload.totalSize || !commitTemporaryUpload()) {
            discardTemporaryUpload();
            upload_failed = true;
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        discardTemporaryUpload();
        upload_failed = true;
    }
}

void finishUpload() {
    if (upload_failed) portal.server.send(400, "text/plain", "Upload rejected or failed; previous file preserved when possible");
    else {
        portal.server.sendHeader("Location", "/", true);
        portal.server.send(303);
    }
}

void downloadFile() {
    if (!filesystem_ready) return portal.server.send(503, "text/plain", "Filesystem unavailable");
    const String name = requestedFilename();
    if (name.isEmpty()) return portal.server.send(400, "text/plain", "Invalid filename");
    File file = LittleFS.open("/" + name, FILE_READ);
    if (!file || file.isDirectory()) return portal.server.send(404, "text/plain", "Not found");
    portal.server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    portal.server.streamFile(file, "application/octet-stream");
    file.close();
}

void deleteFile() {
    if (!filesystem_ready) return portal.server.send(503, "text/plain", "Filesystem unavailable");
    const String name = requestedFilename();
    if (name.isEmpty() || !LittleFS.remove("/" + name)) return portal.server.send(400, "text/plain", "Delete failed");
    portal.server.sendHeader("Location", "/", true);
    portal.server.send(303);
}

void mountFilesystem() {
    Preferences state;
    const bool state_ready = state.begin("filedrop", false);
    const bool initialized_before = state_ready && state.getBool("initialized", false);
    filesystem_ready = LittleFS.begin(false);
    if (shouldFormatFilesystem(state_ready, initialized_before, filesystem_ready)) {
        filesystem_ready = LittleFS.format() && LittleFS.begin(false);
    }
    bool marker_persisted = initialized_before;
    if (filesystem_ready && state_ready && !initialized_before) {
        marker_persisted = state.putBool("initialized", true) == 1;
        if (!marker_persisted) Serial.println("Failed to persist filesystem initialization marker");
    }
    const bool mounted = filesystem_ready;
    filesystem_ready = filesystemInitializationIsDurable(mounted, initialized_before, marker_persisted);
    if (state_ready) state.end();
    if (!filesystem_ready) {
        if (mounted) LittleFS.end();
        Serial.println("LittleFS unavailable; refusing access without a durable initialization marker");
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    mountFilesystem();
    portal.server.on("/", HTTP_GET, serveHome);
    portal.server.on("/upload", HTTP_POST, finishUpload, handleUploadData);
    portal.server.on("/download", HTTP_GET, downloadFile);
    portal.server.on("/delete", HTTP_POST, deleteFile);
    portal.begin("ESP32-Pocket-Drop");
}

void loop() {
    portal.handle();
    delay(2);
}
