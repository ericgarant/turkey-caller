// uploads: phone recordings saved to /sounds/ on the SD card (F03 on the WROVER-E, F12).
// One upload at a time. The file streams to "<id>.tmp" and is renamed to "<id>.wav" only once
// it is complete and checked, so a dropped connection never leaves half a sound in the list.
#pragma once
#include <Arduino.h>

namespace uploads {

struct Result {
  int code;         // HTTP status: 200, 400, 409, 413, 500 or 503
  const char *msg;  // error text for the page ("" on success)
};

// Called from the web server's upload callback, in order: start() with the file name and the
// request's Content-Length, then data() for each chunk, then finish(). Every call returns the
// result so far; once it is not 200 the rest is ignored and the temp file is gone.
// `owner` is the request: chunks of a second, refused upload never touch the open one.
Result start(const void *owner, const char *filename, size_t contentLength);
Result data(const void *owner, const uint8_t *buf, size_t len);
Result finish(const void *owner);
// Drops an upload that stopped sending (the phone left Wi-Fi). From web::loop().
void loop();
bool active();
// Remounts the SD card after a read error (sdcard::remount), holding off uploads and deletes
// meanwhile. Refuses while an upload is open. From the player's task.
bool remountCard();

// Deletes an upload ("mine") from the card. Built-in and pack calls can't be deleted.
Result remove(const char *id);

}  // namespace uploads
