//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// MySQL backend copyright (C) 2006 Pawel Aleksander Fedorynski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_MYSQL_SOURCE
#include "soci/mysql/soci-mysql.h"
#include <cctype>

using namespace soci;
using namespace soci::details;
using std::string;


mysql_statement_backend::mysql_statement_backend(
    mysql_session_backend &session)
    : session_(session), result_(NULL),
       rowsAffectedBulk_(-1LL), justDescribed_(false)
{
}

void mysql_statement_backend::alloc()
{
    // nothing to do here.
}

void mysql_statement_backend::clean_up()
{
    // 'reset' the value for a
    // potential new execution.
    rowsAffectedBulk_ = -1;

    if (result_ != NULL)
    {
        mysql_free_result(result_);
        result_ = NULL;
    }
}

void mysql_statement_backend::prepare(std::string const & query,
    statement_type /* eType */)
{
    queryChunks_.clear();
    enum { eNormal, eInQuotes, eInName } state = eNormal;

    std::string name;
    queryChunks_.push_back("");

    bool escaped = false;
    for (std::string::const_iterator it = query.begin(), end = query.end();
         it != end; ++it)
    {
        switch (state)
        {
        case eNormal:
            if (*it == '\'')
            {
                queryChunks_.back() += *it;
                state = eInQuotes;
            }
            else if (*it == ':')
            {
                const std::string::const_iterator next_it = it + 1;
                // Check whether this is an assignment (e.g. @x:=y)
                // and treat it as a special case, not as a named binding.
                if (next_it != end && *next_it == '=')
                {
                    queryChunks_.back() += ":=";
                    ++it;
                }
                else
                {
                    state = eInName;
                }
            }
            else // regular character, stay in the same state
            {
                queryChunks_.back() += *it;
            }
            break;
        case eInQuotes:
            if (*it == '\'' && !escaped)
            {
                queryChunks_.back() += *it;
                state = eNormal;
            }
            else // regular quoted character
            {
                queryChunks_.back() += *it;
            }
            escaped = *it == '\\' && !escaped;
            break;
        case eInName:
            if (std::isalnum(*it) || *it == '_')
            {
                name += *it;
            }
            else // end of name
            {
                names_.push_back(name);
                name.clear();
                queryChunks_.push_back("");
                queryChunks_.back() += *it;
                state = eNormal;
            }
            break;
        }
    }

    if (state == eInName)
    {
        names_.push_back(name);
    }
/*
  cerr << "Chunks: ";
  for (std::vector<std::string>::iterator i = queryChunks_.begin();
  i != queryChunks_.end(); ++i)
  {
  cerr << "\"" << *i << "\" ";
  }
  cerr << "\nNames: ";
  for (std::vector<std::string>::iterator i = names_.begin();
  i != names_.end(); ++i)
  {
  cerr << "\"" << *i << "\" ";
  }
  cerr << endl;
*/
}

statement_backend::exec_fetch_result
mysql_statement_backend::execute(int number)
{
    if (justDescribed_ == false)
    {
        clean_up();

        if (number > 1 && hasIntoElements_)
        {
             throw soci_error(
                  "Bulk use with single into elements is not supported.");
        }
        // number - size of vectors (into/use)
        // numberOfExecutions - number of loops to perform
        int numberOfExecutions = 1;
        if (number > 0)
        {
             numberOfExecutions = hasUseElements_ ? 1 : number;
        }

        std::string query;
        if (!useByPosBuffers_.empty() || !useByNameBuffers_.empty())
        {
            if (!useByPosBuffers_.empty() && !useByNameBuffers_.empty())
            {
                throw soci_error(
                    "Binding for use elements must be either by position "
                    "or by name.");
            }
            long long rowsAffectedBulkTemp = -1;
            for (int i = 0; i != numberOfExecutions; ++i)
            {
                std::vector<char *> paramValues;

                if (!useByPosBuffers_.empty())
                {
                    // use elements bind by position
                    // the map of use buffers can be traversed
                    // in its natural order

                    for (auto const& kv : useByPosBuffers_)
                    {
                        char **buffers = kv.second;
                        //cerr<<"i: "<<i<<", buffers[i]: "<<buffers[i]<<endl;
                        paramValues.push_back(buffers[i]);
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
                        char **buffers = b->second;
                        paramValues.push_back(buffers[i]);
                    }
                }
                //cerr << "queryChunks_.size(): "<<queryChunks_.size()<<endl;
                //cerr << "paramValues.size(): "<<paramValues.size()<<endl;
                if (queryChunks_.size() != paramValues.size()
                    && queryChunks_.size() != paramValues.size() + 1)
                {
                    throw soci_error("Wrong number of parameters.");
                }

                std::vector<std::string>::const_iterator ci
                    = queryChunks_.begin();
                for (auto p : paramValues)
                {
                    query += *ci++;
                    query += p;
                }
                if (ci != queryChunks_.end())
                {
                    query += *ci;
                }
                if (numberOfExecutions > 1)
                {
                    // bulk operation
                    //std::cerr << "bulk operation:\n" << query << std::endl;
                    if (0 != mysql_real_query(session_.conn_, query.c_str(),
                            static_cast<unsigned long>(query.size())))
                    {
                        // preserve the number of rows affected so far.
                        rowsAffectedBulk_ = rowsAffectedBulkTemp;
                        throw mysql_soci_error(mysql_error(session_.conn_),
                            mysql_errno(session_.conn_));
                    }
                    else
                    {
                        if(rowsAffectedBulkTemp == -1)
                        {
                            rowsAffectedBulkTemp = 0;
                        }
                        rowsAffectedBulkTemp += static_cast<long long>(mysql_affected_rows(session_.conn_));
                    }
                    if (mysql_field_count(session_.conn_) != 0)
                    {
                        throw soci_error("The query shouldn't have returned"
                            " any data but it did.");
                    }
                    query.clear();
                }
            }
            rowsAffectedBulk_ = rowsAffectedBulkTemp;
            if (numberOfExecutions > 1)
            {
                // bulk
                return ef_no_data;
            }
        }
        else
        {
            query = queryChunks_.front();
        }

        //std::cerr << query << std::endl;
        if (0 != mysql_real_query(session_.conn_, query.c_str(),
                static_cast<unsigned long>(query.size())))
        {
            throw mysql_soci_error(mysql_error(session_.conn_),
                mysql_errno(session_.conn_));
        }
        result_ = mysql_store_result(session_.conn_);
        if (result_ == NULL && mysql_field_count(session_.conn_) != 0)
        {
            throw mysql_soci_error(mysql_error(session_.conn_),
                mysql_errno(session_.conn_));
        }
        if (result_ != NULL)
        {
            // Cache the rows offsets to have random access to the rows later.
            // [mysql_data_seek() is O(n) so we don't want to use it].
            int numrows = static_cast<int>(mysql_num_rows(result_));
            resultRowOffsets_.resize(numrows);
            for (int i = 0; i < numrows; i++)
            {
                resultRowOffsets_[i] = mysql_row_tell(result_);
                mysql_fetch_row(result_);
            }
        }
    }
    else
    {
        justDescribed_ = false;
    }

    if (result_ != NULL)
    {
        currentRow_ = 0;
        rowsToConsume_ = 0;

        numberOfRows_ = static_cast<int>(mysql_num_rows(result_));
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
        // it was not a SELECT
        return ef_no_data;
    }
}

statement_backend::exec_fetch_result
mysql_statement_backend::fetch(int number)
{
    // Note: This function does not actually fetch anything from anywhere
    // - the data was already retrieved from the server in the execute()
    // function, and the actual consumption of this data will take place
    // in the postFetch functions, called for each into element.
    // Here, we only prepare for this to happen (to emulate "the Oracle way").

    // forward the "cursor" from the last fetch
    currentRow_ += rowsToConsume_;

    if (currentRow_ >= numberOfRows_)
    {
        // all rows were already consumed
        return ef_no_data;
    }
    else
    {
        if (currentRow_ + number > numberOfRows_)
        {
            rowsToConsume_ = numberOfRows_ - currentRow_;

            // this simulates the behaviour of Oracle
            // - when EOF is hit, we return ef_no_data even when there are
            // actually some rows fetched
            return ef_no_data;
        }
        else
        {
            rowsToConsume_ = number;
            return ef_success;
        }
    }
}

long long mysql_statement_backend::get_affected_rows()
{
    if (rowsAffectedBulk_ >= 0)
    {
        return rowsAffectedBulk_;
    }
    return static_cast<long long>(mysql_affected_rows(session_.conn_));
}

int mysql_statement_backend::get_number_of_rows()
{
    return numberOfRows_ - currentRow_;
}

std::string mysql_statement_backend::get_parameter_name(int index) const
{
    return names_.at(index);
}

std::string mysql_statement_backend::rewrite_for_procedure_call(
    std::string const &query)
{
    std::string newQuery("select ");
    newQuery += query;
    return newQuery;
}

int mysql_statement_backend::prepare_for_describe()
{
    execute(1);
    justDescribed_ = true;

    int columns = mysql_field_count(session_.conn_);
    return columns;
}

void mysql_statement_backend::describe_column(int colNum,
    db_type & dbtype, std::string & columnName)
{
    lastDescribedUnsignedMediumInt_ = false;

    int pos = colNum - 1;
    MYSQL_FIELD *field = mysql_fetch_field_direct(result_, pos);
    switch (field->type)
    {
    case FIELD_TYPE_CHAR:       //MYSQL_TYPE_TINY:
        dbtype = field->flags & UNSIGNED_FLAG ? db_uint8 : db_int8;
        break;
    case FIELD_TYPE_SHORT:      //MYSQL_TYPE_SHORT:
        dbtype = field->flags & UNSIGNED_FLAG ? db_uint16 : db_int16;
        break;
    case FIELD_TYPE_INT24:      //MYSQL_TYPE_INT24:
        dbtype = field->flags & UNSIGNED_FLAG ? db_uint32 : db_int32;
        if (dbtype == db_uint32)
            lastDescribedUnsignedMediumInt_ = true;
        break;
    case FIELD_TYPE_LONG:       //MYSQL_TYPE_LONG:
        if (field->flags & UNSIGNED_FLAG)
        {
            dbtype = db_uint32;
        }
        else
        {
            dbtype = db_int32;
        }
        break;
    case FIELD_TYPE_LONGLONG:   //MYSQL_TYPE_LONGLONG:
        if (field->flags & UNSIGNED_FLAG)
        {
            dbtype = db_uint64;
        }
        else
        {
            dbtype = db_int64;
        }
        break;
    case FIELD_TYPE_FLOAT:      //MYSQL_TYPE_FLOAT:
    case FIELD_TYPE_DOUBLE:     //MYSQL_TYPE_DOUBLE:
    case FIELD_TYPE_DECIMAL:    //MYSQL_TYPE_DECIMAL:
    // Prior to MySQL v. 5.x there was no column type corresponding
    // to MYSQL_TYPE_NEWDECIMAL. However, MySQL server 5.x happily
    // sends field type number 246, no matter which version of libraries
    // the client is using.
    case 246:                   //MYSQL_TYPE_NEWDECIMAL:
        dbtype = db_double;
        break;
    case FIELD_TYPE_TIMESTAMP:  //MYSQL_TYPE_TIMESTAMP:
    case FIELD_TYPE_DATE:       //MYSQL_TYPE_DATE:
    case FIELD_TYPE_TIME:       //MYSQL_TYPE_TIME:
    case FIELD_TYPE_DATETIME:   //MYSQL_TYPE_DATETIME:
    case FIELD_TYPE_YEAR:       //MYSQL_TYPE_YEAR:
    case FIELD_TYPE_NEWDATE:    //MYSQL_TYPE_NEWDATE:
        dbtype = db_date;
        break;
//  case MYSQL_TYPE_VARCHAR:
    case 245:                   //MYSQL_TYPE_JSON:
    case FIELD_TYPE_VAR_STRING: //MYSQL_TYPE_VAR_STRING:
    case FIELD_TYPE_STRING:     //MYSQL_TYPE_STRING:
        dbtype = db_string;
        break;
    case FIELD_TYPE_BLOB:       // BLOB
    case FIELD_TYPE_TINY_BLOB:
    case FIELD_TYPE_MEDIUM_BLOB:
    case FIELD_TYPE_LONG_BLOB:
    {
        // Quoted from the docs:
        // To distinguish between binary and nonbinary data for string data types, check whether the
        // charsetnr value is 63. If so, the character set is binary, which indicates binary rather
        // than nonbinary data.
        dbtype = field->charsetnr == 63 ? db_blob : db_string;
        break;
    }
    default:
        //std::cerr << "field->type: " << field->type << std::endl;
        throw soci_error("Unknown data type.");
    }
    columnName = field->name;
}

data_type mysql_statement_backend::to_data_type(db_type dbt) const
{
    // Before adding db_type, this backend returned dt_integer for 24 bit
    // unsigned values but dt_long_long for 32 bit unsigned ones and now we
    // return the same db_uint32 for both and translate it to different legacy
    // values depending on the flag set by describe_column(). This is pretty
    // ugly but needed to preserve compatibility for the people who use MySQL
    // MEDIUMINT UNSIGNED with SOCI.
    if (lastDescribedUnsignedMediumInt_ && dbt == db_uint32)
    {
        return dt_integer;
    }

    return statement_backend::to_data_type(dbt);
}

mysql_standard_into_type_backend *
mysql_statement_backend::make_into_type_backend()
{
    return new mysql_standard_into_type_backend(*this);
}

mysql_standard_use_type_backend *
mysql_statement_backend::make_use_type_backend()
{
    return new mysql_standard_use_type_backend(*this);
}

mysql_vector_into_type_backend *
mysql_statement_backend::make_vector_into_type_backend()
{
    return new mysql_vector_into_type_backend(*this);
}

mysql_vector_use_type_backend *
mysql_statement_backend::make_vector_use_type_backend()
{
    return new mysql_vector_use_type_backend(*this);
}
