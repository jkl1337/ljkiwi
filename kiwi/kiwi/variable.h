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

class SmallStr {
    static constexpr const std::size_t INLINE_LEN = 15;
public:
    size_t len_;
    union {
        char *ptr_;
        char inline_[INLINE_LEN + 1];
    } d_;

    SmallStr() : len_(0) { d_.inline_[0] = 0; }
    explicit SmallStr(const char *str) {
        if (!str) {
            len_ = 0;
            d_.inline_[0] = 0;
            return;
        }
        len_ = std::strlen(str);
        char *dest;
        if (len_ > INLINE_LEN) {
            d_.ptr_ = new char[len_ + 1];
            dest = d_.ptr_;
        } else {
            dest = d_.inline_;
        }
        std::memcpy(dest, str, len_ + 1);
    }

    ~SmallStr() { if (len_ > INLINE_LEN) delete[] d_.ptr_; }

    SmallStr(const SmallStr &other) : len_(other.len_) {
        if (len_ > INLINE_LEN) {
            d_.ptr_ = new char[len_ + 1];
            std::memcpy(d_.ptr_, other.d_.ptr_, len_ + 1);
        } else {
            std::memcpy(d_.inline_, other.d_.inline_, len_ + 1);
        }
    }

    SmallStr(SmallStr &&other) noexcept : len_(other.len_), d_(other.d_) {
        other.len_ = 0;
    }

    SmallStr& operator=(const SmallStr &) = delete;

    SmallStr& operator=(SmallStr &&other) noexcept {
        std::swap(len_, other.len_);
        std::swap(d_, other.d_);
        return *this;
    }

    const char *c_str() const { return len_ > INLINE_LEN ? d_.ptr_ : d_.inline_; }
    explicit operator bool() const { return len_ != 0; }
};

class VariableData
{
public:
    std::size_t ref_count_;
    double value_;
    SmallStr name_;

    const char* name() const { return name_.c_str(); }
    void setName(const char *name)
    {
        if (name_.c_str() != name) {
            name_ = SmallStr(name);
        }
    }

    static VariableData* alloc(const char *name = nullptr) {
        return new VariableData{1, 0.0, SmallStr(name)};
    }

    void free() { delete this; }

    VariableData* retain() { ref_count_++; return this; }
    void release() {
        if (--ref_count_ == 0)
            delete this;
    }

    ~VariableData() = default;
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
