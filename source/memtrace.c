/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include <aws/common/atomics.h>
#include <aws/common/byte_buf.h>
#include <aws/common/hash_table.h>
#include <aws/common/logging.h>
#include <aws/common/mutex.h>
#include <aws/common/priority_queue.h>
#include <aws/common/string.h>
#include <aws/common/system_info.h>
#include <aws/common/time.h>

/* describes a single live allocation */
struct alloc_info {
    size_t size;
    time_t time;
    uint64_t stack; /* hash of stack frame pointers */
};

/* Using a flexible array member is the C99 compliant way to have the frames immediately follow the header.
 *
 * MSVC doesn't know this for some reason so we need to use a pragma to make
 * it happy.
 */
#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4200) /* nonstandard extension used: zero-sized array in struct/union */
#endif

/* one of these is stored per unique stack */
struct stack_trace {
    size_t depth;         /* length of frames[] */
    void *const frames[]; /* rest of frames are allocated after */
};

#ifdef _MSC_VER
#    pragma warning(pop)
#endif

/* Tracking structure, used as the allocator impl */
struct alloc_tracer {
    struct aws_allocator *allocator;        /* underlying allocator */
    struct aws_allocator *system_allocator; /* bookkeeping allocator */
    enum aws_mem_trace_level level;         /* level to trace at */
    size_t frames_per_stack;                /* how many frames to keep per stack */
    struct aws_atomic_var allocated;        /* bytes currently allocated */
    struct aws_mutex mutex;                 /* protects everything below */
    struct aws_hash_table allocs;           /* live allocations, maps address -> alloc_info */
    struct aws_hash_table stacks;           /* unique stack traces, maps hash -> stack_trace */
    struct aws_hash_table stack_info;       /* only used during dumps, maps stack hash/id -> stack_metadata */
};

/* number of frames to skip in call stacks (s_alloc_tracer_track, and the vtable function) */
#define FRAMES_TO_SKIP 2

static void *s_trace_mem_acquire(struct aws_allocator *allocator, size_t size);
static void s_trace_mem_release(struct aws_allocator *allocator, void *ptr);
static void *s_trace_mem_realloc(struct aws_allocator *allocator, void *ptr, size_t old_size, size_t new_size);
static void *s_trace_mem_calloc(struct aws_allocator *allocator, size_t num, size_t size);

static struct aws_allocator s_trace_allocator = {
    .mem_acquire = s_trace_mem_acquire,
    .mem_release = s_trace_mem_release,
    .mem_realloc = s_trace_mem_realloc,
    .mem_calloc = s_trace_mem_calloc,
};

/* for the hash table, to destroy elements */
static void s_destroy_alloc(void *data) {
    struct aws_allocator *allocator = aws_default_allocator();
    struct alloc_info *alloc = data;
    aws_mem_release(allocator, alloc);
}

static void s_destroy_stacktrace(void *data) {
    struct aws_allocator *allocator = aws_default_allocator();
    struct stack_trace *stack = data;
    aws_mem_release(allocator, stack);
}

static void s_alloc_tracer_init(
    struct alloc_tracer *tracer,
    struct aws_allocator *allocator,
    struct aws_allocator *system_allocator,
    enum aws_mem_trace_level level,
    size_t frames_per_stack) {

    void *stack[1];
    if (!aws_backtrace(stack, 1)) {
        /* clamp level if tracing isn't available */
        level = level > AWS_MEMTRACE_BYTES ? AWS_MEMTRACE_BYTES : level;
    }

    tracer->allocator = allocator;
    tracer->system_allocator = system_allocator;
    tracer->level = level;

    if (tracer->level >= AWS_MEMTRACE_BYTES) {
        aws_atomic_init_int(&tracer->allocated, 0);
        AWS_FATAL_ASSERT(AWS_OP_SUCCESS == aws_mutex_init(&tracer->mutex));
        AWS_FATAL_ASSERT(
            AWS_OP_SUCCESS ==
            aws_hash_table_init(
                &tracer->allocs, tracer->system_allocator, 1024, aws_hash_ptr, aws_ptr_eq, NULL, s_destroy_alloc));
    }

    if (tracer->level == AWS_MEMTRACE_STACKS) {
        if (frames_per_stack > 128) {
            frames_per_stack = 128;
        }
        tracer->frames_per_stack = (frames_per_stack) ? frames_per_stack : 8;
        AWS_FATAL_ASSERT(
            AWS_OP_SUCCESS ==
            aws_hash_table_init(
                &tracer->stacks, tracer->system_allocator, 1024, aws_hash_ptr, aws_ptr_eq, NULL, s_destroy_stacktrace));
    }
}

static void s_alloc_tracer_track(struct alloc_tracer *tracer, void *ptr, size_t size) {
    if (tracer->level == AWS_MEMTRACE_NONE) {
        return;
    }

    aws_atomic_fetch_add(&tracer->allocated, size);

    struct alloc_info *alloc = aws_mem_calloc(tracer->system_allocator, 1, sizeof(struct alloc_info));
    alloc->size = size;
    alloc->time = time(NULL);

    if (tracer->level == AWS_MEMTRACE_STACKS) {
        /* capture stack frames, skip 2 for this function and the allocation vtable function */
        AWS_VARIABLE_LENGTH_ARRAY(void *, stack_frames, (FRAMES_TO_SKIP + tracer->frames_per_stack));
        size_t stack_depth = aws_backtrace(stack_frames, FRAMES_TO_SKIP + tracer->frames_per_stack);
        if (stack_depth) {
            /* hash the stack pointers */
            struct aws_byte_cursor stack_cursor =
                aws_byte_cursor_from_array(stack_frames, stack_depth * sizeof(void *));
            uint64_t stack_id = aws_hash_byte_cursor_ptr(&stack_cursor);
            alloc->stack = stack_id; /* associate the stack with the alloc */

            aws_mutex_lock(&tracer->mutex);
            struct aws_hash_element *item = NULL;
            int was_created = 0;
            AWS_FATAL_ASSERT(
                AWS_OP_SUCCESS ==
                aws_hash_table_create(&tracer->stacks, (void *)(uintptr_t)stack_id, &item, &was_created));
            /* If this is a new stack, save it to the hash */
            if (was_created) {
                struct stack_trace *stack = aws_mem_calloc(
                    tracer->system_allocator,
                    1,
                    sizeof(struct stack_trace) + (sizeof(void *) * tracer->frames_per_stack));
                memcpy(
                    (void **)&stack->frames[0],
                    &stack_frames[FRAMES_TO_SKIP],
                    (stack_depth - FRAMES_TO_SKIP) * sizeof(void *));
                stack->depth = stack_depth - FRAMES_TO_SKIP;
                item->value = stack;
            }
            aws_mutex_unlock(&tracer->mutex);
        }
    }

    aws_mutex_lock(&tracer->mutex);
    AWS_FATAL_ASSERT(AWS_OP_SUCCESS == aws_hash_table_put(&tracer->allocs, ptr, alloc, NULL));
    aws_mutex_unlock(&tracer->mutex);
}

static void s_alloc_tracer_untrack(struct alloc_tracer *tracer, void *ptr) {
    if (tracer->level == AWS_MEMTRACE_NONE) {
        return;
    }

    aws_mutex_lock(&tracer->mutex);
    struct aws_hash_element *item;
    AWS_FATAL_ASSERT(AWS_OP_SUCCESS == aws_hash_table_find(&tracer->allocs, ptr, &item));
    /* because the tracer can be installed at any time, it is possible for an allocation to not
     * be tracked. Therefore, we make sure the find succeeds, but then check the returned
     * value */
    if (item) {
        AWS_FATAL_ASSERT(item->key == ptr && item->value);
        struct alloc_info *alloc = item->value;
        aws_atomic_fetch_sub(&tracer->allocated, alloc->size);
        s_destroy_alloc(item->value);
        AWS_FATAL_ASSERT(AWS_OP_SUCCESS == aws_hash_table_remove_element(&tracer->allocs, item));
    }
    aws_mutex_unlock(&tracer->mutex);
}

/* used only to resolve stacks -> trace, count, size at dump time */
struct stack_metadata {
    struct aws_string *trace;
    size_t count;
    size_t size;
};

static int s_collect_stack_trace(void *context, struct aws_hash_element *item) {
    struct alloc_tracer *tracer = context;
    struct aws_hash_table *all_stacks = &tracer->stacks;
    struct aws_allocator *allocator = tracer->system_allocator;
    struct stack_metadata *stack_info = item->value;
    struct aws_hash_element *stack_item = NULL;
    AWS_FATAL_ASSERT(AWS_OP_SUCCESS == aws_hash_table_find(all_stacks, item->key, &stack_item));
    AWS_FATAL_ASSERT(stack_item);
    struct stack_trace *stack = stack_item->value;
    void *const *stack_frames = &stack->frames[0];

    /* convert the frame pointers to symbols, and concat into a buffer */
    char buf[4096] = {0};
    struct aws_byte_buf stacktrace = aws_byte_buf_from_empty_array(buf, AWS_ARRAY_SIZE(buf));
    struct aws_byte_cursor newline = aws_byte_cursor_from_c_str("\n");
    char **symbols = aws_backtrace_addr2line(stack_frames, stack->depth);
    for (size_t idx = 0; idx < stack->depth; ++idx) {
        if (idx > 0) {
            aws_byte_buf_append(&stacktrace, &newline);
        }
        const char *caller = symbols[idx];
        if (!caller || !caller[0]) {
            break;
        }
        struct aws_byte_cursor cursor = aws_byte_cursor_from_c_str(caller);
        aws_byte_buf_append(&stacktrace, &cursor);
    }
    free(symbols);
    /* record the resultant buffer as a string */
    stack_info->trace = aws_string_new_from_array(allocator, stacktrace.buffer, stacktrace.len);
    aws_byte_buf_clean_up(&stacktrace);
    return AWS_COMMON_HASH_TABLE_ITER_CONTINUE;
}

static int s_stack_info_compare_size(const void *a, const void *b) {
    const struct stack_metadata *stack_a = *(const struct stack_metadata **)a;
    const struct stack_metadata *stack_b = *(const struct stack_metadata **)b;
    return stack_b->size > stack_a->size;
}

static int s_stack_info_compare_count(const void *a, const void *b) {
    const struct stack_metadata *stack_a = *(const struct stack_metadata **)a;
    const struct stack_metadata *stack_b = *(const struct stack_metadata **)b;
    return stack_b->count > stack_a->count;
}

static void s_stack_info_destroy(void *data) {
    struct stack_metadata *stack = data;
    struct aws_allocator *allocator = stack->trace->allocator;
    aws_string_destroy(stack->trace);
    aws_mem_release(allocator, stack);
}

/* tally up count/size per stack from all allocs */
static int s_collect_stack_stats(void *context, struct aws_hash_element *item) {
    struct alloc_tracer *tracer = context;
    struct alloc_info *alloc = item->value;
    struct aws_hash_element *stack_item = NULL;
    int was_created = 0;
    AWS_FATAL_ASSERT(
        AWS_OP_SUCCESS ==
        aws_hash_table_create(&tracer->stack_info, (void *)(uintptr_t)alloc->stack, &stack_item, &was_created));
    if (was_created) {
        stack_item->value = aws_mem_calloc(tracer->system_allocator, 1, sizeof(struct stack_metadata));
    }
    struct stack_metadata *stack = stack_item->value;
    stack->count++;
    stack->size += alloc->size;
    return AWS_COMMON_HASH_TABLE_ITER_CONTINUE;
}

static int s_insert_stacks(void *context, struct aws_hash_element *item) {
    struct aws_priority_queue *pq = context;
    struct stack_metadata *stack = item->value;
    AWS_FATAL_ASSERT(AWS_OP_SUCCESS == aws_priority_queue_push(pq, &stack));
    return AWS_COMMON_HASH_TABLE_ITER_CONTINUE;
}

static int s_insert_allocs(void *context, struct aws_hash_element *item) {
    struct aws_priority_queue *allocs = context;
    struct alloc_info *alloc = item->value;
    AWS_FATAL_ASSERT(AWS_OP_SUCCESS == aws_priority_queue_push(allocs, &alloc));
    return AWS_COMMON_HASH_TABLE_ITER_CONTINUE;
}

static int s_alloc_compare(const void *a, const void *b) {
    const struct alloc_info *alloc_a = *(const struct alloc_info **)a;
    const struct alloc_info *alloc_b = *(const struct alloc_info **)b;
    return alloc_a->time > alloc_b->time;
}

static void s_alloc_tracer_dump(struct alloc_tracer *tracer) {
    if (tracer->level == AWS_MEMTRACE_NONE || aws_atomic_load_int(&tracer->allocated) == 0) {
        return;
    }

    aws_mutex_lock(&tracer->mutex);

    size_t num_allocs = aws_hash_table_get_entry_count(&tracer->allocs);
    AWS_LOGF_TRACE(
        AWS_LS_COMMON_MEMTRACE, "################################################################################\n");
    AWS_LOGF_TRACE(
        AWS_LS_COMMON_MEMTRACE, "#  BEGIN MEMTRACE DUMP                                                         #\n");
    AWS_LOGF_TRACE(
        AWS_LS_COMMON_MEMTRACE, "################################################################################\n");
    AWS_LOGF_TRACE(
        AWS_LS_COMMON_MEMTRACE,
        "tracer: %zu bytes still allocated in %zu allocations\n",
        aws_atomic_load_int(&tracer->allocated),
        num_allocs);

    /* convert stacks from pointers -> symbols */
    if (tracer->level == AWS_MEMTRACE_STACKS) {
        AWS_FATAL_ASSERT(
            AWS_OP_SUCCESS ==
            aws_hash_table_init(
                &tracer->stack_info, tracer->allocator, 64, aws_hash_ptr, aws_ptr_eq, NULL, s_stack_info_destroy));
        /* collect active stacks, tally up sizes and counts */
        aws_hash_table_foreach(&tracer->allocs, s_collect_stack_stats, tracer);
        /* collect stack traces for active stacks */
        aws_hash_table_foreach(&tracer->stack_info, s_collect_stack_trace, tracer);
    }

    /* sort allocs by time */
    struct aws_priority_queue allocs;
    aws_priority_queue_init_dynamic(
        &allocs, tracer->allocator, num_allocs, sizeof(struct alloc_info *), s_alloc_compare);
    aws_hash_table_foreach(&tracer->allocs, s_insert_allocs, &allocs);
    /* dump allocs by time */
    AWS_LOGF_TRACE(
        AWS_LS_COMMON_MEMTRACE, "################################################################################\n");
    AWS_LOGF_TRACE(AWS_LS_COMMON_MEMTRACE, "Leaks in order of allocation:\n");
    AWS_LOGF_TRACE(
        AWS_LS_COMMON_MEMTRACE, "################################################################################\n");
    while (aws_priority_queue_size(&allocs)) {
        struct alloc_info *alloc = NULL;
        aws_priority_queue_pop(&allocs, &alloc);
        AWS_LOGF_TRACE(AWS_LS_COMMON_MEMTRACE, "ALLOC %zu bytes\n", alloc->size);
        if (alloc->stack) {
            struct aws_hash_element *item = NULL;
            AWS_FATAL_ASSERT(
                AWS_OP_SUCCESS == aws_hash_table_find(&tracer->stack_info, (void *)(uintptr_t)alloc->stack, &item));
            struct stack_metadata *stack = item->value;
            AWS_LOGF_TRACE(AWS_LS_COMMON_MEMTRACE, "  stacktrace:\n%s\n", (const char *)aws_string_bytes(stack->trace));
        }
    }

    aws_priority_queue_clean_up(&allocs);

    if (tracer->level == AWS_MEMTRACE_STACKS) {
        size_t num_stacks = aws_hash_table_get_entry_count(&tracer->stack_info);
        /* sort stacks by total size leaked */
        struct aws_priority_queue stacks_by_size;
        AWS_FATAL_ASSERT(
            AWS_OP_SUCCESS == aws_priority_queue_init_dynamic(
                                  &stacks_by_size,
                                  tracer->allocator,
                                  num_stacks,
                                  sizeof(struct stack_metadata *),
                                  s_stack_info_compare_size));
        aws_hash_table_foreach(&tracer->stack_info, s_insert_stacks, &stacks_by_size);
        AWS_LOGF_TRACE(
            AWS_LS_COMMON_MEMTRACE,
            "################################################################################\n");
        AWS_LOGF_TRACE(AWS_LS_COMMON_MEMTRACE, "Stacks by bytes leaked:\n");
        AWS_LOGF_TRACE(
            AWS_LS_COMMON_MEMTRACE,
            "################################################################################\n");
        while (aws_priority_queue_size(&stacks_by_size) > 0) {
            struct stack_metadata *stack = NULL;
            aws_priority_queue_pop(&stacks_by_size, &stack);
            AWS_LOGF_TRACE(AWS_LS_COMMON_MEMTRACE, "%zu bytes in %zu allocations:\n", stack->size, stack->count);
            AWS_LOGF_TRACE(AWS_LS_COMMON_MEMTRACE, "%s\n", (const char *)aws_string_bytes(stack->trace));
        }
        aws_priority_queue_clean_up(&stacks_by_size);

        /* sort stacks by number of leaks */
        struct aws_priority_queue stacks_by_count;
        AWS_FATAL_ASSERT(
            AWS_OP_SUCCESS == aws_priority_queue_init_dynamic(
                                  &stacks_by_count,
                                  tracer->allocator,
                                  num_stacks,
                                  sizeof(struct stack_metadata *),
                                  s_stack_info_compare_count));
        AWS_LOGF_TRACE(
            AWS_LS_COMMON_MEMTRACE,
            "################################################################################\n");
        AWS_LOGF_TRACE(AWS_LS_COMMON_MEMTRACE, "Stacks by number of leaks:\n");
        AWS_LOGF_TRACE(
            AWS_LS_COMMON_MEMTRACE,
            "################################################################################\n");
        aws_hash_table_foreach(&tracer->stack_info, s_insert_stacks, &stacks_by_count);
        while (aws_priority_queue_size(&stacks_by_count) > 0) {
            struct stack_metadata *stack = NULL;
            aws_priority_queue_pop(&stacks_by_count, &stack);
            AWS_LOGF_TRACE(AWS_LS_COMMON_MEMTRACE, "%zu allocations leaking %zu bytes:\n", stack->count, stack->size);
            AWS_LOGF_TRACE(AWS_LS_COMMON_MEMTRACE, "%s\n", (const char *)aws_string_bytes(stack->trace));
        }
        aws_priority_queue_clean_up(&stacks_by_count);
        aws_hash_table_clean_up(&tracer->stack_info);
    }

    AWS_LOGF_TRACE(
        AWS_LS_COMMON_MEMTRACE, "################################################################################\n");
    AWS_LOGF_TRACE(
        AWS_LS_COMMON_MEMTRACE, "#  END MEMTRACE DUMP                                                           #\n");
    AWS_LOGF_TRACE(
        AWS_LS_COMMON_MEMTRACE, "################################################################################\n");

    aws_mutex_unlock(&tracer->mutex);
}

static void *s_trace_mem_acquire(struct aws_allocator *allocator, size_t size) {
    struct alloc_tracer *tracer = allocator->impl;
    void *ptr = aws_mem_acquire(tracer->allocator, size);
    s_alloc_tracer_track(tracer, ptr, size);
    return ptr;
}

static void s_trace_mem_release(struct aws_allocator *allocator, void *ptr) {
    struct alloc_tracer *tracer = allocator->impl;
    s_alloc_tracer_untrack(tracer, ptr);
    aws_mem_release(tracer->allocator, ptr);
}

static void *s_trace_mem_realloc(struct aws_allocator *allocator, void *ptr, size_t old_size, size_t new_size) {
    struct alloc_tracer *tracer = allocator->impl;
    void *new_ptr = ptr;

    AWS_FATAL_ASSERT(AWS_OP_SUCCESS == aws_mem_realloc(tracer->allocator, &new_ptr, old_size, new_size));

    s_alloc_tracer_untrack(tracer, ptr);
    s_alloc_tracer_track(tracer, new_ptr, new_size);

    return new_ptr;
}

static void *s_trace_mem_calloc(struct aws_allocator *allocator, size_t num, size_t size) {
    struct alloc_tracer *tracer = allocator->impl;
    void *ptr = aws_mem_calloc(tracer->allocator, num, size);
    s_alloc_tracer_track(tracer, ptr, num * size);
    return ptr;
}

struct aws_allocator *aws_mem_tracer_new(
    struct aws_allocator *allocator,
    struct aws_allocator *system_allocator,
    enum aws_mem_trace_level level,
    size_t frames_per_stack) {

    if (!system_allocator) {
        system_allocator = aws_default_allocator();
    }

    struct alloc_tracer *tracer = NULL;
    struct aws_allocator *trace_allocator = NULL;
    aws_mem_acquire_many(
        system_allocator, 2, &tracer, sizeof(struct alloc_tracer), &trace_allocator, sizeof(struct aws_allocator));

    AWS_FATAL_ASSERT(trace_allocator);
    AWS_FATAL_ASSERT(tracer);

    AWS_ZERO_STRUCT(*trace_allocator);
    AWS_ZERO_STRUCT(*tracer);

    /* copy the template vtable s*/
    *trace_allocator = s_trace_allocator;
    trace_allocator->impl = tracer;

    s_alloc_tracer_init(tracer, allocator, system_allocator, level, frames_per_stack);
    return trace_allocator;
}

struct aws_allocator *aws_mem_tracer_destroy(struct aws_allocator *trace_allocator) {
    struct alloc_tracer *tracer = trace_allocator->impl;
    struct aws_allocator *allocator = tracer->allocator;

    /* This is not necessary, as if you are destroying the allocator, what are your
     * expectations? Either way, we can, so we might as well...
     */
    aws_mutex_lock(&tracer->mutex);
    aws_hash_table_clean_up(&tracer->allocs);
    aws_hash_table_clean_up(&tracer->stacks);
    aws_mutex_unlock(&tracer->mutex);
    aws_mutex_clean_up(&tracer->mutex);

    struct aws_allocator *system_allocator = tracer->system_allocator;
    aws_mem_release(system_allocator, tracer);
    /* trace_allocator is freed as part of the block tracer was allocated in */
    return allocator;
}

void aws_mem_tracer_dump(struct aws_allocator *trace_allocator) {
    struct alloc_tracer *tracer = trace_allocator->impl;
    s_alloc_tracer_dump(tracer);
}

size_t aws_mem_tracer_bytes(struct aws_allocator *trace_allocator) {
    struct alloc_tracer *tracer = trace_allocator->impl;
    return aws_atomic_load_int(&tracer->allocated);
}

size_t aws_mem_tracer_count(struct aws_allocator *trace_allocator) {
    struct alloc_tracer *tracer = trace_allocator->impl;
    aws_mutex_lock(&tracer->mutex);
    size_t count = aws_hash_table_get_entry_count(&tracer->allocs);
    aws_mutex_unlock(&tracer->mutex);
    return count;
}
