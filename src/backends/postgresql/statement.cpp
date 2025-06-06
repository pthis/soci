//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_POSTGRESQL_SOURCE
#include "soci/postgresql/soci-postgresql.h"
#include "soci/soci-platform.h"
#include "soci-cstrtoi.h"
#include "soci-ssize.h"

#include <libpq-fe.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>

using namespace soci;
using namespace soci::details;

namespace // unnamed
{

// used only with asynchronous operations in single-row mode
void wait_until_operation_complete(postgresql_session_backend & session)
{
    for (;;)
    {
        PGresult * result = PQgetResult(session.conn_);
        if (result == NULL)
        {
            break;
        }
        else
        {
            postgresql_result r(session, result);
            r.check_for_errors("Cannot execute asynchronous query in single-row mode");
        }
    }
}

void throw_soci_error(PGconn * conn, const char * msg)
{
    std::string description = msg;
    description += ": ";
    description += PQerrorMessage(conn);

    throw soci_error(description);
}

} // unnamed namespace

postgresql_statement_backend::postgresql_statement_backend(
    postgresql_session_backend &session, bool single_row_mode)
    : session_(session), single_row_mode_(single_row_mode),
      result_(session, NULL),
      rowsAffectedBulk_(-1LL), justDescribed_(false)
{
}

postgresql_statement_backend::~postgresql_statement_backend()
{
    if (statementName_.empty() == false)
    {
        try
        {
            session_.deallocate_prepared_statement(statementName_);
        }
        catch (...)
        {
            // Don't allow exceptions to escape from dtor. Suppressing them is
            // not ideal, but terminating the program, as would happen if we're
            // already unwinding the stack because of a previous exception,
            // would be even worse.
        }
    }
}

void postgresql_statement_backend::alloc()
{
    // nothing to do here
}

void postgresql_statement_backend::clean_up()
{
    // 'reset' the value for a
    // potential new execution.
    rowsAffectedBulk_ = -1;

    current_row_ = -1;

    // nothing to do here
}

void postgresql_statement_backend::prepare(std::string const & query,
    statement_type stType)
{
    // rewrite the query by transforming all named parameters into
    // the postgresql_ numbers ones (:abc -> $1, etc.)

    enum { normal, in_quotes, in_identifier, in_name } state = normal;

    std::string name;
    int position = 1;

    for (std::string::const_iterator it = query.begin(), end = query.end();
         it != end; ++it)
    {
        switch (state)
        {
        case normal:
            if (*it == '\'')
            {
                query_ += *it;
                state = in_quotes;
            }
            else if (*it == '\"')
            {
                query_ += *it;
                state = in_identifier;
            }
            else if (*it == ':')
            {
                // Check whether this is a cast operator (e.g. 23::float)
                // and treat it as a special case, not as a named binding
                const std::string::const_iterator next_it = it + 1;
                if ((next_it != end) && (*next_it == ':'))
                {
                    query_ += "::";
                    ++it;
                }
                // Check whether this is an assignment(e.g. x:=y)
                // and treat it as a special case, not as a named binding
                else if ((next_it != end) && (*next_it == '='))
                {
                    query_ += ":=";
                    ++it;
                }
                else
                {
                    state = in_name;
                }
            }
            else // regular character, stay in the same state
            {
                query_ += *it;
            }
            break;
        case in_quotes:
           if (*it == '\'' )
            {
                query_ += *it;
                state = normal;
            }
            else // regular quoted character
            {
                query_ += *it;
            }
            break;
        case in_identifier:
            if ( *it == '\"' )
            {
                query_ += *it;
                state = normal;
            }
            else // regular quoted character
            {
                query_ += *it;
            }
            break;
        case in_name:
            if (std::isalnum(*it) || *it == '_')
            {
                name += *it;
            }
            else // end of name
            {
                names_.push_back(name);
                name.clear();
                std::ostringstream ss;
                ss << '$' << position++;
                query_ += ss.str();
                query_ += *it;
                state = normal;

                // Check whether the named parameter is immediatelly
                // followed by a cast operator (e.g. :name::float)
                // and handle the additional colon immediately to avoid
                // its misinterpretation later on.
                if (*it == ':')
                {
                    const std::string::const_iterator next_it = it + 1;
                    if ((next_it != end) && (*next_it == ':'))
                    {
                        query_ += ':';
                        ++it;
                    }
                }
            }
            break;
        }
    }

    if (state == in_name)
    {
        names_.push_back(name);
        std::ostringstream ss;
        ss << '$' << position++;
        query_ += ss.str();
    }

    if (stType == st_repeatable_query)
    {
        if (!statementName_.empty())
        {
            throw soci_error("Shouldn't already have a prepared statement.");
        }

        // Holding the name temporarily in this var because
        // if it fails to prepare it we can't DEALLOCATE it.
        std::string statementName = session_.get_next_statement_name();

        if (single_row_mode_)
        {
            // prepare for single-row retrieval

            int result = PQsendPrepare(session_.conn_, statementName.c_str(),
                query_.c_str(), isize(names_), NULL);
            if (result != 1)
            {
                throw_soci_error(session_.conn_,
                    "Cannot prepare statement in singlerow mode");
            }

            wait_until_operation_complete(session_);
        }
        else
        {
            // default multi-row query execution

            postgresql_result result(session_,
                PQprepare(session_.conn_, statementName.c_str(),
                    query_.c_str(), isize(names_), NULL));
            result.check_for_errors("Cannot prepare statement.");
        }

        // Now it's safe to save this info.
        statementName_ = statementName;
    }

    stType_ = stType;
}

statement_backend::exec_fetch_result
postgresql_statement_backend::execute(int number)
{
    if (single_row_mode_ && (number > 1))
    {
        throw soci_error("Bulk operations are not supported with single-row mode.");
    }

    // If the statement was "just described", then we know that
    // it was actually executed with all the use elements
    // already bound and pre-used. This means that the result of the
    // query is already on the client side, so there is no need
    // to re-execute it.
    // The optimization based on the existing results
    // from the row description can be performed only once.
    // If the same statement is re-executed,
    // it will be *really* re-executed, without reusing existing data.

    if (justDescribed_ == false)
    {
        // This object could have been already filled with data before.
        clean_up();

        if ((number > 1) && hasIntoElements_)
        {
             throw soci_error(
                  "Bulk use with single into elements is not supported.");
        }

        // Since the bulk operations are not natively supported by postgresql_,
        // we have to explicitly loop to achieve the bulk operations.
        // On the other hand, looping is not needed if there are single
        // use elements, even if there is a bulk fetch.
        // We know that single use and bulk use elements in the same query are
        // not supported anyway, so in the effect the 'number' parameter here
        // specifies the size of vectors (into/use), but 'numberOfExecutions'
        // specifies the number of loops that need to be performed.

        int numberOfExecutions = 1;
        if (number > 0)
        {
             numberOfExecutions = hasUseElements_ ? 1 : number;
        }

        if ((useByPosBuffers_.empty() == false) ||
            (useByNameBuffers_.empty() == false))
        {
            if ((useByPosBuffers_.empty() == false) &&
                (useByNameBuffers_.empty() == false))
            {
                throw soci_error(
                    "Binding for use elements must be either by position "
                    "or by name.");
            }

            rowsAffectedBulk_ = 0;
            for (current_row_ = 0; current_row_ != numberOfExecutions; ++current_row_)
            {
                std::vector<char *> paramValues;

                if (useByPosBuffers_.empty() == false)
                {
                    // use elements bind by position
                    // the map of use buffers can be traversed
                    // in its natural order

                    for (auto const& kv : useByPosBuffers_)
                    {
                        char ** buffers = kv.second;
                        paramValues.push_back(buffers[current_row_]);
                    }
                }
                else
                {
                    // use elements bind by name

                    for (auto const& s : names_)
                    {
                        UseByNameBuffersMap::iterator b
                            = useByNameBuffers_.find(s);
                        if (b == useByNameBuffers_.end())
                        {
                            std::string msg(
                                "Missing use element for bind by name (");
                            msg += s;
                            msg += ").";
                            throw soci_error(msg);
                        }
                        char ** buffers = b->second;
                        paramValues.push_back(buffers[current_row_]);
                    }
                }

                if (stType_ == st_repeatable_query)
                {
                    // this query was separately prepared

                    if (single_row_mode_)
                    {
                        int result = PQsendQueryPrepared(session_.conn_,
                            statementName_.c_str(),
                            isize(paramValues),
                            &paramValues[0], NULL, NULL, 0);
                        if (result != 1)
                        {
                            throw_soci_error(session_.conn_,
                                "Cannot execute prepared query in single-row mode");
                        }

                        result = PQsetSingleRowMode(session_.conn_);
                        if (result != 1)
                        {
                            throw_soci_error(session_.conn_,
                                "Cannot set singlerow mode");
                        }
                    }
                    else
                    {
                        // default multi-row execution

                        result_.reset(PQexecPrepared(session_.conn_,
                                statementName_.c_str(),
                                isize(paramValues),
                                &paramValues[0], NULL, NULL, 0));
                    }
                }
                else // stType_ == st_one_time_query
                {
                    // this query was not separately prepared and should
                    // be executed as a one-time query

                    if (single_row_mode_)
                    {
                        int result = PQsendQueryParams(session_.conn_, query_.c_str(),
                            isize(paramValues),
                            NULL, &paramValues[0], NULL, NULL, 0);
                        if (result != 1)
                        {
                            throw_soci_error(session_.conn_,
                                "cannot execute query in single-row mode");
                        }

                        result = PQsetSingleRowMode(session_.conn_);
                        if (result != 1)
                        {
                            throw_soci_error(session_.conn_,
                                "Cannot set singlerow mode");
                        }
                    }
                    else
                    {
                        // default multi-row execution

                        result_.reset(PQexecParams(session_.conn_, query_.c_str(),
                                isize(paramValues),
                                NULL, &paramValues[0], NULL, NULL, 0));
                    }
                }

                if (numberOfExecutions > 1)
                {
                    // there are only bulk use elements (no intos)

                    result_.check_for_errors("Cannot execute query.");

                    rowsAffectedBulk_ += get_affected_rows();
                }
            }

            current_row_ = -1;

            if (numberOfExecutions > 1)
            {
                // it was a bulk operation
                result_.reset();
                return ef_no_data;
            }

            // otherwise (no bulk), follow the code below
        }
        else
        {
            // there are no use elements
            // - execute the query without parameter information
            if (stType_ == st_repeatable_query)
            {
                // this query was separately prepared

                if (single_row_mode_)
                {
                    int result = PQsendQueryPrepared(session_.conn_,
                        statementName_.c_str(), 0, NULL, NULL, NULL, 0);
                    if (result != 1)
                    {
                        throw_soci_error(session_.conn_,
                            "Cannot execute prepared query in single-row mode");
                    }

                    result = PQsetSingleRowMode(session_.conn_);
                    if (result != 1)
                    {
                        throw_soci_error(session_.conn_,
                            "Cannot set singlerow mode");
                    }
                }
                else
                {
                    // default multi-row execution

                    result_.reset(PQexecPrepared(session_.conn_,
                            statementName_.c_str(), 0, NULL, NULL, NULL, 0));
                }
            }
            else // stType_ == st_one_time_query
            {
                if (single_row_mode_)
                {
                    int result = PQsendQuery(session_.conn_, query_.c_str());
                    if (result != 1)
                    {
                        throw_soci_error(session_.conn_,
                            "Cannot execute query in single-row mode");
                    }

                    result = PQsetSingleRowMode(session_.conn_);
                    if (result != 1)
                    {
                        throw_soci_error(session_.conn_,
                            "Cannot set single-row mode");
                    }
                }
                else
                {
                    // default multi-row execution

                    result_.reset(PQexec(session_.conn_, query_.c_str()));
                }
            }
        }
    }

    bool process_result;
    if (single_row_mode_)
    {
        if (justDescribed_)
        {
            // reuse the result_ that was already filled when executing the query
            // for the purpose of row describe
        }
        else
        {
            PGresult * res = PQgetResult(session_.conn_);
            result_.reset(res);
        }

        process_result = result_.check_for_data("Cannot execute query.");
    }
    else
    {
        // default multi-row execution

        process_result = result_.check_for_data("Cannot execute query.");
    }

    justDescribed_ = false;

    if (process_result)
    {
        currentRow_ = 0;
        rowsToConsume_ = 0;

        numberOfRows_ = PQntuples(result_);
        if (numberOfRows_ == 0)
        {
            return ef_no_data;
        }
        else
        {
            if (number > 0)
            {
                // prepare for the subsequent data consumption
                return fetch(number);
            }
            else
            {
                // execute(0) was meant to only perform the query
                return ef_success;
            }
        }
    }
    else
    {
        return ef_no_data;
    }
}

statement_backend::exec_fetch_result
postgresql_statement_backend::fetch(int number)
{
    if (single_row_mode_ && (number > 1))
    {
        throw soci_error("Bulk operations are not supported with single-row mode.");
    }

    // Note:
    // In the multi-row mode this function does not actually fetch anything from anywhere
    // - the data was already retrieved from the server in the execute()
    // function, and the actual consumption of this data will take place
    // in the postFetch functions, called for each into element.
    // Here, we only prepare for this to happen (to emulate "the Oracle way").
    // In the single-row mode the fetch of single row of data is performed as expected.

    // forward the "cursor" from the last fetch
    currentRow_ += rowsToConsume_;

    if (currentRow_ >= numberOfRows_)
    {
        if (single_row_mode_)
        {
            PGresult* res = PQgetResult(session_.conn_);
            result_.reset(res);

            if (res == NULL)
            {
                return ef_no_data;
            }

            currentRow_ = 0;
            rowsToConsume_ = 0;

            numberOfRows_ = PQntuples(result_);
            if (numberOfRows_ == 0)
            {
                return ef_no_data;
            }
            else
            {
                rowsToConsume_ = 1;

                return ef_success;
            }
        }
        else
        {
            // default multi-row execution

            // all rows were already consumed

            return ef_no_data;
        }
    }
    else
    {
        if (currentRow_ + number > numberOfRows_)
        {
            if (single_row_mode_)
            {
                rowsToConsume_ = 1;

                return ef_success;
            }
            else
            {
                // default multi-row execution

                rowsToConsume_ = numberOfRows_ - currentRow_;

                // this simulates the behaviour of Oracle
                // - when EOF is hit, we return ef_no_data even when there are
                // actually some rows fetched
                return ef_no_data;
            }
        }
        else
        {
            if (single_row_mode_)
            {
                rowsToConsume_ = 1;
            }
            else
            {
                rowsToConsume_ = number;
            }

            return ef_success;
        }
    }
}

long long postgresql_statement_backend::get_affected_rows()
{
    // PQcmdTuples() doesn't really modify the result but it takes a non-const
    // pointer to it, so we can't rely on implicit conversion here.
    long long result;
    if (cstring_to_integer(result, PQcmdTuples(result_.get_result())))
        return result;

    return rowsAffectedBulk_;
}

int postgresql_statement_backend::get_number_of_rows()
{
    return numberOfRows_ - currentRow_;
}

std::string postgresql_statement_backend::get_parameter_name(int index) const
{
    return names_.at(index);
}

std::string postgresql_statement_backend::rewrite_for_procedure_call(
    std::string const & query)
{
    std::string newQuery("select ");
    newQuery += query;
    return newQuery;
}

int postgresql_statement_backend::prepare_for_describe()
{
    execute(1);
    justDescribed_ = true;

    int columns = PQnfields(result_);
    return columns;
}

void throw_soci_type_error(Oid typeOid, int colNum, char category, const char* typeName )
{
    std::stringstream message;
    message << "unknown data type"
        << " for column number: " << colNum
        << " with type oid: " << (int)typeOid;
    if( category != '\0' )
    {
        message << " with category: " << category;
    }
    message << " with name: " << typeName;

    throw soci_error(message.str());
}

void postgresql_statement_backend::describe_column(int colNum,
    db_type & dbtype, std::string & columnName)
{
    // In postgresql_ column numbers start from 0
    int const pos = colNum - 1;

    unsigned long const typeOid = PQftype(result_, pos);
    switch (typeOid)
    {
    // Note: the following list of OIDs was taken from the pg_type table
    // we do not claim that this list is exhaustive or even correct.

               // from pg_type:

    case 25:   // text
    case 1043: // varchar
    case 2275: // cstring
    case 18:   // char
    case 19:   // name
    case 1042: // bpchar
    case 142:  // xml
    case 114:  // json
    case 17:   // bytea
    case 2950: // uuid
    case 829:  // macaddr
    case 869:  // inet
    case 650:  // cidr
    case 774:  // macaddr8
        dbtype = db_string;
        break;

    case 702:  // abstime
    case 703:  // reltime
    case 1082: // date
    case 1083: // time
    case 1114: // timestamp
    case 1184: // timestamptz
    case 1266: // timetz
        dbtype = db_date;
        break;

    case 700:  // float4
    case 701:  // float8
    case 1700: // numeric
        dbtype = db_double;
        break;

    case 16:   // bool
        dbtype = db_int8;
        break;

    case 21:   // int2
        dbtype = db_int16;
        break;

    case 23:   // int4
        dbtype = db_int32;
        break;

    case 20:   // int8
        dbtype = db_int64;
        break;

    case 26:   // oid
        // Note that in theory OIDs can refer to all sorts of things, but their use
        // for anything but BLOBs seems to be deprecated since PostreSQL 8, so we simply
        // assume any OID refers to a BLOB.
       dbtype = db_blob;
       break;

    default:
    {
        auto typeCategoryIt = categoryByColumnOID_.find(typeOid);
        if ( typeCategoryIt == categoryByColumnOID_.end() )
        {
            std::stringstream query;
            query << "SELECT typcategory FROM pg_type WHERE oid=" << (int)typeOid;

            soci::details::postgresql_result res(session_, PQexec(session_.conn_, query.str().c_str()));
            if ( PQresultStatus(res.get_result()) != PGRES_TUPLES_OK )
            {
                throw_soci_type_error(typeOid, colNum, '\0', PQfname(result_, pos));
            }

            char* typeVal = PQgetvalue(res.get_result(), 0, 0);
            auto iter_inserted = categoryByColumnOID_.insert( std::pair<unsigned long, char>( typeOid, typeVal[0] ) );
            if ( !iter_inserted.second )
            {
                throw_soci_type_error(typeOid, colNum, typeVal[0], PQfname(result_, pos));
            }

            typeCategoryIt = iter_inserted.first;
        }

        char typeCategory = (*typeCategoryIt).second;
        switch ( typeCategory )
        {
            case 'D': // date type
            case 'E': // enum type
            case 'T': // time type
            case 'S': // string type
            case 'U': // user type
            case 'I': // network address type
                dbtype = db_string;
                break;

            default:
                throw_soci_type_error(typeOid, colNum, typeCategory, PQfname(result_, pos));
        }
    }
    }

    columnName = PQfname(result_, pos);
}

postgresql_standard_into_type_backend *
postgresql_statement_backend::make_into_type_backend()
{
    return new postgresql_standard_into_type_backend(*this);
}

postgresql_standard_use_type_backend *
postgresql_statement_backend::make_use_type_backend()
{
    return new postgresql_standard_use_type_backend(*this);
}

postgresql_vector_into_type_backend *
postgresql_statement_backend::make_vector_into_type_backend()
{
    return new postgresql_vector_into_type_backend(*this);
}

postgresql_vector_use_type_backend *
postgresql_statement_backend::make_vector_use_type_backend()
{
    return new postgresql_vector_use_type_backend(*this);
}
