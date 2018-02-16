// These "x-files" are written in a somewhat unusual way.  When interpreting a
// grammar, we use the code in this file directly.  When compiling a grammar, we
// turn this code into a string to include in the generated file.  To avoid
// involving any external build tools, we enclose the entire file in a macro
// invocation.  The interpreter includes the source directly, while the compiler
// redefines the macro to return the source code as a string.

#ifndef CONSTRUCT_BODY
#define CONSTRUCT_BODY(...) __VA_ARGS__
#endif

#ifndef BINDING_T
#define BINDING_T uint32_t
#endif

#ifndef FINISHED_NODE_T
#define FINISHED_NODE_T void *
#endif

#ifndef FINISH_NODE
#define FINISH_NODE(binding, choice, next_sibling, slots, operand, left, \
 right, info) 0
#endif

#define FINISH_NODE_STRUCT(n, next_sibling, info) (FINISH_NODE((n)->binding, \
 (n)->choice_index, next_sibling, (n)->slots, (n)->operand, (n)->left, \
 (n)->right, info))

#ifndef FINISH_TOKEN
#define FINISH_TOKEN(next_sibling, info) 0
#endif

#ifndef BINDING_LOOKUP
#define BINDING_LOOKUP(parent, slot, info) 0
#endif

#ifndef FIXITY_ASSOCIATIVITY_LOOKUP
#define FIXITY_ASSOCIATIVITY_LOOKUP(binding, choice_index, info) 0
#endif

#include "x-construct-actions.h"
#define CONSTRUCT_ACTION(action) CONSTRUCT_ACTION_##action,

CONSTRUCT_BODY
(

enum construct_fixity_associativity {
    CONSTRUCT_PREFIX,
    CONSTRUCT_POSTFIX,
    CONSTRUCT_INFIX_LEFT,
    CONSTRUCT_INFIX_RIGHT,
    CONSTRUCT_INFIX_FLAT,
};

struct construct_node {
    struct construct_node *next;

    FINISHED_NODE_T slots[MAX_NUMBER_OF_SLOTS];
    FINISHED_NODE_T operand;
    FINISHED_NODE_T left;
    FINISHED_NODE_T right;

    BINDING_T binding;

    // In parent rule.
    uint16_t slot_index;

    uint16_t choice_index;

    // For operators.
    enum construct_fixity_associativity fixity_associativity;
    int precedence;

    size_t start_location;
    size_t end_location;
};

struct construct_expression {
    struct construct_expression *parent;
    struct construct_node *first_operator;
    struct construct_node *first_value;

    BINDING_T binding;

    // In parent rule.
    uint16_t slot_index;
};

struct construct_state {
    struct construct_node *under_construction;

    struct construct_expression *current_expression;

    struct construct_node *node_freelist;
    struct construct_expression *expression_freelist;

    void *info;
};

static struct construct_node *construct_node_alloc(struct construct_state *s)
{
    if (s->node_freelist) {
        struct construct_node *node = s->node_freelist;
        s->node_freelist = node->next;
        memset(node, 0, sizeof(struct construct_node));
        return node;
    }
    struct construct_node *node = calloc(1, sizeof(struct construct_node));
    if (!node)
        abort();
    return node;
}

static struct construct_expression *construct_expression_alloc(struct
 construct_state *s)
{
    if (s->expression_freelist) {
        struct construct_expression *expr = s->expression_freelist;
        s->expression_freelist = expr->parent;
        memset(expr, 0, sizeof(struct construct_expression));
        return expr;
    }
    struct construct_expression *expr =
     calloc(1, sizeof(struct construct_expression));
    if (!expr)
        abort();
    return expr;
}

static void construct_node_free(struct construct_state *state, struct
 construct_node *node)
{
    node->next = state->node_freelist;
    state->node_freelist = node;
}

static void construct_expression_free(struct construct_state *state, struct
 construct_expression *expr)
{
    expr->parent = state->expression_freelist;
    state->expression_freelist = expr;
}

enum construct_action_type { CONSTRUCT_ACTIONS };

struct construct_action {
    enum construct_action_type type;

    // For slots and operands.
    uint16_t slot_index;
    uint16_t choice_index;

    size_t start_location;
    size_t end_location;
};

static bool construct_expression_should_reduce(struct construct_state *s,
 struct construct_expression *expr, struct construct_node *node)
{
    struct construct_node *top = expr->first_operator;
    if (!top)
        return false;
    return node->precedence < top->precedence ||
     (node->precedence == top->precedence &&
      node->fixity_associativity == CONSTRUCT_INFIX_RIGHT);
}

static void construct_expression_reduce(struct construct_state *s,
 struct construct_expression *expr)
{
    struct construct_node *op = expr->first_operator;
    if (op->fixity_associativity == CONSTRUCT_INFIX_FLAT) {
        struct construct_node *first_value = expr->first_value;
        struct construct_node *last_value = first_value;
        struct construct_node *last_operator = op;
        FINISHED_NODE_T operand = op->operand;
        while (last_operator &&
         last_operator->choice_index == op->choice_index) {
            struct construct_node *next_op = last_operator->next;
            // TODO: Combine last_operator slots together instead of just
            // throwing the old ones away.
            construct_node_free(s, last_operator);
            last_operator = next_op;
            // TODO: I think this makes the list backwards...
            operand = FINISH_NODE_STRUCT(last_value, operand, s->info);
            struct construct_node *next_value = last_value->next;
            construct_node_free(s, last_value);
            last_value = next_value;
        }
        operand = FINISH_NODE_STRUCT(last_value, operand, s->info);
        last_operator->next = last_value->next;
        construct_node_free(s, last_value);
        expr->first_operator = last_operator;
        expr->first_value = last_operator;
        last_value->next = 0;
        last_operator->operand = operand;
    } else if (op->fixity_associativity == CONSTRUCT_INFIX_LEFT ||
     op->fixity_associativity == CONSTRUCT_INFIX_RIGHT) {
        expr->first_operator = op->next;
        struct construct_node *left = expr->first_value;
        struct construct_node *right = left->next;
        op->next = right->next;
        expr->first_value = op;

        op->left = FINISH_NODE_STRUCT(left, op->left, s->info);
        op->right = FINISH_NODE_STRUCT(right, op->right, s->info);
        construct_node_free(s, left);
        construct_node_free(s, right);
    } else {
        expr->first_operator = op->next;
        struct construct_node *value = expr->first_value;
        op->next = value->next;
        expr->first_value = op;

        op->operand = FINISH_NODE_STRUCT(value, op->operand, s->info);
        construct_node_free(s, value);
    }
}

static void construct_action_apply(struct construct_state *s, uint16_t action)
{
    switch (CONSTRUCT_ACTION_GET_TYPE(action)) {
    case CONSTRUCT_ACTION_END_SLOT: {
        struct construct_node *node = construct_node_alloc(s);
        node->next = s->under_construction;
        node->slot_index = CONSTRUCT_ACTION_GET_SLOT(action);
        node->choice_index = CONSTRUCT_ACTION_GET_CHOICE(action);
        node->binding = BINDING_LOOKUP(s->under_construction->binding,
         CONSTRUCT_ACTION_GET_SLOT(action), s->info);
        s->under_construction = node;
        break;
    }
    case CONSTRUCT_ACTION_END_EXPRESSION_SLOT: {
        struct construct_expression *expr = construct_expression_alloc(s);
        expr->parent = s->current_expression;
        s->current_expression = expr;
        expr->binding = BINDING_LOOKUP(s->under_construction->binding,
         CONSTRUCT_ACTION_GET_SLOT(action), s->info);
        expr->slot_index = CONSTRUCT_ACTION_GET_SLOT(action);
        break;
    }
    case CONSTRUCT_ACTION_BEGIN_SLOT: {
        struct construct_node *node = s->under_construction;
        s->under_construction = node->next;
        FINISHED_NODE_T *finished;
        finished = &s->under_construction->slots[node->slot_index];
        *finished = FINISH_NODE_STRUCT(node, *finished, s->info);
        construct_node_free(s, node);
        break;
    }
    case CONSTRUCT_ACTION_BEGIN_EXPRESSION_SLOT: {
        struct construct_expression *expr = s->current_expression;
        s->current_expression = expr->parent;
        while (expr->first_operator)
            construct_expression_reduce(s, expr);
        FINISHED_NODE_T *finished;
        finished = &s->under_construction->slots[expr->slot_index];
        struct construct_node *node = expr->first_value;
        if (node) {
            *finished = FINISH_NODE_STRUCT(node, *finished, s->info);
            assert(node->next == 0);
            construct_node_free(s, node);
        }
        construct_expression_free(s, expr);
        break;
    }
    case CONSTRUCT_ACTION_TOKEN_SLOT: {
        uint16_t slot = CONSTRUCT_ACTION_GET_SLOT(action);
        FINISHED_NODE_T *finished = &s->under_construction->slots[slot];
        *finished = FINISH_TOKEN(*finished, s->info);
        break;
    }
    case CONSTRUCT_ACTION_END_OPERAND: {
        struct construct_expression *expr = s->current_expression;
        struct construct_node *node = construct_node_alloc(s);
        node->choice_index = CONSTRUCT_ACTION_GET_CHOICE(action);
        node->binding = expr->binding;
        s->under_construction = node;
        break;
    }
    case CONSTRUCT_ACTION_END_OPERATOR: {
        struct construct_expression *expr = s->current_expression;
        struct construct_node *node = construct_node_alloc(s);
        node->choice_index = CONSTRUCT_ACTION_GET_CHOICE(action);
        node->binding = expr->binding;
        node->fixity_associativity = FIXITY_ASSOCIATIVITY_LOOKUP(expr->binding,
         CONSTRUCT_ACTION_GET_CHOICE(action), s->info);
        s->under_construction = node;
        break;
    }
    case CONSTRUCT_ACTION_BEGIN_OPERAND: {
        struct construct_expression *expr = s->current_expression;
        struct construct_node *node = s->under_construction;
        s->under_construction = node->next;
        node->next = expr->first_value;
        expr->first_value = node;
        break;
    }
    case CONSTRUCT_ACTION_BEGIN_OPERATOR: {
        struct construct_expression *expr = s->current_expression;
        struct construct_node *node = s->under_construction;
        s->under_construction = node->next;
        while (construct_expression_should_reduce(s, expr, node))
            construct_expression_reduce(s, expr);
        node->next = expr->first_operator;
        expr->first_operator = node;
        if (node->fixity_associativity == CONSTRUCT_PREFIX)
            construct_expression_reduce(s, expr);
        break;
    }
    }
}

)

#undef CONSTRUCT_ACTION
