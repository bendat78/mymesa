/*
 * Copyright © 2017 Gert Wollny
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "program/prog_instruction.h"
#include "util/u_math.h"
#include <ostream>
#include <cassert>
#include <algorithm>

#include <iostream>

#include "st_glsl_to_tgsi_array_merge.h"

#if __cplusplus >= 201402L
#include <memory>
using std::unique_ptr;
using std::make_unique;
#endif


array_live_range::array_live_range():
   id(0),
   length(0),
   first_access(0),
   last_access(0),
   component_access_mask(0),
   used_component_count(0)
{
}

array_live_range::array_live_range(unsigned aid, unsigned alength):
   id(aid),
   length(alength),
   first_access(0),
   last_access(0),
   component_access_mask(0),
   used_component_count(0)
{
}

array_live_range::array_live_range(unsigned aid, unsigned alength, int begin,
                               int end, int sw):
   id(aid),
   length(alength),
   first_access(begin),
   last_access(end),
   component_access_mask(sw),
   used_component_count(util_bitcount(sw))
{
}

void array_live_range::set_live_range(int _begin, int _end)
{
   set_begin(_begin);
   set_end(_end);
}

void array_live_range::set_access_mask(int mask)
{
   component_access_mask = mask;
   used_component_count = util_bitcount(mask);
}

void array_live_range::merge_live_range(const array_live_range &other)
{
   if (other.begin() < first_access)
      first_access = other.begin();
   if (other.end() > last_access)
      last_access = other.end();
}

void array_live_range::print(std::ostream& os) const
{
   os << "[id:" << id
      << ", length:" << length
      << ", (b:" << first_access
      << ", e:" << last_access
      << "), sw:" << component_access_mask
      << ", nc:" << used_component_count
      << "]";
}

bool array_live_range::time_doesnt_overlap(const array_live_range& other) const
{
   return (other.last_access < first_access ||
           last_access < other.first_access);
}

namespace tgsi_array_merge {

array_remapping::array_remapping():
   target_id(0),
   reswizzle(false),
   finalized(true)
{
}

array_remapping::array_remapping(int trgt_array_id, unsigned src_access_mask):
   target_id(trgt_array_id),
   original_src_access_mask(src_access_mask),
   reswizzle(false),
   finalized(false)
{
}

array_remapping::array_remapping(int trgt_array_id, int trgt_access_mask,
                                 int src_access_mask):
   target_id(trgt_array_id),
   summary_access_mask(trgt_access_mask),
   original_src_access_mask(src_access_mask),
   reswizzle(true),
   finalized(false)
{
   for (int i = 0; i < 4; ++i) {
      read_swizzle_map[i] = -1;
      writemask_map[i] = 0;
   }

   int src_swizzle_bit = 1;
   int next_free_swizzle_bit = 1;
   int k = 0;
   bool skip = true;
   unsigned last_src_bit = util_last_bit(src_access_mask);

   for (unsigned i = 0; i < 4; ++i, src_swizzle_bit <<= 1) {

      /* The swizzle mapping fills the unused slots with the last used
       * component (think temp[A].xyyy) and maps the write mask accordingly.
       * Hence, if (i < last_src_bit) skip is true and mappings are only addeed
       * for used the components, but for (i >= last_src_bit) the mapping
       * is set for remaining slots.
       */
      if (skip && !(src_swizzle_bit & src_access_mask))
         continue;
      skip = (i < last_src_bit);

      /* Find the next free access slot in the target.*/
      while ((trgt_access_mask & next_free_swizzle_bit) &&
             k < 4) {
         next_free_swizzle_bit <<= 1;
         ++k;
      }
      assert(k < 4 &&
             "Interleaved array would have more then four components");

      /* Set the mapping for this component. */
      read_swizzle_map[i] = k;
      writemask_map[i] = next_free_swizzle_bit;
      trgt_access_mask |= next_free_swizzle_bit;

      /* Update the joined access mask if we didn't just fill the mapping.*/
      if (src_swizzle_bit & src_access_mask)
         summary_access_mask |= next_free_swizzle_bit;
   }
}

int array_remapping::map_writemask(int writemask_to_map) const
{
   assert(is_valid());
   if (!reswizzle)
      return writemask_to_map;

   assert(original_src_access_mask & writemask_to_map);
   int result = 0;
   for (int i = 0; i < 4; ++i) {
      if (1 << i & writemask_to_map)
         result |= writemask_map[i];
   }
   return result;
}

uint16_t array_remapping::move_read_swizzles(uint16_t original_swizzle) const
{
   assert(is_valid());
   if (!reswizzle)
      return original_swizzle;

   /* Since
    *
    *   dst.zw = src.xy in glsl actually is MOV dst.__zw src.__xy
    *
    * when interleaving the arrays the source swizzles must be moved
    * according to the changed dst write mask.
    */
   uint16_t out_swizzle = 0;
   for (int idx = 0; idx < 4; ++idx) {
      uint16_t orig_swz = GET_SWZ(original_swizzle, idx);
      int new_idx = read_swizzle_map[idx];
      if (new_idx >= 0)
         out_swizzle |= orig_swz << 3 * new_idx;
   }
   return out_swizzle;
}

int array_remapping::map_one_swizzle(int swizzle_to_map) const
{
   if (!reswizzle)
      return swizzle_to_map;

   assert(read_swizzle_map[swizzle_to_map] >= 0);
   return read_swizzle_map[swizzle_to_map];
}

uint16_t array_remapping::map_swizzles(uint16_t old_swizzle) const
{
   if (!reswizzle)
      return old_swizzle;

   uint16_t out_swizzle = 0;
   for (int idx = 0; idx < 4; ++idx) {
      uint16_t swz = map_one_swizzle(GET_SWZ(old_swizzle, idx));
      out_swizzle |= swz << 3 * idx;
   }
   return out_swizzle;
}

void array_remapping::print(std::ostream& os) const
{
   static const char xyzw[] = "xyzw";
   if (is_valid()) {
      os << "[aid: " << target_id;

      if (reswizzle) {
         os << " write-swz: ";
         for (int i = 0; i < 4; ++i) {
            if (1 << i & original_src_access_mask) {
               switch (writemask_map[i]) {
               case 1: os << "x"; break;
               case 2: os << "y"; break;
               case 4: os << "z"; break;
               case 8: os << "w"; break;
               }
            } else {
               os << "_";
            }
         }
         os << ", read-swz: ";
         for (int i = 0; i < 4; ++i) {
            if (1 << i & original_src_access_mask && read_swizzle_map[i] >= 0)
               os << xyzw[read_swizzle_map[i]];
                else
               os << "_";
         }
      }
      os << "]";
   } else {
      os << "[unused]";
   }
}

void array_remapping::finalize_mappings(array_remapping *arr_map)
{
   assert(is_valid());

   array_remapping& forward_map = arr_map[target_id];

   /* If no valid map is provided than we have a final target array
    * at the target_id index, no finalization needed. */
   if (!forward_map.is_valid())
      return;

   /* This mappoints to another mapped array that may need finalization. */
   if (!forward_map.is_finalized())
      forward_map.finalize_mappings(arr_map);

   /* Now finalize this mapping by translating the map to represent
    * a mapping to a final target array (i.e. one that is not mapped).
    * This is only necessary if the target_id array map provides reswizzling.
    */
   if (forward_map.reswizzle) {

      /* If this mapping doesn't have a reswizzle map build one now.*/
      if (!reswizzle) {
         for (int i = 0; i < 4; ++i) {
            if (1 << i  & original_src_access_mask) {
               read_swizzle_map[i] = i;
               writemask_map[i] = 1 << i;
            } else {
               read_swizzle_map[i] = -1;
               writemask_map[i] = 0;
            }
         }
         reswizzle = true;
      }

      /* Propagate the swizzle mapping of the forward map.*/
      for (int i = 0; i < 4; ++i) {
         if ((1 << i & original_src_access_mask) == 0)
            continue;
         read_swizzle_map[i] = forward_map.map_one_swizzle(read_swizzle_map[i]);
         writemask_map[i] = forward_map.map_writemask(writemask_map[i]);
      }

   }

   /* Now we can skip the intermediate mapping.*/
   target_id = forward_map.target_id;
   finalized = true;
}

/* Required by the unit tests */
bool operator == (const array_remapping& lhs, const array_remapping& rhs)
{
   if (lhs.target_id != rhs.target_id)
      return false;

   if (lhs.target_id == 0)
      return true;

   if (lhs.reswizzle) {
      if (!rhs.reswizzle)
         return false;

      if (lhs.original_src_access_mask != rhs.original_src_access_mask)
         return false;

      for (int i = 0; i < 4; ++i) {
         if (1 << i & lhs.original_src_access_mask) {
            if (lhs.writemask_map[i] != rhs.writemask_map[i])
               return false;
            if (lhs.read_swizzle_map[i] != rhs.read_swizzle_map[i])
               return false;
         }
      }
   } else {
      return !rhs.reswizzle;
   }
   return true;
}

/* end namespace tgsi_array_merge */
}
