#include <iostream>
#include <cstring>
#include <cstdarg>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdexcept>
#include <algorithm>

#define ADDRESS "192.168.1.38"
#define PORT 3333

int DEBUG = 0;

enum MSG_FLAG {
    PERIOD_FLAG = '0',
    ADRESS_FLAG,
    SSID_FLAG
};

std::string format(std::string ssid, std::string pass) {
    // Validate input lengths
    if (ssid.length() > 32) {
        throw std::invalid_argument("SSID must be 32 characters or less");
    }
    if (pass.length() > 64) {
        throw std::invalid_argument("Password must be 64 characters or less");
    }

    // Create a 96-character string (to be exactly 96 chars + null terminator when converted)
    std::string formatted(96, '\0');

    // Copy SSID to the first 32 characters, padding with null bytes if shorter
    std::copy(ssid.begin(), ssid.end(), formatted.begin());

    // Copy password starting at index 32, padding with null bytes if shorter
    std::copy(pass.begin(), pass.end(), formatted.begin() + 32);

    return formatted;
}

void debug(const char *format, ...) {
    if (DEBUG == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}



int main(int argc, char *argv[]) {
    if (argc == 1 || (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))) {
        std::cout << "Usage: " << argv[0] << " [0/1/2] [[hh:mm] [hh:mm]] [http[s]://...] [SSID PASS]" << std::endl;
        std::cout << std::endl;
        std::cout << "  [0/1]           Select morning or evening period" << std::endl;
        std::cout << "  [hh:mm]         Start of the period between 00:00 and 23:59" << std::endl;
        std::cout << "  [hh:mm]         End of the period between 00:00 and 23:59" << std::endl;
        std::cout << "  [http[s]://...] URl used to update BME datai. Max 200 bytes" << std::endl;
        std::cout << "  [SSID PASS]     SSID and password to connect the ESP32" << std::endl;
        std::cout << "                  SSID has 32 max characters" << std::endl;
        std::cout << "                  PASS has 64 max characters" << std::endl;
        return 0;
    }

    switch (argv[1][0]) {
        case MSG_FLAG::PERIOD_FLAG:
        case MSG_FLAG::SSID_FLAG:
            if (argc != 4) {
                std::cout << "Error: 3 arguments required to send period or SSID" << std::endl;
                return 1;
            }
            break;
        case MSG_FLAG::ADRESS_FLAG:
            if (argc != 3) {
                std::cout << "Error: 2 arguments required to send url" << std::endl;
                return 1;
            }
            break;
        default:
            std::cout << "Error: Unknown flag " << argv[1] << std::endl;
            return 1;
    }

    std::string arg1 = argv[1];
    std::string arg2 = argv[2];


    std::string msg = "";
    // adding flag to msg
    msg += static_cast<char>(stoi(arg1));

    // adding args to msg
    switch (argv[1][0]) {
        case MSG_FLAG::PERIOD_FLAG:
            {
                std::string arg3 = argv[3];
                std::string hours[2] = {arg2, arg3};
                for (int i = 0; i < 2; i++) {
                    std::string hour = "";
                    std::string minute = "";
                    sscanf(hours[i].c_str(), "%2s:%2s", &hour[0], &minute[0]);

                    if (stoi(hour) < 0 || stoi(hour) > 23) {
                        std::cout << "Error: invalid hour: " << hours[i] << std::endl;
                        return 1;
                    }
                    if (stoi(minute) < 0 || stoi(minute) > 59) {
                        std::cout << "Error: invalid minute: " << hours[i] << std::endl;
                        return 1;
                    }

                    char ascii_hour = static_cast<char>(stoi(hour));
                    char ascii_minute = static_cast<char>(stoi(minute));

                    debug("length ascii: hour %ld minute %ld\n", hour.length(), minute.length());
                    msg += ascii_hour;
                    msg += ascii_minute;
                }
            }
            break;
        case MSG_FLAG::ADRESS_FLAG:
            if (arg2.length() > 200) {
                std::cout << "Adress must be 200 char max long. Current length is " << arg2.length() << std::endl;
                return 1;
            }
            msg += arg2;
            break;
        case MSG_FLAG::SSID_FLAG:
            {
                std::string arg3 {argv[3]};
                msg += format(arg2, arg3);
                break;
            }
        default:
            std::cout << "Unknown flag." << argv[0] << std::endl;
    }

    std::cout << "Msg length: " << msg.length() << std::endl;

    int Try = 1;
    while (Try != 0) {
        std::cout << "Try: " << Try << "..." << std::endl;

        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == -1) {
            perror("Error creating socket");
            return 1;
        }

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        inet_aton(ADDRESS, &addr.sin_addr);

        if (sendto(sock, msg.c_str(), msg.length(), 0, (sockaddr *)&addr, sizeof(addr)) == -1) {
            perror("Error sending message");
            close(sock);
            return 1;
        }

        char res[256] = {0};
        socklen_t addr_len = sizeof(addr);
        if (recvfrom(sock, res, sizeof(res), 0, (sockaddr *)&addr, &addr_len) == -1) {
            perror("Error receiving response");
            close(sock);
            return 1;
        }

        std::string res_str = res;
        if (res_str.find("invalid") != std::string::npos) {
            Try = 0;
            std::cout << "Invalid message. Please check" << std::endl;
        } else if (res_str == msg) {
            Try = 0;
        } else {
            Try++;
        }

        close(sock);
    }

    return 0;
}

