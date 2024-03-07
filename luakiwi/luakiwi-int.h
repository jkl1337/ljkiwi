#ifndef LUAKIWI_INT_H_
#define LUAKIWI_INT_H_

#include <kiwi/kiwi.h>

#include <cstring>
#include <new>

#include "luacompat.h"

#if defined(__GNUC__) && !defined(LJKIWI_NO_BUILTIN)
   #define lk_likely(x) (__builtin_expect(((x) != 0), 1))
   #define lk_unlikely(x) (__builtin_expect(((x) != 0), 0))
#else
   #define lk_likely(x) (x)
   #define lk_unlikely(x) (x)
#endif

namespace {

using namespace kiwi;

// Lua 5.1 compatibility for missing lua_arith.
inline void compat_arith_unm(lua_State* L) {
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM == 501
   int isnum;
   lua_Number n = lua_tonumberx(L, -1, &isnum);
   if (isnum) {
      lua_pop(L, 1);
      lua_pushnumber(L, -n);
   } else {
      if (!luaL_callmeta(L, -1, "__unm"))
         luaL_error(L, "attempt to perform arithmetic on a %s value", luaL_typename(L, -1));
      lua_replace(L, -2);
   }
#else
   lua_arith(L, LUA_OPUNM);
#endif
}

// This version supports placeholders.
inline void setfuncs(lua_State* L, const luaL_Reg* l, int nup) {
   luaL_checkstack(L, nup, "too many upvalues");
   for (; l->name != NULL; l++) { /* fill the table with given functions */
      if (l->func == NULL) /* place holder? */
         lua_pushboolean(L, 0);
      else {
         for (int i = 0; i < nup; i++) /* copy upvalues to the top */
            lua_pushvalue(L, -nup);
         lua_pushcclosure(L, l->func, nup); /* closure with those upvalues */
      }
      lua_setfield(L, -(nup + 2), l->name);
   }
   lua_pop(L, nup); /* remove upvalues */
}

template<typename T, std::size_t N>
constexpr int array_count(T (&)[N]) {
   return static_cast<int>(N);
}

inline void newlib(lua_State* L, const luaL_Reg* l) {
   lua_newtable(L);
   setfuncs(L, l, 0);
}

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

struct KiwiTerm {
   VariableData* var;
   double coefficient;
};

struct KiwiExpression {
   double constant;
   int term_count;
   ConstraintData* owner;

#if !defined(_MSC_VER) || _MSC_VER >= 1900
   KiwiTerm terms[];

   static constexpr std::size_t sz(int count) {
      return sizeof(KiwiExpression)
          + sizeof(KiwiTerm) * static_cast<std::size_t>(count > 0 ? count : 0);
   }
#else
   KiwiTerm terms[1];

   static constexpr std::size_t sz(int count) {
      return sizeof(KiwiExpression)
          + sizeof(KiwiTerm) * static_cast<std::size_t>(count > 0 ? count : 0);
   }
#endif

   KiwiExpression() = delete;
   KiwiExpression(const KiwiExpression&) = delete;
   KiwiExpression& operator=(const KiwiExpression&) = delete;
   ~KiwiExpression() = delete;
};

// This mechanism was initially designed for LuaJIT FFI.
struct KiwiErr {
   enum KiwiErrKind kind;
   const char* message;
   bool must_delete;
};

struct KiwiSolver {
   unsigned error_mask;
   Solver solver;
};

inline const KiwiErr* new_error(const KiwiErr* base, const std::exception& ex) {
   if (!std::strcmp(ex.what(), base->message))
      return base;

   const auto msg_n = std::strlen(ex.what()) + 1;

   auto* mem = static_cast<char*>(::operator new(sizeof(KiwiErr) + msg_n, std::nothrow));
   if (!mem) {
      return base;
   }
   auto* msg = mem + sizeof(KiwiErr);
   std::memcpy(msg, ex.what(), msg_n);
   return new (mem) KiwiErr {base->kind, msg, true};
}

template<typename F>
inline const KiwiErr* wrap_err(F&& f) {
   static const constexpr KiwiErr kKiwiErrUnhandledCxxException {
       KiwiErrUnknown,
       "An unhandled C++ exception occurred."
   };

   try {
      f();
   } catch (const UnsatisfiableConstraint&) {
      static const constexpr KiwiErr err {
          KiwiErrUnsatisfiableConstraint,
          "The constraint cannot be satisfied."
      };
      return &err;
   } catch (const UnknownConstraint&) {
      static const constexpr KiwiErr err {
          KiwiErrUnknownConstraint,
          "The constraint has not been added to the solver."
      };
      return &err;

   } catch (const DuplicateConstraint&) {
      static const constexpr KiwiErr err {
          KiwiErrDuplicateConstraint,
          "The constraint has already been added to the solver."
      };
      return &err;

   } catch (const UnknownEditVariable&) {
      static const constexpr KiwiErr err {
          KiwiErrUnknownEditVar,
          "The edit variable has not been added to the solver."
      };
      return &err;

   } catch (const DuplicateEditVariable&) {
      static const constexpr KiwiErr err {
          KiwiErrDuplicateEditVar,
          "The edit variable has already been added to the solver."
      };
      return &err;

   } catch (const BadRequiredStrength&) {
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
inline const KiwiErr* wrap_err(P&& s, F&& f) {
   return wrap_err([&]() { f(s); });
}

template<typename P, typename R, typename F>
inline const KiwiErr* wrap_err(P&& s, R&& ref, F&& f) {
   return wrap_err([&]() { f(s, ref); });
}

template<typename T, typename... Args>
inline T* make_unmanaged(Args... args) {
   auto* p = new (std::nothrow) T(std::forward<Args>(args)...);
   if (lk_unlikely(!p))
      return nullptr;

   p->m_refcount = 1;
   return p;
}

template<typename T>
inline void release_unmanaged(T* p) {
   if (lk_likely(p)) {
      if (--p->m_refcount == 0)
         delete p;
   }
}

template<typename T>
inline T* retain_unmanaged(T* p) {
   if (lk_likely(p))
      p->m_refcount++;
   return p;
}

inline ConstraintData* kiwi_constraint_new(
    const KiwiExpression* lhs,
    const KiwiExpression* rhs,
    RelationalOperator op,
    double strength
) {
   if (strength < 0.0) {
      strength = kiwi::strength::required;
   }

   try {
      std::vector<Term> terms;

      terms.reserve(static_cast<decltype(terms)::size_type>(
          (lhs && lhs->term_count > 0 ? lhs->term_count : 0)
          + (rhs && rhs->term_count > 0 ? rhs->term_count : 0)
      ));

      if (lhs) {
         for (int i = 0; i < lhs->term_count; ++i) {
            const auto& t = lhs->terms[i];
            terms.emplace_back(Variable(t.var), t.coefficient);
         }
      }
      if (rhs) {
         for (int i = 0; i < rhs->term_count; ++i) {
            const auto& t = rhs->terms[i];
            terms.emplace_back(Variable(t.var), -t.coefficient);
         }
      }
      return make_unmanaged<ConstraintData>(
          Expression(std::move(terms), (lhs ? lhs->constant : 0.0) - (rhs ? rhs->constant : 0.0)),
          static_cast<RelationalOperator>(op),
          strength
      );

   } catch (...) {
      return nullptr;
   }
}

inline const KiwiErr* kiwi_solver_add_constraint(Solver& s, ConstraintData* constraint) {
   return wrap_err(s, constraint, [](auto&& solver, auto&& c) {
      solver.addConstraint(Constraint(c));
   });
}

inline const KiwiErr* kiwi_solver_remove_constraint(Solver& s, ConstraintData* constraint) {
   return wrap_err(s, constraint, [](auto&& solver, auto&& c) {
      solver.removeConstraint(Constraint(c));
   });
}

inline const KiwiErr* kiwi_solver_add_edit_var(Solver& s, VariableData* var, double strength) {
   return wrap_err(s, var, [strength](auto&& solver, auto&& v) {
      solver.addEditVariable(Variable(v), strength);
   });
}

inline const KiwiErr* kiwi_solver_remove_edit_var(Solver& s, VariableData* var) {
   return wrap_err(s, var, [](auto&& solver, auto&& v) {
      solver.removeEditVariable(Variable(v));
   });
}

inline const KiwiErr* kiwi_solver_suggest_value(Solver& s, VariableData* var, double value) {
   return wrap_err(s, var, [value](auto&& solver, auto&& v) {
      solver.suggestValue(Variable(v), value);
   });
}

}  // namespace

// Local Variables:
// mode: c++
// End:

#endif  // LUAKIWI_INT_H_
