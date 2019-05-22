/*
 * Copyright (C) 2018-2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* mir_is_live_after performs liveness analysis on the MIR, used primarily
 * as part of register allocation. TODO: Algorithmic improvements for
 * compiler performance (this is the worst algorithm possible -- see
 * backlog with Connor on IRC) */

#include "compiler.h"

static bool
midgard_is_live_in_instr(midgard_instruction *ins, int src)
{
        if (ins->ssa_args.src0 == src) return true;
        if (ins->ssa_args.src1 == src) return true;

        return false;
}

/* Determine if a variable is live in the successors of a block */
static bool
is_live_after_successors(compiler_context *ctx, midgard_block *bl, int src)
{
        for (unsigned i = 0; i < bl->nr_successors; ++i) {
                midgard_block *succ = bl->successors[i];

                /* If we already visited, the value we're seeking
                 * isn't down this path (or we would have short
                 * circuited */

                if (succ->visited) continue;

                /* Otherwise (it's visited *now*), check the block */

                succ->visited = true;

                mir_foreach_instr_in_block(succ, ins) {
                        if (midgard_is_live_in_instr(ins, src))
                                return true;
                }

                /* ...and also, check *its* successors */
                if (is_live_after_successors(ctx, succ, src))
                        return true;

        }

        /* Welp. We're really not live. */

        return false;
}

bool
mir_is_live_after(compiler_context *ctx, midgard_block *block, midgard_instruction *start, int src)
{
        /* Check the rest of the block for liveness */

        mir_foreach_instr_in_block_from(block, ins, mir_next_op(start)) {
                if (midgard_is_live_in_instr(ins, src))
                        return true;
        }

        /* Check the rest of the blocks for liveness recursively */

        bool succ = is_live_after_successors(ctx, block, src);

        mir_foreach_block(ctx, block) {
                block->visited = false;
        }

        return succ;
}
