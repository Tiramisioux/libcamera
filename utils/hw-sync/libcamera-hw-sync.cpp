#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef HAVE_GPIOD
#include <gpiod.h>
#endif

using namespace std;
using namespace std::chrono;

/* SyncPayload structure compatible with libcamera sync algorithm */
struct SyncPayload {
    uint32_t frameDuration;          /* microseconds */
    uint64_t systemFrameTimestamp;   /* unused by this tool */
    uint64_t wallClockFrameTimestamp;
    uint64_t systemReadyTime;        /* unused by this tool */
    uint64_t wallClockReadyTime;
};

static void usage(const char *argv0)
{
    cout << "Usage: " << argv0
         << " [--source timer|stdin|gpio] [--fps N]" << endl;
    cout << "            [--group ADDRESS] [--port PORT]" << endl;
    cout << "            [--chip NAME] [--line PIN]" << endl;
}

int main(int argc, char **argv)
{
    string source = "timer";
    double fps = 30.0;
    string group = "239.255.255.250";
    uint16_t port = 10000;
    string chipName = "gpiochip4";
    int line = -1;

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--source" && i + 1 < argc) {
            source = argv[++i];
        } else if (arg == "--fps" && i + 1 < argc) {
            fps = stod(argv[++i]);
        } else if (arg == "--group" && i + 1 < argc) {
            group = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(stoi(argv[++i]));
        } else if (arg == "--chip" && i + 1 < argc) {
            chipName = argv[++i];
        } else if (arg == "--line" && i + 1 < argc) {
            line = stoi(argv[++i]);
        } else {
            usage(argv[0]);
            return 0;
        }
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(group.c_str());
    addr.sin_port = htons(port);

    microseconds frameDuration(static_cast<int>(1e6 / fps));
    uint64_t frame = 0;

    cerr << "libcamera-hw-sync started with source=" << source
         << " fps=" << fps;
    if (source == "gpio")
        cerr << " chip=" << chipName << " line=" << line;
    cerr << "\n";

#ifdef HAVE_GPIOD
    gpiod_chip *chip = nullptr;
    gpiod_line *gline = nullptr;
    if (source == "gpio") {
        chip = gpiod_chip_open_by_name(chipName.c_str());
        if (!chip) {
            if (chipName == "gpiochip4")
                chip = gpiod_chip_open_by_name("gpiochip0");
        }
        if (!chip) {
            cerr << "Failed to open " << chipName << " or gpiochip0" << endl;
            return 1;
        }
        gline = gpiod_chip_get_line(chip, line);
        if (!gline) {
            cerr << "Failed to get line " << line << endl;
            return 1;
        }
        if (gpiod_line_request_rising_edge_events(gline, "libcamera-hw-sync")) {
            cerr << "Failed to request events for line " << line << endl;
            return 1;
        }
    }
#else
    if (source == "gpio") {
        cerr << "GPIO source requested but gpiod not available" << endl;
        return 1;
    }
#endif

    uint64_t prevUs = 0;
    bool first = true;

    while (true) {
        if (source == "timer") {
            this_thread::sleep_for(frameDuration);
        } else if (source == "stdin") {
            getchar();
        }
#ifdef HAVE_GPIOD
        else if (source == "gpio") {
            gpiod_line_event ev;
            int ret = gpiod_line_event_wait(gline, nullptr);
            if (ret < 0) {
                cerr << "GPIO wait error" << endl;
                continue;
            }
            if (gpiod_line_event_read(gline, &ev) < 0) {
                cerr << "GPIO read error" << endl;
                continue;
            }
        }
#endif

        auto now = system_clock::now();
        uint64_t nowUs = duration_cast<microseconds>(now.time_since_epoch()).count();

        if (!first) {
            uint64_t diff = nowUs - prevUs;
            cerr << "Pulse interval " << diff << " us";
            uint64_t exp = frameDuration.count();
            if (diff < exp * 9 / 10 || diff > exp * 11 / 10)
                cerr << " (irregular)";
            cerr << endl;
        }
        first = false;
        prevUs = nowUs;

        SyncPayload payload{};
        payload.frameDuration = frameDuration.count();
        payload.wallClockFrameTimestamp = nowUs;
        payload.wallClockReadyTime = nowUs + 100 * payload.frameDuration;

        ssize_t ret = sendto(sock, &payload, sizeof(payload), 0,
                              reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        if (ret < 0)
            perror("sendto");

        cerr << "Frame " << frame++ << " sent" << endl;
    }
#ifdef HAVE_GPIOD
    if (gline)
        gpiod_line_release(gline);
    if (chip)
        gpiod_chip_close(chip);
#endif

    return 0;
}

