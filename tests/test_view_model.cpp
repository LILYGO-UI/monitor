#include "pages/monitor/monitor_view_model.hpp"

#include <array>
#include <cassert>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class SequenceSource final : public SystemMetricsSource {
public:
    explicit SequenceSource(std::vector<SystemSample> samples) : samples_(std::move(samples))
    {
    }

    SystemSample read() override
    {
        const auto index = next_ < samples_.size() ? next_++ : samples_.size() - 1;
        return samples_[index];
    }

private:
    std::vector<SystemSample> samples_;
    std::size_t next_ = 0;
};

SystemSample make_sample(std::uint64_t network_bytes, std::uint64_t milliseconds)
{
    SystemSample sample;
    sample.cpu_valid                 = true;
    sample.cpu_total                 = 1000 + milliseconds;
    sample.cpu_idle                  = sample.cpu_total * 30 / 100;
    sample.cpu_count                 = 4;
    sample.memory_valid              = true;
    sample.memory_total_bytes        = 8ULL * 1024 * 1024 * 1024;
    sample.memory_available_bytes    = 3ULL * 1024 * 1024 * 1024;
    sample.storage_valid             = true;
    sample.storage_total_bytes       = 64ULL * 1024 * 1024 * 1024;
    sample.storage_available_bytes   = 16ULL * 1024 * 1024 * 1024;
    sample.temperature_valid         = true;
    sample.temperature_milli_celsius = 52700;
    sample.uptime_valid              = true;
    sample.uptime_seconds            = 93784;
    sample.load_valid                = true;
    sample.load_one                  = 1.42;
    sample.monotonic_milliseconds    = milliseconds;
    sample.network_interfaces        = {
        {"wlan0", "192.168.1.20", NetworkInterfaceState::up, true, network_bytes, network_bytes / 2},
        {"eth0", "", NetworkInterfaceState::down, false, network_bytes / 4, network_bytes / 8},
        {"Meta", "198.18.0.1", NetworkInterfaceState::unknown, false, network_bytes / 2, network_bytes / 4},
    };
    sample.hostname         = "lilygo-cm0";
    sample.operating_system = "Raspberry Pi OS";
    sample.kernel_release   = "6.12.0";
    return sample;
}

}  // namespace

int main()
{
    lv_init();

    {
        std::vector<SystemSample> samples{make_sample(1024, 1000), make_sample(5120, 3000)};
        MonitorViewModel view_model(std::make_unique<SequenceSource>(std::move(samples)));
        const auto initial_history = view_model.history(MonitorMetric::cpu);
        assert(initial_history.size() == 1);
        assert(initial_history.front().timestamp_milliseconds == 1000);
        assert(initial_history.front().value == 70.0);
        assert(!initial_history.front().secondary_value);
        assert(lv_subject_get_int(view_model.memory_percent_subject()) == 63);
        assert(std::string(lv_subject_get_string(view_model.cpu_value_subject())) == "70%");
        assert(std::string(lv_subject_get_string(view_model.temperature_value_subject())) == "53°C");
        const auto &initial_network = view_model.network_interfaces();
        assert(initial_network.size() == 3);
        assert(initial_network[0].name == "wlan0" && initial_network[0].status == "UP · DEFAULT");
        assert(initial_network[0].ip_address == "192.168.1.20");
        assert(initial_network[1].name == "eth0" && initial_network[1].status == "DOWN");
        assert(initial_network[1].ip_address == "No IPv4 address");
        assert(initial_network[2].name == "Meta" && initial_network[2].status == "UNKNOWN");
        assert(std::string(lv_subject_get_string(view_model.hostname_subject())) == "lilygo-cm0");

        view_model.refresh();
        const auto &updated_network = view_model.network_interfaces();
        assert(updated_network[0].receive_rate == "2.0 KB/s");
        assert(updated_network[0].send_rate == "1.0 KB/s");
        assert(updated_network[1].state == NetworkInterfaceState::down);
        assert(updated_network[2].state == NetworkInterfaceState::unknown);
        assert(std::string(lv_subject_get_string(view_model.uptime_subject())) == "Uptime 1d 2h 3m");

        const std::array<std::pair<MonitorMetric, double>, 5> expected_values{{
            {MonitorMetric::cpu, 70.0},
            {MonitorMetric::memory, 63.0},
            {MonitorMetric::storage, 75.0},
            {MonitorMetric::temperature, 53.0},
            {MonitorMetric::uptime, 93784.0},
        }};
        for (const auto &[metric, expected] : expected_values) {
            const auto points = view_model.history(metric);
            assert(points.size() == 2);
            assert(points.back().timestamp_milliseconds == 3000);
            assert(points.back().value == expected);
            assert(!points.back().secondary_value);
        }
        const auto network_history = view_model.history(MonitorMetric::network, "wlan0");
        assert(network_history.size() == 2);
        assert(network_history.front().value == 0.0);
        assert(network_history.back().value == 2048.0);
        assert(network_history.back().secondary_value == 1024.0);
        const auto receive_statistics = view_model.statistics(MonitorMetric::network, "wlan0");
        assert(receive_statistics.current == 2048.0);
        assert(receive_statistics.minimum == 0.0);
        assert(receive_statistics.maximum == 2048.0);
        assert(receive_statistics.average == 1024.0);
        assert(receive_statistics.sample_count == 2);
        const auto send_statistics = view_model.statistics(MonitorMetric::network, "wlan0", true);
        assert(send_statistics.current == 1024.0);
        assert(send_statistics.average == 512.0);
    }

    {
        SystemSample unavailable;
        unavailable.monotonic_milliseconds  = 2000;
        unavailable.memory_valid            = true;
        unavailable.storage_valid           = true;
        auto recovered                      = make_sample(8192, 3000);
        recovered.memory_available_bytes    = 2ULL * 1024 * 1024 * 1024;
        recovered.temperature_milli_celsius = -5500;
        auto latest                         = unavailable;
        latest.monotonic_milliseconds       = 4000;
        std::vector<SystemSample> samples{make_sample(1024, 1000), unavailable, recovered, latest};
        MonitorViewModel view_model(std::make_unique<SequenceSource>(std::move(samples)));
        view_model.refresh();
        view_model.refresh();
        view_model.refresh();
        for (const auto metric : {MonitorMetric::cpu, MonitorMetric::memory, MonitorMetric::storage,
                                  MonitorMetric::temperature, MonitorMetric::network, MonitorMetric::uptime}) {
            const auto points = view_model.history(metric, "wlan0");
            assert(points.size() == 4);
            assert(points[0].value);
            assert(points[1].timestamp_milliseconds == 2000);
            assert(!points[1].value);
            assert(!points[1].secondary_value);
            assert(points[2].value);
            assert(!points[3].value);
            assert(!points[3].secondary_value);
            const auto statistics = view_model.statistics(metric, "wlan0");
            assert(!statistics.current);
            assert(statistics.sample_count == 2);
        }
        const auto memory_statistics = view_model.statistics(MonitorMetric::memory);
        assert(memory_statistics.minimum == 63.0);
        assert(memory_statistics.average == 69.0);
        assert(memory_statistics.maximum == 75.0);
        assert(view_model.history(MonitorMetric::temperature)[2].value == -6.0);
        const auto missing_statistics = view_model.statistics(MonitorMetric::network, "missing");
        assert(!missing_statistics.current);
        assert(!missing_statistics.minimum);
        assert(!missing_statistics.average);
        assert(!missing_statistics.maximum);
        assert(missing_statistics.sample_count == 0);
    }

    {
        auto second = make_sample(5120, 3000);
        std::swap(second.network_interfaces[0], second.network_interfaces[2]);
        second.network_interfaces.push_back({"usb0", "10.0.0.2", NetworkInterfaceState::up, false, 10000, 20000});
        auto third = make_sample(9216, 5000);
        third.network_interfaces.erase(third.network_interfaces.begin());
        third.network_interfaces.push_back({"usb0", "10.0.0.2", NetworkInterfaceState::up, false, 14096, 28192});
        std::vector<SystemSample> samples{make_sample(1024, 1000), second, third, make_sample(13312, 7000)};
        MonitorViewModel view_model(std::make_unique<SequenceSource>(std::move(samples)));
        view_model.refresh();
        view_model.refresh();
        view_model.refresh();
        const auto wireless = view_model.history(MonitorMetric::network, "wlan0");
        assert(wireless[1].value == 2048.0);
        assert(wireless[1].secondary_value == 1024.0);
        assert(!wireless[2].value);
        assert(!wireless[2].secondary_value);
        assert(wireless[3].value == 0.0);
        assert(wireless[3].secondary_value == 0.0);
        const auto virtual_interface = view_model.history(MonitorMetric::network, "Meta");
        assert(virtual_interface[1].value == 1024.0);
        assert(virtual_interface[1].secondary_value == 512.0);
        const auto added = view_model.history(MonitorMetric::network, "usb0");
        assert(!added[0].value);
        assert(!added[0].secondary_value);
        assert(added[1].value == 0.0);
        assert(added[2].value == 2048.0);
        assert(added[2].secondary_value == 4096.0);
        assert(!added[3].value);
        assert(!added[3].secondary_value);
        assert(view_model.statistics(MonitorMetric::network, "usb0").sample_count == 2);
    }

    {
        std::vector<SystemSample> samples;
        for (std::size_t i = 0; i < MonitorViewModel::history_capacity + 5; ++i) {
            auto sample           = make_sample(i * 1024, (i + 1) * 1000);
            sample.uptime_seconds = 100 + i;
            samples.push_back(std::move(sample));
        }
        MonitorViewModel view_model(std::make_unique<SequenceSource>(std::move(samples)));
        for (std::size_t i = 1; i < MonitorViewModel::history_capacity + 5; ++i) view_model.refresh();
        const auto points = view_model.history(MonitorMetric::uptime);
        assert(points.size() == MonitorViewModel::history_capacity);
        assert(points.front().timestamp_milliseconds == 6000);
        assert(points.back().timestamp_milliseconds == 65000);
        assert(points.front().value == 105.0);
        assert(points.back().value == 164.0);
        const auto statistics = view_model.statistics(MonitorMetric::uptime);
        assert(statistics.current == 164.0);
        assert(statistics.minimum == 105.0);
        assert(statistics.average == 134.5);
        assert(statistics.maximum == 164.0);
        assert(statistics.sample_count == MonitorViewModel::history_capacity);
    }

    {
        MonitorViewModel view_model(nullptr);
        view_model.refresh();
        assert(view_model.history(MonitorMetric::cpu).empty());
        const auto statistics = view_model.statistics(MonitorMetric::cpu);
        assert(!statistics.current);
        assert(!statistics.minimum);
        assert(!statistics.average);
        assert(!statistics.maximum);
        assert(statistics.sample_count == 0);
    }

    assert(MonitorViewModel::format_metric_value(MonitorMetric::cpu, 70) == "70%");
    assert(MonitorViewModel::format_metric_value(MonitorMetric::memory, 63) == "63%");
    assert(MonitorViewModel::format_metric_value(MonitorMetric::storage, 75) == "75%");
    assert(MonitorViewModel::format_metric_value(MonitorMetric::temperature, -6) == "-6°C");
    assert(MonitorViewModel::format_metric_value(MonitorMetric::network, 2048) == "2.0 KB/s");
    assert(MonitorViewModel::format_metric_value(MonitorMetric::uptime, 93784) == "1d 2h 3m");
    assert(MonitorViewModel::format_metric_value(MonitorMetric::uptime, 7384) == "2h 3m");
    assert(MonitorViewModel::format_metric_value(MonitorMetric::uptime, 65) == "1m 5s");
    assert(MonitorViewModel::format_metric_value(MonitorMetric::uptime, 5) == "5s");
    assert(MonitorViewModel::format_metric_value(MonitorMetric::cpu, std::numeric_limits<double>::quiet_NaN()) == "--");

    lv_deinit();
    return 0;
}
