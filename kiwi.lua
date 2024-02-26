-- kiwi.lua - LuaJIT FFI bindings with C API fallback to kiwi constraint solver.

local ffi
do
   local ffi_loader = package.preload["ffi"]
   if ffi_loader == nil then
      return require("ljkiwi")
   end
   ffi = ffi_loader() --[[@as ffilib]]
end

local kiwi = {}

local ljkiwi
do
   local cpath, err = package.searchpath("ljkiwi", package.cpath)
   if cpath == nil then
      error("kiwi dynamic library 'ljkiwi' not found\n" .. err)
   end
   ljkiwi = ffi.load(cpath)
end
kiwi.ljkiwi = ljkiwi

ffi.cdef([[
void free(void *);

typedef struct KiwiVar KiwiVar;
typedef struct KiwiConstraint KiwiConstraint;
typedef struct KiwiSolver KiwiSolver;]])

ffi.cdef([[
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

enum KiwiRelOp { LE, GE, EQ };

typedef struct KiwiTerm {
   KiwiVar* var;
   double coefficient;
} KiwiTerm;

typedef struct KiwiExpression {
   double constant;
   int term_count;
   void* owner;

   KiwiTerm terms_[?];
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
]])

local strformat = string.format
local ffi_copy, ffi_gc, ffi_istype, ffi_new, ffi_string =
   ffi.copy, ffi.gc, ffi.istype, ffi.new, ffi.string

local concat = table.concat
local has_table_new, new_tab = pcall(require, "table.new")
if not has_table_new or type(new_tab) ~= "function" then
   new_tab = function(_, _)
      return {}
   end
end

---@alias kiwi.ErrKind
---| '"KiwiErrNone"' # No error.
---| '"KiwiErrUnsatisfiableConstraint"' # The given constraint is required and cannot be satisfied.
---| '"KiwiErrUnknownConstraint"' # The given constraint has not been added to the solver.
---| '"KiwiErrDuplicateConstraint"' # The given constraint has already been added to the solver.
---| '"KiwiErrUnknownEditVariable"' # The given edit variable has not been added to the solver.
---| '"KiwiErrDuplicateEditVariable"' # The given edit variable has already been added to the solver.
---| '"KiwiErrBadRequiredStrength"' # The given strength is >= required.
---| '"KiwiErrInternalSolverError"' # An internal solver error occurred.
---| '"KiwiErrAlloc"' # A memory allocation error occurred.
---| '"KiwiErrNullObject"' # A method was invoked on a null or empty object.
---| '"KiwiErrUnknown"' # An unknown error occurred.
kiwi.ErrKind = ffi.typeof("enum KiwiErrKind") --[[@as kiwi.ErrKind]]

---@alias kiwi.RelOp
---| '"LE"' # <= (less than or equal)
---| '"GE"' # >= (greater than or equal)
---| '"EQ"' # == (equal)
kiwi.RelOp = ffi.typeof("enum KiwiRelOp")

kiwi.strength = {
   REQUIRED = 1001001000.0,
   STRONG = 1000000.0,
   MEDIUM = 1000.0,
   WEAK = 1.0,
}

do
   local function clamp(n)
      return math.max(0, math.min(1000, n))
   end

   --- Create a custom constraint strength.
   ---@param a number: Scale factor 1e6
   ---@param b number: Scale factor 1e3
   ---@param c number: Scale factor 1
   ---@param w? number: Weight
   ---@return number
   ---@nodiscard
   function kiwi.strength.create(a, b, c, w)
      w = w or 1.0
      return clamp(a * w) * 1000000.0 + clamp(b * w) * 1000.0 + clamp(c * w)
   end
end

local Var = ffi.typeof("struct KiwiVar") --[[@as kiwi.Var]]
kiwi.Var = Var

function kiwi.is_var(o)
   return ffi_istype(Var, o)
end

local Term = ffi.typeof("struct KiwiTerm") --[[@as kiwi.Term]]
local SIZEOF_TERM = assert(ffi.sizeof(Term))
kiwi.Term = Term

function kiwi.is_term(o)
   return ffi_istype(Term, o)
end

local Expression = ffi.typeof("struct KiwiExpression") --[[@as kiwi.Expression]]
kiwi.Expression = Expression

function kiwi.is_expression(o)
   return ffi_istype(Expression, o)
end

local Constraint = ffi.typeof("struct KiwiConstraint") --[[@as kiwi.Constraint]]
kiwi.Constraint = Constraint

function kiwi.is_constraint(o)
   return ffi_istype(Constraint, o)
end

---@param expr kiwi.Expression
---@param var kiwi.Var
---@param coeff number?
---@nodiscard
local function add_expr_term(expr, var, coeff)
   local ret = ffi_gc(ffi_new(Expression, expr.term_count + 1), ljkiwi.kiwi_expression_destroy) --[[@as kiwi.Expression]]
   ffi_copy(ret.terms_, expr.terms_, SIZEOF_TERM * expr.term_count)
   local dt = ret.terms_[expr.term_count]
   dt.var = var
   dt.coefficient = coeff or 1.0
   ret.constant = expr.constant
   ret.term_count = expr.term_count + 1
   ljkiwi.kiwi_expression_retain(ret)
   return ret
end

---@param constant number
---@param var kiwi.Var
---@param coeff number?
---@nodiscard
local function new_expr_one(constant, var, coeff)
   local ret = ffi_gc(ffi_new(Expression, 1), ljkiwi.kiwi_expression_destroy) --[[@as kiwi.Expression]]
   local dt = ret.terms_[0]
   dt.var = var
   dt.coefficient = coeff or 1.0
   ret.constant = constant
   ret.term_count = 1
   ljkiwi.kiwi_expression_retain(ret)
   return ret
end

---@param constant number
---@param var1 kiwi.Var
---@param var2 kiwi.Var
---@param coeff1 number?
---@param coeff2 number?
---@nodiscard
local function new_expr_pair(constant, var1, var2, coeff1, coeff2)
   local ret = ffi_gc(ffi_new(Expression, 2), ljkiwi.kiwi_expression_destroy) --[[@as kiwi.Expression]]
   local dt = ret.terms_[0]
   dt.var = var1
   dt.coefficient = coeff1 or 1.0
   dt = ret.terms_[1]
   dt.var = var2
   dt.coefficient = coeff2 or 1.0
   ret.constant = constant
   ret.term_count = 2
   ljkiwi.kiwi_expression_retain(ret)
   return ret
end

local function typename(o)
   if ffi.istype(Var, o) then
      return "Var"
   elseif ffi.istype(Term, o) then
      return "Term"
   elseif ffi.istype(Expression, o) then
      return "Expression"
   elseif ffi.istype(Constraint, o) then
      return "Constraint"
   else
      return type(o)
   end
end

local function op_error(a, b, op)
   --stylua: ignore
   -- level 3 works for arithmetic without TCO (no return), and for rel with TCO forced (explicit return)
   error(strformat(
         "invalid operand type for '%s' %.40s('%.99s') and %.40s('%.99s')",
         op, typename(a), tostring(a), typename(b), tostring(b)), 3)
end

local Strength = kiwi.strength
local REQUIRED = Strength.REQUIRED

local OP_NAMES = {
   LE = "<=",
   GE = ">=",
   EQ = "==",
}

local tmpexpr = ffi_new(Expression, 2) --[[@as kiwi.Expression]]
local tmpexpr_r = ffi_new(Expression, 1) --[[@as kiwi.Expression]]

local function toexpr(o, temp)
   if ffi_istype(Expression, o) then
      return o --[[@as kiwi.Expression]]
   elseif type(o) == "number" then
      temp.constant = o
      temp.term_count = 0
      return temp
   end
   temp.constant = 0
   temp.term_count = 1
   local t = temp.terms_[0]

   if ffi_istype(Var, o) then
      t.var = o --[[@as kiwi.Var]]
      t.coefficient = 1.0
   elseif ffi_istype(Term, o) then
      ffi_copy(t, o, SIZEOF_TERM)
   else
      return nil
   end
   return temp
end

---@param lhs kiwi.Expression|kiwi.Term|kiwi.Var|number
---@param rhs kiwi.Expression|kiwi.Term|kiwi.Var|number
---@param op kiwi.RelOp
---@param strength? number
---@nodiscard
local function rel(lhs, rhs, op, strength)
   local el = toexpr(lhs, tmpexpr)
   local er = toexpr(rhs, tmpexpr_r)
   if el == nil or er == nil then
      op_error(lhs, rhs, OP_NAMES[op])
   end

   return ffi_gc(
      ljkiwi.kiwi_constraint_construct(el, er, op, strength or REQUIRED),
      ljkiwi.kiwi_constraint_release
   ) --[[@as kiwi.Constraint]]
end

--- Define a constraint with expressions as `a <= b`.
---@param lhs kiwi.Expression|kiwi.Term|kiwi.Var|number
---@param rhs kiwi.Expression|kiwi.Term|kiwi.Var|number
---@param strength? number
---@nodiscard
function kiwi.le(lhs, rhs, strength)
   return rel(lhs, rhs, "LE", strength)
end

--- Define a constraint with expressions as `a >= b`.
---@param lhs kiwi.Expression|kiwi.Term|kiwi.Var|number
---@param rhs kiwi.Expression|kiwi.Term|kiwi.Var|number
---@param strength? number
---@nodiscard
function kiwi.ge(lhs, rhs, strength)
   return rel(lhs, rhs, "GE", strength)
end

--- Define a constraint with expressions as `a == b`.
---@param lhs kiwi.Expression|kiwi.Term|kiwi.Var|number
---@param rhs kiwi.Expression|kiwi.Term|kiwi.Var|number
---@param strength? number
---@nodiscard
function kiwi.eq(lhs, rhs, strength)
   return rel(lhs, rhs, "EQ", strength)
end

do
   --- Variables are the values the constraint solver calculates.
   ---@class kiwi.Var: ffi.cdata*
   ---@overload fun(name: string?): kiwi.Var
   ---@operator mul(number): kiwi.Term
   ---@operator div(number): kiwi.Term
   ---@operator unm: kiwi.Term
   ---@operator add(kiwi.Expression|kiwi.Term|kiwi.Var|number): kiwi.Expression
   ---@operator sub(kiwi.Expression|kiwi.Term|kiwi.Var|number): kiwi.Expression
   local Var_cls = {
      le = kiwi.le,
      ge = kiwi.ge,
      eq = kiwi.eq,

      --- Change the name of the variable.
      ---@type fun(self: kiwi.Var, name: string)
      set_name = ljkiwi.kiwi_var_set_name,

      --- Get the current value of the variable.
      ---@type fun(self: kiwi.Var): number
      value = ljkiwi.kiwi_var_value,

      --- Set the value of the variable.
      ---@type fun(self: kiwi.Var, value: number)
      set = ljkiwi.kiwi_var_set_value,
   }

   --- Get the name of the variable.
   ---@return string
   ---@nodiscard
   function Var_cls:name()
      return ffi_string(ljkiwi.kiwi_var_name(self))
   end

   --- Create a term from this variable.
   ---@param coefficient number?
   ---@return kiwi.Term
   ---@nodiscard
   function Var_cls:toterm(coefficient)
      return Term(self, coefficient)
   end

   --- Create a term from this variable.
   ---@param coefficient number?
   ---@param constant number?
   ---@return kiwi.Expression
   ---@nodiscard
   function Var_cls:toexpr(coefficient, constant)
      return new_expr_one(constant or 0.0, self, coefficient)
   end

   local Var_mt = {
      __index = Var_cls,
   }

   function Var_mt:__new(name)
      return ffi_gc(ljkiwi.kiwi_var_construct(name), ljkiwi.kiwi_var_release)
   end

   function Var_mt.__mul(a, b)
      if type(a) == "number" then
         return Term(b, a)
      elseif type(b) == "number" then
         return Term(a, b)
      end
      op_error(a, b, "*")
   end

   function Var_mt.__div(a, b)
      if type(b) ~= "number" then
         op_error(a, b, "/")
      end
      return Term(a, 1.0 / b)
   end

   function Var_mt:__unm()
      return Term(self, -1.0)
   end

   function Var_mt.__add(a, b)
      if ffi_istype(Var, b) then
         if type(a) == "number" then
            return new_expr_one(a, b)
         else
            return new_expr_pair(0.0, a, b)
         end
      elseif ffi_istype(Term, b) then
         return new_expr_pair(0.0, a, b.var, 1.0, b.coefficient)
      elseif ffi_istype(Expression, b) then
         return add_expr_term(b, a)
      elseif type(b) == "number" then
         return new_expr_one(b, a)
      end
      op_error(a, b, "+")
   end

   function Var_mt.__sub(a, b)
      return a + -b
   end

   function Var_mt:__tostring()
      return self:name() .. "(" .. self:value() .. ")"
   end

   ffi.metatype(Var, Var_mt)
end

do
   --- Terms are the components of an expression.
   --- Each term is a variable multiplied by a constant coefficient (default 1.0).
   ---@class kiwi.Term: ffi.cdata*
   ---@overload fun(var: kiwi.Var, coefficient: number?): kiwi.Term
   ---@field var kiwi.Var
   ---@field coefficient number
   ---@operator mul(number): kiwi.Term
   ---@operator div(number): kiwi.Term
   ---@operator unm: kiwi.Term
   ---@operator add(kiwi.Expression|kiwi.Term|kiwi.Var|number): kiwi.Expression
   ---@operator sub(kiwi.Expression|kiwi.Term|kiwi.Var|number): kiwi.Expression
   local Term_cls = {
      le = kiwi.le,
      ge = kiwi.ge,
      eq = kiwi.eq,
   }

   ---@return number
   ---@nodiscard
   function Term_cls:value()
      return self.coefficient * self.var:value()
   end

   --- Create an expression from this term.
   ---@param constant number?
   ---@return kiwi.Expression
   function Term_cls:toexpr(constant)
      return new_expr_one(constant or 0.0, self.var, self.coefficient)
   end

   local Term_mt = { __index = Term_cls }

   local function term_gc(term)
      ljkiwi.kiwi_var_release(term.var)
   end

   function Term_mt.__new(T, var, coefficient)
      local t = ffi_gc(ffi_new(T), term_gc) --[[@as kiwi.Term]]
      ljkiwi.kiwi_var_retain(var)
      t.var = var
      t.coefficient = coefficient or 1.0
      return t
   end

   function Term_mt.__mul(a, b)
      if type(b) == "number" then
         return Term(a.var, a.coefficient * b)
      elseif type(a) == "number" then
         return Term(b.var, b.coefficient * a)
      end
      op_error(a, b, "*")
   end

   function Term_mt.__div(a, b)
      if type(b) ~= "number" then
         op_error(a, b, "/")
      end
      return Term(a.var, a.coefficient / b)
   end

   function Term_mt:__unm()
      return Term(self.var, -self.coefficient)
   end

   function Term_mt.__add(a, b)
      if ffi_istype(Var, b) then
         return new_expr_pair(0.0, a.var, b, a.coefficient)
      elseif ffi_istype(Term, b) then
         if type(a) == "number" then
            return new_expr_one(a, b.var, b.coefficient)
         else
            return new_expr_pair(0.0, a.var, b.var, a.coefficient, b.coefficient)
         end
      elseif ffi_istype(Expression, b) then
         return add_expr_term(b, a.var, a.coefficient)
      elseif type(b) == "number" then
         return new_expr_one(b, a.var, a.coefficient)
      end
      op_error(a, b, "+")
   end

   function Term_mt.__sub(a, b)
      return Term_mt.__add(a, -b)
   end

   function Term_mt:__tostring()
      return tostring(self.coefficient) .. " " .. self.var:name()
   end

   ffi.metatype(Term, Term_mt)
end

do
   --- Expressions are a sum of terms with an added constant.
   ---@class kiwi.Expression: ffi.cdata*
   ---@overload fun(constant: number, ...: kiwi.Term): kiwi.Expression
   ---@field constant number
   ---@field package owner ffi.cdata*
   ---@field package term_count number
   ---@field package terms_ ffi.cdata*
   ---@operator mul(number): kiwi.Expression
   ---@operator div(number): kiwi.Expression
   ---@operator unm: kiwi.Expression
   ---@operator add(kiwi.Expression|kiwi.Term|kiwi.Var|number): kiwi.Expression
   ---@operator sub(kiwi.Expression|kiwi.Term|kiwi.Var|number): kiwi.Expression
   local Expression_cls = {
      le = kiwi.le,
      ge = kiwi.ge,
      eq = kiwi.eq,
   }

   ---@param expr kiwi.Expression
   ---@param constant number
   ---@nodiscard
   local function mul_expr_coeff(expr, constant)
      local ret = ffi_gc(ffi_new(Expression, expr.term_count), ljkiwi.kiwi_expression_destroy) --[[@as kiwi.Expression]]
      for i = 0, expr.term_count - 1 do
         local st = expr.terms_[i] --[[@as kiwi.Term]]
         local dt = ret.terms_[i] --[[@as kiwi.Term]]
         dt.var = st.var
         dt.coefficient = st.coefficient * constant
      end
      ret.constant = expr.constant * constant
      ret.term_count = expr.term_count
      ljkiwi.kiwi_expression_retain(ret)
      return ret
   end

   ---@param a kiwi.Expression
   ---@param b kiwi.Expression
   ---@nodiscard
   local function add_expr_expr(a, b)
      local a_count = a.term_count
      local b_count = b.term_count
      local ret = ffi_gc(ffi_new(Expression, a_count + b_count), ljkiwi.kiwi_expression_destroy) --[[@as kiwi.Expression]]

      for i = 0, a_count - 1 do
         local dt = ret.terms_[i] --[[@as kiwi.Term]]
         local st = a.terms_[i] --[[@as kiwi.Term]]
         dt.var = st.var
         dt.coefficient = st.coefficient
      end
      for i = 0, b_count - 1 do
         local dt = ret.terms_[a_count + i] --[[@as kiwi.Term]]
         local st = b.terms_[i] --[[@as kiwi.Term]]
         dt.var = st.var
         dt.coefficient = st.coefficient
      end
      ret.constant = a.constant + b.constant
      ret.term_count = a_count + b_count
      ljkiwi.kiwi_expression_retain(ret)
      return ret
   end

   ---@param expr kiwi.Expression
   ---@param constant number
   ---@nodiscard
   local function new_expr_constant(expr, constant)
      local ret = ffi_gc(ffi_new(Expression, expr.term_count), ljkiwi.kiwi_expression_destroy) --[[@as kiwi.Expression]]
      ffi_copy(ret.terms_, expr.terms_, SIZEOF_TERM * expr.term_count)
      ret.constant = constant
      ret.term_count = expr.term_count
      ljkiwi.kiwi_expression_retain(ret)
      return ret
   end

   ---@return number
   ---@nodiscard
   function Expression_cls:value()
      local sum = self.constant
      for i = 0, self.term_count - 1 do
         local t = self.terms_[i]
         sum = sum + t.var:value() * t.coefficient
      end
      return sum
   end

   ---@return kiwi.Term[]
   ---@nodiscard
   function Expression_cls:terms()
      local terms = new_tab(self.term_count, 0)
      for i = 0, self.term_count - 1 do
         local t = self.terms_[i] --[[@as kiwi.Term]]
         terms[i + 1] = Term(t.var, t.coefficient)
      end
      return terms
   end

   ---@return kiwi.Expression
   ---@nodiscard
   function Expression_cls:copy()
      return new_expr_constant(self, self.constant)
   end

   local Expression_mt = {
      __index = Expression_cls,
   }

   function Expression_mt:__new(constant, ...)
      local term_count = select("#", ...)
      local e = ffi_gc(ffi_new(self, term_count), ljkiwi.kiwi_expression_destroy) --[[@as kiwi.Expression]]
      e.term_count = term_count
      e.constant = constant
      for i = 1, term_count do
         local t = select(i, ...)
         local dt = e.terms_[i - 1] --[[@as kiwi.Term]]
         dt.var = t.var
         dt.coefficient = t.coefficient
      end
      ljkiwi.kiwi_expression_retain(e)
      return e
   end

   function Expression_mt.__mul(a, b)
      if type(a) == "number" then
         return mul_expr_coeff(b, a)
      elseif type(b) == "number" then
         return mul_expr_coeff(a, b)
      end
      op_error(a, b, "*")
   end

   function Expression_mt.__div(a, b)
      if type(b) ~= "number" then
         op_error(a, b, "/")
      end
      return mul_expr_coeff(a, 1.0 / b)
   end

   function Expression_mt:__unm()
      return mul_expr_coeff(self, -1.0)
   end

   function Expression_mt.__add(a, b)
      if ffi_istype(Var, b) then
         return add_expr_term(a, b)
      elseif ffi_istype(Expression, b) then
         if type(a) == "number" then
            return new_expr_constant(b, a + b.constant)
         else
            return add_expr_expr(a, b)
         end
      elseif ffi_istype(Term, b) then
         return add_expr_term(a, b.var, b.coefficient)
      elseif type(b) == "number" then
         return new_expr_constant(a, a.constant + b)
      end
      op_error(a, b, "+")
   end

   function Expression_mt.__sub(a, b)
      return Expression_mt.__add(a, -b)
   end

   function Expression_mt:__tostring()
      local tab = new_tab(self.term_count + 1, 0)
      for i = 0, self.term_count - 1 do
         local t = self.terms_[i]
         tab[i + 1] = tostring(t.coefficient) .. " " .. t.var:name()
      end
      tab[self.term_count + 1] = self.constant
      return concat(tab, " + ")
   end

   ffi.metatype(Expression, Expression_mt)
end

do
   --- A constraint is a linear inequality or equality with associated strength.
   --- Constraints can be built with arbitrary left and right hand expressions. But
   --- ultimately they all have the form `expression [op] 0`.
   ---@class kiwi.Constraint: ffi.cdata*
   ---@overload fun(lhs: kiwi.Expression?, rhs: kiwi.Expression?, op: kiwi.RelOp?, strength: number?): kiwi.Constraint
   local Constraint_cls = {
      --- The strength of the constraint.
      ---@type fun(self: kiwi.Constraint): number
      strength = ljkiwi.kiwi_constraint_strength,

      --- The relational operator of the constraint.
      ---@type fun(self: kiwi.Constraint): kiwi.RelOp
      op = ljkiwi.kiwi_constraint_op,

      --- Whether the constraint is violated in the current solution.
      ---@type fun(self: kiwi.Constraint): boolean
      violated = ljkiwi.kiwi_constraint_violated,
   }

   --- The reduced expression defining the constraint.
   ---@return kiwi.Expression
   ---@nodiscard
   function Constraint_cls:expression()
      local SZ = 7
      local expr = ffi_new(Expression, SZ) --[[@as kiwi.Expression]]
      local n = ljkiwi.kiwi_constraint_expression(self, expr, SZ)
      if n > SZ then
         expr = ffi_new(Expression, n) --[[@as kiwi.Expression]]
         n = ljkiwi.kiwi_constraint_expression(self, expr, n)
      end
      return ffi_gc(expr, ljkiwi.kiwi_expression_destroy) --[[@as kiwi.Expression]]
   end

   --- Add the constraint to the solver.
   --- Raises:
   --- KiwiErrDuplicateConstraint: The given constraint has already been added to the solver.
   --- KiwiErrUnsatisfiableConstraint: The given constraint is required and cannot be satisfied.
   ---@param solver kiwi.Solver
   ---@return kiwi.Constraint
   function Constraint_cls:add_to(solver)
      solver:add_constraint(self)
      return self
   end

   --- Remove the constraint from the solver.
   --- Raises:
   --- KiwiErrUnknownConstraint: The given constraint has not been added to the solver.
   ---@param solver kiwi.Solver
   ---@return kiwi.Constraint
   function Constraint_cls:remove_from(solver)
      solver:remove_constraint(self)
      return self
   end

   local Constraint_mt = {
      __index = Constraint_cls,
   }

   function Constraint_mt:__new(lhs, rhs, op, strength)
      return ffi_gc(
         ljkiwi.kiwi_constraint_construct(lhs, rhs, op or "EQ", strength or REQUIRED),
         ljkiwi.kiwi_constraint_release
      )
   end

   local OPS = { [0] = "<=", ">=", "==" }
   local STRENGTH_NAMES = {
      [Strength.REQUIRED] = "required",
      [Strength.STRONG] = "strong",
      [Strength.MEDIUM] = "medium",
      [Strength.WEAK] = "weak",
   }

   function Constraint_mt:__tostring()
      local strength = self:strength()
      local strength_str = STRENGTH_NAMES[strength] or tostring(strength)
      local op = OPS[tonumber(self:op())]
      return strformat("%s %s 0 | %s", tostring(self:expression()), op, strength_str)
   end

   ffi.metatype(Constraint, Constraint_mt)
end

do
   local constraints = {}
   kiwi.constraints = constraints

   --- Create a constraint between a pair of variables with ratio.
   --- The constraint is of the form `left [op|==] coeff right + [constant|0.0]`.
   ---@param left kiwi.Var
   ---@param coeff number right side term coefficient
   ---@param right kiwi.Var
   ---@param constant number? constant (default 0.0)
   ---@param op kiwi.RelOp? relational operator (default "EQ")
   ---@param strength number? strength (default REQUIRED)
   ---@return kiwi.Constraint
   ---@nodiscard
   function constraints.pair_ratio(left, coeff, right, constant, op, strength)
      assert(ffi_istype(Var, left) and ffi_istype(Var, right))
      local dt = tmpexpr.terms_[0]
      dt.var = left
      dt.coefficient = 1.0
      dt = tmpexpr.terms_[1]
      dt.var = right
      dt.coefficient = -coeff
      tmpexpr.constant = constant ~= nil and constant or 0
      tmpexpr.term_count = 2

      return ffi_gc(
         ljkiwi.kiwi_constraint_construct(tmpexpr, nil, op or "EQ", strength or REQUIRED),
         ljkiwi.kiwi_constraint_release
      ) --[[@as kiwi.Constraint]]
   end

   local pair_ratio = constraints.pair_ratio

   --- Create a constraint between a pair of variables with ratio.
   --- The constraint is of the form `left [op|==] right + [constant|0.0]`.
   ---@param left kiwi.Var
   ---@param right kiwi.Var
   ---@param constant number? constant (default 0.0)
   ---@param op kiwi.RelOp? relational operator (default "EQ")
   ---@param strength number? strength (default REQUIRED)
   ---@return kiwi.Constraint
   ---@nodiscard
   function constraints.pair(left, right, constant, op, strength)
      return pair_ratio(left, 1.0, right, constant, op, strength)
   end

   --- Create a single term constraint
   --- The constraint is of the form `var [op|==] [constant|0.0]`.
   ---@param var kiwi.Var
   ---@param constant number? constant (default 0.0)
   ---@param op kiwi.RelOp? relational operator (default "EQ")
   ---@param strength number? strength (default REQUIRED)
   ---@return kiwi.Constraint
   ---@nodiscard
   function constraints.single(var, constant, op, strength)
      assert(ffi_istype(Var, var))
      tmpexpr.constant = -(constant or 0)
      tmpexpr.term_count = 1
      local t = tmpexpr.terms_[0]
      t.var = var
      t.coefficient = 1.0

      return ffi_gc(
         ljkiwi.kiwi_constraint_construct(tmpexpr, nil, op or "EQ", strength or REQUIRED),
         ljkiwi.kiwi_constraint_release
      ) --[[@as kiwi.Constraint]]
   end
end

do
   local bit = require("bit")
   local band, bor, lshift = bit.band, bit.bor, bit.lshift
   local C = ffi.C

   --- Produce a custom error raise mask
   --- Error kinds specified in the mask will not cause a lua
   --- error to be raised.
   ---@param kinds (kiwi.ErrKind|integer)[]
   ---@param invert boolean?
   ---@return integer
   function kiwi.error_mask(kinds, invert)
      local mask = 0
      for _, k in ipairs(kinds) do
         mask = bor(mask, lshift(1, kiwi.ErrKind(k)))
      end
      return invert and bit.bnot(mask) or mask
   end

   kiwi.ERROR_MASK_ALL = 0xFFFF
   --- an error mask that raises errors only for fatal conditions
   kiwi.ERROR_MASK_NON_FATAL = bit.bnot(kiwi.error_mask({
      "KiwiErrInternalSolverError",
      "KiwiErrAlloc",
      "KiwiErrNullObject",
      "KiwiErrUnknown",
   }))

   ---@class kiwi.KiwiErr: ffi.cdata*
   ---@field package kind kiwi.ErrKind
   ---@field package message ffi.cdata*
   ---@field package must_free boolean
   ---@overload fun(): kiwi.KiwiErr
   local KiwiErr = ffi.typeof("struct KiwiErr") --[[@as kiwi.KiwiErr]]

   local Error_mt = {
      ---@param self kiwi.Error
      ---@return string
      __tostring = function(self)
         return strformat("%s: (%s, %s)", self.message, tostring(self.solver), tostring(self.item))
      end,
   }

   ---@class kiwi.Error
   ---@field kind kiwi.ErrKind
   ---@field message string
   ---@field solver kiwi.Solver?
   ---@field item any?
   kiwi.Error = Error_mt

   function kiwi.is_error(o)
      return type(o) == "table" and getmetatable(o) == Error_mt
   end

   ---@param kind kiwi.ErrKind
   ---@param message string
   ---@param solver kiwi.Solver
   ---@param item any
   ---@return kiwi.Error
   local function new_error(kind, message, solver, item)
      return setmetatable({
         kind = kind,
         message = message,
         solver = solver,
         item = item,
      }, Error_mt)
   end

   ---@generic T
   ---@param f fun(solver: kiwi.Solver, item: T, ...): kiwi.KiwiErr?
   ---@param solver kiwi.Solver
   ---@param item T
   ---@return T, kiwi.Error?
   local function try_solver(f, solver, item, ...)
      local err = f(solver, item, ...)
      if err ~= nil then
         local kind = err.kind
         local message = err.message ~= nil and ffi_string(err.message) or ""
         if err.must_free then
            C.free(err)
         end
         local errdata = new_error(kind, message, solver, item)
         local error_mask = ljkiwi.kiwi_solver_get_error_mask(solver)
         return item,
            band(error_mask, lshift(1, kind --[[@as integer]])) == 0 and error(errdata)
               or errdata
      end
      return item
   end
   ---@class kiwi.Solver: ffi.cdata*
   ---@overload fun(error_mask: (integer|(kiwi.ErrKind|integer)[] )?): kiwi.Solver
   local Solver_cls = {
      --- Test whether a constraint is in the solver.
      ---@type fun(self: kiwi.Solver, constraint: kiwi.Constraint): boolean
      has_constraint = ljkiwi.kiwi_solver_has_constraint,

      --- Test whether an edit variable has been added to the solver.
      ---@type fun(self: kiwi.Solver, var: kiwi.Var): boolean
      has_edit_var = ljkiwi.kiwi_solver_has_edit_var,

      --- Update the values of the external solver variables.
      ---@type fun(self: kiwi.Solver)
      update_vars = ljkiwi.kiwi_solver_update_vars,

      --- Reset the solver to the empty starting conditions.
      ---
      --- This method resets the internal solver state to the empty starting
      --- condition, as if no constraints or edit variables have been added.
      --- This can be faster than deleting the solver and creating a new one
      --- when the entire system must change, since it can avoid unecessary
      --- heap (de)allocations.
      ---@type fun(self: kiwi.Solver)
      reset = ljkiwi.kiwi_solver_reset,

      --- Dump a representation of the solver to stdout.
      ---@type fun(self: kiwi.Solver)
      dump = ljkiwi.kiwi_solver_dump,
   }

   --- Sets the error mask for the solver.
   ---@param mask integer|(kiwi.ErrKind|integer)[] the mask value or an array of kinds
   ---@param invert boolean? whether to invert the mask if an array was passed for mask
   function Solver_cls:set_error_mask(mask, invert)
      if type(mask) == "table" then
         mask = kiwi.error_mask(mask, invert)
      end
      ljkiwi.kiwi_solver_set_error_mask(self, mask)
   end

   ---@generic T
   ---@param solver kiwi.Solver
   ---@param items T|T[]
   ---@param f fun(solver: kiwi.Solver, item: T, ...): kiwi.KiwiErr?
   ---@return T|T[], kiwi.Error?
   local function add_remove_items(solver, items, f, ...)
      for _, item in ipairs(items) do
         local _, err = try_solver(f, solver, item, ...)
         if err ~= nil then
            return items, err
         end
      end
      return items
   end

   --- Add a constraint to the solver.
   --- Errors:
   --- KiwiErrDuplicateConstraint
   --- KiwiErrUnsatisfiableConstraint
   ---@param constraint kiwi.Constraint
   ---@return kiwi.Constraint constraint, kiwi.Error?
   function Solver_cls:add_constraint(constraint)
      return try_solver(ljkiwi.kiwi_solver_add_constraint, self, constraint)
   end

   --- Add constraints to the solver.
   --- Errors:
   --- KiwiErrDuplicateConstraint
   --- KiwiErrUnsatisfiableConstraint
   ---@param constraints kiwi.Constraint[]
   ---@return kiwi.Constraint[] constraints, kiwi.Error?
   function Solver_cls:add_constraints(constraints)
      return add_remove_items(self, constraints, ljkiwi.kiwi_solver_add_constraint)
   end

   --- Remove a constraint from the solver.
   --- Errors:
   --- KiwiErrUnknownConstraint
   ---@param constraint kiwi.Constraint
   ---@return kiwi.Constraint constraint, kiwi.Error?
   function Solver_cls:remove_constraint(constraint)
      return try_solver(ljkiwi.kiwi_solver_remove_constraint, self, constraint)
   end

   --- Remove constraints from the solver.
   --- Errors:
   --- KiwiErrUnknownConstraint
   ---@param constraints kiwi.Constraint[]
   ---@return kiwi.Constraint[] constraints, kiwi.Error?
   function Solver_cls:remove_constraints(constraints)
      return add_remove_items(self, constraints, ljkiwi.kiwi_solver_remove_constraint)
   end

   --- Add an edit variables to the solver.
   ---
   --- This method should be called before the `suggestValue` method is
   --- used to supply a suggested value for the given edit variable.
   --- Errors:
   --- KiwiErrDuplicateEditVariable
   --- KiwiErrBadRequiredStrength: The given strength is >= required.
   ---@param var kiwi.Var the variable to add as an edit variable
   ---@param strength number the strength of the edit variable (must be less than `Strength.REQUIRED`)
   ---@return kiwi.Var var, kiwi.Error?
   function Solver_cls:add_edit_var(var, strength)
      return try_solver(ljkiwi.kiwi_solver_add_edit_var, self, var, strength)
   end

   --- Add edit variables to the solver.
   ---
   --- This method should be called before the `suggestValue` method is
   --- used to supply a suggested value for the given edit variable.
   --- Errors:
   --- KiwiErrDuplicateEditVariable
   --- KiwiErrBadRequiredStrength: The given strength is >= required.
   ---@param vars kiwi.Var[] the variables to add as an edit variable
   ---@param strength number the strength of the edit variables (must be less than `Strength.REQUIRED`)
   ---@return kiwi.Var[] vars, kiwi.Error?
   function Solver_cls:add_edit_vars(vars, strength)
      return add_remove_items(self, vars, ljkiwi.kiwi_solver_add_edit_var, strength)
   end

   --- Remove an edit variable from the solver.
   --- Raises:
   --- KiwiErrUnknownEditVariable
   ---@param var kiwi.Var the edit variable to remove
   ---@return kiwi.Var var, kiwi.Error?
   function Solver_cls:remove_edit_var(var)
      return try_solver(ljkiwi.kiwi_solver_remove_edit_var, self, var)
   end

   --- Removes edit variables from the solver.
   --- Raises:
   --- KiwiErrUnknownEditVariable
   ---@param vars kiwi.Var[] the edit variables to remove
   ---@return kiwi.Var[] vars, kiwi.Error?
   function Solver_cls:remove_edit_vars(vars)
      return add_remove_items(self, vars, ljkiwi.kiwi_solver_remove_edit_var)
   end

   --- Suggest a value for the given edit variable.
   --- This method should be used after an edit variable has been added to the solver in order
   --- to suggest the value for that variable. After all suggestions have been made,
   --- the `update_vars` methods can be used to update the values of the external solver variables.
   --- Raises:
   --- KiwiErrUnknownEditVariable
   ---@param var kiwi.Var the edit variable to suggest a value for
   ---@param value number the suggested value
   ---@return kiwi.Var var, kiwi.Error?
   function Solver_cls:suggest_value(var, value)
      return try_solver(ljkiwi.kiwi_solver_suggest_value, self, var, value)
   end

   --- Suggest values for the given edit variables.
   --- Convenience wrapper of `suggest_value` that takes tables of `kiwi.Var` and number pairs.
   --- Raises:
   --- KiwiErrUnknownEditVariable: The given edit variable has not been added to the solver.
   ---@param vars kiwi.Var[] edit variables to suggest
   ---@param values number[] suggested values
   ---@return kiwi.Var[] vars, number[] values, kiwi.Error?
   function Solver_cls:suggest_values(vars, values)
      for i, var in ipairs(vars) do
         local _, err = try_solver(ljkiwi.kiwi_solver_suggest_value, self, var, values[i])
         if err ~= nil then
            return vars, values, err
         end
      end
      return vars, values
   end

   --- Dump a representation of the solver to a string.
   ---@return string
   ---@nodiscard
   function Solver_cls:dumps()
      local cs = ljkiwi.kiwi_solver_dumps(self)
      local s = ffi_string(cs)
      C.free(cs)
      return s
   end

   local Solver_mt = {
      __index = Solver_cls,
   }

   function Solver_mt:__new(error_mask)
      if type(error_mask) == "table" then
         error_mask = kiwi.error_mask(error_mask)
      end

      return ffi_gc(ljkiwi.kiwi_solver_construct(error_mask or 0), ljkiwi.kiwi_solver_destroy) --[[@as kiwi.Constraint]]
   end

   local Solver = ffi.metatype(ffi.typeof("struct KiwiSolver"), Solver_mt) --[[@as kiwi.Solver]]
   kiwi.Solver = Solver

   function kiwi.is_solver(s)
      return ffi_istype(Solver, s)
   end
end

return kiwi
