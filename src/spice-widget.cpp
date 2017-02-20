extern "C" 
{
#include "spice-widget.h"
#include "spice-widget-priv.h"
}

#include "spiceqt.h"


G_DEFINE_TYPE(SpiceDisplay, spice_display, SPICE_TYPE_CHANNEL);

//extern SpiceDisplay * global_display;
static void disconnect_main(SpiceDisplay *display);
static void disconnect_display(SpiceDisplay *display);
static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data);
static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data);
static void callbackInvalidate(SpiceDisplayPrivate *d, gint x, gint y, gint w, gint h);
static void callbackSettingsChanged(gint instance, gint width, gint height, gint bpp);
static void sync_keyboard_lock_modifiers(SpiceDisplay *display, guint32 modifiers);

/* ---------------------------------------------------------------- */
static void callbackInvalidate(SpiceDisplayPrivate *d, gint x, gint y, gint w, gint h)
{
    uchar *img = static_cast<uchar*>(d->data);
    SpiceQt::getSpice()->updateImage(img, x, y, w, h);
}

static void callbackSettingsChanged(gint instance, gint width, gint height, gint bpp)
{
    SpiceQt::getSpice()->settingsChanged(width, height, bpp);
}

static void spice_display_dispose(GObject *obj)
{
    SpiceDisplay *display = SPICE_DISPLAY(obj);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    SPICE_DEBUG("spice display dispose");

    disconnect_main(display);
    disconnect_display(display);
    //disconnect_cursor(display);

    //if (d->clipboard) {
    //    g_signal_handlers_disconnect_by_func(d->clipboard, G_CALLBACK(clipboard_owner_change),
    //                                         display);
    //    d->clipboard = NULL;
    //}

    //if (d->clipboard_primary) {
    //    g_signal_handlers_disconnect_by_func(d->clipboard_primary, G_CALLBACK(clipboard_owner_change),
    //                                         display);
    //    d->clipboard_primary = NULL;
    //}

/*
    void *a = static_cast<void*>(channel_new);
    void *b = static_cast<void*>(channel_destroy);
    if (d->session) {
        g_signal_handlers_disconnect_by_func(d->session, G_CALLBACK(a),
                                             display);
        g_signal_handlers_disconnect_by_func(d->session, G_CALLBACK(b),
                                             display);
        g_object_unref(d->session);
        d->session = NULL;
    }
*/
}

static void spice_display_finalize(GObject *obj)
{
    SPICE_DEBUG("Finalize spice display");
    G_OBJECT_CLASS(spice_display_parent_class)->finalize(obj);
}


static void spice_display_class_init(SpiceDisplayClass *klass)
{
    g_type_class_add_private(klass, sizeof(SpiceDisplayPrivate));
}


static void spice_display_init(SpiceDisplay *display)
{
    //global_display = display;
    SpiceDisplayPrivate *d;

    d = display->priv = SPICE_DISPLAY_GET_PRIVATE(display);
    memset(d, 0, sizeof(*d));
    d->have_mitshm = TRUE;
    d->mouse_last_x = -1;
    d->mouse_last_y = -1;
}


gint get_display_id(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    /* supported monitor_id only with display channel #0 */
    if (d->channel_id == 0 && d->monitor_id >= 0)
        return d->monitor_id;

    g_return_val_if_fail(d->monitor_id <= 0, -1);

    return d->channel_id;
}

/* ---------------------------------------------------------------- */

static void update_mouse_mode(SpiceChannel *channel, gpointer data)
{
    SpiceDisplay *display = static_cast<SpiceDisplay*>(data);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (!d)
        return;
    g_object_get(channel, "mouse-mode", &d->mouse_mode, NULL);
}

/* ---------------------------------------------------------------- */

void send_key(SpiceDisplay *display, int scancode, int down)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    uint32_t i, b, m;

    if (!d || !d->inputs)
        return;

    i = scancode / 32;
    b = scancode % 32;
    m = (1 << b);
    g_return_if_fail(i < SPICE_N_ELEMENTS(d->key_state));

    if (down) {
        spice_inputs_key_press(d->inputs, scancode);
        d->key_state[i] |= m;
    } else {
        if (!(d->key_state[i] & m)) {
            return;
        }
        spice_inputs_key_release(d->inputs, scancode);
        d->key_state[i] &= ~m;
    }
}

/* ---------------------------------------------------------------- */

static void primary_create(SpiceChannel *channel, gint format, gint width, gint height, gint stride, gint shmid, gpointer imgdata, gpointer data) {

    SpiceDisplay *display = static_cast<SpiceDisplay*>(data);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    // TODO: For now, don't do anything for secondary monitors
    if (get_display_id(display) > 0) {
        return;
    }

    d->format = static_cast<SpiceSurfaceFmt>(format);
    d->stride = stride;
    d->shmid = shmid;
    d->width = width;
    d->height = height;
    d->data_origin = d->data = imgdata;
    //uiCallbackSettingsChanged (0, width, height, 4);
    callbackSettingsChanged(0, width, height, 4);
}

static void primary_destroy(SpiceChannel *channel, gpointer data) {
    SpiceDisplay *display = SPICE_DISPLAY(data);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (!d)
        return;

    d->format = static_cast<SpiceSurfaceFmt>(0);
    d->width  = 0;
    d->height = 0;
    d->stride = 0;
    d->shmid  = 0;
    d->data   = 0;
    d->data_origin = 0;
}

static void invalidate(SpiceChannel *channel,
                       gint x, gint y, gint w, gint h, gpointer data) {
    SpiceDisplay *display = static_cast<SpiceDisplay*>(data);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (!d)
        return;
    if (x + w > d->width || y + h > d->height) {
        ;
    } else {
        callbackInvalidate(d, x, y, w, h);
    }
}

static void mark(SpiceChannel *channel, gint mark, gpointer data) {
    SpiceDisplay *display = static_cast<SpiceDisplay*>(data);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (!d)
        return;
    d->mark = mark;
    spice_main_set_display_enabled(d->main, d->channel_id, d->mark != 0);
}

static void cursor_invalidate(SpiceDisplay *display)
{
}

static void cursor_move(SpiceCursorChannel *channel, gint x, gint y, gpointer data)
{
}

static void cursor_reset(SpiceCursorChannel *channel, gpointer data)
{
    SpiceQt::getSpice()->showCursor(true);
}

static void cursor_set(SpiceCursorChannel *channel,
                       gint width, gint height, gint hot_x, gint hot_y,
                       gpointer rgba, gpointer data)
{
    SpiceQt::getSpice()->showCursor(true);
}

static void cursor_hide(SpiceCursorChannel *channel, gpointer data)
{
    SpiceQt::getSpice()->showCursor(false);
}

static void disconnect_main(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (d->main == NULL)
        return;
    d->main = NULL;
}

static void disconnect_display(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (d->display == NULL)
        return;
    d->display = NULL;
}

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    SpiceDisplay *display = static_cast<SpiceDisplay*>(data);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (!d)
        return;

    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        d->main = SPICE_MAIN_CHANNEL(channel);
        g_signal_connect(channel, "main-mouse-update",
                                      G_CALLBACK(update_mouse_mode), display);
        update_mouse_mode(channel, display);
        return;
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id != d->channel_id)
            return;
        d->display = channel;
        g_signal_connect(channel, "display-primary-create",
                         G_CALLBACK(primary_create), display);
        g_signal_connect(channel, "display-primary-destroy",
                         G_CALLBACK(primary_destroy), display);
        g_signal_connect(channel, "display-invalidate",
                         G_CALLBACK(invalidate), display);
        g_signal_connect(channel, "display-mark",
                         G_CALLBACK(mark), display);
        spice_channel_connect(channel);
        return;
    }

    if (SPICE_IS_CURSOR_CHANNEL(channel)) {
        if (id != d->channel_id)
            return;
        d->cursor = SPICE_CURSOR_CHANNEL(channel);
        g_signal_connect(channel, "cursor-set",
                         G_CALLBACK(cursor_set), display);
        g_signal_connect(channel, "cursor-move",
                         G_CALLBACK(cursor_move), display);
        g_signal_connect(channel, "cursor-hide",
                         G_CALLBACK(cursor_hide), display);
        g_signal_connect(channel, "cursor-reset",
                         G_CALLBACK(cursor_reset), display);
        spice_channel_connect(channel);
        return;
    }

    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
        d->inputs = SPICE_INPUTS_CHANNEL(channel);
        if (d->disable_inputs)
            return;
        spice_channel_connect(channel);
        guint32 modifiers;
        modifiers = SpiceQt::getSpice()->getKeyboardLockModifiers();
        sync_keyboard_lock_modifiers(display, modifiers);
        return;
    }

#ifdef USE_SMARTCARD
    if (SPICE_IS_SMARTCARD_CHANNEL(channel)) {
        d->smartcard = SPICE_SMARTCARD_CHANNEL(channel);
        spice_channel_connect(channel);
        return;
    }
#endif

    return;
}

static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    SpiceDisplay *display = static_cast<SpiceDisplay*>(data);
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    SPICE_DEBUG("channel_destroy %d", id);

    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        disconnect_main(display);
        return;
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id != d->channel_id)
            return;
        disconnect_display(display);
        return;
    }

    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
        d->inputs = NULL;
        return;
    }

#ifdef USE_SMARTCARD
    if (SPICE_IS_SMARTCARD_CHANNEL(channel)) {
        d->smartcard = NULL;
        return;
    }
#endif

    return;
}

/**
 * spice_display_new:
 * @session: a #SpiceSession
 * @id: the display channel ID to associate with #SpiceDisplay
 *
 * Returns: a new #SpiceDisplay widget.
 **/
SpiceDisplay *spice_display_new(SpiceSession *session, int id)
{
    SpiceDisplay *display;
    SpiceDisplayPrivate *d;
    GList *list;
    GList *it;

    display = static_cast<SpiceDisplay*>(g_object_new(SPICE_TYPE_DISPLAY, NULL));
    d = SPICE_DISPLAY_GET_PRIVATE(display);
    d->session = static_cast<SpiceSession*>(g_object_ref(session));
    d->channel_id = id;
    SPICE_DEBUG("channel_id:%d",d->channel_id);

    g_signal_connect(session, "channel-new",
                     G_CALLBACK(channel_new), display);
    g_signal_connect(session, "channel-destroy",
                     G_CALLBACK(channel_destroy), display);
    list = spice_session_get_channels(session);
    for (it = g_list_first(list); it != NULL; it = g_list_next(it)) {
        channel_new(session, static_cast<SpiceChannel*>(it->data), (gpointer*)display);
    }
    g_list_free(list);

    return display;
}

static void sync_keyboard_lock_modifiers(SpiceDisplay *display, guint32 modifiers)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (!d)
        return;

    if (d->disable_inputs)
        return;

    if (d->inputs)
        spice_inputs_set_key_locks(d->inputs, modifiers);
}
