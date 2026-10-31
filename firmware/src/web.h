// net + web: Wi-Fi softAP, captive DNS, the test page and a small JSON API.
#pragma once

namespace web {

void begin();  // start the access point, DNS and HTTP server
void loop();   // DNS requests and deferred actions (speaker change reboots)
int clients(); // phones joined to the access point
bool wifiOn(); // false in phone-free mode (F15): no access point this boot
// Tells the pages, then restarts the board about 1 s later (main loop). `reason` goes to the pages.
void restart(const char *reason);
int requestsInFlight();  // HTTP requests being served now

}  // namespace web
