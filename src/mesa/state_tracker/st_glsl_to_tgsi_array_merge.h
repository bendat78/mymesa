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

#ifndef MESA_GLSL_TO_TGSI_ARRAY_MERGE_H
#define MESA_GLSL_TO_TGSI_ARRAY_MERGE_H

#include "st_glsl_to_tgsi_private.h"
#include <iosfwd>

/* Helper class to evaluate the required live range of an array.
 *
 * For arrays not only the live range must be tracked, but also the arrays
 * length and since we want to interleave arrays, we also track an access mask.
 * Consequently, one array can be merged into another or interleaved with
 * another only if the target array is longer.
 */
class array_live_range {
public:
   array_live_range();
   array_live_range(unsigned aid, unsigned alength);
   array_live_range(unsigned aid, unsigned alength, int first_access,
                  int last_access, int mask);

   void set_live_range(int first_access, int last_access);
   void set_begin(int _begin){first_access = _begin;}
   void set_end(int _end){last_access = _end;}
   void set_access_mask(int s);
   void merge_live_range(const array_live_range& other);

   unsigned array_id() const {return id;}
   int array_length() const { return length;}
   int begin() const { return first_access;}
   int end() const { return last_access;}
   int access_mask() const { return component_access_mask;}
   int used_components() const {return used_component_count;}

   bool time_doesnt_overlap(const array_live_range& other) const;

   void print(std::ostream& os) const;

private:
   unsigned id;
   unsigned length;
   int first_access;
   int last_access;
   int component_access_mask;
   int used_component_count;
};

inline
std::ostream& operator << (std::ostream& os, const array_live_range& lt) {
   lt.print(os);
   return os;
}

namespace tgsi_array_merge {

/* Helper class to merge and interleave arrays.
 * The interface is exposed here to make unit tests possible.
 */
class array_remapping {
public:

   /** Create an invalid mapping that is used as place-holder for
    * arrays that are not mapped at all.
    */
   array_remapping();

   /** Simple remapping that is done when the lifetimes do not
    * overlap.
    * @param trgt_array_id ID of the array that the new array will
    *        be interleaved with
    */
   array_remapping(int trgt_array_id, unsigned src_access_mask);

   /** Component interleaving of arrays.
    * @param target_array_id ID of the array that the new array will
    *        be interleaved with
    * @param trgt_access_mask the component mast of the target array
    *        (the components that are already reserved)
    * @param orig_component_mask
    */
   array_remapping(int trgt_array_id, int trgt_access_mask,
                   int src_access_mask);

   /* Defines a valid remapping */
   bool is_valid() const {return target_id > 0;}

   /* Resolve the mapping chain so that this mapping remaps to an
    * array that is not remapped.
    */
   void finalize_mappings(array_remapping *arr_map);

   void set_target_id(int tid) {target_id = tid;}

   /* Translates the write mask to the new, interleaved component
    * position
    */
   int map_writemask(int original_src_access_mask) const;

   /* Translates all read swizzles to the new, interleaved component
    * swizzles
    */
   uint16_t map_swizzles(uint16_t original_swizzle) const;

   /** Move the read swizzles to the positiones that correspond to
    * a changed write mask.
    */
   uint16_t move_read_swizzles(uint16_t original_swizzle) const;

   unsigned target_array_id() const {return target_id;}

   int combined_access_mask() const {return summary_access_mask;}

   void print(std::ostream& os) const;

   bool is_finalized() { return finalized;}

   friend bool operator == (const array_remapping& lhs,
                            const array_remapping& rhs);

   int map_one_swizzle(int swizzle_to_map) const;

private:
   unsigned target_id;
   uint16_t writemask_map[4];
   int16_t read_swizzle_map[4];
   unsigned summary_access_mask:4;
   unsigned original_src_access_mask:4;
   int reswizzle:1;
   int finalized:1;
};

inline
std::ostream& operator << (std::ostream& os, const array_remapping& am)
{
   am.print(os);
   return os;
}

}
#endif
