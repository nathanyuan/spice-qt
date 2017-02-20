#include <QDebug>
#include <QPalette>
#include <QX11Info>
#include "spiceqt.h"
#include "spice-widget.h"
#include "spice-widget-priv.h"


#define SPICE_MAIN_CHANNEL_GET_PRIVATE(obj)                             \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_MAIN_CHANNEL, SpiceMainChannelPrivate))
 
static void main_channel_event(SpiceChannel *channel, SpiceChannelEvent event, gpointer data)
{	
    switch (event) {
    case SPICE_CHANNEL_OPENED:
        printf("main channel: connected\n");
        break;
    case SPICE_CHANNEL_CLOSED:
        printf("main channel: connection lost\n");
        SpiceQt::getSpice()->clearImage();
        break;
    case SPICE_CHANNEL_ERROR_CONNECT:
        printf("main channel: failed to connect\n");
        break;
    }
}

static void main_agent_update(SpiceChannel *channel, gpointer data)
{
    bool agent_connected;
    gboolean ac;
    g_object_get(channel, "agent-connected", &ac, NULL);
    agent_connected = ac ? true : false;
    SpiceQt::getSpice()->setAgentConnected(agent_connected);
}

static void inputs_modifiers(SpiceChannel *channel, gpointer data)
{
    int m;

    g_object_get(channel, "key-modifiers", &m, NULL);
    SpiceQt::getSpice()->setKbdModifiers(m);
}

static void channel_new(SpiceSession *session, SpiceChannel *channel)
{
    int id;
    g_object_get(channel, "channel-id", &id, NULL);
    if (SPICE_IS_MAIN_CHANNEL(channel)) {
       g_signal_connect(channel, "channel-event",
                         G_CALLBACK(main_channel_event), 0);
       g_signal_connect(channel, "main-agent-update",
                         G_CALLBACK(main_agent_update), channel);
    }
    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        qDebug() << "display channel new";
        SpiceDisplay *display = SpiceQt::getSpice()->spiceDisplayGLib();
        if (display)
            return;
        display = spice_display_new(session, id);    
        SpiceQt::getSpice()->setSpiceDisplayGLib(display);
    }
    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
        g_signal_connect(channel, "inputs-modifiers",
                         G_CALLBACK(inputs_modifiers), 0);

    }
    if (SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        SpiceAudio *audio = SpiceQt::getSpice()->spiceAudioGLib();
        if (audio)
            return;
        audio = spice_audio_new(session, NULL, NULL);
        SpiceQt::getSpice()->setSpiceAudioGLib(audio);
    }
}

static void channel_destroy(SpiceSession *session, SpiceChannel *channel)
{
}

SpiceQt * SpiceQt::instance = NULL;
QMap<int, int> * SpiceQt::keymap = NULL;

SpiceQt::SpiceQt(QWidget *parent)
    : QWidget(parent), agentConnected(false)
{
    display = NULL;
    SGsession = NULL;
    audio = NULL;
    QPalette palette = this->palette();
    palette.setBrush(QPalette::Window,QBrush(Qt::black));
    setPalette(palette);
    setAutoFillBackground(true);
    setMouseTracking(true);
    grabMouse();
}

SpiceQt * SpiceQt::getSpice(QWidget *parent)
{
    if (instance)
        return instance;
    instance = new SpiceQt(parent);
    return instance;
}

void SpiceQt::clearImage()
{
    img = QImage(dataWidth, dataHeight, QImage::Format_RGB32);
    if (!img.isNull()) {
        QPainter painter(&img);
        painter.setBrush(QBrush(Qt::black));
        painter.drawRect(0, 0, dataWidth, dataHeight);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, tr("Connection lost, Please reconnect."));
        painter.end(); 
    }
}

void SpiceQt::setSpiceAudioGLib(SpiceAudio *sa)
{ 
    audio = sa;
}

void SpiceQt::setSpiceDisplayGLib(SpiceDisplay *sd)
{ 
    display = sd;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
}
void SpiceQt::disconnectFromGuest()
{
    img = QImage(dataWidth, dataHeight, QImage::Format_RGB32);
    if (!img.isNull()) {
        QPainter painter(&img);
        painter.setBrush(QBrush(Qt::black));
        painter.drawRect(0, 0, dataWidth, dataHeight);
        painter.end(); 
    }
    if (SGsession) {
        spice_session_disconnect(SGsession);
        display = NULL;
        SGsession = NULL;
        audio = NULL;
    }
}
void SpiceQt::connectToGuest(const QString &host, const QString &port)
{
    SGsession = spice_session_new();
    g_object_set(SGsession, "host", \
                 host.toLatin1().constData(), NULL);
    g_object_set(SGsession, "port", \
                 port.toLatin1().constData(), NULL);
    g_signal_connect(SGsession, "channel-new",
                                 G_CALLBACK(channel_new), 0);
    g_signal_connect(SGsession, "channel-destroy", G_CALLBACK(channel_destroy), 0);
    if (!spice_session_connect(SGsession)) {
    }
}

quint32 SpiceQt::getKeyboardLockModifiers()
{
    XKeyboardState keyboard_state;
    Display *x_display = QX11Info::display();
    quint32 modifiers = 0;

    XGetKeyboardControl(x_display, &keyboard_state);

    if (keyboard_state.led_mask & 0x01)
        modifiers |= SPICE_INPUTS_CAPS_LOCK;
    if (keyboard_state.led_mask & 0x02)
        modifiers |= SPICE_INPUTS_NUM_LOCK;
    if (keyboard_state.led_mask & 0x04)
        modifiers |= SPICE_INPUTS_SCROLL_LOCK;

    return modifiers;
}

void SpiceQt::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    if (!img.isNull())
        p.drawImage(0, 0, img);
}

void SpiceQt::spiceResize(int w, int h)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (!d)
        return;
    spice_main_set_display(d->main, d->channel_id, 0, 0, w, h);
}

void SpiceQt::settingsChanged(int w, int h, int bpp)
{
    dataWidth = w;
    dataHeight = h;
    rate = double(height()) / double(dataHeight);
    img = QImage(w, h, QImage::Format_RGB32);
    if (w != width() || h != height())
        setFixedSize(w, h);
    emit imageSize(w, h);
}

void SpiceQt::updateImage(uchar *data, int x, int y, int w, int h)
{
    uint *source = reinterpret_cast<uint*>(data);
    for (int i = y; i < y + h; i++)
        for (int j = x; j < x + w; j++)
            img.setPixel(j, i, source[dataWidth * i + j]);
    update(x, y, w, h);
}

void SpiceQt::setKbdModifiers(int m)
{
    scrollLock = m & SPICE_KEYBOARD_MODIFIER_FLAGS_SCROLL_LOCK;
    capsLock   = m & SPICE_KEYBOARD_MODIFIER_FLAGS_CAPS_LOCK;
    numLock    = m & SPICE_KEYBOARD_MODIFIER_FLAGS_NUM_LOCK;
}

void SpiceQt::mouseMoveEvent(QMouseEvent *event)
{
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (!d)
        return;
    if (!d->inputs)
        return;
    if (d->disable_inputs)
        return;

    int button_mask = 0;
    if (event->buttons() & Qt::LeftButton)
        button_mask |= SPICE_MOUSE_BUTTON_MASK_LEFT;
    if (event->buttons() & Qt::MidButton)
        button_mask |= SPICE_MOUSE_BUTTON_MASK_MIDDLE;
    if (event->buttons() & Qt::RightButton)
        button_mask |= SPICE_MOUSE_BUTTON_MASK_RIGHT;

    int x, y;
    x = event->x() - d->mx;
    y = event->y() - d->my;

    switch (d->mouse_mode) {
    case SPICE_MOUSE_MODE_CLIENT:
        if (x >= 0 && x < d->width &&
            y >= 0 && y < d->height) {
            spice_inputs_position(d->inputs,
                                  x, y,
                                  d->channel_id,
                                  button_mask);
        }
        break;
    case SPICE_MOUSE_MODE_SERVER:
        break;
    }
}

void SpiceQt::wheelEvent(QWheelEvent *event)
{
    int button;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (!d->inputs)
        return;
    if (d->disable_inputs)
        return;
    int delta = event->delta();
    if (delta > 0)
        button = SPICE_MOUSE_BUTTON_UP;
    else if (delta < 0)
        button = SPICE_MOUSE_BUTTON_DOWN;
    else
        return;

    int button_mask = 0;
    if (event->buttons() & Qt::LeftButton)
        button_mask |= SPICE_MOUSE_BUTTON_MASK_LEFT;
    if (event->buttons() & Qt::MidButton)
        button_mask |= SPICE_MOUSE_BUTTON_MASK_MIDDLE;
    if (event->buttons() & Qt::RightButton)
        button_mask |= SPICE_MOUSE_BUTTON_MASK_RIGHT;

    spice_inputs_button_press(d->inputs, button,
                              button_mask);
    spice_inputs_button_release(d->inputs, button,
                                button_mask);
}

void SpiceQt::mousePressEvent(QMouseEvent *event)
{
    if (!display)
        return;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (!d)
        return;

    if (d->disable_inputs)
        return;

    if (d->mouse_mode == SPICE_MOUSE_MODE_SERVER)
        ;
    else
        ;

    if (!d->inputs)
        return;

    int button = 0;
    if (event->button() == Qt::LeftButton)
        button = SPICE_MOUSE_BUTTON_LEFT;
    if (event->button() == Qt::RightButton)
        button = SPICE_MOUSE_BUTTON_RIGHT;
    if (event->button() == Qt::MidButton)
        button = SPICE_MOUSE_BUTTON_MIDDLE;
    if (event->button() == Qt::XButton1)
        button = SPICE_MOUSE_BUTTON_UP;
    if (event->button() == Qt::XButton2)
        button = SPICE_MOUSE_BUTTON_DOWN;

    int button_mask = 0;
    if (event->buttons() & Qt::LeftButton)
        button_mask |= SPICE_MOUSE_BUTTON_MASK_LEFT;
    if (event->buttons() & Qt::MidButton)
        button_mask |= SPICE_MOUSE_BUTTON_MASK_MIDDLE;
    if (event->buttons() & Qt::RightButton)
        button_mask |= SPICE_MOUSE_BUTTON_MASK_RIGHT;

    spice_inputs_button_press(d->inputs,
                              button,
                              button_mask);
}

void SpiceQt::mouseReleaseEvent(QMouseEvent *event)
{
    if (!display)
        return;
    SpiceDisplayPrivate *d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (!d)
        return;

    if (d->disable_inputs)
        return;

    if (d->mouse_mode == SPICE_MOUSE_MODE_CLIENT) {
        int x, y;

        /* rule out clicks in outside region */
        x = event->x() - d->mx;
        y = event->y() - d->my;
        if (!(x >= 0 && x < d->width && y >= 0 && y < d->height))
            return;
    }

    if (d->mouse_mode == SPICE_MOUSE_MODE_SERVER)
        ;
    else
        ;

    if (!d->inputs)
        return;

    int button = 0;
    if (event->button() == Qt::LeftButton)
        button = SPICE_MOUSE_BUTTON_LEFT;
    if (event->button() == Qt::RightButton)
        button = SPICE_MOUSE_BUTTON_RIGHT;
    if (event->button() == Qt::MidButton)
        button = SPICE_MOUSE_BUTTON_MIDDLE;
    if (event->button() == Qt::XButton1)
        button = SPICE_MOUSE_BUTTON_UP;
    if (event->button() == Qt::XButton2)
        button = SPICE_MOUSE_BUTTON_DOWN;

    int button_mask = 0;
    if (event->buttons() & Qt::LeftButton)
        button_mask |= SPICE_MOUSE_BUTTON_MASK_LEFT;
    if (event->buttons() & Qt::MidButton)
        button_mask |= SPICE_MOUSE_BUTTON_MASK_MIDDLE;
    if (event->buttons() & Qt::RightButton)
        button_mask |= SPICE_MOUSE_BUTTON_MASK_RIGHT;

    spice_inputs_button_release(d->inputs,
                              button,
                              button_mask);
}

bool SpiceQt::x11Event(XEvent *event)
{
    return false;
}

void SpiceQt::enterEvent(QEvent *event)
{
    XGrabKeyboard(QX11Info::display(), DefaultRootWindow(QX11Info::display()), true, 
                  GrabModeAsync, GrabModeAsync, CurrentTime);
}

void SpiceQt::leaveEvent(QEvent *event)
{
    XUngrabKeyboard(QX11Info::display(), CurrentTime);
}

void SpiceQt::keyPressEvent(QKeyEvent *event)
{
        QMap<int, int> *map = SpiceQt::getKeymap();
        send_key(display, map->value(event->nativeScanCode()), 1);
}

void SpiceQt::keyReleaseEvent(QKeyEvent *event)
{
    QMap<int, int> *map = SpiceQt::getKeymap();
    send_key(display, map->value(event->nativeScanCode()), 0);
}

void SpiceQt::prepareMouseData()
{
}

QMap<int, int>* SpiceQt::getKeymap()
{
    if (keymap)
        return keymap;
    keymap = new QMap<int, int>;
    keymap->insert(0x9, 0x1);
    keymap->insert(0xa, 0x2);
    keymap->insert(0xb, 0x3);
    keymap->insert(0xc, 0x4);
    keymap->insert(0xd, 0x5);
    keymap->insert(0xe, 0x6);
    keymap->insert(0xf, 0x7);
    keymap->insert(0x10, 0x8);
    keymap->insert(0x11, 0x9);
    keymap->insert(0x12, 0xa);
    keymap->insert(0x13, 0xb);
    keymap->insert(0x14, 0xc);
    keymap->insert(0x15, 0xd);
    keymap->insert(0x16, 0xe);
    keymap->insert(0x17, 0xf);
    keymap->insert(0x18, 0x10);
    keymap->insert(0x19, 0x11);
    keymap->insert(0x1a, 0x12);
    keymap->insert(0x1b, 0x13);
    keymap->insert(0x1c, 0x14);
    keymap->insert(0x1d, 0x15);
    keymap->insert(0x1e, 0x16);
    keymap->insert(0x1f, 0x17);
    keymap->insert(0x20, 0x18);
    keymap->insert(0x21, 0x19);
    keymap->insert(0x22, 0x1a);
    keymap->insert(0x23, 0x1b);
    keymap->insert(0x24, 0x1c);
    keymap->insert(0x25, 0x1d);
    keymap->insert(0x26, 0x1e);
    keymap->insert(0x27, 0x1f);
    keymap->insert(0x28, 0x20);
    keymap->insert(0x29, 0x21);
    keymap->insert(0x2a, 0x22);
    keymap->insert(0x2b, 0x23);
    keymap->insert(0x2c, 0x24);
    keymap->insert(0x2d, 0x25);
    keymap->insert(0x2e, 0x26);
    keymap->insert(0x2f, 0x27);
    keymap->insert(0x30, 0x28);
    keymap->insert(0x31, 0x29);
    keymap->insert(0x32, 0x2a);
    keymap->insert(0x33, 0x2b);
    keymap->insert(0x34, 0x2c);
    keymap->insert(0x35, 0x2d);
    keymap->insert(0x36, 0x2e);
    keymap->insert(0x37, 0x2f);
    keymap->insert(0x38, 0x30);
    keymap->insert(0x39, 0x31);
    keymap->insert(0x3a, 0x32);
    keymap->insert(0x3b, 0x33);
    keymap->insert(0x3c, 0x34);
    keymap->insert(0x3d, 0x35);
    keymap->insert(0x3e, 0x36);
    keymap->insert(0x3f, 0x37);
    keymap->insert(0x40, 0x38);
    keymap->insert(0x41, 0x39);
    keymap->insert(0x42, 0x3a);
    keymap->insert(0x43, 0x3b);
    keymap->insert(0x44, 0x3c);
    keymap->insert(0x45, 0x3d);
    keymap->insert(0x46, 0x3e);
    keymap->insert(0x47, 0x3f);
    keymap->insert(0x48, 0x40);
    keymap->insert(0x49, 0x41);
    keymap->insert(0x4a, 0x42);
    keymap->insert(0x4b, 0x43);
    keymap->insert(0x4c, 0x44);
    keymap->insert(0x4d, 0x45);
    keymap->insert(0x4e, 0x46);
    keymap->insert(0x4f, 0x47);
    keymap->insert(0x50, 0x48);
    keymap->insert(0x51, 0x49);
    keymap->insert(0x52, 0x4a);
    keymap->insert(0x53, 0x4b);
    keymap->insert(0x54, 0x4c);
    keymap->insert(0x55, 0x4d);
    keymap->insert(0x56, 0x4e);
    keymap->insert(0x57, 0x4f);
    keymap->insert(0x58, 0x50);
    keymap->insert(0x59, 0x51);
    keymap->insert(0x5a, 0x52);
    keymap->insert(0x5b, 0x53);
    keymap->insert(0x5c, 0x54);
    keymap->insert(0x5d, 0x76);
    keymap->insert(0x5e, 0x56);
    keymap->insert(0x5f, 0x57);
    keymap->insert(0x60, 0x58);
    keymap->insert(0x61, 0x73);
    keymap->insert(0x62, 0x78);
    keymap->insert(0x63, 0x77);
    keymap->insert(0x64, 0x79);
    keymap->insert(0x65, 0x70);
    keymap->insert(0x66, 0x7b);
    keymap->insert(0x67, 0x5c);
    keymap->insert(0x68, 0x11c);
    keymap->insert(0x69, 0x11d);
    keymap->insert(0x6a, 0x135);
    keymap->insert(0x6b, 0x54);
    keymap->insert(0x6c, 0x138);
    keymap->insert(0x6d, 0x5b);
    keymap->insert(0x6e, 0x147);
    keymap->insert(0x6f, 0x148);
    keymap->insert(0x70, 0x149);
    keymap->insert(0x71, 0x14b);
    keymap->insert(0x72, 0x14d);
    keymap->insert(0x73, 0x14f);
    keymap->insert(0x74, 0x150);
    keymap->insert(0x75, 0x151);
    keymap->insert(0x76, 0x152);
    keymap->insert(0x77, 0x153);
    keymap->insert(0x78, 0x16f);
    keymap->insert(0x79, 0x120);
    keymap->insert(0x7a, 0x12e);
    keymap->insert(0x7b, 0x130);
    keymap->insert(0x7c, 0x15e);
    keymap->insert(0x7d, 0x59);
    keymap->insert(0x7e, 0x14e);
    keymap->insert(0x7f, 0x146);
    keymap->insert(0x80, 0x10b);
    keymap->insert(0x81, 0x7e);
    keymap->insert(0x83, 0x10d);
    keymap->insert(0x84, 0x7d);
    keymap->insert(0x85, 0x15b);
    keymap->insert(0x86, 0x15c);
    keymap->insert(0x87, 0x15d);
    keymap->insert(0x88, 0x168);
    keymap->insert(0x89, 0x105);
    keymap->insert(0x8a, 0x106);
    keymap->insert(0x8b, 0x107);
    keymap->insert(0x8c, 0x10c);
    keymap->insert(0x8d, 0x178);
    keymap->insert(0x8e, 0x64);
    keymap->insert(0x8f, 0x65);
    keymap->insert(0x90, 0x141);
    keymap->insert(0x91, 0x13c);
    keymap->insert(0x92, 0x175);
    keymap->insert(0x93, 0x11e);
    keymap->insert(0x94, 0x121);
    keymap->insert(0x95, 0x66);
    keymap->insert(0x96, 0x15f);
    keymap->insert(0x97, 0x163);
    keymap->insert(0x98, 0x67);
    keymap->insert(0x99, 0x68);
    keymap->insert(0x9a, 0x69);
    keymap->insert(0x9b, 0x113);
    keymap->insert(0x9c, 0x11f);
    keymap->insert(0x9d, 0x117);
    keymap->insert(0x9e, 0x102);
    keymap->insert(0x9f, 0x6a);
    keymap->insert(0xa0, 0x112);
    keymap->insert(0xa1, 0x6b);
    keymap->insert(0xa2, 0x126);
    keymap->insert(0xa3, 0x16c);
    keymap->insert(0xa4, 0x166);
    keymap->insert(0xa5, 0x16b);
    keymap->insert(0xa6, 0x16a);
    keymap->insert(0xa7, 0x169);
    keymap->insert(0xa8, 0x123);
    keymap->insert(0xa9, 0x6c);
    keymap->insert(0xaa, 0x17d);
    keymap->insert(0xab, 0x119);
    keymap->insert(0xac, 0x122);
    keymap->insert(0xad, 0x110);
    keymap->insert(0xae, 0x124);
    keymap->insert(0xaf, 0x131);
    keymap->insert(0xb0, 0x118);
    keymap->insert(0xb1, 0x63);
    keymap->insert(0xb2, 0x70);
    keymap->insert(0xb3, 0x101);
    keymap->insert(0xb4, 0x132);
    keymap->insert(0xb5, 0x167);
    keymap->insert(0xb6, 0x71);
    keymap->insert(0xb7, 0x72);
    keymap->insert(0xb8, 0x108);
    keymap->insert(0xb9, 0x75);
    keymap->insert(0xba, 0x10f);
    keymap->insert(0xbb, 0x176);
    keymap->insert(0xbc, 0x17b);
    keymap->insert(0xbd, 0x109);
    keymap->insert(0xbe, 0x10a);
    keymap->insert(0xbf, 0x5d);
    keymap->insert(0xc0, 0x5e);
    keymap->insert(0xc1, 0x5f);
    keymap->insert(0xc2, 0x55);
    keymap->insert(0xc3, 0x103);
    keymap->insert(0xc4, 0x177);
    keymap->insert(0xc5, 0x104);
    keymap->insert(0xc6, 0x5a);
    keymap->insert(0xc7, 0x74);
    keymap->insert(0xc8, 0x179);
    keymap->insert(0xc9, 0x6d);
    keymap->insert(0xca, 0x6f);
    keymap->insert(0xcb, 0x115);
    keymap->insert(0xcc, 0x116);
    keymap->insert(0xcd, 0x11a);
    keymap->insert(0xce, 0x11b);
    keymap->insert(0xcf, 0x127);
    keymap->insert(0xd0, 0x128);
    keymap->insert(0xd1, 0x129);
    keymap->insert(0xd2, 0x12b);
    keymap->insert(0xd3, 0x12c);
    keymap->insert(0xd4, 0x12d);
    keymap->insert(0xd5, 0x125);
    keymap->insert(0xd6, 0x12f);
    keymap->insert(0xd7, 0x133);
    keymap->insert(0xd8, 0x134);
    keymap->insert(0xd9, 0x136);
    keymap->insert(0xda, 0x139);
    keymap->insert(0xdb, 0x13a);
    keymap->insert(0xdc, 0x13b);
    keymap->insert(0xdd, 0x13d);
    keymap->insert(0xde, 0x13e);
    keymap->insert(0xdf, 0x13f);
    keymap->insert(0xe0, 0x140);
    keymap->insert(0xe1, 0x165);
    keymap->insert(0xe2, 0x142);
    keymap->insert(0xe3, 0x143);
    keymap->insert(0xe4, 0x144);
    keymap->insert(0xe5, 0x145);
    keymap->insert(0xe6, 0x114);
    keymap->insert(0xe7, 0x14a);
    keymap->insert(0xe8, 0x14c);
    keymap->insert(0xe9, 0x154);
    keymap->insert(0xea, 0x16d);
    keymap->insert(0xeb, 0x156);
    keymap->insert(0xec, 0x157);
    keymap->insert(0xed, 0x158);
    keymap->insert(0xee, 0x159);
    keymap->insert(0xef, 0x15a);
    keymap->insert(0xf0, 0x164);
    keymap->insert(0xf1, 0x10e);
    keymap->insert(0xf2, 0x155);
    keymap->insert(0xf3, 0x170);
    keymap->insert(0xf4, 0x171);
    keymap->insert(0xf5, 0x172);
    keymap->insert(0xf6, 0x173);
    keymap->insert(0xf7, 0x174);
    return keymap;
}

