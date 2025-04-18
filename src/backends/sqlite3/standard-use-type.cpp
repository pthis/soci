//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SQLITE3_SOURCE
#include "soci/soci-platform.h"
#include "soci/sqlite3/soci-sqlite3.h"
#include "soci/rowid.h"
#include "soci/blob.h"
#include "soci-dtocstr.h"
#include "soci-exchange-cast.h"
#include "soci-mktime.h"
// std
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <sstream>
#include <string>

using namespace soci;
using namespace soci::details;

sqlite3_standard_use_type_backend::sqlite3_standard_use_type_backend(
        sqlite3_statement_backend &st)
    : statement_(st), data_(NULL), type_(x_int32), position_(-1)
{
}


void sqlite3_standard_use_type_backend::bind_by_pos(int& position, void* data,
    exchange_type type, bool /*readOnly*/)
{
    if (statement_.boundByName_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    data_ = data;
    type_ = type;
    position_ = position++;

    statement_.boundByPos_ = true;
}

void sqlite3_standard_use_type_backend::bind_by_name(std::string const& name,
    void* data, exchange_type type, bool /*readOnly*/)
{
    if (statement_.boundByPos_)
    {
        throw soci_error(
         "Binding for use elements must be either by position or by name.");
    }

    data_ = data;
    type_ = type;
    name_ = ":" + name;

    statement_.reset_if_needed();
    position_ = sqlite3_bind_parameter_index(statement_.stmt_, name_.c_str());

    if (0 == position_)
    {
        std::ostringstream ss;
        ss << "Cannot bind to (by name) " << name_;
        throw soci_error(ss.str());
    }
    statement_.boundByName_ = true;
}

void sqlite3_standard_use_type_backend::pre_use(indicator const * ind)
{
    statement_.useData_.resize(1);
    int const pos = position_ - 1;

    if (statement_.useData_[0].size() < static_cast<std::size_t>(position_))
    {
        sqlite3_column empty;
        empty.isNull_ = true;
        statement_.useData_[0].resize(position_, empty);
    }

    sqlite3_column &col = statement_.useData_[0][pos];

    if (ind != NULL && *ind == i_null)
    {
        col.isNull_ = true;
        return;
    }

    col.isNull_ = false;

    // allocate and fill the buffer with text-formatted client data
    switch (type_)
    {
        case x_char:
            col.type_ = dt_string;
            col.dataType_ = db_string;
            col.buffer_.constData_ = &exchange_type_cast<x_char>(data_);
            col.buffer_.size_ = 1;
            break;

        case x_stdstring:
        {
            const std::string &s = exchange_type_cast<x_stdstring>(data_);
            col.type_ = dt_string;
            col.dataType_ = db_string;
            col.buffer_.constData_ = s.c_str();
            col.buffer_.size_ = s.size();
            break;
        }

        case x_int8:
            col.type_ = dt_integer;
            col.dataType_ = db_int8;
            col.int8_ = exchange_type_cast<x_int8>(data_);
            break;

        case x_uint8:
            col.type_ = dt_integer;
            col.dataType_ = db_uint8;
            col.uint8_ = exchange_type_cast<x_uint8>(data_);
            break;

        case x_int16:
            col.type_ = dt_integer;
            col.dataType_ = db_int16;
            col.int16_ = exchange_type_cast<x_int16>(data_);
            break;

        case x_uint16:
            col.type_ = dt_integer;
            col.dataType_ = db_uint16;
            col.uint16_ = exchange_type_cast<x_uint16>(data_);
            break;

        case x_int32:
            col.type_ = dt_integer;
            col.dataType_ = db_int32;
            col.int32_ = exchange_type_cast<x_int32>(data_);
            break;

        case x_uint32:
            col.type_ = dt_long_long;
            col.dataType_ = db_uint32;
            col.uint32_ = exchange_type_cast<x_uint32>(data_);
            break;

        case x_int64:
            col.type_ = dt_long_long;
            col.dataType_ = db_int64;
            col.int64_ = exchange_type_cast<x_int64>(data_);
            break;

        case x_uint64:
            col.type_ = dt_unsigned_long_long;
            col.dataType_ = db_uint64;
            col.uint64_ = exchange_type_cast<x_uint64>(data_);
            break;

        case x_double:
            col.type_ = dt_double;
            col.dataType_ = db_double;
            col.double_ = exchange_type_cast<x_double>(data_);
            break;

        case x_stdtm:
        {
            col.type_ = dt_date;
            col.dataType_ = db_date;
            static const size_t bufSize = 20;
            std::tm &t = exchange_type_cast<x_stdtm>(data_);

            col.buffer_.data_ = new char[bufSize];
            col.buffer_.size_ = format_std_tm(t, col.buffer_.data_, bufSize);
            break;
        }

        case x_rowid:
        {
            col.type_ = dt_long_long;
            col.dataType_ = db_int64;
            // RowID is internally identical to unsigned long
            rowid *rid = static_cast<rowid *>(data_);
            sqlite3_rowid_backend *rbe = static_cast<sqlite3_rowid_backend *>(rid->get_backend());

            col.int64_ = rbe->value_;
            break;
        }

        case x_blob:
        {
            col.type_ = dt_blob;
            col.dataType_ = db_blob;
            blob *b = static_cast<blob *>(data_);
            sqlite3_blob_backend *bbe = static_cast<sqlite3_blob_backend *>(b->get_backend());

            // In case the internal buffer has not been initialized yet, get_buffer() will return nullptr. In this case,
            // we want to make sure to insert an empty BLOB into the DB. However, when passing a nullptr to the
            // sqlite3_bind_blob routine (in statement.cpp), it has the same effect as sqlite3_bind_null and thus
            // is inserting NULL instead.
            // Therefore, we want to make sure that the buffer is definitely initialized (though it can still be empty).
            bbe->ensure_buffer_initialized();

            static_assert(sizeof(decltype(*bbe->get_buffer())) == 1, "Expected byte-like type");
            col.buffer_.constData_ = reinterpret_cast<const char *>(bbe->get_buffer());
            col.buffer_.size_ = bbe->get_len();
            break;
        }

        case x_xmltype:
        {
            const soci::xml_type &xml = exchange_type_cast<x_xmltype>(data_);
            col.type_ = dt_string;
            col.dataType_ = db_string;
            col.buffer_.constData_ = xml.value.c_str();
            col.buffer_.size_ = xml.value.size();
            break;
        }

        default:
            throw soci_error("Use element used with non-supported type.");
    }
}

void sqlite3_standard_use_type_backend::post_use(
    bool /* gotData */, indicator * /* ind */)
{
    // TODO: Is it possible to have the bound element being overwritten
    // by the database?
    // If not, then nothing to do here, please remove this comment.
    // If yes, then use the value of the readOnly parameter:
    // - true:  the given object should not be modified and the backend
    //          should detect if the modification was performed on the
    //          isolated buffer and throw an exception if the buffer was modified
    //          (this indicates logic error, because the user used const object
    //          and executed a query that attempted to modified it)
    // - false: the modification should be propagated to the given object.
    // ...

    // clean up the working buffer, it might be allocated anew in
    // the next run of preUse
    clean_up();
}

void sqlite3_standard_use_type_backend::clean_up()
{
    if (type_ != x_stdtm)
        return;

    sqlite3_column &col = statement_.useData_[0][position_ - 1];

    if (col.isNull_ || !col.buffer_.data_)
        return;

    delete[] col.buffer_.data_;
    col.buffer_.data_ = NULL;
}
