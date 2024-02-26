expose("module", function()
   require("kiwi")
end)

describe("Term", function()
   local kiwi = require("kiwi")
   local LUA_VERSION = tonumber(_VERSION:match("%d+%.%d+"))

   it("construction", function()
      local v = kiwi.Var("foo")
      local t = kiwi.Term(v)
      assert.equal(v, t.var)
      assert.equal(1.0, t.coefficient)

      t = kiwi.Term(v, 100)
      assert.equal(v, t.var)
      assert.equal(100, t.coefficient)

      if LUA_VERSION <= 5.2 then
         assert.equal("100 foo", tostring(t))
      else
         assert.equal("100.0 foo", tostring(t))
      end

      assert.error(function()
         kiwi.Term("")
      end)
   end)

   describe("method", function()
      local v, v2, t, t2

      before_each(function()
         v = kiwi.Var("foo")
         t = kiwi.Term(v, 10)
      end)

      it("has value", function()
         v:set(42)
         assert.equal(420, t:value())
         v:set(87)
         assert.equal(870, t:value())
      end)

      it("has toexpr", function()
         local e = t:toexpr()
         assert.True(kiwi.is_expression(e))
         assert.equal(0, e.constant)
         local terms = e:terms()
         assert.equal(1, #terms)
         assert.equal(v, terms[1].var)
         assert.equal(10.0, terms[1].coefficient)
      end)

      it("neg", function()
         local neg = -t --[[@as kiwi.Term]]
         assert.True(kiwi.is_term(neg))
         assert.equal(v, neg.var)
         assert.equal(-10, neg.coefficient)
      end)

      describe("bin op", function()
         before_each(function()
            v2 = kiwi.Var("bar")
            t2 = kiwi.Term(v2)
         end)

         it("mul", function()
            for _, prod in ipairs({ t * 2.0, 2 * t }) do
               assert.True(kiwi.is_term(prod))
               assert.equal(v, prod.var)
               assert.equal(20, prod.coefficient)
            end

            assert.error(function()
               local _ = t * v
            end)
         end)

         it("div", function()
            local quot = t / 2.0
            assert.True(kiwi.is_term(quot))
            assert.equal(v, quot.var)
            assert.equal(5.0, quot.coefficient)

            assert.error(function()
               local _ = v / v2
            end)
         end)

         it("add", function()
            for _, sum in ipairs({ t + 2.0, 2 + t }) do
               assert.True(kiwi.is_expression(sum))
               assert.equal(2.0, sum.constant)

               local terms = sum:terms()
               assert.equal(1, #terms)
               assert.equal(10.0, terms[1].coefficient)
               assert.equal(v, terms[1].var)
            end

            local sum = t + v2
            assert.True(kiwi.is_expression(sum))
            assert.equal(0, sum.constant)
            local terms = sum:terms()
            assert.equal(2, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(10.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(1.0, terms[2].coefficient)

            sum = t + t2
            assert.True(kiwi.is_expression(sum))
            assert.equal(0, sum.constant)
            terms = sum:terms()
            assert.equal(2, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(10.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(1.0, terms[2].coefficient)

            local t3 = kiwi.Term(v2, 20)
            sum = t3 + sum
            assert.True(kiwi.is_expression(sum))
            assert.equal(0, sum.constant)
            terms = sum:terms()
            assert.equal(3, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(10.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(1.0, terms[2].coefficient)
            assert.equal(v2, terms[3].var)
            assert.equal(20.0, terms[3].coefficient)

            assert.error(function()
               local _ = t + "foo"
            end)
            assert.error(function()
               local _ = t + {}
            end)
         end)

         it("sub", function()
            local constants = { -2, 2 }
            for i, diff in ipairs({ t - 2.0, 2 - t }) do
               local constant = constants[i]
               assert.True(kiwi.is_expression(diff))
               assert.equal(constant, diff.constant)

               local terms = diff:terms()
               assert.equal(1, #terms)
               assert.equal(v, terms[1].var)
               assert.equal(constant < 0 and 10.0 or -10.0, terms[1].coefficient)
            end

            local diff = t - v2
            assert.True(kiwi.is_expression(diff))
            assert.equal(0, diff.constant)
            local terms = diff:terms()
            assert.equal(2, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(10.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(-1.0, terms[2].coefficient)

            diff = t - t2
            assert.True(kiwi.is_expression(diff))
            assert.equal(0, diff.constant)
            terms = diff:terms()
            assert.equal(2, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(10.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(-1.0, terms[2].coefficient)

            local t3 = kiwi.Term(v2, 20)
            diff = t3 - diff
            assert.True(kiwi.is_expression(diff))
            assert.equal(0, diff.constant)
            terms = diff:terms()
            assert.equal(3, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(-10.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(1.0, terms[2].coefficient)
            assert.equal(v2, terms[3].var)
            assert.equal(20.0, terms[3].coefficient)

            assert.error(function()
               local _ = t - "foo"
            end)
            assert.error(function()
               local _ = t - {}
            end)
         end)

         it("constraint term op expr", function()
            local ops = { "LE", "EQ", "GE" }
            for i, meth in ipairs({ "le", "eq", "ge" }) do
               local c = t[meth](t, v2 + 1)
               assert.True(kiwi.is_constraint(c))

               local e = c:expression()
               local terms = e:terms()
               assert.equal(2, #terms)

               -- order can be randomized due to use of map
               if terms[1].var ~= v then
                  terms[1], terms[2] = terms[2], terms[1]
               end
               assert.equal(v, terms[1].var)
               assert.equal(10.0, terms[1].coefficient)
               assert.equal(v2, terms[2].var)
               assert.equal(-1.0, terms[2].coefficient)

               assert.equal(-1, e.constant)
               assert.equal(ops[i], c:op())
               assert.equal(kiwi.strength.REQUIRED, c:strength())
            end
         end)

         it("constraint term op term", function()
            for i, meth in ipairs({ "le", "eq", "ge" }) do
               local c = t[meth](t, t2)
               assert.True(kiwi.is_constraint(c))

               local e = c:expression()
               local terms = e:terms()
               assert.equal(2, #terms)

               -- order can be randomized due to use of map
               if terms[1].var ~= v then
                  terms[1], terms[2] = terms[2], terms[1]
               end
               assert.equal(v, terms[1].var)
               assert.equal(10.0, terms[1].coefficient)
               assert.equal(v2, terms[2].var)
               assert.equal(-1.0, terms[2].coefficient)

               assert.equal(0, e.constant)
            end
         end)
      end)
   end)
end)
