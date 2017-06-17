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


#include "st_glsl_to_tgsi_temprename.h"
#include <tgsi/tgsi_info.h>
#include <mesa/program/prog_instruction.h>
#include <stack>
#include <limits>
#include <iostream>


using std::vector;
using std::stack;
using std::shared_ptr;
using std::weak_ptr;
using std::pair;
using std::make_pair;
using std::make_shared;
using std::numeric_limits;

tgsi_temp_lifetime::tgsi_temp_lifetime(exec_list *instructions, int ntemps):
   lifetimes(ntemps)
{
   evaluate(instructions);
}

const std::vector<std::pair<int, int> >& tgsi_temp_lifetime::get_lifetimes() const
{
   return lifetimes;
}

void tgsi_temp_lifetime::evaluate(exec_list *instructions)
{
   int i = 0;
   int loop_id = 0;
   int if_id = 0;
   int switch_id = 0;
   int scope_level = 0;
   bool is_at_end = false;
   shared_ptr<prog_scope> current = make_shared<prog_scope>(sct_outer, 0, scope_level++, i);
   stack<shared_ptr<prog_scope>> scope_stack;

   vector<temp_access> acc(lifetimes.size());

   foreach_in_list(glsl_to_tgsi_instruction, inst, instructions) {
      if (is_at_end) {
         std::cerr << "GLSL_TO_TGSI: shader has instructions past end marker\n";
         break;
      }

      switch (inst->op) {
      case TGSI_OPCODE_BGNLOOP: {
         shared_ptr<prog_scope> scope = make_shared<prog_scope>(current, sct_loop, loop_id, scope_level, i);
         ++loop_id;
         ++scope_level;
         scope_stack.push(current);
         current = scope;
         break;
      }
      case TGSI_OPCODE_ENDLOOP: {
         --scope_level;
         current->set_end(i);
         current = scope_stack.top();
         scope_stack.pop();
         break;
      }
      case TGSI_OPCODE_IF:
      case TGSI_OPCODE_UIF:{
         for (unsigned j = 0; j < num_inst_src_regs(inst); j++) {
            if (inst->src[j].file == PROGRAM_TEMPORARY) {
               acc[inst->src[j].index].append(i, acc_read, current);
            }
         }
         shared_ptr<prog_scope> scope = make_shared<prog_scope>(current, sct_if, if_id, scope_level, i);
         ++if_id;
         ++scope_level;
         scope_stack.push(current);
         current = scope;
         break;
      }
      case TGSI_OPCODE_ELSE: {
         current->set_end(i-1);
         current = make_shared<prog_scope>(current->parent(), sct_else, current->id(),
                                              current->level(), i);
         break;
      }
      case TGSI_OPCODE_END:{
         current->set_end(i);
         is_at_end = true;
         break;
      }
      case TGSI_OPCODE_ENDIF:{
         --scope_level;
         current->set_end(i-1);
         current = scope_stack.top();
         scope_stack.pop();
         break;
      }
      case TGSI_OPCODE_SWITCH: {
         shared_ptr<prog_scope> scope = make_shared<prog_scope>(current, sct_switch, switch_id, scope_level, i);
         ++scope_level;
         ++switch_id;
         scope_stack.push(current);
         current = scope;
         break;
      }
      case TGSI_OPCODE_ENDSWITCH: {
         --scope_level;
         current->set_end(i-1);

         // remove the case level
         if  (current->type() != sct_switch ) {
            current = scope_stack.top();
            scope_stack.pop();
         }
         current = scope_stack.top();
         scope_stack.pop();
         break;
      }

      case TGSI_OPCODE_CASE:
      case TGSI_OPCODE_DEFAULT: {
         if ( current->type() == sct_switch ) {
            scope_stack.push(current);
            current = make_shared<prog_scope>(current, sct_case, current->id(), scope_level, i);
         }else{
            auto p = current->parent();
            auto scope = make_shared<prog_scope>(p, sct_case, p->id(), p->level(), i);
            if (current->end() == -1)
               scope->set_previous(current);
            current = scope;
         }
         for (unsigned j = 0; j < num_inst_src_regs(inst); j++) {
            if (inst->src[j].file == PROGRAM_TEMPORARY) {
               acc[inst->src[j].index].append(i, acc_read, current);
            }
         }
      }
      case TGSI_OPCODE_BRK:  {
         if ( current->type() == sct_case) {
            current->set_end(i-1);
         }
      }
      case TGSI_OPCODE_CONT: {
         current->set_continue(current, i);
         break;
      }

      default: {

         for (unsigned j = 0; j < num_inst_dst_regs(inst); j++) {
            if (inst->dst[j].file == PROGRAM_TEMPORARY) {
               acc[inst->dst[j].index].append(i, acc_write, current);
            }
         }

         for (unsigned j = 0; j < num_inst_src_regs(inst); j++) {
            if (inst->src[j].file == PROGRAM_TEMPORARY) {
               acc[inst->src[j].index].append(i, acc_read, current);
            }
         }

         for (unsigned j = 0; j < inst->tex_offset_num_offset; j++) {
            if (inst->tex_offsets[j].file == PROGRAM_TEMPORARY) {
               acc[inst->tex_offsets[j].index].append(i, acc_read, current);
            }
         }

      } // end default
      } // end switch (op)

      ++i;
   }

   // make sure last scope is closed, even though no
   // TGSI_OPCODE_END was given
   if (current->end() < 0) {
      current->set_end(i-1);
   }

   for(unsigned i = 1; i < lifetimes.size(); ++i) {
      lifetimes[i] = acc[i].get_required_lifetime();
   }
}


tgsi_temp_lifetime::prog_scope::prog_scope(e_scope_type type, int id, int lvl, int s_begin):
   prog_scope(std::shared_ptr<prog_scope>(), type, id, lvl, s_begin)
{
}

tgsi_temp_lifetime::prog_scope::prog_scope(std::shared_ptr<prog_scope> p, e_scope_type type, int id, int lvl, int s_begin):
   scope_type(type),
   scope_id(id),
   nested_level(lvl),
   scope_begin(s_begin),
   scope_end(-1),
   loop_continue(numeric_limits<int>::max()),
   parent_scope(p)
{
}

tgsi_temp_lifetime::e_scope_type tgsi_temp_lifetime::prog_scope::type() const
{
   return scope_type;
}


std::shared_ptr<tgsi_temp_lifetime::prog_scope>
tgsi_temp_lifetime::prog_scope::parent() const
{
   return parent_scope;
}

int tgsi_temp_lifetime::prog_scope::level() const
{
   return nested_level;
}

bool tgsi_temp_lifetime::prog_scope::in_loop() const
{
   if (scope_type == sct_loop)
      return true;
   if (parent_scope)
      return parent_scope->in_loop();
   return false;
}

const tgsi_temp_lifetime::prog_scope *
tgsi_temp_lifetime::prog_scope::get_parent_loop() const
{
   if (scope_type == sct_loop)
      return this;
   if (parent_scope)
      return parent_scope->get_parent_loop();
   else
      return nullptr;
}

bool tgsi_temp_lifetime::prog_scope::contains(const prog_scope& other) const
{
   return (begin() <= other.begin()) &&  (end() >= other.end());
}

bool tgsi_temp_lifetime::prog_scope::is_conditional() const
{
   return scope_type == sct_if || scope_type == sct_else ||
         scope_type == sct_case;
}

const tgsi_temp_lifetime::prog_scope *
tgsi_temp_lifetime::prog_scope::in_ifelse() const
{
   if (scope_type == sct_if ||
       scope_type == sct_else)
      return this;
   else if (parent_scope)
      return parent_scope->in_ifelse();
   else
      return nullptr;
}

const tgsi_temp_lifetime::prog_scope *
tgsi_temp_lifetime::prog_scope::in_switchcase() const
{
   if (scope_type == sct_case)
      return this;
   else if (parent_scope)
      return parent_scope->in_switchcase();
   else
      return nullptr;
}

int tgsi_temp_lifetime::prog_scope::id() const
{
   return scope_id;
}

int tgsi_temp_lifetime::prog_scope::begin() const
{
   return scope_begin;
}

int tgsi_temp_lifetime::prog_scope::end() const
{
   return scope_end;
}

void tgsi_temp_lifetime::prog_scope::set_previous(std::shared_ptr<prog_scope> prev)
{
   previous = prev;
}

void tgsi_temp_lifetime::prog_scope::set_end(int end)
{
   if (scope_end == -1) {
      scope_end = end;
      if (previous)
         previous->set_end(end);
   }
}

void tgsi_temp_lifetime::prog_scope::set_continue(weak_ptr<prog_scope> scope, int i)
{
   if (scope_type == sct_loop) {
      loop_to_continue_scope = scope;
      loop_continue = i;
   } else if (parent_scope)
      parent()->set_continue(scope, i);
}

int tgsi_temp_lifetime::prog_scope::loop_continue_idx() const
{
   return loop_continue;
}

void tgsi_temp_lifetime::temp_access::append(int index, e_acc_type rw, std::shared_ptr<prog_scope> pstate)
{
   temp_access_record r = {index, rw, pstate};
   timeline.push_back(r);
}

pair<int, int> tgsi_temp_lifetime::temp_access::get_required_lifetime() const
{
   bool keep_for_full_loop = false;

   std::shared_ptr<prog_scope> lr_scope;
   std::shared_ptr<prog_scope> fr_scope;
   std::shared_ptr<prog_scope> fw_scope;
   const prog_scope *fw_ifthen_scope = nullptr;
   const prog_scope *fw_switch_scope = nullptr;

   int first_write = -1;
   int last_read = -1;
   int first_read = numeric_limits<int>::max();

   for (temp_access_record i: timeline) {
      if (i.acc == acc_read) {
         last_read = i.index;
         lr_scope = i.pstate;
         if (first_read > i.index) {
            first_read = i.index;
            fr_scope = i.pstate;
         }
      }else{
         if (first_write == -1) {
            first_write = i.index;
            fw_scope = i.pstate;

            // we write in an if-branch
            fw_ifthen_scope = i.pstate->in_ifelse();
            if (fw_ifthen_scope && fw_ifthen_scope->in_loop()) {
               // value not always written, in loops we must keep it
               keep_for_full_loop = true;
            } else {
               // test for switch-case
               fw_switch_scope = i.pstate->in_switchcase();

               if (fw_switch_scope && fw_switch_scope->in_loop()) {
                  keep_for_full_loop = true;
               }
            }
         } else if (keep_for_full_loop) {

            // written in if branch?
            // disable because read first in else branch
            // makes this invalid and this is not (yet) tracked
            if (0 && fw_ifthen_scope) {
               auto s = i.pstate->in_ifelse();
               // also written in the else branch?
               if (s  && (s->id() == fw_ifthen_scope->id())) {
                  keep_for_full_loop = false;
               }
            }
         }
      }
   }

   // this temp is only read, this is undefined
   // behaviour, so we can use the register otherwise
   if (!fw_scope) {
      return make_pair(-1, -1);
   }

   // Only written to, just make sure it doesn't overlap
   if (!lr_scope) {
      return make_pair(first_write, first_write);
   }

   int target_level = -1;
   // evaluate the shared scope
   while (target_level  < 0) {
      if (lr_scope->contains(*fw_scope)) {
         target_level = lr_scope->level();
      } else if (fw_scope->contains(*lr_scope)) {
         target_level = fw_scope->level();
      } else {
         // scopes (partially) disjunct, move up
         if (lr_scope->type() == sct_loop) {
            last_read = lr_scope->end();
         }
         lr_scope = lr_scope->parent();
      }
   }

   // propagate the read scope to the target_level
   while (lr_scope->level() > target_level) {
      // if the read is in a loop we need to extend the
      // variables life time to the end of that loop
      if (lr_scope->type() == sct_loop) {
         last_read = lr_scope->end();
      }
      lr_scope = lr_scope->parent();
   }


   // propagate the first_write scope to the target_level
   bool has_continue = false;
   while (target_level < fw_scope->level()) {

      // propagate lifetime also if there was a continue
      // in a loop and the write was after the continue
      if (has_continue  || (fw_scope->loop_continue_idx() < first_write)) {
         has_continue  = true;
         first_write = fw_scope->begin();
         int lr = fw_scope->end();
         if (last_read < lr)
            last_read = lr;
      }

      if ((fw_scope->type() == sct_loop) && (first_read < first_write)) {
         first_write = fw_scope->begin();
         int lr = fw_scope->end();
         if (last_read < lr)
            last_read = lr;
      }

      fw_scope = fw_scope->parent();

      // if the value is conditionally written in a loop
      // then propagate its lifetime to the full loop
      if (fw_scope->type() == sct_loop) {
         if (keep_for_full_loop || (first_read < first_write)) {
            first_write = fw_scope->begin();
            int lr = fw_scope->end();
            if (last_read < lr)
               last_read = lr;
         }
      }

      // if we currently don't propagate the lifetime but
      // the enclosing scope is a conditional within a loop
      // up to the last-read level we need to propagate,
      // todo: to tighten the life time check whether the value
      // is written in all consitional code path below the loop
      if (!keep_for_full_loop &&
          fw_scope->is_conditional() &&
          fw_scope->in_loop()) {
         keep_for_full_loop = true;
      }
   }




   // same level and same range means it is first
   // written and last read in the same scope
   // ignore the case when first read is before
   // first write, because it is undefined behaviour
   if ((lr_scope->begin() == fw_scope->begin()) &&
       (lr_scope->end() == fw_scope->end())) {
      return make_pair(first_write, last_read);
   }

   // different scopes,
   if (!keep_for_full_loop && first_read > first_write) {
      return make_pair(first_write, last_read);
   }else{
      // 1. if the value is not always written in a loop
      // it must be kept for the whole loop scope.
      //
      // 2. if the value is read (maybe conditionally)
      // before it is written first it also must be
      // kept valid for the whole loop
      auto enclosing_loop = lr_scope->get_parent_loop();
      assert(enclosing_loop);
      return make_pair(enclosing_loop->begin(), enclosing_loop->end());
   }
}


void evaluate_remapping(const vector<std::pair<int, int>>& lt, std::vector<rename_reg_pair>& result)
{

   struct out_access_record {
      int end;
      unsigned reg;
      bool tested;
   };

   std::multimap<int, out_access_record> m;
   for (unsigned i = 1; i < lt.size(); ++i) {
      out_access_record oar = {lt[i].second, i, false};
      m.insert(make_pair(lt[i].first, oar));
   }

   while (true) {
      auto trgt = m.begin();

      while ( trgt != m.end() && trgt->second.tested)
         ++trgt;

      if (trgt == m.end())
         break;

      while (trgt != m.end()) {
         auto src = m.upper_bound(trgt->second.end - 1);
         if (src ==  m.end()) {
            trgt->second.tested = true;
         } else {
            result[src->second.reg].new_reg = trgt->second.reg;
            result[src->second.reg].valid = true;
            trgt->second.end = src->second.end;
            m.erase(src);
            break;
         }
         ++trgt;
      }
   }
}
