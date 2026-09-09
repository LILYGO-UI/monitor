#ifndef LILYGO_UI_MONITOR_PAGES_MONITOR_SYSTEM_METRICS_SOURCE_HPP
#define LILYGO_UI_MONITOR_PAGES_MONITOR_SYSTEM_METRICS_SOURCE_HPP

#include "pages/monitor/monitor_model.hpp"

class SystemMetricsSource {
public:
    virtual ~SystemMetricsSource()            = default;
    [[nodiscard]] virtual SystemSample read() = 0;
};

class LinuxSystemMetricsSource final : public SystemMetricsSource {
public:
    [[nodiscard]] SystemSample read() override;
};

#endif
