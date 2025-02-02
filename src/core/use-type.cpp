//
// Copyright (C) 2004-2016 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "soci/soci-platform.h"
#include "soci/use-type.h"
#include "soci/statement.h"
#include "soci/soci-unicode.h"
#include "soci-exchange-cast.h"
#include "soci-mktime.h"

#include <cstdio>

using namespace soci;
using namespace soci::details;

standard_use_type::~standard_use_type()
{
    delete backEnd_;
}

void standard_use_type::bind(statement_impl & st, int & position)
{
    if (backEnd_ == NULL)
    {
        backEnd_ = st.make_use_type_backend();
    }

    if (name_.empty())
    {
        backEnd_->bind_by_pos(position, data_, type_, readOnly_);
    }
    else
    {
        backEnd_->bind_by_name(name_, data_, type_, readOnly_);
    }
}

void standard_use_type::dump_value(std::ostream& os) const
{
    if (ind_ && *ind_ == i_null)
    {
        os << "NULL";
        return;
    }

    switch (type_)
    {
        case x_char:
            os << "'" << exchange_type_cast<x_char>(data_) << "'";
            return;

        case x_stdstring:
            // TODO: Escape quotes?
            os << "\"" << exchange_type_cast<x_stdstring>(data_) << "\"";
            return;

        case x_stdwstring:
            os << "\"" << wide_to_utf8(exchange_type_cast<x_stdwstring>(data_)) << "\"";
            return;

        case x_int8:
            os << exchange_type_cast<x_int8>(data_);
            return;

        case x_uint8:
            os << exchange_type_cast<x_uint8>(data_);
            return;

        case x_int16:
            os << exchange_type_cast<x_int16>(data_);
            return;

        case x_uint16:
            os << exchange_type_cast<x_uint16>(data_);
            return;

        case x_int32:
            os << exchange_type_cast<x_int32>(data_);
            return;

        case x_uint32:
            os << exchange_type_cast<x_uint32>(data_);
            return;

        case x_int64:
            os << exchange_type_cast<x_int64>(data_);
            return;

        case x_uint64:
            os << exchange_type_cast<x_uint64>(data_);
            return;

        case x_double:
            os << exchange_type_cast<x_double>(data_);
            return;

        case x_stdtm:
            {
                std::tm const& t = exchange_type_cast<x_stdtm>(data_);

                char buf[80];
                format_std_tm(t, buf, sizeof(buf));

                os << buf;
            }
            return;

        case x_statement:
            os << "<statement>";
            return;

        case x_rowid:
            os << "<rowid>";
            return;

        case x_blob:
            os << "<blob>";
            return;

        case x_xmltype:
            os << "<xml>";
            return;

        case x_longstring:
            os << "<long string>";
            return;
    }

    // This is normally unreachable, but avoid throwing from here as we're
    // typically called from an exception handler.
    os << "<unknown>";
}

void standard_use_type::pre_exec(int num)
{
    backEnd_->pre_exec(num);
}

void standard_use_type::pre_use()
{
    // Handle IN direction of parameters of SQL statements and procedures
    convert_to_base();
    backEnd_->pre_use(ind_);
}

void standard_use_type::post_use(bool gotData)
{
    // Handle OUT direction of IN/OUT parameters of stored procedures
    backEnd_->post_use(gotData, ind_);
    convert_from_base();

    // IMPORTANT:
    // This treatment of input ("use") parameter as output data sink may be
    // confusing, but it is necessary to store OUT data back in the same
    // object as IN, of IN/OUT parameter.
    // As there is no symmetry for IN/OUT in SQL and there are no OUT/IN
    // we do not perform convert_to_base() for output ("into") parameter.
    // See conversion_use_type<T>::convert_from_base() for more details.
}

void standard_use_type::clean_up()
{
    if (backEnd_ != NULL)
    {
        backEnd_->clean_up();
    }
}

vector_use_type::~vector_use_type()
{
    delete backEnd_;
}

void vector_use_type::bind(statement_impl & st, int & position)
{
    if (backEnd_ == NULL)
    {
        backEnd_ = st.make_vector_use_type_backend();
    }

    if (name_.empty())
    {
        if (end_ != NULL)
        {
            backEnd_->bind_by_pos_bulk(position, data_, type_, begin_, end_);
        }
        else
        {
            backEnd_->bind_by_pos(position, data_, type_);
        }
    }
    else
    {
        if (end_ != NULL)
        {
            backEnd_->bind_by_name_bulk(name_, data_, type_, begin_, end_);
        }
        else
        {
            backEnd_->bind_by_name(name_, data_, type_);
        }
    }
}

void vector_use_type::dump_value(std::ostream& os) const
{
    // TODO: Provide more information.
    os << "<vector>";
}

void vector_use_type::pre_exec(int num)
{
    backEnd_->pre_exec(num);
}

void vector_use_type::pre_use()
{
    convert_to_base();

    backEnd_->pre_use(ind_ ? &ind_->at(0) : NULL);
}

std::size_t vector_use_type::size() const
{
    return backEnd_->size();
}

void vector_use_type::clean_up()
{
    if (backEnd_ != NULL)
    {
        backEnd_->clean_up();
    }
}
