#include "ckiwi.h"

#include <kiwi/kiwi.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__GNUC__) && !defined(LJKIWI_NO_BUILTIN)
   #define lk_likely(x) (__builtin_expect(((x) != 0), 1))
   #define lk_unlikely(x) (__builtin_expect(((x) != 0), 0))
#else
   #define lk_likely(x) (x)
   #define lk_unlikely(x) (x)
#endif

namespace {

using namespace kiwi;

const KiwiErr* new_error(const KiwiErr* base, const std::exception& ex) {
   if (!std::strcmp(ex.what(), base->message))
      return base;

   const auto msg_n = std::strlen(ex.what()) + 1;

   auto* mem = static_cast<char*>(std::malloc(sizeof(KiwiErr) + msg_n));
   if (!mem) {
      return base;
   }

   const auto* err = new (mem) KiwiErr {base->kind, mem + sizeof(KiwiErr), true};
   std::memcpy(const_cast<char*>(err->message), ex.what(), msg_n);
   return err;
}

static const constexpr KiwiErr kKiwiErrUnhandledCxxException {
    KiwiErrUnknown,
    "An unhandled C++ exception occurred."
};

static const constexpr KiwiErr kKiwiErrNullObjectArg0 {
    KiwiErrNullObject,
    "null object passed as argument #0 (self)."
};

static const constexpr KiwiErr kKiwiErrNullObjectArg1 {
    KiwiErrNullObject,
    "null object passed as argument #1."
};

template<typename F>
const KiwiErr* wrap_err(F&& f) {
   try {
      f();
   } catch (const UnsatisfiableConstraint& ex) {
      static const constexpr KiwiErr err {KiwiErrUnsatisfiableConstraint};
      return &err;
   } catch (const UnknownConstraint& ex) {
      static const constexpr KiwiErr err {KiwiErrUnknownConstraint};
      return &err;

   } catch (const DuplicateConstraint& ex) {
      static const constexpr KiwiErr err {KiwiErrDuplicateConstraint};
      return &err;

   } catch (const UnknownEditVariable& ex) {
      static const constexpr KiwiErr err {KiwiErrUnknownEditVar};
      return &err;

   } catch (const DuplicateEditVariable& ex) {
      static const constexpr KiwiErr err {KiwiErrDuplicateEditVar};
      return &err;

   } catch (const BadRequiredStrength& ex) {
      static const constexpr KiwiErr err {KiwiErrBadRequiredStrength};
      return &err;

   } catch (const InternalSolverError& ex) {
      static const constexpr KiwiErr base {
          KiwiErrInternalSolverError,
          "An internal solver error occurred."
      };
      return new_error(&base, ex);
   } catch (std::bad_alloc&) {
      static const constexpr KiwiErr err {KiwiErrAlloc, "A memory allocation failed."};
      return &err;
   } catch (const std::exception& ex) {
      return new_error(&kKiwiErrUnhandledCxxException, ex);
   } catch (...) {
      return &kKiwiErrUnhandledCxxException;
   }
   return nullptr;
}

template<typename P, typename R, typename F>
const KiwiErr* wrap_err(P self, F&& f) {
   if (lk_unlikely(!self)) {
      return &kKiwiErrNullObjectArg0;
   }
   return wrap_err([&]() { f(self->solver); });
}

template<typename P, typename R, typename F>
const KiwiErr* wrap_err(P* self, R* item, F&& f) {
   if (lk_unlikely(!self)) {
      return &kKiwiErrNullObjectArg0;
   } else if (lk_unlikely(!item)) {
      return &kKiwiErrNullObjectArg1;
   }
   return wrap_err([&]() { f(self->solver, item); });
}

template<typename T, typename... Args>
T* make_unmanaged(Args... args) {
   auto* o = new T(std::forward<Args>(args)...);
   o->m_refcount = 1;
   return o;
}

template<typename T>
void release_unmanaged(T* p) {
   if (lk_likely(p)) {
      if (--p->m_refcount == 0)
         delete p;
   }
}

template<typename T>
T* retain_unmanaged(T* p) {
   if (lk_likely(p))
      p->m_refcount++;
   return p;
}

KiwiVar* var_retain(KiwiVar* var) {
   if (lk_likely(var))
      var->retain();
   return var;
}

void var_release(KiwiVar* var) {
   if (lk_likely(var))
      var->release();
}

}  // namespace

extern "C" {

void kiwi_str_release(char* str) {
   if (lk_likely(str))
      std::free(str);
}

void kiwi_err_release(const KiwiErr* err) {
   if (lk_likely(err && err->must_release))
      std::free(const_cast<KiwiErr*>(err));
}

KiwiVar* kiwi_var_new(const char* name) {
   return VariableData::alloc(name);
}

void kiwi_var_free(KiwiVar* var) {
   if (lk_likely(var)) {
      var->free();
   }
}

const char* kiwi_var_name(const KiwiVar* var) {
   return lk_likely(var) ? var->name() : "(<null>)";
}

void kiwi_var_set_name(KiwiVar* var, const char* name) {
   if (lk_likely(var && name))
      var->setName(name);
}

double kiwi_var_value(const KiwiVar* var) {
   return lk_likely(var) ? var->value_ : std::numeric_limits<double>::quiet_NaN();
}

void kiwi_var_set_value(KiwiVar* var, double value) {
   if (lk_likely(var))
      var->value_ = value;
}

void kiwi_expression_retain(KiwiExpression* expr) {
   if (lk_unlikely(!expr))
      return;
   for (auto* t = expr->terms_; t != expr->terms_ + std::max(expr->term_count, 0); ++t) {
      var_retain(t->var);
   }
   expr->owner = expr;
}

void kiwi_expression_destroy(KiwiExpression* expr) {
   if (lk_unlikely(!expr || !expr->owner))
      return;

   if (expr->owner == expr) {
      for (auto* t = expr->terms_; t != expr->terms_ + std::max(expr->term_count, 0); ++t) {
         var_release(t->var);
      }
   } else {
      release_unmanaged(static_cast<ConstraintData*>(expr->owner));
   }
}

void kiwi_expression_add_term(
    const KiwiExpression* expr,
    KiwiVar* var,
    double coefficient,
    KiwiExpression* out
) {
   if (lk_unlikely(!expr || expr->term_count == INT_MAX || expr->term_count < 0)) {
      out->term_count = 0;
      return;
   }

   out->owner = out;
   out->term_count = expr->term_count + 1;
   out->constant = expr->constant;

   auto* d = out->terms_;
   for (auto* s = expr->terms_; s != expr->terms_ + expr->term_count; ++s, ++d) {
      d->var = var_retain(s->var);
      d->coefficient = s->coefficient;
   }
   d->var = var_retain(var);
   d->coefficient = coefficient;
}

void kiwi_expression_set_constant(
    const KiwiExpression* expr,
    double constant,
    KiwiExpression* out
) {
   if (lk_unlikely(!expr || expr->term_count < 0)) {
      out->term_count = 0;
      return;
   }

   out->owner = out;
   out->term_count = expr->term_count;
   out->constant = constant;

   auto* d = out->terms_;
   for (auto* s = expr->terms_; s != expr->terms_ + expr->term_count; ++s, ++d) {
      d->var = var_retain(s->var);
      d->coefficient = s->coefficient;
   }
}

KiwiConstraint* kiwi_constraint_new(
    const KiwiExpression* lhs,
    const KiwiExpression* rhs,
    enum KiwiRelOp op,
    double strength
) {
   if (strength < 0.0) {
      strength = kiwi::strength::required;
   }

   std::vector<Term> terms;
   terms.reserve(static_cast<decltype(terms)::size_type>(
       (lhs && lhs->term_count > 0 ? lhs->term_count : 0)
       + (rhs && rhs->term_count > 0 ? rhs->term_count : 0)
   ));

   if (lhs) {
      for (int i = 0; i < lhs->term_count; ++i) {
         const auto& t = lhs->terms_[i];
         if (t.var)
            terms.emplace_back(Variable(t.var), t.coefficient);
      }
   }
   if (rhs) {
      for (int i = 0; i < rhs->term_count; ++i) {
         const auto& t = rhs->terms_[i];
         if (t.var)
            terms.emplace_back(Variable(t.var), -t.coefficient);
      }
   }

   return make_unmanaged<ConstraintData>(
       Expression(std::move(terms), (lhs ? lhs->constant : 0.0) - (rhs ? rhs->constant : 0.0)),
       static_cast<RelationalOperator>(op),
       strength
   );
}

void kiwi_constraint_release(KiwiConstraint* c) {
   release_unmanaged(c);
}

void kiwi_constraint_retain(KiwiConstraint* c) {
   retain_unmanaged(c);
}

double kiwi_constraint_strength(const KiwiConstraint* c) {
   return lk_likely(c) ? c->strength() : std::numeric_limits<double>::quiet_NaN();
}

enum KiwiRelOp kiwi_constraint_op(const KiwiConstraint* c) {
   return lk_likely(c) ? static_cast<KiwiRelOp>(c->op()) : KiwiRelOp::KIWI_OP_EQ;
}

bool kiwi_constraint_violated(const KiwiConstraint* c) {
   return lk_likely(c) ? c->violated() : false;
}

int kiwi_constraint_expression(KiwiConstraint* c, KiwiExpression* out, int out_size) {
   if (lk_unlikely(!c))
      return 0;

   const auto& expr = c->expression();
   const auto& terms = expr.terms();
   int n = terms.size() < INT_MAX ? static_cast<int>(terms.size()) : INT_MAX;
   if (!out || out_size < n)
      return n;

   for (int i = 0; i < n; ++i) {
      const auto& t = terms[static_cast<std::size_t>(i)];
      out->terms_[i].var = const_cast<Variable&>(t.variable()).ptr();
      out->terms_[i].coefficient = t.coefficient();
   }
   out->constant = expr.constant();
   out->term_count = n;
   out->owner = retain_unmanaged(c);

   return n;
}

struct KiwiSolver {
   unsigned error_mask;
   Solver solver;
};

void kiwi_solver_type_layout(unsigned sz_align[2]) {
   sz_align[0] = sizeof(KiwiSolver);
   sz_align[1] = alignof(KiwiSolver);
}

KiwiSolver* kiwi_solver_new(unsigned error_mask) {
   return new KiwiSolver {error_mask};
}

void kiwi_solver_free(KiwiSolver* s) {
   if (lk_likely(s))
      delete s;
}

void kiwi_solver_init(KiwiSolver* s, unsigned error_mask) {
   new (s) KiwiSolver {error_mask};
}

void kiwi_solver_destroy(KiwiSolver* s) {
   if (lk_likely(s))
      s->~KiwiSolver();
}

unsigned kiwi_solver_get_error_mask(const KiwiSolver* s) {
   return lk_likely(s) ? s->error_mask : 0;
}

void kiwi_solver_set_error_mask(KiwiSolver* s, unsigned mask) {
   if (lk_likely(s))
      s->error_mask = mask;
}

const KiwiErr* kiwi_solver_add_constraint(KiwiSolver* s, KiwiConstraint* constraint) {
   return wrap_err(s, constraint, [](auto&& s, auto&& c) { s.addConstraint(Constraint(c)); });
}

const KiwiErr* kiwi_solver_remove_constraint(KiwiSolver* s, KiwiConstraint* constraint) {
   return wrap_err(s, constraint, [](auto&& s, auto&& c) { s.removeConstraint(Constraint(c)); });
}

bool kiwi_solver_has_constraint(const KiwiSolver* s, KiwiConstraint* constraint) {
   if (lk_unlikely(!s || !constraint))
      return 0;
   return s->solver.hasConstraint(Constraint(constraint));
}

const KiwiErr* kiwi_solver_add_edit_var(KiwiSolver* s, KiwiVar* var, double strength) {
   return wrap_err(s, var, [strength](auto&& s, auto&& v) {
      s.addEditVariable(Variable(v), strength);
   });
}

const KiwiErr* kiwi_solver_remove_edit_var(KiwiSolver* s, KiwiVar* var) {
   return wrap_err(s, var, [](auto&& s, auto&& v) { s.removeEditVariable(Variable(v)); });
}

bool kiwi_solver_has_edit_var(const KiwiSolver* s, KiwiVar* var) {
   if (lk_unlikely(!s || !var))
      return 0;
   return s->solver.hasEditVariable(Variable(var));
}

const KiwiErr* kiwi_solver_suggest_value(KiwiSolver* s, KiwiVar* var, double value) {
   return wrap_err(s, var, [value](auto&& s, auto&& v) { s.suggestValue(Variable(v), value); });
}

void kiwi_solver_update_vars(KiwiSolver* s) {
   if (lk_likely(s))
      s->solver.updateVariables();
}

void kiwi_solver_reset(KiwiSolver* s) {
   if (lk_likely(s))
      s->solver.reset();
}

void kiwi_solver_dump(const KiwiSolver* s) {
   if (lk_likely(s))
      s->solver.dump();
}

char* kiwi_solver_dumps(const KiwiSolver* s) {
   if (lk_unlikely(!s))
      return nullptr;

   const auto& str = s->solver.dumps();
   const auto buf_size = str.size() + 1;
   auto* buf = static_cast<char*>(std::malloc(buf_size));
   if (!buf)
      return nullptr;
   std::memcpy(buf, str.c_str(), str.size() + 1);
   return buf;
}

}  // extern "C"
