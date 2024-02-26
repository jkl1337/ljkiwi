#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "luakiwi-int.h"

namespace {

// Note some of the internal functions do not bother cleaning up the stack, they
// are marked with accordingly.

enum TypeId { NOTYPE, VAR = 1, TERM, EXPR, CONSTRAINT, SOLVER, ERROR, NUMBER };

enum { ERR_KIND_TAB = NUMBER + 1, VAR_SUB_FN, MEM_ERR_MSG, CONTEXT_TAB_MAX };

constexpr const char* const lkiwi_error_kinds[] = {
    "KiwiErrNone",
    "KiwiErrUnsatisfiableConstraint",
    "KiwiErrUnknownConstraint",
    "KiwiErrDuplicateConstraint",
    "KiwiErrUnknownEditVariable",
    "KiwiErrDuplicateEditVariable",
    "KiwiErrBadRequiredStrength",
    "KiwiErrInternalSolverError",
    "KiwiErrAlloc",
    "KiwiErrNullObject",
    "KiwiErrUnknown",
};

const double STRENGTH_REQUIRED = 1001001000.0;
const double STRENGTH_STRONG = 1000000.0;
const double STRENGTH_MEDIUM = 1000.0;
const double STRENGTH_WEAK = 1.0;

kiwi::RelationalOperator get_op_opt(lua_State* L, int idx) {
   size_t opn;
   const char* op = luaL_optlstring(L, idx, "EQ", &opn);

   if (opn == 2) {
      if (op[0] == 'E' && op[1] == 'Q') {
         return kiwi::OP_EQ;
      } else if (op[0] == 'L' && op[1] == 'E') {
         return kiwi::OP_LE;
      } else if (op[0] == 'G' && op[1] == 'E') {
         return kiwi::OP_GE;
      }
   }
   luaL_argerror(L, idx, "invalid operator");
   return kiwi::OP_EQ;
}

inline void push_type(lua_State* L, int type_id) {
   lua_rawgeti(L, lua_upvalueindex(1), type_id);
}

// stack disposition: dirty
inline int is_udata_obj(lua_State* L, int type_id) {
   int result = 0;
   if (lua_isuserdata(L, 1) && lua_getmetatable(L, 1)) {
      push_type(L, type_id);
      result = lua_rawequal(L, -1, -2);
   }
   lua_pushboolean(L, result);
   return 1;
}

// get typename, copy the stack string to tidx, helpful when using
// with buffers.
const char* lk_typename(lua_State* L, int idx, int tidx) {
   const char* ret = 0;
   if (lua_getmetatable(L, idx)) {
      lua_getfield(L, -1, "__name");
      ret = lua_tolstring(L, -1, 0);
      lua_replace(L, tidx);
      lua_pop(L, 1);
   }

   return ret ? ret : luaL_typename(L, idx);
}

// never returns
int op_error(lua_State* L, const char* op, int lidx, int ridx) {
   luaL_Buffer buf;
   size_t len;
   const char* str;

   // scratch space for strings
   lua_pushnil(L);
   int stridx = lua_gettop(L);

   luaL_buffinit(L, &buf);
   lua_pushfstring(L, "invalid operand type for '%s' %s('", op, lk_typename(L, lidx, stridx));
   luaL_addvalue(&buf);

   str = luaL_tolstring(L, lidx, &len);
   lua_replace(L, stridx);
   luaL_addlstring(&buf, str, len < 100 ? len : 100);

   lua_pushfstring(L, "') and %s('", lk_typename(L, ridx, stridx));
   luaL_addvalue(&buf);

   str = luaL_tolstring(L, ridx, &len);
   lua_replace(L, stridx);
   luaL_addlstring(&buf, str, len < 100 ? len : 100);

   luaL_addstring(&buf, "')");
   luaL_pushresult(&buf);
   lua_error(L);
   return 0;
}

void check_arg_error(lua_State* L, int idx, int have_mt) {
   lua_pushstring(L, "__name");
   lua_rawget(L, -2);
   // TODO: simplify this. This is a bit of a hack to deal with missing args.
   // Also these error messages are funky when idx is negative.
   int top = lua_gettop(L);
   if (idx > 0 && top <= 2 + have_mt) {
      lua_pushnil(L);
      lua_replace(L, idx);
   }
   luaL_typeerror(L, idx < 0 ? top + idx - have_mt - 2 + 1 : idx, lua_tostring(L, -1));
}

inline void* check_arg(lua_State* L, int idx, int type_id) {
   void* udp = lua_touserdata(L, idx);
   int have_mt = lua_getmetatable(L, idx);
   push_type(L, type_id);

   if (lk_unlikely(!udp || !have_mt || !lua_rawequal(L, -1, -2)))
      check_arg_error(L, idx, have_mt);

   lua_pop(L, 2);
   return udp;
}

inline void* try_type(lua_State* L, int idx, TypeId type_id) {
   void* p = lua_touserdata(L, idx);
   if (!p || !lua_getmetatable(L, idx))
      return 0;
   push_type(L, type_id);
   return lua_rawequal(L, -1, -2) ? p : 0;
}

inline VariableData* try_var(lua_State* L, int idx) {
   return *static_cast<VariableData**>(try_type(L, idx, VAR));
}

inline KiwiTerm* try_term(lua_State* L, int idx) {
   return static_cast<KiwiTerm*>(try_type(L, idx, TERM));
}

inline KiwiExpression* try_expr(lua_State* L, int idx) {
   return static_cast<KiwiExpression*>(try_type(L, idx, EXPR));
}

// method to test types for expression functions
// stack disposition: dirty
inline void* try_arg(lua_State* L, int idx, TypeId* type_id, double* num) {
   void* p = lua_touserdata(L, idx);
   if (!p || !lua_getmetatable(L, idx)) {
      int isnum;
      *num = lua_tonumberx(L, idx, &isnum);
      if (isnum) {
         *type_id = NUMBER;
      } else
         *type_id = NOTYPE;
      return 0;
   }

   push_type(L, EXPR);
   if (lua_rawequal(L, -1, -2)) {
      *type_id = EXPR;
      return p;
   }
   push_type(L, VAR);
   if (lua_rawequal(L, -1, -3)) {
      *type_id = VAR;
      return p;
   }
   push_type(L, TERM);
   if (lua_rawequal(L, -1, -4)) {
      *type_id = TERM;
      return p;
   }
   *type_id = NOTYPE;
   return 0;
}

inline VariableData* get_var(lua_State* L, int idx) {
   return *static_cast<VariableData**>(check_arg(L, idx, VAR));
}

inline KiwiTerm* get_term(lua_State* L, int idx) {
   return static_cast<KiwiTerm*>(check_arg(L, idx, TERM));
}

inline KiwiExpression* get_expr(lua_State* L, int idx) {
   return static_cast<KiwiExpression*>(check_arg(L, idx, EXPR));
}

inline KiwiExpression* get_expr_opt(lua_State* L, int idx) {
   if (lua_isnoneornil(L, idx)) {
      return 0;
   }
   return static_cast<KiwiExpression*>(check_arg(L, idx, EXPR));
}

inline ConstraintData* get_constraint(lua_State* L, int idx) {
   return *static_cast<ConstraintData**>(check_arg(L, idx, CONSTRAINT));
}

inline KiwiSolver* get_solver(lua_State* L, int idx) {
   return static_cast<KiwiSolver*>(check_arg(L, idx, SOLVER));
}

VariableData** var_new(lua_State* L) {
   auto** varp = static_cast<VariableData**>(lua_newuserdata(L, sizeof(VariableData*)));
   push_type(L, VAR);
   lua_setmetatable(L, -2);
   return varp;
}

// note this expects the 2nd upvalue to have the variable weak table
VariableData* var_register(lua_State* L, VariableData* var) {
   if (lk_unlikely(!var)) {
      lua_rawgeti(L, lua_upvalueindex(1), MEM_ERR_MSG);
      lua_error(L);
   }
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM == 501
   // a true compatibility shim has performance implications here
   lua_pushlightuserdata(L, var);
   lua_pushvalue(L, -2);
   lua_rawset(L, lua_upvalueindex(2));
#else
   lua_pushvalue(L, -1);
   lua_rawsetp(L, lua_upvalueindex(2), var);
#endif
   return var;
}

KiwiTerm* term_new(lua_State* L) {
   auto* term = static_cast<KiwiTerm*>(lua_newuserdata(L, sizeof(KiwiTerm)));
   push_type(L, TERM);
   lua_setmetatable(L, -2);
   return term;
}

inline KiwiExpression* expr_new(lua_State* L, int nterms) {
   auto* expr = static_cast<KiwiExpression*>(lua_newuserdata(L, KiwiExpression::sz(nterms)));
   expr->owner = nullptr;
   push_type(L, EXPR);
   lua_setmetatable(L, -2);
   return expr;
}

inline ConstraintData* constraint_new(
    lua_State* L,
    const KiwiExpression* lhs,
    const KiwiExpression* rhs,
    kiwi::RelationalOperator op,
    double strength
) {
   auto** c = static_cast<ConstraintData**>(lua_newuserdata(L, sizeof(ConstraintData*)));
   push_type(L, CONSTRAINT);
   lua_setmetatable(L, -2);

   if (lk_unlikely(!(*c = kiwi_constraint_new(lhs, rhs, op, strength)))) {
      lua_rawgeti(L, lua_upvalueindex(1), MEM_ERR_MSG);
      lua_error(L);
   }
   return *c;
}

// stack disposition: dirty
KiwiExpression* toexpr(lua_State* L, int idx, KiwiExpression* temp) {
   void* ud = lua_touserdata(L, idx);

   if (!ud) {
      int isnum;
      temp->constant = lua_tonumberx(L, idx, &isnum);
      temp->term_count = 0;
      return isnum ? temp : 0;
   }
   if (!lua_getmetatable(L, idx))
      return 0;

   push_type(L, EXPR);
   if (lua_rawequal(L, -1, -2)) {
      return static_cast<KiwiExpression*>(ud);
   }

   temp->constant = 0;
   temp->term_count = 1;
   push_type(L, VAR);
   if (lua_rawequal(L, -1, -3)) {
      temp->terms[0].var = *static_cast<VariableData**>(ud);
      temp->terms[0].coefficient = 1.0;
      return temp;
   }
   push_type(L, TERM);
   if (lua_rawequal(L, -1, -4)) {
      temp->terms[0] = *static_cast<KiwiTerm*>(ud);
      return temp;
   }
   return 0;
}

int relop(lua_State* L, kiwi::RelationalOperator op, const char opdisp[2]) {
   alignas(KiwiExpression) unsigned char tmpl[KiwiExpression::sz(1)];
   alignas(KiwiExpression) unsigned char tmpr[KiwiExpression::sz(1)];
   double strength = luaL_optnumber(L, 3, STRENGTH_REQUIRED);
   const auto* lhs = toexpr(L, 1, reinterpret_cast<KiwiExpression*>(tmpl));
   const auto* rhs = toexpr(L, 2, reinterpret_cast<KiwiExpression*>(tmpr));

   if (!lhs || !rhs) {
      op_error(L, opdisp, 1, 2);
   }

   constraint_new(L, lhs, rhs, op, strength);
   return 1;
}

int lkiwi_eq(lua_State* L) {
   return relop(L, kiwi::OP_EQ, "==");
}

int lkiwi_le(lua_State* L) {
   return relop(L, kiwi::OP_LE, "<=");
}

int lkiwi_ge(lua_State* L) {
   return relop(L, kiwi::OP_GE, ">=");
}

inline int push_expr_one(lua_State* L, double constant, const KiwiTerm* term) {
   auto* expr = expr_new(L, 1);
   expr->constant = constant;
   expr->term_count = 1;
   expr->terms[0].coefficient = term->coefficient;
   expr->terms[0].var = retain_unmanaged(term->var);
   return 1;
}

inline int push_expr_pair(lua_State* L, double constant, const KiwiTerm* ta, const KiwiTerm* tb) {
   auto* e = expr_new(L, 2);
   e->constant = constant;
   e->term_count = 2;
   e->terms[0].coefficient = ta->coefficient;
   e->terms[0].var = retain_unmanaged(ta->var);
   e->terms[1].coefficient = tb->coefficient;
   e->terms[1].var = retain_unmanaged(tb->var);
   return 1;
}

inline int
push_expr_var_term(lua_State* L, double constant, VariableData* var, const KiwiTerm* t) {
   auto* e = expr_new(L, 2);
   e->constant = constant;
   e->term_count = 2;
   e->terms[0].coefficient = 1.0;
   e->terms[0].var = retain_unmanaged(var);
   e->terms[1].coefficient = t->coefficient;
   e->terms[1].var = retain_unmanaged(t->var);
   return 1;
}

int push_add_expr_term(lua_State* L, const KiwiExpression* expr, const KiwiTerm* t) {
   auto* e = expr_new(L, expr->term_count + 1);
   e->constant = expr->constant;
   e->term_count = expr->term_count + 1;
   int i = 0;
   for (; i < expr->term_count; ++i) {
      e->terms[i].coefficient = expr->terms[i].coefficient;
      e->terms[i].var = retain_unmanaged(expr->terms[i].var);
   }
   e->terms[i].coefficient = t->coefficient;
   e->terms[i].var = retain_unmanaged(t->var);
   return 1;
}

int lkiwi_var_m_add(lua_State* L) {
   TypeId type_id_b;
   double num = 0.0;
   void* arg_b = try_arg(L, 2, &type_id_b, &num);

   if (type_id_b == VAR) {
      int isnum_a;
      num = lua_tonumberx(L, 1, &isnum_a);
      if (isnum_a) {
         const KiwiTerm t {*static_cast<VariableData**>(arg_b), 1.0};
         return push_expr_one(L, num, &t);
      }
   }

   auto* var_a = try_var(L, 1);
   if (var_a) {
      switch (type_id_b) {
         case VAR: {
            const KiwiTerm ta {var_a, 1.0}, tb {*static_cast<VariableData**>(arg_b), 1.0};
            return push_expr_pair(L, 0.0, &ta, &tb);
         }
         case TERM:
            return push_expr_var_term(L, 0.0, var_a, (static_cast<KiwiTerm*>(arg_b)));
         case EXPR: {
            const KiwiTerm t {var_a, 1.0};
            return push_add_expr_term(L, static_cast<KiwiExpression*>(arg_b), &t);
         }
         case NUMBER: {
            const KiwiTerm t {var_a, 1.0};
            return push_expr_one(L, num, &t);
         }
         default:
            break;
      }
   }
   return op_error(L, "+", 1, 2);
}

int lkiwi_var_m_sub(lua_State* L) {
   lua_settop(L, 2);
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM == 501
   lua_rawgeti(L, lua_upvalueindex(1), VAR_SUB_FN);
   lua_insert(L, 1);
   lua_call(L, 2, 1);
#else
   lua_arith(L, LUA_OPUNM);
   lua_arith(L, LUA_OPADD);
#endif
   return 1;
}

int lkiwi_var_m_mul(lua_State* L) {
   int isnum, varidx = 2;
   double num = lua_tonumberx(L, 1, &isnum);

   if (!isnum) {
      varidx = 1;
      num = lua_tonumberx(L, 2, &isnum);
   }

   if (isnum) {
      auto* var = try_var(L, varidx);
      if (var) {
         auto* term = term_new(L);
         term->var = retain_unmanaged(var);
         term->coefficient = num;
         return 1;
      }
   }
   return op_error(L, "*", 1, 2);
}

int lkiwi_var_m_div(lua_State* L) {
   auto* var = try_var(L, 1);
   int isnum;
   double num = lua_tonumberx(L, 2, &isnum);
   if (!var || !isnum) {
      return op_error(L, "/", 1, 2);
   }
   auto* term = term_new(L);
   term->var = retain_unmanaged(var);
   term->coefficient = 1.0 / num;
   return 1;
}

int lkiwi_var_m_unm(lua_State* L) {
   auto* term = term_new(L);
   term->var = retain_unmanaged(get_var(L, 1));
   term->coefficient = -1.0;
   return 1;
}

int lkiwi_var_m_eq(lua_State* L) {
   lua_pushboolean(L, get_var(L, 1) == get_var(L, 2));
   return 1;
}

int lkiwi_var_m_tostring(lua_State* L) {
   auto* var = get_var(L, 1);
   lua_pushfstring(L, "%s(%f)", var->name().c_str(), var->value());
   return 1;
}

int lkiwi_var_m_gc(lua_State* L) {
   release_unmanaged(get_var(L, 1));
   return 0;
}

int lkiwi_var_set_name(lua_State* L) {
   auto* var = get_var(L, 1);
   const char* name = luaL_checkstring(L, 2);
   var->setName(name);
   return 0;
}

int lkiwi_var_name(lua_State* L) {
   lua_pushstring(L, get_var(L, 1)->name().c_str());
   return 1;
}

int lkiwi_var_set(lua_State* L) {
   auto* var = get_var(L, 1);
   const double value = luaL_checknumber(L, 2);
   var->setValue(value);
   return 0;
}

int lkiwi_var_value(lua_State* L) {
   lua_pushnumber(L, get_var(L, 1)->value());
   return 1;
}

int lkiwi_var_toterm(lua_State* L) {
   auto* var = get_var(L, 1);
   double coefficient = luaL_optnumber(L, 2, 1.0);
   auto* term = term_new(L);

   term->var = retain_unmanaged(var);
   term->coefficient = coefficient;

   return 1;
}

int lkiwi_var_toexpr(lua_State* L) {
   const KiwiTerm t {get_var(L, 1), 1.0};
   return push_expr_one(L, 0.0, &t);
}

constexpr const struct luaL_Reg kiwi_var_m[] = {
    {"__add", lkiwi_var_m_add},
    {"__sub", lkiwi_var_m_sub},
    {"__mul", lkiwi_var_m_mul},
    {"__div", lkiwi_var_m_div},
    {"__unm", lkiwi_var_m_unm},
    {"__eq", lkiwi_var_m_eq},
    {"__tostring", lkiwi_var_m_tostring},
    {"__gc", lkiwi_var_m_gc},
    {"name", lkiwi_var_name},
    {"set_name", lkiwi_var_set_name},
    {"value", lkiwi_var_value},
    {"set", lkiwi_var_set},
    {"toterm", lkiwi_var_toterm},
    {"toexpr", lkiwi_var_toexpr},
    {"eq", lkiwi_eq},
    {"le", lkiwi_le},
    {"ge", lkiwi_ge},
    {0, 0}
};

int lkiwi_var_new(lua_State* L) {
   const char* name = luaL_optstring(L, 1, "");

   auto* varp = var_new(L);
   var_register(L, *varp = make_unmanaged<VariableData>(name));

   return 1;
}

int lkiwi_term_m_add(lua_State* L) {
   TypeId type_id_b;
   double num = 0.0;
   void* arg_b = try_arg(L, 2, &type_id_b, &num);

   if (type_id_b == TERM) {
      int isnum_a;
      num = lua_tonumberx(L, 1, &isnum_a);
      if (isnum_a) {
         return push_expr_one(L, num, (const KiwiTerm*)arg_b);
      }
   }

   const auto* term_a = try_term(L, 1);
   if (term_a) {
      switch (type_id_b) {
         case TERM:
            return push_expr_pair(L, 0.0, term_a, static_cast<KiwiTerm*>(arg_b));
         case VAR: {
            const KiwiTerm term_b {*static_cast<VariableData**>(arg_b), 1.0};
            return push_expr_pair(L, 0.0, term_a, &term_b);
         }
         case EXPR:
            return push_add_expr_term(L, static_cast<KiwiExpression*>(arg_b), term_a);
         case NUMBER:
            return push_expr_one(L, num, term_a);
         default:
            break;
      }
   }
   return op_error(L, "+", 1, 2);
}

int lkiwi_term_m_sub(lua_State* L) {
   lua_settop(L, 2);
   compat_arith_unm(L);
   lkiwi_term_m_add(L);
   return 1;
}

int lkiwi_term_m_mul(lua_State* L) {
   int isnum, termidx = 2;
   double num = lua_tonumberx(L, 1, &isnum);

   if (!isnum) {
      termidx = 1;
      num = lua_tonumberx(L, 2, &isnum);
   }

   if (isnum) {
      const auto* term = try_term(L, termidx);
      if (term) {
         auto* ret = term_new(L);
         ret->var = retain_unmanaged(term->var);
         ret->coefficient = term->coefficient * num;
         return 1;
      }
   }
   return op_error(L, "*", 1, 2);
}

int lkiwi_term_m_div(lua_State* L) {
   const KiwiTerm* term = try_term(L, 1);
   int isnum;
   double num = lua_tonumberx(L, 2, &isnum);
   if (!term || !isnum) {
      return op_error(L, "/", 1, 2);
   }
   auto* ret = term_new(L);
   ret->var = retain_unmanaged(term->var);
   ret->coefficient = term->coefficient / num;
   return 1;
}

int lkiwi_term_m_unm(lua_State* L) {
   const auto* term = get_term(L, 1);
   auto* ret = term_new(L);
   ret->var = retain_unmanaged(term->var);
   ret->coefficient = -term->coefficient;
   return 1;
}

int lkiwi_term_toexpr(lua_State* L) {
   return push_expr_one(L, 0.0, get_term(L, 1));
}

int lkiwi_term_value(lua_State* L) {
   const auto* term = get_term(L, 1);
   lua_pushnumber(L, term->var->value() * term->coefficient);
   return 1;
}

int lkiwi_term_m_tostring(lua_State* L) {
   const auto* term = get_term(L, 1);
   lua_pushfstring(L, "%f %s", term->coefficient, term->var->name().c_str());
   return 1;
}

int lkiwi_term_m_gc(lua_State* L) {
   release_unmanaged(get_term(L, 1)->var);
   return 0;
}

int lkiwi_term_m_index(lua_State* L) {
   const auto* term = get_term(L, 1);
   size_t len;
   const char* k = lua_tolstring(L, 2, &len);
   if (len == 3 && memcmp("var", k, len) == 0) {
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM == 501
      lua_pushlightuserdata(L, term->var);
      lua_rawget(L, lua_upvalueindex(2));
#else
      lua_rawgetp(L, lua_upvalueindex(2), term->var);
#endif
      if (lua_isnil(L, -1)) {
         auto* varp = var_new(L);
         var_register(L, *varp = retain_unmanaged(term->var));
      }
      return 1;
   } else if (len == 11 && memcmp("coefficient", k, len) == 0) {
      lua_pushnumber(L, term->coefficient);
      return 1;
   }
   lua_getmetatable(L, 1);
   lua_pushvalue(L, 2);
   lua_rawget(L, -2);
   if (lua_isnil(L, -1)) {
      luaL_error(L, "kiwi.Term has no member named '%s'", k);
   }
   return 1;
}

constexpr const struct luaL_Reg kiwi_term_m[] = {
    {"__add", lkiwi_term_m_add},
    {"__sub", lkiwi_term_m_sub},
    {"__mul", lkiwi_term_m_mul},
    {"__div", lkiwi_term_m_div},
    {"__unm", lkiwi_term_m_unm},
    {"__tostring", lkiwi_term_m_tostring},
    {"__gc", lkiwi_term_m_gc},
    {"__index", 0},
    {"toexpr", lkiwi_term_toexpr},
    {"value", lkiwi_term_value},
    {"eq", lkiwi_eq},
    {"le", lkiwi_le},
    {"ge", lkiwi_ge},
    {0, 0}
};

int lkiwi_term_new(lua_State* L) {
   auto* var = get_var(L, 1);
   double coefficient = luaL_optnumber(L, 2, 1.0);
   auto* term = term_new(L);
   term->var = retain_unmanaged(var);
   term->coefficient = coefficient;
   return 1;
}

int push_expr_constant(lua_State* L, const KiwiExpression* expr, double constant) {
   auto* ne = expr_new(L, expr->term_count);
   for (int i = 0; i < expr->term_count; i++) {
      ne->terms[i].var = retain_unmanaged(expr->terms[i].var);
      ne->terms[i].coefficient = expr->terms[i].coefficient;
   }
   ne->constant = constant;
   ne->term_count = expr->term_count;
   return 1;
}

int push_mul_expr_coeff(lua_State* L, const KiwiExpression* expr, double coeff) {
   auto* ne = expr_new(L, expr->term_count);
   ne->constant = expr->constant * coeff;
   ne->term_count = expr->term_count;
   for (int i = 0; i < expr->term_count; i++) {
      ne->terms[i].var = retain_unmanaged(expr->terms[i].var);
      ne->terms[i].coefficient = expr->terms[i].coefficient * coeff;
   }
   return 1;
}

int push_add_expr_expr(lua_State* L, const KiwiExpression* a, const KiwiExpression* b) {
   int na = a->term_count, nb = b->term_count;

   auto* ne = expr_new(L, na + nb);
   ne->constant = a->constant + b->constant;
   ne->term_count = na + nb;

   for (int i = 0; i < na; i++) {
      ne->terms[i].var = retain_unmanaged(a->terms[i].var);
      ne->terms[i].coefficient = a->terms[i].coefficient;
   }
   for (int i = 0; i < nb; i++) {
      ne->terms[i + na].var = retain_unmanaged(b->terms[i].var);
      ne->terms[i + na].coefficient = b->terms[i].coefficient;
   }
   return 1;
}

int lkiwi_expr_m_add(lua_State* L) {
   TypeId type_id_b;
   double num = 0.0;
   void* arg_b = try_arg(L, 2, &type_id_b, &num);

   if (type_id_b == EXPR) {
      int isnum_a;
      num = lua_tonumberx(L, 1, &isnum_a);
      if (isnum_a) {
         auto* expr_b = static_cast<const KiwiExpression*>(arg_b);
         return push_expr_constant(L, expr_b, num + expr_b->constant);
      }
   }

   const auto* expr_a = try_expr(L, 1);
   if (expr_a) {
      switch (type_id_b) {
         case EXPR:
            return push_add_expr_expr(L, expr_a, static_cast<KiwiExpression*>(arg_b));
         case TERM:
            return push_add_expr_term(L, expr_a, static_cast<KiwiTerm*>(arg_b));
         case VAR: {
            const KiwiTerm term_b {*static_cast<VariableData**>(arg_b), 1.0};
            return push_add_expr_term(L, expr_a, &term_b);
         }
         case NUMBER:
            return push_expr_constant(L, expr_a, num + expr_a->constant);
         default:
            break;
      }
   }
   return op_error(L, "+", 1, 2);
}

int lkiwi_expr_m_sub(lua_State* L) {
   lua_settop(L, 2);
   compat_arith_unm(L);
   lkiwi_expr_m_add(L);
   return 1;
}

int lkiwi_expr_m_mul(lua_State* L) {
   int isnum, expridx = 2;
   double num = lua_tonumberx(L, 1, &isnum);

   if (!isnum) {
      expridx = 1;
      num = lua_tonumberx(L, 2, &isnum);
   }

   if (isnum) {
      const auto* expr = try_expr(L, expridx);
      if (expr)
         return push_mul_expr_coeff(L, expr, num);
   }
   return op_error(L, "*", 1, 2);
}

int lkiwi_expr_m_div(lua_State* L) {
   const auto* expr = try_expr(L, 1);
   int isnum;
   double num = lua_tonumberx(L, 2, &isnum);
   if (!expr || !isnum) {
      return op_error(L, "/", 1, 2);
   }
   return push_mul_expr_coeff(L, expr, 1.0 / num);
}

int lkiwi_expr_m_unm(lua_State* L) {
   const auto* expr = get_expr(L, 1);
   return push_mul_expr_coeff(L, expr, -1.0);
}

int lkiwi_expr_value(lua_State* L) {
   const auto* expr = get_expr(L, 1);
   double sum = expr->constant;
   for (int i = 0; i < expr->term_count; i++) {
      const auto* t = &expr->terms[i];
      sum += t->var->value() * t->coefficient;
   }
   lua_pushnumber(L, sum);
   return 1;
}

int lkiwi_expr_terms(lua_State* L) {
   const auto* expr = get_expr(L, 1);
   lua_createtable(L, expr->term_count, 0);
   for (int i = 0; i < expr->term_count; i++) {
      const auto* t = &expr->terms[i];
      auto* new_term = term_new(L);
      new_term->var = retain_unmanaged(t->var);
      new_term->coefficient = t->coefficient;
      lua_rawseti(L, -2, i + 1);
   }
   return 1;
}

int lkiwi_expr_copy(lua_State* L) {
   auto* expr = get_expr(L, 1);
   return push_expr_constant(L, expr, expr->constant);
}

int lkiwi_expr_m_tostring(lua_State* L) {
   const auto* expr = get_expr(L, 1);
   luaL_Buffer buf;
   luaL_buffinit(L, &buf);

   for (int i = 0; i < expr->term_count; i++) {
      const auto* t = &expr->terms[i];
      lua_pushfstring(L, "%f %s", t->coefficient, t->var->name().c_str());
      luaL_addvalue(&buf);
      luaL_addstring(&buf, " + ");
   }

   lua_pushfstring(L, "%f", expr->constant);
   luaL_addvalue(&buf);
   luaL_pushresult(&buf);

   return 1;
}

int lkiwi_expr_m_gc(lua_State* L) {
   const auto* expr = get_expr(L, 1);
   if (expr->owner) {
      release_unmanaged(expr->owner);
   } else {
      for (auto* t = expr->terms; t != expr->terms + expr->term_count; ++t) {
         release_unmanaged(t->var);
      }
   }
   return 0;
}

int lkiwi_expr_m_index(lua_State* L) {
   const auto* expr = get_expr(L, 1);
   size_t len;
   const char* k = lua_tolstring(L, 2, &len);
   if (len == 8 && memcmp("constant", k, len) == 0) {
      lua_pushnumber(L, expr->constant);
      return 1;
   }
   lua_getmetatable(L, 1);
   lua_pushvalue(L, 2);
   lua_rawget(L, -2);
   if (lua_isnil(L, -1)) {
      luaL_error(L, "kiwi.Expression has no member named '%s'", k);
   }
   return 1;
}

constexpr const struct luaL_Reg kiwi_expr_m[] = {
    {"__add", lkiwi_expr_m_add},
    {"__sub", lkiwi_expr_m_sub},
    {"__mul", lkiwi_expr_m_mul},
    {"__div", lkiwi_expr_m_div},
    {"__unm", lkiwi_expr_m_unm},
    {"__tostring", lkiwi_expr_m_tostring},
    {"__gc", lkiwi_expr_m_gc},
    {"__index", lkiwi_expr_m_index},
    {"value", lkiwi_expr_value},
    {"terms", lkiwi_expr_terms},
    {"copy", lkiwi_expr_copy},
    {"eq", lkiwi_eq},
    {"le", lkiwi_le},
    {"ge", lkiwi_ge},
    {0, 0}
};

int lkiwi_expr_new(lua_State* L) {
   int nterms = lua_gettop(L) - 1;
   lua_Number constant = luaL_checknumber(L, 1);

   auto* expr = expr_new(L, nterms);
   expr->constant = constant;
   expr->term_count = nterms;

   for (int i = 0; i < nterms; i++) {
      const auto* term = get_term(L, i + 2);
      expr->terms[i].var = retain_unmanaged(term->var);
      expr->terms[i].coefficient = term->coefficient;
   }
   return 1;
}

int lkiwi_constraint_strength(lua_State* L) {
   lua_pushnumber(L, get_constraint(L, 1)->strength());
   return 1;
}

int lkiwi_constraint_op(lua_State* L) {
   auto op = get_constraint(L, 1)->op();
   const char* opstr = "??";
   switch (op) {
      case kiwi::OP_LE:
         opstr = "LE";
         break;
      case kiwi::OP_GE:
         opstr = "GE";
         break;
      case kiwi::OP_EQ:
         opstr = "EQ";
         break;
   }
   lua_pushlstring(L, opstr, 2);
   return 1;
}

int lkiwi_constraint_violated(lua_State* L) {
   lua_pushboolean(L, get_constraint(L, 1)->violated());
   return 1;
}

int lkiwi_constraint_expression(lua_State* L) {
   auto* c = get_constraint(L, 1);
   const auto& expr = c->expression();
   const auto& terms = expr.terms();
   const auto term_count = static_cast<int>(terms.size() > INT_MAX ? INT_MAX : terms.size());

   auto* ne = expr_new(L, term_count);
   ne->owner = retain_unmanaged(c);
   ne->constant = expr.constant();
   ne->term_count = term_count;

   for (int i = 0; i < term_count; ++i) {
      const auto& t = terms[static_cast<std::size_t>(i)];
      ne->terms[i].var = const_cast<Variable&>(t.variable()).ptr();
      ne->terms[i].coefficient = t.coefficient();
   }
   return 1;
}

int lkiwi_constraint_m_tostring(lua_State* L) {
   const auto& c = *get_constraint(L, 1);

   luaL_Buffer buf;
   luaL_buffinit(L, &buf);
   const char* oppart = " ?? 0 | ";
   switch (c.op()) {
      case kiwi::OP_LE:
         oppart = " <= 0 | ";
         break;
      case kiwi::OP_GE:
         oppart = " >= 0 | ";
         break;
      case kiwi::OP_EQ:
         oppart = " == 0 | ";
         break;
   }

   const auto& expr = c.expression();

   for (const auto& t : expr.terms()) {
      lua_pushfstring(L, "%f %s", t.coefficient(), t.variable().name().c_str());
      luaL_addvalue(&buf);
      luaL_addstring(&buf, " + ");
   }

   lua_pushfstring(L, "%f", expr.constant());
   luaL_addvalue(&buf);

   luaL_addlstring(&buf, oppart, 8);
   const char* strength_name = 0;
   const double strength = c.strength();

   if (strength == STRENGTH_REQUIRED) {
      strength_name = "required";
   } else if (strength == STRENGTH_STRONG) {
      strength_name = "strong";
   } else if (strength == STRENGTH_MEDIUM) {
      strength_name = "medium";
   } else if (strength == STRENGTH_WEAK) {
      strength_name = "weak";
   }

   if (strength_name) {
      luaL_addstring(&buf, strength_name);
   } else {
      lua_pushfstring(L, "%f", strength);
      luaL_addvalue(&buf);
   }
   luaL_pushresult(&buf);

   return 1;
}

int lkiwi_constraint_m_gc(lua_State* L) {
   release_unmanaged(get_constraint(L, 1));
   return 0;
}

int lkiwi_solver_add_constraint(lua_State* L);
int lkiwi_solver_remove_constraint(lua_State* L);

int lkiwi_constraint_add_to(lua_State* L) {
   lua_settop(L, 2);
   lua_rotate(L, 1, 1);
   lkiwi_solver_add_constraint(L);
   lua_settop(L, 2);
   return 1;
}

int lkiwi_constraint_remove_from(lua_State* L) {
   lua_settop(L, 2);
   lua_rotate(L, 1, 1);
   lkiwi_solver_remove_constraint(L);
   lua_settop(L, 2);
   return 1;
}

constexpr const struct luaL_Reg kiwi_constraint_m[] = {
    {"__tostring", lkiwi_constraint_m_tostring},
    {"__gc", lkiwi_constraint_m_gc},
    {"strength", lkiwi_constraint_strength},
    {"op", lkiwi_constraint_op},
    {"violated", lkiwi_constraint_violated},
    {"expression", lkiwi_constraint_expression},
    {"add_to", lkiwi_constraint_add_to},
    {"remove_from", lkiwi_constraint_remove_from},
    {0, 0}
};

int lkiwi_constraint_new(lua_State* L) {
   const auto* lhs = get_expr_opt(L, 1);
   const auto* rhs = get_expr_opt(L, 2);
   const auto op = get_op_opt(L, 3);
   double strength = luaL_optnumber(L, 4, STRENGTH_REQUIRED);

   constraint_new(L, lhs, rhs, op, strength);
   return 1;
}

int push_pair_constraint(
    lua_State* L,
    VariableData* left,
    double coeff,
    VariableData* right,
    double constant,
    kiwi::RelationalOperator op,
    double strength
) {
   alignas(KiwiExpression) unsigned char expr_buf[KiwiExpression::sz(2)];
   auto* expr = reinterpret_cast<KiwiExpression*>(&expr_buf);
   expr->constant = constant;
   expr->term_count = 2;
   expr->terms[0].var = left;
   expr->terms[0].coefficient = 1.0;
   expr->terms[1].var = right;
   expr->terms[1].coefficient = -coeff;
   constraint_new(L, expr, 0, op, strength);
   return 1;
}

int lkiwi_constraints_pair_ratio(lua_State* L) {
   return push_pair_constraint(
       L,
       get_var(L, 1),
       luaL_checknumber(L, 2),
       get_var(L, 3),
       luaL_optnumber(L, 4, 0.0),
       get_op_opt(L, 5),
       luaL_optnumber(L, 6, STRENGTH_REQUIRED)
   );
}

int lkiwi_constraints_pair(lua_State* L) {
   return push_pair_constraint(
       L,
       get_var(L, 1),
       1.0,
       get_var(L, 2),
       luaL_optnumber(L, 3, 0.0),
       get_op_opt(L, 4),
       luaL_optnumber(L, 4, STRENGTH_REQUIRED)
   );
}

int lkiwi_constraints_single(lua_State* L) {
   alignas(KiwiExpression) unsigned char expr_buf[KiwiExpression::sz(1)];
   auto* expr = reinterpret_cast<KiwiExpression*>(&expr_buf);
   expr->term_count = 1;
   expr->terms[0].var = get_var(L, 1);
   expr->terms[0].coefficient = 1.0;
   expr->constant = luaL_optnumber(L, 2, 0.0);

   constraint_new(L, expr, 0, get_op_opt(L, 3), luaL_optnumber(L, 4, STRENGTH_REQUIRED));
   return 1;
}

constexpr const struct luaL_Reg lkiwi_constraints[] = {
    {"pair_ratio", lkiwi_constraints_pair_ratio},
    {"pair", lkiwi_constraints_pair},
    {"single", lkiwi_constraints_single},
    {0, 0}
};

void lkiwi_mod_constraints_new(lua_State* L, int ctx_i) {
   luaL_newlibtable(L, lkiwi_constraints);
   lua_pushvalue(L, ctx_i);
   setfuncs(L, lkiwi_constraints, 1);
}

/* kiwi.Error */

void error_new(lua_State* L, const KiwiErr* err, int solver_absi, int item_absi) {
   lua_createtable(L, 0, 4);
   push_type(L, ERROR);
   lua_setmetatable(L, -2);

   lua_pushstring(L, lkiwi_error_kinds[err->kind < KiwiErrUnknown ? err->kind : KiwiErrUnknown]);
   lua_setfield(L, -2, "kind");

   lua_pushstring(L, err->message);
   lua_setfield(L, -2, "message");

   if (solver_absi) {
      lua_pushvalue(L, solver_absi);
      lua_setfield(L, -2, "solver");
   }
   if (item_absi) {
      lua_pushvalue(L, item_absi);
      lua_setfield(L, -2, "item");
   }

   if (err->must_delete) {
      delete const_cast<KiwiErr*>(err);
   }
}

int lkiwi_error_m_tostring(lua_State* L) {
   luaL_Buffer buf;
   luaL_buffinit(L, &buf);

   lua_getfield(L, 1, "message");
   luaL_addvalue(&buf);

   lua_getfield(L, 1, "solver");
   lua_pushfstring(L, ": (kiwi.Solver(%p), ", get_solver(L, -1));
   lua_remove(L, -2);  // remove solver
   luaL_addvalue(&buf);

   lua_getfield(L, 1, "item");
   luaL_tolstring(L, -1, 0);
   lua_remove(L, -2);  // remove item
   luaL_addvalue(&buf);
   luaL_addstring(&buf, ")");
   luaL_pushresult(&buf);

   return 1;
}

constexpr const struct luaL_Reg lkiwi_error_m[] = {
    {"__tostring", lkiwi_error_m_tostring},
    {0, 0}
};

int lkiwi_error_mask(lua_State* L) {
   int invert = lua_toboolean(L, 2);

   if (lua_type(L, 1) == LUA_TSTRING) {
      luaL_typeerror(L, 1, "indexable");
   }

   lua_rawgeti(L, lua_upvalueindex(1), ERR_KIND_TAB);

   unsigned mask = 0;
   for (int n = 1; lua_geti(L, 1, n) != LUA_TNIL; ++n) {
      int isnum;
      auto shift = lua_tointegerx(L, -1, &isnum);
      if (!isnum) {
         lua_rawget(L, -2 /* err_kind table */);
         shift = lua_tointegerx(L, -1, &isnum);
         if (!isnum) {
            luaL_error(L, "unknown error kind at index %d: %s", n, luaL_tolstring(L, -2, 0));
         }
      }
      mask |= 1 << shift;
      lua_pop(L, 1);
   }
   lua_pushinteger(L, invert ? ~mask : mask);
   return 1;
}

int lkiwi_solver_handle_err(lua_State* L, const KiwiErr* err, const KiwiSolver* solver) {
   /* This assumes solver is at index 1 */
   lua_settop(L, 2);
   if (err) {
      error_new(L, err, 1, 2);
      unsigned error_mask = solver->error_mask;
      if (error_mask & (1 << err->kind)) {
         return 2;
      } else {
         lua_error(L);
      }
   }
   return 1;
}

int lkiwi_solver_add_constraint(lua_State* L) {
   auto* self = get_solver(L, 1);
   auto* c = get_constraint(L, 2);
   auto* err = kiwi_solver_add_constraint(self->solver, c);
   return lkiwi_solver_handle_err(L, err, self);
}

int lkiwi_solver_remove_constraint(lua_State* L) {
   auto* self = get_solver(L, 1);
   auto* c = get_constraint(L, 2);
   auto* err = kiwi_solver_remove_constraint(self->solver, c);
   return lkiwi_solver_handle_err(L, err, self);
}

int lkiwi_solver_add_edit_var(lua_State* L) {
   auto* self = get_solver(L, 1);
   auto* var = get_var(L, 2);
   double strength = luaL_checknumber(L, 3);
   auto* err = kiwi_solver_add_edit_var(self->solver, var, strength);
   return lkiwi_solver_handle_err(L, err, self);
}

int lkiwi_solver_remove_edit_var(lua_State* L) {
   auto* self = get_solver(L, 1);
   auto* var = get_var(L, 2);
   auto* err = kiwi_solver_remove_edit_var(self->solver, var);
   return lkiwi_solver_handle_err(L, err, self);
}

int lkiwi_solver_suggest_value(lua_State* L) {
   auto* self = get_solver(L, 1);
   auto* var = get_var(L, 2);
   double value = luaL_checknumber(L, 3);
   auto* err = kiwi_solver_suggest_value(self->solver, var, value);
   return lkiwi_solver_handle_err(L, err, self);
}

int lkiwi_solver_update_vars(lua_State* L) {
   get_solver(L, 1)->solver.updateVariables();
   return 0;
}

int lkiwi_solver_reset(lua_State* L) {
   get_solver(L, 1)->solver.reset();
   return 0;
}

int lkiwi_solver_has_constraint(lua_State* L) {
   auto* s = get_solver(L, 1);
   auto* c = get_constraint(L, 2);
   lua_pushboolean(L, s->solver.hasConstraint(Constraint(c)));
   return 1;
}

int lkiwi_solver_has_edit_var(lua_State* L) {
   auto* s = get_solver(L, 1);
   auto* var = get_var(L, 2);
   lua_pushboolean(L, s->solver.hasEditVariable(Variable(var)));
   return 1;
}

int lkiwi_solver_dump(lua_State* L) {
   get_solver(L, 1)->solver.dump();
   return 0;
}

int lkiwi_solver_dumps(lua_State* L) {
   const auto& s = get_solver(L, 1)->solver.dumps();
   lua_pushlstring(L, s.data(), s.length());
   return 1;
}

template<typename F>
int lkiwi_add_remove_tab(lua_State* L, F&& fn) {
   auto* solver = get_solver(L, 1);
   int narg = lua_gettop(L);

   // block this particularly obnoxious case which is always a bug
   if (lua_type(L, 2) == LUA_TSTRING) {
      luaL_typeerror(L, 2, "indexable");
   }
   for (int i = 1; lua_geti(L, 2, i) != LUA_TNIL; ++i) {
      const KiwiErr* err = fn(L, solver);
      if (err) {
         error_new(L, err, 1, narg + 1 /* item_absi */);
         const auto error_mask = solver->error_mask;
         if (error_mask & (1 << err->kind)) {
            lua_replace(L, 3);
            lua_settop(L, 3);
            return 2;
         } else {
            lua_error(L);
         }
      }
      lua_pop(L, 1);
   }
   lua_settop(L, 2);
   return 1;
}

int lkiwi_solver_add_constraints(lua_State* L) {
   return lkiwi_add_remove_tab(L, [](lua_State* L, KiwiSolver* s) {
      return kiwi_solver_add_constraint(s->solver, get_constraint(L, -1));
   });
}

int lkiwi_solver_remove_constraints(lua_State* L) {
   return lkiwi_add_remove_tab(L, [](lua_State* L, KiwiSolver* s) {
      return kiwi_solver_add_constraint(s->solver, get_constraint(L, -1));
   });
}

int lkiwi_solver_add_edit_vars(lua_State* L) {
   double strength = luaL_checknumber(L, 3);
   return lkiwi_add_remove_tab(L, [strength](lua_State* L, KiwiSolver* s) {
      return kiwi_solver_add_edit_var(s->solver, get_var(L, -1), strength);
   });
}

int lkiwi_solver_remove_edit_vars(lua_State* L) {
   return lkiwi_add_remove_tab(L, [](lua_State* L, KiwiSolver* s) {
      return kiwi_solver_remove_edit_var(s->solver, get_var(L, -1));
   });
}

int lkiwi_solver_suggest_values(lua_State* L) {
   auto* self = get_solver(L, 1);
   int narg = lua_gettop(L);

   // catch this obnoxious case which is always a bug
   if (lua_type(L, 2) == LUA_TSTRING) {
      luaL_typeerror(L, 2, "indexable");
   }
   if (lua_type(L, 3) == LUA_TSTRING) {
      luaL_typeerror(L, 3, "indexable");
   }

   for (int i = 1; lua_geti(L, 2, i) != LUA_TNIL; ++i) {
      auto* var = get_var(L, -1);

      lua_geti(L, 3, i);
      double value = luaL_checknumber(L, -1);

      const KiwiErr* err = kiwi_solver_suggest_value(self->solver, var, value);
      if (err) {
         error_new(L, err, 1, narg + 1 /* item_absi */);
         unsigned error_mask = self->error_mask;
         if (error_mask & (1 << err->kind)) {
            lua_replace(L, 4);
            lua_settop(L, 4);
            return 3;
         } else {
            lua_error(L);
         }
      }
      lua_pop(L, 2);
   }
   lua_settop(L, 3);
   return 2;
}

int lkiwi_solver_set_error_mask(lua_State* L) {
   auto* solver = get_solver(L, 1);

   lua_Integer error_mask;
   if (lua_istable(L, 2)) {
      lua_settop(L, 3);
      lua_rotate(L, 1, -1);
      lkiwi_error_mask(L);
      error_mask = lua_tointeger(L, -1);
   } else {
      error_mask = luaL_checkinteger(L, 2);
   }

   solver->error_mask = static_cast<unsigned>(error_mask);
   return 0;
}

int lkiwi_solver_m_tostring(lua_State* L) {
   lua_pushfstring(L, "kiwi.Solver(%p)", get_solver(L, 1));
   return 1;
}

int lkiwi_solver_m_gc(lua_State* L) {
   get_solver(L, 1)->~KiwiSolver();
   return 0;
}

constexpr const struct luaL_Reg kiwi_solver_m[] = {
    {"add_constraint", lkiwi_solver_add_constraint},
    {"add_constraints", lkiwi_solver_add_constraints},
    {"remove_constraint", lkiwi_solver_remove_constraint},
    {"remove_constraints", lkiwi_solver_remove_constraints},
    {"add_edit_var", lkiwi_solver_add_edit_var},
    {"add_edit_vars", lkiwi_solver_add_edit_vars},
    {"remove_edit_var", lkiwi_solver_remove_edit_var},
    {"remove_edit_vars", lkiwi_solver_remove_edit_vars},
    {"suggest_value", lkiwi_solver_suggest_value},
    {"suggest_values", lkiwi_solver_suggest_values},
    {"update_vars", lkiwi_solver_update_vars},
    {"reset", lkiwi_solver_reset},
    {"has_constraint", lkiwi_solver_has_constraint},
    {"has_edit_var", lkiwi_solver_has_edit_var},
    {"dump", lkiwi_solver_dump},
    {"dumps", lkiwi_solver_dumps},
    {"set_error_mask", lkiwi_solver_set_error_mask},
    {"__tostring", lkiwi_solver_m_tostring},
    {"__gc", lkiwi_solver_m_gc},
    {0, 0}
};

int lkiwi_solver_new(lua_State* L) {
   lua_Integer error_mask;
   if (lua_istable(L, 1)) {
      lkiwi_error_mask(L);
      error_mask = lua_tointeger(L, -1);
   } else {
      error_mask = luaL_optinteger(L, 1, 0);
   }

   new (lua_newuserdata(L, sizeof(KiwiSolver))) KiwiSolver {static_cast<unsigned>(error_mask)};
   push_type(L, SOLVER);
   lua_setmetatable(L, -2);
   return 1;
}

inline double clamp(double n) {
   return fmax(0.0, fmin(1000, n));
}

int lkiwi_strength_create(lua_State* L) {
   const double a = luaL_checknumber(L, 1);
   const double b = luaL_checknumber(L, 2);
   const double c = luaL_checknumber(L, 3);
   const double w = luaL_optnumber(L, 4, 1.0);

   const double result = clamp(a * w) * 1000000.0 + clamp(b * w) * 1000.0 + clamp(c * w);
   lua_pushnumber(L, result);
   return 1;
}

constexpr const struct luaL_Reg lkiwi_strength[] = {{"create", lkiwi_strength_create}, {0, 0}};

void lkiwi_mod_strength_new(lua_State* L) {
   newlib(L, lkiwi_strength);

   lua_pushnumber(L, STRENGTH_REQUIRED);
   lua_setfield(L, -2, "REQUIRED");

   lua_pushnumber(L, STRENGTH_STRONG);
   lua_setfield(L, -2, "STRONG");

   lua_pushnumber(L, STRENGTH_MEDIUM);
   lua_setfield(L, -2, "MEDIUM");

   lua_pushnumber(L, STRENGTH_WEAK);
   lua_setfield(L, -2, "WEAK");
}

int lkiwi_is_var(lua_State* L) {
   return is_udata_obj(L, VAR);
}

int lkiwi_is_term(lua_State* L) {
   return is_udata_obj(L, TERM);
}

int lkiwi_is_expression(lua_State* L) {
   return is_udata_obj(L, EXPR);
}

int lkiwi_is_constraint(lua_State* L) {
   return is_udata_obj(L, CONSTRAINT);
}

int lkiwi_is_solver(lua_State* L) {
   return is_udata_obj(L, SOLVER);
}

int lkiwi_is_error(lua_State* L) {
   int result = 0;
   if (lua_getmetatable(L, 1)) {
      push_type(L, ERROR);
      result = lua_rawequal(L, -1, -2);
      lua_pop(L, 2);
   }
   lua_pushboolean(L, result);
   return 1;
}

constexpr const struct luaL_Reg lkiwi[] = {
    {"Var", 0},
    {"is_var", lkiwi_is_var},
    {"Term", lkiwi_term_new},
    {"is_term", lkiwi_is_term},
    {"Expression", lkiwi_expr_new},
    {"is_expression", lkiwi_is_expression},
    {"Constraint", lkiwi_constraint_new},
    {"is_constraint", lkiwi_is_constraint},
    {"Solver", lkiwi_solver_new},
    {"is_solver", lkiwi_is_solver},
    {"error_mask", lkiwi_error_mask},
    {"is_error", lkiwi_is_error},
    {"eq", lkiwi_eq},
    {"le", lkiwi_le},
    {"ge", lkiwi_ge},
    {0, 0}
};

int no_member_mt_index(lua_State* L) {
   luaL_error(L, "attempt to access non-existent member '%s'", lua_tostring(L, 2));
   return 0;
}

void no_member_mt_new(lua_State* L) {
   lua_createtable(L, 0, 1);
   lua_pushcfunction(L, no_member_mt_index);
   lua_setfield(L, -2, "__index");
}

void register_type_n(
    lua_State* L,
    const char* name,
    int context_absi,
    int type_id,
    const luaL_Reg* m,
    size_t mcnt
) {
   lua_createtable(L, 0, static_cast<int>(mcnt + 2));
   lua_pushvalue(L, -2);  // no_member_mt
   lua_setmetatable(L, -2);
   lua_pushstring(L, name);
   lua_setfield(L, -2, "__name");
   lua_pushvalue(L, -1);
   lua_setfield(L, -2, "__index");

   /* set type_tab udata as upvalue */
   lua_pushvalue(L, context_absi);
   setfuncs(L, m, 1);

   lua_rawseti(L, context_absi, type_id);
}

template<std::size_t N>
constexpr inline void register_type(
    lua_State* L,
    const char* name,
    int context_absi,
    int type_id,
    const luaL_Reg (&m)[N]
) {
   register_type_n(L, name, context_absi, type_id, m, N);
}

#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM == 501
void compat_init(lua_State* L, int context_absi) {
   static const char var_sub_code[] =
       "local a,b=...\n"
       "return a + -b";

   if (luaL_loadbuffer(L, var_sub_code, sizeof(var_sub_code) - 1, "=kiwi internal"))
      lua_error(L);

   lua_rawseti(L, context_absi, VAR_SUB_FN);
}
#else
void compat_init(lua_State*, int) {}
#endif /* Lua 5.1 */

}  // namespace

#if defined __GNUC__ && (!defined _WIN32 || defined __CYGWIN__)
   #define LJKIWI_EXPORT __attribute__((__visibility__("default")))
#elif defined _WIN32
   #define LJKIWI_EXPORT __declspec(dllexport)
#endif

extern "C" LJKIWI_EXPORT int luaopen_ljkiwi(lua_State* L) {
   luaL_checkversion(L);

   /* context table */
   lua_createtable(L, 0, CONTEXT_TAB_MAX);
   int ctx_i = lua_gettop(L);

   compat_init(L, ctx_i);
   lua_pushliteral(L, "kiwi library memory allocation error");
   lua_rawseti(L, ctx_i, MEM_ERR_MSG);

   no_member_mt_new(L);
   register_type(L, "kiwi.Var", ctx_i, VAR, kiwi_var_m);
   register_type(L, "kiwi.Term", ctx_i, TERM, kiwi_term_m);
   register_type(L, "kiwi.Expression", ctx_i, EXPR, kiwi_expr_m);
   register_type(L, "kiwi.Constraint", ctx_i, CONSTRAINT, kiwi_constraint_m);
   register_type(L, "kiwi.Solver", ctx_i, SOLVER, kiwi_solver_m);
   register_type(L, "kiwi.Error", ctx_i, ERROR, lkiwi_error_m);

   lua_createtable(L, 0, array_count(lkiwi) + 6);
   lua_pushvalue(L, ctx_i);
   setfuncs(L, lkiwi, 1);

   /* var weak table */
   /* set as upvalue for selected functions */
   lua_createtable(L, 0, 0);
   lua_createtable(L, 0, 1);
   lua_pushstring(L, "v");
   lua_setfield(L, -2, "__mode");
   lua_setmetatable(L, -2);

   lua_pushvalue(L, ctx_i);
   lua_pushvalue(L, -2);
   lua_pushcclosure(L, lkiwi_var_new, 2);
   lua_setfield(L, -3, "Var");

   lua_rawgeti(L, ctx_i, TERM);
   lua_pushvalue(L, ctx_i);
   lua_pushvalue(L, -3);
   lua_pushcclosure(L, lkiwi_term_m_index, 2);
   lua_setfield(L, -2, "__index");
   lua_pop(L, 2);  // TERM mt and var weak table

   /* ErrKind table */
   /* TODO: implement __call metamethod for these */
   lua_createtable(L, array_count(lkiwi_error_kinds) + 1, array_count(lkiwi_error_kinds));
   for (int i = 0; i < array_count(lkiwi_error_kinds); i++) {
      lua_pushstring(L, lkiwi_error_kinds[i]);
      lua_pushvalue(L, -1);
      lua_rawseti(L, -3, i);
      lua_pushinteger(L, i);
      lua_rawset(L, -3);
   }

   lua_pushvalue(L, -1);
   lua_rawseti(L, ctx_i, ERR_KIND_TAB);
   lua_setfield(L, -2, "ErrKind");

   lua_rawgeti(L, ctx_i, ERROR);
   lua_setfield(L, -2, "Error");

   lua_pushinteger(L, 0xFFFF);
   lua_setfield(L, -2, "ERROR_MASK_ALL");

   lua_pushinteger(
       L,
       ~((1 << KiwiErrInternalSolverError) | (1 << KiwiErrAlloc) | (1 << KiwiErrNullObject)
         | (1 << KiwiErrUnknown))
   );
   lua_setfield(L, -2, "ERROR_MASK_NON_FATAL");

   lkiwi_mod_strength_new(L);
   lua_setfield(L, -2, "strength");

   lkiwi_mod_constraints_new(L, ctx_i);
   lua_setfield(L, -2, "constraints");

   return 1;
}
