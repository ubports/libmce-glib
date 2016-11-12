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

#include "mce_display.h"
#include "mce_proxy.h"
#include "mce_log_p.h"

#include <mce/dbus-names.h>
#include <mce/mode-names.h>

#include <gutil_misc.h>

/* Generated headers */
#include "com.nokia.mce.request.h"
#include "com.nokia.mce.signal.h"

struct mce_display_priv {
    MceProxy* proxy;
    gulong proxy_valid_id;
    gulong display_status_ind_id;
};

enum mce_display_signal {
    SIGNAL_VALID_CHANGED,
    SIGNAL_STATE_CHANGED,
    SIGNAL_COUNT
};

#define SIGNAL_VALID_CHANGED_NAME   "mce-display-valid-changed"
#define SIGNAL_STATE_CHANGED_NAME   "mce-display-state-changed"

static guint mce_display_signals[SIGNAL_COUNT] = { 0 };

typedef GObjectClass MceDisplayClass;
G_DEFINE_TYPE(MceDisplay, mce_display, G_TYPE_OBJECT)
#define PARENT_CLASS mce_display_parent_class
#define MCE_DISPLAY_TYPE (mce_display_get_type())
#define MCE_DISPLAY(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj,\
        MCE_DISPLAY_TYPE,MceDisplay))

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
mce_display_status_update(
    MceDisplay* self,
    const char* status)
{
    MCE_DISPLAY_STATE state;
    MceDisplayPriv* priv = self->priv;

    if (!g_strcmp0(status, MCE_DISPLAY_OFF_STRING)) {
        state = MCE_DISPLAY_STATE_OFF;
    } else if (!g_strcmp0(status, MCE_DISPLAY_DIM_STRING)) {
        state = MCE_DISPLAY_STATE_DIM;
    } else {
        GASSERT(!g_strcmp0(status, MCE_DISPLAY_ON_STRING));
        state = MCE_DISPLAY_STATE_ON;
    }
    if (self->state != state) {
        self->state = state;
        g_signal_emit(self, mce_display_signals[SIGNAL_STATE_CHANGED], 0);
    }
    if (priv->proxy->valid && !self->valid) {
        self->valid = TRUE;
        g_signal_emit(self, mce_display_signals[SIGNAL_VALID_CHANGED], 0);
    }
}

static
void
mce_display_status_query_done(
    GObject* proxy,
    GAsyncResult* result,
    gpointer arg)
{
    GError* error = NULL;
    char* status = NULL;
    MceDisplay* self = MCE_DISPLAY(arg);

    if (com_nokia_mce_request_call_get_display_status_finish(
        COM_NOKIA_MCE_REQUEST(proxy), &status, result, &error)) {
        GDEBUG("Display is currently %s", status);
        mce_display_status_update(self, status);
        g_free(status);
    } else {
        /*
         * We could retry but it's probably not worth the trouble
         * because the next time display state changes we receive
         * display_status_ind signal and sync our state with mce.
         * Until then, this object stays invalid.
         */
        GWARN("Failed to query display state %s", GERRMSG(error));
        g_error_free(error);
    }
    mce_display_unref(self);
}

static
void
mce_display_status_query(
    MceDisplay* self)
{
    MceProxy* proxy = self->priv->proxy;

    if (proxy->valid) {
        com_nokia_mce_request_call_get_display_status(proxy->request, NULL,
            mce_display_status_query_done, mce_display_ref(self));
    }
}

static
void
mce_display_valid_changed(
    MceProxy* proxy,
    void* arg)
{
    MceDisplay* self = MCE_DISPLAY(arg);

    if (proxy->valid) {
        mce_display_status_query(self);
    } else {
        if (self->valid) {
            self->valid = FALSE;
            g_signal_emit(self, mce_display_signals[SIGNAL_VALID_CHANGED], 0);
        }
    }
}

static
void
mce_display_status_ind(
    ComNokiaMceSignal* proxy,
    const char* status,
    gpointer arg)
{
    GDEBUG("Display is %s", status);
    mce_display_status_update(MCE_DISPLAY(arg), status);
}

/*==========================================================================*
 * API
 *==========================================================================*/

MceDisplay*
mce_display_new()
{
    /* MCE assumes one display */
    static MceDisplay* mce_display_instance = NULL;

    if (mce_display_instance) {
        mce_display_ref(mce_display_instance);
    } else {
        mce_display_instance = g_object_new(MCE_DISPLAY_TYPE, NULL);
        mce_display_status_query(mce_display_instance);
        g_object_add_weak_pointer(G_OBJECT(mce_display_instance),
            (gpointer*)(&mce_display_instance));
    }
    return mce_display_instance;
}

MceDisplay*
mce_display_ref(
    MceDisplay* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(MCE_DISPLAY(self));
    }
    return self;
}

void
mce_display_unref(
    MceDisplay* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(MCE_DISPLAY(self));
    }
}

gulong
mce_display_add_valid_changed_handler(
    MceDisplay* self,
    MceDisplayFunc fn,
    void* arg)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_VALID_CHANGED_NAME, G_CALLBACK(fn), arg) : 0;
}

gulong
mce_display_add_state_changed_handler(
    MceDisplay* self,
    MceDisplayFunc fn,
    void* arg)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_STATE_CHANGED_NAME, G_CALLBACK(fn), arg) : 0;
}

void
mce_display_remove_handler(
    MceDisplay* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
mce_display_remove_handlers(
    MceDisplay* self,
    gulong *ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
mce_display_init(
    MceDisplay* self)
{
    MceDisplayPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, MCE_DISPLAY_TYPE,
        MceDisplayPriv);

    self->priv = priv;
    priv->proxy = mce_proxy_new();
    priv->proxy_valid_id = mce_proxy_add_valid_changed_handler(priv->proxy,
        mce_display_valid_changed, self);
    priv->display_status_ind_id = g_signal_connect(priv->proxy->signal,
        MCE_DISPLAY_SIG, G_CALLBACK(mce_display_status_ind), self);
}

static
void
mce_display_finalize(
    GObject* object)
{
    MceDisplay* self = MCE_DISPLAY(object);
    MceDisplayPriv* priv = self->priv;

    g_signal_handler_disconnect(priv->proxy->signal,
        priv->display_status_ind_id);
    mce_proxy_unref(priv->proxy);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
mce_display_class_init(
    MceDisplayClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = mce_display_finalize;
    g_type_class_add_private(klass, sizeof(MceDisplayPriv));
    mce_display_signals[SIGNAL_VALID_CHANGED] =
        g_signal_new(SIGNAL_VALID_CHANGED_NAME,
            G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST,
            0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    mce_display_signals[SIGNAL_STATE_CHANGED] =
        g_signal_new(SIGNAL_STATE_CHANGED_NAME,
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
