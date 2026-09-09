#include "pages/monitor/monitor_view_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

std::string format_bytes(double bytes, bool rate = false)
{
    constexpr std::array<const char *, 5> units{"B", "KB", "MB", "GB", "TB"};
    std::size_t unit = 0;
    while (bytes >= 1024.0 && unit + 1 < units.size()) {
        bytes /= 1024.0;
        ++unit;
    }
    char text[32];
    const char *suffix = rate ? "/s" : "";
    if (unit == 0)
        std::snprintf(text, sizeof(text), "%.0f %s%s", bytes, units[unit], suffix);
    else if (bytes >= 100.0)
        std::snprintf(text, sizeof(text), "%.0f %s%s", bytes, units[unit], suffix);
    else
        std::snprintf(text, sizeof(text), "%.1f %s%s", bytes, units[unit], suffix);
    return text;
}

std::string format_capacity(std::uint64_t used, std::uint64_t total)
{
    return format_bytes(static_cast<double>(used)) + " / " + format_bytes(static_cast<double>(total));
}

std::string format_uptime(std::uint64_t seconds, bool concise = false)
{
    const auto days    = seconds / 86400;
    const auto hours   = (seconds % 86400) / 3600;
    const auto minutes = (seconds % 3600) / 60;
    char text[96];
    if (days > 0)
        std::snprintf(text, sizeof(text), "%llud %lluh %llum", static_cast<unsigned long long>(days),
                      static_cast<unsigned long long>(hours), static_cast<unsigned long long>(minutes));
    else if (!concise || hours > 0)
        std::snprintf(text, sizeof(text), "%lluh %llum", static_cast<unsigned long long>(hours),
                      static_cast<unsigned long long>(minutes));
    else if (minutes > 0)
        std::snprintf(text, sizeof(text), "%llum %llus", static_cast<unsigned long long>(minutes),
                      static_cast<unsigned long long>(seconds % 60));
    else
        std::snprintf(text, sizeof(text), "%llus", static_cast<unsigned long long>(seconds));
    return concise ? std::string(text) : "Uptime " + std::string(text);
}

const char *format_network_state(NetworkInterfaceState state)
{
    switch (state) {
        case NetworkInterfaceState::up:
            return "UP";
        case NetworkInterfaceState::down:
            return "DOWN";
        case NetworkInterfaceState::unknown:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace

MonitorViewModel::MonitorViewModel() : MonitorViewModel(std::make_unique<LinuxSystemMetricsSource>())
{
}

MonitorViewModel::MonitorViewModel(std::unique_ptr<SystemMetricsSource> source) : source_(std::move(source))
{
    refresh();
}

void MonitorViewModel::refresh()
{
    if (!source_) return;
    const auto sample = source_->read();
    model_.update(sample);
    history_.push_back({sample.monotonic_milliseconds, snapshot()});
    if (history_.size() > history_capacity) history_.pop_front();
    publish();
}

const MonitorSnapshot &MonitorViewModel::snapshot() const noexcept
{
    return model_.snapshot();
}

const std::vector<NetworkInterfaceViewData> &MonitorViewModel::network_interfaces() const noexcept
{
    return network_interfaces_;
}

std::vector<MeasurementHistoryPoint> MonitorViewModel::history(MonitorMetric metric,
                                                               const std::string &interface_name) const
{
    std::vector<MeasurementHistoryPoint> points;
    points.reserve(history_.size());
    for (const auto &entry : history_) {
        MeasurementHistoryPoint point;
        point.timestamp_milliseconds = entry.timestamp_milliseconds;
        const auto &current          = entry.snapshot;
        switch (metric) {
            case MonitorMetric::cpu:
                if (current.cpu_valid) point.value = current.cpu_percent;
                break;
            case MonitorMetric::memory:
                if (current.memory_valid) point.value = current.memory_percent;
                break;
            case MonitorMetric::storage:
                if (current.storage_valid) point.value = current.storage_percent;
                break;
            case MonitorMetric::temperature:
                if (current.temperature_valid) point.value = current.temperature_celsius;
                break;
            case MonitorMetric::network: {
                const auto interface =
                    std::find_if(current.network_interfaces.begin(), current.network_interfaces.end(),
                                 [&interface_name](const auto &item) { return item.name == interface_name; });
                if (interface != current.network_interfaces.end()) {
                    point.value           = interface->receive_bytes_per_second;
                    point.secondary_value = interface->send_bytes_per_second;
                }
                break;
            }
            case MonitorMetric::uptime:
                if (current.uptime_valid) point.value = static_cast<double>(current.uptime_seconds);
                break;
        }
        points.push_back(point);
    }
    return points;
}

MeasurementStatistics MonitorViewModel::statistics(MonitorMetric metric, const std::string &interface_name,
                                                   bool secondary) const
{
    MeasurementStatistics result;
    const auto points = history(metric, interface_name);
    if (!points.empty()) result.current = secondary ? points.back().secondary_value : points.back().value;
    double total = 0.0;
    for (const auto &point : points) {
        const auto &value = secondary ? point.secondary_value : point.value;
        if (!value) continue;
        result.minimum = result.minimum ? std::min(*result.minimum, *value) : *value;
        result.maximum = result.maximum ? std::max(*result.maximum, *value) : *value;
        total += *value;
        ++result.sample_count;
    }
    if (result.sample_count > 0) result.average = total / result.sample_count;
    return result;
}

std::string MonitorViewModel::format_metric_value(MonitorMetric metric, double value)
{
    if (!std::isfinite(value)) return "--";
    char text[32];
    switch (metric) {
        case MonitorMetric::cpu:
        case MonitorMetric::memory:
        case MonitorMetric::storage:
            std::snprintf(text, sizeof(text), "%.0f%%", value);
            return text;
        case MonitorMetric::temperature:
            std::snprintf(text, sizeof(text), "%.0f°C", value);
            return text;
        case MonitorMetric::network:
            return format_bytes(value, true);
        case MonitorMetric::uptime:
            return format_uptime(static_cast<std::uint64_t>(std::max(0.0, value)), true);
    }
    return "--";
}

void MonitorViewModel::publish()
{
    const auto &current = snapshot();
    char text[128];

    cpu_percent_.set(current.cpu_valid ? current.cpu_percent : 0);
    if (current.cpu_valid) {
        std::snprintf(text, sizeof(text), "%d%%", current.cpu_percent);
        cpu_value_.set(text);
        if (current.load_valid && current.cpu_count > 0)
            std::snprintf(text, sizeof(text), "%d cores · Load %.2f", current.cpu_count, current.load_one);
        else if (current.cpu_count > 0)
            std::snprintf(text, sizeof(text), "%d cores", current.cpu_count);
        else
            std::snprintf(text, sizeof(text), "CPU usage");
        cpu_detail_.set(text);
    } else {
        cpu_value_.set("--");
        cpu_detail_.set("Unavailable");
    }

    memory_percent_.set(current.memory_valid ? current.memory_percent : 0);
    if (current.memory_valid) {
        std::snprintf(text, sizeof(text), "%d%%", current.memory_percent);
        memory_value_.set(text);
        memory_detail_.set(format_capacity(current.memory_used_bytes, current.memory_total_bytes).c_str());
    } else {
        memory_value_.set("--");
        memory_detail_.set("Unavailable");
    }

    storage_percent_.set(current.storage_valid ? current.storage_percent : 0);
    if (current.storage_valid) {
        std::snprintf(text, sizeof(text), "%d%%", current.storage_percent);
        storage_value_.set(text);
        storage_detail_.set(format_capacity(current.storage_used_bytes, current.storage_total_bytes).c_str());
    } else {
        storage_value_.set("--");
        storage_detail_.set("Unavailable");
    }

    temperature_progress_.set(current.temperature_valid ? std::clamp(current.temperature_celsius, 0, 100) : 0);
    if (current.temperature_valid) {
        std::snprintf(text, sizeof(text), "%d°C", current.temperature_celsius);
        temperature_value_.set(text);
        temperature_detail_.set(current.temperature_celsius >= 80 ? "High temperature" : "CPU core temperature");
    } else {
        temperature_value_.set("--");
        temperature_detail_.set("Unavailable");
    }

    network_interfaces_.clear();
    network_interfaces_.reserve(current.network_interfaces.size());
    for (const auto &interface : current.network_interfaces) {
        NetworkInterfaceViewData data;
        data.name         = interface.name;
        data.status       = format_network_state(interface.state);
        data.ip_address   = interface.ip_address.empty() ? "No IPv4 address" : interface.ip_address;
        data.receive_rate = format_bytes(interface.receive_bytes_per_second, true);
        data.send_rate    = format_bytes(interface.send_bytes_per_second, true);
        data.state        = interface.state;
        if (interface.is_default) data.status += " · DEFAULT";
        network_interfaces_.push_back(std::move(data));
    }

    hostname_.set(current.hostname.empty() ? "Unknown device" : current.hostname.c_str());
    operating_system_.set(
        ("OS " + (current.operating_system.empty() ? std::string("--") : current.operating_system)).c_str());
    kernel_.set(("Kernel " + (current.kernel_release.empty() ? std::string("--") : current.kernel_release)).c_str());
    uptime_.set(current.uptime_valid ? format_uptime(current.uptime_seconds).c_str() : "Uptime --");
}

lv_subject_t *MonitorViewModel::cpu_percent_subject() noexcept
{
    return cpu_percent_.get();
}

lv_subject_t *MonitorViewModel::memory_percent_subject() noexcept
{
    return memory_percent_.get();
}

lv_subject_t *MonitorViewModel::storage_percent_subject() noexcept
{
    return storage_percent_.get();
}

lv_subject_t *MonitorViewModel::temperature_progress_subject() noexcept
{
    return temperature_progress_.get();
}

lv_subject_t *MonitorViewModel::cpu_value_subject() noexcept
{
    return cpu_value_.get();
}

lv_subject_t *MonitorViewModel::cpu_detail_subject() noexcept
{
    return cpu_detail_.get();
}

lv_subject_t *MonitorViewModel::memory_value_subject() noexcept
{
    return memory_value_.get();
}

lv_subject_t *MonitorViewModel::memory_detail_subject() noexcept
{
    return memory_detail_.get();
}

lv_subject_t *MonitorViewModel::storage_value_subject() noexcept
{
    return storage_value_.get();
}

lv_subject_t *MonitorViewModel::storage_detail_subject() noexcept
{
    return storage_detail_.get();
}

lv_subject_t *MonitorViewModel::temperature_value_subject() noexcept
{
    return temperature_value_.get();
}

lv_subject_t *MonitorViewModel::temperature_detail_subject() noexcept
{
    return temperature_detail_.get();
}

lv_subject_t *MonitorViewModel::hostname_subject() noexcept
{
    return hostname_.get();
}

lv_subject_t *MonitorViewModel::operating_system_subject() noexcept
{
    return operating_system_.get();
}

lv_subject_t *MonitorViewModel::kernel_subject() noexcept
{
    return kernel_.get();
}

lv_subject_t *MonitorViewModel::uptime_subject() noexcept
{
    return uptime_.get();
}
