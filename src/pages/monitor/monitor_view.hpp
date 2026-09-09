#ifndef LILYGO_UI_MONITOR_PAGES_MONITOR_MONITOR_VIEW_HPP
#define LILYGO_UI_MONITOR_PAGES_MONITOR_MONITOR_VIEW_HPP

#include "pages/monitor/monitor_view_model.hpp"

#include <lvgl.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class MonitorView {
public:
    explicit MonitorView(MonitorViewModel &view_model) noexcept;

    lv_obj_t *create(lv_obj_t *parent, const char *application_name);
    void destroy() noexcept;

private:
    struct NetworkCardWidgets {
        std::string interface_name;
        lv_obj_t *card         = nullptr;
        lv_obj_t *status_dot   = nullptr;
        lv_obj_t *status       = nullptr;
        lv_obj_t *ip_address   = nullptr;
        lv_obj_t *receive_rate = nullptr;
        lv_obj_t *send_rate    = nullptr;
    };

    static void size_changed_event(lv_event_t *event);
    static void refresh_timer_event(lv_timer_t *timer);
    static void card_clicked_event(lv_event_t *event);
    static void close_dialog_event(lv_event_t *event);

    lv_obj_t *create_metric_card(lv_obj_t *parent, const char *name, const char *title, std::uint32_t accent,
                                 lv_subject_t *value, lv_subject_t *detail, lv_subject_t *progress,
                                 const char *value_name, const char *bar_name);
    lv_obj_t *create_network_section(lv_obj_t *parent);
    NetworkCardWidgets create_network_card(lv_obj_t *parent, const NetworkInterfaceViewData &interface);
    lv_obj_t *create_system_card(lv_obj_t *parent);
    void sync_network_cards();
    void update_network_card(NetworkCardWidgets &widgets, const NetworkInterfaceViewData &interface);
    void bind_card_click(lv_obj_t *card);
    void open_metric_dialog(MonitorMetric metric, const std::string &interface_name = {});
    void close_metric_dialog() noexcept;
    void refresh_metric_dialog();
    void layout_metric_dialog() noexcept;
    void apply_layout() noexcept;

    MonitorViewModel &view_model_;
    lv_obj_t *surface_ = nullptr;
    lv_obj_t *body_    = nullptr;
    lv_obj_t *summary_ = nullptr;
    std::array<lv_obj_t *, 4> metric_cards_{};
    lv_obj_t *network_section_  = nullptr;
    lv_obj_t *network_cards_    = nullptr;
    lv_obj_t *no_network_label_ = nullptr;
    std::vector<NetworkCardWidgets> network_card_widgets_;
    lv_obj_t *system_card_         = nullptr;
    lv_timer_t *refresh_timer_     = nullptr;
    MonitorMetric selected_metric_ = MonitorMetric::cpu;
    std::string selected_interface_;
    lv_obj_t *metric_overlay_                   = nullptr;
    lv_obj_t *metric_dialog_                    = nullptr;
    lv_obj_t *metric_chart_                     = nullptr;
    lv_obj_t *metric_empty_                     = nullptr;
    lv_obj_t *metric_axis_max_                  = nullptr;
    lv_obj_t *metric_axis_min_                  = nullptr;
    lv_obj_t *metric_time_start_                = nullptr;
    lv_chart_series_t *metric_series_           = nullptr;
    lv_chart_series_t *metric_secondary_series_ = nullptr;
    std::array<lv_obj_t *, 4> metric_statistics_{};
    std::array<lv_obj_t *, 4> metric_secondary_statistics_{};
};

#endif
