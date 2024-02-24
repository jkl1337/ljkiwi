#ifndef LJKIWI_CKIWI_H_
#define LJKIWI_CKIWI_H_

#if !defined(_MSC_VER) || _MSC_VER >= 1900
   #undef LJKIWI_USE_FAM_1
#else
   #define LJKIWI_USE_FAM_1
#endif

#ifdef __cplusplus

namespace kiwi {
class VariableData;
class Constraint;
}  // namespace kiwi

typedef kiwi::VariableData KiwiVar;
typedef kiwi::ConstraintData KiwiConstraint;

extern "C" {

#else
typedef struct KiwiVar KiwiVar;
typedef struct KiwiConstraint KiwiConstraint;

#endif

#if __GNUC__
   #pragma GCC visibility push(default)
   #define LJKIWI_DATA_EXPORT __attribute__((visibility("default")))
#endif

// LuaJIT start
enum KiwiErrKind {
   KiwiErrNone,
   KiwiErrUnsatisfiableConstraint = 1,
   KiwiErrUnknownConstraint,
   KiwiErrDuplicateConstraint,
   KiwiErrUnknownEditVariable,
   KiwiErrDuplicateEditVariable,
   KiwiErrBadRequiredStrength,
   KiwiErrInternalSolverError,
   KiwiErrAlloc,
   KiwiErrNullObject,
   KiwiErrUnknown,
};

enum KiwiRelOp { KIWI_OP_LE, KIWI_OP_GE, KIWI_OP_EQ };

typedef struct KiwiTerm {
   KiwiVar* var;
   double coefficient;
} KiwiTerm;

typedef struct KiwiExpression {
   double constant;
   int term_count;
   KiwiConstraint* owner;

#if defined(LJKIWI_LUAJIT_DEF)
   KiwiTerm terms_[?];
#elif defined(LJKIWI_USE_FAM_1)
   KiwiTerm terms_[1];  // LuaJIT: struct KiwiTerm terms_[?];
#else
   KiwiTerm terms_[];
#endif

} KiwiExpression;

typedef struct KiwiErr {
   enum KiwiErrKind kind;
   const char* message;
   bool must_free;
} KiwiErr;

struct KiwiSolver;

KiwiVar* kiwi_var_construct(const char* name);
void kiwi_var_release(KiwiVar* var);
void kiwi_var_retain(KiwiVar* var);

const char* kiwi_var_name(const KiwiVar* var);
void kiwi_var_set_name(KiwiVar* var, const char* name);
double kiwi_var_value(const KiwiVar* var);
void kiwi_var_set_value(KiwiVar* var, double value);

void kiwi_expression_retain(KiwiExpression* expr);
void kiwi_expression_destroy(KiwiExpression* expr);

KiwiConstraint* kiwi_constraint_construct(
    const KiwiExpression* lhs,
    const KiwiExpression* rhs,
    enum KiwiRelOp op,
    double strength
);
void kiwi_constraint_release(KiwiConstraint* c);
void kiwi_constraint_retain(KiwiConstraint* c);

double kiwi_constraint_strength(const KiwiConstraint* c);
enum KiwiRelOp kiwi_constraint_op(const KiwiConstraint* c);
bool kiwi_constraint_violated(const KiwiConstraint* c);
int kiwi_constraint_expression(KiwiConstraint* c, KiwiExpression* out, int out_size);

KiwiSolver* kiwi_solver_construct(unsigned error_mask);
void kiwi_solver_destroy(KiwiSolver* s);
unsigned kiwi_solver_get_error_mask(const KiwiSolver* s);
void kiwi_solver_set_error_mask(KiwiSolver* s, unsigned mask);

const KiwiErr* kiwi_solver_add_constraint(KiwiSolver* s, KiwiConstraint* constraint);
const KiwiErr* kiwi_solver_remove_constraint(KiwiSolver* s, KiwiConstraint* constraint);
bool kiwi_solver_has_constraint(const KiwiSolver* s, KiwiConstraint* constraint);
const KiwiErr* kiwi_solver_add_edit_var(KiwiSolver* s, KiwiVar* var, double strength);
const KiwiErr* kiwi_solver_remove_edit_var(KiwiSolver* s, KiwiVar* var);
bool kiwi_solver_has_edit_var(const KiwiSolver* s, KiwiVar* var);
const KiwiErr* kiwi_solver_suggest_value(KiwiSolver* s, KiwiVar* var, double value);
void kiwi_solver_update_vars(KiwiSolver* sp);
void kiwi_solver_reset(KiwiSolver* sp);
void kiwi_solver_dump(const KiwiSolver* sp);
char* kiwi_solver_dumps(const KiwiSolver* sp);
// LuaJIT end

#if __GNUC__
   #pragma GCC visibility pop
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

// Local Variables:
// mode: c++
// End:
#endif  // LJKIWI_CKIWI_H_
