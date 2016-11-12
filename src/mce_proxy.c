/*
 * Copyright (C) 2016 Jolla Ltd.
 * Contact: Slava Monich <slava.monich@jolla.com>
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the name of Jolla Ltd nor the names of its contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "mce_proxy.h"
#include "mce_log_p.h"

#include "mce/dbus-names.h"

/* Generated headers */
#include "com.nokia.mce.request.h"
#include "com.nokia.mce.signal.h"

GLOG_MODULE_DEFINE("mce");

struct mce_proxy_priv {
    GDBusConnection* bus;
    guint mce_watch_id;
};

enum mce_proxy_signal {
    SIGNAL_VALID_CHANGED,
    SIGNAL_COUNT
};

#define SIGNAL_VALID_CHANGED_NAME   "mce-proxy-valid-changed"

static guint mce_proxy_signals[SIGNAL_COUNT] = { 0 };

typedef GObjectClass MceProxyClass;
G_DEFINE_TYPE(MceProxy, mce_proxy, G_TYPE_OBJECT)
#define PARENT_CLASS mce_proxy_parent_class
#define MCE_PROXY_TYPE (mce_proxy_get_type())
#define MCE_PROXY(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj,\
        MCE_PROXY_TYPE,MceProxy))

static
void
mce_name_appeared(
    GDBusConnection* bus,
    const gchar* name,
    const gchar* owner,
    gpointer arg)
{
    MceProxy* self = MCE_PROXY(arg);
    GDEBUG("Name '%s' is owned by %s", name, owner);
    GASSERT(!self->valid);
    self->valid = TRUE;
    g_signal_emit(self, mce_proxy_signals[SIGNAL_VALID_CHANGED], 0);
}

static
void
mce_name_vanished(
    GDBusConnection* bus,
    const gchar* name,
    gpointer arg)
{
    MceProxy* self = MCE_PROXY(arg);
    GDEBUG("Name '%s' has disappeared", name);
    if (self->valid) {
        self->valid = FALSE;
        g_signal_emit(self, mce_proxy_signals[SIGNAL_VALID_CHANGED], 0);
    }
}

MceProxy*
mce_proxy_new()
{
    /*
     * Since there's only one mce in the system, there's no need for
     * more than one proxy object.
     */
    static MceProxy* mce_proxy_instance = NULL;
    if (mce_proxy_instance) {
        mce_proxy_ref(mce_proxy_instance);
    } else {
        mce_proxy_instance = g_object_new(MCE_PROXY_TYPE, NULL);
        g_object_add_weak_pointer(G_OBJECT(mce_proxy_instance),
            (gpointer*)(&mce_proxy_instance));
    }
    return mce_proxy_instance;
}

MceProxy*
mce_proxy_ref(
    MceProxy* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(MCE_PROXY(self));
    }
    return self;
}

void
mce_proxy_unref(
    MceProxy* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(MCE_PROXY(self));
    }
}

gulong
mce_proxy_add_valid_changed_handler(
    MceProxy* self,
    MceProxyFunc fn,
    void* arg)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_VALID_CHANGED_NAME, G_CALLBACK(fn), arg) : 0;
}

void
mce_proxy_remove_handler(
    MceProxy* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

static
void
mce_proxy_init(
    MceProxy* self)
{
    MceProxyPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, MCE_PROXY_TYPE,
        MceProxyPriv);
    self->priv = priv;
    priv->bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    self->signal = com_nokia_mce_signal_proxy_new_sync(priv->bus,
        G_DBUS_PROXY_FLAGS_NONE, MCE_SERVICE, MCE_SIGNAL_PATH, NULL, NULL);
    self->request = com_nokia_mce_request_proxy_new_sync(priv->bus,
        G_DBUS_PROXY_FLAGS_NONE, MCE_SERVICE, MCE_REQUEST_PATH, NULL, NULL);
    priv->mce_watch_id = g_bus_watch_name_on_connection(priv->bus,
        MCE_SERVICE, G_BUS_NAME_WATCHER_FLAGS_NONE,
        mce_name_appeared, mce_name_vanished, self, NULL);
}

static
void
mce_proxy_finalize(
    GObject* object)
{
    MceProxy* self = MCE_PROXY(object);
    MceProxyPriv* priv = self->priv;
    g_bus_unwatch_name(priv->mce_watch_id);
    g_object_unref(self->signal);
    g_object_unref(self->request);
    g_object_unref(priv->bus);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
mce_proxy_class_init(
    MceProxyClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = mce_proxy_finalize;
    g_type_class_add_private(klass, sizeof(MceProxyPriv));
    mce_proxy_signals[SIGNAL_VALID_CHANGED] =
        g_signal_new(SIGNAL_VALID_CHANGED_NAME,
            G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST,
            0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
