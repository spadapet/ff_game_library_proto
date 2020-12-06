#include "pch.h"
#include "../persist.h"
#include "value.h"
#include "value_register_default.h"

static const uint32_t REFS_MASK = 0x00FFFFFF;
static std::array<std::unique_ptr<ff::value_type>, 256> value_type_array;
static std::unordered_map<std::type_index, ff::value_type*> type_index_to_value_type;
static std::unordered_map<uint32_t, ff::value_type*, ff::no_hash<uint32_t>> persist_id_to_value_type;
static ff::internal::value_register_default register_defaults;

ff::value::value()
    : data{}
{}

ff::value::~value()
{
    assert(this->data.refs == 0 || this->data.refs == 1);
}

bool ff::value::can_have_named_children() const
{
    return this->type()->can_have_named_children();
}

ff::value_ptr ff::value::named_child(std::string_view name) const
{
    return this->type()->named_child(this, name);
}

std::vector<std::string> ff::value::child_names() const
{
    return this->type()->child_names(this);
}

bool ff::value::can_have_indexed_children() const
{
    return this->type()->can_have_indexed_children();
}

ff::value_ptr ff::value::index_child(size_t index) const
{
    return this->type()->index_child(this, index);
}

size_t ff::value::index_child_count() const
{
    return this->type()->index_child_count(this);
}

ff::value_ptr ff::value::load_typed(reader_base& reader)
{
    uint32_t persist_id;
    if (ff::load(reader, persist_id))
    {
        const value_type* type = ff::value::get_type_by_persist_id(persist_id);
        if (type)
        {
            return type->load(reader);
        }
    }

    assert(false);
    return nullptr;
}

bool ff::value::save_typed(writer_base& writer) const
{
    uint32_t persist_id = this->type_persist_id();
    bool status = ff::save(writer, persist_id) && this->type()->save(this, writer);
    assert(status);
    return status;
}

void ff::value::print(std::ostream& output) const
{
    this->type()->print(this, output);
}

void ff::value::print_tree(std::ostream& output) const
{
    this->type()->print_tree(this, output);
}

void ff::value::debug_print_tree() const
{
#ifdef _DEBUG
    std::stringstream output;
    this->print_tree(output);
    ::OutputDebugString(ff::string::to_wstring(output.str()).c_str());
#endif
}

const ff::value_type* ff::value::type() const
{
    return this->get_type_by_lookup_id(this->data.type_lookup_id);
}

bool ff::value::equals(const value* other) const
{
    if (!this)
    {
        return !other;
    }

    if (this == other)
    {
        return true;
    }

    if (!other || this->type()->type_index() != other->type()->type_index())
    {
        return false;
    }

    return this->type()->equals(this, other);
}

std::type_index ff::value::type_index() const
{
    return this->type()->type_index();
}

std::string_view ff::value::type_name() const
{
    return this->type()->type_name();
}

uint32_t ff::value::type_lookup_id() const
{
    return this->type()->type_persist_id();
}

uint32_t ff::value::type_persist_id() const
{
    return this->type()->type_persist_id();
}

void ff::value::add_ref() const
{
    if (this->data.refs)
    {
        ::InterlockedIncrement(&this->data.type_and_ref);
    }
}

void ff::value::release_ref() const
{
    if (this->data.refs && (::InterlockedDecrement(&this->data.type_and_ref) & ::REFS_MASK) == 1)
    {
        const value_type* type = this->type();
        value* self = const_cast<value*>(this);
        type->destruct(self);
        ff::internal::value_allocator::delete_bytes(self, type->size_of());
    }
}

bool ff::value::register_type(std::unique_ptr<value_type>&& type)
{
    uint32_t lookup_id = (type != nullptr) ? type->type_lookup_id() : 0;
    if (lookup_id > 0 && lookup_id < ::value_type_array.size() && ::persist_id_to_value_type.find(type->type_persist_id()) == ::persist_id_to_value_type.cend())
    {
        ::type_index_to_value_type.try_emplace(type->type_index(), type.get());
        ::persist_id_to_value_type.try_emplace(type->type_persist_id(), type.get());
        ::value_type_array[lookup_id] = std::move(type);
        return true;
    }

    assert(false);
    return false;
}

const ff::value_type* ff::value::get_type(std::type_index type_index)
{
    auto i = ::type_index_to_value_type.find(type_index);
    if (i != ::type_index_to_value_type.cend())
    {
        return i->second;
    }

    assert(false);
    return nullptr;
}

const ff::value_type* ff::value::get_type_by_lookup_id(uint32_t id)
{
    assert(id > 0 && id < ::value_type_array.size());
    return ::value_type_array[id].get();
}

const ff::value_type* ff::value::get_type_by_persist_id(uint32_t id)
{
    auto i = ::persist_id_to_value_type.find(id);
    if (i != ::persist_id_to_value_type.cend())
    {
        return i->second;
    }

    assert(false);
    return nullptr;
}

bool ff::value::is_type(std::type_index type_index) const
{
    return this->type()->type_index() == type_index;
}

ff::value_ptr ff::value::try_convert(std::type_index type_index) const
{
    if (!this || this->is_type(type_index))
    {
        return this;
    }

    value_ptr new_val = this->type()->try_convert_to(this, type_index);
    if (!new_val)
    {
        const value_type* new_type = ff::value::get_type(type_index);
        if (new_type)
        {
            new_val = new_type->try_convert_from(this);
        }
    }

    return new_val;
}
