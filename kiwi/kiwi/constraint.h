/*-----------------------------------------------------------------------------
| Copyright (c) 2013-2017, Nucleic Development Team.
|
| Distributed under the terms of the Modified BSD License.
|
| The full license is in the file LICENSE, distributed with this software.
|----------------------------------------------------------------------------*/
#pragma once
#include <cstdlib>
#include <map>
#include <vector>
#include "expression.h"
#include "shareddata.h"
#include "strength.h"
#include "term.h"
#include "variable.h"
#include "util.h"

namespace kiwi
{

enum RelationalOperator
{
    OP_LE,
    OP_GE,
    OP_EQ
};

class Constraint;
class ConstraintData : public SharedData
{
    static Expression reduce(const Expression &expr)
    {
        std::map<Variable, double> vars;
        for (const auto & term : expr.terms())
            vars[term.variable()] += term.coefficient();

        std::vector<Term> terms(vars.begin(), vars.end());
        return Expression(std::move(terms), expr.constant());
    }

public:
    ConstraintData(const Expression &expr,
                   RelationalOperator op,
                   double strength) : SharedData(),
                                      m_expression(reduce(expr)),
                                      m_strength(strength::clip(strength)),
                                      m_op(op) {}

    ConstraintData(const ConstraintData &other, double strength) : SharedData(),
                                                                   m_expression(other.m_expression),
                                                                   m_strength(strength::clip(strength)),
                                                                   m_op(other.m_op) {}

    ~ConstraintData() = default;

    const Expression &expression() const { return m_expression; }
    RelationalOperator op() const { return m_op; }
    double strength() const { return m_strength; }

    bool violated() const
    {
        switch (m_op)
        {
            case OP_EQ: return !impl::nearZero(m_expression.value());
            case OP_GE: return m_expression.value() < impl::EPSILON;
            case OP_LE: return m_expression.value() > impl::EPSILON;
        }
        std::abort();
    }

private:
    Expression m_expression;
    double m_strength;
    RelationalOperator m_op;

    ConstraintData(const ConstraintData &other) = delete;
    ConstraintData &operator=(const ConstraintData &other) = delete;
};

class Constraint
{
public:
    explicit Constraint(ConstraintData *p) : m_data(p) {}

    Constraint() = default;

    Constraint(const Expression &expr,
               RelationalOperator op,
               double strength = strength::required) : m_data(new ConstraintData(expr, op, strength)) {}

    Constraint(const Constraint &other, double strength) : m_data(new ConstraintData(*other.m_data, strength)) {}

    Constraint(const Constraint &) = default;

    Constraint(Constraint &&) noexcept = default;

    ~Constraint() = default;

    const Expression &expression() const { return m_data->expression(); }
    RelationalOperator op() const { return m_data->op(); }
    double strength() const { return m_data->strength(); }
    bool violated() const { return m_data->violated(); }

    bool operator!() const
    {
        return !m_data;
    }

    Constraint& operator=(const Constraint &) = default;

    Constraint& operator=(Constraint &&) noexcept = default;

private:
    SharedDataPtr<ConstraintData> m_data;

public:

    friend bool operator<(const Constraint &lhs, const Constraint &rhs)
    {
        return lhs.m_data < rhs.m_data;
    }

    friend bool operator==(const Constraint &lhs, const Constraint &rhs)
    {
        return lhs.m_data == rhs.m_data;
    }

    friend bool operator!=(const Constraint &lhs, const Constraint &rhs)
    {
        return lhs.m_data != rhs.m_data;
    }
};

} // namespace kiwi
