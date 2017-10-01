/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014 Neil Roberts
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

#include "config.h"

#include "fv-transform.h"

void
fv_transform_ensure_mvp(struct fv_transform *transform)
{
        if (!transform->mvp_dirty)
                return;

        /* Calculate the combined modelview-projection matrix */
        fv_matrix_multiply(&transform->mvp,
                           &transform->projection,
                           &transform->modelview);

        transform->mvp_dirty = false;
}

void
fv_transform_ensure_normal_transform(struct fv_transform *transform)
{
        struct fv_matrix inverse_modelview;

        if (!transform->normal_transform_dirty)
                return;

        /* Invert the matrix */
        fv_matrix_get_inverse(&transform->modelview, &inverse_modelview);

        /* Transpose it while converting it to 3x3 */
        transform->normal_transform[0] = inverse_modelview.xx;
        transform->normal_transform[1] = inverse_modelview.xy;
        transform->normal_transform[2] = inverse_modelview.xz;

        transform->normal_transform[3] = inverse_modelview.yx;
        transform->normal_transform[4] = inverse_modelview.yy;
        transform->normal_transform[5] = inverse_modelview.yz;

        transform->normal_transform[6] = inverse_modelview.zx;
        transform->normal_transform[7] = inverse_modelview.zy;
        transform->normal_transform[8] = inverse_modelview.zz;

        transform->normal_transform_dirty = false;
}
