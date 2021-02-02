#include "pch.h"
#include "graphics.h"
#include "init.h"
#include "shader.h"

static bool init_graphics_status;

namespace
{
    struct one_time_init_grahics
    {
        one_time_init_grahics()
        {
            // Resource objects
            ff::resource_object_base::register_factory<ff::internal::shader_factory>("shader");

            ::init_graphics_status = ff::graphics::internal::init();
        }

        ~one_time_init_grahics()
        {
            ff::graphics::internal::destroy();
            ::init_graphics_status = false;
        }
    };
}

static std::atomic_int init_graphics_refs;
static std::unique_ptr<one_time_init_grahics> init_graphics_data;

ff::init_graphics::init_graphics()
{
    if (::init_graphics_refs.fetch_add(1) == 0)
    {
        ::init_graphics_data = std::make_unique<one_time_init_grahics>();
    }
}

ff::init_graphics::~init_graphics()
{
    if (::init_graphics_refs.fetch_sub(1) == 1)
    {
        ::init_graphics_data.reset();
    }
}

ff::init_graphics::operator bool() const
{
    return this->init_resource && ::init_graphics_status;
}
