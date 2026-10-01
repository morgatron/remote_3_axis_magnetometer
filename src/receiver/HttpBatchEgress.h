#ifndef HTTP_BATCH_EGRESS_H
#define HTTP_BATCH_EGRESS_H

#include <Arduino.h>
#include "TelemetryPacket.h"

/**
 * @brief Handles HTTP/HTTPS JSON batch egress directly to the Central Data Server.
 */
class HttpBatchEgress {
public:
    static void begin();
    static void loadSettings();
    static void saveSettings();

    static bool hasServerUrl();
    static String getServerUrl();
    static String getApiKey();

    static void setServerUrl(const String& url);
    static void setApiKey(const String& key);

    /**
     * @brief Formats and POSTs an array of TelemetryItem records to the central server.
     * @param items Pointer to array of TelemetryItem objects.
     * @param count Number of items in array (e.g. 1 to 18).
     * @return true if HTTP 201 Created or 200 OK received.
     */
    static bool postBatch(const TelemetryItem* items, size_t count);

    /**
     * @brief Sends a synthetic single-sample batch to verify end-to-end server reachability.
     */
    static bool testPost();

private:
    static String _serverUrl;
    static String _apiKey;
};

#endif // HTTP_BATCH_EGRESS_H
