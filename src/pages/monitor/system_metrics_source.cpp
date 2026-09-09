#include "pages/monitor/system_metrics_source.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace {

const char *environment_path(const char *name, const char *fallback) noexcept
{
    const char *value = std::getenv(name);
    return value && value[0] ? value : fallback;
}

std::string join_path(const std::string &root, const std::string &path)
{
    if (root.empty() || root == "/") return "/" + path;
    return root.back() == '/' ? root + path : root + "/" + path;
}

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

void read_cpu(const std::string &proc_root, SystemSample &sample)
{
    std::ifstream file(join_path(proc_root, "stat"));
    std::string label;
    std::uint64_t user = 0, nice = 0, system = 0, idle = 0, io_wait = 0, irq = 0, soft_irq = 0, steal = 0;
    if (file >> label >> user >> nice >> system >> idle >> io_wait >> irq >> soft_irq >> steal && label == "cpu") {
        sample.cpu_total = user + nice + system + idle + io_wait + irq + soft_irq + steal;
        sample.cpu_idle  = idle + io_wait;
        sample.cpu_valid = sample.cpu_total > 0;
    }
    const long count = sysconf(_SC_NPROCESSORS_ONLN);
    sample.cpu_count = count > 0 && count <= std::numeric_limits<int>::max() ? static_cast<int>(count) : 0;
}

void read_memory(const std::string &proc_root, SystemSample &sample)
{
    std::ifstream file(join_path(proc_root, "meminfo"));
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream stream(line);
        std::string key;
        std::uint64_t value = 0;
        if (!(stream >> key >> value)) continue;
        if (key == "MemTotal:") sample.memory_total_bytes = value * 1024;
        if (key == "MemAvailable:") sample.memory_available_bytes = value * 1024;
    }
    sample.memory_valid = sample.memory_total_bytes > 0;
}

void read_storage(SystemSample &sample)
{
    struct statvfs status{};
    if (statvfs(environment_path("LILYGO_MONITOR_STORAGE_PATH", "/"), &status) != 0 || status.f_blocks == 0) return;
    sample.storage_total_bytes     = static_cast<std::uint64_t>(status.f_blocks) * status.f_frsize;
    sample.storage_available_bytes = static_cast<std::uint64_t>(status.f_bavail) * status.f_frsize;
    sample.storage_valid           = true;
}

void read_temperature(const std::string &sys_root, SystemSample &sample)
{
    for (int index = 0; index < 10; ++index) {
        std::ifstream file(join_path(sys_root, "class/thermal/thermal_zone" + std::to_string(index) + "/temp"));
        if (file >> sample.temperature_milli_celsius) {
            sample.temperature_valid = true;
            return;
        }
    }
}

void read_uptime_and_load(const std::string &proc_root, SystemSample &sample)
{
    double uptime = 0.0;
    std::ifstream uptime_file(join_path(proc_root, "uptime"));
    if (uptime_file >> uptime && uptime >= 0.0) {
        sample.uptime_seconds = static_cast<std::uint64_t>(uptime);
        sample.uptime_valid   = true;
    }
    std::ifstream load_file(join_path(proc_root, "loadavg"));
    if (load_file >> sample.load_one) sample.load_valid = true;
}

std::string default_network_interface(const std::string &proc_root)
{
    std::ifstream file(join_path(proc_root, "net/route"));
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line)) {
        std::istringstream stream(line);
        std::string name;
        std::string destination;
        std::string gateway;
        unsigned int flags = 0;
        if (stream >> name >> destination >> gateway >> std::hex >> flags && destination == "00000000" &&
            (flags & 0x1U) != 0 && name != "lo")
            return name;
    }
    return {};
}

struct InterfaceDetails {
    std::string ip_address;
    bool administratively_up = false;
};

InterfaceDetails interface_details(const std::string &name)
{
    struct ifaddrs *addresses = nullptr;
    if (getifaddrs(&addresses) != 0) return {};
    InterfaceDetails result;
    for (auto *address = addresses; address; address = address->ifa_next) {
        if (!address->ifa_name || name != address->ifa_name) continue;
        result.administratively_up = result.administratively_up || (address->ifa_flags & IFF_UP) != 0;
        if (!address->ifa_addr || address->ifa_addr->sa_family != AF_INET || !result.ip_address.empty()) continue;
        char text[INET_ADDRSTRLEN]{};
        const auto *ipv4 = reinterpret_cast<const struct sockaddr_in *>(address->ifa_addr);
        if (inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text))) result.ip_address = text;
    }
    freeifaddrs(addresses);
    return result;
}

bool starts_with(const std::string &value, const char *prefix)
{
    return value.rfind(prefix, 0) == 0;
}

bool ignored_network_interface(const std::string &name)
{
    return name == "lo" || starts_with(name, "docker") || starts_with(name, "br-") || starts_with(name, "veth");
}

NetworkInterfaceState interface_state(const std::string &sys_root, const std::string &name, bool administratively_up)
{
    std::ifstream file(join_path(sys_root, "class/net/" + name + "/operstate"));
    std::string state;
    if (file >> state) {
        if (state == "up") return NetworkInterfaceState::up;
        if (state == "down" || state == "dormant" || state == "lowerlayerdown" || state == "notpresent" ||
            state == "testing")
            return NetworkInterfaceState::down;
        return NetworkInterfaceState::unknown;
    }
    return administratively_up ? NetworkInterfaceState::up : NetworkInterfaceState::down;
}

void read_network(const std::string &proc_root, const std::string &sys_root, SystemSample &sample)
{
    const std::string preferred = default_network_interface(proc_root);
    std::ifstream file(join_path(proc_root, "net/dev"));
    std::string line;
    while (std::getline(file, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = trim(line.substr(0, colon));
        if (name.empty() || ignored_network_interface(name)) continue;

        std::istringstream stream(line.substr(colon + 1));
        std::uint64_t values[16]{};
        bool valid = true;
        for (auto &value : values) {
            if (!(stream >> value)) {
                valid = false;
                break;
            }
        }
        if (!valid) continue;
        const auto details = interface_details(name);
        NetworkInterfaceSample interface;
        interface.name           = name;
        interface.ip_address     = details.ip_address;
        interface.state          = interface_state(sys_root, name, details.administratively_up);
        interface.is_default     = name == preferred;
        interface.received_bytes = values[0];
        interface.sent_bytes     = values[8];
        sample.network_interfaces.push_back(std::move(interface));
    }
    std::stable_sort(sample.network_interfaces.begin(), sample.network_interfaces.end(),
                     [](const auto &left, const auto &right) { return left.is_default && !right.is_default; });
    sample.monotonic_milliseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::string read_first_line(const std::string &path)
{
    std::ifstream file(path);
    std::string value;
    return std::getline(file, value) ? trim(value) : std::string{};
}

std::string read_pretty_name()
{
    std::ifstream file(environment_path("LILYGO_MONITOR_OS_RELEASE", "/etc/os-release"));
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
        std::string value = line.substr(12);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        return value;
    }
    return {};
}

void read_identity(const std::string &proc_root, SystemSample &sample)
{
    sample.hostname = read_first_line(join_path(proc_root, "sys/kernel/hostname"));
    if (sample.hostname.empty()) {
        char hostname[256]{};
        if (gethostname(hostname, sizeof(hostname) - 1) == 0) sample.hostname = hostname;
    }
    sample.operating_system = read_pretty_name();
    sample.kernel_release   = read_first_line(join_path(proc_root, "sys/kernel/osrelease"));
    if (sample.kernel_release.empty()) {
        struct utsname system_info{};
        if (uname(&system_info) == 0) sample.kernel_release = system_info.release;
    }
}

}  // namespace

SystemSample LinuxSystemMetricsSource::read()
{
    SystemSample sample;
    const std::string proc_root = environment_path("LILYGO_MONITOR_PROC_ROOT", "/proc");
    const std::string sys_root  = environment_path("LILYGO_MONITOR_SYS_ROOT", "/sys");
    read_cpu(proc_root, sample);
    read_memory(proc_root, sample);
    read_storage(sample);
    read_temperature(sys_root, sample);
    read_uptime_and_load(proc_root, sample);
    read_network(proc_root, sys_root, sample);
    read_identity(proc_root, sample);
    return sample;
}
