#ifndef LILYGO_UI_MONITOR_COMPONENTS_HPP
#define LILYGO_UI_MONITOR_COMPONENTS_HPP

#include <lvgl.h>

#include <cstdint>

namespace components {

inline constexpr std::uint32_t color_page_background      = 0xf2f2f7;
inline constexpr std::uint32_t color_container_background = 0xffffff;
inline constexpr std::uint32_t color_container_title      = 0x1a1a1a;
inline constexpr std::uint32_t color_text                 = 0x000000;
inline constexpr std::uint32_t color_secondary_text       = 0x666d75;
inline constexpr std::uint32_t color_border               = 0xd8dde3;
inline constexpr std::uint32_t color_action               = 0x20262d;
inline constexpr std::uint32_t color_progress_track       = 0xe9edf1;
inline constexpr std::uint32_t color_cpu                  = 0x1677ff;
inline constexpr std::uint32_t color_memory               = 0x159570;
inline constexpr std::uint32_t color_storage              = 0xd98218;
inline constexpr std::uint32_t color_temperature          = 0xd94d4d;
inline constexpr std::uint32_t color_success              = 0x159570;

lv_obj_t *create_page(lv_obj_t *parent);
lv_obj_t *create_header(lv_obj_t *parent, const char *title);
lv_obj_t *create_card(lv_obj_t *parent);
lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback, void *user_data);

}  // namespace components

#endif
