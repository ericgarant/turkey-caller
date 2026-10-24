// net + web: Wi-Fi softAP, captive DNS, the test page and a small JSON API.
#pragma once

namespace web {

void begin();  // start the access point, DNS and HTTP server
void loop();   // DNS requests and deferred actions (speaker change reboots)
int clients(); // phones joined to the access point
int requestsInFlight();  // HTTP requests being served now

}  // namespace web
