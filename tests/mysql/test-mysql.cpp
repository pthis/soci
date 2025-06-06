//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// MySQL backend copyright (C) 2006 Pawel Aleksander Fedorynski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#include "soci/soci.h"

#include "soci-compiler.h"
#include "soci-ssize.h"
#include "soci/mysql/soci-mysql.h"
#include "mysql/test-mysql.h"
#include <string.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <mysqld_error.h>
#include <errmsg.h>
#include <cstdint>

#include <catch.hpp>

std::string connectString;
backend_factory const &backEnd = *soci::factory_mysql();

// procedure call test
TEST_CASE("MySQL stored procedures", "[mysql][stored-procedure]")
{
    soci::session sql(backEnd, connectString);

    mysql_session_backend *sessionBackEnd
        = static_cast<mysql_session_backend *>(sql.get_backend());
    std::string version = mysql_get_server_info(sessionBackEnd->conn_);
    int v;
    std::istringstream iss(version);
    if ((iss >> v) && v < 5)
    {
        WARN("MySQL server version " << v
                << " does not support stored procedures, skipping test.");
        return;
    }

    try { sql << "drop function myecho"; }
    catch (soci_error const &) {}

    sql <<
        "create function myecho(msg text) "
        "returns text deterministic "
        "  return msg; ";

    std::string in("my message");
    std::string out;

    statement st = (sql.prepare <<
        "select myecho(:input)",
        into(out),
        use(in, "input"));

    st.execute(1);
    CHECK(out == in);

    // explicit procedure syntax
    {
        procedure proc = (sql.prepare <<
            "myecho(:input)",
            into(out), use(in, "input"));

        proc.execute(1);
        CHECK(out == in);
    }

    sql << "drop function myecho";
}

// MySQL error reporting test.
TEST_CASE("MySQL error reporting", "[mysql][exception]")
{
    {
        try
        {
            soci::session sql(backEnd, "host=test.soci.invalid");
        }
        catch (mysql_soci_error const &e)
        {
            if (e.err_num_ != CR_UNKNOWN_HOST &&
                   e.err_num_ != CR_CONN_HOST_ERROR)
            {
                CAPTURE(e.err_num_);
                FAIL("Unexpected error trying to connect to invalid host.");
            }
        }
    }

    {
        soci::session sql(backEnd, connectString);
        sql << "create table soci_test (id integer)";
        try
        {
            int n;
            sql << "select id from soci_test_nosuchtable", into(n);
        }
        catch (mysql_soci_error const &e)
        {
            CHECK(e.err_num_ == ER_NO_SUCH_TABLE);
        }
        try
        {
            sql << "insert into soci_test (invalid) values (256)";
        }
        catch (mysql_soci_error const &e)
        {
            CHECK(e.err_num_ == ER_BAD_FIELD_ERROR);
        }
        // A bulk operation.
        try
        {
            std::vector<int> v(3, 5);
            sql << "insert into soci_test_nosuchtable values (:n)", use(v);
        }
        catch (mysql_soci_error const &e)
        {
            CHECK(e.err_num_ == ER_NO_SUCH_TABLE);
        }
        sql << "drop table soci_test";
    }
}

struct bigint_table_creator : table_creator_base
{
    bigint_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val bigint)";
    }
};

struct bigint_unsigned_table_creator : table_creator_base
{
    bigint_unsigned_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val bigint unsigned)";
    }
};

TEST_CASE("MySQL long long", "[mysql][longlong]")
{
    {
        soci::session sql(backEnd, connectString);

        bigint_table_creator tableCreator(sql);

        long long v1 = 1000000000000LL;
        sql << "insert into soci_test(val) values(:val)", use(v1);

        long long v2 = 0LL;
        sql << "select val from soci_test", into(v2);

        CHECK(v2 == v1);
    }

    // vector<long long>
    {
        soci::session sql(backEnd, connectString);

        bigint_table_creator tableCreator(sql);

        std::vector<long long> v1;
        v1.push_back(1000000000000LL);
        v1.push_back(1000000000001LL);
        v1.push_back(1000000000002LL);
        v1.push_back(1000000000003LL);
        v1.push_back(1000000000004LL);

        sql << "insert into soci_test(val) values(:val)", use(v1);

        std::vector<long long> v2(10);
        sql << "select val from soci_test order by val desc", into(v2);

        REQUIRE(v2.size() == 5);
        CHECK(v2[0] == 1000000000004LL);
        CHECK(v2[1] == 1000000000003LL);
        CHECK(v2[2] == 1000000000002LL);
        CHECK(v2[3] == 1000000000001LL);
        CHECK(v2[4] == 1000000000000LL);
    }

    {
        soci::session sql(backEnd, connectString);

        bigint_unsigned_table_creator tableCreator(sql);

        sql << "insert into soci_test set val = 18446744073709551615";
        row v;
        sql << "select * from soci_test", into(v);
    }

    {
        soci::session sql(backEnd, connectString);

        bigint_unsigned_table_creator tableCreator(sql);

        const char* source = "18446744073709551615";
        sql << "insert into soci_test set val = " << source;
        unsigned long long vv = 0;
        sql << "select val from soci_test", into(vv);
        std::stringstream buf;
        buf << vv;
        CHECK(buf.str() == source);
    }

    {
        soci::session sql(backEnd, connectString);

        bigint_unsigned_table_creator tableCreator(sql);

        const char* source = "18446744073709551615";
        sql << "insert into soci_test set val = " << source;
        std::vector<unsigned long long> v(1);
        sql << "select val from soci_test", into(v);
        std::stringstream buf;
        buf << v.at(0);
        CHECK(buf.str() == source);
    }

    {
        soci::session sql(backEnd, connectString);

        bigint_unsigned_table_creator tableCreator(sql);

        unsigned long long n = 18446744073709551615ULL;
        sql << "insert into soci_test(val) values (:n)", use(n);
        unsigned long long m = 0;
        sql << "select val from soci_test", into(m);
        CHECK(n == m);
    }

    {
        soci::session sql(backEnd, connectString);

        bigint_unsigned_table_creator tableCreator(sql);

        std::vector<unsigned long long> v1;
        v1.push_back(18446744073709551615ULL);
        v1.push_back(18446744073709551614ULL);
        v1.push_back(18446744073709551613ULL);
        sql << "insert into soci_test(val) values(:val)", use(v1);

        std::vector<unsigned long long> v2(10);
        sql << "select val from soci_test order by val", into(v2);

        REQUIRE(v2.size() == 3);
        CHECK(v2[0] == 18446744073709551613ULL);
        CHECK(v2[1] == 18446744073709551614ULL);
        CHECK(v2[2] == 18446744073709551615ULL);
    }
}

template <typename T>
void test_num(const char* s, bool valid, T value)
{
    try
    {
        soci::session sql(backEnd, connectString);
        T val;
        sql << "select \'" << s << "\'", into(val);
        if (valid)
        {
            double v1 = static_cast<double>(value);
            double v2 = static_cast<double>(val);
            double d = std::fabs(v1 - v2);
            double epsilon = 0.001;
            if (d >= epsilon &&
                   d >= epsilon * (std::fabs(v1) + std::fabs(v2)))
            {
                FAIL("Difference between " << value
                       << " and " << val << " is too big.");
            }
        }
        else
        {
            FAIL("string \"" << s << "\" parsed as " << val
                      << " but should have failed.");
        }
    }
    catch (soci_error const& e)
    {
        if (valid)
        {
            FAIL("couldn't parse number: \"" << s << "\"");
        }
        else
        {
            char const * expectedPrefix = "Cannot convert data";
            CAPTURE(e.what());
            CHECK(strncmp(e.what(), expectedPrefix, strlen(expectedPrefix)) == 0);
        }
    }
}

// Number conversion test.
TEST_CASE("MySQL number conversion", "[mysql][float][int]")
{
    test_num<double>("", false, 0);
    test_num<double>("foo", false, 0);
    test_num<double>("1", true, 1);
    test_num<double>("12", true, 12);
    test_num<double>("123", true, 123);
    test_num<double>("12345", true, 12345);
    test_num<double>("12341234123412341234123412341234123412341234123412341",
        true, 1.23412e+52);
    test_num<double>("99999999999999999999999912222222222222222222222222223"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333"
        "9999999999999999999999991222222222222222222222222222333333333333",
        false, 0);
    test_num<double>("1e3", true, 1000);
    test_num<double>("1.2", true, 1.2);
    test_num<double>("1.2345e2", true, 123.45);
    test_num<double>("1 ", false, 0);
    test_num<double>("     123", true, 123);
    test_num<double>("1,2", false, 0);
    test_num<double>("123abc", false, 0);
    test_num<double>("-0", true, 0);

    test_num<short>("123", true, 123);
    test_num<short>("100000", false, 0);

    test_num<int>("123", true, 123);
    test_num<int>("2147483647", true, 2147483647);
    test_num<int>("2147483647a", false, 0);
    test_num<int>("2147483648", false, 0);
    // -2147483648 causes a warning because it is interpreted as
    // 2147483648 (which doesn't fit in an integer) to which a negation
    // is applied.
    test_num<int>("-2147483648", true, -2147483647 - 1);
    test_num<int>("-2147483649", false, 0);
    test_num<int>("-0", true, 0);
    test_num<int>("1.1", false, 0);

    test_num<long long>("123", true, 123);
    test_num<long long>("9223372036854775807", true, 9223372036854775807LL);
    test_num<long long>("9223372036854775808", false, 0);
}

TEST_CASE("MySQL datetime", "[mysql][datetime]")
{
    soci::session sql(backEnd, connectString);
    std::tm t = std::tm();
    sql << "select maketime(19, 54, 52)", into(t);
    CHECK(t.tm_year == 0);
    CHECK(t.tm_mon == 0);
    CHECK(t.tm_mday == 1);
    CHECK(t.tm_hour == 19);
    CHECK(t.tm_min == 54);
    CHECK(t.tm_sec == 52);
}

// TEXT type support test.
TEST_CASE("MySQL text", "[mysql][text]")
{
    soci::session sql(backEnd, connectString);
    std::string a("asdfg\0hjkl", 10);
    // The maximum length for TEXT and BLOB is 65536.
    std::string x(60000, 'X');

    sql << "create table soci_test (id int, text_value text)";
    sql << "insert into soci_test values (1, \'foo\')";
    sql << "insert into soci_test "
        << "values (2, \'qwerty\\0uiop\')";
    sql << "insert into soci_test values (3, :a)",
           use(a);
    sql << "insert into soci_test values (4, :x)",
           use(x);

    std::vector<std::string> text_vec(100);
    sql << "select text_value from soci_test order by id", into(text_vec);
    REQUIRE(text_vec.size() == 4);
    CHECK(text_vec[0] == "foo");
    CHECK(text_vec[1] == std::string("qwerty\0uiop", 11));
    CHECK(text_vec[2] == a);
    CHECK(text_vec[3] == x);

    std::string text;
    sql << "select text_value from soci_test where id = 1", into(text);
    CHECK(text == "foo");
    sql << "select text_value from soci_test where id = 2", into(text);
    CHECK(text == std::string("qwerty\0uiop", 11));
    sql << "select text_value from soci_test where id = 3", into(text);
    CHECK(text == a);
    sql << "select text_value from soci_test where id = 4", into(text);
    CHECK(text == x);

    rowset<row> rs =
        (sql.prepare << "select text_value from soci_test order by id");
    rowset<row>::const_iterator r = rs.begin();
    CHECK(r->get_properties(0).get_data_type() == dt_string);
    CHECK(r->get_properties(0).get_db_type() == db_string);
    CHECK(r->get<std::string>(0) == "foo");
    ++r;
    CHECK(r->get_properties(0).get_data_type() == dt_string);
    CHECK(r->get_properties(0).get_db_type() == db_string);
    CHECK(r->get<std::string>(0) == std::string("qwerty\0uiop", 11));
    ++r;
    CHECK(r->get_properties(0).get_data_type() == dt_string);
    CHECK(r->get_properties(0).get_db_type() == db_string);
    CHECK(r->get<std::string>(0) == a);
    ++r;
    CHECK(r->get_properties(0).get_data_type() == dt_string);
    CHECK(r->get_properties(0).get_db_type() == db_string);
    CHECK(r->get<std::string>(0) == x);
    ++r;
    CHECK(r == rs.end());

    sql << "drop table soci_test";
}

// test for number of affected rows

struct integer_value_table_creator : table_creator_base
{
    integer_value_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val integer)";
    }
};

TEST_CASE("MySQL get affected rows", "[mysql][affected-rows]")
{
    soci::session sql(backEnd, connectString);

    integer_value_table_creator tableCreator(sql);

    for (int i = 0; i != 10; i++)
    {
        sql << "insert into soci_test(val) values(:val)", use(i);
    }

    statement st1 = (sql.prepare <<
        "update soci_test set val = val + 1");
    st1.execute(false);

    CHECK(st1.get_affected_rows() == 10);

    statement st2 = (sql.prepare <<
        "delete from soci_test where val <= 5");
    st2.execute(false);

    CHECK(st2.get_affected_rows() == 5);
}


// The prepared statements should survive session::reconnect().
// However currently it doesn't and attempting to use it results in crashes due
// to accessing the already destroyed session backend, so disable this test.
TEST_CASE("MySQL statements after reconnect", "[mysql][connect][.]")
{
    soci::session sql(backEnd, connectString);

    integer_value_table_creator tableCreator(sql);

    int i;
    statement st = (sql.prepare
        << "insert into soci_test(val) values(:val)", use(i));
    i = 5;
    st.execute(true);

    sql.reconnect();

    i = 6;
    st.execute(true);

    sql.close();
    sql.reconnect();

    i = 7;
    st.execute(true);

    std::vector<int> v(5);
    sql << "select val from soci_test order by val", into(v);
    REQUIRE(v.size() == 3);
    CHECK(v[0] == 5);
    CHECK(v[1] == 6);
    CHECK(v[2] == 7);
}

struct unsigned_value_table_creator : table_creator_base
{
    unsigned_value_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val int unsigned)";
    }
};

// rowset<> should be able to take INT UNSIGNED.
TEST_CASE("MySQL unsigned int", "[mysql][int]")
{
    soci::session sql(backEnd, connectString);

    unsigned_value_table_creator tableCreator(sql);

    unsigned int mask = 0xffffff00;
    sql << "insert into soci_test set val = " << mask;
    soci::rowset<> rows(sql.prepare << "select val from soci_test");
    CHECK(std::distance(rows.begin(), rows.end()) == 1);
}

TEST_CASE("MySQL function call", "[mysql][function]")
{
    soci::session sql(backEnd, connectString);

    row r;

    sql << "set @day = '5'";
    sql << "set @mm = 'december'";
    sql << "set @year = '2012'";
    sql << "select concat(@day,' ',@mm,' ',@year)", into(r);
}

struct double_value_table_creator : table_creator_base
{
    double_value_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val double)";
    }
};

TEST_CASE("MySQL special floating point values", "[mysql][float]")
{
    static bool is_iec559 = std::numeric_limits<double>::is_iec559;
    if (!is_iec559)
    {
        WARN("C++ double type is not IEC-559, skipping test.");
        return;
    }

  const std::string expectedError =
      "Use element used with infinity or NaN, which are "
      "not supported by the MySQL server.";
  {
    soci::session sql(backEnd, connectString);

    double x = std::numeric_limits<double>::quiet_NaN();
    statement st = (sql.prepare << "SELECT :x", use(x, "x"));
    try {
        st.execute(true);
    } catch (soci_error const &e) {
        CHECK(e.get_error_message() == expectedError);
    }
  }
  {
    soci::session sql(backEnd, connectString);

    double x = std::numeric_limits<double>::infinity();
    statement st = (sql.prepare << "SELECT :x", use(x, "x"));
    try {
        st.execute(true);
    } catch (soci_error const &e) {
        CHECK(e.get_error_message() == expectedError);
    }
  }
  {
    soci::session sql(backEnd, connectString);
    double_value_table_creator tableCreator(sql);

    std::vector<double> v(1, std::numeric_limits<double>::quiet_NaN());
    try {
        sql << "insert into soci_test (val) values (:val)", use(v);
    } catch (soci_error const &e) {
        CHECK(e.get_error_message() == expectedError);
    }
  }
  {
    soci::session sql(backEnd, connectString);
    double_value_table_creator tableCreator(sql);

    std::vector<double> v(1, std::numeric_limits<double>::infinity());
    try {
        sql << "insert into soci_test (val) values (:val)", use(v);
    } catch (soci_error const &e) {
        CHECK(e.get_error_message() == expectedError);
    }
  }
}

struct tinyint_value_table_creator : table_creator_base
{
    tinyint_value_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val tinyint)";
    }
};

struct tinyint_unsigned_value_table_creator : table_creator_base
{
    tinyint_unsigned_value_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val tinyint unsigned)";
    }
};

TEST_CASE("MySQL tinyint", "[mysql][int][tinyint]")
{
  {
    soci::session sql(backEnd, connectString);
    unsigned_value_table_creator tableCreator(sql);
    unsigned int mask = 0xffffff00;
    sql << "insert into soci_test set val = " << mask;
    row r;
    sql << "select val from soci_test", into(r);
    REQUIRE(r.size() == 1);
    CHECK(r.get_properties("val").get_data_type() == dt_long_long);
    CHECK(r.get_properties("val").get_db_type() == db_uint32);
    CHECK(r.get<long long>("val") == 0xffffff00);
    CHECK(r.get<unsigned>("val") == 0xffffff00);
    CHECK(r.get<uint32_t>("val") == 0xffffff00);
  }
  {
    soci::session sql(backEnd, connectString);
    tinyint_value_table_creator tableCreator(sql);
    sql << "insert into soci_test set val = -123";
    row r;
    sql << "select val from soci_test", into(r);
    REQUIRE(r.size() == 1);
    CHECK(r.get_properties("val").get_data_type() == dt_integer);
    CHECK(r.get_properties("val").get_db_type() == db_int8);
    CHECK(r.get<int>("val") == -123);
    CHECK(r.get<int8_t>("val") == -123);
  }
  {
    soci::session sql(backEnd, connectString);
    tinyint_unsigned_value_table_creator tableCreator(sql);
    sql << "insert into soci_test set val = 123";
    row r;
    sql << "select val from soci_test", into(r);
    REQUIRE(r.size() == 1);
    CHECK(r.get_properties("val").get_data_type() == dt_integer);
    CHECK(r.get_properties("val").get_db_type() == db_uint8);
    CHECK(r.get<int>("val") == 123);
    CHECK(r.get<uint8_t>("val") == 123);
  }
  {
    soci::session sql(backEnd, connectString);
    bigint_unsigned_table_creator tableCreator(sql);
    sql << "insert into soci_test set val = 123456789012345";
    row r;
    sql << "select val from soci_test", into(r);
    REQUIRE(r.size() == 1);
    CHECK(r.get_properties("val").get_data_type() == dt_unsigned_long_long);
    CHECK(r.get_properties("val").get_db_type() == db_uint64);
    CHECK(r.get<unsigned long long>("val") == 123456789012345ULL);
    CHECK(r.get<uint64_t>("val") == 123456789012345ULL);
  }
  {
    soci::session sql(backEnd, connectString);
    bigint_table_creator tableCreator(sql);
    sql << "insert into soci_test set val = -123456789012345";
    row r;
    sql << "select val from soci_test", into(r);
    REQUIRE(r.size() == 1);
    CHECK(r.get_properties("val").get_data_type() == dt_long_long);
    CHECK(r.get_properties("val").get_db_type() == db_int64);
    CHECK(r.get<long long>("val") == -123456789012345LL);
    CHECK(r.get<int64_t>("val") == -123456789012345LL);
  }
}

struct strings_table_creator : table_creator_base
{
    strings_table_creator(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(s1 char(20), s2 varchar(20), "
            "s3 tinytext, s4 mediumtext, s5 text, s6 longtext, "
            "b1 binary(20), b2 varbinary(20), e1 enum ('foo', 'bar', 'baz'))";
    }
};

TEST_CASE("MySQL strings", "[mysql][string]")
{
    soci::session sql(backEnd, connectString);
    strings_table_creator tableCreator(sql);
    std::string text = "Ala ma kota.";
    std::string binary("Ala\0ma\0kota.........", 20);
    sql << "insert into soci_test "
        "(s1, s2, s3, s4, s5, s6, b1, b2, e1) values "
        "(:s1, :s2, :s3, :s4, :d5, :s6, :b1, :b2, 'foo')",
        use(text), use(text), use(text), use(text), use(text), use(text),
        use(binary), use(binary);
    row r;
    sql << "select s1, s2, s3, s4, s5, s6, b1, b2, e1 "
        "from soci_test", into(r);
    REQUIRE(r.size() == 9);
    for (int i = 0; i < ssize(r); i++) {
        CHECK(r.get_properties(i).get_data_type() == dt_string);
        CHECK(r.get_properties(i).get_db_type() == db_string);
        if (i < 6) {
            CHECK(r.get<std::string>(i) == text);
        } else if (i < 8) {
            CHECK(r.get<std::string>(i) == binary);
        } else {
            CHECK(r.get<std::string>(i) == "foo");
        }
    }
}

struct table_creator_for_get_last_insert_id : table_creator_base
{
    table_creator_for_get_last_insert_id(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(id integer not null auto_increment, "
            "primary key (id))";
        sql << "alter table soci_test auto_increment = 42";
    }
};

TEST_CASE("MySQL last insert id", "[mysql][last-insert-id]")
{
    soci::session sql(backEnd, connectString);
    table_creator_for_get_last_insert_id tableCreator(sql);
    sql << "insert into soci_test () values ()";
    long long id;
    bool result = sql.get_last_insert_id("soci_test", id);
    CHECK(result == true);
    CHECK(id == 42);
}

TEST_CASE("MySQL DDL with metadata", "[mysql][ddl]")
{
    soci::session sql(backEnd, connectString);

    // note: prepare_column_descriptions expects l-value
    std::string ddl_t1 = "ddl_t1";
    std::string ddl_t2 = "ddl_t2";
    std::string ddl_t3 = "ddl_t3";

    // single-expression variant:
    sql.create_table(ddl_t1).column("i", soci::dt_integer).column("j", soci::dt_integer);

    // check whether this table was created:

    bool ddl_t1_found = false;
    bool ddl_t2_found = false;
    bool ddl_t3_found = false;
    std::string table_name;
    soci::statement st = (sql.prepare_table_names(), into(table_name));
    st.execute();
    while (st.fetch())
    {
        if (table_name == ddl_t1) { ddl_t1_found = true; }
        if (table_name == ddl_t2) { ddl_t2_found = true; }
        if (table_name == ddl_t3) { ddl_t3_found = true; }
    }

    CHECK(ddl_t1_found);
    CHECK(ddl_t2_found == false);
    CHECK(ddl_t3_found == false);

    // check whether ddl_t1 has the right structure:

    bool i_found = false;
    bool j_found = false;
    bool other_found = false;
    soci::column_info ci;
    soci::statement st1 = (sql.prepare_column_descriptions(ddl_t1), into(ci));
    st1.execute();
    while (st1.fetch())
    {
        if (ci.name == "i")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.dataType == soci::db_int32);
            CHECK(ci.nullable);
            i_found = true;
        }
        else if (ci.name == "j")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.dataType == soci::db_int32);
            CHECK(ci.nullable);
            j_found = true;
        }
        else
        {
            other_found = true;
        }
    }

    CHECK(i_found);
    CHECK(j_found);
    CHECK(other_found == false);

    // two more tables:

    // separately defined columns:
    // (note: statement is executed when ddl object goes out of scope)
    {
        soci::ddl_type ddl = sql.create_table(ddl_t2);
        ddl.column("i", soci::dt_integer);
        ddl.column("j", soci::dt_integer);
        ddl.column("k", soci::dt_integer)("not null");
        ddl.primary_key("t2_pk", "j");
    }

    sql.add_column(ddl_t1, "k", soci::dt_integer);
    sql.add_column(ddl_t1, "big", soci::dt_string, 0); // "unlimited" length -> text
    sql.drop_column(ddl_t1, "i");

    // or with constraint as in t2:
    sql.add_column(ddl_t2, "m", soci::dt_integer)("not null");

    // third table with a foreign key to the second one
    {
        soci::ddl_type ddl = sql.create_table(ddl_t3);
        ddl.column("x", soci::dt_integer);
        ddl.column("y", soci::dt_integer);
        ddl.foreign_key("t3_fk", "x", ddl_t2, "j");
    }

    // check if all tables were created:

    ddl_t1_found = false;
    ddl_t2_found = false;
    ddl_t3_found = false;
    soci::statement st2 = (sql.prepare_table_names(), into(table_name));
    st2.execute();
    while (st2.fetch())
    {
        if (table_name == ddl_t1) { ddl_t1_found = true; }
        if (table_name == ddl_t2) { ddl_t2_found = true; }
        if (table_name == ddl_t3) { ddl_t3_found = true; }
    }

    CHECK(ddl_t1_found);
    CHECK(ddl_t2_found);
    CHECK(ddl_t3_found);

    // check if ddl_t1 has the right structure (it was altered):

    i_found = false;
    j_found = false;
    bool k_found = false;
    bool big_found = false;
    other_found = false;
    soci::statement st3 = (sql.prepare_column_descriptions(ddl_t1), into(ci));
    st3.execute();
    while (st3.fetch())
    {
        if (ci.name == "j")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.dataType == soci::db_int32);
            CHECK(ci.nullable);
            j_found = true;
        }
        else if (ci.name == "k")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.dataType == soci::db_int32);
            CHECK(ci.nullable);
            k_found = true;
        }
        else if (ci.name == "big")
        {
            CHECK(ci.type == soci::dt_string);
            CHECK(ci.dataType == soci::db_string);
            CHECK(ci.precision == 0); // "unlimited" for strings
            big_found = true;
        }
        else
        {
            other_found = true;
        }
    }

    CHECK(i_found == false);
    CHECK(j_found);
    CHECK(k_found);
    CHECK(big_found);
    CHECK(other_found == false);

    // check if ddl_t2 has the right structure:

    i_found = false;
    j_found = false;
    k_found = false;
    bool m_found = false;
    other_found = false;
    soci::statement st4 = (sql.prepare_column_descriptions(ddl_t2), into(ci));
    st4.execute();
    while (st4.fetch())
    {
        if (ci.name == "i")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.dataType == soci::db_int32);
            CHECK(ci.nullable);
            i_found = true;
        }
        else if (ci.name == "j")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.dataType == soci::db_int32);
            CHECK(ci.nullable == false); // primary key
            j_found = true;
        }
        else if (ci.name == "k")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.dataType == soci::db_int32);
            CHECK(ci.nullable == false);
            k_found = true;
        }
        else if (ci.name == "m")
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.dataType == soci::db_int32);
            CHECK(ci.nullable == false);
            m_found = true;
        }
        else
        {
            other_found = true;
        }
    }

    CHECK(i_found);
    CHECK(j_found);
    CHECK(k_found);
    CHECK(m_found);
    CHECK(other_found == false);

    sql.drop_table(ddl_t1);
    sql.drop_table(ddl_t3); // note: this must be dropped before ddl_t2
    sql.drop_table(ddl_t2);

    // check if all tables were dropped:

    ddl_t1_found = false;
    ddl_t2_found = false;
    ddl_t3_found = false;
    st2 = (sql.prepare_table_names(), into(table_name));
    st2.execute();
    while (st2.fetch())
    {
        if (table_name == ddl_t1) { ddl_t1_found = true; }
        if (table_name == ddl_t2) { ddl_t2_found = true; }
        if (table_name == ddl_t3) { ddl_t3_found = true; }
    }

    CHECK(ddl_t1_found == false);
    CHECK(ddl_t2_found == false);
    CHECK(ddl_t3_found == false);
}

// Test cross-schema metadata
TEST_CASE("Cross-schema metadata", "[mysql][cross-schema]")
{
    soci::session sql(backEnd, connectString);

    // note: prepare_column_descriptions expects l-value
    std::string tables = "tables";
    std::string column_name = "TABLE_NAME";

    // Get the database name - which happens to be the schema
    // name in generic DB lingo.
    std::string schema;
    sql << "select DATABASE()", into(schema);
    CHECK(!schema.empty());

    // create a table in local schema (database) with the same name
    // as the tables table in the information_schema
    sql.create_table(tables).column(column_name, soci::dt_integer);

    // check whether this table was created:

    bool tables_found = false;
    std::string table_name;
    soci::statement st1 = (sql.prepare_table_names(), into(table_name));
    st1.execute();
    while (st1.fetch())
    {
        if (table_name == tables)
        {
            tables_found = true;
        }
    }

    CHECK(tables_found);

    // Get information for the tables table we just created and not
    // the tables table in information_schema which isn't in our path.
    int  records = 0;
    soci::column_info ci;
    soci::statement st2 = (sql.prepare_column_descriptions(tables), into(ci));
    st2.execute();
    while (st2.fetch())
    {
        if (ci.name == column_name)
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.dataType == soci::db_int32);
            records++;
        }
    }

    CHECK(records == 1);

    // Run the same query but this time specific with the schema.
    std::string schemaTables = schema + "." + tables;
    records = 0;
    soci::statement st3 = (sql.prepare_column_descriptions(schemaTables), into(ci));
    st3.execute();
    while (st3.fetch())
    {
        if (ci.name == column_name)
        {
            CHECK(ci.type == soci::dt_integer);
            CHECK(ci.dataType == soci::db_int32);
            records++;
        }
    }

    CHECK(records == 1);

    // Finally run the query with the information_schema.
    std::string information_schemaTables = "information_schema." + tables;
    records = 0;
    soci::statement st4 = (sql.prepare_column_descriptions(information_schemaTables), into(ci));
    st4.execute();
    while (st4.fetch())
    {
        if (ci.name == column_name)
        {
            CHECK(ci.type == soci::dt_string);
            CHECK(ci.dataType == soci::db_string);
            records++;
        }
    }

    CHECK(records == 1);

    // Delete table and check that it is gone
    sql.drop_table(tables);
    tables_found = false;
    st4 = (sql.prepare_table_names(), into(table_name));
    st4.execute();
    while (st4.fetch())
    {
        if (ci.name == column_name)
        {
            tables_found = true;
        }
    }

    CHECK(tables_found == false);
}


std::string escape_string(soci::session& sql, const std::string& s)
{
    mysql_session_backend* backend = static_cast<mysql_session_backend*>(
        sql.get_backend());
    char* escaped = new char[2 * s.size() + 1];
    mysql_real_escape_string(backend->conn_, escaped, s.data(), static_cast<unsigned long>(s.size()));
    std::string retv = escaped;
    delete [] escaped;
    return retv;
}

void test14()
{
    {
        soci::session sql(backEnd, connectString);
        strings_table_creator tableCreator(sql);
        std::string s = "word1'word2:word3";
        std::string escaped = escape_string(sql, s);
        std::string query = "insert into soci_test (s5) values ('";
        query.append(escaped);
        query.append("')");
        sql << query;
        std::string s2;
        sql << "select s5 from soci_test", into(s2);
        CHECK(s == s2);
    }

    std::cout << "test 14 passed" << std::endl;
}

void test15()
{
    {
        soci::session sql(backEnd, connectString);
        int n;
        sql << "select @a := 123", into(n);
        CHECK(n == 123);
    }

    std::cout << "test 15 passed" << std::endl;
}

test_context tc_mysql;
