/*
 * Copyright 2011-2017 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

CCL_NAMESPACE_BEGIN

ccl_device_inline void kernel_filter_construct_gramian(int x, int y,
                                                       int storage_stride,
                                                       int dx, int dy,
                                                       int w, int h,
                                                       float ccl_readonly_ptr buffer,
                                                       float *color_pass,
                                                       float *variance_pass,
                                                       float ccl_readonly_ptr transform,
                                                       int *rank,
                                                       float weight,
                                                       float *XtWX,
                                                       float3 *XtWY)
{
	const int pass_stride = w*h;

	int p_offset =  y    *w +  x;
	int q_offset = (y+dy)*w + (x+dx);

#ifdef __KERNEL_CPU__
	const int stride = 1;
	(void)storage_stride;
#else
	const int stride = storage_stride;
#endif

	float3 p_color = filter_get_pixel_color(color_pass + p_offset, pass_stride);
	float3 q_color = filter_get_pixel_color(color_pass + q_offset, pass_stride);

	float p_std_dev = sqrtf(filter_get_pixel_variance(variance_pass + p_offset, pass_stride));
	float q_std_dev = sqrtf(filter_get_pixel_variance(variance_pass + q_offset, pass_stride));

	if(average(fabs(p_color - q_color)) > 3.0f*(p_std_dev + q_std_dev + 1e-3f)) {
		return;
	}

	float feature_means[DENOISE_FEATURES], features[DENOISE_FEATURES];
	filter_get_features(make_int2(x, y), buffer + p_offset, feature_means, NULL, pass_stride);

	float design_row[DENOISE_FEATURES+1];
	filter_get_design_row_transform(make_int2(x+dx, y+dy), buffer + q_offset, feature_means, pass_stride, features, *rank, design_row, transform, stride);

	math_trimatrix_add_gramian_strided(XtWX, (*rank)+1, design_row, weight, stride);
	math_vec3_add_strided(XtWY, (*rank)+1, design_row, weight * q_color, stride);
}

ccl_device_inline void kernel_filter_finalize(int x, int y, int w, int h,
                                              float *buffer,
                                              int *rank, int storage_stride,
                                              float *XtWX, float3 *XtWY,
                                              int4 buffer_params, int sample)
{
#ifdef __KERNEL_CPU__
	const int stride = 1;
	(void)storage_stride;
#else
	const int stride = storage_stride;
#endif

	math_trimatrix_vec3_solve(XtWX, XtWY, (*rank)+1, stride);

	float3 final_color = XtWY[0];
	if(buffer_params.z) {
		float *combined_buffer = buffer + (y*buffer_params.y + x + buffer_params.x)*buffer_params.z;
		final_color *= sample;
		if(buffer_params.w) {
			final_color.x += combined_buffer[buffer_params.w+0];
			final_color.y += combined_buffer[buffer_params.w+1];
			final_color.z += combined_buffer[buffer_params.w+2];
		}
		combined_buffer[0] = final_color.x;
		combined_buffer[1] = final_color.y;
		combined_buffer[2] = final_color.z;
	}
	else {
		int idx = y*w+x;
		buffer[idx] = final_color.x;
		buffer[buffer_params.w + idx] = final_color.y;
		buffer[2*buffer_params.w + idx] = final_color.z;
	}
}

#undef STORAGE_TYPE

CCL_NAMESPACE_END
