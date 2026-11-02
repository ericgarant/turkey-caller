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

// F16: an upload's labels, from the form fields `name`, `animal` and `hint`. Fields the request
// left out keep their current value (or the default: name from the id, turkey, no hint).
struct Labels {
  bool hasName = false, hasAnimal = false, hasHint = false;
  char name[33] = "";   // 32 bytes of UTF-8 at most; "" = the default name from the id
  char animal[8] = "";  // turkey, moose or deer
  char hint[48] = "";   // 47 bytes at most
};
// Checks and cleans the fields (nullptr = not sent): control characters become spaces, the
// ends are trimmed, and long text is cut at a character boundary. 400 for an unknown animal.
Result makeLabels(const char *name, const char *animal, const char *hint, Labels &out);

// Called from the web server's upload callback, in order: start() with the file name and the
// request's Content-Length, then data() for each chunk, then finish(). Every call returns the
// result so far; once it is not 200 the rest is ignored and the temp file is gone.
// `owner` is the request: chunks of a second, refused upload never touch the open one.
// `labels` (F16) go into the sidecar /sounds/<id>.json once the file is saved.
Result start(const void *owner, const char *filename, size_t contentLength, const Labels &labels);
Result data(const void *owner, const uint8_t *buf, size_t len);
Result finish(const void *owner);
// Drops an upload that stopped sending (the phone left Wi-Fi). From web::loop().
void loop();
bool active();
// Remounts the SD card after a read error (sdcard::remount), holding off uploads and deletes
// meanwhile. Refuses while an upload is open. From the player's task.
bool remountCard();

// Deletes an upload ("mine") from the card, and its sidecar. Built-in and pack calls can't be deleted.
Result remove(const char *id);
// F16: renames an upload, moves it to another animal or changes its hint, by rewriting its
// sidecar; the audio is not touched. Uploads only.
Result edit(const char *id, const Labels &labels);

}  // namespace uploads
