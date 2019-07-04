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

#include "compiler.h"
#include "midgard_ops.h"
#include "util/u_memory.h"

/* Create a mask of accessed components from a swizzle to figure out vector
 * dependencies */

static unsigned
swizzle_to_access_mask(unsigned swizzle)
{
        unsigned component_mask = 0;

        for (int i = 0; i < 4; ++i) {
                unsigned c = (swizzle >> (2 * i)) & 3;
                component_mask |= (1 << c);
        }

        return component_mask;
}

/* Does the mask cover more than a scalar? */

static bool
is_single_component_mask(unsigned mask)
{
        int components = 0;

        for (int c = 0; c < 4; ++c)
                if (mask & (3 << (2 * c)))
                        components++;

        return components == 1;
}

/* Checks for an SSA data hazard between two adjacent instructions, keeping in
 * mind that we are a vector architecture and we can write to different
 * components simultaneously */

static bool
can_run_concurrent_ssa(midgard_instruction *first, midgard_instruction *second)
{
        /* Each instruction reads some registers and writes to a register. See
         * where the first writes */

        /* Figure out where exactly we wrote to */
        int source = first->ssa_args.dest;
        int source_mask = first->type == TAG_ALU_4 ? squeeze_writemask(first->alu.mask) : 0xF;

        /* As long as the second doesn't read from the first, we're okay */
        if (second->ssa_args.src0 == source) {
                if (first->type == TAG_ALU_4) {
                        /* Figure out which components we just read from */

                        int q = second->alu.src1;
                        midgard_vector_alu_src *m = (midgard_vector_alu_src *) &q;

                        /* Check if there are components in common, and fail if so */
                        if (swizzle_to_access_mask(m->swizzle) & source_mask)
                                return false;
                } else
                        return false;

        }

        if (second->ssa_args.src1 == source)
                return false;

        /* Otherwise, it's safe in that regard. Another data hazard is both
         * writing to the same place, of course */

        if (second->ssa_args.dest == source) {
                /* ...but only if the components overlap */
                int dest_mask = second->type == TAG_ALU_4 ? squeeze_writemask(second->alu.mask) : 0xF;

                if (dest_mask & source_mask)
                        return false;
        }

        /* ...That's it */
        return true;
}

static bool
midgard_has_hazard(
                midgard_instruction **segment, unsigned segment_size,
                midgard_instruction *ains)
{
        for (int s = 0; s < segment_size; ++s)
                if (!can_run_concurrent_ssa(segment[s], ains))
                        return true;

        return false;


}

/* Schedules, but does not emit, a single basic block. After scheduling, the
 * final tag and size of the block are known, which are necessary for branching
 * */

static midgard_bundle
schedule_bundle(compiler_context *ctx, midgard_block *block, midgard_instruction *ins, int *skip)
{
        int instructions_emitted = 0, packed_idx = 0;
        midgard_bundle bundle = { 0 };

        uint8_t tag = ins->type;

        /* Default to the instruction's tag */
        bundle.tag = tag;

        switch (ins->type) {
        case TAG_ALU_4: {
                uint32_t control = 0;
                size_t bytes_emitted = sizeof(control);

                /* TODO: Constant combining */
                int index = 0, last_unit = 0;

                /* Previous instructions, for the purpose of parallelism */
                midgard_instruction *segment[4] = {0};
                int segment_size = 0;

                instructions_emitted = -1;
                midgard_instruction *pins = ins;

                unsigned constant_count = 0;

                for (;;) {
                        midgard_instruction *ains = pins;

                        /* Advance instruction pointer */
                        if (index) {
                                ains = mir_next_op(pins);
                                pins = ains;
                        }

                        /* Out-of-work condition */
                        if ((struct list_head *) ains == &block->instructions)
                                break;

                        /* Ensure that the chain can continue */
                        if (ains->type != TAG_ALU_4) break;

                        /* If there's already something in the bundle and we
                         * have weird scheduler constraints, break now */
                        if (ains->precede_break && index) break;

                        /* According to the presentation "The ARM
                         * Mali-T880 Mobile GPU" from HotChips 27,
                         * there are two pipeline stages. Branching
                         * position determined experimentally. Lines
                         * are executed in parallel:
                         *
                         * [ VMUL ] [ SADD ]
                         * [ VADD ] [ SMUL ] [ LUT ] [ BRANCH ]
                         *
                         * Verify that there are no ordering dependencies here.
                         *
                         * TODO: Allow for parallelism!!!
                         */

                        /* Pick a unit for it if it doesn't force a particular unit */

                        int unit = ains->unit;

                        if (!unit) {
                                int op = ains->alu.op;
                                int units = alu_opcode_props[op].props;

                                bool vectorable = units & UNITS_ANY_VECTOR;
                                bool scalarable = units & UNITS_SCALAR;
                                bool could_scalar = is_single_component_mask(ains->alu.mask);
                                bool vector = vectorable && !(could_scalar && scalarable);

                                if (!vector)
                                        assert(units & UNITS_SCALAR);

                                if (vector) {
                                       if (last_unit >= UNIT_VADD) {
                                                if (units & UNIT_VLUT)
                                                        unit = UNIT_VLUT;
                                                else
                                                        break;
                                        } else {
                                                if ((units & UNIT_VMUL) && !(control & UNIT_VMUL))
                                                        unit = UNIT_VMUL;
                                                else if ((units & UNIT_VADD) && !(control & UNIT_VADD))
                                                        unit = UNIT_VADD;
                                                else if (units & UNIT_VLUT)
                                                        unit = UNIT_VLUT;
                                                else
                                                        break;
                                        }
                                } else {
                                        if (last_unit >= UNIT_VADD) {
                                                if ((units & UNIT_SMUL) && !(control & UNIT_SMUL))
                                                        unit = UNIT_SMUL;
                                                else if (units & UNIT_VLUT)
                                                        unit = UNIT_VLUT;
                                                else
                                                        break;
                                        } else {
                                                if ((units & UNIT_SADD) && !(control & UNIT_SADD) && !midgard_has_hazard(segment, segment_size, ains))
                                                        unit = UNIT_SADD;
                                                else if (units & UNIT_SMUL)
                                                        unit = ((units & UNIT_VMUL) && !(control & UNIT_VMUL)) ? UNIT_VMUL : UNIT_SMUL;
                                                else if ((units & UNIT_VADD) && !(control & UNIT_VADD))
                                                        unit = UNIT_VADD;
                                                else
                                                        break;
                                        }
                                }

                                assert(unit & units);
                        }

                        /* Late unit check, this time for encoding (not parallelism) */
                        if (unit <= last_unit) break;

                        /* Clear the segment */
                        if (last_unit < UNIT_VADD && unit >= UNIT_VADD)
                                segment_size = 0;

                        if (midgard_has_hazard(segment, segment_size, ains))
                                break;

                        /* We're good to go -- emit the instruction */
                        ains->unit = unit;

                        segment[segment_size++] = ains;

                        /* We try to reuse constants if possible, by adjusting
                         * the swizzle */

                        if (ains->has_blend_constant) {
                                bundle.has_blend_constant = 1;
                                bundle.has_embedded_constants = 1;
                        } else if (ains->has_constants) {
                                /* By definition, blend constants conflict with
                                 * everything, so if there are already
                                 * constants we break the bundle *now* */

                                if (bundle.has_blend_constant)
                                        break;

                                /* For anything but blend constants, we can do
                                 * proper analysis, however */

                                /* TODO: Mask by which are used */
                                uint32_t *constants = (uint32_t *) ains->constants;
                                uint32_t *bundles = (uint32_t *) bundle.constants;

                                uint32_t indices[4] = { 0 };
                                bool break_bundle = false;

                                for (unsigned i = 0; i < 4; ++i) {
                                        uint32_t cons = constants[i];
                                        bool constant_found = false;

                                        /* Search for the constant */
                                        for (unsigned j = 0; j < constant_count; ++j) {
                                                if (bundles[j] != cons)
                                                        continue;

                                                /* We found it, reuse */
                                                indices[i] = j;
                                                constant_found = true;
                                                break;
                                        }

                                        if (constant_found)
                                                continue;

                                        /* We didn't find it, so allocate it */
                                        unsigned idx = constant_count++;

                                        if (idx >= 4) {
                                                /* Uh-oh, out of space */
                                                break_bundle = true;
                                                break;
                                        }

                                        /* We have space, copy it in! */
                                        bundles[idx] = cons;
                                        indices[i] = idx;
                                }

                                if (break_bundle)
                                        break;

                                /* Cool, we have it in. So use indices as a
                                 * swizzle */

                                unsigned swizzle = SWIZZLE_FROM_ARRAY(indices);
                                unsigned r_constant = SSA_FIXED_REGISTER(REGISTER_CONSTANT);

                                if (ains->ssa_args.src0 == r_constant)
                                        ains->alu.src1 = vector_alu_apply_swizzle(ains->alu.src1, swizzle);

                                if (ains->ssa_args.src1 == r_constant)
                                        ains->alu.src2 = vector_alu_apply_swizzle(ains->alu.src2, swizzle);

                                bundle.has_embedded_constants = true;
                        }

                        if (ains->unit & UNITS_ANY_VECTOR) {
                                bytes_emitted += sizeof(midgard_reg_info);
                                bytes_emitted += sizeof(midgard_vector_alu);
                        } else if (ains->compact_branch) {
                                /* All of r0 has to be written out along with
                                 * the branch writeout */

                                if (ains->writeout) {
                                        /* The rules for when "bare" writeout
                                         * is safe are when all components are
                                         * r0 are written out in the final
                                         * bundle, earlier than VLUT, where any
                                         * register dependencies of r0 are from
                                         * an earlier bundle. We can't verify
                                         * this before RA, so we don't try. */

                                        if (index != 0)
                                                break;

                                        /* Inject a move */
                                        midgard_instruction ins = v_mov(0, blank_alu_src, SSA_FIXED_REGISTER(0));
                                        ins.unit = UNIT_VMUL;
                                        control |= ins.unit;

                                        /* TODO don't leak */
                                        midgard_instruction *move =
                                                mem_dup(&ins, sizeof(midgard_instruction));
                                        bytes_emitted += sizeof(midgard_reg_info);
                                        bytes_emitted += sizeof(midgard_vector_alu);
                                        bundle.instructions[packed_idx++] = move;
                                }

                                if (ains->unit == ALU_ENAB_BRANCH) {
                                        bytes_emitted += sizeof(midgard_branch_extended);
                                } else {
                                        bytes_emitted += sizeof(ains->br_compact);
                                }
                        } else {
                                bytes_emitted += sizeof(midgard_reg_info);
                                bytes_emitted += sizeof(midgard_scalar_alu);
                        }

                        /* Defer marking until after writing to allow for break */
                        control |= ains->unit;
                        last_unit = ains->unit;
                        ++instructions_emitted;
                        ++index;
                }

                int padding = 0;

                /* Pad ALU op to nearest word */

                if (bytes_emitted & 15) {
                        padding = 16 - (bytes_emitted & 15);
                        bytes_emitted += padding;
                }

                /* Constants must always be quadwords */
                if (bundle.has_embedded_constants)
                        bytes_emitted += 16;

                /* Size ALU instruction for tag */
                bundle.tag = (TAG_ALU_4) + (bytes_emitted / 16) - 1;
                bundle.padding = padding;
                bundle.control = bundle.tag | control;

                break;
        }

        case TAG_LOAD_STORE_4: {
                /* Load store instructions have two words at once. If
                 * we only have one queued up, we need to NOP pad.
                 * Otherwise, we store both in succession to save space
                 * and cycles -- letting them go in parallel -- skip
                 * the next. The usefulness of this optimisation is
                 * greatly dependent on the quality of the instruction
                 * scheduler.
                 */

                midgard_instruction *next_op = mir_next_op(ins);

                if ((struct list_head *) next_op != &block->instructions && next_op->type == TAG_LOAD_STORE_4) {
                        /* TODO: Concurrency check */
                        instructions_emitted++;
                }

                break;
        }

        case TAG_TEXTURE_4: {
                /* Which tag we use depends on the shader stage */
                bool in_frag = ctx->stage == MESA_SHADER_FRAGMENT;
                bundle.tag = in_frag ? TAG_TEXTURE_4 : TAG_TEXTURE_4_VTX;
                break;
        }

        default:
                unreachable("Unknown tag");
                break;
        }

        /* Copy the instructions into the bundle */
        bundle.instruction_count = instructions_emitted + 1 + packed_idx;

        midgard_instruction *uins = ins;
        for (; packed_idx < bundle.instruction_count; ++packed_idx) {
                bundle.instructions[packed_idx] = uins;
                uins = mir_next_op(uins);
        }

        *skip = instructions_emitted;

        return bundle;
}

/* Schedule a single block by iterating its instruction to create bundles.
 * While we go, tally about the bundle sizes to compute the block size. */

static void
schedule_block(compiler_context *ctx, midgard_block *block)
{
        util_dynarray_init(&block->bundles, NULL);

        block->quadword_count = 0;

        mir_foreach_instr_in_block(block, ins) {
                int skip;
                midgard_bundle bundle = schedule_bundle(ctx, block, ins, &skip);
                util_dynarray_append(&block->bundles, midgard_bundle, bundle);

                if (bundle.has_blend_constant) {
                        /* TODO: Multiblock? */
                        int quadwords_within_block = block->quadword_count + quadword_size(bundle.tag) - 1;
                        ctx->blend_constant_offset = quadwords_within_block * 0x10;
                }

                while(skip--)
                        ins = mir_next_op(ins);

                block->quadword_count += quadword_size(bundle.tag);
        }

        block->is_scheduled = true;
}

void
schedule_program(compiler_context *ctx)
{
        /* We run RA prior to scheduling */

        mir_foreach_block(ctx, block) {
                schedule_block(ctx, block);
        }

        /* Pipeline registers creation is a prepass before RA */
        mir_create_pipeline_registers(ctx);

        struct ra_graph *g = allocate_registers(ctx);
        install_registers(ctx, g);
}
