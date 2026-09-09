#include "app.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kPartialRows                        = 80;
constexpr std::uint32_t kColorPageBackground      = 0xf2f2f7;
constexpr std::uint32_t kColorContainerBackground = 0xffffff;
constexpr std::uint32_t kColorContainerTitle      = 0x1a1a1a;
constexpr std::uint32_t kColorText                = 0x000000;

std::vector<lv_color32_t> framebuffer;
int display_width = 0;
lv_point_t pointer_position{};
lv_indev_state_t pointer_state = LV_INDEV_STATE_RELEASED;

void read_pointer(lv_indev_t *, lv_indev_data_t *data)
{
    data->point = pointer_position;
    data->state = pointer_state;
}

void click_pointer(lv_indev_t *pointer, int x, int y)
{
    pointer_position = {x, y};
    pointer_state    = LV_INDEV_STATE_PRESSED;
    lv_indev_read(pointer);
    pointer_state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(pointer);
}

std::string label_text(lv_obj_t *root, const char *name)
{
    auto *label = lv_obj_find_by_name(root, name);
    assert(label && lv_obj_check_type(label, &lv_label_class));
    return lv_label_get_text(label);
}

std::size_t valid_chart_points(lv_obj_t *chart, lv_chart_series_t *series)
{
    const auto *points = lv_chart_get_series_y_array(chart, series);
    return static_cast<std::size_t>(std::count_if(points, points + lv_chart_get_point_count(chart),
                                                  [](int32_t value) { return value != LV_CHART_POINT_NONE; }));
}

lv_obj_t *assert_metric_popup(lv_obj_t *root, const char *title, std::size_t series_count)
{
    lv_obj_update_layout(root);
    auto *surface = lv_obj_find_by_name(root, "app_surface");
    auto *overlay = lv_obj_find_by_name(root, "monitor_metric_overlay");
    auto *dialog  = lv_obj_find_by_name(root, "monitor_metric_dialog");
    auto *chart   = lv_obj_find_by_name(root, "monitor_metric_chart");
    assert(surface && overlay && dialog && chart);
    assert(lv_obj_get_parent(overlay) == surface);
    assert(lv_obj_check_type(chart, &lv_chart_class));
    assert(label_text(root, "monitor_metric_title") == title);
    assert(lv_obj_find_by_name(root, "monitor_metric_close"));
    for (const char *name :
         {"monitor_metric_current", "monitor_metric_min", "monitor_metric_average", "monitor_metric_max"}) {
        assert(!label_text(root, name).empty());
    }
    if (series_count == 2) {
        for (const char *name : {"monitor_metric_secondary_current", "monitor_metric_secondary_min",
                                 "monitor_metric_secondary_average", "monitor_metric_secondary_max"}) {
            assert(!label_text(root, name).empty());
        }
    }

    lv_area_t surface_bounds{};
    lv_area_t overlay_bounds{};
    lv_area_t dialog_bounds{};
    lv_area_t chart_bounds{};
    lv_obj_get_coords(surface, &surface_bounds);
    lv_obj_get_coords(overlay, &overlay_bounds);
    lv_obj_get_coords(dialog, &dialog_bounds);
    lv_obj_get_coords(chart, &chart_bounds);
    assert(overlay_bounds.x1 == surface_bounds.x1 && overlay_bounds.x2 == surface_bounds.x2);
    assert(overlay_bounds.y1 == surface_bounds.y1 && overlay_bounds.y2 == surface_bounds.y2);
    assert(dialog_bounds.x1 >= surface_bounds.x1 + 16 && dialog_bounds.x2 <= surface_bounds.x2 - 16);
    assert(dialog_bounds.y1 >= surface_bounds.y1 + 16 && dialog_bounds.y2 <= surface_bounds.y2 - 16);
    assert(lv_area_get_width(&dialog_bounds) <= 760 && lv_area_get_height(&dialog_bounds) <= 600);
    assert(chart_bounds.x1 >= dialog_bounds.x1 && chart_bounds.x2 <= dialog_bounds.x2);
    assert(chart_bounds.y1 >= dialog_bounds.y1 && chart_bounds.y2 <= dialog_bounds.y2);
    assert(lv_area_get_width(&chart_bounds) > 100 && lv_area_get_height(&chart_bounds) > 60);
    assert(lv_chart_get_point_count(chart) > 0 && lv_chart_get_point_count(chart) <= 60);
    std::size_t actual_series_count = 0;
    for (auto *series = lv_chart_get_series_next(chart, nullptr); series;
         series       = lv_chart_get_series_next(chart, series)) {
        ++actual_series_count;
    }
    assert(actual_series_count == series_count);
    return chart;
}

void close_metric_popup(lv_obj_t *root)
{
    auto *close = lv_obj_find_by_name(root, "monitor_metric_close");
    assert(close);
    lv_obj_send_event(close, LV_EVENT_CLICKED, nullptr);
    assert(!lv_obj_find_by_name(root, "monitor_metric_overlay"));
}

void assert_clickable_card_tree(lv_obj_t *root, lv_obj_t *object, const char *title, std::size_t series_count)
{
    lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
    assert_metric_popup(root, title, series_count);
    close_metric_popup(root);
    for (std::uint32_t index = 0; index < lv_obj_get_child_count(object); ++index) {
        assert_clickable_card_tree(root, lv_obj_get_child(object, static_cast<int32_t>(index)), title, series_count);
    }
}

void assert_initial_statistics(lv_obj_t *root, const char *expected)
{
    for (const char *name :
         {"monitor_metric_current", "monitor_metric_min", "monitor_metric_average", "monitor_metric_max"}) {
        assert(label_text(root, name) == expected);
    }
    auto *empty = lv_obj_find_by_name(root, "monitor_metric_empty");
    assert(empty && lv_obj_has_flag(empty, LV_OBJ_FLAG_HIDDEN));
    auto *chart = lv_obj_find_by_name(root, "monitor_metric_chart");
    assert(valid_chart_points(chart, lv_chart_get_series_next(chart, nullptr)) == 1);
}

void flush_display(lv_display_t *display, const lv_area_t *area, std::uint8_t *pixels)
{
    const int width    = area->x2 - area->x1 + 1;
    const auto *source = reinterpret_cast<const lv_color32_t *>(pixels);

    for (int y = area->y1; y <= area->y2; ++y) {
        const auto source_offset = static_cast<std::size_t>(y - area->y1) * static_cast<std::size_t>(width);
        const auto destination_offset =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(display_width) + static_cast<std::size_t>(area->x1);
        std::copy_n(source + source_offset, width, framebuffer.begin() + destination_offset);
    }
    lv_display_flush_ready(display);
}

bool write_ppm(const char *path, int width, int height)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;

    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto &pixel : framebuffer) {
        file.put(static_cast<char>(pixel.red));
        file.put(static_cast<char>(pixel.green));
        file.put(static_cast<char>(pixel.blue));
    }
    return file.good();
}

bool colors_differ(const lv_color32_t &left, const lv_color32_t &right)
{
    return left.red != right.red || left.green != right.green || left.blue != right.blue || left.alpha != right.alpha;
}

void write_text(const std::filesystem::path &path, const char *text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << text;
    assert(file.good());
}

std::filesystem::path create_linux_fixture(const char *output_path)
{
    const auto fixture    = std::filesystem::path(std::string(output_path) + ".fixture");
    const auto proc       = fixture / "proc";
    const auto sys        = fixture / "sys";
    const auto os_release = fixture / "os-release";
    write_text(proc / "stat", "cpu  470 0 230 300 0 0 0 0 0 0\n");
    write_text(proc / "meminfo", "MemTotal:        8388608 kB\nMemAvailable:    3145728 kB\n");
    write_text(proc / "uptime", "93784.00 20000.00\n");
    write_text(proc / "loadavg", "1.42 1.00 0.75 1/100 123\n");
    write_text(proc / "net/route",
               "Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT\n"
               "wlan0 00000000 0101A8C0 0003 0 0 100 00000000 0 0 0\n");
    write_text(
        proc / "net/dev",
        "Inter-| Receive | Transmit\n face |bytes packets errs drop fifo frame compressed multicast|bytes packets errs "
        "drop fifo colls carrier compressed\n"
        " lo: 100 1 0 0 0 0 0 0 100 1 0 0 0 0 0 0\n"
        " eth0: 1048576 1 0 0 0 0 0 0 524288 1 0 0 0 0 0 0\n"
        " wlan0: 2097152 1 0 0 0 0 0 0 1048576 1 0 0 0 0 0 0\n"
        " docker0: 200 1 0 0 0 0 0 0 300 1 0 0 0 0 0 0\n"
        " br-0123456789ab: 200 1 0 0 0 0 0 0 300 1 0 0 0 0 0 0\n"
        " veth0123456: 200 1 0 0 0 0 0 0 300 1 0 0 0 0 0 0\n"
        " Meta: 4096 1 0 0 0 0 0 0 2048 1 0 0 0 0 0 0\n");
    write_text(proc / "sys/kernel/hostname", "lilygo-cm0\n");
    write_text(proc / "sys/kernel/osrelease", "6.12.0-cm0\n");
    write_text(sys / "class/thermal/thermal_zone0/temp", "52400\n");
    write_text(sys / "class/net/eth0/operstate", "down\n");
    write_text(sys / "class/net/wlan0/operstate", "up\n");
    write_text(sys / "class/net/Meta/operstate", "unknown\n");
    write_text(os_release, "PRETTY_NAME=\"Raspberry Pi OS\"\n");
    setenv("LILYGO_MONITOR_PROC_ROOT", proc.c_str(), 1);
    setenv("LILYGO_MONITOR_SYS_ROOT", sys.c_str(), 1);
    setenv("LILYGO_MONITOR_OS_RELEASE", os_release.c_str(), 1);
    return fixture;
}

}  // namespace

int main(int argc, char **argv)
{
    assert(argc == 4);
    const int width  = std::atoi(argv[1]);
    const int height = std::atoi(argv[2]);
    assert(width >= 320 && height >= 240);
    const auto fixture = create_linux_fixture(argv[3]);

    display_width = width;
    framebuffer.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    std::vector<lv_color32_t> draw_buffer(static_cast<std::size_t>(width) * kPartialRows);

    lv_init();
    auto *display = lv_display_create(width, height);
    assert(display);
    lv_display_set_buffers(display, draw_buffer.data(), nullptr,
                           static_cast<std::uint32_t>(draw_buffer.size() * sizeof(lv_color32_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_display);
    lv_display_set_default(display);

    cm0_app_context_t context{};
    context.root           = lv_screen_active();
    const auto *descriptor = cm0_app_get_descriptor();
    descriptor->open(&context);
    lv_obj_update_layout(context.root);

    auto *surface           = lv_obj_find_by_name(context.root, "app_surface");
    auto *header            = lv_obj_find_by_name(context.root, "app_header");
    auto *title             = lv_obj_find_by_name(context.root, "app_title");
    auto *summary           = lv_obj_find_by_name(context.root, "monitor_summary");
    auto *cpu_card          = lv_obj_find_by_name(context.root, "monitor_cpu_card");
    auto *memory_card       = lv_obj_find_by_name(context.root, "monitor_memory_card");
    auto *storage_card      = lv_obj_find_by_name(context.root, "monitor_storage_card");
    auto *temperature_card  = lv_obj_find_by_name(context.root, "monitor_temperature_card");
    auto *cpu_value         = lv_obj_find_by_name(context.root, "monitor_cpu_value");
    auto *memory_value      = lv_obj_find_by_name(context.root, "monitor_memory_value");
    auto *temperature_value = lv_obj_find_by_name(context.root, "monitor_temperature_value");
    auto *cpu_bar           = lv_obj_find_by_name(context.root, "monitor_cpu_bar");
    auto *memory_bar        = lv_obj_find_by_name(context.root, "monitor_memory_bar");
    auto *network_section   = lv_obj_find_by_name(context.root, "monitor_network_section");
    auto *network_cards     = lv_obj_find_by_name(context.root, "monitor_network_cards");
    auto *wlan_card         = lv_obj_find_by_name(context.root, "monitor_network_card_wlan0");
    auto *eth_card          = lv_obj_find_by_name(context.root, "monitor_network_card_eth0");
    auto *meta_card         = lv_obj_find_by_name(context.root, "monitor_network_card_Meta");
    auto *wlan_status       = lv_obj_find_by_name(context.root, "monitor_network_status_wlan0");
    auto *eth_status        = lv_obj_find_by_name(context.root, "monitor_network_status_eth0");
    auto *meta_status       = lv_obj_find_by_name(context.root, "monitor_network_status_Meta");
    auto *system_card       = lv_obj_find_by_name(context.root, "monitor_system_card");
    auto *hostname          = lv_obj_find_by_name(context.root, "monitor_hostname");
    auto *operating_system  = lv_obj_find_by_name(context.root, "monitor_operating_system");
    auto *kernel            = lv_obj_find_by_name(context.root, "monitor_kernel");
    auto *uptime            = lv_obj_find_by_name(context.root, "monitor_uptime");
    assert(surface && header && title && summary && cpu_card && memory_card && storage_card && temperature_card);
    assert(cpu_value && memory_value && temperature_value && cpu_bar && memory_bar);
    assert(network_section && network_cards && wlan_card && eth_card && meta_card);
    assert(wlan_status && eth_status && meta_status);
    assert(system_card && uptime);
    assert(!hostname && !operating_system && !kernel);
    assert(lv_color_eq(lv_obj_get_style_bg_color(surface, LV_PART_MAIN), lv_color_hex(kColorPageBackground)));
    assert(lv_obj_get_style_bg_opa(surface, LV_PART_MAIN) == LV_OPA_COVER);
    assert(lv_obj_get_style_bg_grad_dir(surface, LV_PART_MAIN) == LV_GRAD_DIR_NONE);
    assert(lv_obj_get_style_bg_opa(header, LV_PART_MAIN) == LV_OPA_TRANSP);
    assert(lv_obj_get_style_border_width(header, LV_PART_MAIN) == 0);
    lv_area_t title_bounds{};
    lv_area_t card_bounds{};
    lv_obj_get_coords(title, &title_bounds);
    lv_obj_get_coords(cpu_card, &card_bounds);
    const int title_card_gap = card_bounds.y1 - title_bounds.y2 - 1;
    assert(title_card_gap >= 0 && title_card_gap <= 24);
    assert(lv_color_eq(lv_obj_get_style_bg_color(cpu_card, LV_PART_MAIN), lv_color_hex(kColorContainerBackground)));
    assert(lv_obj_get_style_bg_opa(cpu_card, LV_PART_MAIN) == LV_OPA_COVER);
    assert(lv_color_eq(lv_obj_get_style_bg_color(wlan_card, LV_PART_MAIN), lv_color_hex(kColorContainerBackground)));
    assert(lv_color_eq(lv_obj_get_style_text_color(title, LV_PART_MAIN), lv_color_hex(kColorContainerTitle)));
    assert(lv_color_eq(lv_obj_get_style_text_color(cpu_value, LV_PART_MAIN), lv_color_hex(kColorText)));
    assert(std::string(lv_label_get_text(cpu_value)) == "70%");
    assert(std::string(lv_label_get_text(memory_value)) == "63%");
    assert(std::string(lv_label_get_text(temperature_value)) == "52°C");
    assert(std::string(lv_label_get_text(wlan_status)) == "UP · DEFAULT");
    assert(std::string(lv_label_get_text(eth_status)) == "DOWN");
    assert(std::string(lv_label_get_text(meta_status)) == "UNKNOWN");
    assert(lv_obj_find_by_name(context.root, "monitor_network_card_docker0") == nullptr);
    assert(std::string(lv_label_get_text(uptime)) == "Uptime 1d 2h 3m");
    assert(lv_bar_get_value(cpu_bar) >= 0 && lv_bar_get_value(cpu_bar) <= 100);
    assert(lv_bar_get_value(memory_bar) >= 0 && lv_bar_get_value(memory_bar) <= 100);

    auto *system_section = lv_obj_find_by_name(context.root, "monitor_system_section");
    assert(system_section && lv_obj_get_parent(system_card) == system_section);
    lv_area_t network_bounds{};
    lv_area_t system_bounds{};
    lv_obj_get_coords(network_section, &network_bounds);
    lv_obj_get_coords(system_section, &system_bounds);
    assert(system_bounds.y1 > network_bounds.y2);

    if (width >= 800) {
        assert(lv_obj_get_y(cpu_card) == lv_obj_get_y(memory_card));
        assert(lv_obj_get_y(cpu_card) == lv_obj_get_y(storage_card));
        assert(lv_obj_get_y(cpu_card) == lv_obj_get_y(temperature_card));
        assert(lv_obj_get_y(wlan_card) == lv_obj_get_y(eth_card));
        assert(lv_obj_get_y(wlan_card) == lv_obj_get_y(meta_card));
        assert(lv_obj_get_x(eth_card) > lv_obj_get_x(wlan_card));
        assert(lv_obj_get_x(meta_card) > lv_obj_get_x(eth_card));
    } else {
        assert(lv_obj_get_y(cpu_card) == lv_obj_get_y(memory_card));
        assert(lv_obj_get_y(storage_card) > lv_obj_get_y(cpu_card));
        assert(lv_obj_get_y(storage_card) == lv_obj_get_y(temperature_card));
        assert(lv_obj_get_y(eth_card) > lv_obj_get_y(wlan_card));
        assert(lv_obj_get_y(meta_card) > lv_obj_get_y(eth_card));
    }

    lv_tick_inc(20);
    lv_refr_now(display);
    const bool has_contrast = std::any_of(framebuffer.cbegin() + 1, framebuffer.cend(),
                                          [](const auto &pixel) { return colors_differ(pixel, framebuffer[0]); });
    assert(has_contrast);
    assert(write_ppm(argv[3], width, height));

    assert(!lv_obj_find_by_name(context.root, "monitor_metric_overlay"));
    assert_clickable_card_tree(context.root, cpu_card, "CPU", 1);
    assert_clickable_card_tree(context.root, memory_card, "Memory", 1);
    assert_clickable_card_tree(context.root, storage_card, "Storage", 1);
    assert_clickable_card_tree(context.root, temperature_card, "Temperature", 1);
    assert_clickable_card_tree(context.root, wlan_card, "wlan0", 2);
    assert_clickable_card_tree(context.root, eth_card, "eth0", 2);
    assert_clickable_card_tree(context.root, meta_card, "Meta", 2);
    assert_clickable_card_tree(context.root, system_card, "Uptime", 1);

    const auto save_popup = [display, width, height, output_path = std::string(argv[3])](const char *suffix) {
        lv_refr_now(display);
        assert(write_ppm((output_path + suffix + ".ppm").c_str(), width, height));
    };
    lv_obj_send_event(memory_value, LV_EVENT_CLICKED, nullptr);
    assert_metric_popup(context.root, "Memory", 1);
    assert_initial_statistics(context.root, "63%");
    close_metric_popup(context.root);

    lv_obj_send_event(temperature_value, LV_EVENT_CLICKED, nullptr);
    assert_metric_popup(context.root, "Temperature", 1);
    assert_initial_statistics(context.root, "52°C");
    save_popup(".temperature");
    close_metric_popup(context.root);

    auto *pointer = lv_indev_create();
    assert(pointer);
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer, display);
    lv_indev_set_mode(pointer, LV_INDEV_MODE_EVENT);
    lv_indev_set_read_cb(pointer, read_pointer);
    lv_area_t value_bounds{};
    lv_obj_get_coords(cpu_value, &value_bounds);
    click_pointer(pointer, (value_bounds.x1 + value_bounds.x2) / 2, (value_bounds.y1 + value_bounds.y2) / 2);
    assert_metric_popup(context.root, "CPU", 1);
    assert_initial_statistics(context.root, "70%");
    auto *dialog = lv_obj_find_by_name(context.root, "monitor_metric_dialog");
    lv_obj_send_event(dialog, LV_EVENT_CLICKED, nullptr);
    assert_metric_popup(context.root, "CPU", 1);

    write_text(fixture / "proc/stat", "cpu  770 0 430 800 0 0 0 0 0 0\n");
    write_text(fixture / "sys/class/thermal/thermal_zone0/temp", "48400\n");
    lv_tick_inc(1000);
    lv_timer_handler();
    auto *cpu_chart = assert_metric_popup(context.root, "CPU", 1);
    assert(label_text(context.root, "monitor_metric_current") == "50%");
    assert(label_text(context.root, "monitor_metric_min") == "50%");
    assert(label_text(context.root, "monitor_metric_average") == "60%");
    assert(label_text(context.root, "monitor_metric_max") == "70%");
    assert(valid_chart_points(cpu_chart, lv_chart_get_series_next(cpu_chart, nullptr)) == 2);

    auto *overlay = lv_obj_find_by_name(context.root, "monitor_metric_overlay");
    constexpr const char *statistic_names[]{"monitor_metric_current", "monitor_metric_min", "monitor_metric_average",
                                            "monitor_metric_max"};
    std::vector<std::string> statistics_before_resize;
    for (const char *name : statistic_names) statistics_before_resize.push_back(label_text(context.root, name));
    auto *cpu_series       = lv_chart_get_series_next(cpu_chart, nullptr);
    const auto point_count = lv_chart_get_point_count(cpu_chart);
    const auto *x_values   = lv_chart_get_series_x_array(cpu_chart, cpu_series);
    const auto *y_values   = lv_chart_get_series_y_array(cpu_chart, cpu_series);
    const std::vector<std::int32_t> x_values_before_resize(x_values, x_values + point_count);
    const std::vector<std::int32_t> y_values_before_resize(y_values, y_values + point_count);
    // Restore the display before rendering into the framebuffer with its original dimensions.
    for (const bool swapped : {true, false}) {
        const int resized_width  = swapped ? height : width;
        const int resized_height = swapped ? width : height;
        lv_display_set_resolution(display, resized_width, resized_height);
        lv_obj_update_layout(context.root);
        assert(assert_metric_popup(context.root, "CPU", 1) == cpu_chart);
        assert(lv_obj_find_by_name(context.root, "monitor_metric_overlay") == overlay);
        assert(lv_obj_find_by_name(context.root, "monitor_metric_dialog") == dialog);
        assert(lv_obj_get_width(surface) == resized_width && lv_obj_get_height(surface) == resized_height);
        std::size_t statistic_index = 0;
        for (const char *name : statistic_names)
            assert(label_text(context.root, name) == statistics_before_resize[statistic_index++]);
        assert(lv_chart_get_point_count(cpu_chart) == point_count);
        assert(std::equal(x_values_before_resize.begin(), x_values_before_resize.end(),
                          lv_chart_get_series_x_array(cpu_chart, cpu_series)));
        assert(std::equal(y_values_before_resize.begin(), y_values_before_resize.end(),
                          lv_chart_get_series_y_array(cpu_chart, cpu_series)));
    }
    save_popup(".cpu");

    lv_area_t surface_bounds{};
    lv_obj_get_coords(surface, &surface_bounds);
    click_pointer(pointer, surface_bounds.x1 + 2, surface_bounds.y1 + 2);
    assert(!lv_obj_find_by_name(context.root, "monitor_metric_overlay"));
    lv_obj_send_event(meta_card, LV_EVENT_CLICKED, nullptr);
    auto *meta_chart = assert_metric_popup(context.root, "Meta", 2);
    std::vector<std::size_t> meta_history_counts;
    for (auto *series = lv_chart_get_series_next(meta_chart, nullptr); series;
         series       = lv_chart_get_series_next(meta_chart, series)) {
        meta_history_counts.push_back(valid_chart_points(meta_chart, series));
        assert(meta_history_counts.back() > 0);
    }

    write_text(
        fixture / "proc/net/dev",
        "Inter-| Receive | Transmit\n face |bytes packets errs drop fifo frame compressed multicast|bytes packets errs "
        "drop fifo colls carrier compressed\n"
        " lo: 100 1 0 0 0 0 0 0 100 1 0 0 0 0 0 0\n"
        " eth0: 1048576 1 0 0 0 0 0 0 524288 1 0 0 0 0 0 0\n"
        " wlan0: 2101248 1 0 0 0 0 0 0 1050624 1 0 0 0 0 0 0\n"
        " docker0: 300 1 0 0 0 0 0 0 400 1 0 0 0 0 0 0\n"
        " usb0: 8192 1 0 0 0 0 0 0 4096 1 0 0 0 0 0 0\n");
    write_text(fixture / "sys/class/net/usb0/operstate", "up\n");
    lv_tick_inc(1000);
    lv_timer_handler();
    lv_obj_update_layout(context.root);
    assert(lv_obj_find_by_name(context.root, "monitor_network_card_Meta") == nullptr);
    assert(lv_obj_find_by_name(context.root, "monitor_network_card_usb0") != nullptr);
    assert(lv_obj_find_by_name(context.root, "monitor_network_card_wlan0") != nullptr);
    assert(lv_obj_find_by_name(context.root, "monitor_network_card_eth0") != nullptr);
    assert(lv_obj_find_by_name(context.root, "monitor_network_card_docker0") == nullptr);

    meta_chart = assert_metric_popup(context.root, "Meta", 2);
    assert(label_text(context.root, "monitor_metric_current") == "--");
    assert(label_text(context.root, "monitor_metric_secondary_current") == "--");
    std::size_t series_index = 0;
    for (auto *series = lv_chart_get_series_next(meta_chart, nullptr); series;
         series       = lv_chart_get_series_next(meta_chart, series)) {
        assert(valid_chart_points(meta_chart, series) == meta_history_counts[series_index++]);
    }
    auto *empty = lv_obj_find_by_name(context.root, "monitor_metric_empty");
    assert(empty && lv_obj_has_flag(empty, LV_OBJ_FLAG_HIDDEN));
    close_metric_popup(context.root);

    auto *wlan_rx = lv_obj_find_by_name(context.root, "monitor_network_rx_wlan0");
    assert(wlan_rx);
    lv_obj_send_event(wlan_rx, LV_EVENT_CLICKED, nullptr);
    assert_metric_popup(context.root, "wlan0", 2);
    assert(label_text(context.root, "monitor_metric_current") == lv_label_get_text(wlan_rx));
    assert(label_text(context.root, "monitor_metric_secondary_current") ==
           label_text(context.root, "monitor_network_tx_wlan0"));
    save_popup(".network");
    close_metric_popup(context.root);

    lv_obj_send_event(temperature_card, LV_EVENT_CLICKED, nullptr);
    assert_metric_popup(context.root, "Temperature", 1);
    assert(label_text(context.root, "monitor_metric_current") == "48°C");
    assert(label_text(context.root, "monitor_metric_min") == "48°C");
    assert(label_text(context.root, "monitor_metric_max") == "52°C");
    save_popup(".temperature");
    close_metric_popup(context.root);

    lv_obj_send_event(uptime, LV_EVENT_CLICKED, nullptr);
    assert_metric_popup(context.root, "Uptime", 1);
    save_popup(".uptime");
    descriptor->close();
    assert(!lv_obj_find_by_name(context.root, "app_surface"));
    lv_tick_inc(1000);
    lv_timer_handler();
    lv_indev_delete(pointer);

    std::filesystem::remove(fixture / "sys/class/thermal/thermal_zone0/temp");
    descriptor->open(&context);
    lv_obj_update_layout(context.root);
    temperature_card = lv_obj_find_by_name(context.root, "monitor_temperature_card");
    assert(temperature_card);
    lv_obj_send_event(temperature_card, LV_EVENT_CLICKED, nullptr);
    auto *empty_chart = assert_metric_popup(context.root, "Temperature", 1);
    assert(valid_chart_points(empty_chart, lv_chart_get_series_next(empty_chart, nullptr)) == 0);
    for (const char *name :
         {"monitor_metric_current", "monitor_metric_min", "monitor_metric_average", "monitor_metric_max"}) {
        assert(label_text(context.root, name) == "--");
    }
    empty = lv_obj_find_by_name(context.root, "monitor_metric_empty");
    assert(empty && !lv_obj_has_flag(empty, LV_OBJ_FLAG_HIDDEN));
    descriptor->close();

    unsetenv("LILYGO_MONITOR_PROC_ROOT");
    unsetenv("LILYGO_MONITOR_SYS_ROOT");
    unsetenv("LILYGO_MONITOR_OS_RELEASE");
    std::filesystem::remove_all(fixture);
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
    return 0;
}
