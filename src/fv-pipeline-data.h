/*
 * Finvenkisto
 *
 * Copyright (C) 2016, 2017 Neil Roberts
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

#ifndef FV_PIPELINE_DATA_H
#define FV_PIPELINE_DATA_H

#include <stdbool.h>
#include "fv-vk.h"
#include "fv-vk-data.h"

enum fv_pipeline_data_attrib {
        FV_PIPELINE_DATA_ATTRIB_POSITION,
        FV_PIPELINE_DATA_ATTRIB_TEX_COORD,
        FV_PIPELINE_DATA_ATTRIB_NORMAL,
        FV_PIPELINE_DATA_ATTRIB_COLOR
};

enum fv_pipeline_data_dsl {
        FV_PIPELINE_DATA_DSL_TEXTURE,
        FV_PIPELINE_DATA_N_DSLS
};

enum fv_pipeline_data_layout {
        FV_PIPELINE_DATA_LAYOUT_MAP,
        FV_PIPELINE_DATA_LAYOUT_EMPTY,
        FV_PIPELINE_DATA_LAYOUT_SPECIAL_TEXTURE,
        FV_PIPELINE_DATA_N_LAYOUTS
};

enum fv_pipeline_data_pipeline {
        FV_PIPELINE_DATA_PIPELINE_MAP,
        FV_PIPELINE_DATA_PIPELINE_SPECIAL_COLOR,
        FV_PIPELINE_DATA_PIPELINE_SPECIAL_TEXTURE,
        FV_PIPELINE_DATA_N_PIPELINES
};

struct fv_pipeline_data {
        VkDescriptorSetLayout dsls[FV_PIPELINE_DATA_N_DSLS];
        VkPipelineLayout layouts[FV_PIPELINE_DATA_N_LAYOUTS];
        VkPipeline pipelines[FV_PIPELINE_DATA_N_PIPELINES];
};

bool
fv_pipeline_data_init(const struct fv_vk_data *vk_data,
                      VkRenderPass render_pass,
                      struct fv_pipeline_data *data);

void
fv_pipeline_data_destroy(const struct fv_vk_data *vk_data,
                         struct fv_pipeline_data *data);

#endif /* FV_PIPELINE_DATA_H */
