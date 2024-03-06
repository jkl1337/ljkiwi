expose("module", function()
   require("kiwi")
end)

describe("solver", function()
   local kiwi = require("kiwi")
   ---@type kiwi.Solver
   local solver

   before_each(function()
      solver = kiwi.Solver()
   end)

   it("should create a solver", function()
      assert.True(kiwi.is_solver(solver))
      assert.False(kiwi.is_solver(kiwi.Term(kiwi.Var("v1"))))
   end)

   describe("edit variables", function()
      local v1, v2, v3
      before_each(function()
         v1 = kiwi.Var("foo")
         v2 = kiwi.Var("bar")
         v3 = kiwi.Var("baz")
      end)

      describe("add_edit_var", function()
         it("should add a variable", function()
            solver:add_edit_var(v1, kiwi.strength.STRONG)
            assert.True(solver:has_edit_var(v1))
         end)

         it("should return the argument", function()
            assert.equal(v1, solver:add_edit_var(v1, kiwi.strength.STRONG))
         end)

         it("should error on incorrect type", function()
            assert.error(function()
               solver:add_edit_var("", kiwi.strength.STRONG) ---@diagnostic disable-line: param-type-mismatch
            end)
            assert.error(function()
               solver:add_edit_var(v1, "") ---@diagnostic disable-line: param-type-mismatch
            end)
         end)

         it("should require a strength argument", function()
            assert.error(function()
               solver:add_edit_var(v1) ---@diagnostic disable-line: missing-parameter
            end)
         end)

         it("should error on duplicate variable", function()
            solver:add_edit_var(v1, kiwi.strength.STRONG)
            local _, err = pcall(function()
               return solver:add_edit_var(v1, kiwi.strength.STRONG)
            end)
            assert.True(kiwi.is_error(err))
            assert.True(kiwi.is_solver(err.solver))
            assert.equal(v1, err.item)
            assert.equal("KiwiErrDuplicateEditVar", err.kind)
            assert.equal("The edit variable has already been added to the solver.", err.message)
         end)

         it("should error on invalid strength", function()
            local _, err = pcall(function()
               return solver:add_edit_var(v1, kiwi.strength.REQUIRED)
            end)
            assert.True(kiwi.is_error(err))
            assert.True(kiwi.is_solver(err.solver))
            assert.equal(v1, err.item)
            assert.equal("KiwiErrBadRequiredStrength", err.kind)
            assert.equal("A required strength cannot be used in this context.", err.message)
         end)

         it("should return errors for duplicate variables", function()
            solver:set_error_mask({ "KiwiErrDuplicateEditVar", "KiwiErrBadRequiredStrength" })
            local ret, err = solver:add_edit_var(v1, kiwi.strength.STRONG)
            assert.Nil(err)

            ret, err = solver:add_edit_var(v1, kiwi.strength.STRONG)

            assert.equal(v1, ret)
            assert.True(kiwi.is_error(err))
            ---@diagnostic disable: need-check-nil
            assert.True(kiwi.is_solver(err.solver))
            assert.equal(v1, err.item)
            assert.equal("KiwiErrDuplicateEditVar", err.kind)
            assert.equal("The edit variable has already been added to the solver.", err.message)
            ---@diagnostic enable: need-check-nil
         end)

         it("should return errors for invalid strength", function()
            solver:set_error_mask({ "KiwiErrDuplicateEditVar", "KiwiErrBadRequiredStrength" })

            ---@diagnostic disable: need-check-nil
            local ret, err = solver:add_edit_var(v2, kiwi.strength.REQUIRED)
            assert.equal(v2, ret)
            assert.True(kiwi.is_error(err))
            assert.True(kiwi.is_solver(err.solver))
            assert.equal(v2, err.item)
            assert.equal("KiwiErrBadRequiredStrength", err.kind)
            assert.equal("A required strength cannot be used in this context.", err.message)
            ---@diagnostic enable: need-check-nil
         end)

         it("tolerates a nil self", function()
            assert.error(function()
               kiwi.Solver.add_edit_var(nil, v1, kiwi.strength.STRONG) ---@diagnostic disable-line: param-type-mismatch
            end)
         end)

         it("tolerates a nil var", function()
            assert.error(function()
               solver:add_edit_var(nil, kiwi.strength.STRONG) ---@diagnostic disable-line: param-type-mismatch
            end)
         end)
      end)

      describe("add_edit_vars", function()
         it("should add variables", function()
            solver:add_edit_vars({ v1, v2 }, kiwi.strength.STRONG)
            assert.True(solver:has_edit_var(v1))
            assert.True(solver:has_edit_var(v2))
            assert.False(solver:has_edit_var(v3))
         end)

         it("should return the argument", function()
            local arg = { v1, v2, v3 }
            assert.equal(arg, solver:add_edit_vars(arg, kiwi.strength.STRONG))
         end)

         it("should error on incorrect type", function()
            assert.error(function()
               solver:add_edit_vars(v1, kiwi.strength.STRONG) ---@diagnostic disable-line: param-type-mismatch
            end)
            assert.error(function()
               solver:add_edit_vars("", kiwi.strength.STRONG) ---@diagnostic disable-line: param-type-mismatch
            end)
            assert.error(function()
               solver:add_edit_vars(v1, "") ---@diagnostic disable-line: param-type-mismatch
            end)
         end)

         it("should require a strength argument", function()
            assert.error(function()
               solver:add_edit_vars({ v1, v2 }) ---@diagnostic disable-line: missing-parameter
            end)
         end)

         it("should error on duplicate variable", function()
            local _, err = pcall(function()
               return solver:add_edit_vars({ v1, v2, v3, v2, v3 }, kiwi.strength.STRONG)
            end)
            assert.True(kiwi.is_error(err))
            assert.True(kiwi.is_solver(err.solver))
            assert.equal(v2, err.item)
            assert.equal("KiwiErrDuplicateEditVar", err.kind)
            assert.equal("The edit variable has already been added to the solver.", err.message)
         end)

         it("should error on invalid strength", function()
            local _, err = pcall(function()
               return solver:add_edit_vars({ v1, v2 }, kiwi.strength.REQUIRED)
            end)
            assert.True(kiwi.is_error(err))
            assert.True(kiwi.is_solver(err.solver))
            assert.equal(v1, err.item)
            assert.equal("KiwiErrBadRequiredStrength", err.kind)
            assert.equal("A required strength cannot be used in this context.", err.message)
         end)

         it("should return errors for duplicate variables", function()
            solver:set_error_mask({ "KiwiErrDuplicateEditVar", "KiwiErrBadRequiredStrength" })
            local ret, err = solver:add_edit_vars({ v1, v2, v3 }, kiwi.strength.STRONG)
            assert.Nil(err)

            local arg = { v1, v2, v3 }
            ret, err = solver:add_edit_vars(arg, kiwi.strength.STRONG)
            assert.equal(arg, ret)
            assert.True(kiwi.is_error(err))
            ---@diagnostic disable: need-check-nil
            assert.True(kiwi.is_solver(err.solver))
            assert.equal(v1, err.item)
            assert.equal("KiwiErrDuplicateEditVar", err.kind)
            assert.equal("The edit variable has already been added to the solver.", err.message)
            ---@diagnostic enable: need-check-nil
         end)

         it("should return errors for invalid strength", function()
            solver:set_error_mask({ "KiwiErrDuplicateEditVar", "KiwiErrBadRequiredStrength" })
            arg = { v2, v3 }
            local ret, err = solver:add_edit_vars(arg, kiwi.strength.REQUIRED)
            assert.equal(arg, ret)
            assert.True(kiwi.is_error(err))
            ---@diagnostic disable: need-check-nil
            assert.True(kiwi.is_solver(err.solver))
            assert.equal(v2, err.item)
            assert.equal("KiwiErrBadRequiredStrength", err.kind)
            assert.equal("A required strength cannot be used in this context.", err.message)
            ---@diagnostic enable: need-check-nil
         end)

         it("tolerates a nil self", function()
            assert.has_error(function()
               kiwi.Solver.add_edit_vars(nil, { v1, v2 }, kiwi.strength.STRONG) ---@diagnostic disable-line: param-type-mismatch
            end)
         end)
      end)

      describe("remove_edit_var", function()
         it("should remove a variable", function()
            solver:add_edit_vars({ v1, v2, v3 }, kiwi.strength.STRONG)
            assert.True(solver:has_edit_var(v2))
            solver:remove_edit_var(v2)
            assert.True(solver:has_edit_var(v1))
            assert.False(solver:has_edit_var(v2))
            assert.True(solver:has_edit_var(v3))
         end)

         it("should return the argument", function()
            solver:add_edit_var(v1, kiwi.strength.STRONG)
            assert.equal(v1, solver:remove_edit_var(v1))
         end)

         it("should error on incorrect type", function()
            assert.error(function()
               solver:remove_edit_var("") ---@diagnostic disable-line: param-type-mismatch
            end)
            assert.error(function()
               solver:remove_edit_var({ v1 }) ---@diagnostic disable-line: param-type-mismatch
            end)
         end)

         it("should error on unknown variable", function()
            solver:add_edit_var(v1, kiwi.strength.STRONG)
            local _, err = pcall(function()
               return solver:remove_edit_var(v2)
            end)
            assert.True(kiwi.is_error(err))
            assert.True(kiwi.is_solver(err.solver))
            assert.equal(v2, err.item)
            assert.equal("KiwiErrUnknownEditVar", err.kind)
            assert.equal("The edit variable has not been added to the solver.", err.message)
         end)

         it("should return errors if requested", function()
            solver:set_error_mask({ "KiwiErrDuplicateEditVar", "KiwiErrUnknownEditVar" })

            local ret, err = solver:remove_edit_var(v1)

            assert.equal(v1, ret)
            assert.True(kiwi.is_error(err))
            ---@diagnostic disable: need-check-nil
            assert.True(kiwi.is_solver(err.solver))
            assert.equal(v1, err.item)
            assert.equal("KiwiErrUnknownEditVar", err.kind)
            assert.equal("The edit variable has not been added to the solver.", err.message)
            ---@diagnostic enable: need-check-nil
         end)

         it("tolerates a nil self", function()
            assert.has_error(function()
               kiwi.Solver.remove_edit_var(nil, v1) ---@diagnostic disable-line: param-type-mismatch
            end)
         end)

         it("tolerates a nil var", function()
            assert.has_error(function()
               solver:remove_edit_var(nil) ---@diagnostic disable-line: param-type-mismatch
            end)
         end)
      end)

      describe("remove_edit_vars", function()
         it("should remove variables", function()
            solver:add_edit_vars({ v1, v2, v3 }, kiwi.strength.STRONG)
            assert.True(solver:has_edit_var(v2))
            assert.True(solver:has_edit_var(v3))

            solver:remove_edit_vars({ v2, v3 })
            assert.False(solver:has_edit_var(v2))
            assert.False(solver:has_edit_var(v3))
         end)

         it("should return the argument", function()
            local arg = { v1, v2, v3 }
            solver:add_edit_vars(arg, kiwi.strength.STRONG)
            assert.equal(arg, solver:remove_edit_vars(arg))
         end)

         it("should error on incorrect type", function()
            assert.error(function()
               solver:remove_edit_vars(v1) ---@diagnostic disable-line: param-type-mismatch
            end)
            assert.error(function()
               solver:remove_edit_vars("") ---@diagnostic disable-line: param-type-mismatch
            end)
         end)

         it("should error on unknown variables", function()
            local _, err = pcall(function()
               return solver:remove_edit_vars({ v2, v1 })
            end)
            assert.True(kiwi.is_error(err))
            assert.True(kiwi.is_solver(err.solver))
            assert.equal(v2, err.item)
            assert.equal("KiwiErrUnknownEditVar", err.kind)
            assert.equal("The edit variable has not been added to the solver.", err.message)
         end)

         it("should return errors for unknown variables", function()
            solver:set_error_mask({ "KiwiErrDuplicateEditVar", "KiwiErrUnknownEditVar" })
            local ret, err = solver:add_edit_vars({ v1, v2 }, kiwi.strength.STRONG)
            assert.Nil(err)

            local arg = { v1, v2, v3 }
            ret, err = solver:remove_edit_vars(arg)
            assert.equal(arg, ret)
            assert.True(kiwi.is_error(err))
            ---@diagnostic disable: need-check-nil
            assert.True(kiwi.is_solver(err.solver))
            assert.equal(v3, err.item)
            assert.equal("KiwiErrUnknownEditVar", err.kind)
            assert.equal("The edit variable has not been added to the solver.", err.message)
            ---@diagnostic enable: need-check-nil
         end)

         it("tolerates a nil self", function()
            assert.has_error(function()
               kiwi.Solver.remove_edit_vars(nil, { v1, v2 }) ---@diagnostic disable-line: param-type-mismatch
            end)
         end)
      end)
   end)
end)
