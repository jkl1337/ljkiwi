/*-----------------------------------------------------------------------------
| Copyright (c) 2013-2017, Nucleic Development Team.
|
| Distributed under the terms of the Modified BSD License.
|
| The full license is in the file LICENSE, distributed with this software.
|----------------------------------------------------------------------------*/
#pragma once
#include <cstring>
#include <string>

namespace kiwi
{

class VariableData
{
public:
    std::size_t ref_count_;
    double value_;
    char *name_;

    const char* name() const { return name_ ? name_ : ""; }
    void setName(const char *name)
    {
        if (name_ != name) {
            if (name_) delete[] name_;
            auto len = name ? std::strlen(name) : 0;
            if (len) {
                name_ = new char[len + 1];
                std::memcpy(name_, name, len + 1);
            } else {
                name_ = nullptr;
            }
        }
    }

    static VariableData* alloc(const char *name = nullptr) {
        VariableData *data = new VariableData{1, 0.0};
        try {
            data->setName(name);
        } catch (...) {
            delete data;
            throw;
        }
        return data;
    }

    void free() { delete this; }

    VariableData* retain() { ref_count_++; return this; }
    void release() {
        if (--ref_count_ == 0)
            delete this;
    }

    ~VariableData() { if (name_) delete[] name_; }
    VariableData(VariableData&) = delete;
    VariableData(VariableData&&) = delete;
    VariableData& operator=(VariableData&) = delete;
    VariableData& operator=(VariableData&&) = delete;
};

static_assert(std::is_standard_layout<VariableData>::value == true, "VariableData must be standard layout");

class Variable
{
public:
    explicit Variable(VariableData *p) : m_data(p->retain()) {}
    VariableData *ptr() { return m_data; }

    Variable() : m_data(VariableData::alloc()) {}

    Variable(const char *name) : m_data(VariableData::alloc(name)) {}
    Variable(std::string name) : Variable(name.c_str()) {}

    Variable(const Variable& other) : m_data(other.m_data->retain()) {}
    Variable(Variable&& other) noexcept {
        m_data = other.m_data;
        other.m_data = nullptr;
    }

    ~Variable() {
        if (m_data)
            m_data->release();
    }

    const char *name() const { return m_data->name(); }
    void setName(const char *name) { m_data->setName(name); }
    void setName(const std::string &name) { m_data->setName(name.c_str()); }

    double value() const { return m_data->value_; }
    void setValue(double value) { m_data->value_ = value; }

    // operator== is used for symbolics
    bool equals(const Variable &other) const
    {
        return m_data == other.m_data;
    }

    Variable& operator=(const Variable& other) {
        if (m_data != other.m_data) {
            m_data->release();
            m_data = other.m_data->retain();
        }
        return *this;
    }

    Variable& operator=(Variable&& other) noexcept {
        std::swap(m_data, other.m_data);
        return *this;
    }

private:
    VariableData *m_data;

    friend bool operator<(const Variable &lhs, const Variable &rhs)
    {
        return lhs.m_data < rhs.m_data;
    }
};

} // namespace kiwi
