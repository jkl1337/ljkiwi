expose("module", function()
   require("kiwi")
end)

describe("Var", function()
   local kiwi = require("kiwi")

   it("construction", function()
      assert.True(kiwi.is_var(kiwi.Var()))
      assert.False(kiwi.is_var(kiwi.Constraint()))

      assert.error(function()
         kiwi.Var({})
      end)
   end)

   describe("method", function()
      local v

      before_each(function()
         v = kiwi.Var("goo")
      end)

      it("has settable name", function()
         assert.equal("goo", v:name())
         v:set_name("Δ")
         assert.equal("Δ", v:name())
         v:set_name("酒酒酒酒酒酒")
         assert.equal("酒酒酒酒酒酒", v:name())

         assert.error(function()
            v:set_name({})
         end)
      end)

      it("has a initial value of 0.0", function()
         assert.equal(0.0, v:value())
      end)

      it("has a settable value", function()
         v:set(47.0)
         assert.equal(47.0, v:value())
      end)

      it("neg", function()
         local neg = -v --[[@as kiwi.Term]]
         assert.True(kiwi.is_term(neg))
         assert.equal(v, neg.var)
         assert.equal(-1.0, neg.coefficient)
      end)

      describe("bin op", function()
         local v2
         before_each(function()
            v2 = kiwi.Var("foo")
         end)

         it("mul", function()
            for _, prod in ipairs({ v * 2.0, 2 * v }) do
               assert.True(kiwi.is_term(prod))
               assert.equal(v, prod.var)
               assert.equal(2.0, prod.coefficient)
            end

            assert.error(function()
               local _ = v * v2
            end)
         end)

         it("div", function()
            local quot = v / 2.0
            assert.True(kiwi.is_term(quot))
            assert.equal(v, quot.var)
            assert.equal(0.5, quot.coefficient)

            assert.error(function()
               local _ = v / v2
            end)
         end)

         it("add", function()
            for _, sum in ipairs({ v + 2.0, 2 + v }) do
               assert.True(kiwi.is_expression(sum))
               assert.equal(2.0, sum.constant)

               local terms = sum:terms()
               assert.equal(1, #terms)
               assert.equal(1.0, terms[1].coefficient)
               assert.equal(v, terms[1].var)
            end

            local sum = v + v2
            assert.True(kiwi.is_expression(sum))
            assert.equal(0, sum.constant)
            local terms = sum:terms()
            assert.equal(2, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(1.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(1.0, terms[2].coefficient)

            assert.error(function()
               local _ = v + "foo"
            end)
            assert.error(function()
               local _ = v + {}
            end)
         end)

         it("sub", function()
            local constants = { -2, 2 }
            for i, diff in ipairs({ v - 2.0, 2 - v }) do
               local constant = constants[i]
               assert.True(kiwi.is_expression(diff))
               assert.equal(constant, diff.constant)

               local terms = diff:terms()
               assert.equal(1, #terms)
               assert.equal(v, terms[1].var)
               assert.equal(constant < 0 and 1 or -1, terms[1].coefficient)
            end

            local diff = v - v2
            assert.True(kiwi.is_expression(diff))
            assert.equal(0, diff.constant)
            local terms = diff:terms()
            assert.equal(2, #terms)
            assert.equal(v, terms[1].var)
            assert.equal(1.0, terms[1].coefficient)
            assert.equal(v2, terms[2].var)
            assert.equal(-1.0, terms[2].coefficient)

            -- TODO: terms and expressions

            assert.error(function()
               local _ = v - "foo"
            end)
            assert.error(function()
               local _ = v - {}
            end)
         end)

         it("constraint var op expr", function()
            local ops = { "LE", "EQ", "GE" }
            for i, meth in ipairs({ "le", "eq", "ge" }) do
               local c = v[meth](v, v2 + 1)
               assert.True(kiwi.is_constraint(c))

               local e = c:expression()
               local t = e:terms()
               assert.equal(2, #t)

               -- order can be randomized due to use of map
               if t[1].var ~= v then
                  t[1], t[2] = t[2], t[1]
               end
               assert.equal(v, t[1].var)
               assert.equal(1.0, t[1].coefficient)
               assert.equal(v2, t[2].var)
               assert.equal(-1.0, t[2].coefficient)

               assert.equal(-1, e.constant)
               assert.equal(ops[i], c:op())
               assert.equal(kiwi.strength.REQUIRED, c:strength())
            end
         end)

         it("constraint var op var", function()
            for i, meth in ipairs({ "le", "eq", "ge" }) do
               local c = v[meth](v, v2)
               assert.True(kiwi.is_constraint(c))

               local e = c:expression()
               local t = e:terms()
               assert.equal(2, #t)

               -- order can be randomized due to use of map
               if t[1].var ~= v then
                  t[1], t[2] = t[2], t[1]
               end
               assert.equal(v, t[1].var)
               assert.equal(1.0, t[1].coefficient)
               assert.equal(v2, t[2].var)
               assert.equal(-1.0, t[2].coefficient)

               assert.equal(0, e.constant)
            end
         end)
      end)
   end)
end)
