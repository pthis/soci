//
// Copyright (C) 2011-2013 Denis Chapligin
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_DB2_SOURCE
#include "soci/db2/soci-db2.h"
#include "soci/connection-parameters.h"

#include "soci-autostatement.h"

#include <cstdio>

using namespace soci;
using namespace soci::details;

const char* soci::db2_option_driver_complete = "db2.driver_complete";

const std::string db2_soci_error::sqlState(std::string const & msg,const SQLSMALLINT htype,const SQLHANDLE hndl) {
    std::ostringstream ss(msg, std::ostringstream::app);


    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH + 1];
    SQLCHAR sqlstate[SQL_SQLSTATE_SIZE + 1];
    SQLINTEGER sqlcode;
    SQLSMALLINT length;

    if ( SQLGetDiagRec(htype,
                       hndl,
                       1,
                       sqlstate,
                       &sqlcode,
                       message,
                       SQL_MAX_MESSAGE_LENGTH + 1,
                       &length) == SQL_SUCCESS ) {
        ss<<" SQLMESSAGE: ";
        ss<<message;
    }
    return ss.str();
}

void db2_session_backend::parseKeyVal(std::string const & keyVal) {
    size_t delimiter=keyVal.find_first_of("=");
    std::string key=keyVal.substr(0,delimiter);
    std::string value=keyVal.substr(delimiter+1,keyVal.length());

    this->autocommit=true; //Default value
    if (!key.compare("autocommit")) {
        if (!value.compare("off")) {
            this->autocommit=false;
	}
    }
}

/* DSN=SAMPLE;Uid=db2inst1;Pwd=db2inst1;AutoCommit=off */
/* DATABASE=SAMPLE;hostname=server.com;UID=db2inst1;PWD=db2inst1;ServiceName=50000;Protocol=TCPIP; */
void db2_session_backend::parseConnectString(std::string const &  connectString) {
    std::string processingString(connectString);
    size_t delimiter=processingString.find_first_of(";");
    while(delimiter!=std::string::npos) {
        std::string keyVal=processingString.substr(0,delimiter);
        parseKeyVal(keyVal);
        processingString=processingString.erase(0,delimiter+1);
        delimiter=processingString.find_first_of(";");
    }
    if (!processingString.empty()) {
        parseKeyVal(processingString);
    }
}

db2_session_backend::db2_session_backend(
    connection_parameters const & parameters) :
        in_transaction(false)
{
    std::string const& connectString = parameters.get_connect_string();
    parseConnectString(connectString);

    SQLRETURN cliRC = SQL_ERROR;

    /* Prepare handles */
    cliRC = SQLAllocHandle(SQL_HANDLE_ENV,SQL_NULL_HANDLE,&hEnv);
    if (cliRC != SQL_SUCCESS) {
        throw db2_soci_error("Error while allocating the environment handle",cliRC);
    }

    cliRC = SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc);
    if (cliRC != SQL_SUCCESS) {
        std::string msg=db2_soci_error::sqlState("Error while allocating the connection handle",SQL_HANDLE_ENV,hEnv);
        SQLFreeHandle(SQL_HANDLE_ENV,hEnv);
        throw db2_soci_error(msg,cliRC);
    }

    /* Set autocommit */
    if(this->autocommit) {
        cliRC = SQLSetConnectAttr(hDbc,SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, SQL_NTS);
    } else {
        cliRC = SQLSetConnectAttr(hDbc,SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, SQL_NTS);
    }
    if (cliRC != SQL_SUCCESS) {
        std::string msg=db2_soci_error::sqlState("Error while setting autocommit attribute",SQL_HANDLE_DBC,hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC,hDbc);
        SQLFreeHandle(SQL_HANDLE_ENV,hEnv);
        throw db2_soci_error(msg,cliRC);
    }

    /* Connect to database */
    // NOTE: SQLDriverConnect preparation steps below copied from ODBC backend.
    SQLCHAR outConnString[1024];
    SQLSMALLINT strLength;

    // Prompt the user for any missing information (typically UID/PWD) in the
    // connection string by default but allow overriding this using "prompt"
    // option.
    SQLHWND hwnd_for_prompt = NULL;
    unsigned completion = SQL_DRIVER_COMPLETE;
    std::string completionString;
    if (parameters.get_option(db2_option_driver_complete, completionString))
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

    #ifdef _WIN32
    if (completion != SQL_DRIVER_NOPROMPT)
      hwnd_for_prompt = ::GetDesktopWindow();
    #endif // _WIN32

    cliRC = SQLDriverConnect(hDbc, hwnd_for_prompt,
                reinterpret_cast<SQLCHAR*>(const_cast<char*>(connectString.c_str())),
                (SQLSMALLINT)connectString.size(),
                outConnString, 1024, &strLength,
                static_cast<SQLUSMALLINT>(completion));

    if (cliRC != SQL_SUCCESS) {
        std::string msg=db2_soci_error::sqlState("Error connecting to database",SQL_HANDLE_DBC,hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC,hDbc);
        SQLFreeHandle(SQL_HANDLE_ENV,hEnv);
        throw db2_soci_error(msg,cliRC);
    }

    connection_string_.assign((const char*)outConnString, strLength);
}

db2_session_backend::~db2_session_backend()
{
    clean_up();
}

bool db2_session_backend::is_connected()
{
    details::auto_statement<db2_statement_backend> st(*this);

    // Force preparing the statement immediately (documentation states that
    // "Deferred prepare is on by default").
    SQLSetStmtAttr(st.hStmt, SQL_ATTR_DEFERRED_PREPARE, SQL_DEFERRED_PREPARE_OFF, 0);

    st.prepare("values 1", st_one_time_query);

    return true;
}

void db2_session_backend::begin()
{
    // In DB2, transations begin implicitly; however, autocommit must be disabled for the duration of the transaction
    if(autocommit)
    {
        SQLRETURN cliRC = SQLSetConnectAttr(hDbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_OFF, SQL_NTS);
        if (cliRC != SQL_SUCCESS && cliRC != SQL_SUCCESS_WITH_INFO)
        {
            std::string msg=db2_soci_error::sqlState("Clearing the autocommit attribute failed", SQL_HANDLE_DBC, hDbc);
            SQLFreeHandle(SQL_HANDLE_DBC,hDbc);
            SQLFreeHandle(SQL_HANDLE_ENV,hEnv);
            throw db2_soci_error(msg,cliRC);
        }
    }

    in_transaction = true;
}

void db2_session_backend::commit()
{
    if (!autocommit || in_transaction) {
        in_transaction = false;
        SQLRETURN cliRC = SQLEndTran(SQL_HANDLE_DBC,hDbc,SQL_COMMIT);
        if(autocommit)
        {
            SQLRETURN cliRC2 = SQLSetConnectAttr(hDbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_NTS);
            if ((cliRC == SQL_SUCCESS || cliRC == SQL_SUCCESS_WITH_INFO) &&
                cliRC2 != SQL_SUCCESS && cliRC2 != SQL_SUCCESS_WITH_INFO)
            {
                std::string msg=db2_soci_error::sqlState("Setting the autocommit attribute failed", SQL_HANDLE_DBC, hDbc);
                SQLFreeHandle(SQL_HANDLE_DBC,hDbc);
                SQLFreeHandle(SQL_HANDLE_ENV,hEnv);
                throw db2_soci_error(msg,cliRC);
            }
        }
        if (cliRC != SQL_SUCCESS && cliRC != SQL_SUCCESS_WITH_INFO) {
            throw db2_soci_error("Commit failed",cliRC);
        }
    }
}

void db2_session_backend::rollback()
{
    if (!autocommit || in_transaction) {
        in_transaction = false;
        SQLRETURN cliRC = SQLEndTran(SQL_HANDLE_DBC,hDbc,SQL_ROLLBACK);
        if(autocommit)
        {
            SQLRETURN cliRC2 = SQLSetConnectAttr(hDbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_NTS);
            if ((cliRC == SQL_SUCCESS || cliRC == SQL_SUCCESS_WITH_INFO) &&
                cliRC2 != SQL_SUCCESS && cliRC2 != SQL_SUCCESS_WITH_INFO)
            {
                std::string msg=db2_soci_error::sqlState("Setting the autocommit attribute failed", SQL_HANDLE_DBC, hDbc);
                SQLFreeHandle(SQL_HANDLE_DBC,hDbc);
                SQLFreeHandle(SQL_HANDLE_ENV,hEnv);
                throw db2_soci_error(msg,cliRC);
            }
        }
        if (cliRC != SQL_SUCCESS && cliRC != SQL_SUCCESS_WITH_INFO) {
            throw db2_soci_error("Rollback failed",cliRC);
        }
    }
}

void db2_session_backend::clean_up()
{
    // if a transaction is in progress, it will automatically be rolled back upon when the connection is disconnected/freed
    in_transaction = false;

    SQLDisconnect(hDbc);
    SQLFreeHandle(SQL_HANDLE_DBC,hDbc);
    SQLFreeHandle(SQL_HANDLE_ENV,hEnv);
}

db2_statement_backend * db2_session_backend::make_statement_backend()
{
    return new db2_statement_backend(*this);
}

db2_rowid_backend * db2_session_backend::make_rowid_backend()
{
    return new db2_rowid_backend(*this);
}

db2_blob_backend * db2_session_backend::make_blob_backend()
{
    return new db2_blob_backend(*this);
}
