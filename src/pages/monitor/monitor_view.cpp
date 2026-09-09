#include "pages/monitor/monitor_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr int kNetworkCardHeight = 142;
constexpr int kNetworkCardGap    = 12;

void make_transparent(lv_obj_t *object)
{
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *create_bound_label(lv_obj_t *parent, lv_subject_t *subject, int font_size, std::uint32_t color,
                             const char *name = nullptr)
{
    auto *label = lv_label_create(parent);
    if (name) lv_obj_set_name_static(label, name);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(label, lilygo_ui_font_get(font_size), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_bind_text(label, subject, nullptr);
    return label;
}

lv_obj_t *create_section_title(lv_obj_t *parent, const char *text)
{
    auto *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, lilygo_ui_font_get(22), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(components::color_container_title), 0);
    return label;
}

lv_obj_t *create_label(lv_obj_t *parent, const char *text, int font_size, std::uint32_t color)
{
    auto *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, lilygo_ui_font_get(font_size), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

std::uint32_t network_state_color(NetworkInterfaceState state)
{
    switch (state) {
        case NetworkInterfaceState::up:
            return components::color_success;
        case NetworkInterfaceState::down:
            return components::color_temperature;
        case NetworkInterfaceState::unknown:
            return components::color_secondary_text;
    }
    return components::color_secondary_text;
}

void bubble_child_events(lv_obj_t *parent)
{
    for (std::uint32_t index = 0; index < lv_obj_get_child_count(parent); ++index) {
        auto *child = lv_obj_get_child(parent, index);
        lv_obj_add_flag(child, LV_OBJ_FLAG_EVENT_BUBBLE);
        bubble_child_events(child);
    }
}

const char *metric_title(MonitorMetric metric)
{
    switch (metric) {
        case MonitorMetric::cpu:
            return "CPU";
        case MonitorMetric::memory:
            return "Memory";
        case MonitorMetric::storage:
            return "Storage";
        case MonitorMetric::temperature:
            return "Temperature";
        case MonitorMetric::network:
            return "Network";
        case MonitorMetric::uptime:
            return "Uptime";
    }
    return "";
}

std::uint32_t metric_color(MonitorMetric metric)
{
    switch (metric) {
        case MonitorMetric::cpu:
            return components::color_cpu;
        case MonitorMetric::memory:
            return components::color_memory;
        case MonitorMetric::storage:
            return components::color_storage;
        case MonitorMetric::temperature:
            return components::color_temperature;
        case MonitorMetric::network:
            return components::color_cpu;
        case MonitorMetric::uptime:
            return components::color_memory;
    }
    return components::color_text;
}

std::array<lv_obj_t *, 4> create_statistics(lv_obj_t *parent, bool secondary, MonitorMetric metric)
{
    auto *row = lv_obj_create(parent);
    make_transparent(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 12, 0);
    constexpr std::array<const char *, 4> captions{"Current", "Min", "Average", "Max"};
    constexpr std::array<const char *, 4> names{"current", "min", "average", "max"};
    std::array<lv_obj_t *, 4> labels{};
    for (std::size_t index = 0; index < labels.size(); ++index) {
        auto *column = lv_obj_create(row);
        make_transparent(column);
        lv_obj_set_size(column, 1, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(column, 1);
        lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(column, 4, 0);
        const bool direction = metric == MonitorMetric::network && index == 0;
        create_label(column, direction ? (secondary ? "TX" : "RX") : captions[index], 14,
                     direction ? (secondary ? components::color_memory : components::color_cpu)
                               : components::color_secondary_text);
        const bool compact = metric == MonitorMetric::network || metric == MonitorMetric::uptime;
        labels[index]      = create_label(column, "--", compact ? 18 : 22, components::color_text);
        const auto name    = std::string(secondary ? "monitor_metric_secondary_" : "monitor_metric_") + names[index];
        lv_obj_set_name(labels[index], name.c_str());
        lv_obj_set_width(labels[index], LV_PCT(100));
        lv_obj_set_style_min_height(labels[index], compact ? 44 : 30, 0);
    }
    return labels;
}

}  // namespace

MonitorView::MonitorView(MonitorViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *MonitorView::create(lv_obj_t *parent, const char *application_name)
{
    surface_ = components::create_page(parent);
    lv_obj_add_event_cb(surface_, size_changed_event, LV_EVENT_SIZE_CHANGED, this);
    auto *header = components::create_header(surface_, application_name);

    // auto *live = lv_obj_create(header);
    // make_transparent(live);
    // lv_obj_set_size(live, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    // lv_obj_set_flex_flow(live, LV_FLEX_FLOW_ROW);
    // lv_obj_set_flex_align(live, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // lv_obj_set_style_pad_column(live, 8, 0);
    // lv_obj_align(live, LV_ALIGN_RIGHT_MID, 0, 0);
    // auto *live_dot = lv_obj_create(live);
    // lv_obj_set_size(live_dot, 10, 10);
    // lv_obj_set_style_radius(live_dot, LV_RADIUS_CIRCLE, 0);
    // lv_obj_set_style_border_width(live_dot, 0, 0);
    // lv_obj_set_style_bg_color(live_dot, lv_color_hex(components::color_success), 0);
    // lv_obj_set_style_bg_opa(live_dot, LV_OPA_COVER, 0);
    // auto *live_label = lv_label_create(live);
    // lv_label_set_text(live_label, "LIVE");
    // lv_obj_set_style_text_font(live_label, lilygo_ui_font_get(14), 0);
    // lv_obj_set_style_text_color(live_label, lv_color_hex(components::color_secondary_text), 0);

    body_ = lv_obj_create(surface_);
    lv_obj_set_name_static(body_, "monitor_body");
    lv_obj_set_width(body_, LV_PCT(100));
    lv_obj_set_flex_grow(body_, 1);
    lv_obj_set_style_border_width(body_, 0, 0);
    lv_obj_set_style_radius(body_, 0, 0);
    lv_obj_set_style_bg_opa(body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(body_, 20, 0);
    lv_obj_set_style_pad_top(body_, 4, 0);
    lv_obj_set_style_pad_row(body_, 14, 0);
    lv_obj_set_scroll_dir(body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(body_, LV_FLEX_FLOW_COLUMN);

    summary_ = lv_obj_create(body_);
    lv_obj_set_name_static(summary_, "monitor_summary");
    make_transparent(summary_);
    lv_obj_set_width(summary_, LV_PCT(100));
    lv_obj_set_style_pad_row(summary_, 12, 0);
    lv_obj_set_style_pad_column(summary_, 12, 0);
    lv_obj_set_layout(summary_, LV_LAYOUT_GRID);

    metric_cards_[0] = create_metric_card(summary_, "monitor_cpu_card", "CPU", components::color_cpu,
                                          view_model_.cpu_value_subject(), view_model_.cpu_detail_subject(),
                                          view_model_.cpu_percent_subject(), "monitor_cpu_value", "monitor_cpu_bar");
    metric_cards_[1] =
        create_metric_card(summary_, "monitor_memory_card", "Memory", components::color_memory,
                           view_model_.memory_value_subject(), view_model_.memory_detail_subject(),
                           view_model_.memory_percent_subject(), "monitor_memory_value", "monitor_memory_bar");
    metric_cards_[2] =
        create_metric_card(summary_, "monitor_storage_card", "Storage", components::color_storage,
                           view_model_.storage_value_subject(), view_model_.storage_detail_subject(),
                           view_model_.storage_percent_subject(), "monitor_storage_value", "monitor_storage_bar");
    metric_cards_[3] = create_metric_card(
        summary_, "monitor_temperature_card", "Temperature", components::color_temperature,
        view_model_.temperature_value_subject(), view_model_.temperature_detail_subject(),
        view_model_.temperature_progress_subject(), "monitor_temperature_value", "monitor_temperature_bar");

    network_section_ = create_network_section(body_);
    sync_network_cards();
    system_card_ = create_system_card(body_);

    lv_obj_update_layout(surface_);
    apply_layout();
    refresh_timer_ = lv_timer_create(refresh_timer_event, 1000, this);
    return surface_;
}

lv_obj_t *MonitorView::create_metric_card(lv_obj_t *parent, const char *name, const char *title, std::uint32_t accent,
                                          lv_subject_t *value, lv_subject_t *detail, lv_subject_t *progress,
                                          const char *value_name, const char *bar_name)
{
    auto *card = components::create_card(parent);
    lv_obj_set_name_static(card, name);
    lv_obj_set_height(card, 154);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    auto *heading = lv_obj_create(card);
    make_transparent(heading);
    lv_obj_set_size(heading, LV_PCT(100), 24);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(heading, 8, 0);
    auto *dot = lv_obj_create(heading);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    create_section_title(heading, title);

    create_bound_label(card, value, 36, components::color_text, value_name);
    create_bound_label(card, detail, 14, components::color_secondary_text);

    auto *bar = lv_bar_create(card);
    lv_obj_set_name_static(bar, bar_name);
    lv_obj_set_size(bar, LV_PCT(100), 6);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(components::color_progress_track), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_bind_value(bar, progress);
    bind_card_click(card);
    return card;
}

lv_obj_t *MonitorView::create_network_section(lv_obj_t *parent)
{
    auto *section = lv_obj_create(parent);
    lv_obj_set_name_static(section, "monitor_network_section");
    make_transparent(section);
    lv_obj_set_width(section, LV_PCT(100));
    lv_obj_set_style_pad_row(section, 10, 0);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    create_section_title(section, "Network");

    network_cards_ = lv_obj_create(section);
    lv_obj_set_name_static(network_cards_, "monitor_network_cards");
    make_transparent(network_cards_);
    lv_obj_set_width(network_cards_, LV_PCT(100));
    lv_obj_set_style_pad_row(network_cards_, kNetworkCardGap, 0);
    lv_obj_set_style_pad_column(network_cards_, kNetworkCardGap, 0);
    return section;
}

MonitorView::NetworkCardWidgets MonitorView::create_network_card(lv_obj_t *parent,
                                                                 const NetworkInterfaceViewData &interface)
{
    NetworkCardWidgets widgets;
    widgets.interface_name = interface.name;
    widgets.card           = components::create_card(parent);
    lv_obj_set_name(widgets.card, ("monitor_network_card_" + interface.name).c_str());
    lv_obj_set_height(widgets.card, kNetworkCardHeight);
    lv_obj_set_style_pad_all(widgets.card, 14, 0);
    lv_obj_set_style_pad_row(widgets.card, 5, 0);
    lv_obj_set_flex_flow(widgets.card, LV_FLEX_FLOW_COLUMN);

    auto *heading = lv_obj_create(widgets.card);
    make_transparent(heading);
    lv_obj_set_size(heading, LV_PCT(100), 24);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(heading, 8, 0);

    widgets.status_dot = lv_obj_create(heading);
    lv_obj_set_size(widgets.status_dot, 10, 10);
    lv_obj_set_style_radius(widgets.status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(widgets.status_dot, 0, 0);
    lv_obj_set_style_bg_opa(widgets.status_dot, LV_OPA_COVER, 0);

    auto *name = create_label(heading, interface.name.c_str(), 22, components::color_container_title);
    lv_obj_set_width(name, 1);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    widgets.status = create_label(heading, "", 14, components::color_secondary_text);
    lv_obj_set_name(widgets.status, ("monitor_network_status_" + interface.name).c_str());

    widgets.ip_address = create_label(widgets.card, "", 14, components::color_secondary_text);
    lv_obj_set_name(widgets.ip_address, ("monitor_network_ip_" + interface.name).c_str());
    lv_obj_set_width(widgets.ip_address, LV_PCT(100));
    lv_label_set_long_mode(widgets.ip_address, LV_LABEL_LONG_MODE_DOTS);

    auto *rates = lv_obj_create(widgets.card);
    make_transparent(rates);
    lv_obj_set_width(rates, LV_PCT(100));
    lv_obj_set_flex_grow(rates, 1);
    lv_obj_set_flex_flow(rates, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(rates, 18, 0);

    const auto create_rate = [rates](const char *caption) {
        auto *column = lv_obj_create(rates);
        make_transparent(column);
        lv_obj_set_width(column, 1);
        lv_obj_set_flex_grow(column, 1);
        lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
        create_label(column, caption, 14, components::color_secondary_text);
        auto *value = create_label(column, "--", 22, components::color_text);
        lv_obj_set_width(value, LV_PCT(100));
        lv_label_set_long_mode(value, LV_LABEL_LONG_MODE_DOTS);
        return value;
    };
    widgets.receive_rate = create_rate("RX");
    widgets.send_rate    = create_rate("TX");
    lv_obj_set_name(widgets.receive_rate, ("monitor_network_rx_" + interface.name).c_str());
    lv_obj_set_name(widgets.send_rate, ("monitor_network_tx_" + interface.name).c_str());
    update_network_card(widgets, interface);
    bind_card_click(widgets.card);
    return widgets;
}

lv_obj_t *MonitorView::create_system_card(lv_obj_t *parent)
{
    auto *section = lv_obj_create(parent);
    lv_obj_set_name_static(section, "monitor_system_section");
    make_transparent(section);
    lv_obj_set_size(section, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(section, 10, 0);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    create_section_title(section, "System");

    auto *card = components::create_card(section);
    lv_obj_set_name_static(card, "monitor_system_card");
    lv_obj_set_height(card, 68);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 3, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    create_bound_label(card, view_model_.uptime_subject(), 18, components::color_text, "monitor_uptime");
    bind_card_click(card);
    return card;
}

void MonitorView::destroy() noexcept
{
    if (refresh_timer_) lv_timer_delete(refresh_timer_);
    refresh_timer_ = nullptr;
    close_metric_dialog();
    if (surface_) lv_obj_delete(surface_);
    surface_ = nullptr;
    body_    = nullptr;
    summary_ = nullptr;
    metric_cards_.fill(nullptr);
    network_section_  = nullptr;
    network_cards_    = nullptr;
    no_network_label_ = nullptr;
    network_card_widgets_.clear();
    system_card_ = nullptr;
}

void MonitorView::size_changed_event(lv_event_t *event)
{
    static_cast<MonitorView *>(lv_event_get_user_data(event))->apply_layout();
}

void MonitorView::refresh_timer_event(lv_timer_t *timer)
{
    auto *view = static_cast<MonitorView *>(lv_timer_get_user_data(timer));
    view->view_model_.refresh();
    view->sync_network_cards();
    view->refresh_metric_dialog();
}

void MonitorView::bind_card_click(lv_obj_t *card)
{
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(components::color_progress_track), LV_STATE_PRESSED);
    lv_obj_add_event_cb(card, card_clicked_event, LV_EVENT_CLICKED, this);
    bubble_child_events(card);
}

void MonitorView::card_clicked_event(lv_event_t *event)
{
    auto *view = static_cast<MonitorView *>(lv_event_get_user_data(event));
    auto *card = lv_event_get_current_target_obj(event);
    constexpr std::array<MonitorMetric, 4> metrics{MonitorMetric::cpu, MonitorMetric::memory, MonitorMetric::storage,
                                                   MonitorMetric::temperature};
    for (std::size_t index = 0; index < view->metric_cards_.size(); ++index) {
        if (card == view->metric_cards_[index]) {
            view->open_metric_dialog(metrics[index]);
            return;
        }
    }
    for (const auto &widgets : view->network_card_widgets_) {
        if (card == widgets.card) {
            view->open_metric_dialog(MonitorMetric::network, widgets.interface_name);
            return;
        }
    }
    if (card == view->system_card_) view->open_metric_dialog(MonitorMetric::uptime);
}

void MonitorView::close_dialog_event(lv_event_t *event)
{
    auto *view = static_cast<MonitorView *>(lv_event_get_user_data(event));
    if (lv_event_get_target_obj(event) == lv_event_get_current_target_obj(event)) view->close_metric_dialog();
}

void MonitorView::open_metric_dialog(MonitorMetric metric, const std::string &interface_name)
{
    close_metric_dialog();
    selected_metric_    = metric;
    selected_interface_ = interface_name;
    metric_overlay_     = lv_obj_create(surface_);
    lv_obj_set_name_static(metric_overlay_, "monitor_metric_overlay");
    make_transparent(metric_overlay_);
    lv_obj_add_flag(metric_overlay_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(metric_overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(metric_overlay_);
    lv_obj_set_style_bg_color(metric_overlay_, lv_color_hex(components::color_text), 0);
    lv_obj_set_style_bg_opa(metric_overlay_, LV_OPA_50, 0);
    lv_obj_add_event_cb(metric_overlay_, close_dialog_event, LV_EVENT_CLICKED, this);

    metric_dialog_ = components::create_card(metric_overlay_);
    lv_obj_set_name_static(metric_dialog_, "monitor_metric_dialog");
    lv_obj_set_style_pad_all(metric_dialog_, 20, 0);
    lv_obj_set_style_pad_row(metric_dialog_, 12, 0);
    lv_obj_set_flex_flow(metric_dialog_, LV_FLEX_FLOW_COLUMN);

    auto *header = lv_obj_create(metric_dialog_);
    make_transparent(header);
    lv_obj_set_size(header, LV_PCT(100), 44);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    auto *title = create_label(header, metric == MonitorMetric::network ? interface_name.c_str() : metric_title(metric),
                               28, components::color_container_title);
    lv_obj_set_name_static(title, "monitor_metric_title");
    lv_obj_set_width(title, 1);
    lv_obj_set_flex_grow(title, 1);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    auto *close = components::create_button(header, LV_SYMBOL_CLOSE, close_dialog_event, this);
    lv_obj_set_name_static(close, "monitor_metric_close");
    lv_obj_set_size(close, 44, 44);
    lv_obj_set_style_pad_all(close, 0, 0);
    create_label(metric_dialog_, "Last 60 samples", 14, components::color_secondary_text);

    metric_statistics_ = create_statistics(metric_dialog_, false, metric);
    if (metric == MonitorMetric::network) {
        metric_secondary_statistics_ = create_statistics(metric_dialog_, true, metric);
    }

    auto *plot = lv_obj_create(metric_dialog_);
    make_transparent(plot);
    lv_obj_set_width(plot, LV_PCT(100));
    lv_obj_set_flex_grow(plot, 1);
    lv_obj_set_style_min_height(plot, 100, 0);
    lv_obj_set_flex_flow(plot, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(plot, 10, 0);
    const int axis_width = metric == MonitorMetric::network || metric == MonitorMetric::uptime ? 90 : 52;
    auto *axis           = lv_obj_create(plot);
    make_transparent(axis);
    lv_obj_set_size(axis, axis_width, LV_PCT(100));
    metric_axis_max_ = create_label(axis, "", 14, components::color_secondary_text);
    metric_axis_min_ = create_label(axis, "", 14, components::color_secondary_text);
    for (auto *label : {metric_axis_max_, metric_axis_min_}) {
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    }
    lv_obj_align(metric_axis_max_, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_align(metric_axis_min_, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    metric_chart_ = lv_chart_create(plot);
    lv_obj_set_name_static(metric_chart_, "monitor_metric_chart");
    lv_obj_set_size(metric_chart_, 1, LV_PCT(100));
    lv_obj_set_flex_grow(metric_chart_, 1);
    lv_obj_set_style_bg_opa(metric_chart_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(metric_chart_, 0, 0);
    lv_obj_set_style_radius(metric_chart_, 0, 0);
    lv_obj_set_style_pad_all(metric_chart_, 4, 0);
    lv_obj_set_style_line_color(metric_chart_, lv_color_hex(components::color_border), LV_PART_MAIN);
    lv_obj_set_style_line_width(metric_chart_, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(metric_chart_, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(metric_chart_, 5, 5, LV_PART_INDICATOR);
    lv_chart_set_type(metric_chart_, LV_CHART_TYPE_SCATTER);
    lv_chart_set_div_line_count(metric_chart_, 5, 5);
    lv_chart_set_axis_range(metric_chart_, LV_CHART_AXIS_PRIMARY_X, 0, 1000);
    lv_chart_set_axis_range(metric_chart_, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    metric_series_ = lv_chart_add_series(metric_chart_, lv_color_hex(metric_color(metric)), LV_CHART_AXIS_PRIMARY_Y);
    if (metric == MonitorMetric::network)
        metric_secondary_series_ =
            lv_chart_add_series(metric_chart_, lv_color_hex(components::color_memory), LV_CHART_AXIS_PRIMARY_Y);
    metric_empty_ = create_label(metric_chart_, "No data", 18, components::color_secondary_text);
    lv_obj_set_name_static(metric_empty_, "monitor_metric_empty");
    lv_obj_center(metric_empty_);

    auto *time_axis = lv_obj_create(metric_dialog_);
    make_transparent(time_axis);
    lv_obj_set_size(time_axis, LV_PCT(100), 20);
    lv_obj_set_style_pad_left(time_axis, axis_width + 10, 0);
    metric_time_start_ = create_label(time_axis, "", 14, components::color_secondary_text);
    auto *now          = create_label(time_axis, "Now", 14, components::color_secondary_text);
    lv_obj_align(now, LV_ALIGN_RIGHT_MID, 0, 0);
    layout_metric_dialog();
    refresh_metric_dialog();
}

void MonitorView::close_metric_dialog() noexcept
{
    if (metric_overlay_) lv_obj_delete(metric_overlay_);
    metric_overlay_          = nullptr;
    metric_dialog_           = nullptr;
    metric_chart_            = nullptr;
    metric_empty_            = nullptr;
    metric_axis_max_         = nullptr;
    metric_axis_min_         = nullptr;
    metric_time_start_       = nullptr;
    metric_series_           = nullptr;
    metric_secondary_series_ = nullptr;
    metric_statistics_.fill(nullptr);
    metric_secondary_statistics_.fill(nullptr);
    selected_interface_.clear();
}

void MonitorView::refresh_metric_dialog()
{
    if (!metric_dialog_) return;
    const auto history           = view_model_.history(selected_metric_, selected_interface_);
    const auto primary           = view_model_.statistics(selected_metric_, selected_interface_);
    const auto secondary         = view_model_.statistics(selected_metric_, selected_interface_, true);
    const auto update_statistics = [this](const auto &labels, const MeasurementStatistics &statistics) {
        const std::array<std::optional<double>, 4> values{statistics.current, statistics.minimum, statistics.average,
                                                          statistics.maximum};
        for (std::size_t index = 0; index < labels.size(); ++index) {
            if (!labels[index]) continue;
            lv_label_set_text(
                labels[index],
                values[index] ? MonitorViewModel::format_metric_value(selected_metric_, *values[index]).c_str() : "--");
        }
    };
    update_statistics(metric_statistics_, primary);
    update_statistics(metric_secondary_statistics_, secondary);
    const bool has_data = primary.sample_count > 0 || secondary.sample_count > 0;
    if (has_data)
        lv_obj_add_flag(metric_empty_, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(metric_empty_, LV_OBJ_FLAG_HIDDEN);

    double axis_min      = 0;
    double axis_max      = 100;
    const double maximum = std::max(primary.maximum.value_or(0), secondary.maximum.value_or(0));
    if (selected_metric_ == MonitorMetric::network) {
        const double step = std::pow(10.0, std::floor(std::log10(std::max(1.0, maximum))));
        axis_max          = std::max(1.0, std::ceil(maximum / step) * step);
    } else if (selected_metric_ == MonitorMetric::temperature) {
        axis_min = std::min(0.0, std::floor(primary.minimum.value_or(0) / 10.0) * 10.0);
        axis_max = std::max(100.0, std::ceil(maximum / 10.0) * 10.0);
    } else if (selected_metric_ == MonitorMetric::uptime) {
        axis_min = std::floor(primary.minimum.value_or(0) / 60.0) * 60.0;
        axis_max = std::max(axis_min + 60.0, std::ceil(maximum / 60.0) * 60.0);
    }
    lv_label_set_text(metric_axis_max_, MonitorViewModel::format_metric_value(selected_metric_, axis_max).c_str());
    lv_label_set_text(metric_axis_min_, MonitorViewModel::format_metric_value(selected_metric_, axis_min).c_str());

    // Normalize chart coordinates so large byte rates and uptime cannot overflow LVGL's integer axes.
    const auto point_count = static_cast<std::uint32_t>(std::max<std::size_t>(2, history.size()));
    lv_chart_set_point_count(metric_chart_, point_count);
    lv_chart_set_all_values(metric_chart_, metric_series_, LV_CHART_POINT_NONE);
    if (metric_secondary_series_) lv_chart_set_all_values(metric_chart_, metric_secondary_series_, LV_CHART_POINT_NONE);
    const auto start      = history.empty() ? 0 : history.front().timestamp_milliseconds;
    const auto end        = history.empty() ? start : history.back().timestamp_milliseconds;
    const auto elapsed    = end >= start ? end - start : 0;
    const auto plot_value = [axis_min, axis_max](std::optional<double> value) {
        return value ? static_cast<std::int32_t>(
                           std::lround(std::clamp((*value - axis_min) / (axis_max - axis_min), 0.0, 1.0) * 1000))
                     : LV_CHART_POINT_NONE;
    };
    for (std::size_t index = 0; index < history.size(); ++index) {
        const auto &point = history[index];
        const auto offset = point.timestamp_milliseconds >= start ? point.timestamp_milliseconds - start : 0;
        const auto x      = elapsed > 0
                                ? static_cast<std::int32_t>(std::min(1.0, static_cast<double>(offset) / elapsed) * 1000)
                            : history.size() > 1 ? static_cast<std::int32_t>(index * 1000 / (history.size() - 1))
                                                 : 1000;
        lv_chart_set_series_value_by_id2(metric_chart_, metric_series_, index, x, plot_value(point.value));
        if (metric_secondary_series_)
            lv_chart_set_series_value_by_id2(metric_chart_, metric_secondary_series_, index, x,
                                             plot_value(point.secondary_value));
    }
    char duration[48];
    std::snprintf(duration, sizeof(duration), "%llus ago", static_cast<unsigned long long>(elapsed / 1000));
    lv_label_set_text(metric_time_start_, duration);
    lv_chart_refresh(metric_chart_);
}

void MonitorView::layout_metric_dialog() noexcept
{
    if (!metric_dialog_ || !surface_) return;
    lv_obj_set_size(metric_dialog_, std::min(760, std::max(1, lv_obj_get_content_width(surface_) - 32)),
                    std::min(600, std::max(1, lv_obj_get_content_height(surface_) - 32)));
    lv_obj_center(metric_dialog_);
}

void MonitorView::sync_network_cards()
{
    if (!network_cards_) return;
    const auto &interfaces = view_model_.network_interfaces();
    bool rebuild =
        interfaces.size() != network_card_widgets_.size() || (interfaces.empty() && no_network_label_ == nullptr);
    if (!rebuild) {
        for (std::size_t index = 0; index < interfaces.size(); ++index) {
            if (interfaces[index].name != network_card_widgets_[index].interface_name) {
                rebuild = true;
                break;
            }
        }
    }

    if (rebuild) {
        lv_obj_clean(network_cards_);
        network_card_widgets_.clear();
        no_network_label_ = nullptr;
        if (interfaces.empty()) {
            no_network_label_ =
                create_label(network_cards_, "No network interfaces", 14, components::color_secondary_text);
            lv_obj_set_name_static(no_network_label_, "monitor_no_network_interfaces");
        } else {
            network_card_widgets_.reserve(interfaces.size());
            for (const auto &interface : interfaces)
                network_card_widgets_.push_back(create_network_card(network_cards_, interface));
        }
        apply_layout();
    }

    for (std::size_t index = 0; index < interfaces.size(); ++index)
        update_network_card(network_card_widgets_[index], interfaces[index]);
}

void MonitorView::update_network_card(NetworkCardWidgets &widgets, const NetworkInterfaceViewData &interface)
{
    const auto color = lv_color_hex(network_state_color(interface.state));
    lv_obj_set_style_bg_color(widgets.status_dot, color, 0);
    lv_label_set_text(widgets.status, interface.status.c_str());
    lv_obj_set_style_text_color(widgets.status, color, 0);
    lv_label_set_text(widgets.ip_address, interface.ip_address.c_str());
    lv_label_set_text(widgets.receive_rate, interface.receive_rate.c_str());
    lv_label_set_text(widgets.send_rate, interface.send_rate.c_str());
}

void MonitorView::apply_layout() noexcept
{
    layout_metric_dialog();
    if (!surface_ || !body_ || !summary_ || !network_section_ || !network_cards_) return;
    const auto width = lv_obj_get_content_width(surface_);
    if (width <= 0) return;
    const bool wide = width >= 800;

    static const int32_t narrow_columns[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t narrow_rows[]    = {154, 154, LV_GRID_TEMPLATE_LAST};
    static const int32_t wide_columns[]   = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                             LV_GRID_TEMPLATE_LAST};
    static const int32_t wide_rows[]      = {154, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(summary_, wide ? wide_columns : narrow_columns, wide ? wide_rows : narrow_rows);
    lv_obj_set_height(summary_, wide ? 154 : 320);
    for (std::size_t index = 0; index < metric_cards_.size(); ++index) {
        const int column = wide ? static_cast<int>(index) : static_cast<int>(index % 2);
        const int row    = wide ? 0 : static_cast<int>(index / 2);
        lv_obj_set_grid_cell(metric_cards_[index], LV_GRID_ALIGN_STRETCH, column, 1, LV_GRID_ALIGN_STRETCH, row, 1);
    }

    const std::size_t card_count = network_card_widgets_.size();
    const std::size_t columns    = wide ? std::min<std::size_t>(3, std::max<std::size_t>(1, card_count)) : 1;
    const std::size_t rows       = card_count == 0 ? 1 : (card_count + columns - 1) / columns;
    const int cards_height =
        card_count == 0 ? 36
                        : static_cast<int>(rows) * kNetworkCardHeight + static_cast<int>(rows - 1) * kNetworkCardGap;
    lv_obj_set_flex_flow(network_cards_, wide ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(network_cards_, cards_height);
    lv_obj_set_height(network_section_, 28 + 10 + cards_height);
    for (auto &widgets : network_card_widgets_) {
        if (!wide || columns == 1)
            lv_obj_set_width(widgets.card, LV_PCT(100));
        else if (columns == 2)
            lv_obj_set_width(widgets.card, LV_PCT(49));
        else
            lv_obj_set_width(widgets.card, LV_PCT(32));
    }
    if (system_card_) {
        lv_obj_set_width(system_card_, LV_PCT(100));
        lv_obj_set_height(system_card_, 68);
    }
    lv_obj_set_style_pad_all(body_, wide ? 18 : 20, 0);
    lv_obj_set_style_pad_top(body_, 4, 0);
}
