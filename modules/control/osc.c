/*****************************************************************************
 * osc.c: OSC/UDP control interface for VLC 3.0.x
 *
 * Prototype: OSC 1.0 messages over UDP.
 * Addresses:
 *   /vlc/play       no args
 *   /vlc/pause      no args
 *   /vlc/stop       no args
 *   /vlc/next       no args
 *   /vlc/previous   no args
 *   /vlc/volume     int32 (0..256)
 *   /vlc/seek       float32 (normalized 0.0..1.0)
 *
 * This is an initial source patch, not yet compiled against VLC 3.0.24.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define VLC_MODULE_LICENSE VLC_LICENSE_GPL_2_PLUS
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_interface.h>
#include <vlc_input.h>
#include <vlc_playlist.h>
#include <vlc_aout.h>
#include <vlc_network.h>
#include <vlc_interrupt.h>

#ifdef HAVE_POLL
# include <poll.h>
#endif

#define OSC_DEFAULT_PORT 8000
#define OSC_MAX_PACKET 2048

static int Open(vlc_object_t *);
static void Close(vlc_object_t *);
static void *Run(void *);

#define PORT_TEXT N_("OSC UDP port")
#define PORT_LONGTEXT N_("UDP port on which this interface receives OSC messages.")

vlc_module_begin()
    set_shortname(N_("OSC"))
    set_description(N_("OSC remote control interface"))
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_MAIN)
    add_integer("osc-port", OSC_DEFAULT_PORT, PORT_TEXT, PORT_LONGTEXT, true)
    set_capability("interface", 0)
    set_callbacks(Open, Close)
    add_shortcut("osc")
vlc_module_end()

typedef struct
{
    int fd;
    vlc_thread_t thread;
    playlist_t *playlist;
} intf_sys_t;

/* Read an OSC string including its NUL terminator and 4-byte padding. */
static bool ReadString(const uint8_t *buf, size_t len, size_t *offset,
                       const char **out)
{
    size_t start = *offset;
    size_t end = start;
    if (start >= len)
        return false;
    while (end < len && buf[end] != '\0')
        ++end;
    if (end == len)
        return false;
    size_t next = (end + 4) & ~(size_t)3;
    if (next > len)
        return false;
    *out = (const char *)(buf + start);
    *offset = next;
    return true;
}

static uint32_t ReadBE32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static input_thread_t *CurrentInput(playlist_t *playlist)
{
    return playlist_CurrentInput(playlist); /* caller releases the reference */
}

static void Dispatch(intf_thread_t *intf, const uint8_t *buf, size_t len)
{
    intf_sys_t *sys = intf->p_sys;
    size_t offset = 0;
    const char *address, *types;

    if (!ReadString(buf, len, &offset, &address) ||
        !ReadString(buf, len, &offset, &types))
    {
        msg_Warn(intf, "OSC: malformed OSC string/header");
        return;
    }

    if (!strcmp(address, "/vlc/play") && !strcmp(types, ","))
    {
        playlist_Control(sys->playlist, PLAYLIST_PLAY, pl_Unlocked);
    }
    else if (!strcmp(address, "/vlc/pause") && !strcmp(types, ","))
    {
        playlist_TogglePause(sys->playlist);
    }
    else if (!strcmp(address, "/vlc/stop") && !strcmp(types, ","))
    {
        playlist_Control(sys->playlist, PLAYLIST_STOP, pl_Unlocked);
    }
    else if (!strcmp(address, "/vlc/next") && !strcmp(types, ","))
    {
        playlist_Control(sys->playlist, PLAYLIST_SKIP, pl_Unlocked, 1);
    }
    else if (!strcmp(address, "/vlc/previous") && !strcmp(types, ","))
    {
        playlist_Control(sys->playlist, PLAYLIST_SKIP, pl_Unlocked, -1);
    }
    else if (!strcmp(address, "/vlc/volume") && !strcmp(types, ",i") && offset + 4 <= len)
    {
        int32_t volume = (int32_t)ReadBE32(buf + offset);
        if (volume < 0) volume = 0;
        if (volume > 256) volume = 256;
        playlist_VolumeSet(sys->playlist, volume / (float)AOUT_VOLUME_DEFAULT);
        playlist_MuteSet(sys->playlist, volume == 0);
    }
    else if (!strcmp(address, "/vlc/seek") && !strcmp(types, ",f") && offset + 4 <= len)
    {
        uint32_t bits = ReadBE32(buf + offset);
        float position;
        /* ReadBE32 reconstructs the IEEE-754 bit pattern as a host integer;
           copying that integer's memory representation into float is correct
           on both little- and big-endian hosts. */
        memcpy(&position, &bits, sizeof(position));
        if (isfinite(position))
        {
            if (position < 0.f) position = 0.f;
            if (position > 1.f) position = 1.f;
            input_thread_t *input = CurrentInput(sys->playlist);
            if (input != NULL)
            {
                var_SetFloat(input, "position", position);
                vlc_object_release(input);
            }
        }
    }
    else
        msg_Dbg(intf, "OSC: ignored unsupported/malformed message '%s'", address);
}

static void *Run(void *data)
{
    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;
    uint8_t packet[OSC_MAX_PACKET];

    vlc_thread_set_name("vlc-osc");
    for (;;)
    {
        struct pollfd pfd = { .fd = sys->fd, .events = POLLIN };
        int ret = vlc_poll_i11e(&pfd, 1, 250);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            msg_Warn(intf, "OSC: UDP poll failed: %s", vlc_strerror_c(errno));
            continue;
        }
        if (ret == 0)
        {
            vlc_testcancel();
            continue;
        }
        if (!(pfd.revents & POLLIN))
            continue;
        ssize_t n = recv(sys->fd, packet, sizeof(packet), 0);
        if (n > 0)
            Dispatch(intf, packet, (size_t)n);
    }
    return NULL;
}

static int Open(vlc_object_t *object)
{
    intf_thread_t *intf = (intf_thread_t *)object;
    int port = var_InheritInteger(intf, "osc-port");
    if (port < 1 || port > 65535)
    {
        msg_Err(intf, "OSC: invalid UDP port %d", port);
        return VLC_EGENERIC;
    }

    int fd = net_ListenUDP1(object, NULL, port);
    if (fd < 0)
    {
        msg_Err(intf, "OSC: cannot listen on UDP port %d", port);
        return VLC_EGENERIC;
    }

    intf_sys_t *sys = calloc(1, sizeof(*sys));
    if (sys == NULL)
    {
        net_Close(fd);
        return VLC_ENOMEM;
    }
    sys->fd = fd;
    sys->playlist = pl_Get(intf);
    intf->p_sys = sys;

    if (vlc_clone(&sys->thread, Run, intf, VLC_THREAD_PRIORITY_LOW))
    {
        net_Close(fd);
        free(sys);
        intf->p_sys = NULL;
        return VLC_EGENERIC;
    }
    msg_Info(intf, "OSC control listening on UDP port %d", port);
    return VLC_SUCCESS;
}

static void Close(vlc_object_t *object)
{
    intf_thread_t *intf = (intf_thread_t *)object;
    intf_sys_t *sys = intf->p_sys;
    if (sys == NULL)
        return;
    vlc_cancel(sys->thread);
    vlc_join(sys->thread, NULL);
    net_Close(sys->fd);
    free(sys);
}
