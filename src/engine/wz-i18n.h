/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WZ_I18N_H
#define WZ_I18N_H

#ifdef WZ_ENABLE_NLS
#include <libintl.h>
#define _(s) gettext(s)
#else
#define _(s) (s)
#endif
#define N_(s) (s)

#endif
