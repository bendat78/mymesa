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

#include "st_glsl_to_tgsi_private.h"
#include <memory>
#include <map>

class tgsi_temp_lifetime {
public:

   enum e_scope_type {
      sct_outer,
      sct_loop,
      sct_if,
      sct_else,
      sct_switch,
      sct_case,
      sct_unknown
   };

   enum e_acc_type {
      acc_read,
      acc_write
   };

   class prog_scope {

   public:
      prog_scope(e_scope_type type, int id, int lvl, int s_begin);
      prog_scope(std::shared_ptr<prog_scope> p, e_scope_type type, int id, int lvl, int s_begin);

      e_scope_type type() const;
      std::shared_ptr<prog_scope> parent() const;
      int level() const;
      int id() const;
      int end() const;
      int begin() const;
      const prog_scope *in_ifelse() const;
      const prog_scope *in_switchcase() const;
      int loop_continue_idx() const;
      bool in_loop() const;
      const prog_scope *get_parent_loop() const;
      bool is_conditional() const;
      bool contains(const prog_scope& other) const;
      void set_end(int end);
      void set_previous(std::shared_ptr<prog_scope> prev);
      void set_continue(std::weak_ptr<prog_scope> scope, int i);
   private:
      e_scope_type scope_type;
      int scope_id;
      int nested_level;
      int scope_begin;
      int scope_end;
      int loop_continue;
      std::weak_ptr<prog_scope> loop_to_continue_scope;
      std::shared_ptr<prog_scope> previous;
      std::shared_ptr<prog_scope> parent_scope;

   };

   class temp_access {

   public:

      void append(int index, e_acc_type rw, std::shared_ptr<prog_scope> pstate);
      std::pair<int, int> get_required_lifetime() const;

   private:

      struct temp_access_record {
         int index;
         e_acc_type acc;
         std::shared_ptr<prog_scope> pstate;
      };

      std::vector< temp_access_record > timeline;

   };

   tgsi_temp_lifetime(exec_list *instructions, int ntemps);

   const std::vector<std::pair<int, int> >&  get_lifetimes() const;


private:

   void evaluate(exec_list *instructions);

   std::vector<std::pair<int, int> > lifetimes;
};

void evaluate_remapping(const std::vector<std::pair<int, int>>& lt, std::vector<rename_reg_pair>& result);


