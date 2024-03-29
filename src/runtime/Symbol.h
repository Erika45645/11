/*
 * Copyright (c) 2018-present Samsung Electronics Co., Ltd
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 */

#ifndef __EscargotSymbol__
#define __EscargotSymbol__

#include "runtime/PointerValue.h"

namespace Escargot {

class VMInstance;

class Symbol : public PointerValue {
public:
    explicit Symbol(Optional<String*> desc = nullptr)
        : m_typeTag(POINTER_VALUE_SYMBOL_TAG_IN_DATA)
        , m_description(desc)
    {
    }

    Optional<String*> description() const
    {
        return m_description;
    }

    String* symbolDescriptiveString() const;

    static Symbol* fromGlobalSymbolRegistry(VMInstance* vm, String* stringKey);

private:
    size_t m_typeTag;
    Optional<String*> m_description;
};
} // namespace Escargot

#endif
