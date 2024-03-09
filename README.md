ljkiwi - LuaJIT FFI and Lua C API kiwi (Cassowary derived) constraint solver.

[![CI](https://github.com/jkl1337/ljkiwi/actions/workflows/busted.yml/badge.svg)](https://github.com/jkl1337/ljkiwi/actions/workflows/busted.yml)
[![Coverage Status](https://coveralls.io/repos/github/jkl1337/ljkiwi/badge.svg?branch=master)](https://coveralls.io/github/jkl1337/ljkiwi?branch=master)
[![luarocks](https://img.shields.io/luarocks/v/jkl/kiwi)](https://luarocks.org/modules/jkl/kiwi)

# Introduction

Kiwi is a reasonably efficient C++ implementation of the Cassowary constraint solving algorithm. It is an implementation of the algorithm as described in the paper ["The Cassowary Linear Arithmetic Constraint Solving Algorithm"](http://www.cs.washington.edu/research/constraints/cassowary/techreports/cassowaryTR.pdf) by Greg J. Badros and Alan Borning. The Kiwi implementation is not based on the original C++ implementation, but is a ground-up reimplementation with performance 10x to 500x faster in typical use.
Cassowary constraint solving is a technique that is particularly well suited to user interface layout. It is the algorithm Apple uses for iOS and OS X Auto Layout.

[Casaurius](https://docs.rs/casuarius/latest/casuarius/) is an excellent and performant Rust implementation of Kiwi.

This rock can produce a LuaJIT FFI binding for both the C++ and Rust implementations. The Rust implementation is not currently built by default. It can be built
by setting the `FRUST` environment variable to any non-empty value. If the Rust implementation
is built it will take precedence over the C++ implementation. The C++ implementation can be
forced at runtime by setting the global `KIWI_CKIWI` prior to loading the `kiwi` package.

The C++ and Rust implementations provide a nearly semantically equivalent API. One difference
is that the Rust implementation tracks constraints by value, not identity, so adding an
equivalent constraint will raise an error while the C++ implementation does not notice this
mistake.

There is also a Lua C API binding with support for 5.1 through 5.4. Currently only the C++
implementation is wrapped by the Lua C API binding.

This package has no mandatory dependencies other than a supported C++14 compiler to compile the included Kiwi library and a small C wrapper.


## Example

```lua
local kiwi = require("kiwi")
local Var = kiwi.Var

local Button = setmetatable({}, {
   __call = function(_, identifier)
      return setmetatable({
         left = Var(identifier .. " left"),
         width = Var(identifier .. " width"),
      }, {
         __tostring = function(self)
            return "Button(" .. self.left:value() .. ", " .. self.width:value() .. ")"
         end,
      })
   end,
})

local b1 = Button("b1")
local b2 = Button("b2")

local left_edge = Var("left")
local right_edge = Var("width")

local STRONG = kiwi.Strength.STRONG

-- stylua: ignore start
local constraints = {
   left_edge    :eq(0.0),
   -- two buttons are the same width
   b1.width     :eq(b2.width),
   -- button1 starts 50 from the left margin
   b1.left      :eq(left_edge + 50),
   -- button2 ends 50 from the right margin
   right_edge   :eq(b2.left + b2.width + 50),
   -- button2 starts at least 100 from the end of button1. This is the "elastic" constraint
   b2.left      :ge(b1.left + b1.width + 100),
   -- button1 has a minimum width of 87
   b1.width     :ge(87),
   -- button1 has a preferred width of 87
   b1.width     :eq(87, STRONG),
   -- button2 has minimum width of 113
   b2.width     :ge(113),
   -- button2 has a preferred width of 113
   b2.width     :eq(113, STRONG),
}
-- stylua: ignore end

local solver = kiwi.Solver()

for _, c in ipairs(constraints) do
   solver:add_constraint(c)
end

solver:update_vars()

print(b1) -- Button(50, 113)
print(b2) -- Button(263, 113)
print(left_edge:value()) -- 0
print(right_edge:value()) -- 426

solver:add_edit_var(right_edge, STRONG)
solver:suggest_value(right_edge, 500)
solver:update_vars()
print(b1) -- Button(50, 113)
print(b2) -- Button(337, 113)
print(right_edge:value()) -- 500

```

In addition to the expression builder there is a convenience constraints submodule with: `pair_ratio`, `pair`, and `single` to allow efficient construction of the most common simple expression types for GUI layout.

## Documentation
The API is fully annotated and will work with lua-language-server. Documentation can also be generated with lua-language-server.
