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

#include <epoxy/gl.h>

#include "fv-transform.h"

void
fv_transform_update_derived_values(struct fv_transform *transform)
{
        struct fv_matrix inverse_modelview;

        /* Calculate the normal matrix */

        /* Invert the matrix */
        fv_matrix_get_inverse(&transform->modelview, &inverse_modelview);

        /* Transpose it while converting it to 3x3 */
        transform->normal_matrix[0] = inverse_modelview.xx;
        transform->normal_matrix[1] = inverse_modelview.xy;
        transform->normal_matrix[2] = inverse_modelview.xz;

        transform->normal_matrix[3] = inverse_modelview.yx;
        transform->normal_matrix[4] = inverse_modelview.yy;
        transform->normal_matrix[5] = inverse_modelview.yz;

        transform->normal_matrix[6] = inverse_modelview.zx;
        transform->normal_matrix[7] = inverse_modelview.zy;
        transform->normal_matrix[8] = inverse_modelview.zz;

        /* Calculate the combined modelview-projection matrix */
        fv_matrix_multiply(&transform->mvp,
                           &transform->projection,
                           &transform->modelview);
}
