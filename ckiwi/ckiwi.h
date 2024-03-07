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
class ConstraintData;
}  // namespace kiwi

typedef kiwi::VariableData KiwiVar;
typedef kiwi::ConstraintData KiwiConstraint;

extern "C" {

#else
typedef struct KiwiVar KiwiVar;
typedef struct KiwiConstraint KiwiConstraint;

#endif

#if defined __GNUC__ && (!defined _WIN32 || defined __CYGWIN__)
   #define LJKIWI_EXP __attribute__((visibility("default")))
#elif defined _WIN32
   #define LJKIWI_EXP __declspec(dllexport)
#endif

// LuaJIT start
enum KiwiErrKind {
   KiwiErrNone,
   KiwiErrUnsatisfiableConstraint = 1,
   KiwiErrUnknownConstraint,
   KiwiErrDuplicateConstraint,
   KiwiErrUnknownEditVar,
   KiwiErrDuplicateEditVar,
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
   void* owner;

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
   bool must_release;
} KiwiErr;

struct KiwiSolver;
LJKIWI_EXP void kiwi_solver_type_info(unsigned sz_align[2]);

LJKIWI_EXP void kiwi_str_release(char* str);
LJKIWI_EXP void kiwi_err_release(const KiwiErr* err);

LJKIWI_EXP KiwiVar* kiwi_var_new(const char* name);
LJKIWI_EXP void kiwi_var_release(KiwiVar* var);
LJKIWI_EXP void kiwi_var_retain(KiwiVar* var);

LJKIWI_EXP const char* kiwi_var_name(const KiwiVar* var);
LJKIWI_EXP void kiwi_var_set_name(KiwiVar* var, const char* name);
LJKIWI_EXP double kiwi_var_value(const KiwiVar* var);
LJKIWI_EXP void kiwi_var_set_value(KiwiVar* var, double value);

LJKIWI_EXP void kiwi_expression_retain(KiwiExpression* expr);
LJKIWI_EXP void kiwi_expression_destroy(KiwiExpression* expr);

LJKIWI_EXP KiwiConstraint* kiwi_constraint_new(
    const KiwiExpression* lhs,
    const KiwiExpression* rhs,
    enum KiwiRelOp op,
    double strength
);
LJKIWI_EXP void kiwi_constraint_release(KiwiConstraint* c);
LJKIWI_EXP void kiwi_constraint_retain(KiwiConstraint* c);

LJKIWI_EXP double kiwi_constraint_strength(const KiwiConstraint* c);
LJKIWI_EXP enum KiwiRelOp kiwi_constraint_op(const KiwiConstraint* c);
LJKIWI_EXP bool kiwi_constraint_violated(const KiwiConstraint* c);
LJKIWI_EXP int kiwi_constraint_expression(KiwiConstraint* c, KiwiExpression* out, int out_size);

LJKIWI_EXP KiwiSolver* kiwi_solver_new(unsigned error_mask);
LJKIWI_EXP void kiwi_solver_free(KiwiSolver* s);
LJKIWI_EXP void kiwi_solver_init(KiwiSolver* s, unsigned error_mask);
LJKIWI_EXP void kiwi_solver_destroy(KiwiSolver* s);
LJKIWI_EXP unsigned kiwi_solver_get_error_mask(const KiwiSolver* s);
LJKIWI_EXP void kiwi_solver_set_error_mask(KiwiSolver* s, unsigned mask);

LJKIWI_EXP const KiwiErr* kiwi_solver_add_constraint(KiwiSolver* s, KiwiConstraint* constraint);
LJKIWI_EXP const KiwiErr*
kiwi_solver_remove_constraint(KiwiSolver* s, KiwiConstraint* constraint);
LJKIWI_EXP bool kiwi_solver_has_constraint(const KiwiSolver* s, KiwiConstraint* constraint);
LJKIWI_EXP const KiwiErr* kiwi_solver_add_edit_var(KiwiSolver* s, KiwiVar* var, double strength);
LJKIWI_EXP const KiwiErr* kiwi_solver_remove_edit_var(KiwiSolver* s, KiwiVar* var);
LJKIWI_EXP bool kiwi_solver_has_edit_var(const KiwiSolver* s, KiwiVar* var);
LJKIWI_EXP const KiwiErr* kiwi_solver_suggest_value(KiwiSolver* s, KiwiVar* var, double value);
LJKIWI_EXP void kiwi_solver_update_vars(KiwiSolver* sp);
LJKIWI_EXP void kiwi_solver_reset(KiwiSolver* sp);
LJKIWI_EXP void kiwi_solver_dump(const KiwiSolver* sp);
LJKIWI_EXP char* kiwi_solver_dumps(const KiwiSolver* sp);
// LuaJIT end

#ifdef __cplusplus
}  // extern "C"
#endif

// Local Variables:
// mode: c++
// End:
#endif  // LJKIWI_CKIWI_H_
