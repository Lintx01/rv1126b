#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

#include <iostream>
#include <string>

namespace {

struct Options {
    std::string input{"http://127.0.0.1:8080/live.flv"};
    std::string port{"8554"};
    std::string mount{"/live"};
};

void printUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " [--input <url>] [--port <port>] [--mount <path>]\n";
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            options.input = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            options.port = argv[++i];
        } else if (arg == "--mount" && i + 1 < argc) {
            options.mount = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        } else {
            std::cerr << "[RTSP] unknown or incomplete argument: " << arg << "\n";
            printUsage(argv[0]);
            return false;
        }
    }

    if (options.mount.empty()) {
        options.mount = "/live";
    }
    if (options.mount.front() != '/') {
        options.mount.insert(options.mount.begin(), '/');
    }
    return true;
}

std::string escapeLaunchString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string detectBoardIp() {
    ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) != 0 || addrs == nullptr) {
        return "<board_ip>";
    }

    std::string fallback;
    for (ifaddrs* it = addrs; it != nullptr; it = it->ifa_next) {
        if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((it->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        char buf[INET_ADDRSTRLEN]{};
        const auto* addr = reinterpret_cast<sockaddr_in*>(it->ifa_addr);
        if (inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf)) == nullptr) {
            continue;
        }

        const std::string ip = buf;
        if (ip.rfind("192.168.137.", 0) == 0) {
            freeifaddrs(addrs);
            return ip;
        }
        if (fallback.empty()) {
            fallback = ip;
        }
    }

    freeifaddrs(addrs);
    return fallback.empty() ? "<board_ip>" : fallback;
}

std::string makePipeline(const std::string& input) {
    return "( souphttpsrc location=\"" + escapeLaunchString(input) +
           "\" is-live=true do-timestamp=true "
           "! flvdemux name=demux "
           "demux.video ! queue leaky=downstream max-size-buffers=5 max-size-bytes=0 max-size-time=0 "
           "! h264parse config-interval=1 "
           "! rtph264pay name=pay0 pt=96 config-interval=1 )";
}

void onClientConnected(GstRTSPServer*, GstRTSPClient*, gpointer) {
    std::cout << "[RTSP] client connected\n";
}

void onMediaConfigure(GstRTSPMediaFactory*, GstRTSPMedia*, gpointer) {
    std::cout << "[RTSP] media configured\n";
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        return argc > 1 ? 1 : 0;
    }

    gst_init(&argc, &argv);

    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    GstRTSPServer* server = gst_rtsp_server_new();
    gst_rtsp_server_set_address(server, "0.0.0.0");
    gst_rtsp_server_set_service(server, options.port.c_str());

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server);
    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
    const std::string pipeline = makePipeline(options.input);
    gst_rtsp_media_factory_set_launch(factory, pipeline.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    gst_rtsp_media_factory_set_latency(factory, 100);
    g_signal_connect(factory, "media-configure", G_CALLBACK(onMediaConfigure), nullptr);
    gst_rtsp_mount_points_add_factory(mounts, options.mount.c_str(), factory);
    g_object_unref(mounts);
    g_signal_connect(server, "client-connected", G_CALLBACK(onClientConnected), nullptr);

    const guint id = gst_rtsp_server_attach(server, nullptr);
    if (id == 0) {
        std::cerr << "[RTSP] failed to attach RTSP server on port " << options.port << "\n";
        g_object_unref(server);
        g_main_loop_unref(loop);
        return 1;
    }

    const std::string board_ip = detectBoardIp();
    std::cout << "[RTSP] input: " << options.input << "\n";
    std::cout << "[RTSP] pipeline launch: " << pipeline << "\n";
    std::cout << "[RTSP] url: rtsp://0.0.0.0:" << options.port << options.mount << "\n";
    std::cout << "[RTSP] VLC: rtsp://" << board_ip << ":" << options.port << options.mount << "\n";

    g_main_loop_run(loop);

    g_source_remove(id);
    g_object_unref(server);
    g_main_loop_unref(loop);
    return 0;
}
