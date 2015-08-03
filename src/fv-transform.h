/*
 * Finvenkisto
 *
 * Copyright (C) 2014 Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FV_TRANSFORM_H
#define FV_TRANSFORM_H

#include "fv-matrix.h"

struct fv_transform {
        struct fv_matrix modelview;
        struct fv_matrix projection;

        /* Derived values */
        float normal_transform[3 * 3];
        struct fv_matrix mvp;

        bool normal_transform_dirty;
        bool mvp_dirty;
};

static inline void
fv_transform_dirty(struct fv_transform *transform)
{
        transform->normal_transform_dirty = true;
        transform->mvp_dirty = true;
}

void
fv_transform_ensure_mvp(struct fv_transform *transform);

void
fv_transform_ensure_normal_transform(struct fv_transform *transform);

#endif /* FV_TRANSFORM_H */
