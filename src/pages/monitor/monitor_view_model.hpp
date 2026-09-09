#ifndef LILYGO_UI_MONITOR_PAGES_MONITOR_MONITOR_VIEW_MODEL_HPP
#define LILYGO_UI_MONITOR_PAGES_MONITOR_MONITOR_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "pages/monitor/monitor_model.hpp"
#include "pages/monitor/system_metrics_source.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class MonitorMetric {
    cpu,
    memory,
    storage,
    temperature,
    network,
    uptime,
};

struct MeasurementHistoryPoint {
    std::uint64_t timestamp_milliseconds = 0;
    std::optional<double> value;
    std::optional<double> secondary_value;
};

struct MeasurementStatistics {
    std::optional<double> current;
    std::optional<double> minimum;
    std::optional<double> average;
    std::optional<double> maximum;
    std::size_t sample_count = 0;
};

struct NetworkInterfaceViewData {
    std::string name;
    std::string status;
    std::string ip_address;
    std::string receive_rate;
    std::string send_rate;
    NetworkInterfaceState state = NetworkInterfaceState::unknown;
};

class MonitorViewModel {
public:
    static constexpr std::size_t history_capacity = 60;

    MonitorViewModel();
    explicit MonitorViewModel(std::unique_ptr<SystemMetricsSource> source);

    void refresh();
    [[nodiscard]] const MonitorSnapshot &snapshot() const noexcept;
    [[nodiscard]] const std::vector<NetworkInterfaceViewData> &network_interfaces() const noexcept;
    [[nodiscard]] std::vector<MeasurementHistoryPoint> history(MonitorMetric metric,
                                                               const std::string &interface_name = {}) const;
    [[nodiscard]] MeasurementStatistics statistics(MonitorMetric metric, const std::string &interface_name = {},
                                                   bool secondary = false) const;
    [[nodiscard]] static std::string format_metric_value(MonitorMetric metric, double value);

    [[nodiscard]] lv_subject_t *cpu_percent_subject() noexcept;
    [[nodiscard]] lv_subject_t *memory_percent_subject() noexcept;
    [[nodiscard]] lv_subject_t *storage_percent_subject() noexcept;
    [[nodiscard]] lv_subject_t *temperature_progress_subject() noexcept;
    [[nodiscard]] lv_subject_t *cpu_value_subject() noexcept;
    [[nodiscard]] lv_subject_t *cpu_detail_subject() noexcept;
    [[nodiscard]] lv_subject_t *memory_value_subject() noexcept;
    [[nodiscard]] lv_subject_t *memory_detail_subject() noexcept;
    [[nodiscard]] lv_subject_t *storage_value_subject() noexcept;
    [[nodiscard]] lv_subject_t *storage_detail_subject() noexcept;
    [[nodiscard]] lv_subject_t *temperature_value_subject() noexcept;
    [[nodiscard]] lv_subject_t *temperature_detail_subject() noexcept;
    [[nodiscard]] lv_subject_t *hostname_subject() noexcept;
    [[nodiscard]] lv_subject_t *operating_system_subject() noexcept;
    [[nodiscard]] lv_subject_t *kernel_subject() noexcept;
    [[nodiscard]] lv_subject_t *uptime_subject() noexcept;

private:
    struct TimestampedSnapshot {
        std::uint64_t timestamp_milliseconds;
        MonitorSnapshot snapshot;
    };

    void publish();

    std::unique_ptr<SystemMetricsSource> source_;
    MonitorModel model_;
    std::deque<TimestampedSnapshot> history_;
    IntSubject cpu_percent_{0, 0, 100};
    IntSubject memory_percent_{0, 0, 100};
    IntSubject storage_percent_{0, 0, 100};
    IntSubject temperature_progress_{0, 0, 100};
    StringSubject<24> cpu_value_{"--"};
    StringSubject<64> cpu_detail_{"Unavailable"};
    StringSubject<24> memory_value_{"--"};
    StringSubject<64> memory_detail_{"Unavailable"};
    StringSubject<24> storage_value_{"--"};
    StringSubject<64> storage_detail_{"Unavailable"};
    StringSubject<24> temperature_value_{"--"};
    StringSubject<64> temperature_detail_{"Unavailable"};
    std::vector<NetworkInterfaceViewData> network_interfaces_;
    StringSubject<96> hostname_{"Unknown device"};
    StringSubject<128> operating_system_{"OS --"};
    StringSubject<96> kernel_{"Kernel --"};
    StringSubject<96> uptime_{"Uptime --"};
};

#endif
