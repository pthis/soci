//
// Copyright (C) 2011-2013 Denis Chapligin
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#include "soci/soci.h"
#include "soci/db2/soci-db2.h"
#include "test-context.h"
#include <iostream>
#include <string>
#include <cstdlib>
#include <ctime>

#include <catch.hpp>

using namespace soci;
using namespace soci::tests;

std::string connectString;
backend_factory const &backEnd = *soci::factory_db2();

//
// Support for soci Common Tests
//

struct table_creator_one : public table_creator_base
{
    table_creator_one(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "CREATE TABLE SOCI_TEST(ID INTEGER, VAL SMALLINT, C CHAR, STR VARCHAR(20), SH SMALLINT, LL BIGINT, UL NUMERIC(20), "
            "D DOUBLE, NUM76 NUMERIC(7,6), "
            "TM TIMESTAMP, I1 INTEGER, I2 INTEGER, I3 INTEGER, NAME VARCHAR(20))";
    }
};

struct table_creator_two : public table_creator_base
{
    table_creator_two(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "CREATE TABLE SOCI_TEST(NUM_FLOAT DOUBLE, NUM_INT INTEGER, NAME VARCHAR(20), SOMETIME TIMESTAMP, CHR CHAR)";
    }
};

struct table_creator_three : public table_creator_base
{
    table_creator_three(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "CREATE TABLE SOCI_TEST(NAME VARCHAR(100) NOT NULL, PHONE VARCHAR(15))";
    }
};

struct table_creator_for_get_affected_rows : table_creator_base
{
    table_creator_for_get_affected_rows(soci::session & sql)
        : table_creator_base(sql)
    {
        sql << "CREATE TABLE SOCI_TEST(VAL INTEGER)";
    }
};

class test_context :public test_context_common
{
public:
    test_context() = default;

    std::string get_example_connection_string() const override
    {
        return "DSN=SAMPLE;Uid=db2inst1;Pwd=db2inst1;autocommit=off";
    }

    std::string get_backend_name() const override
    {
        return "db2";
    }

    table_creator_base* table_creator_1(soci::session & pr_s) const override
    {
        pr_s << "SET CURRENT SCHEMA = 'DB2INST1'";
        return new table_creator_one(pr_s);
    }

    table_creator_base* table_creator_2(soci::session & pr_s) const override
    {
        pr_s << "SET CURRENT SCHEMA = 'DB2INST1'";
        return new table_creator_two(pr_s);
    }

    table_creator_base* table_creator_3(soci::session & pr_s) const override
    {
        pr_s << "SET CURRENT SCHEMA = 'DB2INST1'";
        return new table_creator_three(pr_s);
    }

    table_creator_base* table_creator_4(soci::session& s) const override
    {
        return new table_creator_for_get_affected_rows(s);
    }

    std::string to_date_time(std::string const & pi_datdt_string) const override
    {
        return "to_date('" + pi_datdt_string + "', 'YYYY-MM-DD HH24:MI:SS')";
    }

    std::string sql_length(std::string const& s) const override
    {
        return "length(" + s + ")";
    }
};

test_context tc_db2;

//
// Additional tests to exercise the DB2 backend
//

TEST_CASE("DB2 test 1", "[db2]")
{
    soci::session sql(backEnd, connectString);

    sql << "SELECT CURRENT TIMESTAMP FROM SYSIBM.SYSDUMMY1";
    sql << "SELECT " << 123 << " FROM SYSIBM.SYSDUMMY1";

    std::string query = "CREATE TABLE DB2INST1.SOCI_TEST (ID BIGINT,DATA VARCHAR(8))";
    sql << query;

    {
        const int i = 7;
        sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(i,"id");
        int j = 0;
        sql << "select id from db2inst1.SOCI_TEST where id=7", into(j);
        CHECK(j == i);
    }

    {
        const long int li = 9;
        sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(li,"id");
        long int lj = 0;;
        sql << "select id from db2inst1.SOCI_TEST where id=9", into(lj);
        CHECK(lj == li);
    }

    {
        const long long ll = 11;
        sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(ll,"id");
        long long lj = 0;
        sql << "select id from db2inst1.SOCI_TEST where id=11", into(lj);
        CHECK(lj == ll);
    }

    {
        const int i = 13;
        indicator i_ind = i_ok;
        sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(i,i_ind,"id");
        int j = 0;
        indicator j_ind = i_null;
        sql << "select id from db2inst1.SOCI_TEST where id=13", into(j,j_ind);
        CHECK(j == i);
        CHECK(j_ind == i_ok);
    }

    {
        std::vector<int> numbers(100);
        for (int i = 0 ; i < 100 ; i++)
        {
            numbers[i] = i + 1000;
        }
        sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(numbers,"id");
        sql << "select id from db2inst1.SOCI_TEST where id >= 1000 and id < 2000 order by id", into(numbers);
        for (int i = 0 ; i < 100 ; i++)
        {
            CHECK(numbers[i] == i + 1000);
        }
    }

    {
        std::vector<int> numbers(100);
        std::vector<indicator> inds(100);
        for (int i = 0 ; i < 100 ; i++)
        {
            numbers[i] = i + 2000;
            inds[i] = i_ok;
        }
        sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(numbers,inds,"id");
        for (int i = 0 ; i < 100 ; i++)
        {
            numbers[i] = 0;
            inds[i] = i_null;
        }
        sql << "select id from db2inst1.SOCI_TEST where id >= 2000 and id < 3000 order by id", into(numbers,inds);
        for (int i = 0 ; i < 100 ; i++)
        {
            CHECK(numbers[i] == i + 2000);
            CHECK(inds[i] == i_ok);
        }
    }

    {
        int i = 0;
        statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST where id < 1000", into(i));
        st.execute();
        st.fetch();
        CHECK (i == 7);
        st.fetch();
        CHECK (i == 9);
        st.fetch();
        CHECK (i == 11);
        st.fetch();
        CHECK (i == 13);
    }

    {
        int i = 0;
        indicator i_ind = i_null;
        std::string d;
        indicator d_ind = i_ok;
        statement st = (sql.prepare << "select id, data from db2inst1.SOCI_TEST where id = 13", into(i, i_ind), into(d, d_ind));
        st.execute();
        st.fetch();
        CHECK (i == 13);
        CHECK (i_ind == i_ok);
        CHECK (d_ind == i_null);
    }

    {
        std::vector<int> numbers(100);
        for (int i = 0 ; i < 100 ; i++)
        {
            numbers[i] = 0;
        }
        statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST where id >= 1000 order by id", into(numbers));
        st.execute();
        st.fetch();
        for (int i = 0 ; i < 100 ; i++)
        {
            CHECK(numbers[i] == i + 1000);
        }
        st.fetch();
        for (int i = 0 ; i < 100 ; i++)
        {
            CHECK(numbers[i] == i + 2000);
        }
    }

    {
        std::vector<int> numbers(100);
        std::vector<indicator> inds(100);
        for (int i = 0 ; i < 100 ; i++)
        {
            numbers[i] = 0;
            inds[i] = i_null;
        }
        statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST where id >= 1000 order by id", into(numbers, inds));
        st.execute();
        st.fetch();
        for (int i = 0 ; i < 100 ; i++)
        {
            CHECK(numbers[i] == i + 1000);
            CHECK(inds[i] == i_ok);
        }
        st.fetch();
        for (int i = 0 ; i < 100 ; i++)
        {
            CHECK(numbers[i] == i + 2000);
            CHECK(inds[i] == i_ok);
        }
    }

    {
        // XXX: what is the purpose of this test??  what is the expected value?
        int i = 0;
        statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST", use(i));
    }

    {
        // XXX: what is the purpose of this test??  what is the expected value?
        int i = 0;
        indicator ind = i_ok;
        statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST", use(i, ind));
    }

    {
        // XXX: what is the purpose of this test??  what is the expected value?
        std::vector<int> numbers(100);
        statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST", use(numbers));
    }

    {
        // XXX: what is the purpose of this test??  what is the expected value?
        std::vector<int> numbers(100);
        std::vector<indicator> inds(100);
        statement st = (sql.prepare << "select id from db2inst1.SOCI_TEST", use(numbers, inds));
    }

    sql<<"DROP TABLE DB2INST1.SOCI_TEST";
    sql.commit();
}

TEST_CASE("DB2 test 2", "[db2]")
{
    soci::session sql(backEnd, connectString);

    std::string query = "CREATE TABLE DB2INST1.SOCI_TEST (ID BIGINT,DATA VARCHAR(8),DT TIMESTAMP)";
    sql << query;

    {
        int i = 7;
        std::string n("test");
        sql << "insert into db2inst1.SOCI_TEST (id,data) values (:id,:name)", use(i,"id"),use(n,"name");
        int j;
        std::string m;
        sql << "select id,data from db2inst1.SOCI_TEST where id=7", into(j),into(m);
        CHECK (j == i);
        CHECK (m == n);
    }

    {
        int i = 8;
        sql << "insert into db2inst1.SOCI_TEST (id) values (:id)", use(i,"id");
        int j;
        std::string m;
        indicator ind = i_ok;
        sql << "select id,data from db2inst1.SOCI_TEST where id=8", into(j),into(m,ind);
        CHECK(j == i);
        CHECK(ind==i_null);
    }

    {
        std::tm dt;
        sql << "select current timestamp from sysibm.sysdummy1",into(dt);
        sql << "insert into db2inst1.SOCI_TEST (dt) values (:dt)",use(dt,"dt");
        std::tm dt2;
        sql << "select dt from db2inst1.SOCI_TEST where dt is not null", into(dt2);
        CHECK(dt2.tm_year == dt.tm_year);
        CHECK(dt2.tm_mon == dt.tm_mon);
        CHECK(dt2.tm_mday == dt.tm_mday);
        CHECK(dt2.tm_hour == dt.tm_hour);
        CHECK(dt2.tm_min == dt.tm_min);
        CHECK(dt2.tm_sec == dt.tm_sec);
    }

    sql<<"DROP TABLE DB2INST1.SOCI_TEST";
    sql.commit();
}

TEST_CASE("DB2 test 3", "[db2]")
{
    soci::session sql(backEnd, connectString);
    int i;

    std::string query = "CREATE TABLE DB2INST1.SOCI_TEST (ID BIGINT,DATA VARCHAR(8),DT TIMESTAMP)";
    sql << query;

    std::vector<long long> ids(100);
    std::vector<std::string> data(100);
    std::vector<std::tm> dts(100);
    for (int i = 0; i < 100; i++)
    {
        ids[i] = 1000000000LL + i;
        data[i] = "test";
        dts[i].tm_year = 112;
        dts[i].tm_mon = 7;
        dts[i].tm_mday = 17;
        dts[i].tm_hour = 0;
        dts[i].tm_min = 0;
        dts[i].tm_sec = i % 60;
    }

    sql << "insert into db2inst1.SOCI_TEST (id, data, dt) values (:id, :data, :dt)",
        use(ids, "id"), use(data,"data"), use(dts, "dt");

    i = 0;
    rowset<row> rs = (sql.prepare<<"SELECT ID, DATA, DT FROM DB2INST1.SOCI_TEST");
    for (row const& r : rs)
    {
        const long long id = r.get<long long>(0);
        const std::string data = r.get<std::string>(1);
        const std::tm dt = r.get<std::tm>(2);

        CHECK(id == 1000000000LL + i);
        CHECK(data == "test");
        CHECK(dt.tm_year == 112);
        CHECK(dt.tm_mon == 7);
        CHECK(dt.tm_mday == 17);
        CHECK(dt.tm_hour == 0);
        CHECK(dt.tm_min == 0);
        CHECK(dt.tm_sec == i % 60);

        i += 1;
    }

    sql<<"DROP TABLE DB2INST1.SOCI_TEST";
    sql.commit();
}
