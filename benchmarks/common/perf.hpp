#pragma once
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <cstdlib>
#include <string_view>


namespace bench {
    struct PerfCtl {
        int fd = -1;

        PerfCtl() {
            if (const char* p = std::getenv("PERF_CTL"))
                fd = open(p, O_WRONLY);
        }

        void send(std::string_view s) const {
            if (fd >= 0 && ::write(fd, s.data(), s.size()) < 0) {
            }
        }

        void enable() {
            send("enable\n");
        }

        void disable() {
            send("disable\n");
        }
    };
}