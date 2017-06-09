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

#include <state_tracker/st_glsl_to_tgsi_temprename.h>
#include <tgsi/tgsi_ureg.h>
#include <tgsi/tgsi_info.h>
#include <compiler/glsl/list.h>
#include <gtest/gtest.h>

using std::vector;
using std::pair;

struct MockCodeline {
   MockCodeline(unsigned  _op): op(_op) {}
   MockCodeline(unsigned _op, const vector<int>& _dst, const vector<int>& _src, const vector<int>&_to):
      op(_op), dst(_dst), src(_src), tex_offsets(_to){}
   unsigned op;
   vector<int> dst;
   vector<int> src;
   vector<int> tex_offsets;
};

const int in0 = 0;
const int in1 = -1;
const int in2 = -2;

const int out0 = 0;
const int out1 = -1;



/*
 * supported opcodes for mock-shaders:
 *
 * Arithmentic
 *
 * TGSI_OPCODE_MOV, TGSI_OPCODE_UADD, TGSI_OPCODE_UMAD
 *
 * Flow control
 * loops:
 * TGSI_OPCODE_BGNLOOP, TGSI_OPCODE_ENDLOOP,
 * TGSI_OPCODE_BRK, TGSI_OPCODE_CONT
 *
 * if/switch:
 * TGSI_OPCODE_IF, TGSI_OPCODE_UIF, TGSI_OPCODE_ELSE, TGSI_OPCODE_ENDIF
 * TGSI_OPCODE_SWITCH, TGSI_OPCODE_CASE, TGSI_OPCODE_DEFAULT
 *
 * TGSI_OPCODE_END
 */


class MockShader {
public:
   MockShader(const vector<MockCodeline>& source);
   ~MockShader();

   void free();

   exec_list* get_program();
   int get_num_temps();
private:
   st_src_reg create_src_register(int src_idx);
   st_dst_reg create_dst_register(int dst_idx);
   exec_list* program;
   int num_temps;
   void *mem_ctx;
};

using expectation = vector<vector<int>>;

MockShader::~MockShader()
{
   free();
   ralloc_free(mem_ctx);
}

int MockShader::get_num_temps()
{
   return num_temps;
}


exec_list* MockShader::get_program()
{
   return program;
}

MockShader::MockShader(const vector<MockCodeline>& source):
   num_temps(0)
{
   mem_ctx = ralloc_context(NULL);

   program = new(mem_ctx) exec_list();

   for (MockCodeline i: source) {
      glsl_to_tgsi_instruction *next_instr = new(mem_ctx) glsl_to_tgsi_instruction();
      next_instr->op = i.op;
      next_instr->info = tgsi_get_opcode_info(i.op);

      assert(i.src.size() < 4);
      assert(i.dst.size() < 3);
      assert(i.tex_offsets.size() < 3);

      for (unsigned  k = 0; k < i.src.size(); ++k) {
         next_instr->src[k] = create_src_register(i.src[k]);
      }
      for (unsigned k = 0; k < i.dst.size(); ++k) {
         next_instr->dst[k] = create_dst_register(i.dst[k]);
      }

      // set texture registers
      next_instr->tex_offset_num_offset = i.tex_offsets.size();
      next_instr->tex_offsets = new st_src_reg[i.tex_offsets.size()];
      for (unsigned k = 0; k < i.tex_offsets.size(); ++k) {
         next_instr->tex_offsets[k] = create_src_register(i.tex_offsets[k]);
      }

      program->push_tail(next_instr);
   }
   ++num_temps;
}

void MockShader::free()
{
   // the list is not fully initialized, so
   // tearing it down also must be done manually.
   exec_node *p;
   while ((p = program->pop_head())) {
      glsl_to_tgsi_instruction * instr = static_cast<glsl_to_tgsi_instruction *>(p);
      if (instr->tex_offset_num_offset > 0)
         delete[] instr->tex_offsets;
      delete p;
   }
   program = 0;
   num_temps = 0;
}

st_src_reg MockShader::create_src_register(int src_idx)
{
   gl_register_file file;
   int idx = 0;
   if (src_idx > 0) {
      file = PROGRAM_TEMPORARY;
      idx = src_idx;
      if (num_temps < idx)
         num_temps = idx;
   } else {
      file = PROGRAM_INPUT;
      idx = -src_idx;
   }
   return st_src_reg(file, idx, GLSL_TYPE_INT);

}

st_dst_reg MockShader::create_dst_register(int dst_idx)
{
   gl_register_file file;
   int idx = 0;
   if (dst_idx > 0) {
      file = PROGRAM_TEMPORARY;
      idx = dst_idx;
      if (num_temps < idx)
         num_temps = idx;
   } else {
      file = PROGRAM_OUTPUT;
      idx = - dst_idx;
   }
   return st_dst_reg(file, 0xF, GLSL_TYPE_INT, idx);
}

/**
  This is a text class to check the exact life times
*/
class LifetimeEvaluatorExactTest : public testing::Test {
protected:
   void run(const vector<MockCodeline>& code, const expectation& e);
};

/**
  This is a text class to check that the life time is at least
  in the expected range
*/
class LifetimeEvaluatorAtLeastTest : public testing::Test {
protected:
   void run(const vector<MockCodeline>& code, const expectation& e);
};


void LifetimeEvaluatorExactTest::run(const vector<MockCodeline>& code, const expectation& e)
{
   MockShader shader(code);

   tgsi_temp_lifetime ana(shader.get_program(), shader.get_num_temps());
   auto lifetimes = ana.get_lifetimes();

   // lifetimes[0] not used, but created for simpler processing
   ASSERT_EQ(lifetimes.size(), e.size());

   for (unsigned i = 1; i < lifetimes.size(); ++i) {
      EXPECT_EQ(lifetimes[i].first, e[i][0]);
      EXPECT_EQ(lifetimes[i].second, e[i][1]);
   }
}

void LifetimeEvaluatorAtLeastTest::run(const vector<MockCodeline>& code, const expectation& e)
{
   MockShader shader(code);

   tgsi_temp_lifetime ana(shader.get_program(), shader.get_num_temps());
   auto lifetimes = ana.get_lifetimes();

   // lifetimes[0] not used, but created for simpler processing
   ASSERT_EQ(lifetimes.size(), e.size());

   for (unsigned i = 1; i < lifetimes.size(); ++i) {
      EXPECT_LE(lifetimes[i].first, e[i][0]);
      EXPECT_GE(lifetimes[i].second, e[i][1]);
   }
}

TEST_F(LifetimeEvaluatorExactTest, SimpleMoveAdd)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV, {1}, {in0}, {}},
      { TGSI_OPCODE_UADD, {out0}, {1, in0},  {}},
      { TGSI_OPCODE_END}
   };
   run(code, expectation({{-1, -1}, {0,1}}));
}


TEST_F(LifetimeEvaluatorExactTest, SimpleMoveAddMove)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV, {1}, {in0}, {}},
      { TGSI_OPCODE_UADD, {2}, {1, in0},  {}},
      { TGSI_OPCODE_MOV, {out0}, {2},  {}},
      { TGSI_OPCODE_END}
   };
   run(code, expectation({{-1, -1}, {0,1}, {1,2}}));
}

TEST_F(LifetimeEvaluatorExactTest, SimpleMoveAddMoveTexoffset)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV, {1}, {in0}, {}},
      { TGSI_OPCODE_MOV, {2}, {in1}, {}},
      { TGSI_OPCODE_UADD, {out0}, {},  {1,2}},
      { TGSI_OPCODE_END}
   };
   run(code, expectation({{-1, -1}, {0,2}, {1,2}}));
}


TEST_F(LifetimeEvaluatorExactTest, SimpleMoveInLoop)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV, {1}, {in0}, {}},      // 0
      { TGSI_OPCODE_BGNLOOP },                 // 1
      { TGSI_OPCODE_UADD, {2}, {1, in0},  {}}, // 2
      { TGSI_OPCODE_UADD, {3}, {1, 2},  {}},   // 3
      { TGSI_OPCODE_UADD, {3}, {3, in1},  {}}, // 4
      { TGSI_OPCODE_ENDLOOP },                 // 5
      { TGSI_OPCODE_MOV, {out0}, {3},  {}},    // 6
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0, 5}, {2,3}, {3, 6}}));
}


// in loop if/else value written only in one path, and read later
//   - value must survive the whole loop
TEST_F(LifetimeEvaluatorExactTest, MoveInIfInLoop)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV, {1}, {in0}, {}},      // 0
      { TGSI_OPCODE_BGNLOOP },                 // 1
      { TGSI_OPCODE_IF},                       // 2
      { TGSI_OPCODE_UADD, {2}, {1, in0},  {}}, // 3
      { TGSI_OPCODE_ENDIF},                    // 4
      { TGSI_OPCODE_UADD, {3}, {1, 2},  {}},   // 5
      { TGSI_OPCODE_UADD, {3}, {3, in1},  {}}, // 6
      { TGSI_OPCODE_ENDLOOP },                 // 7
      { TGSI_OPCODE_MOV, {out0}, {3},  {}},    // 8
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0, 7}, {1,7}, {5, 8}}));
}


// in loop if/else value written in both path, and read later
//   - value must survive from first write to last read in loop
// for now we only check that the minimum life time is correct
TEST_F(LifetimeEvaluatorAtLeastTest, WriteInIfAndElseInLoop)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV, {1}, {in0}, {}},      // 0
      { TGSI_OPCODE_BGNLOOP },                 // 1
      { TGSI_OPCODE_IF},                       // 2
      { TGSI_OPCODE_UADD, {2}, {1, in0},  {}}, // 3
      { TGSI_OPCODE_ELSE },                    // 4
      { TGSI_OPCODE_MOV, {2}, {1},  {}},       // 5
      { TGSI_OPCODE_ENDIF},                    // 6
      { TGSI_OPCODE_UADD, {3}, {1, 2},  {}},   // 7
      { TGSI_OPCODE_UADD, {3}, {3, in1},  {}}, // 8
      { TGSI_OPCODE_ENDLOOP },                 // 9
      { TGSI_OPCODE_MOV, {out0}, {3},  {}},    // 10
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0, 9}, {3,7}, {7, 10}}));
}

// in loop if/else value written in both path, red in else path
// before read and also read later
//   - value must survive from first write to last read in loop
TEST_F(LifetimeEvaluatorExactTest, WriteInIfAndElseReadInElseInLoop)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV, {1}, {in0}, {}},      // 0
      { TGSI_OPCODE_BGNLOOP },                 // 1
      { TGSI_OPCODE_IF},                       // 2
      { TGSI_OPCODE_UADD, {2}, {1, in0},  {}}, // 3
      { TGSI_OPCODE_ELSE },                    // 4
      { TGSI_OPCODE_ADD, {2}, {1, 2},  {}},    // 5
      { TGSI_OPCODE_ENDIF},                    // 6
      { TGSI_OPCODE_UADD, {3}, {1, 2},  {}},   // 7
      { TGSI_OPCODE_UADD, {3}, {3, in1},  {}}, // 8
      { TGSI_OPCODE_ENDLOOP },                 // 9
      { TGSI_OPCODE_MOV, {out0}, {3},  {}},    // 10
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0, 9}, {1,9}, {7, 10}}));
}

// in loop if/else read in one path before written in the same loop
//   - value must survive the whole loop
TEST_F(LifetimeEvaluatorExactTest, ReadInIfInLoopBeforeWrite)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV, {1}, {in0}, {}},      // 0
      { TGSI_OPCODE_BGNLOOP },                 // 1
      { TGSI_OPCODE_IF, {}, {in0},  {}},                       // 2
      { TGSI_OPCODE_UADD, {2}, {1, 3},  {}},   // 3
      { TGSI_OPCODE_ENDIF},                    // 4
      { TGSI_OPCODE_UADD, {3}, {1, 2},  {}},   // 5
      { TGSI_OPCODE_UADD, {3}, {3, in1},  {}}, // 6
      { TGSI_OPCODE_ENDLOOP },                 // 7
      { TGSI_OPCODE_MOV, {out0}, {3},  {}},    // 8
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0, 7}, {1,7}, {1, 8}}));
}

/* Write in nested ifs in loop, for now we do test whether the
 * life time is atleast what is required, but we know that the
 * implementation doesn't do a full check and sets larger boundaries
 */
TEST_F(LifetimeEvaluatorAtLeastTest, NestedIfInLoopAlwaysWriteButNotPropagated)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },                     // 1
      {   TGSI_OPCODE_IF, {}, {in0},  {}},
      {     TGSI_OPCODE_IF, {}, {in0},  {}},
      {       TGSI_OPCODE_MOV, {1}, {in0},  {}},
      {     TGSI_OPCODE_ELSE},
      {       TGSI_OPCODE_MOV, {1}, {in0},  {}},   // 5
      {     TGSI_OPCODE_ENDIF},
      {   TGSI_OPCODE_ELSE},
      {     TGSI_OPCODE_IF, {}, {in0},  {}},
      {       TGSI_OPCODE_MOV, {1}, {in0},  {}},
      {     TGSI_OPCODE_ELSE},                     // 10
      {       TGSI_OPCODE_MOV, {1}, {in0},  {}},
      {     TGSI_OPCODE_ENDIF},
      {   TGSI_OPCODE_ENDIF},
      {   TGSI_OPCODE_MOV, {out0}, {1},  {}},
      { TGSI_OPCODE_ENDLOOP },                    // 15
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{3, 14}}));
}



TEST_F(LifetimeEvaluatorExactTest, NestedIfInLoopWriteNotAlways)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },                     // 0
      {   TGSI_OPCODE_IF, {}, {in0},  {}},
      {     TGSI_OPCODE_IF, {}, {in0},  {}},
      {       TGSI_OPCODE_MOV, {1}, {in0},  {}},
      {     TGSI_OPCODE_ELSE},
      {       TGSI_OPCODE_MOV, {1}, {in0},  {}},   // 5
      {     TGSI_OPCODE_ENDIF},
      {   TGSI_OPCODE_ELSE},
      {     TGSI_OPCODE_IF, {}, {in0},  {}},
      {       TGSI_OPCODE_MOV, {1}, {in0},  {}},
      {     TGSI_OPCODE_ENDIF},                   // 10
      {   TGSI_OPCODE_ENDIF},
      {   TGSI_OPCODE_MOV, {out0}, {1},  {}},
      { TGSI_OPCODE_ENDLOOP },                     // 13
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0, 13}}));
}


// if a continue is in the loop, all variables written after the
// continue and used outside the loop must be maintained for the
// whole loop
TEST_F(LifetimeEvaluatorExactTest, LoopWithWriteAfterContinue)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },                     // 0
      {   TGSI_OPCODE_IF, {}, {in0},  {}},
      {     TGSI_OPCODE_CONT},
      {   TGSI_OPCODE_ENDIF},
      {   TGSI_OPCODE_MOV, {1}, {in0},  {}},
      { TGSI_OPCODE_ENDLOOP },                     // 5
      { TGSI_OPCODE_MOV, {out0}, {1},  {}},        // 6
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0, 6}}));
}

// if a continue is in the loop, all variables written after the
// continue and used outside the loop must be maintained for the
// whole loop, but not further
TEST_F(LifetimeEvaluatorExactTest, NestedLoopWithWriteAfterContinue)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },                     // 0
      {   TGSI_OPCODE_BGNLOOP },                     // 1
      {     TGSI_OPCODE_IF, {}, {in0},  {}},
      {       TGSI_OPCODE_CONT},
      {     TGSI_OPCODE_ENDIF},
      {     TGSI_OPCODE_MOV, {1}, {in0},  {}},
      {   TGSI_OPCODE_ENDLOOP },                     // 6
      {   TGSI_OPCODE_MOV, {out0}, {1},  {}},        // 7
      { TGSI_OPCODE_ENDLOOP },                     // 6
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{1, 7}}));
}

// if a continue is in the loop, all variables written after the
// continue and used outside the loop must be maintained for all
// loops up untto the read scope, but not further
TEST_F(LifetimeEvaluatorExactTest, Nested2LoopWithWriteAfterContinue)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },                     // 0
      {   TGSI_OPCODE_BGNLOOP },                     // 1
      {     TGSI_OPCODE_BGNLOOP },                   // 2
      {       TGSI_OPCODE_IF, {}, {in0},  {}},
      {         TGSI_OPCODE_CONT},
      {       TGSI_OPCODE_ENDIF},
      {       TGSI_OPCODE_MOV, {1}, {in0},  {}},
      {     TGSI_OPCODE_ENDLOOP },
      {   TGSI_OPCODE_ENDLOOP },
      {   TGSI_OPCODE_MOV, {out0}, {1},  {}},        // 9
      { TGSI_OPCODE_ENDLOOP },                     // 6
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{1, 9}}));
}

TEST_F(LifetimeEvaluatorExactTest, LoopWithWriteInSwitch)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },
      {   TGSI_OPCODE_SWITCH, {}, {in0},  {} },
      {    TGSI_OPCODE_CASE, {}, {in0},  {} },
      {     TGSI_OPCODE_MOV, {1}, {in0},  {}},
      {    TGSI_OPCODE_BRK },
      {   TGSI_OPCODE_DEFAULT },
      {   TGSI_OPCODE_BRK },
      {   TGSI_OPCODE_ENDSWITCH },
      {   TGSI_OPCODE_MOV, {out0}, {1},  {}},
      { TGSI_OPCODE_ENDLOOP },
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0, 9}}));
}

// value written in one case, and read in other, in loop
//  - must survive the loop
TEST_F(LifetimeEvaluatorExactTest, LoopWithReadWriteInSwitchDifferentCase)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },                     // 0
      {   TGSI_OPCODE_SWITCH, {}, {in0},  {} },                     // 0
      {    TGSI_OPCODE_CASE, {}, {in0},  {} },                     // 0
      {     TGSI_OPCODE_MOV, {1}, {in0},  {}},
      {    TGSI_OPCODE_BRK },                     // 0
      {   TGSI_OPCODE_DEFAULT },                     // 0
      {     TGSI_OPCODE_MOV, {out0}, {1},  {}},
      {   TGSI_OPCODE_BRK },                     // 0
      {   TGSI_OPCODE_ENDSWITCH },                     // 0
      { TGSI_OPCODE_ENDLOOP },                     // 6
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0, 9}}));
}


TEST_F(LifetimeEvaluatorExactTest, LoopRWInSwitchCaseLastCaseWithoutBreak)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },                     // 0
      {   TGSI_OPCODE_SWITCH, {}, {in0},  {} },                     // 0
      {    TGSI_OPCODE_CASE, {}, {in0},  {} },                     // 0
      {     TGSI_OPCODE_MOV, {1}, {in0},  {}},
      {    TGSI_OPCODE_BRK },                     // 0
      {   TGSI_OPCODE_DEFAULT },                     // 0
      {     TGSI_OPCODE_MOV, {out0}, {1},  {}},
      {   TGSI_OPCODE_ENDSWITCH },                     // 0
      { TGSI_OPCODE_ENDLOOP },                     // 6
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0, 8}}));
}


// value read/write in same case, stays there


TEST_F(LifetimeEvaluatorExactTest, LoopWithReadWriteInSwitchSameCase)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },                     // 0
      {   TGSI_OPCODE_SWITCH, {}, {in0},  {} },                     // 0
      {    TGSI_OPCODE_CASE, {}, {in0},  {} },                     // 0
      {     TGSI_OPCODE_MOV, {1}, {in0},  {}},
      {     TGSI_OPCODE_MOV, {out0}, {1},  {}},
      {    TGSI_OPCODE_BRK },                     // 0
      {   TGSI_OPCODE_DEFAULT },                     // 0
      {   TGSI_OPCODE_BRK },                     // 0
      {   TGSI_OPCODE_ENDSWITCH },                     // 0
      { TGSI_OPCODE_ENDLOOP },                     // 6
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{3,4}}));
}

// value read/write in all cases, should only live from first
// write to last read, but currently the whole loop is used.
TEST_F(LifetimeEvaluatorAtLeastTest, LoopWithReadWriteInSwitchSameCase)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },                     // 0
      {   TGSI_OPCODE_SWITCH, {}, {in0},  {}},                     // 0
      {    TGSI_OPCODE_CASE, {}, {in0},  {} },                     // 0
      {     TGSI_OPCODE_MOV, {}, {in0},  {}},
      {    TGSI_OPCODE_BRK },                     // 0
      {   TGSI_OPCODE_DEFAULT },                     // 0
      {     TGSI_OPCODE_MOV, {1}, {in0},  {}},
      {   TGSI_OPCODE_BRK },                     // 0
      {   TGSI_OPCODE_ENDSWITCH },                     // 0
      {   TGSI_OPCODE_MOV, {out0}, {1},  {}},
      { TGSI_OPCODE_ENDLOOP },                     // 6
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{3,9}}));
}

// value written in one case, and read in other, in loop, may fall through
//  - must survive the loop

// value read/write in differnt loops
TEST_F(LifetimeEvaluatorExactTest, LoopsWithDifferntScopes)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },                     // 0
      {     TGSI_OPCODE_MOV, {1}, {in0},  {}},     // 1
      { TGSI_OPCODE_ENDLOOP },                     // 2
      { TGSI_OPCODE_BGNLOOP },                     // 3
      {     TGSI_OPCODE_MOV, {out0}, {1},  {}},    // 4
      { TGSI_OPCODE_ENDLOOP },                     // 5
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{1,5}}));
}

// value read/write in differnt loops
TEST_F(LifetimeEvaluatorExactTest, LoopsWithDifferntScopesConditionalWrite)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },                     // 0
      {  TGSI_OPCODE_IF, {}, {in0},  {}},
      {     TGSI_OPCODE_MOV, {1}, {in0},  {}},     // 3
      {   TGSI_OPCODE_ENDIF},     // 1
      { TGSI_OPCODE_ENDLOOP },                     // 5
      { TGSI_OPCODE_BGNLOOP },                     // 6
      {     TGSI_OPCODE_MOV, {out0}, {1},  {}},    // 7
      { TGSI_OPCODE_ENDLOOP },                     // 5
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0,7}}));
}

// first read before first write wiredness with nested loops
TEST_F(LifetimeEvaluatorExactTest, LoopsWithDifferntScopesConditionalReadBeforeWrite)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_BGNLOOP },                     // 0
      {   TGSI_OPCODE_BGNLOOP },                   // 1
      {    TGSI_OPCODE_IF, {}, {in0},  {}},                        // 2
      {     TGSI_OPCODE_MOV, {out0}, {1},  {}},    // 3
      {    TGSI_OPCODE_ENDIF},                     // 4
      {   TGSI_OPCODE_ENDLOOP },                   // 5
      {   TGSI_OPCODE_BGNLOOP },                   // 6
      {     TGSI_OPCODE_MOV, {1}, {in0},  {}},     // 7
      {   TGSI_OPCODE_ENDLOOP },                   // 8
      { TGSI_OPCODE_ENDLOOP },                     // 9
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0,9}}));
}

// register is only written. This should not happen,
// but to handle the case we want the register to life
// at least one instruction
TEST_F(LifetimeEvaluatorExactTest, WriteOnly)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV, {1}, {in0},  {}},    // 3
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0,0}}));
}

// register read in if
TEST_F(LifetimeEvaluatorExactTest, SimpleReadForIf)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV, {1}, {in0},  {}},    // 3
      { TGSI_OPCODE_ADD, {out0}, {in0, in1},  {}},    // 3
      { TGSI_OPCODE_IF, {}, {1}, {}},
      { TGSI_OPCODE_ENDIF}
   };
   run (code, expectation({{-1,-1},{0,2}}));
}

// register read in switch
TEST_F(LifetimeEvaluatorExactTest, SimpleReadForSwitchAndCase)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV, {1}, {in0},  {}},    // 3
      { TGSI_OPCODE_SWITCH, {}, {1}, {}},
      { TGSI_OPCODE_CASE, {}, {1}, {}},
      { TGSI_OPCODE_CASE, {}, {1}, {}},
      { TGSI_OPCODE_END, {}, {1}, {}},
   };
   run (code, expectation({{-1,-1},{0,3}}));
}

TEST_F(LifetimeEvaluatorExactTest, DistinceScopesAndNoEndProgramId)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV, {1}, {in0},  {}},    // 3
      { TGSI_OPCODE_IF, {}, {1}, {}},
      { TGSI_OPCODE_MOV, {2}, {1}, {}},
      { TGSI_OPCODE_ENDIF},
      { TGSI_OPCODE_IF, {}, {1}, {}},
      { TGSI_OPCODE_MOV, {out0}, {2}, {}},
      { TGSI_OPCODE_ENDIF},

   };
   run (code, expectation({{-1,-1},{0,4}, {2,5}}));
}

/* Check that two destination registers are used
*/
TEST_F(LifetimeEvaluatorExactTest, TwoDestRegisters)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_DFRACEXP , {1,2}, {in0},  {}},    // 3
      { TGSI_OPCODE_ADD, {out0}, {1,2}, {}},
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0,1}, {0,1}}));
}

/* Check that two destination registers are used
*/
TEST_F(LifetimeEvaluatorExactTest, ThreeSourceRegisters)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_DFRACEXP , {1,2}, {in0},  {}},    // 3
      { TGSI_OPCODE_ADD , {3}, {in0, in1},  {}},    // 3
      { TGSI_OPCODE_MAD, {out0}, {1,2, 3}, {}},
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0,2}, {0,2}, {1,2}}));
}

/* Check that two destination registers are used
*/
TEST_F(LifetimeEvaluatorExactTest, OverwriteWrittenOnlyTemps)
{
   const vector<MockCodeline> code = {
      { TGSI_OPCODE_MOV , {1}, {in0},  {}},    // 3
      { TGSI_OPCODE_MOV , {2}, {in1},  {}},    // 3
      { TGSI_OPCODE_END}
   };
   run (code, expectation({{-1,-1},{0,0}, {1,1}}));
}


TEST(RegisterRemapping, RegisterRemapping)
{
   rename_reg_pair proto{false, 0};
   vector<rename_reg_pair> result(7, proto);

   vector<pair<int, int>> lt({{-1,-1},
                             {0, 1}, // 1
                             {0, 2}, // 2
                             {1, 2}, // 3
                             {2, 10}, // 4
                             {3, 5},  // 5
                             {5, 10}  // 6
                            });



   evaluate_remapping(lt, result);

   vector<int> remap({0,1, 2, 3, 4, 5, 6});

   std::transform(remap.begin(), remap.end(), result.begin(), remap.begin(),
                  [](int x, const rename_reg_pair& rn) {return rn.valid ? rn.new_reg : x;});

   vector<int> expect({0, 1, 2, 1, 1, 2, 2});

   for(unsigned  i = 1; i < remap.size(); ++i) {
      EXPECT_EQ(remap[i], expect[i]);
   }

}


TEST(RegisterRemapping, RegisterRemapping2)
{
   rename_reg_pair proto{false, 0};
   vector<rename_reg_pair> result(7, proto);

   vector<pair<int, int>> lt({{-1,-1},
                             {0, 1}, // 1
                             {0, 2}, // 2
                             {3, 3}, // 3
                             {4, 4}, // 4
                            });



   evaluate_remapping(lt, result);

   vector<int> remap({0, 1, 2, 3, 4});

   std::transform(remap.begin(), remap.end(), result.begin(), remap.begin(),
                  [](int x, const rename_reg_pair& rn) {return rn.valid ? rn.new_reg : x;});

   vector<int> expect({0, 1, 2, 1, 1});

   for(unsigned  i = 1; i < remap.size(); ++i) {
      EXPECT_EQ(remap[i], expect[i]);
   }

}
