#pragma once

#include "dict.h"
#include "value.h"
#include "value_type_base.h"

namespace ff::type
{
    class dict_v : public ff::value
    {
    public:
        dict_v(ff::dict&& value, bool save_compressed = false);

        const ff::dict& get() const;
        bool save_compressed() const;
        static ff::value* get_static_value(ff::dict&& value, bool save_compressed = false);
        static ff::value* get_static_default_value();

    private:
        ff::dict value;
        bool save_compressed_;
    };

    template<>
    struct value_traits<ff::dict> : public value_derived_traits<ff::type::dict_v>
    {};

    class dict_type : public ff::internal::value_type_base<ff::type::dict_v>
    {
    public:
        using value_type_base::value_type_base;

        virtual value_ptr try_convert_to(const value* val, std::type_index type) const override;
        virtual bool can_have_named_children() const override;
        virtual value_ptr named_child(const value* val, std::string_view name) const override;
        virtual std::vector<std::string_view> child_names(const value* val) const override;
        virtual value_ptr load(reader_base& reader) const override;
        virtual bool save(const value* val, writer_base& writer) const override;
    };

    value_ptr try_get_dict_from_data(const value* value);
}
