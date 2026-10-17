#include <harrancraft/d3d11_depth.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace harrancraft::graphics {
namespace {

template<class T> class ComRef {
public:
    ComRef() = default;
    ~ComRef() { if (value_) value_->Release(); }
    ComRef(const ComRef&) = delete;
    ComRef& operator=(const ComRef&) = delete;
    T* get() const { return value_; }
    T** put() { return &value_; }
    T* take() { return std::exchange(value_, nullptr); }
    void attach(T* value) { value_ = value; }
private:
    T* value_ = nullptr;
};

DepthResult result(DepthStatus status, HRESULT error = S_OK) {
    return {status, error, std::nullopt};
}

HRESULT same_object(IUnknown* left, IUnknown* right, bool& equal) {
    equal = false;
    if (!left || !right) return E_POINTER;
    ComRef<IUnknown> a, b;
    HRESULT hr = left->QueryInterface(__uuidof(IUnknown),
        reinterpret_cast<void**>(a.put()));
    if (hr != S_OK) return hr;
    hr = right->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(b.put()));
    if (hr != S_OK) return hr;
    if (!a.get() || !b.get()) return E_UNEXPECTED;
    equal = a.get() == b.get();
    return S_OK;
}

DepthResult same_device(ID3D11Device* expected, ID3D11DeviceChild* child) {
    ComRef<ID3D11Device> device;
    child->GetDevice(device.put());
    bool equal = false;
    const HRESULT hr = same_object(expected, device.get(), equal);
    if (hr != S_OK) return result(DepthStatus::api_failure, hr);
    return result(equal ? DepthStatus::ready : DepthStatus::device_changed);
}

struct Format {
    DXGI_FORMAT typeless;
    std::uint32_t bytes;
    bool stencil = false;
};

std::optional<Format> color_format(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return Format{DXGI_FORMAT_R8G8B8A8_TYPELESS, 4};
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return Format{DXGI_FORMAT_B8G8R8A8_TYPELESS, 4};
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return Format{DXGI_FORMAT_B8G8R8X8_TYPELESS, 4};
    case DXGI_FORMAT_R10G10B10A2_UNORM: return Format{DXGI_FORMAT_R10G10B10A2_TYPELESS, 4};
    case DXGI_FORMAT_R11G11B10_FLOAT: return Format{DXGI_FORMAT_R11G11B10_FLOAT, 4};
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return Format{DXGI_FORMAT_R16G16B16A16_TYPELESS, 8};
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return Format{DXGI_FORMAT_R32G32B32A32_TYPELESS, 16};
    default: return std::nullopt;
    }
}

std::optional<Format> depth_format(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_D16_UNORM: return Format{DXGI_FORMAT_R16_TYPELESS, 2};
    case DXGI_FORMAT_D24_UNORM_S8_UINT: return Format{DXGI_FORMAT_R24G8_TYPELESS, 4, true};
    case DXGI_FORMAT_D32_FLOAT: return Format{DXGI_FORMAT_R32_TYPELESS, 4};
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return Format{DXGI_FORMAT_R32G8X24_TYPELESS, 8, true};
    default: return std::nullopt;
    }
}

bool texture_layout(const D3D11_TEXTURE2D_DESC& desc, UINT binding) {
    const UINT count = desc.SampleDesc.Count;
    return desc.Width > 0 && desc.Height > 0
        && desc.Width <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
        && desc.Height <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
        && desc.MipLevels == 1 && desc.ArraySize == 1
        && desc.Usage == D3D11_USAGE_DEFAULT && desc.CPUAccessFlags == 0
        && (desc.BindFlags & binding) != 0
        && count > 0 && count <= D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT
        && (count & (count - 1)) == 0 && desc.SampleDesc.Quality == 0;
}

bool viewport_matches(ID3D11DeviceContext* context,
    const projection::Viewport& expected, UINT width, UINT height) {
    if (expected.origin != projection::ScreenOrigin::top_left
        || expected.x < 0 || expected.y < 0
        || static_cast<std::uint64_t>(expected.x) + expected.width > width
        || static_cast<std::uint64_t>(expected.y) + expected.height > height) return false;
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> values{};
    UINT count = static_cast<UINT>(values.size());
    context->RSGetViewports(&count, values.data());
    if (count != 1) return false;
    const auto& actual = values[0];
    return actual.TopLeftX == static_cast<float>(expected.x)
        && actual.TopLeftY == static_cast<float>(expected.y)
        && actual.Width == static_cast<float>(expected.width)
        && actual.Height == static_cast<float>(expected.height)
        && actual.MinDepth == 0 && actual.MaxDepth == 1;
}

struct Binding {
    ComRef<ID3D11Device> device;
    ComRef<ID3D11RenderTargetView> color;
    ComRef<ID3D11DepthStencilView> depth;
    ComRef<ID3D11Texture2D> color_texture, depth_texture;
    D3D11_TEXTURE2D_DESC color_desc{}, depth_desc{};
    D3D11_RENDER_TARGET_VIEW_DESC color_view{};
    D3D11_DEPTH_STENCIL_VIEW_DESC depth_view{};
    std::uint64_t source_bytes = 0;
};

HRESULT texture(ID3D11View* view, ComRef<ID3D11Texture2D>& output) {
    ComRef<ID3D11Resource> resource;
    view->GetResource(resource.put());
    if (!resource.get()) return E_UNEXPECTED;
    const HRESULT hr = resource.get()->QueryInterface(__uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(output.put()));
    return hr == S_OK && !output.get() ? E_UNEXPECTED : hr;
}

DepthResult read_binding(ID3D11DeviceContext* context,
    const projection::CameraContract& camera, std::uint32_t slot,
    std::uint64_t budget, ID3D11Device* expected_device, Binding& binding) {
    if (!context || slot >= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) {
        return result(DepthStatus::invalid_argument);
    }
    if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        return result(DepthStatus::unsupported_context);
    }
    context->GetDevice(binding.device.put());
    if (!binding.device.get()) return result(DepthStatus::api_failure, E_UNEXPECTED);
    if (expected_device) {
        bool equal = false;
        const HRESULT hr = same_object(expected_device, binding.device.get(), equal);
        if (hr != S_OK) return result(DepthStatus::api_failure, hr);
        if (!equal) return result(DepthStatus::device_changed);
    }
    const HRESULT removed = binding.device.get()->GetDeviceRemovedReason();
    if (removed != S_OK) return result(DepthStatus::device_lost, removed);
    if (binding.device.get()->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) {
        return result(DepthStatus::unsupported_context);
    }
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> colors{};
    context->OMGetRenderTargets(static_cast<UINT>(colors.size()), colors.data(), binding.depth.put());
    for (std::size_t index = 0; index < colors.size(); ++index) {
        if (index == slot) binding.color.attach(colors[index]);
        else if (colors[index]) colors[index]->Release();
    }
    if (!binding.color.get() || !binding.depth.get()) return result(DepthStatus::missing_target);
    for (ID3D11DeviceChild* child : {static_cast<ID3D11DeviceChild*>(binding.color.get()),
             static_cast<ID3D11DeviceChild*>(binding.depth.get())}) {
        const auto checked = same_device(binding.device.get(), child);
        if (checked.status != DepthStatus::ready) return checked;
    }
    HRESULT hr = texture(binding.color.get(), binding.color_texture);
    if (hr != S_OK) return result(DepthStatus::unsupported_surface, hr);
    hr = texture(binding.depth.get(), binding.depth_texture);
    if (hr != S_OK) return result(DepthStatus::unsupported_surface, hr);
    binding.color_texture.get()->GetDesc(&binding.color_desc);
    binding.depth_texture.get()->GetDesc(&binding.depth_desc);
    binding.color.get()->GetDesc(&binding.color_view);
    binding.depth.get()->GetDesc(&binding.depth_view);
    const auto& color = binding.color_desc;
    const auto& depth = binding.depth_desc;
    const auto& rtv = binding.color_view;
    const auto& dsv = binding.depth_view;
    if (!texture_layout(color, D3D11_BIND_RENDER_TARGET)
        || !texture_layout(depth, D3D11_BIND_DEPTH_STENCIL)) {
        return result(DepthStatus::unsupported_surface);
    }
    if (color.Width != depth.Width || color.Height != depth.Height
        || color.SampleDesc.Count != depth.SampleDesc.Count
        || color.SampleDesc.Quality != depth.SampleDesc.Quality) {
        return result(DepthStatus::target_mismatch);
    }
    const bool multisampled = depth.SampleDesc.Count > 1;
    if ((multisampled && (rtv.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2DMS
            || dsv.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2DMS))
        || (!multisampled && (rtv.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D
            || dsv.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D
            || rtv.Texture2D.MipSlice != 0 || dsv.Texture2D.MipSlice != 0))) {
        return result(DepthStatus::unsupported_surface);
    }
    const auto color_info = color_format(rtv.Format);
    const auto depth_info = depth_format(dsv.Format);
    constexpr UINT known_flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
    if (!color_info || !depth_info
        || (color.Format != rtv.Format && color.Format != color_info->typeless)
        || (depth.Format != dsv.Format && depth.Format != depth_info->typeless)
        || (dsv.Flags & ~known_flags) != 0
        || (!depth_info->stencil && (dsv.Flags & D3D11_DSV_READ_ONLY_STENCIL) != 0)) {
        return result(DepthStatus::unsupported_surface);
    }
    for (const DXGI_FORMAT format : {rtv.Format, dsv.Format}) {
        UINT levels = 0;
        hr = binding.device.get()->CheckMultisampleQualityLevels(format, depth.SampleDesc.Count, &levels);
        if (hr != S_OK || levels == 0) return result(DepthStatus::unsupported_surface, hr);
    }
    // Single mip/slice and bounded D3D11 dimensions make this product fit uint64_t.
    binding.source_bytes = static_cast<std::uint64_t>(depth.Width) * depth.Height
        * depth.SampleDesc.Count * (color_info->bytes + depth_info->bytes);
    if (binding.source_bytes > budget) return result(DepthStatus::budget_exceeded);
    if (!viewport_matches(context, camera.viewport, depth.Width, depth.Height)) {
        return result(DepthStatus::invalid_viewport);
    }
    return result(DepthStatus::ready);
}

bool valid_camera(const projection::CameraContract& camera) {
    return projection::valid(camera)
        && camera.perspective.depth_range == projection::DepthRange::zero_to_one
        && camera.viewport.origin == projection::ScreenOrigin::top_left;
}

} // namespace

D3D11DepthAttachment::D3D11DepthAttachment(host::BuildSession& source,
    std::uint64_t source_budget)
    : owner_(std::this_thread::get_id()), source_(source), source_budget_(source_budget) {
    if (source_budget == 0 || source_budget > max_depth_source_budget) {
        throw std::invalid_argument("Invalid D3D11 source texture budget");
    }
}

D3D11DepthAttachment::~D3D11DepthAttachment() { release(); }

void D3D11DepthAttachment::check_owner() const {
    if (owner_ != std::this_thread::get_id()) {
        throw std::logic_error("D3D11 depth attachment accessed from another thread");
    }
}

void D3D11DepthAttachment::release() noexcept {
    if (depth_) std::exchange(depth_, nullptr)->Release();
    if (color_) std::exchange(color_, nullptr)->Release();
    if (device_) std::exchange(device_, nullptr)->Release();
    description_.reset();
    camera_.reset();
}

void D3D11DepthAttachment::stop() {
    check_owner();
    release();
}

bool D3D11DepthAttachment::matches_camera(const host::Evidence& evidence,
    const projection::CameraContract& camera) {
    const auto observed = source_.snapshot(evidence, camera.key);
    const auto expected = projection::perspective_matrix(camera.perspective);
    if (!observed || !expected || observed->view != camera.view
        || observed->position != camera.position || !host::finite(observed->projection)) return false;
    for (std::size_t index = 0; index < expected->size(); ++index) {
        const double scale = std::max(std::abs(observed->projection[index]), std::abs((*expected)[index]));
        if (std::abs(observed->projection[index] - (*expected)[index]) > 1e-6 * scale) return false;
    }
    return true;
}

bool D3D11DepthAttachment::follows(const projection::CameraContract& next) const {
    if (!previous_) return true;
    const auto& old = *previous_;
    if (next.key.epoch < old.key.epoch) return false;
    if (next.key.epoch > old.key.epoch) return true;
    if (next.key.world != old.key.world || next.key.frame <= old.key.frame
        || next.key.capture <= old.key.capture) return false;
    if (next.key.camera != old.key.camera) return true;
    if (next.configuration_revision < old.configuration_revision) return false;
    return next.configuration_revision > old.configuration_revision
        || (next.perspective == old.perspective && next.viewport == old.viewport
            && next.max_position_error == old.max_position_error);
}

DepthResult D3D11DepthAttachment::prepare(const host::Evidence& evidence,
    const projection::CameraContract& camera, std::uint64_t generation,
    ID3D11DeviceContext* context, std::uint32_t color_slot) {
    check_owner();
    release();
    if (generation == 0) return result(DepthStatus::invalid_argument);
    if (!valid_camera(camera)) return result(DepthStatus::invalid_camera);
    if (generation <= last_generation_ || !follows(camera)) return result(DepthStatus::stale_capture);
    if (!matches_camera(evidence, camera)) return result(DepthStatus::stale_capture);
    Binding binding;
    const auto checked = read_binding(context, camera, color_slot, source_budget_, nullptr, binding);
    if (checked.status != DepthStatus::ready) return checked;
    auto descriptor = binding.depth_view;
    descriptor.Flags = D3D11_DSV_READ_ONLY_DEPTH;
    if (depth_format(descriptor.Format)->stencil) descriptor.Flags |= D3D11_DSV_READ_ONLY_STENCIL;
    ComRef<ID3D11DepthStencilView> readonly;
    const HRESULT hr = binding.device.get()->CreateDepthStencilView(binding.depth_texture.get(),
        &descriptor, readonly.put());
    const HRESULT removed = binding.device.get()->GetDeviceRemovedReason();
    if (removed != S_OK) return result(DepthStatus::device_lost, removed);
    if (hr != S_OK || !readonly.get()) {
        return result(DepthStatus::api_failure, hr == S_OK ? E_UNEXPECTED : hr);
    }
    const DepthDescription description{
        {depth_contract_version, generation, camera.key, camera.configuration_revision, color_slot},
        binding.depth_desc.Width, binding.depth_desc.Height,
        binding.depth_desc.SampleDesc.Count, binding.depth_desc.SampleDesc.Quality,
        binding.color_view.Format, binding.depth_view.Format, binding.source_bytes,
        camera.perspective.reverse_z ? D3D11_COMPARISON_GREATER_EQUAL : D3D11_COMPARISON_LESS_EQUAL};
    device_ = binding.device.take();
    color_ = binding.color.take();
    depth_ = readonly.take();
    camera_ = camera;
    previous_ = camera;
    description_ = description;
    last_generation_ = generation;
    return {DepthStatus::ready, S_OK, description};
}

DepthAccess D3D11DepthAttachment::access(const DepthKey& key, const host::Evidence& evidence,
    const projection::CameraContract& camera, ID3D11DeviceContext* context) {
    check_owner();
    const auto failed = [this](DepthResult failure) {
        release();
        return DepthAccess{failure, nullptr};
    };
    if (!description_) return {result(DepthStatus::inactive), nullptr};
    try {
        if (key != description_->key) return failed(result(DepthStatus::stale_capture));
        if (!camera_ || camera != *camera_) return failed(result(DepthStatus::invalid_camera));
        if (!matches_camera(evidence, camera)) return failed(result(DepthStatus::stale_capture));
        Binding binding;
        const auto checked = read_binding(context, camera, key.color_slot, source_budget_, device_, binding);
        if (checked.status != DepthStatus::ready) return failed(checked);
        bool equal = false;
        HRESULT hr = same_object(color_, binding.color.get(), equal);
        if (hr != S_OK) return failed(result(DepthStatus::api_failure, hr));
        if (!equal) return failed(result(DepthStatus::target_mismatch));
        ComRef<ID3D11Resource> source;
        depth_->GetResource(source.put());
        hr = same_object(source.get(), binding.depth_texture.get(), equal);
        if (hr != S_OK) return failed(result(DepthStatus::api_failure, hr));
        D3D11_DEPTH_STENCIL_VIEW_DESC saved{};
        depth_->GetDesc(&saved);
        // Read-only flags may differ when a consumer binds the returned view.
        if (!equal || saved.Format != binding.depth_view.Format
            || saved.ViewDimension != binding.depth_view.ViewDimension
            || (saved.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2D
                && saved.Texture2D.MipSlice != binding.depth_view.Texture2D.MipSlice)) {
            return failed(result(DepthStatus::target_mismatch));
        }
        const HRESULT removed = device_->GetDeviceRemovedReason();
        if (removed != S_OK) return failed(result(DepthStatus::device_lost, removed));
        return {{DepthStatus::ready, S_OK, description_}, depth_};
    } catch (...) {
        release();
        throw;
    }
}

} // namespace harrancraft::graphics
