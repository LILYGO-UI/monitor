#include "pages/monitor/monitor_model.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

int percent_used(std::uint64_t total, std::uint64_t available) noexcept
{
    if (total == 0) return 0;
    const auto used = total - std::min(total, available);
    return std::clamp(static_cast<int>(std::lround(static_cast<double>(used) * 100.0 / total)), 0, 100);
}

int cpu_percent(const SystemSample &sample, const SystemSample *previous) noexcept
{
    std::uint64_t total = sample.cpu_total;
    std::uint64_t idle  = std::min(sample.cpu_idle, total);
    if (previous && sample.cpu_total > previous->cpu_total && sample.cpu_idle >= previous->cpu_idle) {
        total = sample.cpu_total - previous->cpu_total;
        idle  = std::min(sample.cpu_idle - previous->cpu_idle, total);
    }
    if (total == 0) return 0;
    return std::clamp(static_cast<int>(std::lround(static_cast<double>(total - idle) * 100.0 / total)), 0, 100);
}

}  // namespace

void MonitorModel::update(const SystemSample &sample) noexcept
{
    const auto *previous = has_previous_sample_ ? &previous_sample_ : nullptr;

    snapshot_.cpu_valid = sample.cpu_valid;
    snapshot_.cpu_percent =
        sample.cpu_valid ? cpu_percent(sample, previous && previous->cpu_valid ? previous : nullptr) : 0;
    snapshot_.cpu_count = sample.cpu_count;

    snapshot_.memory_valid       = sample.memory_valid && sample.memory_total_bytes > 0;
    snapshot_.memory_total_bytes = sample.memory_total_bytes;
    snapshot_.memory_used_bytes =
        sample.memory_total_bytes - std::min(sample.memory_total_bytes, sample.memory_available_bytes);
    snapshot_.memory_percent =
        snapshot_.memory_valid ? percent_used(sample.memory_total_bytes, sample.memory_available_bytes) : 0;

    snapshot_.storage_valid       = sample.storage_valid && sample.storage_total_bytes > 0;
    snapshot_.storage_total_bytes = sample.storage_total_bytes;
    snapshot_.storage_used_bytes =
        sample.storage_total_bytes - std::min(sample.storage_total_bytes, sample.storage_available_bytes);
    snapshot_.storage_percent =
        snapshot_.storage_valid ? percent_used(sample.storage_total_bytes, sample.storage_available_bytes) : 0;

    snapshot_.temperature_valid   = sample.temperature_valid;
    snapshot_.temperature_celsius = static_cast<int>(std::lround(sample.temperature_milli_celsius / 1000.0));
    snapshot_.uptime_valid        = sample.uptime_valid;
    snapshot_.uptime_seconds      = sample.uptime_seconds;
    snapshot_.load_valid          = sample.load_valid;
    snapshot_.load_one            = sample.load_one;

    snapshot_.network_interfaces.clear();
    snapshot_.network_interfaces.reserve(sample.network_interfaces.size());
    std::unordered_map<std::string, const NetworkInterfaceSample *> previous_interfaces;
    if (previous) {
        previous_interfaces.reserve(previous->network_interfaces.size());
        for (const auto &interface : previous->network_interfaces)
            previous_interfaces.emplace(interface.name, &interface);
    }
    const double elapsed_seconds =
        previous && sample.monotonic_milliseconds > previous->monotonic_milliseconds
            ? static_cast<double>(sample.monotonic_milliseconds - previous->monotonic_milliseconds) / 1000.0
            : 0.0;
    for (const auto &interface : sample.network_interfaces) {
        NetworkInterfaceSnapshot current;
        current.name       = interface.name;
        current.ip_address = interface.ip_address;
        current.state      = interface.state;
        current.is_default = interface.is_default;
        const auto found   = previous_interfaces.find(interface.name);
        if (found != previous_interfaces.end() && elapsed_seconds > 0.0) {
            const auto &old = *found->second;
            if (interface.received_bytes >= old.received_bytes)
                current.receive_bytes_per_second = (interface.received_bytes - old.received_bytes) / elapsed_seconds;
            if (interface.sent_bytes >= old.sent_bytes)
                current.send_bytes_per_second = (interface.sent_bytes - old.sent_bytes) / elapsed_seconds;
        }
        snapshot_.network_interfaces.push_back(std::move(current));
    }

    snapshot_.hostname         = sample.hostname;
    snapshot_.operating_system = sample.operating_system;
    snapshot_.kernel_release   = sample.kernel_release;
    previous_sample_           = sample;
    has_previous_sample_       = true;
}

const MonitorSnapshot &MonitorModel::snapshot() const noexcept
{
    return snapshot_;
}
