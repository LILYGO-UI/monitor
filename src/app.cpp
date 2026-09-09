#include "app.hpp"

#include "app_identity.h"
#include "pages/monitor/monitor_view.hpp"

#include <memory>

namespace {

struct AppState {
    std::unique_ptr<MonitorViewModel> monitor_view_model;
    std::unique_ptr<MonitorView> monitor_view;
};

AppState app_state;

void app_open(const cm0_app_context_t *context)
{
    app_state.monitor_view_model = std::make_unique<MonitorViewModel>();
    app_state.monitor_view       = std::make_unique<MonitorView>(*app_state.monitor_view_model);
    app_state.monitor_view->create(context->root, LILYGO_UI_MONITOR_APP_NAME);
}

void app_close()
{
    if (app_state.monitor_view) app_state.monitor_view->destroy();
    app_state.monitor_view.reset();
    app_state.monitor_view_model.reset();
}

const cm0_app_descriptor_t app_descriptor{
    CM0_APP_API_VERSION, sizeof(cm0_app_descriptor_t), LILYGO_UI_MONITOR_APP_ID, app_open, app_close,
};

}  // namespace

extern "C" const cm0_app_descriptor_t *cm0_app_get_descriptor()
{
    return &app_descriptor;
}
