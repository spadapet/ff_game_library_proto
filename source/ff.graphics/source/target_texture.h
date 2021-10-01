#pragma once

#include "target_base.h"

namespace ff
{
    class texture;

    class target_texture
        : public ff::target_base
        , public ff_internal_dx::device_child_base
    {
    public:
        target_texture(ff::texture&& texture, size_t array_start = 0, size_t array_count = 0, size_t mip_level = 0);
        target_texture(const std::shared_ptr<ff::texture>& texture, size_t array_start = 0, size_t array_count = 0, size_t mip_level = 0);
        target_texture(target_texture&& other) noexcept = default;
        target_texture(const target_texture& other) = delete;
        virtual ~target_texture() override;

        target_texture& operator=(target_texture&& other) noexcept = default;
        target_texture& operator=(const target_texture & other) = delete;
        operator bool() const;

        const std::shared_ptr<ff::texture>& shared_texture() const;

        // target_base
        virtual bool pre_render(const DirectX::XMFLOAT4* clear_color) override;
        virtual bool post_render() override;
        virtual ff::signal_sink<ff::target_base*, uint64_t>& render_presented() override;
        virtual DXGI_FORMAT format() const override;
        virtual ff::window_size size() const override;
#if DXVER == 11
        virtual ID3D11Texture2D* texture() override;
        virtual ID3D11RenderTargetView* view() override;
#endif

        // graphics_child_base
        virtual bool reset() override;

    private:
#if DXVER == 11
        std::shared_ptr<ff::texture> texture_;
#endif
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view_;
        ff::signal<ff::target_base*, uint64_t> render_presented_;
        size_t array_start;
        size_t array_count;
        size_t mip_level;
    };
}
