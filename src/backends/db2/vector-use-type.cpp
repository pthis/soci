//
// Copyright (C) 2011-2013 Denis Chapligin
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_DB2_SOURCE
#include "soci/soci-platform.h"
#include "soci/db2/soci-db2.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

using namespace soci;
using namespace soci::details;

void db2_vector_use_type_backend::prepare_indicators(std::size_t size)
{
    if (size == 0)
    {
         throw soci_error("Vectors of size 0 are not allowed.");
    }

    indVec.resize(size);
}

void *db2_vector_use_type_backend::prepare_for_bind(SQLUINTEGER &size,
    SQLSMALLINT &sqlType, SQLSMALLINT &cType)
{
    void* sqlData = NULL;
    switch (type)
    {    // simple cases
    case x_int8:
        {
            sqlType = SQL_SMALLINT;
            cType = SQL_C_STINYINT;
            size = sizeof(int8_t);
            std::vector<int8_t> *vp = static_cast<std::vector<int8_t> *>(data);
            std::vector<int8_t> &v(*vp);
            prepare_indicators(v.size());
            sqlData = &v[0];
        }
        break;
    case x_uint8:
        {
            sqlType = SQL_SMALLINT;
            cType = SQL_C_UTINYINT;
            size = sizeof(uint8_t);
            std::vector<uint8_t> *vp = static_cast<std::vector<uint8_t> *>(data);
            std::vector<uint8_t> &v(*vp);
            prepare_indicators(v.size());
            sqlData = &v[0];
        }
        break;
    case x_int16:
        {
            sqlType = SQL_SMALLINT;
            cType = SQL_C_SSHORT;
            size = sizeof(int16_t);
            std::vector<int16_t> *vp = static_cast<std::vector<int16_t> *>(data);
            std::vector<int16_t> &v(*vp);
            prepare_indicators(v.size());
            sqlData = &v[0];
        }
        break;
    case x_uint16:
        {
            sqlType = SQL_SMALLINT;
            cType = SQL_C_USHORT;
            size = sizeof(uint16_t);
            std::vector<uint16_t> *vp = static_cast<std::vector<uint16_t> *>(data);
            std::vector<uint16_t> &v(*vp);
            prepare_indicators(v.size());
            sqlData = &v[0];
        }
        break;
    case x_int32:
        {
            sqlType = SQL_INTEGER;
            cType = SQL_C_SLONG;
            size = sizeof(int32_t);
            std::vector<int32_t> *vp = static_cast<std::vector<int32_t> *>(data);
            std::vector<int32_t> &v(*vp);
            prepare_indicators(v.size());
            sqlData = &v[0];
        }
        break;
    case x_uint32:
        {
            sqlType = SQL_INTEGER;
            cType = SQL_C_ULONG;
            size = sizeof(uint32_t);
            std::vector<uint32_t> *vp = static_cast<std::vector<uint32_t> *>(data);
            std::vector<uint32_t> &v(*vp);
            prepare_indicators(v.size());
            sqlData = &v[0];
        }
        break;
    case x_int64:
        {
            sqlType = SQL_BIGINT;
            cType = SQL_C_SBIGINT;
            size = sizeof(int64_t);
            std::vector<int64_t> *vp
                 = static_cast<std::vector<int64_t> *>(data);
            std::vector<int64_t> &v(*vp);
            prepare_indicators(v.size());
            sqlData = &v[0];
        }
        break;
    case x_uint64:
        {
            sqlType = SQL_BIGINT;
            cType = SQL_C_UBIGINT;
            size = sizeof(uint64_t);
            std::vector<uint64_t> *vp
                 = static_cast<std::vector<uint64_t> *>(data);
            std::vector<uint64_t> &v(*vp);
            prepare_indicators(v.size());
            sqlData = &v[0];
        }
        break;
    case x_double:
        {
            sqlType = SQL_DOUBLE;
            cType = SQL_C_DOUBLE;
            size = sizeof(double);
            std::vector<double> *vp = static_cast<std::vector<double> *>(data);
            std::vector<double> &v(*vp);
            prepare_indicators(v.size());
            sqlData = &v[0];
        }
        break;

    // cases that require adjustments and buffer management
    case x_char:
        {
            std::vector<char> *vp
                = static_cast<std::vector<char> *>(data);
            std::size_t const vsize = vp->size();

            prepare_indicators(vsize);

            size = sizeof(char) * 2;
            buf = new char[size * vsize];

            char *pos = buf;

            for (std::size_t i = 0; i != vsize; ++i)
            {
                *pos++ = (*vp)[i];
                *pos++ = 0;
            }

            sqlType = SQL_CHAR;
            cType = SQL_C_CHAR;
            sqlData = buf;
        }
        break;
    case x_stdstring:
        {
            sqlType = SQL_CHAR;
            cType = SQL_C_CHAR;

            std::vector<std::string> *vp
                = static_cast<std::vector<std::string> *>(data);
            std::vector<std::string> &v(*vp);

            std::size_t maxSize = 0;
            std::size_t const vecSize = v.size();
            prepare_indicators(vecSize);
            for (std::size_t i = 0; i != vecSize; ++i)
            {
                std::size_t sz = v[i].length();
                indVec[i] = static_cast<long>(sz);
                maxSize = sz > maxSize ? sz : maxSize;
            }

            maxSize++; // For terminating nul.

            buf = new char[maxSize * vecSize];
            memset(buf, 0, maxSize * vecSize);

            char *pos = buf;
            for (std::size_t i = 0; i != vecSize; ++i)
            {
                memcpy(pos, v[i].c_str(), v[i].length());
                pos += maxSize;
            }

            sqlData = buf;
            size = static_cast<SQLINTEGER>(maxSize);
        }
        break;
    case x_stdtm:
        {
            std::vector<std::tm> *vp
                = static_cast<std::vector<std::tm> *>(data);

            prepare_indicators(vp->size());

            buf = new char[sizeof(TIMESTAMP_STRUCT) * vp->size()];

            sqlType = SQL_TYPE_TIMESTAMP;
            cType = SQL_C_TYPE_TIMESTAMP;
            sqlData = buf;
            size = 19; // This number is not the size in bytes, but the number
                      // of characters in the date if it was written out
                      // yyyy-mm-dd hh:mm:ss
        }
        break;

    case x_statement: break; // not supported
    case x_rowid:     break; // not supported
    case x_blob:      break; // not supported
    case x_xmltype:   break; // not supported
    case x_longstring:break; // not supported
    }

    colSize = size;

    return sqlData;
}

void db2_vector_use_type_backend::bind_by_pos(int &position,
        void *data, exchange_type type)
{
    if (statement_.use_binding_method_ == details::db2::BOUND_BY_NAME)
    {
        throw soci_error("Binding for use elements must be either by position or by name.");
    }
    statement_.use_binding_method_ = details::db2::BOUND_BY_POSITION;

    this->data = data;
    this->type = type;
    this->position = position++;
}

void db2_vector_use_type_backend::bind_by_name(
    std::string const &name, void *data, exchange_type type)
{
    int position = -1;
    int count = 1;

    if (statement_.use_binding_method_ == details::db2::BOUND_BY_POSITION)
    {
        throw soci_error("Binding for use elements must be either by position or by name.");
    }
    statement_.use_binding_method_ = details::db2::BOUND_BY_NAME;

    for (auto const& s : statement_.names_)
    {
        if (s == name)
        {
            position = count;
            break;
        }
        count++;
    }

    if (position == -1)
    {
        std::ostringstream ss;
        ss << "Unable to find name '" << name << "' to bind to";
        throw soci_error(ss.str());
    }

    this->position = position;
    this->data = data;
    this->type = type;
}

void db2_vector_use_type_backend::pre_use(indicator const *ind)
{
    SQLSMALLINT sqlType;
    SQLSMALLINT cType;
    SQLUINTEGER size;

    void* const sqlData = prepare_for_bind(size, sqlType, cType);

    // first deal with data
    if (type == x_stdtm)
    {
        std::vector<std::tm> *vp
             = static_cast<std::vector<std::tm> *>(data);

        std::vector<std::tm> &v(*vp);

        char *pos = buf;
        std::size_t const vsize = v.size();
        for (std::size_t i = 0; i != vsize; ++i)
        {
            std::tm t = v[i];
            TIMESTAMP_STRUCT * ts = reinterpret_cast<TIMESTAMP_STRUCT*>(pos);

            ts->year = static_cast<SQLSMALLINT>(t.tm_year + 1900);
            ts->month = static_cast<SQLUSMALLINT>(t.tm_mon + 1);
            ts->day = static_cast<SQLUSMALLINT>(t.tm_mday);
            ts->hour = static_cast<SQLUSMALLINT>(t.tm_hour);
            ts->minute = static_cast<SQLUSMALLINT>(t.tm_min);
            ts->second = static_cast<SQLUSMALLINT>(t.tm_sec);
            ts->fraction = 0;
            pos += sizeof(TIMESTAMP_STRUCT);
        }
    }

    // then handle indicators
    if (ind != NULL)
    {
        std::size_t const vsize = this->size();
        for (std::size_t i = 0; i != vsize; ++i, ++ind)
        {
            if (*ind == i_null)
            {
                indVec[i] = SQL_NULL_DATA; // null
            }
            else
            {
            // for strings we have already set the values
            if (type != x_stdstring)
                {
                    indVec[i] = SQL_NTS;  // value is OK
                }
            }
        }
    }
    else
    {
        // no indicators - treat all fields as OK
        std::size_t const vsize = this->size();
        for (std::size_t i = 0; i != vsize; ++i)
        {
            // for strings we have already set the values
            if (type != x_stdstring)
            {
                indVec[i] = SQL_NTS;  // value is OK
            }
        }
    }

    SQLINTEGER arraySize = (SQLINTEGER)indVec.size();
    SQLSetStmtAttr(statement_.hStmt, SQL_ATTR_PARAMSET_SIZE, db2::int_as_ptr(arraySize), 0);

    SQLRETURN cliRC = SQLBindParameter(statement_.hStmt, static_cast<SQLUSMALLINT>(position),
                                    SQL_PARAM_INPUT, cType, sqlType, size, 0,
                                    static_cast<SQLPOINTER>(sqlData), size, &indVec[0]);

    if ( cliRC != SQL_SUCCESS )
    {
        throw db2_soci_error("Error while binding value to column", cliRC);
    }
}

std::size_t db2_vector_use_type_backend::size() const
{
    std::size_t sz SOCI_DUMMY_INIT(0);
    switch (type)
    {
    // simple cases
    case x_char:
        {
            std::vector<char> *vp = static_cast<std::vector<char> *>(data);
            sz = vp->size();
        }
        break;
    case x_int8:
        {
            std::vector<int8_t> *vp = static_cast<std::vector<int8_t> *>(data);
            sz = vp->size();
        }
        break;
    case x_uint8:
        {
            std::vector<uint8_t> *vp = static_cast<std::vector<uint8_t> *>(data);
            sz = vp->size();
        }
        break;
    case x_int16:
        {
            std::vector<int16_t> *vp = static_cast<std::vector<int16_t> *>(data);
            sz = vp->size();
        }
        break;
    case x_uint16:
        {
            std::vector<uint16_t> *vp = static_cast<std::vector<uint16_t> *>(data);
            sz = vp->size();
        }
        break;
    case x_int32:
        {
            std::vector<int32_t> *vp = static_cast<std::vector<int32_t> *>(data);
            sz = vp->size();
        }
        break;
    case x_uint32:
        {
            std::vector<uint32_t> *vp = static_cast<std::vector<uint32_t> *>(data);
            sz = vp->size();
        }
        break;
    case x_int64:
        {
            std::vector<int64_t> *vp
                = static_cast<std::vector<int64_t> *>(data);
            sz = vp->size();
        }
        break;
    case x_uint64:
        {
            std::vector<uint64_t> *vp
                = static_cast<std::vector<uint64_t> *>(data);
            sz = vp->size();
        }
        break;
    case x_double:
        {
            std::vector<double> *vp
                = static_cast<std::vector<double> *>(data);
            sz = vp->size();
        }
        break;
    case x_stdstring:
        {
            std::vector<std::string> *vp
                = static_cast<std::vector<std::string> *>(data);
            sz = vp->size();
        }
        break;
    case x_stdtm:
        {
            std::vector<std::tm> *vp
                = static_cast<std::vector<std::tm> *>(data);
            sz = vp->size();
        }
        break;

    case x_statement: break; // not supported
    case x_rowid:     break; // not supported
    case x_blob:      break; // not supported
    case x_xmltype:   break; // not supported
    case x_longstring:break; // not supported
    }

    return sz;
}

void db2_vector_use_type_backend::clean_up()
{
    if (buf != NULL)
    {
        delete [] buf;
        buf = NULL;
    }
}
