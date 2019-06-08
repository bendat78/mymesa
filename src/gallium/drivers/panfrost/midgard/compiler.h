/*
 * Copyright (C) 2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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

#ifndef _MDG_COMPILER_H
#define _MDG_COMPILER_H

#include "midgard.h"
#include "helpers.h"
#include "midgard_compile.h"

#include "util/hash_table.h"
#include "util/u_dynarray.h"
#include "util/set.h"
#include "util/list.h"

#include "main/mtypes.h"
#include "compiler/nir_types.h"
#include "compiler/nir/nir.h"

/* Forward declare */
struct midgard_block;

/* Target types. Defaults to TARGET_GOTO (the type corresponding directly to
 * the hardware), hence why that must be zero. TARGET_DISCARD signals this
 * instruction is actually a discard op. */

#define TARGET_GOTO 0
#define TARGET_BREAK 1
#define TARGET_CONTINUE 2
#define TARGET_DISCARD 3

typedef struct midgard_branch {
        /* If conditional, the condition is specified in r31.w */
        bool conditional;

        /* For conditionals, if this is true, we branch on FALSE. If false, we  branch on TRUE. */
        bool invert_conditional;

        /* Branch targets: the start of a block, the start of a loop (continue), the end of a loop (break). Value is one of TARGET_ */
        unsigned target_type;

        /* The actual target */
        union {
                int target_block;
                int target_break;
                int target_continue;
        };
} midgard_branch;

/* Instruction arguments represented as block-local SSA indices, rather than
 * registers. Negative values mean unused. */

typedef struct {
        int src0;
        int src1;
        int dest;

        /* src1 is -not- SSA but instead a 16-bit inline constant to be smudged
         * in. Only valid for ALU ops. */
        bool inline_constant;
} ssa_args;

/* Generic in-memory data type repesenting a single logical instruction, rather
 * than a single instruction group. This is the preferred form for code gen.
 * Multiple midgard_insturctions will later be combined during scheduling,
 * though this is not represented in this structure.  Its format bridges
 * the low-level binary representation with the higher level semantic meaning.
 *
 * Notably, it allows registers to be specified as block local SSA, for code
 * emitted before the register allocation pass.
 */

typedef struct midgard_instruction {
        /* Must be first for casting */
        struct list_head link;

        unsigned type; /* ALU, load/store, texture */

        /* If the register allocator has not run yet... */
        ssa_args ssa_args;

        /* Special fields for an ALU instruction */
        midgard_reg_info registers;

        /* I.e. (1 << alu_bit) */
        int unit;

        /* When emitting bundle, should this instruction have a break forced
         * before it? Used for r31 writes which are valid only within a single
         * bundle and *need* to happen as early as possible... this is a hack,
         * TODO remove when we have a scheduler */
        bool precede_break;

        bool has_constants;
        float constants[4];
        uint16_t inline_constant;
        bool has_blend_constant;

        bool compact_branch;
        bool writeout;
        bool prepacked_branch;

        union {
                midgard_load_store_word load_store;
                midgard_vector_alu alu;
                midgard_texture_word texture;
                midgard_branch_extended branch_extended;
                uint16_t br_compact;

                /* General branch, rather than packed br_compact. Higher level
                 * than the other components */
                midgard_branch branch;
        };
} midgard_instruction;

typedef struct midgard_block {
        /* Link to next block. Must be first for mir_get_block */
        struct list_head link;

        /* List of midgard_instructions emitted for the current block */
        struct list_head instructions;

        bool is_scheduled;

        /* List of midgard_bundles emitted (after the scheduler has run) */
        struct util_dynarray bundles;

        /* Number of quadwords _actually_ emitted, as determined after scheduling */
        unsigned quadword_count;

        /* Successors: always one forward (the block after us), maybe
         * one backwards (for a backward branch). No need for a second
         * forward, since graph traversal would get there eventually
         * anyway */
        struct midgard_block *successors[2];
        unsigned nr_successors;

        /* The successors pointer form a graph, and in the case of
         * complex control flow, this graph has a cycles. To aid
         * traversal during liveness analysis, we have a visited?
         * boolean for passes to use as they see fit, provided they
         * clean up later */
        bool visited;
} midgard_block;

typedef struct midgard_bundle {
        /* Tag for the overall bundle */
        int tag;

        /* Instructions contained by the bundle */
        int instruction_count;
        midgard_instruction *instructions[5];

        /* Bundle-wide ALU configuration */
        int padding;
        int control;
        bool has_embedded_constants;
        float constants[4];
        bool has_blend_constant;
} midgard_bundle;

typedef struct compiler_context {
        nir_shader *nir;
        gl_shader_stage stage;

        /* Is internally a blend shader? Depends on stage == FRAGMENT */
        bool is_blend;

        /* Tracking for blend constant patching */
        int blend_constant_offset;

        /* Current NIR function */
        nir_function *func;

        /* Unordered list of midgard_blocks */
        int block_count;
        struct list_head blocks;

        midgard_block *initial_block;
        midgard_block *previous_source_block;
        midgard_block *final_block;

        /* List of midgard_instructions emitted for the current block */
        midgard_block *current_block;

        /* The current "depth" of the loop, for disambiguating breaks/continues
         * when using nested loops */
        int current_loop_depth;

        /* Constants which have been loaded, for later inlining */
        struct hash_table_u64 *ssa_constants;

        /* SSA values / registers which have been aliased. Naively, these
         * demand a fmov output; instead, we alias them in a later pass to
         * avoid the wasted op.
         *
         * A note on encoding: to avoid dynamic memory management here, rather
         * than ampping to a pointer, we map to the source index; the key
         * itself is just the destination index. */

        struct hash_table_u64 *ssa_to_alias;
        struct set *leftover_ssa_to_alias;

        /* Actual SSA-to-register for RA */
        struct hash_table_u64 *ssa_to_register;

        /* Mapping of hashes computed from NIR indices to the sequential temp indices ultimately used in MIR */
        struct hash_table_u64 *hash_to_temp;
        int temp_count;
        int max_hash;

        /* Just the count of the max register used. Higher count => higher
         * register pressure */
        int work_registers;

        /* Used for cont/last hinting. Increase when a tex op is added.
         * Decrease when a tex op is removed. */
        int texture_op_count;

        /* Mapping of texture register -> SSA index for unaliasing */
        int texture_index[2];

        /* If any path hits a discard instruction */
        bool can_discard;

        /* The number of uniforms allowable for the fast path */
        int uniform_cutoff;

        /* Count of instructions emitted from NIR overall, across all blocks */
        int instruction_count;

        /* Alpha ref value passed in */
        float alpha_ref;

        /* The index corresponding to the fragment output */
        unsigned fragment_output;

        /* The mapping of sysvals to uniforms, the count, and the off-by-one inverse */
        unsigned sysvals[MAX_SYSVAL_COUNT];
        unsigned sysval_count;
        struct hash_table_u64 *sysval_to_id;
} compiler_context;

/* Helpers for manipulating the above structures (forming the driver IR) */

/* Append instruction to end of current block */

static inline midgard_instruction *
mir_upload_ins(struct midgard_instruction ins)
{
        midgard_instruction *heap = malloc(sizeof(ins));
        memcpy(heap, &ins, sizeof(ins));
        return heap;
}

static inline void
emit_mir_instruction(struct compiler_context *ctx, struct midgard_instruction ins)
{
        list_addtail(&(mir_upload_ins(ins))->link, &ctx->current_block->instructions);
}

static inline void
mir_insert_instruction_before(struct midgard_instruction *tag, struct midgard_instruction ins)
{
        list_addtail(&(mir_upload_ins(ins))->link, &tag->link);
}

static inline void
mir_remove_instruction(struct midgard_instruction *ins)
{
        list_del(&ins->link);
}

static inline midgard_instruction*
mir_prev_op(struct midgard_instruction *ins)
{
        return list_last_entry(&(ins->link), midgard_instruction, link);
}

static inline midgard_instruction*
mir_next_op(struct midgard_instruction *ins)
{
        return list_first_entry(&(ins->link), midgard_instruction, link);
}

#define mir_foreach_block(ctx, v) \
        list_for_each_entry(struct midgard_block, v, &ctx->blocks, link) 

#define mir_foreach_block_from(ctx, from, v) \
        list_for_each_entry_from(struct midgard_block, v, from, &ctx->blocks, link)

#define mir_foreach_instr(ctx, v) \
        list_for_each_entry(struct midgard_instruction, v, &ctx->current_block->instructions, link) 

#define mir_foreach_instr_safe(ctx, v) \
        list_for_each_entry_safe(struct midgard_instruction, v, &ctx->current_block->instructions, link) 

#define mir_foreach_instr_in_block(block, v) \
        list_for_each_entry(struct midgard_instruction, v, &block->instructions, link) 

#define mir_foreach_instr_in_block_safe(block, v) \
        list_for_each_entry_safe(struct midgard_instruction, v, &block->instructions, link) 

#define mir_foreach_instr_in_block_safe_rev(block, v) \
        list_for_each_entry_safe_rev(struct midgard_instruction, v, &block->instructions, link) 

#define mir_foreach_instr_in_block_from(block, v, from) \
        list_for_each_entry_from(struct midgard_instruction, v, from, &block->instructions, link) 

#define mir_foreach_instr_in_block_from_rev(block, v, from) \
        list_for_each_entry_from_rev(struct midgard_instruction, v, from, &block->instructions, link) 

#define mir_foreach_bundle_in_block(block, v) \
        util_dynarray_foreach(&block->bundles, midgard_bundle, v)

#define mir_foreach_instr_global(ctx, v) \
        mir_foreach_block(ctx, v_block) \
                mir_foreach_instr_in_block(v_block, v)


static inline midgard_instruction *
mir_last_in_block(struct midgard_block *block)
{
        return list_last_entry(&block->instructions, struct midgard_instruction, link);
}

static inline midgard_block *
mir_get_block(compiler_context *ctx, int idx)
{
        struct list_head *lst = &ctx->blocks;

        while ((idx--) + 1)
                lst = lst->next;

        return (struct midgard_block *) lst;
}

static inline bool
mir_is_alu_bundle(midgard_bundle *bundle)
{
        return IS_ALU(bundle->tag);
}

/* MIR manipulation */

void mir_rewrite_index(compiler_context *ctx, unsigned old, unsigned new);
void mir_rewrite_index_src(compiler_context *ctx, unsigned old, unsigned new);
void mir_rewrite_index_dst(compiler_context *ctx, unsigned old, unsigned new);

/* MIR printing */

void mir_print_instruction(midgard_instruction *ins);
void mir_print_bundle(midgard_bundle *ctx);
void mir_print_block(midgard_block *block);
void mir_print_shader(compiler_context *ctx);

/* MIR goodies */

static const midgard_vector_alu_src blank_alu_src = {
        .swizzle = SWIZZLE(COMPONENT_X, COMPONENT_Y, COMPONENT_Z, COMPONENT_W),
};

static const midgard_vector_alu_src blank_alu_src_xxxx = {
        .swizzle = SWIZZLE(COMPONENT_X, COMPONENT_X, COMPONENT_X, COMPONENT_X),
};

static const midgard_scalar_alu_src blank_scalar_alu_src = {
        .full = true
};

/* Used for encoding the unused source of 1-op instructions */
static const midgard_vector_alu_src zero_alu_src = { 0 };

/* 'Intrinsic' move for aliasing */

static inline midgard_instruction
v_fmov(unsigned src, midgard_vector_alu_src mod, unsigned dest)
{
        midgard_instruction ins = {
                .type = TAG_ALU_4,
                .ssa_args = {
                        .src0 = SSA_UNUSED_1,
                        .src1 = src,
                        .dest = dest,
                },
                .alu = {
                        .op = midgard_alu_op_fmov,
                        .reg_mode = midgard_reg_mode_32,
                        .dest_override = midgard_dest_override_none,
                        .mask = 0xFF,
                        .src1 = vector_alu_srco_unsigned(zero_alu_src),
                        .src2 = vector_alu_srco_unsigned(mod)
                },
        };

        return ins;
}

/* Scheduling */

void schedule_program(compiler_context *ctx);

/* Register allocation */

struct ra_graph;

struct ra_graph* allocate_registers(compiler_context *ctx);
void install_registers(compiler_context *ctx, struct ra_graph *g);
bool mir_is_live_after(compiler_context *ctx, midgard_block *block, midgard_instruction *start, int src);
bool mir_has_multiple_writes(compiler_context *ctx, int src);

void mir_create_pipeline_registers(compiler_context *ctx);

/* Final emission */

void emit_binary_bundle(
                compiler_context *ctx,
                midgard_bundle *bundle,
                struct util_dynarray *emission,
                int next_tag);
#endif
