#include "pch.h"
#include "resource.h"
#include "resource_load.h"
#include "resource_load_context.h"
#include "resource_object_base.h"
#include "resource_object_v.h"
#include "resource_objects.h"
#include "resource_v.h"
#include "resource_value_provider.h"

using namespace std::string_view_literals;

static const size_t RESOURCE_PERSIST_COOKIE = ff::stable_hash_func("ff::resource_objects@0"sv);
static const size_t RESOURCE_PERSIST_SOURCES = ff::stable_hash_func("ff::resource_objects::sources@0"sv);
static const size_t RESOURCE_PERSIST_DATA = ff::stable_hash_func("ff::resource_objects::data@0"sv);

static ff::value_ptr create_dict_value(std::shared_ptr<ff::saved_data_base> saved_data)
{
    assert_ret_val(saved_data, nullptr);

    auto reader = saved_data->loaded_reader();
    assert_ret_val(reader, nullptr);

    return ff::value::load_typed(*reader);
}

ff::resource_objects::resource_objects()
    : loading_count(0)
    , rebuild_connection(ff::global_resources::rebuild_resources_sink().connect(std::bind(&ff::resource_objects::rebuild, this, std::placeholders::_1)))
{
    this->done_loading_event.set();
}

ff::resource_objects::resource_objects(const ff::dict& dict, bool debug)
    : resource_objects()
{
    this->add_resources(dict, debug);
}

ff::resource_objects::resource_objects(ff::reader_base& reader, bool debug)
    : resource_objects()
{
    this->add_resources(reader, debug);
}

ff::resource_objects::~resource_objects()
{
    this->flush_all_resources();
}

std::shared_ptr<ff::resource> ff::resource_objects::get_resource_object(std::string_view name)
{
    std::shared_ptr<ff::resource> value;
    {
        std::scoped_lock lock(this->resource_object_info_mutex);
        value = this->get_resource_object_here(name);
    }

    if (!value)
    {
        value = std::make_shared<ff::resource>(name, ff::value::create<nullptr_t>());
    }

    return value;
}

// Caller must own the this->resource_object_info_mutex lock
std::shared_ptr<ff::resource> ff::resource_objects::get_resource_object_here(std::string_view name)
{
    std::shared_ptr<ff::resource> resource_result;

    auto iter = this->resource_object_infos.find(name);
    if (iter != this->resource_object_infos.cend())
    {
        resource_object_info& info = iter->second;
        resource_result = info.weak_value.lock();

        if (!resource_result)
        {
            if (this->loading_count.fetch_add(1) == 0)
            {
                this->done_loading_event.reset();
            }

            resource_result = std::make_shared<ff::resource>(name);

            auto loading_info = std::make_shared<resource_object_loading_info>();
            loading_info->loading_resource = resource_result;
            loading_info->name = name;
            loading_info->owner = &info;
            loading_info->start_time = ff::timer::current_raw_time();
            loading_info->blocked_count = 1;

            info.weak_value = resource_result;
            info.weak_loading_info = loading_info;

            ff::log::write(ff::log::type::resource_load, "Loading: ", name);

            ff::thread_pool::add_task([this, loading_info]()
            {
                ff::value_ptr dict_value = ::create_dict_value(loading_info->owner->saved_value);
                ff::value_ptr new_value = this->create_resource_objects(loading_info, dict_value);
                this->update_resource_object_info(loading_info, new_value);
                // no code here since the destructor may be running
            });
        }
    }

    return resource_result;
}

std::vector<std::string_view> ff::resource_objects::resource_object_names() const
{
    std::scoped_lock lock(this->resource_object_info_mutex);
    std::vector<std::string_view> names;
    names.reserve(this->resource_object_infos.size());

    for (auto& i : this->resource_object_infos)
    {
        names.push_back(i.first);
    }

    return names;
}

void ff::resource_objects::flush_all_resources()
{
    this->done_loading_event.wait();
}

ff::co_task<> ff::resource_objects::flush_all_resources_async()
{
    co_await this->done_loading_event;
}

bool ff::resource_objects::add_resources(const ff::dict& dict, bool debug)
{
    std::scoped_lock lock(this->resource_object_info_mutex);

    for (std::string_view name : dict.child_names())
    {
        if (!ff::string::starts_with(name, ff::internal::RES_PREFIX))
        {
            auto data_vector = std::make_shared<std::vector<uint8_t>>();
            auto data = std::make_shared<ff::data_vector>(data_vector);
            ff::data_writer data_writer(data_vector);

            ff::value_ptr dict_value = dict.get(name);
            if (dict_value->save_typed(data_writer))
            {
                resource_object_info info;
                info.saved_value = std::make_shared<ff::saved_data_static>(data, data->size(), ff::saved_data_type::none);
                this->resource_object_infos.try_emplace(name, std::move(info));
            }
        }
        else if (debug && name == ff::internal::RES_SOURCE)
        {
            std::string source_string = dict.get<std::string>(ff::internal::RES_SOURCE);
            this->rebuild_source_files.emplace(ff::filesystem::to_path(source_string));

            std::vector<std::string> input_files = dict.get<std::vector<std::string>>(ff::internal::RES_FILES);
            // TODO: ...
        }
    }

    return true;
}

bool ff::resource_objects::add_resources(ff::reader_base& reader, bool debug)
{
    size_t cookie, size;
    std::vector<std::tuple<std::string, size_t, size_t>> resource_datas;
    std::vector<std::string> source_files;

    // Read header
    {
        assert_ret_val(ff::load(reader, cookie) && cookie == ::RESOURCE_PERSIST_COOKIE && ff::load(reader, size), false);

        for (size_t i = 0; i < size; i++)
        {
            std::string name;
            size_t data_offset, data_size;
            assert_ret_val(ff::load(reader, name) && ff::load(reader, data_offset) && ff::load(reader, data_size), false);
            resource_datas.push_back(std::make_tuple(std::move(name), data_offset, data_size));
        }
    }

    // Read sources
    {
        assert_ret_val(ff::load(reader, cookie) && cookie == ::RESOURCE_PERSIST_SOURCES && ff::load(reader, size), false);

        for (size_t i = 0; i < size; i++)
        {
            std::string name;
            assert_ret_val(ff::load(reader, name), false);
            source_files.push_back(std::move(name));
        }
    }

    assert_ret_val(ff::load(reader, cookie) && cookie == ::RESOURCE_PERSIST_DATA, false);
    const size_t data_start = reader.pos();

    std::scoped_lock lock(this->resource_object_info_mutex);

    for (auto& name : source_files)
    {
        this->rebuild_source_files.emplace(ff::filesystem::to_path(name));
    }

    for (auto& [name, data_offset, data_size] : resource_datas)
    {
        resource_object_info info;
        info.saved_value = reader.saved_data(data_start + data_offset, data_size, data_size, ff::saved_data_type::none);
        this->resource_object_infos.try_emplace(name, std::move(info));
    }

    return true;
}

bool ff::resource_objects::save(ff::writer_base& writer) const
{
    // Collect the memory for each resource
    std::vector<std::tuple<std::string_view, std::shared_ptr<ff::saved_data_base>>> resource_datas;
    std::unordered_set<std::filesystem::path> source_files;
    size_t full_size_guess = sizeof(size_t) * 8; // cookies and sizes
    {
        std::scoped_lock lock(this->resource_object_info_mutex);
        resource_datas.reserve(this->resource_object_infos.size());
        source_files = this->rebuild_source_files;

        for (auto& [name, info] : this->resource_object_infos)
        {
            resource_datas.push_back(std::make_tuple(name, info.saved_value));
            full_size_guess += name.size() + info.saved_value->saved_size();
        }
    }

    full_size_guess += source_files.size() * 128; // source files
    full_size_guess += resource_datas.size() * sizeof(size_t) * 4; // size, offset, name length
    writer.reserve(full_size_guess);

    // Write header
    {
        const size_t size = resource_datas.size();
        assert_ret_val(ff::save(writer, ::RESOURCE_PERSIST_COOKIE) && ff::save(writer, size), false);

        size_t data_offset = 0;
        for (auto& [name, saved_data] : resource_datas)
        {
            const size_t data_size = saved_data->saved_size();
            assert_ret_val(ff::save(writer, name) && ff::save(writer, data_offset) && ff::save(writer, data_size), false);
            data_offset += saved_data->saved_size();
        }
    }

    // Write source files
    {
        const size_t size = source_files.size();
        assert_ret_val(ff::save(writer, ::RESOURCE_PERSIST_SOURCES) && ff::save(writer, size), false);

        for (auto& path : source_files)
        {
            std::string path_string = ff::filesystem::to_string(path);
            assert_ret_val(ff::save(writer, path_string), false);
        }
    }

    // Write data
    {
        assert_ret_val(ff::save(writer, ::RESOURCE_PERSIST_DATA), false);

        for (auto& [name, saved_data] : resource_datas)
        {
            auto data = saved_data->saved_data();
            assert_ret_val(data && ff::save(writer, name), false);
            assert_ret_val(ff::save_bytes(writer, *data), false);
        }
    }

    return true;
}

bool ff::resource_objects::save(ff::dict& dict) const
{
    std::scoped_lock lock(this->resource_object_info_mutex);

    for (auto& [name, info] : this->resource_object_infos)
    {
        ff::value_ptr dict_value = ::create_dict_value(info.saved_value);
        dict.set(name, dict_value);
    }

    return true;
}

bool ff::resource_objects::save_to_cache(ff::dict& dict) const
{
    std::shared_ptr<ff::saved_data_base> saved_data;
    {
        auto data_vector = std::make_shared<std::vector<uint8_t>>();
        ff::data_writer writer(data_vector);
        assert_ret_val(this->save(writer), false);

        auto data = std::make_shared<ff::data_vector>(data_vector);
        saved_data = std::make_shared<ff::saved_data_static>(data, data->size(), ff::saved_data_type::none);
    }

    dict.set<ff::saved_data_base>("resources", std::move(saved_data));
    return true;
}

void ff::resource_objects::update_resource_object_info(std::shared_ptr<resource_object_loading_info> loading_info, ff::value_ptr new_value)
{
    std::unique_lock loading_lock(loading_info->mutex);

    assert(loading_info->blocked_count > 0);
    bool loading_done = loading_info->blocked_count > 0 && !--loading_info->blocked_count;

    ff::log::write(ff::log::type::resource_load, "Update: ", loading_info->name, " (", loading_info->blocked_count, (loading_done ? ", done)" : ", blocked)"));

    if (loading_done)
    {
        std::shared_ptr<ff::resource_object_base> new_obj = new_value->get<ff::resource_object_base>();
        if (new_obj && !new_obj->resource_load_complete(false))
        {
            assert(false);
            new_value = ff::value::create<nullptr_t>();
        }
    }

    loading_info->final_value = new_value;

    if (loading_done)
    {
        loading_info->loading_resource->finalize_value(new_value);
        {
            std::scoped_lock lock(this->resource_object_info_mutex);
            loading_info->owner->weak_loading_info.reset();
        }

        for (auto parent_loading_info : loading_info->parent_loading_infos)
        {
            ff::log::write(ff::log::type::resource_load, "Unblocking: '", parent_loading_info->name, "' unblocked by '", loading_info->name, "'");

            this->update_resource_object_info(parent_loading_info, parent_loading_info->final_value);
        }

        const double seconds = ff::timer::seconds_since_raw(loading_info->start_time);
        ff::log::write(ff::log::type::resource, "Loaded: ", loading_info->name, " (", &std::fixed, std::setprecision(1), seconds * 1000.0, "ms)");
    }

    loading_lock.unlock();

    if (loading_done)
    {
        int old_loading_count = this->loading_count.fetch_sub(1);
        assert(old_loading_count >= 1);

        if (old_loading_count == 1)
        {
            this->done_loading_event.set();
        }
    }
}

ff::value_ptr ff::resource_objects::create_resource_objects(std::shared_ptr<resource_object_loading_info> loading_info, ff::value_ptr value)
{
    if (!value)
    {
        return value;
    }

    ff::value_ptr dict_value = ff::type::try_get_dict_from_data(value);
    if (dict_value)
    {
        // Convert dicts with a "res:type" value to a ff::resource_object_base
        ff::dict dict = dict_value->get<ff::dict>();
        std::string type = dict.get<std::string>(ff::internal::RES_TYPE);
        const ff::resource_object_factory_base* factory = ff::resource_object_base::get_factory(type);

        std::vector<std::string_view> names = dict.child_names();
        for (std::string_view name : names)
        {
            ff::value_ptr new_value = this->create_resource_objects(loading_info, dict.get(name));
            dict.set(name, new_value);
        }

        if (factory)
        {
            std::shared_ptr<ff::resource_object_base> obj = factory->load_from_cache(dict);
            value = ff::value::create<ff::resource_object_base>(obj);
        }
        else
        {
            value = ff::value::create<ff::dict>(std::move(dict));
        }
    }
    else if (value->is_type<std::vector<ff::value_ptr>>())
    {
        std::vector<ff::value_ptr> vec = value->get<std::vector<ff::value_ptr>>();
        for (size_t i = 0; i < vec.size(); i++)
        {
            vec[i] = this->create_resource_objects(loading_info, vec[i]);
        }

        value = ff::value::create<std::vector<ff::value_ptr>>(std::move(vec));
    }
    else if (value->is_type<std::string>() || value->is_type<ff::resource>())
    {
        // Resolve references to other resources (and update existing references with the latest value)
        ff::value_ptr string_val = value->convert_or_default<std::string>();
        std::string_view str = string_val->get<std::string>();

        if (ff::string::starts_with(str, ff::internal::REF_PREFIX))
        {
            std::string_view ref_name = str.substr(ff::internal::REF_PREFIX.size());
            std::shared_ptr<ff::resource> ref_value = this->get_resource_object(ref_name);
            {
                std::shared_ptr<resource_object_loading_info> ref_loading_info;
                {
                    std::scoped_lock lock(this->resource_object_info_mutex);
                    auto i = this->resource_object_infos.find(ref_name);
                    if (i != this->resource_object_infos.cend())
                    {
                        ref_loading_info = i->second.weak_loading_info.lock();
                    }
                }

                if (ref_loading_info)
                {
                    std::scoped_lock locks(loading_info->mutex, ref_loading_info->mutex);

                    if (ref_loading_info->blocked_count)
                    {
                        ff::log::write(ff::log::type::resource_load, "Blocking: '", loading_info->name, "' blocked by '", ref_loading_info->name, "'");

                        loading_info->blocked_count++;
                        ref_loading_info->parent_loading_infos.push_back(loading_info);
                    }
                }
            }

            value = ff::value::create<ff::resource>(ref_value);
        }
        else if (ff::string::starts_with(str, ff::internal::LOC_PREFIX))
        {
            std::string_view loc_name = str.substr(ff::internal::LOC_PREFIX.size());
            value = nullptr; // this->get_localized_value(loc_name);

            if (!value)
            {
                ff::log::write_debug_fail(ff::log::type::resource, "Missing localized resource value: ",  loc_name);
            }
        }
    }
    else if (value->is_type<ff::saved_data_base>())
    {
        // fully load and uncompress all saved data
        value = this->create_resource_objects(loading_info, value->try_convert<ff::data_base>());
    }

    return value;
}

void ff::resource_objects::rebuild(ff::push_base<ff::co_task<>>& tasks)
{
    tasks.push(this->rebuild_async());
}

ff::co_task<> ff::resource_objects::rebuild_async()
{
    if constexpr (!ff::constants::profile_build)
    {
        co_return;
    }

    co_await ff::task::yield_on_task();
    co_await this->flush_all_resources_async();

    std::scoped_lock lock(this->resource_object_info_mutex);
    std::unordered_map<std::string_view, std::shared_ptr<ff::resource>> old_resources;

    for (auto& [name, info] : this->resource_object_infos)
    {
        std::shared_ptr<ff::resource> old_resource = info.weak_value.lock();
        if (old_resource)
        {
            old_resources.try_emplace(name, std::move(old_resource));
        }
    }

    this->resource_object_infos.clear();

    for (const std::filesystem::path& source_path : this->rebuild_source_files)
    {
        ff::load_resources_result result = ff::load_resources_from_file(source_path, true, ff::constants::debug_build);
        this->add_resources(result.dict);
    }

    for (auto& [name, old_resource] : old_resources)
    {
        std::shared_ptr<ff::resource> new_resource = this->get_resource_object_here(name);
        if (new_resource)
        {
            old_resource->new_resource(new_resource);
        }
    }
}

std::shared_ptr<ff::resource_object_base> ff::internal::resource_objects_factory::load_from_source(const ff::dict& dict, resource_load_context& context) const
{
    std::vector<std::string> errors;
    ff::load_resources_result result = ff::load_resources_from_json(dict, context.base_path(), context.debug());

    if (!result.errors.empty())
    {
        for (const std::string& error : result.errors)
        {
            context.add_error(error);
        }
    }

    return result.status ? std::make_shared<ff::resource_objects>(result.dict) : nullptr;
}

std::shared_ptr<ff::resource_object_base> ff::internal::resource_objects_factory::load_from_cache(const ff::dict& dict) const
{
    std::shared_ptr<ff::saved_data_base> saved_data = dict.get<ff::saved_data_base>("resources");
    assert_ret_val(saved_data, nullptr);

    auto reader = saved_data->loaded_reader();
    assert_ret_val(reader, nullptr);

    return std::make_shared<ff::resource_objects>(*reader);
}
