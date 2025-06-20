//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_ODBC_SOURCE
#include "soci/soci-platform.h"
#include "soci/odbc/soci-odbc.h"
#include "soci/session.h"

#include "soci-autostatement.h"

#include <cstdio>

using namespace soci;
using namespace soci::details;

char const * soci::odbc_option_driver_complete = "odbc.driver_complete";

odbc_session_backend::odbc_session_backend(
    connection_parameters const & parameters)
    : henv_(0), hdbc_(0), product_(prod_uninitialized)
{
    SQLRETURN rc;

    // Allocate environment handle
    rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv_);
    if (is_odbc_error(rc))
    {
        throw soci_error("Unable to get environment handle");
    }

    // Set the ODBC version environment attribute
    rc = SQLSetEnvAttr(henv_, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_ENV, henv_, "setting ODBC version 3");
    }

    // Allocate connection handle
    rc = SQLAllocHandle(SQL_HANDLE_DBC, henv_, &hdbc_);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_,
                              "allocating connection handle");
    }

    SQLCHAR outConnString[1024];
    SQLSMALLINT strLength = 0;

    // Prompt the user for any missing information (typically UID/PWD) in the
    // connection string by default but allow overriding this using "prompt"
    // option and also suppress prompts when reconnecting, see the comment in
    // soci::session::reconnect().
    SQLHWND hwnd_for_prompt = NULL;
    unsigned completion = SQL_DRIVER_COMPLETE;

    if (parameters.is_option_on(option_reconnect))
    {
      completion = SQL_DRIVER_NOPROMPT;
    }
    else
    {
      std::string completionString;
      if (parameters.get_option(odbc_option_driver_complete, completionString))
      {
        // The value of the option is supposed to be just the integer value of
        // one of SQL_DRIVER_XXX constants but don't check for the exact value in
        // case more of them are added in the future, the ODBC driver will return
        // an error if we pass it an invalid value anyhow.
        if (soci::sscanf(completionString.c_str(), "%u", &completion) != 1)
        {
          throw soci_error("Invalid non-numeric driver completion option value \"" +
                            completionString + "\".");
        }
      }
    }

#ifdef _WIN32
    if (completion != SQL_DRIVER_NOPROMPT)
      hwnd_for_prompt = ::GetDesktopWindow();
#endif // _WIN32

    std::string const & connectString = parameters.get_connect_string();

    // This "infinite" loop can be executed at most twice.
    std::string errContext;
    for (;;)
    {
        rc = SQLDriverConnect(hdbc_, hwnd_for_prompt,
                              sqlchar_cast(connectString),
                              (SQLSMALLINT)connectString.size(),
                              outConnString, 1024, &strLength,
                              static_cast<SQLUSMALLINT>(completion));

        // Don't use is_odbc_error() here as it doesn't consider SQL_NO_DATA to be
        // an error -- but it is one here, as it's returned if a message box shown
        // by SQLDriverConnect() was cancelled and this means we failed to connect.
        switch (rc)
        {
          case SQL_SUCCESS:
          case SQL_SUCCESS_WITH_INFO:
            break;

          case SQL_NO_DATA:
            throw soci_error("Connecting to the database cancelled by user.");

          default:
            odbc_soci_error err(SQL_HANDLE_DBC, hdbc_, "connecting to database");

            // If connection pooling had been enabled by the application, we
            // would get HY110 ODBC error for any connection attempt not using
            // SQL_DRIVER_NOPROMPT, so it's worth retrying with it in this
            // case: in the worst case, we'll hit 28000 ODBC error (login
            // failed), which we'll report together with the context helping to
            // understand where it came from.
            if (memcmp(err.odbc_error_code(), "HY110", 6) == 0 &&
                    completion != SQL_DRIVER_NOPROMPT)
            {
                errContext = "while retrying to connect without prompting, as "
                             "prompting the user is not supported when using "
                             "pooled connections";
                completion = SQL_DRIVER_NOPROMPT;
                continue;
            }

            if (!errContext.empty())
                err.add_context(errContext);

            throw err;
        }

        // This loop only runs once unless we retry in case of HY110 above.
        break;
    }

    connection_string_.assign((const char*)outConnString, strLength);

    reset_transaction();

    configure_connection();
}

void odbc_session_backend::configure_connection()
{
    if ( get_database_product() == prod_postgresql )
    {
        // Increase the number of digits used for floating point values to
        // ensure that the conversions to/from text round trip correctly, which
        // is not the case with the default value of 0. Use the maximal
        // supported value, which was 2 until 9.x and is 3 since it.

        char product_ver[1024];
        SQLSMALLINT len = sizeof(product_ver);
        SQLRETURN rc = SQLGetInfo(hdbc_, SQL_DBMS_VER, product_ver, len, &len);
        if (is_odbc_error(rc))
        {
            throw odbc_soci_error(SQL_HANDLE_DBC, henv_,
                                  "getting PostgreSQL ODBC driver version");
        }

        // The returned string is of the form "##.##.#### ...", but we don't
        // need to parse it fully, we just need the major version which,
        // conveniently, comes first.
        unsigned major_ver = 0;
        if (soci::sscanf(product_ver, "%u", &major_ver) != 1)
        {
            throw soci_error("DBMS version \"" + std::string(product_ver) +
                             "\" in unrecognizable format.");
        }

        // As explained in src/backends/postgresql/session.cpp, we need to
        // increase the number of digits used for floating point values to
        // ensure that all numbers round trip correctly with old PostgreSQL.
        if (major_ver < 12)
        {
            details::auto_statement<odbc_statement_backend> st(*this);

            std::string const q(major_ver >= 9 ? "SET extra_float_digits = 3"
                                               : "SET extra_float_digits = 2");
            rc = SQLExecDirect(st.hstmt_, sqlchar_cast(q), static_cast<SQLINTEGER>(q.size()));

            if (is_odbc_error(rc))
            {
                throw odbc_soci_error(SQL_HANDLE_DBC, henv_,
                                      "setting extra_float_digits for PostgreSQL");
            }
        }

        // This is extracted from pgapifunc.h header from psqlODBC driver.
        enum
        {
            SQL_ATTR_PGOPT_UNKNOWNSASLONGVARCHAR = 65544
        };

        // Also configure the driver to handle unknown types, such as "xml",
        // that we use for x_xmltype, as long varchar instead of limiting them
        // to 256 characters (by default).
        rc = SQLSetConnectAttr(hdbc_, SQL_ATTR_PGOPT_UNKNOWNSASLONGVARCHAR, (SQLPOINTER)1, 0);

        // Ignore the error from this one, failure to set it is not fatal and
        // the attribute is only supported in very recent version of the driver
        // (>= 9.6.300). Using "UnknownsAsLongVarchar=1" in odbc.ini (or
        // setting the corresponding option in the driver dialog box) should
        // work with all versions however.
    }
}

odbc_session_backend::~odbc_session_backend()
{
    clean_up();
}

bool odbc_session_backend::is_connected()
{
    details::auto_statement<odbc_statement_backend> st(*this);

    // The name of the table we check for is irrelevant, as long as we have a
    // working connection, it should still find (or, hopefully, not) something.
    return !is_odbc_error(SQLTables(st.hstmt_,
                                    NULL, SQL_NTS,
                                    NULL, SQL_NTS,
                                    sqlchar_cast("bloordyblop"), SQL_NTS,
                                    NULL, SQL_NTS));
}

void odbc_session_backend::begin()
{
    SQLRETURN rc = SQLSetConnectAttr( hdbc_, SQL_ATTR_AUTOCOMMIT,
                    (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0 );
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_, "beginning transaction");
    }
}

void odbc_session_backend::commit()
{
    SQLRETURN rc = SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_COMMIT);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_, "committing transaction");
    }
    reset_transaction();
}

void odbc_session_backend::rollback()
{
    SQLRETURN rc = SQLEndTran(SQL_HANDLE_DBC, hdbc_, SQL_ROLLBACK);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_, "rolling back transaction");
    }
    reset_transaction();
}

bool odbc_session_backend::get_next_sequence_value(
    session & s, std::string const & sequence, long long & value)
{
    std::string query;

    switch ( get_database_product() )
    {
        case prod_db2:
            query = "select next value for " + sequence + " from SYSIBM.SYSDUMMY1";
            break;

        case prod_firebird:
            query = "select next value for " + sequence + " from rdb$database";
            break;

        case prod_oracle:
            query = "select " + sequence + ".nextval from dual";
            break;

        case prod_postgresql:
            query = "select nextval('" + sequence + "')";
            break;

        case prod_mssql:
        case prod_mysql:
        case prod_sqlite:
            // These RDBMS implement get_last_insert_id() instead.
            return false;

        case prod_unknown:
            // For this one we can't do anything at all.
            return false;

        case prod_uninitialized:
            // This is not supposed to happen at all but still cover this case
            // here to avoid gcc warnings about unhandled enum values in a
            // switch.
            return false;
    }

    s << query, into(value);

    return true;
}

bool odbc_session_backend::get_last_insert_id(
    session & s, std::string const & table, long long & value)
{
    std::string query;

    switch ( get_database_product() )
    {
        case prod_db2:
            query = "SELECT IDENTITY_VAL_LOCAL() AS LASTID FROM SYSIBM.SYSDUMMY1";
            break;

        case prod_mssql:
            {
                // We can't use `ident_current()` because it doesn't
                // distinguish between the empty table and a table with one
                // row inserted and returns `seed_value` in both cases, while
                // we need previous to the initial value in the former
                // (i.e. `seed_value` - `increment_value`).
                long long last, seed, inc;
                indicator ind;

                s << "select last_value, seed_value, increment_value "
                     "from sys.identity_columns where "
                     "object_id = object_id('" << table << "')"
                     , into(last, ind), into(seed), into(inc);

                if (ind == i_null)
                {
                    value = seed - inc;
                }
                else
                {
                    value = last;
                }
            }
            return true;

        case prod_mysql:
            query = "select last_insert_id()";
            break;

        case prod_sqlite:
            query = "select last_insert_rowid()";
            break;

        case prod_firebird:
        case prod_oracle:
        case prod_postgresql:
            // For these RDBMS get_next_sequence_value() should have been used.
            return false;


        case prod_unknown:
            // For this one we can't do anything at all.
            return false;

        case prod_uninitialized:
            // As above, this is not supposed to happen but put it here to
            // mollify gcc.
            return false;
    }

    s << query, into(value);

    return true;
}

std::string odbc_session_backend::get_dummy_from_table() const
{
    std::string table;

    switch ( get_database_product() )
    {
        case prod_db2:
            table = "SYSIBM.SYSDUMMY1";
            break;

        case prod_firebird:
            table = "rdb$database";
            break;

        case prod_oracle:
            table = "dual";
            break;

        case prod_mssql:
        case prod_mysql:
        case prod_sqlite:
        case prod_postgresql:
            // No special dummy table needed.
            break;

            // These cases are here just to make the switch exhaustive, we
            // can't really do anything about them anyhow.
        case prod_unknown:
        case prod_uninitialized:
            break;
    }

    return table;
}

void odbc_session_backend::reset_transaction()
{
    SQLRETURN rc = SQLSetConnectAttr( hdbc_, SQL_ATTR_AUTOCOMMIT,
                    (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0 );
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_, "enabling auto commit");
    }
}


void odbc_session_backend::clean_up()
{
    SQLRETURN rc = SQLDisconnect(hdbc_);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_, "disconnecting");
    }

    rc = SQLFreeHandle(SQL_HANDLE_DBC, hdbc_);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, hdbc_, "freeing connection");
    }

    rc = SQLFreeHandle(SQL_HANDLE_ENV, henv_);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_ENV, henv_, "freeing environment");
    }
}

odbc_statement_backend * odbc_session_backend::make_statement_backend()
{
    return new odbc_statement_backend(*this);
}

odbc_rowid_backend * odbc_session_backend::make_rowid_backend()
{
    return new odbc_rowid_backend(*this);
}

odbc_blob_backend * odbc_session_backend::make_blob_backend()
{
    return new odbc_blob_backend(*this);
}

odbc_session_backend::database_product
odbc_session_backend::get_database_product() const
{
    // Cache the product type, it's not going to change during our life time.
    if (product_ != prod_uninitialized)
        return product_;

    char product_name[1024];
    SQLSMALLINT len = sizeof(product_name);
    SQLRETURN rc = SQLGetInfo(hdbc_, SQL_DBMS_NAME, product_name, len, &len);
    if (is_odbc_error(rc))
    {
        throw odbc_soci_error(SQL_HANDLE_DBC, henv_,
                              "getting ODBC driver name");
    }

    if (strcmp(product_name, "Firebird") == 0)
        product_ = prod_firebird;
    else if (strcmp(product_name, "Microsoft SQL Server") == 0)
        product_ = prod_mssql;
    else if (strcmp(product_name, "MySQL") == 0)
        product_ = prod_mysql;
    else if (strcmp(product_name, "Oracle") == 0)
        product_ = prod_oracle;
    else if (strcmp(product_name, "PostgreSQL") == 0)
        product_ = prod_postgresql;
    else if (strcmp(product_name, "SQLite") == 0)
        product_ = prod_sqlite;
    else if (strstr(product_name, "DB2") == product_name) // "DB2/LINUXX8664"
        product_ = prod_db2;
    else
        product_ = prod_unknown;

    return product_;
}
