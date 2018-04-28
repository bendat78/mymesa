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

/* A short overview on how the array merging works:
 *
 * Inputs:
 *   - per array information: live range, access mask, size
 *   - the program
 *
 * Output:
 *   - the program with updated array addressing
 *
 * Pseudo algorithm:
 *
 * repeat
 *    for all pairs of arrays:
 *       if they have non-overlapping live ranges and equal access masks:
 *          - pick shorter array
 *          - merge its live range into the longer array
 *          - set its merge target array to the longer array
 *          - mark the shorter array as processed
 *
 *    for all pairs of arrays:
 *       if they have overlapping live ranges use in sum at most four components:
 *          - pick shorter array
 *          - evaluate reswizzle map to move its components into the components
 *            that are not used by the longer array
 *          - set its merge target array to the longer array
 *          - mark the shorter array as processed
 *          - bail out loop
 *  until no more successfull merges were found
 *
 *  for all pairs of arrays:
 *     if they have non-overlapping live ranges:
 *          - pick shorter array
 *          - merge its live range into the longer array
 *          - set its merge target array to the longer array
 *          - mark the shorter array as processed
 *
 * Finalize remapping map so that target arrays are always final, i.e. have
 * themselfes no merge target set.
 *
 * Example:
 *   ID  | Length | Live range | access mask | target id | reswizzle
 *   ================================================================
 *   1       3       3-10          x___            0        ____
 *   2       4      13-20          x___            0        ____
 *   3       8       3-20          x___            0        ____
 *   4       6      21-40          xy__            0        ____
 *   5       7      12-30          xy__            0        ____
 *
 * 1. merge live ranges 1 and 2
 *
 *   ID  | Length | Live range | access mask | target id | reswizzle
 *   ================================================================
 *   1       -        -            x___            2        ____
 *   2       4       3-20          x___            0        ____
 *   3       8       3-20          x___            0        ____
 *   4       6      21-40          xy__            0        ____
 *   5       7      12-30          xy__            0        ____
 *
 *
 *  3. interleave 2 and 3
 *
 *   ID  | Length | Live range | access mask | target id | reswizzle
 *   ================================================================
 *   1       -        -            x___            2        ____
 *   2       -        -            x___            3        _x__
 *   3       8       3-20          xy__            0        ____
 *   4       6      21-40          xy__            0        ____
 *   5       7      12-30          xy__            0        ____
 *
 *   3. merge live ranges 3 and 4
 *
 *   ID  | Length | Live range | access mask | target id | reswizzle
 *   ================================================================
 *   1       -        -            x___            2        ____
 *   2       -        -            x___            3        _x__
 *   3       8       3-40          xy__            0        ____
 *   4       -        -            xy__            3        ____
 *   5       7       3-21          xy__            0        ____
 *
 *   4. interleave 3 and 5
 *
 *   ID  | Length | Live range | access mask | target id | reswizzle
 *   ================================================================
 *   1       -        -            x___            2        ____
 *   2       -        -            x___            3        _x__
 *   3       8       3-40          xy__            0        ____
 *   4       -        -            xy__            3        ____
 *   5       -        -            xy__            3        __xy
 *
 *   5. finalize remapping
 *   (Array 1 has been merged with 2 that was later interleaved, so
 *   the reswizzeling must be propagated.
 *
 *   ID  | Length | Live range | new access mask | target id | reswizzle
 *   ================================================================
 *   1       -        -               _y__            3        _x__
 *   2       -        -               _y__            3        _x__
 *   3       8       3-40             xy__            0        ____
 *   4       -        -               xy__            3        ____
 *   5       -        -               __zw            3        __xy
 *
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

#define ARRAY_MERGE_DEBUG 0

#if ARRAY_MERGE_DEBUG > 0
#define ARRAY_MERGE_DUMP(x) do std::cerr << x; while (0)
#else
#define ARRAY_MERGE_DUMP(x)
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

static
bool sort_by_begin(const array_live_range& lhs, const array_live_range& rhs) {
   return lhs.begin() < rhs.begin();
}

/* Helper class to evaluate merging and interleaving of arrays */
class array_merge_evaluator {
public:
   typedef int (*array_merger)(array_live_range& range_1,
                               array_live_range& range_2,
                               array_remapping *_remapping);

   array_merge_evaluator(int _narrays, array_live_range *_ranges,
                         array_remapping *_remapping);

   /** Run the merge strategy on all arrays
    * @returns number of successfull merges
    */
   int run(array_merger merger, bool always_restart);

private:
   int narrays;
   array_live_range *ranges;
   array_remapping *remapping;


};

/** Execute the live range merge */
static
int merge_live_range(array_live_range& range_1, array_live_range& range_2,
           array_remapping *remapping)
{
   if (range_2.time_doesnt_overlap(range_1)) {
      if (range_1.array_length() < range_2.array_length())
         std::swap(range_2, range_1);

      ARRAY_MERGE_DUMP("merge " << range_2 << " into " << range_1 << "\n");

      remapping[range_2.array_id()] = array_remapping(range_1.array_id(),
                                                      range_1.access_mask());
      range_1.merge_live_range(range_2);
      return 1;
   }
   return 0;
}

/** Merge arrays that have non-overlapping live ranges
 *  and equal access masks.
 */
static
int merge_live_range_equal_swizzle(array_live_range& range_1,
                                      array_live_range& range_2,
                                      array_remapping *remapping)
{
   if (range_1.access_mask() == range_2.access_mask())
      return merge_live_range(range_1, range_2, remapping);
   return 0;
}

static
int array_interleave(array_live_range& range_1, array_live_range& range_2,
                     array_remapping *remapping)
{
   if ((range_2.used_components() + range_1.used_components() > 4) ||
       range_1.time_doesnt_overlap(range_2))
      return 0;

   if (range_1.array_length() < range_2.array_length())
      std::swap(range_2, range_1);

   ARRAY_MERGE_DUMP("Interleave " << range_2 << " into " << range_1 << "\n");
   remapping[range_2.array_id()] = array_remapping(range_1.array_id(),
                                               range_1.access_mask(),
                                               range_2.access_mask());
   range_1.merge_live_range(range_2);
   range_1.set_access_mask(remapping[range_2.array_id()].combined_access_mask());
   ARRAY_MERGE_DUMP("  Interleaved is " << range_1 << "\n");
   return 1;
}



/* Implementation of the helper classes follows */
array_merge_evaluator::array_merge_evaluator(int _narrays,
                                             array_live_range *_ranges,
                                             array_remapping *_remapping):
   narrays(_narrays),
   ranges(_ranges),
   remapping(_remapping)
{
}

int array_merge_evaluator::run(array_merger merger, bool always_restart)
{
   int remaps = 0;

   for (int i = 0; i < narrays; ++i) {

      if (remapping[ranges[i].array_id()].is_valid())
         continue;

      for (int j = i + 1; j < narrays; ++j) {

         if (!remapping[ranges[j].array_id()].is_valid()) {
            int n = merger(ranges[i], ranges[j], remapping);
            if (always_restart && n)
               return n;
            remaps += n;
         }

      }
   }
   return remaps;
}

/* Estimate the array merging: First in a loop, arrays with equal access mask
 * are merged then interleave arrays that together use at most four components,
 * and finally arrays are merged regardless of access mask.
 * @param[in] narrays number of arrays
 * @param[in,out] alt array life times, the merge target life time will be
 *   updated with the new life time.
 * @param[in,out] remapping track the arraay index remapping and reswizzeling.
 * @returns number of merged arrays
 */
bool get_array_remapping(int narrays, array_live_range *ranges,
                         array_remapping *remapping)
{
   int total_remapped = 0;
   int n_remapped;

   /* Sort by "begin of live range" so that we don't have to restart searching
    * after every merge.
    */
   std::sort(ranges, ranges + narrays, sort_by_begin);
   array_merge_evaluator merge_evaluator(narrays, ranges, remapping);

   do {

      n_remapped = merge_evaluator.run(merge_live_range_equal_swizzle, false);

      /* try only one array interleave, if successfull, another
       * live_range merge is tried. The test MergeAndInterleave5
       * (mesa/st/tests/test_glsl_to_tgsi_array_merge.cpp)
       * shows how this can result in more arrays being merged.
       */
      n_remapped += merge_evaluator.run(array_interleave, true);
      total_remapped += n_remapped;

      ARRAY_MERGE_DUMP("Remapped " << n_remapped << " arrays\n");
   } while (n_remapped > 0);

   total_remapped += merge_evaluator.run(merge_live_range, false);
   ARRAY_MERGE_DUMP("Remapped a total of " << total_remapped << " arrays\n");

   for (int i = 1; i <= narrays; ++i) {
      if (remapping[i].is_valid()) {
         remapping[i].finalize_mappings(remapping);
      }
   }
   return total_remapped > 0;
}

/* Remap the arrays in a TGSI program according to the given mapping.
 * @param narrays number of arrays
 * @param array_sizes array of arrays sizes
 * @param map the array remapping information
 * @param instructions TGSI program
 * @returns number of arrays after remapping
 */
int remap_arrays(int narrays, unsigned *array_sizes,
                 exec_list *instructions,
                 array_remapping *map)
{
   /* re-calculate arrays */
#if __cplusplus < 201402L
   int *idx_map = new int[narrays + 1];
   unsigned *old_sizes = new unsigned[narrays + 1];
#else
   unique_ptr<int[]> idx_map = make_unique<int[]>(narrays + 1);
   unique_ptr<unsigned[]> old_sizes = make_unique<unsigned[]>(narrays + 1);
#endif

   memcpy(&old_sizes[0], &array_sizes[0], sizeof(unsigned) * narrays);

   /* Evaluate mapping for the array indices and update array sizes */
   int new_narrays = 0;
   for (int i = 1; i <= narrays; ++i) {
      if (!map[i].is_valid()) {
         ++new_narrays;
         idx_map[i] = new_narrays;
         array_sizes[new_narrays] = old_sizes[i];
      }
   }

   /* Map the array ids of merge arrays. */
   for (int i = 1; i <= narrays; ++i) {
      if (map[i].is_valid()) {
         map[i].set_target_id(idx_map[map[i].target_array_id()]);
      }
   }

   /* Map the array ids of merge targets that got only renumbered. */
   for (int i = 1; i <= narrays; ++i) {
      if (!map[i].is_valid()) {
         map[i].set_target_id(idx_map[i]);
      }
   }

   /* Update the array ids and swizzles in the registers */
   foreach_in_list(glsl_to_tgsi_instruction, inst, instructions) {
      for (unsigned j = 0; j < num_inst_src_regs(inst); j++) {
         st_src_reg& src = inst->src[j];
         if (src.file == PROGRAM_ARRAY && src.array_id > 0) {
            array_remapping& m = map[src.array_id];
            if (m.is_valid()) {
               src.array_id = m.target_array_id();
               src.swizzle = m.map_swizzles(src.swizzle);
            }
         }
      }
      for (unsigned j = 0; j < inst->tex_offset_num_offset; j++) {
         st_src_reg& src = inst->tex_offsets[j];
         if (src.file == PROGRAM_ARRAY && src.array_id > 0) {
            array_remapping& m = map[src.array_id];
            if (m.is_valid()) {
               src.array_id = m.target_array_id();
               src.swizzle = m.map_swizzles(src.swizzle);
            }
         }
      }
      for (unsigned j = 0; j < num_inst_dst_regs(inst); j++) {
         st_dst_reg& dst = inst->dst[j];
         if (dst.file == PROGRAM_ARRAY && dst.array_id > 0) {
            array_remapping& m = map[dst.array_id];
            if (m.is_valid()) {
               assert(j == 0 &&
                      "remapping can only be done for single dest ops");
               dst.array_id = m.target_array_id();
               dst.writemask = m.map_writemask(dst.writemask);

               /* If the target component is moved, then the source swizzles
                * must be moved accordingly.
                */
               for (unsigned j = 0; j < num_inst_src_regs(inst); j++) {
                  st_src_reg& src = inst->src[j];
                  src.swizzle = m.move_read_swizzles(src.swizzle);
               }
            }
         }
      }
   }

#if __cplusplus < 201402L
   delete[] old_sizes;
   delete[] idx_map;
#endif

   return new_narrays;
}

}

using namespace tgsi_array_merge;

int  merge_arrays(int narrays,
                  unsigned *array_sizes,
                  exec_list *instructions,
                  struct array_live_range *arr_live_ranges)
{
   array_remapping *map= new array_remapping[narrays + 1];

   if (get_array_remapping(narrays, arr_live_ranges, map))
      narrays = remap_arrays(narrays, array_sizes, instructions, map);

   delete[] map;
   return narrays;
}