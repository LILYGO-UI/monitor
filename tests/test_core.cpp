#include "pages/monitor/monitor_model.hpp"
#include "pages/monitor/system_metrics_source.hpp"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace {

void test_model()
{
    MonitorModel model;
    SystemSample first;
    first.cpu_valid                 = true;
    first.cpu_total                 = 1000;
    first.cpu_idle                  = 400;
    first.cpu_count                 = 4;
    first.memory_valid              = true;
    first.memory_total_bytes        = 8000;
    first.memory_available_bytes    = 2000;
    first.storage_valid             = true;
    first.storage_total_bytes       = 10000;
    first.storage_available_bytes   = 2500;
    first.temperature_valid         = true;
    first.temperature_milli_celsius = 52400;
    first.uptime_valid              = true;
    first.uptime_seconds            = 93784;
    first.load_valid                = true;
    first.load_one                  = 1.25;
    first.monotonic_milliseconds    = 1000;
    first.network_interfaces        = {
        {"eth0", "192.168.1.20", NetworkInterfaceState::up, true, 10000, 5000},
        {"wlan0", "", NetworkInterfaceState::down, false, 4000, 2000},
    };
    first.hostname         = "cm0";
    first.operating_system = "Raspberry Pi OS";
    first.kernel_release   = "6.12.0";
    model.update(first);

    const auto &initial = model.snapshot();
    assert(initial.cpu_percent == 60);
    assert(initial.memory_percent == 75);
    assert(initial.memory_used_bytes == 6000);
    assert(initial.storage_percent == 75);
    assert(initial.temperature_celsius == 52);
    assert(initial.network_interfaces.size() == 2);
    assert(initial.network_interfaces[0].receive_bytes_per_second == 0.0);
    assert(initial.network_interfaces[1].state == NetworkInterfaceState::down);
    assert(initial.hostname == "cm0");

    SystemSample second                         = first;
    second.cpu_total                            = 1200;
    second.cpu_idle                             = 450;
    second.network_interfaces[0].received_bytes = 14096;
    second.network_interfaces[0].sent_bytes     = 7048;
    second.network_interfaces[1].received_bytes = 6048;
    second.network_interfaces[1].sent_bytes     = 3024;
    second.monotonic_milliseconds               = 3000;
    model.update(second);
    const auto &updated = model.snapshot();
    assert(updated.cpu_percent == 75);
    assert(std::abs(updated.network_interfaces[0].receive_bytes_per_second - 2048.0) < 0.01);
    assert(std::abs(updated.network_interfaces[0].send_bytes_per_second - 1024.0) < 0.01);
    assert(std::abs(updated.network_interfaces[1].receive_bytes_per_second - 1024.0) < 0.01);
    assert(std::abs(updated.network_interfaces[1].send_bytes_per_second - 512.0) < 0.01);

    SystemSample bounded            = second;
    bounded.memory_available_bytes  = 9000;
    bounded.storage_available_bytes = 12000;
    model.update(bounded);
    assert(model.snapshot().memory_percent == 0);
    assert(model.snapshot().storage_percent == 0);
}

void write_text(const std::filesystem::path &path, const char *text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << text;
    assert(file.good());
}

void test_system_metrics_source()
{
    const auto fixture = std::filesystem::temp_directory_path() /
                         ("lilygo-monitor-core-" + std::to_string(static_cast<long long>(getpid())));
    const auto proc       = fixture / "proc";
    const auto sys        = fixture / "sys";
    const auto os_release = fixture / "os-release";
    write_text(proc / "stat", "cpu  470 0 230 300 0 0 0 0 0 0\n");
    write_text(proc / "meminfo",
               "MemTotal:        8388608 kB\nHugePages_Total:       0\nMemAvailable:    3145728 kB\n");
    write_text(proc / "uptime", "93784.00 20000.00\n");
    write_text(proc / "loadavg", "1.42 1.00 0.75 1/100 123\n");
    write_text(proc / "net/route",
               "Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT\n"
               "fake0 00000000 0101A8C0 0003 0 0 100 00000000 0 0 0\n");
    write_text(
        proc / "net/dev",
        "Inter-| Receive | Transmit\n face |bytes packets errs drop fifo frame compressed multicast|bytes packets errs "
        "drop fifo colls carrier compressed\n"
        " lo: 100 1 0 0 0 0 0 0 100 1 0 0 0 0 0 0\n"
        " eth0: 1048576 1 0 0 0 0 0 0 524288 1 0 0 0 0 0 0\n"
        " docker0: 200 1 0 0 0 0 0 0 300 1 0 0 0 0 0 0\n"
        " br-0123456789ab: 200 1 0 0 0 0 0 0 300 1 0 0 0 0 0 0\n"
        " veth0123456: 200 1 0 0 0 0 0 0 300 1 0 0 0 0 0 0\n"
        " fake0: 2097152 1 0 0 0 0 0 0 1048576 1 0 0 0 0 0 0\n"
        " Meta: 4096 1 0 0 0 0 0 0 2048 1 0 0 0 0 0 0\n");
    write_text(proc / "sys/kernel/hostname", "lilygo-cm0\n");
    write_text(proc / "sys/kernel/osrelease", "6.12.0-cm0\n");
    write_text(sys / "class/thermal/thermal_zone0/temp", "52400\n");
    write_text(sys / "class/net/eth0/operstate", "down\n");
    write_text(sys / "class/net/fake0/operstate", "up\n");
    write_text(sys / "class/net/Meta/operstate", "unknown\n");
    write_text(os_release, "PRETTY_NAME=\"Raspberry Pi OS\"\n");

    setenv("LILYGO_MONITOR_PROC_ROOT", proc.c_str(), 1);
    setenv("LILYGO_MONITOR_SYS_ROOT", sys.c_str(), 1);
    setenv("LILYGO_MONITOR_OS_RELEASE", os_release.c_str(), 1);
    LinuxSystemMetricsSource source;
    const auto sample = source.read();
    unsetenv("LILYGO_MONITOR_PROC_ROOT");
    unsetenv("LILYGO_MONITOR_SYS_ROOT");
    unsetenv("LILYGO_MONITOR_OS_RELEASE");
    std::filesystem::remove_all(fixture);

    assert(sample.cpu_valid && sample.cpu_total == 1000 && sample.cpu_idle == 300);
    assert(sample.memory_valid && sample.memory_total_bytes == 8388608ULL * 1024);
    assert(sample.memory_available_bytes == 3145728ULL * 1024);
    assert(sample.temperature_valid && sample.temperature_milli_celsius == 52400);
    assert(sample.uptime_valid && sample.uptime_seconds == 93784);
    assert(sample.load_valid && std::abs(sample.load_one - 1.42) < 0.001);
    assert(sample.network_interfaces.size() == 3);
    assert(sample.network_interfaces[0].name == "fake0" && sample.network_interfaces[0].is_default);
    assert(sample.network_interfaces[0].received_bytes == 2097152 &&
           sample.network_interfaces[0].sent_bytes == 1048576);
    assert(sample.network_interfaces[0].state == NetworkInterfaceState::up);
    assert(sample.network_interfaces[1].name == "eth0" &&
           sample.network_interfaces[1].state == NetworkInterfaceState::down);
    assert(sample.network_interfaces[2].name == "Meta" &&
           sample.network_interfaces[2].state == NetworkInterfaceState::unknown);
    assert(sample.hostname == "lilygo-cm0");
    assert(sample.operating_system == "Raspberry Pi OS");
    assert(sample.kernel_release == "6.12.0-cm0");
}

}  // namespace

int main()
{
    test_model();
    test_system_metrics_source();
    return 0;
}
