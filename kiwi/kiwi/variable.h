/*-----------------------------------------------------------------------------
| Copyright (c) 2013-2017, Nucleic Development Team.
|
| Distributed under the terms of the Modified BSD License.
|
| The full license is in the file LICENSE, distributed with this software.
|----------------------------------------------------------------------------*/
#pragma once
#include <memory>
#include <string>
#include "shareddata.h"

namespace kiwi
{

class VariableData : public SharedData
{
public:
    VariableData(std::string name) : SharedData(),
                                         m_name(std::move(name)),
                                         m_value(0.0) {}

    VariableData(const char *name) : SharedData(),
                                         m_name(name),
                                         m_value(0.0) {}

    ~VariableData() = default;

    const std::string &name() const { return m_name; }
    void setName(const char *name) { m_name = name; }
    void setName(const std::string &name) { m_name = name; }

    double value() const { return m_value; }
    void setValue(double value) { m_value = value; }

private:
    std::string m_name;
    double m_value;

    VariableData(const VariableData &other) = delete;
    VariableData &operator=(const VariableData &other) = delete;
};

class Variable
{
public:
    explicit Variable(VariableData *p) : m_data(p) {}
    VariableData *ptr() { return m_data; }

    Variable() : m_data(new VariableData("")) {}

    Variable(std::string name) : m_data(new VariableData(std::move(name))) {}
    Variable(const char *name) : m_data(new VariableData(name)) {}
    Variable(const Variable&) = default;
    Variable(Variable&&) noexcept = default;

    ~Variable() = default;

    const std::string &name() const { return m_data->name(); }
    void setName(const char *name) { m_data->setName(name); }
    void setName(const std::string &name) { m_data->setName(name); }

    double value() const { return m_data->value(); }
    void setValue(double value) { m_data->setValue(value); }

    // operator== is used for symbolics
    bool equals(const Variable &other)
    {
        return m_data == other.m_data;
    }

    Variable& operator=(const Variable&) = default;

    Variable& operator=(Variable&&) noexcept = default;

private:
    SharedDataPtr<VariableData> m_data;

    friend bool operator<(const Variable &lhs, const Variable &rhs)
    {
        return lhs.m_data < rhs.m_data;
    }
};

} // namespace kiwi
