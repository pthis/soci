//
// Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_ROW_H_INCLUDED
#define SOCI_ROW_H_INCLUDED

#include "soci/type-holder.h"
#include "soci/blob.h"
#include "soci/type-conversion-traits.h"
#include "soci/soci-backend.h"
#include "soci/type-conversion.h"
// std
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace soci
{

class SOCI_DECL column_properties
{
    // use getters/setters in case we want to make some
    // of the getters lazy in the future
public:

    std::string get_name() const { return name_; }
    // DEPRECATED. USE get_db_type() INSTEAD.
    data_type get_data_type() const { return dataType_; }
    db_type get_db_type() const { return dbType_; }

    void set_name(std::string const& name) { name_ = name; }
    // DEPRECATED. USE set_db_type(db_type) INSTEAD.
    void set_data_type(data_type dataType)  { dataType_ = dataType; }
    void set_db_type(db_type dataType)  { dbType_ = dataType; }

private:
    std::string name_;
    // DEPRECATED. USE exchangeDataType_ INSTEAD.
    data_type dataType_;
    db_type dbType_;
};

class SOCI_DECL row
{
public:
    row();
    ~row();

    row(row &&other) = default;
    row &operator=(row &&other) = default;

    void uppercase_column_names(bool forceToUpper);
    void add_properties(column_properties const& cp);
    std::size_t size() const;
    void clean_up();

    indicator get_indicator(std::size_t pos) const;
    indicator get_indicator(std::string const& name) const;

    template <typename T>
    inline void add_holder(T* t, indicator* ind)
    {
        holders_.push_back(details::holder::make_holder(t));
        indicators_.push_back(ind);
    }

    column_properties const& get_properties(std::size_t pos) const;
    column_properties const& get_properties(std::string const& name) const;

    template <typename T>
    T get(std::size_t pos) const
    {
        typedef typename type_conversion<T>::base_type base_type;
        static_assert(details::can_use_from_base<type_conversion<T>>(),
                "Can't use row::get() with this type (not convertible/copy-assignable from base_type) - did you mean to use move_as?");
        base_type const& baseVal =
            holders_.at(pos)->get<base_type>(details::value_cast_tag{});

        T ret;
        type_conversion<T>::from_base(baseVal, *indicators_.at(pos), ret);
        return ret;
    }

    template <typename T>
    T move_as(std::size_t pos) const
    {
        typedef typename type_conversion<T>::base_type base_type;
        static_assert(details::can_use_move_from_base<T, base_type>(),
                "row::move_as() can only be called with types that can be instantiated from a base type rvalue reference");
        base_type & baseVal =
            holders_.at(pos)->get<base_type>(details::value_reference_tag{});

        T ret;
        type_conversion<T>::move_from_base(baseVal, *indicators_.at(pos), ret);

        // Re-initialize the holder in order to be able to use this row object
        // for binding to another data set
        baseVal = T{};

        return ret;
    }

    template <typename T>
    T get(std::size_t pos, T const &nullValue) const
    {
        if (i_null == *indicators_.at(pos))
        {
            return nullValue;
        }

        return get<T>(pos);
    }

    template <typename T>
    T move_as(std::size_t pos, T const &nullValue) const
    {
        if (i_null == *indicators_.at(pos))
        {
            return nullValue;
        }

        return move_as<T>(pos);
    }

    template <typename T>
    T get(std::string const &name) const
    {
        std::size_t const pos = find_column(name);
        return get<T>(pos);
    }

    template <typename T>
    T move_as(std::string const &name) const
    {
        std::size_t const pos = find_column(name);
        return move_as<T>(pos);
    }

    template <typename T>
    T get(std::string const &name, T const &nullValue) const
    {
        std::size_t const pos = find_column(name);

        if (i_null == *indicators_[pos])
        {
            return nullValue;
        }

        return get<T>(pos);
    }

    template <typename T>
    T move_as(std::string const &name, T const &nullValue) const
    {
        std::size_t const pos = find_column(name);

        if (i_null == *indicators_[pos])
        {
            return nullValue;
        }

        return move_as<T>(pos);
    }

    template <typename T>
    row const& operator>>(T& value) const
    {
        value = get<T>(currentPos_);
        ++currentPos_;
        return *this;
    }

    void skip(std::size_t num = 1) const
    {
        currentPos_ += num;
    }

    void reset_get_counter() const
    {
        currentPos_ = 0;
    }

private:
    SOCI_NOT_COPYABLE(row)

    std::size_t find_column(std::string const& name) const;

    std::vector<column_properties> columns_;
    std::vector<details::holder*> holders_;
    std::vector<indicator*> indicators_;
    std::map<std::string, std::size_t> index_;

    bool uppercaseColumnNames_;
    mutable std::size_t currentPos_;
};

template <>
blob SOCI_DECL row::move_as<blob>(std::size_t pos) const;

} // namespace soci

#endif // SOCI_ROW_H_INCLUDED
