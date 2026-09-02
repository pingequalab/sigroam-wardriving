/*
 * SigRoam — FAP entry.
 *
 * App struct + ViewDispatcher + SceneManager + Start/About + sr_io / sr_worker.
 */

#include "sigroam.h"

#include "src/sr_parse_marauder.h"

#include <string.h>

/* Log Device lives in an RTC backup register, not on disk (V-043).
 * Default USART = Pin 13/14, the same pair Scout Lite uses. */
const char* sigroam_log_device_name(FuriHalRtcLogDevice d) {
    switch(d) {
    case FuriHalRtcLogDeviceUsart:
        return "Usart(pin13/14) <-- CONFLICTS WITH SCOUT LITE";
    case FuriHalRtcLogDeviceLpuart:
        return "Lpuart(pin15/16)";
    case FuriHalRtcLogDeviceReserved:
        return "Reserved";
    case FuriHalRtcLogDeviceNone:
        return "None(serial logging off)";
    default:
        return "unknown";
    }
}

uint32_t sigroam_log_baud_value(FuriHalRtcLogBaudRate b) {
    switch(b) {
    case FuriHalRtcLogBaudRate230400:
        return 230400;
    case FuriHalRtcLogBaudRate9600:
        return 9600;
    case FuriHalRtcLogBaudRate38400:
        return 38400;
    case FuriHalRtcLogBaudRate57600:
        return 57600;
    case FuriHalRtcLogBaudRate115200:
        return 115200;
    case FuriHalRtcLogBaudRate460800:
        return 460800;
    case FuriHalRtcLogBaudRate921600:
        return 921600;
    case FuriHalRtcLogBaudRate1843200:
        return 1843200;
    default:
        return 0;
    }
}

bool sigroam_log_device_conflicts(void) {
    return furi_hal_rtc_get_log_device() == FuriHalRtcLogDeviceUsart;
}

const SrSourceCodec* sigroam_codec(const SigRoamApp* app) {
    if(app == NULL) {
        return NULL;
    }
    if(app->settings.source == SrSourceMarauder || app->settings.source == SrSourceUnknown) {
        return &sr_codec_marauder;
    }
    return NULL;
}

/*
 * NOTE: every return value must be a **single line** of <= SR_VIEW_COLS(20) characters.
 * Reason (defect D1, confirmed by on-screen observation during B stage on 2026-08-19): when
 * serial_open == false the Dash tab draws this hint first and then four statistics rows; the tab
 * bar takes 11 px leaving 53 px, so with a one-line hint the statistics land at y=30/40/50/60
 * and all fit on screen, but a two-line hint pushes them to 40/50/60/70 and the last baseline of
 * 70 > SR_CANVAS_H(64) runs off screen.
 * Change the sr_view_dash_draw_dash layout in views/sr_view_dash.c before adding multi-line copy.
 */
const char* sigroam_io_status_hint(SrIoStatus st) {
    switch(st) {
    case SrIoErrNoOtg:
        return "5V OTG is off";
    case SrIoErrLogDevice:
        return "Log Dev uses 13/14"; /* Single line: a two-line hint pushes the Dash tab's fourth row off screen (D1) */
    case SrIoErrPortBusy:
        return "Serial port busy";
    case SrIoErrBadBaud:
        return "Baud not supported";
    case SrIoErrAlloc:
        return "Out of memory";
    case SrIoErrState:
        return "Bad io state";
    case SrIoOk:
    default:
        return "Serial not open";
    }
}

static void sigroam_log_device_diag(void) {
    FuriHalRtcLogDevice dev = furi_hal_rtc_get_log_device();
    FuriHalRtcLogBaudRate baud = furi_hal_rtc_get_log_baud_rate();
    FURI_LOG_I(
        SR_TAG,
        "log_device=%d (%s)  log_baud=%lu  log_level=%d",
        (int)dev,
        sigroam_log_device_name(dev),
        (unsigned long)sigroam_log_baud_value(baud),
        (int)furi_hal_rtc_get_log_level());
}

/* Custom / tick: zero-wait. Fail → drop this frame. Never FuriWaitForever. */
static bool sigroam_custom_event_callback(void* context, uint32_t event) {
    SigRoamApp* app = context;
    if(furi_mutex_acquire(app->mtx, 0) != FuriStatusOk) {
        return false;
    }
    bool consumed = scene_manager_handle_custom_event(app->scene_manager, event);
    furi_mutex_release(app->mtx);
    return consumed;
}

static void sigroam_tick_event_callback(void* context) {
    SigRoamApp* app = context;
    SrIoStats st;
    SrWorkerStats ws;
    bool log_io = false;

    if(furi_mutex_acquire(app->mtx, 0) != FuriStatusOk) {
        return;
    }
    app->tick_n++;
    memset(&st, 0, sizeof(st));
    memset(&ws, 0, sizeof(ws));
    if((app->tick_n % 10u) == 0u && app->io) {
        sr_io_get_stats(app->io, &st);
        if(app->worker) {
            sr_worker_get_stats(app->worker, &ws);
        }

        log_io = true;
    }
    if(scene_manager_get_current_scene(app->scene_manager) == SigRoamSceneDash) {
        sigroam_dash_refresh(app);
    }
    scene_manager_handle_tick_event(app->scene_manager);
    furi_mutex_release(app->mtx);

    if(log_io) {
        FURI_LOG_I(
            SR_TAG,
            "io: rx=%lu drop=%lu err=%lu mask=0x%lx tx=%lu stack_min=%lu lines=%lu fill=%lu",
            (unsigned long)st.rx_bytes,
            (unsigned long)st.rx_dropped,
            (unsigned long)st.rx_errors,
            (unsigned long)st.rx_event_mask,
            (unsigned long)st.tx_bytes,
            (unsigned long)ws.stack_min,
            (unsigned long)ws.lines_ready,
            (unsigned long)st.rx_max_fill);
    }
}

/* Navigation / previous: lock-free, no side effects, return is determined. */
static bool sigroam_navigation_event_callback(void* context) {
    SigRoamApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static SigRoamApp* sigroam_app_alloc(void) {
    SigRoamApp* app = malloc(sizeof(SigRoamApp));
    furi_check(app);
    memset(app, 0, sizeof(SigRoamApp));

    /* T4.10: the Drive scene was deleted, so the one-time initialization of the scan ctx moved here.
     * NOTE: the constant SR_SCAN_CTL_TIMEOUT_MS = 2000 is unchanged (src/sr_scan_ctl.h:10) --
     * this relocates **where initialization happens**, it does not change the timeout
     * (plan section 6, constraint 1).
     * Do not move this into scene_dash_on_enter: the Dash page is entered and left repeatedly, and
     * resetting each time would lose in-flight command state. */
    app->scan.timeout_ms = SR_SCAN_CTL_TIMEOUT_MS;

    app->mtx = furi_mutex_alloc(FuriMutexTypeNormal);
    furi_check(app->mtx);

    app->gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&sigroam_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, sigroam_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, sigroam_navigation_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, sigroam_tick_event_callback, SR_TICK_PERIOD_MS);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SigRoamViewSubmenu, submenu_get_view(app->submenu));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SigRoamViewWidget, widget_get_view(app->widget));

    app->text_box = text_box_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SigRoamViewTextBox, text_box_get_view(app->text_box));

    app->dash = sr_view_dash_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SigRoamViewDash, sr_view_dash_get_view(app->dash));

    app->var_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SigRoamViewVarList, variable_item_list_get_view(app->var_list));

    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    /* Blocking IO is allowed during startup. scene on_enter must never take this path. */
    {
        SrSettingsLoadStatus st = sr_settings_store_load(&app->settings, NULL);
        FURI_LOG_I(
            SR_TAG,
            "settings: %s baud=%lu source=%d sound=%d vibro=%d bl=%d stealth=%d",
            sr_settings_store_status_str(st),
            (unsigned long)app->settings.baud,
            (int)app->settings.source,
            app->settings.sound ? 1 : 0,
            app->settings.vibro ? 1 : 0,
            app->settings.backlight_always ? 1 : 0,
            app->settings.stealth ? 1 : 0);
        if(st != SrSettingsLoadOk) {
            if(sr_settings_store_save(&app->settings)) {
                FURI_LOG_I(
                    SR_TAG, "settings: rewrote (%s)", sr_settings_store_status_str(st));
            } else {
                FURI_LOG_E(SR_TAG, "settings: rewrite FAILED");
            }
        }
    }

    /* Open the serial port during startup. Failure is not fatal; the app keeps running. */
    app->io = sr_io_alloc();
    if(app->io) {
        app->io_status = sr_io_open(app->io, app->settings.baud);
    } else {
        app->io_status = SrIoErrAlloc;
    }
    FURI_LOG_I(
        SR_TAG,
        "io: open %s baud=%lu",
        sr_io_status_str(app->io_status),
        (unsigned long)app->settings.baud);

    app->bloom = malloc(sizeof(SrBloom));
    sr_rawlog_init(&app->rawlog);
    if(app->bloom == NULL) {
        FURI_LOG_E(SR_TAG, "bloom: alloc failed");
        sr_model_init(&app->model, NULL, &app->rawlog);
    } else {
        sr_bloom_init(app->bloom);
        sr_model_init(&app->model, app->bloom, &app->rawlog);
        if(sr_io_is_open(app->io)) {
            app->worker = sr_worker_alloc(app->io, &app->model, app->mtx);
            if(app->worker == NULL) {
                FURI_LOG_E(SR_TAG, "worker: alloc failed");
            } else if(!sr_worker_start(app->worker)) {
                FURI_LOG_E(SR_TAG, "worker: start failed");
            }
        }
    }

    return app;
}

static void sigroam_app_free(SigRoamApp* app) {
    if(app->worker) {
        sr_worker_request_stop(app->worker);
    }
    if(app->io) {
        sr_io_rx_stop(app->io);
    }
    if(app->worker) {
        sr_worker_join(app->worker);
    }

    if(app->io) {
        SrIoStats st;
        SrWorkerStats ws;

        sr_io_get_stats(app->io, &st);
        if(app->worker) {
            sr_worker_get_stats(app->worker, &ws);
            FURI_LOG_I(
                SR_TAG,
                "io: final rx=%lu drop=%lu err=%lu mask=0x%lx opens=%lu closes=%lu "
                "stack_min=%lu lines_ready=%lu lines_truncated=%lu lines_applied=%lu",
                (unsigned long)st.rx_bytes,
                (unsigned long)st.rx_dropped,
                (unsigned long)st.rx_errors,
                (unsigned long)st.rx_event_mask,
                (unsigned long)st.opens,
                (unsigned long)st.closes,
                (unsigned long)ws.stack_min,
                (unsigned long)ws.lines_ready,
                (unsigned long)ws.lines_truncated,
                (unsigned long)ws.lines_applied);
        } else {
            FURI_LOG_I(
                SR_TAG,
                "io: final rx=%lu drop=%lu err=%lu mask=0x%lx opens=%lu closes=%lu worker=none",
                (unsigned long)st.rx_bytes,
                (unsigned long)st.rx_dropped,
                (unsigned long)st.rx_errors,
                (unsigned long)st.rx_event_mask,
                (unsigned long)st.opens,
                (unsigned long)st.closes);
        }
        sr_io_close(app->io);
        /* The second line must come after close: closes is incremented only at the end of
         * sr_io_close, so the closes in the final stats line above is always 0. The B2 criterion
         * opens == closes reads this line. */
        sr_io_get_stats(app->io, &st);
        FURI_LOG_I(
            SR_TAG,
            "io: post-close opens=%lu closes=%lu",
            (unsigned long)st.opens,
            (unsigned long)st.closes);
        sr_io_free(app->io);
        app->io = NULL;
    }

    sr_worker_free(app->worker);
    app->worker = NULL;
    free(app->bloom);
    app->bloom = NULL;

    view_dispatcher_remove_view(app->view_dispatcher, SigRoamViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, SigRoamViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, SigRoamViewTextBox);
    view_dispatcher_remove_view(app->view_dispatcher, SigRoamViewDash);
    view_dispatcher_remove_view(app->view_dispatcher, SigRoamViewVarList);

    submenu_free(app->submenu);
    widget_free(app->widget);
    text_box_free(app->text_box);
    sr_view_dash_free(app->dash);
    app->dash = NULL;
    variable_item_list_free(app->var_list);
    app->var_list = NULL;

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    furi_mutex_free(app->mtx);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t sigroam_app(void* p) {
    UNUSED(p);

    FURI_LOG_I(SR_TAG, "T1.1 skeleton: entry");
    sigroam_log_device_diag();

    SigRoamApp* app = sigroam_app_alloc();
    FURI_LOG_I(
        SR_TAG,
        "sizeof(SrDashModel)=%u sizeof(SigRoamApp)=%u",
        (unsigned)sizeof(SrDashModel),
        (unsigned)sizeof(SigRoamApp));
    scene_manager_next_scene(app->scene_manager, SigRoamSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    FURI_LOG_I(SR_TAG, "T1.1 skeleton: exiting cleanly");
    sigroam_app_free(app);
    return 0;
}
