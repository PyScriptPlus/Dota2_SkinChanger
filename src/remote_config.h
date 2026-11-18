#pragma once


#include <string>

#define APP_VERSION "1.0.0"

struct RemoteStatus {
    bool enabled = true;
    std::string latest;
    std::string minVersion;
    std::string message;
    std::string downloadUrl;
    bool fetched = false;
    bool failed = false; 
    std::string error;
};

class RemoteConfig {
public:

    static void start();

    static void stop();

    static bool ready();

    static RemoteStatus snapshot();

    static bool updateAvailable();

    static bool mustUpdate();


    static int compareVersion(const std::string& a, const std::string& b);
};
