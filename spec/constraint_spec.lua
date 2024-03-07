expose("module", function()
   require("kiwi")
end)

describe("Constraint", function()
   local kiwi = require("kiwi")
   local LUA_VERSION = tonumber(_VERSION:match("%d+%.%d+"))

   describe("construction", function()
      local v, lhs
      before_each(function()
         v = kiwi.Var("foo")
         lhs = v + 1
      end)

      it("has correct type", function()
         assert.True(kiwi.is_constraint(kiwi.Constraint()))
         assert.False(kiwi.is_constraint(v))
      end)

      it("default op and strength", function()
         local c = kiwi.Constraint(lhs)
         assert.equal("EQ", c:op())
         assert.equal(kiwi.strength.REQUIRED, c:strength())
      end)

      it("configure op", function()
         local c = kiwi.Constraint(lhs, nil, "LE")
         assert.equal("LE", c:op())
      end)
      it("configure strength", function()
         local c = kiwi.Constraint(lhs, nil, "GE", kiwi.strength.STRONG)
         assert.equal(kiwi.strength.STRONG, c:strength())
      end)

      -- TODO: standardize formatting
      it("formats well", function()
         local c = kiwi.Constraint(lhs)
         if LUA_VERSION <= 5.2 then
            assert.equal("1 foo + 1 == 0 | required", tostring(c))
         else
            assert.equal("1.0 foo + 1.0 == 0 | required", tostring(c))
         end

         c = kiwi.Constraint(lhs * 2, nil, "GE", kiwi.strength.STRONG)
         if LUA_VERSION <= 5.2 then
            assert.equal("2 foo + 2 >= 0 | strong", tostring(c))
         else
            assert.equal("2.0 foo + 2.0 >= 0 | strong", tostring(c))
         end

         c = kiwi.Constraint(lhs / 2, nil, "LE", kiwi.strength.MEDIUM)
         assert.equal("0.5 foo + 0.5 <= 0 | medium", tostring(c))

         c = kiwi.Constraint(lhs, kiwi.Expression(3), "GE", kiwi.strength.WEAK)
         if LUA_VERSION <= 5.2 then
            assert.equal("1 foo + -2 >= 0 | weak", tostring(c))
         else
            assert.equal("1.0 foo + -2.0 >= 0 | weak", tostring(c))
         end
      end)

      it("rejects invalid args", function()
         assert.error(function()
            local _ = kiwi.Constraint(1)
         end)
         assert.error(function()
            local _ = kiwi.Constraint(lhs, 1)
         end)
         assert.error(function()
            local _ = kiwi.Constraint("")
         end)
         assert.error(function()
            local _ = kiwi.Constraint(lhs, "")
         end)
         assert.error(function()
            local _ = kiwi.Constraint(lhs, nil, "foo")
         end)
         assert.error(function()
            local _ = kiwi.Constraint(lhs, nil, "LE", "foo")
         end)
      end)
      it("combines lhs and rhs", function()
         local v2 = kiwi.Var("bar")
         local rhs = kiwi.Expression(3, 5 * v2, 3 * v)
         local c = kiwi.Constraint(lhs, rhs)
         assert.True(c:violated())
         v:set(9)
         v2:set(-4)
         assert.False(c:violated())

         assert.equal(-2.0, c:expression().constant)
      end)
   end)

   describe("method", function()
      local c, v

      before_each(function()
         v = kiwi.Var("foo")
         c = kiwi.Constraint(2 * v + 1)
      end)

      it("violated", function()
         assert.True(c:violated())
         v:set(-0.5)
         assert.False(c:violated())
      end)

      it("add/remove constraint", function()
         local s = kiwi.Solver()
         c:add_to(s)
         assert.True(s:has_constraint(c))

         c:remove_from(s)
         assert.False(s:has_constraint(c))
      end)
   end)
end)
