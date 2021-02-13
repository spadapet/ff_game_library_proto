#include "pch.h"
#include "source/utility.h"

namespace graphics_test
{
    TEST_CLASS(sprite_test)
    {
    public:
        TEST_METHOD(sprite_list_resource)
        {
            std::string_view json_source =
                "{\n"
                "    'test_sprites': { 'res:type': 'sprites', 'optimize': true, 'format': 'bc3', 'mips': '4',\n"
                "        'sprites': {\n"
                "            'one': { 'file': 'file:test_texture.png', 'pos': [ 16, 16 ], 'size': [ 16, 16 ], 'handle': [ 8, 8 ] },\n"
                "            'two': { 'file': 'file:test_texture.png', 'pos': [ 16, 32 ], 'size': [ 8, 8 ], 'handle': [ 4, 4 ], 'repeat': 4 }\n"
                "        }\n"
                "    }\n"
                "}\n";
            auto result = ff::test::create_resources(json_source);
            auto& res = std::get<0>(result);

            ff::auto_resource<ff::sprite_list> sprites_res = res->get_resource_object("test_sprites");
            Assert::IsTrue(sprites_res.valid());

            std::shared_ptr<ff::sprite_list> sprites = sprites_res.object();
            Assert::IsNotNull(sprites.get());
            Assert::IsTrue(sprites->size() == 5);
            Assert::IsNotNull(sprites->get(0)->sprite_data().view());
        }
    };
}
