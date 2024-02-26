expose("module", function()
   require("kiwi")
end)

describe("Expression", function()
   local kiwi = require("kiwi")
   local LUA_VERSION = tonumber(_VERSION:match("%d+%.%d+"))

   it("construction", function()
      local v = kiwi.Var("foo")
      local v2 = kiwi.Var("bar")
      local v3 = kiwi.Var("aux")
      local e1 = kiwi.Expression(0, v * 1, v2 * 2, v3 * 3)
      local e2 = kiwi.Expression(10, v * 1, v2 * 2, v3 * 3)

      local constants = { 0, 10 }
      for i, e in ipairs({ e1, e2 }) do
         assert.equal(constants[i], e.constant)
         local terms = e:terms()
         assert.equal(3, #terms)
         assert.equal(v, terms[1].var)
         assert.equal(1.0, terms[1].coefficient)
         assert.equal(v2, terms[2].var)
         assert.equal(2.0, terms[2].coefficient)
         assert.equal(v3, terms[3].var)
         assert.equal(3.0, terms[3].coefficient)
      end

      if LUA_VERSION <= 5.2 then
         assert.equal("1 foo + 2 bar + 3 aux + 10", tostring(e2))
      else
         assert.equal("1.0 foo + 2.0 bar + 3.0 aux + 10.0", tostring(e2))
      end

      assert.error(function()
         kiwi.Expression(0, 0, v2 * 2, v3 * 3)
      end)
   end)

   describe("method", function()
      local v, t, e
      before_each(function()
         v = kiwi.Var("foo")
         v:set(42)
         t = kiwi.Term(v, 10)
         e = t + 5
      end)

      it("has value", function()
         v:set(42)
         assert.equal(425, e:value())
         v:set(87)
         assert.equal(875, e:value())
      end)

      it("can be copied", function()
         local e2 = e:copy()
         assert.equal(e.constant, e2.constant)
         local t1, t2 = e:terms(), e2:terms()
         assert.equal(#t1, #t2)
         for i = 1, #t1 do
            assert.equal(t1[i].var, t2[i].var)
            assert.equal(t1[i].coefficient, t2[i].coefficient)
         end
      end)

      it("neg", function()
         local neg = -e --[[@as kiwi.Expression]]
         assert.True(kiwi.is_expression(neg))
         local terms = neg:terms()
         assert.equal(1, #terms)
         assert.equal(v, terms[1].var)
         assert.equal(-10.0, terms[1].coefficient)
         assert.equal(-5, neg.constant)
      end)

      describe("bin op", function()
         local v2, t2, e2
         before_each(function()
            v2 = kiwi.Var("bar")
            t2 = kiwi.Term(v2)
            e2 = v2 - 10
         end)

         it("mul", function()
            for _, prod in ipairs({ e * 2.0, 2 * e }) do
               assert.True(kiwi.is_expression(prod))
               local terms = prod:terms()
               assert.equal(1, #terms)
               assert.equal(v, terms[1].var)
               assert.equal(20.0, terms[1].coefficient)
               assert.equal(10, prod.constant)
            end

            assert.error(function()
               local _ = e * v
            end)
         end)

         it("div", function()
            local quot = e / 2.0
            assert.True(kiwi.is_expression(quot))
            local terms = quot:terms()
            assert.equal(1, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(5.0, terms[1].coefficient)
            assert.equal(2.5, quot.constant)

            assert.error(function()
               local _ = e / v2
            end)
         end)

         it("add", function()
            for _, sum in ipairs({ e + 2.0, 2 + e }) do
               assert.True(kiwi.is_expression(sum))
               assert.equal(7.0, sum.constant)

               local terms = sum:terms()
               assert.equal(1, #terms)
               assert.equal(10.0, terms[1].coefficient)
               assert.equal(v, terms[1].var)
            end

            local sum = e + v2
            assert.True(kiwi.is_expression(sum))
            assert.equal(5, sum.constant)
            local terms = sum:terms()
            assert.equal(2, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(10.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(1.0, terms[2].coefficient)

            sum = e + t2
            assert.True(kiwi.is_expression(sum))
            assert.equal(5, sum.constant)
            terms = sum:terms()
            assert.equal(2, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(10.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(1.0, terms[2].coefficient)

            sum = e + e2
            assert.True(kiwi.is_expression(sum))
            assert.equal(-5, sum.constant)
            terms = sum:terms()
            assert.equal(2, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(10.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(1.0, terms[2].coefficient)

            assert.error(function()
               local _ = t + "foo"
            end)
            assert.error(function()
               local _ = t + {}
            end)
         end)

         it("sub", function()
            local constants = { 3, -3 }
            for i, diff in ipairs({ e - 2.0, 2 - e }) do
               local constant = constants[i]
               assert.True(kiwi.is_expression(diff))
               assert.equal(constant, diff.constant)

               local terms = diff:terms()
               assert.equal(1, #terms)
               assert.equal(v, terms[1].var)
               assert.equal(constant < 0 and -10.0 or 10.0, terms[1].coefficient)
            end

            local diff = e - v2
            assert.True(kiwi.is_expression(diff))
            assert.equal(5, diff.constant)
            local terms = diff:terms()
            assert.equal(2, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(10.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(-1.0, terms[2].coefficient)

            diff = e - t2
            assert.True(kiwi.is_expression(diff))
            assert.equal(5, diff.constant)
            terms = diff:terms()
            assert.equal(2, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(10.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(-1.0, terms[2].coefficient)

            diff = e - e2
            assert.True(kiwi.is_expression(diff))
            assert.equal(15, diff.constant)
            terms = diff:terms()
            assert.equal(2, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(10.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(-1.0, terms[2].coefficient)

            assert.error(function()
               local _ = e - "foo"
            end)
            assert.error(function()
               local _ = e - {}
            end)
         end)

         it("constraint expr op expr", function()
            local ops = { "LE", "EQ", "GE" }
            for i, meth in ipairs({ "le", "eq", "ge" }) do
               local c = e[meth](e, e2)
               assert.True(kiwi.is_constraint(c))

               local expr = c:expression()
               local terms = expr:terms()
               assert.equal(2, #terms)

               -- order can be randomized due to use of map
               if terms[1].var ~= v then
                  terms[1], terms[2] = terms[2], terms[1]
               end
               assert.equal(v, terms[1].var)
               assert.equal(10.0, terms[1].coefficient)
               assert.equal(v2, terms[2].var)
               assert.equal(-1.0, terms[2].coefficient)

               assert.equal(15, expr.constant)
               assert.equal(ops[i], c:op())
               assert.equal(kiwi.strength.REQUIRED, c:strength())
            end
         end)
      end)
   end)
end)
