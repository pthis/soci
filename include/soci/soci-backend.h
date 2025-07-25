//
// Copyright (C) 2004-2016 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_BACKEND_H_INCLUDED
#define SOCI_BACKEND_H_INCLUDED

#include "soci/soci-platform.h"
#include "soci/error.h"
// std
#include <cstddef>
#include <map>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstring>

namespace soci
{

// data types, as seen by the user
enum db_type
{
    db_string,
    db_wstring,
    db_int8,
    db_uint8,
    db_int16,
    db_uint16,
    db_int32,
    db_uint32,
    db_int64,
    db_uint64,
    db_double,
    db_date,
    db_blob,
    db_xml
};

// DEPRECATED. USE db_type INSTEAD.
enum data_type
{
    dt_string, dt_date, dt_double, dt_integer, dt_long_long, dt_unsigned_long_long,
    dt_blob, dt_xml
};

// the enum type for indicator variables
enum indicator { i_ok, i_null, i_truncated };

class session;
class failover_callback;

namespace details
{

// data types, as used to describe exchange format
enum exchange_type
{
    x_char,
    x_stdstring,
    x_stdwstring,
    x_int8,
    x_uint8,
    x_int16,
    x_uint16,
    x_int32,
    x_uint32,
    x_int64,
    x_uint64,
    x_double,
    x_stdtm,
    x_statement,
    x_rowid,
    x_blob,

    x_xmltype,
    x_longstring,

    // Deprecated synonyms.
    x_short = x_int16,
    x_integer = x_int32,
    x_long_long = x_int64,
    x_unsigned_long_long = x_uint64
};

// type of statement (used for optimizing statement preparation)
enum statement_type
{
    st_one_time_query,
    st_repeatable_query
};

// (lossless) conversion from the legacy data type enum
inline db_type to_db_type(data_type dt)
{
    switch (dt)
    {
        case dt_string:             return db_string;
        case dt_date:               return db_date;
        case dt_double:             return db_double;
        case dt_integer:            return db_int32;
        case dt_long_long:          return db_int64;
        case dt_unsigned_long_long: return db_uint64;
        case dt_blob:               return db_blob;
        case dt_xml:                return db_xml;
    }

    // unreachable
    return db_string;
}

// polymorphic into type backend

class standard_into_type_backend
{
public:
    standard_into_type_backend() {}
    virtual ~standard_into_type_backend() {}

    virtual void define_by_pos(int& position, void* data, exchange_type type) = 0;

    virtual void pre_exec(int /* num */) {}
    virtual void pre_fetch() = 0;
    virtual void post_fetch(bool gotData, bool calledFromFetch, indicator* ind) = 0;

    virtual void clean_up() = 0;

private:
    SOCI_NOT_COPYABLE(standard_into_type_backend)
};

class vector_into_type_backend
{
public:

    vector_into_type_backend() {}
    virtual ~vector_into_type_backend() {}

    virtual void define_by_pos_bulk(
        int & /* position */, void * /* data */, exchange_type /* type */,
        std::size_t /* begin */, std::size_t * /* end */)
    {
        throw soci_error("into bulk iterators are not supported with this backend");
    }

    virtual void define_by_pos(int& position, void* data, exchange_type type) = 0;

    virtual void pre_exec(int /* num */) {}
    virtual void pre_fetch() = 0;
    virtual void post_fetch(bool gotData, indicator* ind) = 0;

    virtual void resize(std::size_t sz) = 0;
    virtual std::size_t size() const = 0;

    virtual void clean_up() = 0;

private:
    SOCI_NOT_COPYABLE(vector_into_type_backend)
};

// polymorphic use type backend

class standard_use_type_backend
{
public:
    standard_use_type_backend() {}
    virtual ~standard_use_type_backend() {}

    virtual void bind_by_pos(int& position, void* data,
        exchange_type type, bool readOnly) = 0;
    virtual void bind_by_name(std::string const& name,
        void* data, exchange_type type, bool readOnly) = 0;

    virtual void pre_exec(int /* num */) {}
    virtual void pre_use(indicator const* ind) = 0;
    virtual void post_use(bool gotData, indicator * ind) = 0;

    virtual void clean_up() = 0;

private:
    SOCI_NOT_COPYABLE(standard_use_type_backend)
};

class vector_use_type_backend
{
public:
    vector_use_type_backend() {}
    virtual ~vector_use_type_backend() {}

    virtual void bind_by_pos(int& position, void* data, exchange_type type) = 0;
    virtual void bind_by_pos_bulk(int& /* position */, void* /* data */, exchange_type /* type */,
        std::size_t /* begin */, std::size_t * /* end */)
    {
        throw soci_error("using bulk iterators are not supported with this backend");
    }

    virtual void bind_by_name(std::string const& name,
        void* data, exchange_type type) = 0;

    virtual void bind_by_name_bulk(std::string const& /* name */,
        void* /* data */, exchange_type /* type */,
        std::size_t /* begin */, std::size_t * /* end */)
    {
        throw soci_error("using bulk iterators are not supported with this backend");
    }

    virtual void pre_exec(int /* num */) {}
    virtual void pre_use(indicator const* ind) = 0;

    virtual std::size_t size() const = 0;

    virtual void clean_up() = 0;

private:
    SOCI_NOT_COPYABLE(vector_use_type_backend)
};

// polymorphic statement backend

class statement_backend
{
public:
    statement_backend() {}
    virtual ~statement_backend() {}

    virtual void alloc() = 0;
    virtual void clean_up() = 0;

    virtual void prepare(std::string const& query, statement_type eType) = 0;

    enum exec_fetch_result
    {
        ef_success,
        ef_no_data
    };

    virtual exec_fetch_result execute(int number) = 0;
    virtual exec_fetch_result fetch(int number) = 0;

    virtual long long get_affected_rows() = 0;
    virtual int get_number_of_rows() = 0;

    virtual std::string get_parameter_name(int index) const = 0;

    // This function returns the index of the row which is used when dumping
    // the value of query parameters for bulk operations. If it is -1, which is
    // the default value returned by it, no specific row is shown, but if a
    // backend can determine the row of interest, e.g. the first row which
    // triggered an error during execute(), it should return its index.
    virtual int get_row_to_dump() const { return -1; }

    virtual std::string rewrite_for_procedure_call(std::string const& query) = 0;

    virtual int prepare_for_describe() = 0;
    virtual void describe_column(int colNum,
        db_type& dbtype,
        std::string& column_name) = 0;

    // Function converting db_type to legacy data_type: this is mostly, but not
    // quite, backend-independent because different backends handled the same
    // type differently before db_type introduction.
    virtual data_type to_data_type(db_type dbt) const
    {
        switch (dbt)
        {
            case db_wstring:
            case db_string: return dt_string;
            case db_date:   return dt_date;
            case db_double: return dt_double;
            case db_int8:
            case db_uint8:
            case db_int16:
            case db_uint16:
            case db_int32:  return dt_integer;
            case db_uint32:
            case db_int64:  return dt_long_long;
            case db_uint64: return dt_unsigned_long_long;
            case db_blob:   return dt_blob;
            case db_xml:    return dt_xml;
        }

        // unreachable
        return dt_string;
    }

    virtual standard_into_type_backend* make_into_type_backend() = 0;
    virtual standard_use_type_backend* make_use_type_backend() = 0;
    virtual vector_into_type_backend* make_vector_into_type_backend() = 0;
    virtual vector_use_type_backend* make_vector_use_type_backend() = 0;

    // Converts the given (deduced) DB type and converts it into the data type
    // that should be used for fetching the corresponding data.
    // Normally, the deduced DB type should be exactly what should be used but
    // we need to give backends the possibility to overwrite this to e.g. account
    // for known shortcomings in the DB type deduction (e.g. for SQLite).
    // This function is only relevant in cases where we are not dealing with a
    // user-provided exchange type but instead have to set one up ourselves. Most
    // notably this is the case for dynamic (i.e. row-based) binding.
    virtual db_type exchange_dbtype_for(db_type type) const { return type; }

    // These are set when the corresponding make_xxx_backend() above is called
    // by statement_impl. This goes against encapsulation but allows to avoid
    // having to set them in all backends implementations of these functions.
    bool hasIntoElements_ = false;
    bool hasVectorIntoElements_ = false;
    bool hasUseElements_ = false;
    bool hasVectorUseElements_ = false;

private:
    SOCI_NOT_COPYABLE(statement_backend)
};

// polymorphic RowID backend

class rowid_backend
{
public:
    virtual ~rowid_backend() {}
};

class session_backend;

// polymorphic blob backend

class blob_backend
{
public:
    blob_backend() {}
    virtual ~blob_backend() {}

    virtual std::size_t get_len() = 0;

    virtual std::size_t read_from_start(void* buf, std::size_t toRead, std::size_t offset) = 0;

    virtual std::size_t write_from_start(const void* buf, std::size_t toWrite, std::size_t offset) = 0;

    virtual std::size_t append(const void* buf, std::size_t toWrite) = 0;

    virtual void trim(std::size_t newLen) = 0;

    virtual session_backend &get_session_backend() = 0;

    // Deprecated functions with backend-specific semantics preserved only for
    // compatibility.
    std::size_t read(std::size_t offset, void* buf, std::size_t toRead) { return do_deprecated_read(offset, buf, toRead); }
    std::size_t write(std::size_t offset, const void* buf, std::size_t toWrite) { return do_deprecated_write(offset, buf, toWrite); }

private:
    virtual std::size_t do_deprecated_read(std::size_t offset, void* buf, std::size_t toRead) { return read_from_start(buf, toRead, offset); }
    virtual std::size_t do_deprecated_write(std::size_t offset, const void* buf, std::size_t toWrite) { return write_from_start(buf, toWrite, offset); }

    SOCI_NOT_COPYABLE(blob_backend)
};

// polymorphic session backend

class session_backend
{
public:
    session_backend() : failoverCallback_(NULL), session_(NULL) {}
    virtual ~session_backend() {}

    virtual bool is_connected() = 0;

    virtual void begin() = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;

    // At least one of these functions is usually not implemented for any given
    // backend as RDBMS support either sequences or auto-generated values, so
    // we don't declare them as pure virtuals to avoid having to define trivial
    // versions of them in the derived classes. However every backend should
    // define at least one of them to allow the code using auto-generated values
    // to work.
    virtual bool get_next_sequence_value(session&, std::string const&, long long&)
    {
        return false;
    }
    virtual bool get_last_insert_id(session&, std::string const&, long long&)
    {
        return false;
    }

    // There is a set of standard SQL metadata structures that can be
    // queried in a portable way - backends that are standard compliant
    // do not need to override the following methods, which are intended
    // to return a proper query for basic metadata statements.

    // Returns a parameterless query for the list of table names in the current schema.
    virtual std::string get_table_names_query() const
    {
        return "select table_name as \"TABLE_NAME\""
            " from information_schema.tables"
            " where table_schema = 'public'";
    }

    // Returns a query with a single parameter (table name) for the list
    // of columns and their properties.
    virtual std::string get_column_descriptions_query() const
    {
        return "select column_name as \"COLUMN_NAME\","
            " data_type as \"DATA_TYPE\","
            " character_maximum_length as \"CHARACTER_MAXIMUM_LENGTH\","
            " numeric_precision as \"NUMERIC_PRECISION\","
            " numeric_scale as \"NUMERIC_SCALE\","
            " is_nullable as \"IS_NULLABLE\""
            " from information_schema.columns"
            " where table_schema = 'public' and table_name = :t";
    }

    virtual std::string create_table(const std::string & tableName)
    {
        return "create table " + tableName + " (";
    }
    virtual std::string drop_table(const std::string & tableName)
    {
        return "drop table " + tableName;
    }
    virtual std::string truncate_table(const std::string & tableName)
    {
        return "truncate table " + tableName;
    }

    virtual std::string create_column_type(db_type dt,
        int precision, int scale)
    {
        // PostgreSQL was selected as a baseline for the syntax:

        std::string res;
        switch (dt)
        {
        case db_string:
            {
                std::ostringstream oss;

                if (precision == 0)
                {
                    oss << "text";
                }
                else
                {
                    oss << "varchar(" << precision << ")";
                }

                res += oss.str();
            }
            break;

        case db_date:
            res += "timestamp";
            break;

        case db_double:
            {
                std::ostringstream oss;
                if (precision == 0)
                {
                    oss << "numeric";
                }
                else
                {
                    oss << "numeric(" << precision << ", " << scale << ")";
                }

                res += oss.str();
            }
            break;

        case db_int16:
        case db_uint16:
            res += "smallint";
            break;

        case db_int32:
        case db_uint32:
            res += "integer";
            break;

        case db_int64:
        case db_uint64:
            res += "bigint";
            break;

        case db_blob:
            res += "oid";
            break;

        case db_xml:
            res += "xml";
            break;

        default:
            throw soci_error("this db_type is not supported in create_column");
        }

        return res;
    }

    virtual std::string add_column(const std::string & tableName,
                                   const std::string & columnName,
                                   db_type dt,
                                   int precision, int scale)
    {
        return "alter table " + tableName + " add column " + columnName +
               " " + create_column_type(dt, precision, scale);
    }
    virtual std::string alter_column(const std::string & tableName,
                                     const std::string & columnName,
                                     db_type dt,
                                     int precision, int scale)
    {
        return "alter table " + tableName + " alter column " +
               columnName + " type " +
               create_column_type(dt, precision, scale);
    }
    virtual std::string drop_column(const std::string & tableName,
        const std::string & columnName)
    {
        return "alter table " + tableName +
            " drop column " + columnName;
    }
    virtual std::string constraint_unique(const std::string & name,
        const std::string & columnNames)
    {
        return "constraint " + name +
            " unique (" + columnNames + ")";
    }
    virtual std::string constraint_primary_key(const std::string & name,
        const std::string & columnNames)
    {
        return "constraint " + name +
            " primary key (" + columnNames + ")";
    }
    virtual std::string constraint_foreign_key(const std::string & name,
        const std::string & columnNames,
        const std::string & refTableName,
        const std::string & refColumnNames)
    {
        return "constraint " + name +
            " foreign key (" + columnNames + ")" +
            " references " + refTableName + " (" + refColumnNames + ")";
    }
    virtual std::string empty_blob()
    {
        return "lo_creat(-1)";
    }
    virtual std::string nvl()
    {
        return "coalesce";
    }

    virtual std::string get_dummy_from_table() const = 0;

    void set_failover_callback(failover_callback & callback, session & sql)
    {
        failoverCallback_ = &callback;
        session_ = &sql;
    }

    virtual std::string get_backend_name() const = 0;

    virtual statement_backend* make_statement_backend() = 0;
    virtual rowid_backend* make_rowid_backend() = 0;
    virtual blob_backend* make_blob_backend() = 0;

    // The functions below still work but are deprecated (but we don't give
    // deprecation warnings for them because there is no real harm in using
    // them).
    //
    // Use the overloads taking db_type instead in the new code.
    std::string create_column_type(data_type dt, int precision, int scale)
    {
        return create_column_type(to_db_type(dt), precision, scale);
    }

    std::string add_column(const std::string & tableName,
        const std::string & columnName, data_type dt,
        int precision, int scale)
    {
        return add_column(tableName, columnName, to_db_type(dt), precision, scale);
    }

    std::string alter_column(const std::string & tableName,
        const std::string & columnName, data_type dt,
        int precision, int scale)
    {
        return alter_column(tableName, columnName, to_db_type(dt), precision, scale);
    }


    failover_callback * failoverCallback_;
    session * session_;

private:
    SOCI_NOT_COPYABLE(session_backend)
};

} // namespace details

// simple base class for the session back-end factory

class connection_parameters;

class SOCI_DECL backend_factory
{
public:
    backend_factory() {}
    virtual ~backend_factory() {}

    virtual details::session_backend* make_session(
        connection_parameters const& parameters) const = 0;
};

} // namespace soci

#endif // SOCI_BACKEND_H_INCLUDED
