/*-----------------------------------------------------------------------------
| Copyright (c) 2013-2017, Nucleic Development Team.
|
| Distributed under the terms of the Modified BSD License.
|
| The full license is in the file LICENSE, distributed with this software.
|----------------------------------------------------------------------------*/
#pragma once

namespace kiwi
{

namespace impl
{

constexpr const double EPSILON = 1.0e-8;

inline bool nearZero(double value)
{
    return value < 0.0 ? -value < EPSILON : value < EPSILON;
}

} // namespace impl

} // namespace kiwi
