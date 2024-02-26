#include "ckiwi.h"

#include <kiwi/kiwi.h>

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
    "null object passed as argument #0 (self)"
};

static const constexpr KiwiErr kKiwiErrNullObjectArg1 {
    KiwiErrNullObject,
    "null object passed as argument #1"
};

template<typename F>
const KiwiErr* wrap_err(F&& f) {
   try {
      f();
   } catch (const UnsatisfiableConstraint& ex) {
      static const constexpr KiwiErr err {
          KiwiErrUnsatisfiableConstraint,
          "The constraint cannot be satisfied."
      };
      return &err;
   } catch (const UnknownConstraint& ex) {
      static const constexpr KiwiErr err {
          KiwiErrUnknownConstraint,
          "The constraint has not been added to the solver."
      };
      return &err;

   } catch (const DuplicateConstraint& ex) {
      static const constexpr KiwiErr err {
          KiwiErrDuplicateConstraint,
          "The constraint has already been added to the solver."
      };
      return &err;

   } catch (const UnknownEditVariable& ex) {
      static const constexpr KiwiErr err {
          KiwiErrUnknownEditVariable,
          "The edit variable has not been added to the solver."
      };
      return &err;

   } catch (const DuplicateEditVariable& ex) {
      static const constexpr KiwiErr err {
          KiwiErrDuplicateEditVariable,
          "The edit variable has already been added to the solver."
      };
      return &err;

   } catch (const BadRequiredStrength& ex) {
      static const constexpr KiwiErr err {
          KiwiErrBadRequiredStrength,
          "A required strength cannot be used in this context."
      };
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

}  // namespace

extern "C" {

KiwiVar* kiwi_var_construct(const char* name) {
   return make_unmanaged<VariableData>(lk_likely(name) ? name : "");
}

void kiwi_var_release(KiwiVar* var) {
   release_unmanaged(var);
}

void kiwi_var_retain(KiwiVar* var) {
   retain_unmanaged(var);
}

const char* kiwi_var_name(const KiwiVar* var) {
   return lk_likely(var) ? var->name().c_str() : "(<null>)";
}

void kiwi_var_set_name(KiwiVar* var, const char* name) {
   if (lk_likely(var && name))
      var->setName(name);
}

double kiwi_var_value(const KiwiVar* var) {
   return lk_likely(var) ? var->value() : std::numeric_limits<double>::quiet_NaN();
}

void kiwi_var_set_value(KiwiVar* var, double value) {
   if (lk_likely(var))
      var->setValue(value);
}

void kiwi_expression_retain(KiwiExpression* expr) {
   if (lk_unlikely(!expr))
      return;
   for (auto* t = expr->terms_; t != expr->terms_ + expr->term_count; ++t) {
      retain_unmanaged(t->var);
   }
   expr->owner = expr;
}

void kiwi_expression_destroy(KiwiExpression* expr) {
   if (lk_unlikely(!expr || !expr->owner))
      return;

   if (expr->owner == expr) {
      for (auto* t = expr->terms_; t != expr->terms_ + expr->term_count; ++t) {
         release_unmanaged(t->var);
      }
   } else {
      release_unmanaged(static_cast<ConstraintData*>(expr->owner));
   }
}

KiwiConstraint* kiwi_constraint_construct(
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

KiwiSolver* kiwi_solver_construct(unsigned error_mask) {
   return new KiwiSolver {error_mask};
}

void kiwi_solver_destroy(KiwiSolver* s) {
   if (lk_likely(s))
      delete s;
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
   return wrap_err(s, var, [strength](auto& s, auto&& v) {
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
