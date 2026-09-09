#ifndef LILYGO_UI_MONITOR_PAGES_MONITOR_MONITOR_MODEL_HPP
#define LILYGO_UI_MONITOR_PAGES_MONITOR_MONITOR_MODEL_HPP

#include <cstdint>
#include <string>
#include <vector>

enum class NetworkInterfaceState {
    unknown,
    down,
    up,
};

struct NetworkInterfaceSample {
    std::string name;
    std::string ip_address;
    NetworkInterfaceState state  = NetworkInterfaceState::unknown;
    bool is_default              = false;
    std::uint64_t received_bytes = 0;
    std::uint64_t sent_bytes     = 0;
};

struct SystemSample {
    bool cpu_valid          = false;
    std::uint64_t cpu_total = 0;
    std::uint64_t cpu_idle  = 0;
    int cpu_count           = 0;

    bool memory_valid                    = false;
    std::uint64_t memory_total_bytes     = 0;
    std::uint64_t memory_available_bytes = 0;

    bool storage_valid                    = false;
    std::uint64_t storage_total_bytes     = 0;
    std::uint64_t storage_available_bytes = 0;

    bool temperature_valid        = false;
    int temperature_milli_celsius = 0;

    bool uptime_valid            = false;
    std::uint64_t uptime_seconds = 0;
    bool load_valid              = false;
    double load_one              = 0.0;

    std::uint64_t monotonic_milliseconds = 0;
    std::vector<NetworkInterfaceSample> network_interfaces;

    std::string hostname;
    std::string operating_system;
    std::string kernel_release;
};

struct NetworkInterfaceSnapshot {
    std::string name;
    std::string ip_address;
    NetworkInterfaceState state     = NetworkInterfaceState::unknown;
    bool is_default                 = false;
    double receive_bytes_per_second = 0.0;
    double send_bytes_per_second    = 0.0;
};

struct MonitorSnapshot {
    bool cpu_valid  = false;
    int cpu_percent = 0;
    int cpu_count   = 0;

    bool memory_valid                = false;
    int memory_percent               = 0;
    std::uint64_t memory_used_bytes  = 0;
    std::uint64_t memory_total_bytes = 0;

    bool storage_valid                = false;
    int storage_percent               = 0;
    std::uint64_t storage_used_bytes  = 0;
    std::uint64_t storage_total_bytes = 0;

    bool temperature_valid  = false;
    int temperature_celsius = 0;

    bool uptime_valid            = false;
    std::uint64_t uptime_seconds = 0;
    bool load_valid              = false;
    double load_one              = 0.0;

    std::vector<NetworkInterfaceSnapshot> network_interfaces;

    std::string hostname;
    std::string operating_system;
    std::string kernel_release;
};

class MonitorModel {
public:
    void update(const SystemSample &sample) noexcept;
    [[nodiscard]] const MonitorSnapshot &snapshot() const noexcept;

private:
    MonitorSnapshot snapshot_;
    SystemSample previous_sample_;
    bool has_previous_sample_ = false;
};

#endif
