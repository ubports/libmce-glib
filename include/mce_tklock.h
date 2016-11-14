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

#ifndef MCE_TKLOCK_H
#define MCE_TKLOCK_H

#include "mce_types.h"

G_BEGIN_DECLS

typedef enum mce_tklock_mode {
    MCE_TKLOCK_MODE_LOCKED,
    MCE_TKLOCK_MODE_SILENT_LOCKED,
    MCE_TKLOCK_MODE_LOCKED_DIM,
    MCE_TKLOCK_MODE_LOCKED_DELAY,
    MCE_TKLOCK_MODE_SILENT_LOCKED_DIM,
    MCE_TKLOCK_MODE_UNLOCKED,
    MCE_TKLOCK_MODE_SILENT_UNLOCKED
} MCE_TKLOCK_MODE;

typedef struct mce_tklock_priv MceTklockPriv;

typedef struct mce_tklock {
    GObject object;
    MceTklockPriv* priv;
    gboolean valid;
    MCE_TKLOCK_MODE mode;
    gboolean locked;
} MceTklock;

typedef void
(*MceTklockFunc)(
    MceTklock* tklock,
    void* arg);

MceTklock*
mce_tklock_new(
    void);

MceTklock*
mce_tklock_ref(
    MceTklock* tklock);

void
mce_tklock_unref(
    MceTklock* tklock);

gulong
mce_tklock_add_valid_changed_handler(
    MceTklock* tklock,
    MceTklockFunc fn,
    void* arg);

gulong
mce_tklock_add_mode_changed_handler(
    MceTklock* tklock,
    MceTklockFunc fn,
    void* arg);

gulong
mce_tklock_add_locked_changed_handler(
    MceTklock* tklock,
    MceTklockFunc fn,
    void* arg);

void
mce_tklock_remove_handler(
    MceTklock* tklock,
    gulong id);

void
mce_tklock_remove_handlers(
    MceTklock* tklock,
    gulong *ids,
    guint count);

G_END_DECLS

#endif /* MCE_TKLOCK_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
