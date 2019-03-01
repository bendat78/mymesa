/**************************************************************************
 *
 * Copyright 2019 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: James Zhu <james.zhu<@amd.com>
 *
 **************************************************************************/

#include <assert.h>

#include "tgsi/tgsi_text.h"
#include "vl_compositor_cs.h"

struct cs_viewport {
   float scale_x;
   float scale_y;
   struct u_rect area;
   int translate_x;
   int translate_y;
};

const char *compute_shader_video_buffer =
      "COMP\n"
      "PROPERTY CS_FIXED_BLOCK_WIDTH 8\n"
      "PROPERTY CS_FIXED_BLOCK_HEIGHT 8\n"
      "PROPERTY CS_FIXED_BLOCK_DEPTH 1\n"

      "DCL SV[0], THREAD_ID\n"
      "DCL SV[1], BLOCK_ID\n"

      "DCL CONST[0..5]\n"
      "DCL SVIEW[0..2], RECT, FLOAT\n"
      "DCL SAMP[0..2]\n"

      "DCL IMAGE[0], 2D, WR\n"
      "DCL TEMP[0..7]\n"

      "IMM[0] UINT32 { 8, 8, 1, 0}\n"
      "IMM[1] FLT32 { 1.0, 2.0, 0.0, 0.0}\n"

      "UMAD TEMP[0], SV[1], IMM[0], SV[0]\n"

      /* Drawn area check */
      "USGE TEMP[1].xy, TEMP[0].xyxy, CONST[4].xyxy\n"
      "USLT TEMP[1].zw, TEMP[0].xyxy, CONST[4].zwzw\n"
      "AND TEMP[1].x, TEMP[1].xxxx, TEMP[1].yyyy\n"
      "AND TEMP[1].x, TEMP[1].xxxx, TEMP[1].zzzz\n"
      "AND TEMP[1].x, TEMP[1].xxxx, TEMP[1].wwww\n"

      "UIF TEMP[1]\n"
         /* Translate */
         "UADD TEMP[2].xy, TEMP[0], -CONST[5].xyxy\n"
         "U2F TEMP[2], TEMP[2]\n"
         "DIV TEMP[3], TEMP[2], IMM[1].yyyy\n"

         /* Scale */
         "DIV TEMP[2], TEMP[2], CONST[3].zwzw\n"
         "DIV TEMP[3], TEMP[3], CONST[3].zwzw\n"

         /* Fetch texels */
         "TEX_LZ TEMP[4].x, TEMP[2], SAMP[0], RECT\n"
         "TEX_LZ TEMP[4].y, TEMP[3], SAMP[1], RECT\n"
         "TEX_LZ TEMP[4].z, TEMP[3], SAMP[2], RECT\n"

         "MOV TEMP[4].w, IMM[1].xxxx\n"

         /* Color Space Conversion */
         "DP4 TEMP[7].x, CONST[0], TEMP[4]\n"
         "DP4 TEMP[7].y, CONST[1], TEMP[4]\n"
         "DP4 TEMP[7].z, CONST[2], TEMP[4]\n"

         "MOV TEMP[5].w, TEMP[4].zzzz\n"
         "SLE TEMP[6].w, TEMP[5], CONST[3].xxxx\n"
         "SGT TEMP[5].w, TEMP[5], CONST[3].yyyy\n"

         "MAX TEMP[7].w, TEMP[5], TEMP[6]\n"

         "STORE IMAGE[0], TEMP[0], TEMP[7], 2D\n"
      "ENDIF\n"

      "END\n";

const char *compute_shader_weave =
      "COMP\n"
      "PROPERTY CS_FIXED_BLOCK_WIDTH 8\n"
      "PROPERTY CS_FIXED_BLOCK_HEIGHT 8\n"
      "PROPERTY CS_FIXED_BLOCK_DEPTH 1\n"

      "DCL SV[0], THREAD_ID\n"
      "DCL SV[1], BLOCK_ID\n"

      "DCL CONST[0..5]\n"
      "DCL SVIEW[0..2], RECT, FLOAT\n"
      "DCL SAMP[0..2]\n"

      "DCL IMAGE[0], 2D, WR\n"
      "DCL TEMP[0..9]\n"

      "IMM[0] UINT32 { 8, 8, 1, 0}\n"
      "IMM[1] FLT32 { 1.0, 2.0, 0.0, 0.0}\n"
      "IMM[2] UINT32 { 1, 2, 4, 0}\n"

      "UMAD TEMP[0], SV[1], IMM[0], SV[0]\n"

      /* Drawn area check */
      "USGE TEMP[1].xy, TEMP[0].xyxy, CONST[4].xyxy\n"
      "USLT TEMP[1].zw, TEMP[0].xyxy, CONST[4].zwzw\n"
      "AND TEMP[1].x, TEMP[1].xxxx, TEMP[1].yyyy\n"
      "AND TEMP[1].x, TEMP[1].xxxx, TEMP[1].zzzz\n"
      "AND TEMP[1].x, TEMP[1].xxxx, TEMP[1].wwww\n"

      "UIF TEMP[1]\n"
         "MOV TEMP[2], TEMP[0]\n"
         /* Translate */
         "UADD TEMP[2].xy, TEMP[2], -CONST[5].xyxy\n"

         /* Texture layer */
         "UMOD TEMP[2].z, TEMP[2].yyyy, IMM[2].yyyy\n"
         "UMOD TEMP[3].z, TEMP[2].yyyy, IMM[2].zzzz\n"
         "USHR TEMP[3].z, TEMP[3].zzzz, IMM[2].xxxx\n"

         "USHR TEMP[2].y, TEMP[2], IMM[2].xxxx\n"
         "USHR TEMP[3].xy, TEMP[2], IMM[2].xxxx\n"

         "U2F TEMP[4], TEMP[2]\n"
         "U2F TEMP[5], TEMP[3]\n"

         /* Scale */
         "DIV TEMP[4], TEMP[4], CONST[3].zwzw\n"
         "DIV TEMP[5], TEMP[5], CONST[3].zwzw\n"

         /* Fetch texels */
         "TEX_LZ TEMP[6].x, TEMP[4], SAMP[0], RECT\n"
         "TEX_LZ TEMP[6].y, TEMP[5], SAMP[1], RECT\n"
         "TEX_LZ TEMP[6].z, TEMP[5], SAMP[2], RECT\n"

         "MOV TEMP[6].w, IMM[1].xxxx\n"

         /* Color Space Conversion */
         "DP4 TEMP[9].x, CONST[0], TEMP[6]\n"
         "DP4 TEMP[9].y, CONST[1], TEMP[6]\n"
         "DP4 TEMP[9].z, CONST[2], TEMP[6]\n"

         "MOV TEMP[7].w, TEMP[6].zzzz\n"
         "SLE TEMP[8].w, TEMP[7], CONST[3].xxxx\n"
         "SGT TEMP[7].w, TEMP[7], CONST[3].yyyy\n"

         "MAX TEMP[9].w, TEMP[7], TEMP[8]\n"

         "STORE IMAGE[0], TEMP[0], TEMP[9], 2D\n"
      "ENDIF\n"

      "END\n";

const char *compute_shader_rgba =
      "COMP\n"
      "PROPERTY CS_FIXED_BLOCK_WIDTH 8\n"
      "PROPERTY CS_FIXED_BLOCK_HEIGHT 8\n"
      "PROPERTY CS_FIXED_BLOCK_DEPTH 1\n"

      "DCL SV[0], THREAD_ID\n"
      "DCL SV[1], BLOCK_ID\n"

      "DCL CONST[0..5]\n"
      "DCL SVIEW[0], RECT, FLOAT\n"
      "DCL SAMP[0]\n"

      "DCL IMAGE[0], 2D, WR\n"
      "DCL TEMP[0..3]\n"

      "IMM[0] UINT32 { 8, 8, 1, 0}\n"
      "IMM[1] FLT32 { 1.0, 2.0, 0.0, 0.0}\n"

      "UMAD TEMP[0], SV[1], IMM[0], SV[0]\n"

      /* Drawn area check */
      "USGE TEMP[1].xy, TEMP[0].xyxy, CONST[4].xyxy\n"
      "USLT TEMP[1].zw, TEMP[0].xyxy, CONST[4].zwzw\n"
      "AND TEMP[1].x, TEMP[1].xxxx, TEMP[1].yyyy\n"
      "AND TEMP[1].x, TEMP[1].xxxx, TEMP[1].zzzz\n"
      "AND TEMP[1].x, TEMP[1].xxxx, TEMP[1].wwww\n"

      "UIF TEMP[1]\n"
         /* Translate */
         "UADD TEMP[2].xy, TEMP[0], -CONST[5].xyxy\n"
         "U2F TEMP[2], TEMP[2]\n"

         /* Scale */
         "DIV TEMP[2], TEMP[2], CONST[3].zwzw\n"

         /* Fetch texels */
         "TEX_LZ TEMP[3], TEMP[2], SAMP[0], RECT\n"

         "STORE IMAGE[0], TEMP[0], TEMP[3], 2D\n"
      "ENDIF\n"

      "END\n";

static void
cs_launch(struct vl_compositor *c,
          void                 *cs)
{
   struct pipe_context *ctx = c->pipe;

   /* Bind the image */
   struct pipe_image_view image = {};
   image.resource = c->fb_state.cbufs[0]->texture;
   image.shader_access = image.access = PIPE_IMAGE_ACCESS_READ_WRITE;
   image.format = c->fb_state.cbufs[0]->texture->format;

   ctx->set_shader_images(c->pipe, PIPE_SHADER_COMPUTE, 0, 1, &image);

   /* Bind compute shader */
   ctx->bind_compute_state(ctx, cs);

   /* Dispatch compute */
   struct pipe_grid_info info = {};
   info.block[0] = 8;
   info.block[1] = 8;
   info.block[2] = 1;
   info.grid[0] = DIV_ROUND_UP(c->fb_state.width, info.block[0]);
   info.grid[1] = DIV_ROUND_UP(c->fb_state.height, info.block[1]);
   info.grid[2] = 1;

   ctx->launch_grid(ctx, &info);
}

static inline struct u_rect
calc_drawn_area(struct vl_compositor_state *s,
                struct vl_compositor_layer *layer)
{
   struct vertex2f tl, br;
   struct u_rect result;

   assert(s && layer);

   tl = layer->dst.tl;
   br = layer->dst.br;

   /* Scale */
   result.x0 = tl.x * layer->viewport.scale[0] + layer->viewport.translate[0];
   result.y0 = tl.y * layer->viewport.scale[1] + layer->viewport.translate[1];
   result.x1 = br.x * layer->viewport.scale[0] + layer->viewport.translate[0];
   result.y1 = br.y * layer->viewport.scale[1] + layer->viewport.translate[1];

   /* Clip */
   result.x0 = MAX2(result.x0, s->scissor.minx);
   result.y0 = MAX2(result.y0, s->scissor.miny);
   result.x1 = MIN2(result.x1, s->scissor.maxx);
   result.y1 = MIN2(result.y1, s->scissor.maxy);
   return result;
}

static bool
set_viewport(struct vl_compositor_state *s,
             struct cs_viewport         *drawn)
{
   struct pipe_transfer *buf_transfer;

   assert(s && drawn);

   void *ptr = pipe_buffer_map(s->pipe, s->shader_params,
                               PIPE_TRANSFER_READ | PIPE_TRANSFER_WRITE,
                               &buf_transfer);

   if (!ptr)
     return false;

   float *ptr_float = (float *)ptr;
   ptr_float += sizeof(vl_csc_matrix)/sizeof(float) + 2;
   *ptr_float++ = drawn->scale_x;
   *ptr_float++ = drawn->scale_y;

   int *ptr_int = (int *)ptr_float;
   *ptr_int++ = drawn->area.x0;
   *ptr_int++ = drawn->area.y0;
   *ptr_int++ = drawn->area.x1;
   *ptr_int++ = drawn->area.y1;
   *ptr_int++ = drawn->translate_x;
   *ptr_int = drawn->translate_y;

   pipe_buffer_unmap(s->pipe, buf_transfer);

   return true;
}

static void
draw_layers(struct vl_compositor       *c,
            struct vl_compositor_state *s,
            struct u_rect              *dirty)
{
   unsigned i;
   static struct cs_viewport old_drawn;

   assert(c);

   for (i = 0; i < VL_COMPOSITOR_MAX_LAYERS; ++i) {
      if (s->used_layers & (1 << i)) {
         struct vl_compositor_layer *layer = &s->layers[i];
         struct pipe_sampler_view **samplers = &layer->sampler_views[0];
         unsigned num_sampler_views = !samplers[1] ? 1 : !samplers[2] ? 2 : 3;
         struct cs_viewport drawn;

         drawn.area = calc_drawn_area(s, layer);
         drawn.scale_x = layer->viewport.scale[0] /
                  (float)layer->sampler_views[0]->texture->width0;
         drawn.scale_y = drawn.scale_x;
         drawn.translate_x = (int)layer->viewport.translate[0];
         drawn.translate_y = (int)layer->viewport.translate[1];

         if (memcmp(&drawn, &old_drawn, sizeof(struct cs_viewport))) {
            set_viewport(s, &drawn);
            old_drawn = drawn;
         }

         c->pipe->bind_sampler_states(c->pipe, PIPE_SHADER_COMPUTE, 0,
                        num_sampler_views, layer->samplers);
         c->pipe->set_sampler_views(c->pipe, PIPE_SHADER_COMPUTE, 0,
                        num_sampler_views, samplers);

         cs_launch(c, layer->cs);

         if (dirty) {
            struct u_rect drawn = calc_drawn_area(s, layer);
            dirty->x0 = MIN2(drawn.x0, dirty->x0);
            dirty->y0 = MIN2(drawn.y0, dirty->y0);
            dirty->x1 = MAX2(drawn.x1, dirty->x1);
            dirty->y1 = MAX2(drawn.y1, dirty->y1);
         }
      }
   }
}

void *
vl_compositor_cs_create_shader(struct vl_compositor *c,
                               const char           *compute_shader_text)
{
   assert(c && compute_shader_text);

   struct tgsi_token tokens[1024];
   if (!tgsi_text_translate(compute_shader_text, tokens, ARRAY_SIZE(tokens))) {
      assert(0);
      return NULL;
   }

   struct pipe_compute_state state = {};
   state.ir_type = PIPE_SHADER_IR_TGSI;
   state.prog = tokens;

   /* create compute shader */
   return c->pipe->create_compute_state(c->pipe, &state);
}

void
vl_compositor_cs_render(struct vl_compositor_state *s,
                        struct vl_compositor       *c,
                        struct pipe_surface        *dst_surface,
                        struct u_rect              *dirty_area,
                        bool                        clear_dirty)
{
   assert(c && s);
   assert(dst_surface);

   c->fb_state.width = dst_surface->width;
   c->fb_state.height = dst_surface->height;
   c->fb_state.cbufs[0] = dst_surface;

   if (!s->scissor_valid) {
      s->scissor.minx = 0;
      s->scissor.miny = 0;
      s->scissor.maxx = dst_surface->width;
      s->scissor.maxy = dst_surface->height;
   }

   if (clear_dirty && dirty_area &&
       (dirty_area->x0 < dirty_area->x1 || dirty_area->y0 < dirty_area->y1)) {

      c->pipe->clear_render_target(c->pipe, dst_surface, &s->clear_color,
                       0, 0, dst_surface->width, dst_surface->height, false);
      dirty_area->x0 = dirty_area->y0 = VL_COMPOSITOR_MAX_DIRTY;
      dirty_area->x1 = dirty_area->y1 = VL_COMPOSITOR_MIN_DIRTY;
   }

   pipe_set_constant_buffer(c->pipe, PIPE_SHADER_COMPUTE, 0, s->shader_params);

   draw_layers(c, s, dirty_area);
}
