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

#include "mce_tklock.h"
#include "mce_proxy.h"
#include "mce_log_p.h"

#include <mce/dbus-names.h>
#include <mce/mode-names.h>

#include <gutil_misc.h>

/* Generated headers */
#include "com.nokia.mce.request.h"
#include "com.nokia.mce.signal.h"

struct mce_tklock_priv {
    MceProxy* proxy;
    gulong proxy_valid_id;
    gulong tklock_mode_ind_id;
};

enum mce_tklock_signal {
    SIGNAL_VALID_CHANGED,
    SIGNAL_MODE_CHANGED,
    SIGNAL_LOCKED_CHANGED,
    SIGNAL_COUNT
};

#define SIGNAL_VALID_CHANGED_NAME  "mce-tklock-valid-changed"
#define SIGNAL_MODE_CHANGED_NAME   "mce-tklock-mode-changed"
#define SIGNAL_LOCKED_CHANGED_NAME "mce-tklock-locked-changed"

static guint mce_tklock_signals[SIGNAL_COUNT] = { 0 };

typedef GObjectClass MceTklockClass;
G_DEFINE_TYPE(MceTklock, mce_tklock, G_TYPE_OBJECT)
#define PARENT_CLASS mce_tklock_parent_class
#define MCE_TKLOCK_TYPE (mce_tklock_get_type())
#define MCE_TKLOCK(obj) (G_TYPE_CHECK_INSTANCE_CAST(obj,\
        MCE_TKLOCK_TYPE,MceTklock))

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
mce_tklock_mode_update(
    MceTklock* self,
    const char* mode)
{
    MceTklockPriv* priv = self->priv;
    const MCE_TKLOCK_MODE prev_mode = self->mode;
    const gboolean prev_locked = self->locked;
    static const struct mce_tklock_mode_desc {
        const char* name;
        MCE_TKLOCK_MODE mode;
        gboolean locked;
    } mce_tklock_modes [] = {
        /* Most commonly used modes first */
        { MCE_TK_LOCKED, MCE_TKLOCK_MODE_LOCKED, TRUE },
        { MCE_TK_UNLOCKED, MCE_TKLOCK_MODE_UNLOCKED, FALSE },
        { MCE_TK_SILENT_LOCKED, MCE_TKLOCK_MODE_SILENT_LOCKED, TRUE },
        { MCE_TK_LOCKED_DIM, MCE_TKLOCK_MODE_LOCKED_DIM, TRUE },
        { MCE_TK_LOCKED_DELAY, MCE_TKLOCK_MODE_LOCKED_DELAY, TRUE },
        { MCE_TK_SILENT_LOCKED_DIM, MCE_TKLOCK_MODE_SILENT_LOCKED_DIM, TRUE },
        { MCE_TK_SILENT_UNLOCKED, MCE_TKLOCK_MODE_SILENT_UNLOCKED, FALSE }
    };
    int i;

    for (i=0; i<G_N_ELEMENTS(mce_tklock_modes); i++) {
        if (!g_strcmp0(mode, mce_tklock_modes[i].name)) {
            self->mode = mce_tklock_modes[i].mode;
            self->locked = mce_tklock_modes[i].locked;
            break;
        }
    }
    if (i == G_N_ELEMENTS(mce_tklock_modes)) {
        GWARN("Unexpected mode '%s'", mode);
    }
    if (self->mode != prev_mode) {
        g_signal_emit(self, mce_tklock_signals[SIGNAL_MODE_CHANGED], 0);
    }
    if (self->locked != prev_locked) {
        g_signal_emit(self, mce_tklock_signals[SIGNAL_LOCKED_CHANGED], 0);
    }
    if (priv->proxy->valid && !self->valid) {
        self->valid = TRUE;
        g_signal_emit(self, mce_tklock_signals[SIGNAL_VALID_CHANGED], 0);
    }
}

static
void
mce_tklock_mode_query_done(
    GObject* proxy,
    GAsyncResult* result,
    gpointer arg)
{
    GError* error = NULL;
    char* status = NULL;
    MceTklock* self = MCE_TKLOCK(arg);

    if (com_nokia_mce_request_call_get_tklock_mode_finish(
        COM_NOKIA_MCE_REQUEST(proxy), &status, result, &error)) {
        GDEBUG("Mode is currently %s", status);
        mce_tklock_mode_update(self, status);
        g_free(status);
    } else {
        /*
         * We could retry but it's probably not worth the trouble
         * because the next time user locks/unlocks the screen we
         * receive mce_tklock_mode_ind and sync our state with mce.
         * Until then, this object stays invalid.
         */
        GWARN("Failed to query tklock mode %s", GERRMSG(error));
        g_error_free(error);
    }
    mce_tklock_unref(self);
}

static
void
mce_tklock_mode_ind(
    ComNokiaMceSignal* proxy,
    const char* mode,
    gpointer arg)
{
    GDEBUG("Mode is %s", mode);
    mce_tklock_mode_update(MCE_TKLOCK(arg), mode);
}

static
void
mce_tklock_mode_query(
    MceTklock* self)
{
    MceTklockPriv* priv = self->priv;
    MceProxy* proxy = priv->proxy;

    /*
     * proxy->signal and proxy->request may not be available at the
     * time when MceTklock is created. In that case we have to wait
     * for the valid signal before we can connect the tklock mode
     * signal and submit the initial query.
     */
    if (proxy->signal && priv->proxy->signal) {
        priv->tklock_mode_ind_id = g_signal_connect(proxy->signal,
            MCE_TKLOCK_MODE_SIG, G_CALLBACK(mce_tklock_mode_ind), self);
    }
    if (proxy->request && proxy->valid) {
        com_nokia_mce_request_call_get_tklock_mode(proxy->request, NULL,
            mce_tklock_mode_query_done, mce_tklock_ref(self));
    }
}

static
void
mce_tklock_valid_changed(
    MceProxy* proxy,
    void* arg)
{
    MceTklock* self = MCE_TKLOCK(arg);

    if (proxy->valid) {
        mce_tklock_mode_query(self);
    } else {
        if (self->valid) {
            self->valid = FALSE;
            g_signal_emit(self, mce_tklock_signals[SIGNAL_VALID_CHANGED], 0);
        }
    }
}

/*==========================================================================*
 * API
 *==========================================================================*/

MceTklock*
mce_tklock_new()
{
    /* MCE assumes one tklock */
    static MceTklock* mce_tklock_instance = NULL;

    if (mce_tklock_instance) {
        mce_tklock_ref(mce_tklock_instance);
    } else {
        mce_tklock_instance = g_object_new(MCE_TKLOCK_TYPE, NULL);
        mce_tklock_mode_query(mce_tklock_instance);
        g_object_add_weak_pointer(G_OBJECT(mce_tklock_instance),
            (gpointer*)(&mce_tklock_instance));
    }
    return mce_tklock_instance;
}

MceTklock*
mce_tklock_ref(
    MceTklock* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(MCE_TKLOCK(self));
    }
    return self;
}

void
mce_tklock_unref(
    MceTklock* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(MCE_TKLOCK(self));
    }
}

gulong
mce_tklock_add_valid_changed_handler(
    MceTklock* self,
    MceTklockFunc fn,
    void* arg)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_VALID_CHANGED_NAME, G_CALLBACK(fn), arg) : 0;
}

gulong
mce_tklock_add_mode_changed_handler(
    MceTklock* self,
    MceTklockFunc fn,
    void* arg)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_MODE_CHANGED_NAME, G_CALLBACK(fn), arg) : 0;
}

gulong
mce_tklock_add_locked_changed_handler(
    MceTklock* self,
    MceTklockFunc fn,
    void* arg)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_LOCKED_CHANGED_NAME, G_CALLBACK(fn), arg) : 0;
}

void
mce_tklock_remove_handler(
    MceTklock* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
mce_tklock_remove_handlers(
    MceTklock* self,
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
mce_tklock_init(
    MceTklock* self)
{
    MceTklockPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self, MCE_TKLOCK_TYPE,
        MceTklockPriv);

    self->priv = priv;
    self->mode = MCE_TKLOCK_MODE_LOCKED;
    self->locked = TRUE;
    priv->proxy = mce_proxy_new();
    priv->proxy_valid_id = mce_proxy_add_valid_changed_handler(priv->proxy,
        mce_tklock_valid_changed, self);
}

static
void
mce_tklock_finalize(
    GObject* object)
{
    MceTklock* self = MCE_TKLOCK(object);
    MceTklockPriv* priv = self->priv;

    if (priv->tklock_mode_ind_id) {
        g_signal_handler_disconnect(priv->proxy->signal,
            priv->tklock_mode_ind_id);
    }
    mce_proxy_remove_handler(priv->proxy, priv->proxy_valid_id);
    mce_proxy_unref(priv->proxy);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
mce_tklock_class_init(
    MceTklockClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = mce_tklock_finalize;
    g_type_class_add_private(klass, sizeof(MceTklockPriv));
    mce_tklock_signals[SIGNAL_VALID_CHANGED] =
        g_signal_new(SIGNAL_VALID_CHANGED_NAME,
            G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST,
            0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    mce_tklock_signals[SIGNAL_MODE_CHANGED] =
        g_signal_new(SIGNAL_MODE_CHANGED_NAME,
            G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST,
            0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    mce_tklock_signals[SIGNAL_LOCKED_CHANGED] =
        g_signal_new(SIGNAL_LOCKED_CHANGED_NAME,
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
