/*
 *  Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License").
 *  You may not use this file except in compliance with the License.
 *  A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 *  or in the "license" file accompanying this file. This file is distributed
 *  on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *  express or implied. See the License for the specific language governing
 *  permissions and limitations under the License.
 */

#include <aws/common/linked_list.h>

#include <aws/testing/aws_test_harness.h>

struct int_value {
    int value;
    struct aws_linked_list_node node;
};

static int s_test_linked_list_order_push_back_pop_front(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_linked_list list;

    aws_linked_list_init(&list);
    ASSERT_TRUE(aws_linked_list_empty(&list));

    struct int_value first = (struct int_value){.value = 1};
    struct int_value second = (struct int_value){.value = 2};
    struct int_value third = (struct int_value){.value = 3};
    struct int_value fourth = (struct int_value){.value = 4};

    aws_linked_list_push_back(&list, &first.node);
    aws_linked_list_push_back(&list, &second.node);
    aws_linked_list_push_back(&list, &third.node);
    aws_linked_list_push_back(&list, &fourth.node);

    int item;
    struct aws_linked_list_node *node = aws_linked_list_pop_front(&list);
    item = AWS_CONTAINER_OF(node, struct int_value, node)->value;
    ASSERT_INT_EQUALS(first.value, item);

    node = aws_linked_list_pop_front(&list);
    item = AWS_CONTAINER_OF(node, struct int_value, node)->value;
    ASSERT_INT_EQUALS(second.value, item);

    node = aws_linked_list_pop_front(&list);
    item = AWS_CONTAINER_OF(node, struct int_value, node)->value;
    ASSERT_INT_EQUALS(third.value, item);

    node = aws_linked_list_pop_front(&list);
    item = AWS_CONTAINER_OF(node, struct int_value, node)->value;
    ASSERT_INT_EQUALS(fourth.value, item);

    ASSERT_TRUE(aws_linked_list_empty(&list));
    return 0;
}

static int s_test_linked_list_order_push_front_pop_back(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_linked_list list;

    aws_linked_list_init(&list);

    ASSERT_TRUE(aws_linked_list_empty(&list));

    struct int_value first = (struct int_value){.value = 1};
    struct int_value second = (struct int_value){.value = 2};
    struct int_value third = (struct int_value){.value = 3};
    struct int_value fourth = (struct int_value){.value = 4};

    aws_linked_list_push_front(&list, &first.node);
    aws_linked_list_push_front(&list, &second.node);
    aws_linked_list_push_front(&list, &third.node);
    aws_linked_list_push_front(&list, &fourth.node);

    ASSERT_FALSE(aws_linked_list_empty(&list));

    int item;
    struct aws_linked_list_node *node = aws_linked_list_pop_back(&list);
    item = AWS_CONTAINER_OF(node, struct int_value, node)->value;
    ASSERT_INT_EQUALS(first.value, item);

    node = aws_linked_list_pop_back(&list);
    item = AWS_CONTAINER_OF(node, struct int_value, node)->value;
    ASSERT_INT_EQUALS(second.value, item);

    node = aws_linked_list_pop_back(&list);
    item = AWS_CONTAINER_OF(node, struct int_value, node)->value;
    ASSERT_INT_EQUALS(third.value, item);

    node = aws_linked_list_pop_back(&list);
    item = AWS_CONTAINER_OF(node, struct int_value, node)->value;
    ASSERT_INT_EQUALS(fourth.value, item);

    ASSERT_TRUE(aws_linked_list_empty(&list));

    return 0;
}

static int s_test_linked_list_swap_nodes(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_linked_list list;

    aws_linked_list_init(&list);
    ASSERT_TRUE(aws_linked_list_empty(&list));

    struct int_value first = (struct int_value){.value = 1};
    struct int_value second = (struct int_value){.value = 2};
    struct int_value third = (struct int_value){.value = 3};
    struct int_value fourth = (struct int_value){.value = 4};

    aws_linked_list_push_back(&list, &first.node);
    aws_linked_list_push_back(&list, &second.node);
    aws_linked_list_push_back(&list, &third.node);
    aws_linked_list_push_back(&list, &fourth.node);

    /* non-adjacent swap: new order becomes 3, 2, 1, 4 */
    aws_linked_list_swap_nodes(&first.node, &third.node);
    /* adjacent swap: new order becomes 3, 2, 4, 1 */
    aws_linked_list_swap_nodes(&first.node, &fourth.node);

    int item;
    struct aws_linked_list_node *node = aws_linked_list_pop_front(&list);
    item = AWS_CONTAINER_OF(node, struct int_value, node)->value;
    ASSERT_INT_EQUALS(third.value, item);

    node = aws_linked_list_pop_front(&list);
    item = AWS_CONTAINER_OF(node, struct int_value, node)->value;
    ASSERT_INT_EQUALS(second.value, item);

    node = aws_linked_list_pop_front(&list);
    item = AWS_CONTAINER_OF(node, struct int_value, node)->value;
    ASSERT_INT_EQUALS(fourth.value, item);

    node = aws_linked_list_pop_front(&list);
    item = AWS_CONTAINER_OF(node, struct int_value, node)->value;
    ASSERT_INT_EQUALS(first.value, item);

    ASSERT_TRUE(aws_linked_list_empty(&list));
    return 0;
}

static int s_test_linked_list_iteration(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_linked_list list;

    aws_linked_list_init(&list);

    ASSERT_TRUE(aws_linked_list_empty(&list));
    ASSERT_PTR_EQUALS(aws_linked_list_begin(&list), aws_linked_list_end(&list));

    struct int_value first = (struct int_value){.value = 1};
    struct int_value second = (struct int_value){.value = 2};
    struct int_value third = (struct int_value){.value = 3};
    struct int_value fourth = (struct int_value){.value = 4};

    aws_linked_list_push_back(&list, &first.node);
    aws_linked_list_push_back(&list, &second.node);
    aws_linked_list_push_back(&list, &third.node);
    aws_linked_list_push_back(&list, &fourth.node);

    ASSERT_FALSE(aws_linked_list_empty(&list));
    ASSERT_FALSE(aws_linked_list_begin(&list) == aws_linked_list_end(&list));

    int count = 1;
    for (struct aws_linked_list_node *iter = aws_linked_list_begin(&list); iter != aws_linked_list_end(&list);
         iter = aws_linked_list_next(iter)) {

        int item = AWS_CONTAINER_OF(iter, struct int_value, node)->value;
        ASSERT_INT_EQUALS(count, item);
        ++count;
    }

    return 0;
}

static int s_test_linked_list_reverse_iteration(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_linked_list list;

    aws_linked_list_init(&list);

    ASSERT_TRUE(aws_linked_list_empty(&list));
    ASSERT_PTR_EQUALS(aws_linked_list_rbegin(&list), aws_linked_list_rend(&list));

    struct int_value first = (struct int_value){.value = 1};
    struct int_value second = (struct int_value){.value = 2};
    struct int_value third = (struct int_value){.value = 3};
    struct int_value fourth = (struct int_value){.value = 4};

    aws_linked_list_push_back(&list, &first.node);
    aws_linked_list_push_back(&list, &second.node);
    aws_linked_list_push_back(&list, &third.node);
    aws_linked_list_push_back(&list, &fourth.node);

    ASSERT_FALSE(aws_linked_list_empty(&list));
    ASSERT_FALSE(aws_linked_list_rbegin(&list) == aws_linked_list_rend(&list));

    int count = 4;
    for (struct aws_linked_list_node *iter = aws_linked_list_rbegin(&list); iter != aws_linked_list_rend(&list);
         iter = aws_linked_list_prev(iter)) {

        int item = AWS_CONTAINER_OF(iter, struct int_value, node)->value;
        ASSERT_INT_EQUALS(count, item);
        --count;
    }

    return 0;
}

static int s_test_linked_list_swap_contents(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_linked_list a, b;
    struct aws_linked_list_node a1, a2, b1, b2;

    /* Setup lists like:
     * a = {a1, a2}
     * b = {b1, b2}
     *
     * After swap should be like:
     * a = {b1, b2}
     * b = {a1, a2}
     */
    aws_linked_list_init(&a);
    aws_linked_list_push_back(&a, &a1);
    aws_linked_list_push_back(&a, &a2);

    aws_linked_list_init(&b);
    aws_linked_list_push_back(&b, &b1);
    aws_linked_list_push_back(&b, &b2);

    aws_linked_list_swap_contents(&a, &b);

    ASSERT_PTR_EQUALS(&b1, aws_linked_list_pop_front(&a));
    ASSERT_PTR_EQUALS(&b2, aws_linked_list_pop_front(&a));
    ASSERT_TRUE(aws_linked_list_empty(&a));

    ASSERT_PTR_EQUALS(&a1, aws_linked_list_pop_front(&b));
    ASSERT_PTR_EQUALS(&a2, aws_linked_list_pop_front(&b));
    ASSERT_TRUE(aws_linked_list_empty(&b));

    /* Setup lists like:
     * a = {a1, a2}
     * b = {}
     *
     * After swap should be like:
     * a = {}
     * b = {a1, a2}
     */
    aws_linked_list_init(&a);
    aws_linked_list_push_back(&a, &a1);
    aws_linked_list_push_back(&a, &a2);

    aws_linked_list_init(&b);

    aws_linked_list_swap_contents(&a, &b);

    ASSERT_TRUE(aws_linked_list_empty(&a));

    ASSERT_PTR_EQUALS(&a1, aws_linked_list_pop_front(&b));
    ASSERT_PTR_EQUALS(&a2, aws_linked_list_pop_front(&b));
    ASSERT_TRUE(aws_linked_list_empty(&b));

    /* Setup lists like:
     * a = {}
     * b = {b1, b2}
     *
     * After swap should be like:
     * a = {b1, b2}
     * b = {}
     */
    aws_linked_list_init(&a);

    aws_linked_list_init(&b);
    aws_linked_list_push_back(&b, &b1);
    aws_linked_list_push_back(&b, &b2);

    aws_linked_list_swap_contents(&a, &b);

    ASSERT_PTR_EQUALS(&b1, aws_linked_list_pop_front(&a));
    ASSERT_PTR_EQUALS(&b2, aws_linked_list_pop_front(&a));
    ASSERT_TRUE(aws_linked_list_empty(&a));

    ASSERT_TRUE(aws_linked_list_empty(&b));

    /* Setup two empty lists, after swap they should both still be ok. */
    aws_linked_list_init(&a);
    aws_linked_list_init(&b);

    aws_linked_list_swap_contents(&a, &b);

    ASSERT_TRUE(aws_linked_list_empty(&a));
    ASSERT_TRUE(aws_linked_list_empty(&b));

    return AWS_OP_SUCCESS;
}

static int s_test_linked_list_move_all_back(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_linked_list a, b;
    struct aws_linked_list_node a1, a2, b1, b2;

    /* Setup lists like:
     * a = {a1, a2}
     * b = {b1, b2}
     *
     * After move should be like:
     * a = {a1, a2, b1, b2}
     * b = {}
     */
    aws_linked_list_init(&a);
    aws_linked_list_push_back(&a, &a1);
    aws_linked_list_push_back(&a, &a2);

    aws_linked_list_init(&b);
    aws_linked_list_push_back(&b, &b1);
    aws_linked_list_push_back(&b, &b2);

    aws_linked_list_move_all_back(&a, &b);
    ASSERT_TRUE(aws_linked_list_is_valid_deep(&a));
    ASSERT_TRUE(aws_linked_list_is_valid_deep(&b));
    ASSERT_TRUE(aws_linked_list_empty(&b));

    struct aws_linked_list_node *expected_a1a2b1b2[] = {&a1, &a2, &b1, &b2};
    struct aws_linked_list_node *it = aws_linked_list_begin(&a);
    size_t i = 0;
    while (it != aws_linked_list_end(&a)) {
        ASSERT_PTR_EQUALS(expected_a1a2b1b2[i], it);
        it = aws_linked_list_next(it);
        i++;
    };
    ASSERT_UINT_EQUALS(AWS_ARRAY_SIZE(expected_a1a2b1b2), i);

    /* Setup lists like:
     * a = {}
     * b = {b1, b2}
     *
     * After move should be like:
     * a = {b1, b2}
     * b = {}
     */
    aws_linked_list_init(&a);

    aws_linked_list_init(&b);
    aws_linked_list_push_back(&b, &b1);
    aws_linked_list_push_back(&b, &b2);

    aws_linked_list_move_all_back(&a, &b);
    ASSERT_TRUE(aws_linked_list_is_valid_deep(&a));
    ASSERT_TRUE(aws_linked_list_is_valid_deep(&b));
    ASSERT_TRUE(aws_linked_list_empty(&b));

    struct aws_linked_list_node *expected_b1b2[] = {&b1, &b2};
    it = aws_linked_list_begin(&a);
    i = 0;
    while (it != aws_linked_list_end(&a)) {
        ASSERT_PTR_EQUALS(expected_b1b2[i], it);
        it = aws_linked_list_next(it);
        i++;
    };
    ASSERT_UINT_EQUALS(AWS_ARRAY_SIZE(expected_b1b2), i);

    /* Setup lists like:
     * a = {a1}
     * b = {b1}
     *
     * After move should be like:
     * a = {a1, b1}
     * b = {}
     */
    aws_linked_list_init(&a);
    aws_linked_list_push_back(&a, &a1);

    aws_linked_list_init(&b);
    aws_linked_list_push_back(&b, &b1);

    aws_linked_list_move_all_back(&a, &b);
    ASSERT_TRUE(aws_linked_list_is_valid_deep(&a));
    ASSERT_TRUE(aws_linked_list_is_valid_deep(&b));
    ASSERT_TRUE(aws_linked_list_empty(&b));

    struct aws_linked_list_node *expected_a1b1[] = {&a1, &b1};
    it = aws_linked_list_begin(&a);
    i = 0;
    while (it != aws_linked_list_end(&a)) {
        ASSERT_PTR_EQUALS(expected_a1b1[i], it);
        it = aws_linked_list_next(it);
        i++;
    };
    ASSERT_UINT_EQUALS(AWS_ARRAY_SIZE(expected_a1b1), i);

    return AWS_OP_SUCCESS;
}

static int s_test_linked_list_move_all_front(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_linked_list a, b;
    struct aws_linked_list_node a1, a2, b1, b2;

    /* Setup lists like:
     * a = {a2, a1}
     * b = {b2, b1}
     *
     * After move should be like:
     * a = {b2, b1, a2, a1}
     * b = {}
     */
    aws_linked_list_init(&a);
    aws_linked_list_push_front(&a, &a1);
    aws_linked_list_push_front(&a, &a2);

    aws_linked_list_init(&b);
    aws_linked_list_push_front(&b, &b1);
    aws_linked_list_push_front(&b, &b2);

    aws_linked_list_move_all_front(&a, &b);
    ASSERT_TRUE(aws_linked_list_is_valid_deep(&a));
    ASSERT_TRUE(aws_linked_list_is_valid_deep(&b));
    ASSERT_TRUE(aws_linked_list_empty(&b));

    struct aws_linked_list_node *expected_b2b1a2a1[] = {&b2, &b1, &a2, &a1};
    struct aws_linked_list_node *it = aws_linked_list_begin(&a);
    size_t i = 0;
    while (it != aws_linked_list_end(&a)) {
        ASSERT_PTR_EQUALS(expected_b2b1a2a1[i], it);
        it = aws_linked_list_next(it);
        i++;
    };
    ASSERT_UINT_EQUALS(AWS_ARRAY_SIZE(expected_b2b1a2a1), i);

    /* Setup lists like:
     * a = {}
     * b = {b2, b1}
     *
     * After move should be like:
     * a = {b2, b1}
     * b = {}
     */
    aws_linked_list_init(&a);

    aws_linked_list_init(&b);
    aws_linked_list_push_front(&b, &b1);
    aws_linked_list_push_front(&b, &b2);

    aws_linked_list_move_all_front(&a, &b);
    ASSERT_TRUE(aws_linked_list_is_valid_deep(&a));
    ASSERT_TRUE(aws_linked_list_is_valid_deep(&b));
    ASSERT_TRUE(aws_linked_list_empty(&b));

    struct aws_linked_list_node *expected_b2b1[] = {&b2, &b1};
    it = aws_linked_list_begin(&a);
    i = 0;
    while (it != aws_linked_list_end(&a)) {
        ASSERT_PTR_EQUALS(expected_b2b1[i], it);
        it = aws_linked_list_next(it);
        i++;
    };
    ASSERT_UINT_EQUALS(AWS_ARRAY_SIZE(expected_b2b1), i);

    /* Setup lists like:
     * a = {a1}
     * b = {b1}
     *
     * After move should be like:
     * a = {b1, a1}
     * b = {}
     */
    aws_linked_list_init(&a);
    aws_linked_list_push_front(&a, &a1);

    aws_linked_list_init(&b);
    aws_linked_list_push_front(&b, &b1);

    aws_linked_list_move_all_front(&a, &b);
    ASSERT_TRUE(aws_linked_list_is_valid_deep(&a));
    ASSERT_TRUE(aws_linked_list_is_valid_deep(&b));
    ASSERT_TRUE(aws_linked_list_empty(&b));

    struct aws_linked_list_node *expected_b1a1[] = {&b1, &a1};
    it = aws_linked_list_begin(&a);
    i = 0;
    while (it != aws_linked_list_end(&a)) {
        ASSERT_PTR_EQUALS(expected_b1a1[i], it);
        it = aws_linked_list_next(it);
        i++;
    };
    ASSERT_UINT_EQUALS(AWS_ARRAY_SIZE(expected_b1a1), i);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(linked_list_push_back_pop_front, s_test_linked_list_order_push_back_pop_front)
AWS_TEST_CASE(linked_list_push_front_pop_back, s_test_linked_list_order_push_front_pop_back)
AWS_TEST_CASE(linked_list_swap_nodes, s_test_linked_list_swap_nodes)
AWS_TEST_CASE(linked_list_iteration, s_test_linked_list_iteration)
AWS_TEST_CASE(linked_list_reverse_iteration, s_test_linked_list_reverse_iteration)
AWS_TEST_CASE(linked_list_swap_contents, s_test_linked_list_swap_contents)
AWS_TEST_CASE(linked_list_move_all_back, s_test_linked_list_move_all_back)
AWS_TEST_CASE(linked_list_move_all_front, s_test_linked_list_move_all_front)

static int s_linked_list_node_in_list_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_linked_list list;
    aws_linked_list_init(&list);

    struct aws_linked_list_node node;
    AWS_ZERO_STRUCT(node);

    ASSERT_FALSE(aws_linked_list_node_is_in_list(&node));

    aws_linked_list_push_back(&list, &node);
    ASSERT_TRUE(aws_linked_list_node_is_in_list(&node));
    aws_linked_list_remove(&node);

    ASSERT_FALSE(aws_linked_list_node_is_in_list(&node));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(linked_list_node_in_list, s_linked_list_node_in_list_fn)
