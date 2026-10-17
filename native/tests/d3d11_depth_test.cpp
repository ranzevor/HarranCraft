#include <harrancraft/d3d11_depth.hpp>

#include <d3dcompiler.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace graphics = harrancraft::graphics;
namespace host = harrancraft::host;
namespace projection = harrancraft::projection;
using graphics::D3D11DepthAttachment;
using graphics::DepthAccess;
using graphics::DepthDescription;
using graphics::DepthKey;
using graphics::DepthResult;
using graphics::DepthStatus;

namespace {

std::size_t assertions = 0;

void require(bool condition, std::string_view reason) {
    ++assertions;
    if (!condition) throw std::runtime_error(std::string(reason));
}

std::string hresult_text(HRESULT value) {
    std::ostringstream text;
    text << "0x" << std::hex << static_cast<std::uint32_t>(value);
    return text.str();
}

void checked(HRESULT value, std::string_view operation) {
    require(SUCCEEDED(value), std::string(operation) + ": " + hresult_text(value));
}

const char* status_name(DepthStatus value) {
    switch (value) {
    case DepthStatus::ready: return "ready";
    case DepthStatus::inactive: return "inactive";
    case DepthStatus::invalid_argument: return "invalid_argument";
    case DepthStatus::invalid_camera: return "invalid_camera";
    case DepthStatus::stale_capture: return "stale_capture";
    case DepthStatus::unsupported_context: return "unsupported_context";
    case DepthStatus::missing_target: return "missing_target";
    case DepthStatus::unsupported_surface: return "unsupported_surface";
    case DepthStatus::target_mismatch: return "target_mismatch";
    case DepthStatus::invalid_viewport: return "invalid_viewport";
    case DepthStatus::budget_exceeded: return "budget_exceeded";
    case DepthStatus::device_changed: return "device_changed";
    case DepthStatus::device_lost: return "device_lost";
    case DepthStatus::api_failure: return "api_failure";
    }
    return "unknown";
}

void expect_status(const DepthResult& result, DepthStatus expected, std::string_view reason) {
    require(result.status == expected, std::string(reason) + ": expected "
        + status_name(expected) + ", received " + status_name(result.status)
        + " (" + hresult_text(result.error) + ")");
    require(result.description.has_value() == (expected == DepthStatus::ready),
        std::string(reason) + ": description lifetime disagrees with status");
}

DepthDescription ready(const DepthResult& result, std::string_view reason) {
    expect_status(result, DepthStatus::ready, reason);
    require(result.error == S_OK, "A successful attachment retained an HRESULT failure");
    return *result.description;
}

ID3D11DepthStencilView* ready(const DepthAccess& access, std::string_view reason) {
    static_cast<void>(ready(access.result, reason));
    require(access.view != nullptr, std::string(reason) + ": missing borrowed depth view");
    return access.view;
}

void rejected(const DepthAccess& access, DepthStatus status, std::string_view reason) {
    expect_status(access.result, status, reason);
    require(access.view == nullptr, std::string(reason) + ": failed access exposed a view");
}

template<class T> class Com {
public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    Com(Com&& other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}
    Com& operator=(Com&& other) noexcept {
        if (this != &other) reset(std::exchange(other.pointer_, nullptr));
        return *this;
    }
    T* get() const { return pointer_; }
    T* operator->() const { return pointer_; }
    T** put() { reset(); return &pointer_; }
    void reset(T* pointer = nullptr) {
        if (pointer_) pointer_->Release();
        pointer_ = pointer;
    }
private:
    T* pointer_ = nullptr;
};

bool same_object(IUnknown* left, IUnknown* right) {
    if (!left || !right) return left == right;
    Com<IUnknown> first, second;
    checked(left->QueryInterface(__uuidof(IUnknown),
        reinterpret_cast<void**>(first.put())), "Query first COM identity");
    checked(right->QueryInterface(__uuidof(IUnknown),
        reinterpret_cast<void**>(second.put())), "Query second COM identity");
    return first.get() == second.get();
}

struct Device {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    explicit Device(D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0) {
        D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_9_1;
        checked(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, &requested, 1, D3D11_SDK_VERSION,
            device.put(), &obtained, context.put()), "Create actual WARP device");
        require(obtained == requested, "WARP did not create the requested feature level");
        require(context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE,
            "WARP returned a deferred context");
    }
    ~Device() {
        if (context.get()) {
            context->ClearState();
            context->Flush();
        }
    }
};

host::ImageIdentity image(std::uint8_t seed) {
    host::ImageIdentity result;
    result.sha256[0] = seed;
    result.file_bytes = 4096 + static_cast<std::uint64_t>(seed);
    result.machine = 0x8664;
    return result;
}

host::BuildImage build() {
    return {image(1), {{"engine_x64_rwdi.dll", image(2)},
        {"gamedll_x64_rwdi.dll", image(3)}}};
}

projection::CameraContract camera(bool reverse) {
    return {{}, 1, projection::identity_matrix(), {0, 0, 0},
        {projection::contract_version, std::numbers::pi / 2, 1, 1, 10,
            projection::Handedness::left, projection::DepthRange::zero_to_one, reverse},
        {0, 0, 64, 64, projection::ScreenOrigin::top_left}, 1e-6};
}

// Synthetic fingerprints and supplied phase evidence exercise only the WARP fixture's
// BuildSession gate; they do not measure executable files or establish a game read phase.
struct Host {
    host::BuildImage profile = build();
    host::BuildSession session{{profile}};
    host::Evidence evidence{host::Mode::single_player, host::Lifecycle::playing,
        host::ReadPhase::verified_engine_read, 17};
    projection::CameraContract current;
    explicit Host(bool reverse = false) : current(camera(reverse)) { restart(17); }
    host::CameraSample sample() const {
        const auto matrix = projection::perspective_matrix(current.perspective);
        require(matrix.has_value(), "Fixture perspective was invalid");
        return {current.key, current.view, *matrix, current.position};
    }
    void publish() {
        require(session.capture(evidence, sample()), "BuildSession rejected fixture capture");
    }
    void advance() {
        ++current.key.frame;
        ++current.key.capture;
        publish();
    }
    void restart(std::uint64_t world) {
        evidence = {host::Mode::single_player, host::Lifecycle::playing,
            host::ReadPhase::verified_engine_read, world};
        const auto epoch = session.begin(profile, evidence);
        require(epoch.has_value(), "BuildSession rejected fixture build");
        current = camera(current.perspective.reverse_z);
        current.key = {*epoch, world, 9, 1, 1};
        publish();
    }
};

struct SurfaceOptions {
    UINT width = 64, height = 64;
    DXGI_FORMAT color = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depth = DXGI_FORMAT_D32_FLOAT;
    UINT samples = 1, quality = 0, mips = 1, slices = 1;
    bool typed_depth = false;
    bool typeless_color = false;
};

DXGI_FORMAT depth_storage(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
    case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24G8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32G8X24_TYPELESS;
    default: throw std::invalid_argument("Unsupported fixture depth format");
    }
}

struct Surface {
    Com<ID3D11Texture2D> color, depth;
    Com<ID3D11RenderTargetView> rtv;
    Com<ID3D11DepthStencilView> dsv;
};

Surface surface(ID3D11Device* device, const SurfaceOptions& options = {}) {
    Surface result;
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = options.width;
    texture.Height = options.height;
    texture.MipLevels = options.mips;
    texture.ArraySize = options.slices;
    texture.Format = options.typeless_color ? DXGI_FORMAT_R8G8B8A8_TYPELESS : options.color;
    texture.SampleDesc = {options.samples, options.quality};
    texture.Usage = D3D11_USAGE_DEFAULT;
    texture.BindFlags = D3D11_BIND_RENDER_TARGET;
    checked(device->CreateTexture2D(&texture, nullptr, result.color.put()), "Create color texture");
    texture.Format = options.typed_depth ? options.depth : depth_storage(options.depth);
    texture.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    checked(device->CreateTexture2D(&texture, nullptr, result.depth.put()), "Create depth texture");
    D3D11_RENDER_TARGET_VIEW_DESC rtv{};
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
    rtv.Format = options.color;
    dsv.Format = options.depth;
    if (options.slices > 1) {
        require(options.samples == 1, "Fixture does not construct multisampled arrays");
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv.Texture2DArray.ArraySize = 1;
        dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv.Texture2DArray.ArraySize = 1;
    } else if (options.samples > 1) {
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
        dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
    } else {
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    }
    checked(device->CreateRenderTargetView(result.color.get(), &rtv, result.rtv.put()),
        "Create actual render target view");
    checked(device->CreateDepthStencilView(result.depth.get(), &dsv, result.dsv.put()),
        "Create actual writable depth view");
    return result;
}

D3D11_VIEWPORT viewport(const projection::Viewport& expected) {
    return {static_cast<float>(expected.x), static_cast<float>(expected.y),
        static_cast<float>(expected.width), static_cast<float>(expected.height), 0, 1};
}

void bind(ID3D11DeviceContext* context, const Surface& source,
    const projection::CameraContract& expected, UINT slot = 0,
    ID3D11DepthStencilView* depth = nullptr) {
    require(slot < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, "Fixture render slot was invalid");
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> colors{};
    colors[slot] = source.rtv.get();
    context->OMSetRenderTargets(slot + 1, colors.data(), depth ? depth : source.dsv.get());
    const auto actual = viewport(expected.viewport);
    context->RSSetViewports(1, &actual);
}

struct State {
    std::array<Com<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> colors;
    Com<ID3D11DepthStencilView> depth;
    Com<ID3D11DepthStencilState> depth_state;
    Com<ID3D11BlendState> blend;
    Com<ID3D11RasterizerState> raster;
    Com<ID3D11VertexShader> vertex;
    Com<ID3D11PixelShader> pixel;
    Com<ID3D11Buffer> constants;
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
    UINT viewport_count = static_cast<UINT>(viewports.size());
    UINT stencil = 0, sample_mask = 0;
    std::array<float, 4> blend_factor{};
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    explicit State(ID3D11DeviceContext* context) {
        std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> raw{};
        context->OMGetRenderTargets(static_cast<UINT>(raw.size()), raw.data(), depth.put());
        for (std::size_t index = 0; index < raw.size(); ++index) colors[index].reset(raw[index]);
        context->OMGetDepthStencilState(depth_state.put(), &stencil);
        context->OMGetBlendState(blend.put(), blend_factor.data(), &sample_mask);
        context->RSGetState(raster.put());
        context->RSGetViewports(&viewport_count, viewports.data());
        context->VSGetShader(vertex.put(), nullptr, nullptr);
        context->PSGetShader(pixel.put(), nullptr, nullptr);
        context->VSGetConstantBuffers(0, 1, constants.put());
        context->IAGetPrimitiveTopology(&topology);
    }
    void unchanged(ID3D11DeviceContext* context, std::string_view reason) const {
        const State after(context);
        for (std::size_t index = 0; index < colors.size(); ++index) {
            require(same_object(colors[index].get(), after.colors[index].get()), reason);
        }
        require(same_object(depth.get(), after.depth.get())
            && same_object(depth_state.get(), after.depth_state.get())
            && same_object(blend.get(), after.blend.get())
            && same_object(raster.get(), after.raster.get())
            && same_object(vertex.get(), after.vertex.get())
            && same_object(pixel.get(), after.pixel.get())
            && same_object(constants.get(), after.constants.get()), reason);
        require(stencil == after.stencil && sample_mask == after.sample_mask
            && blend_factor == after.blend_factor && topology == after.topology
            && viewport_count == after.viewport_count, reason);
        require(viewport_count <= viewports.size(), "Unexpected D3D11 viewport count");
        for (std::size_t index = 0; index < viewport_count; ++index) {
            const auto& a = viewports[index];
            const auto& b = after.viewports[index];
            require(a.TopLeftX == b.TopLeftX && a.TopLeftY == b.TopLeftY
                && a.Width == b.Width && a.Height == b.Height
                && a.MinDepth == b.MinDepth && a.MaxDepth == b.MaxDepth, reason);
        }
    }
};

Com<ID3DBlob> shader(std::string_view entry, std::string_view target) {
    constexpr char source[] = R"(
cbuffer Parameters : register(b0) { float4 rectangle; float4 color; float depth; float3 padding; };
struct Vertex { float4 position : SV_Position; float4 color : COLOR0; };
Vertex vs_main(uint index : SV_VertexID) {
    const float2 corners[6] = { float2(0,0), float2(1,0), float2(0,1),
        float2(0,1), float2(1,0), float2(1,1) };
    Vertex output;
    output.position = float4(lerp(rectangle.xy, rectangle.zw, corners[index]), depth, 1);
    output.color = color;
    return output;
}
float4 ps_main(Vertex input) : SV_Target0 { return input.color; }
)";
    Com<ID3DBlob> bytecode, diagnostics;
    const HRESULT hr = D3DCompile(source, sizeof(source) - 1, "d3d11-depth-test.hlsl",
        nullptr, nullptr, std::string(entry).c_str(), std::string(target).c_str(),
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        bytecode.put(), diagnostics.put());
    if (FAILED(hr)) {
        const std::string details = diagnostics.get()
            ? std::string(static_cast<const char*>(diagnostics->GetBufferPointer()),
                diagnostics->GetBufferSize()) : std::string();
        throw std::runtime_error("Compile WARP test shader: " + hresult_text(hr) + " " + details);
    }
    require(bytecode.get() != nullptr, "Shader compiler returned no bytecode");
    return bytecode;
}

struct alignas(16) Parameters {
    std::array<float, 4> rectangle;
    std::array<float, 4> color;
    float depth;
    std::array<float, 3> padding{};
};
static_assert(sizeof(Parameters) == 48);

struct Program {
    Com<ID3D11VertexShader> vertex;
    Com<ID3D11PixelShader> pixel;
    Com<ID3D11Buffer> constants;
    Com<ID3D11RasterizerState> raster;
    explicit Program(ID3D11Device* device) {
        const auto vs = shader("vs_main", "vs_5_0");
        const auto ps = shader("ps_main", "ps_5_0");
        checked(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
            nullptr, vertex.put()), "Create WARP vertex shader");
        checked(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
            nullptr, pixel.put()), "Create WARP pixel shader");
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = static_cast<UINT>(sizeof(Parameters));
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        checked(device->CreateBuffer(&buffer, nullptr, constants.put()), "Create test constants");
        D3D11_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D11_FILL_SOLID;
        rasterizer.CullMode = D3D11_CULL_NONE;
        rasterizer.DepthClipEnable = TRUE;
        checked(device->CreateRasterizerState(&rasterizer, raster.put()), "Create test rasterizer");
    }
    void install(ID3D11DeviceContext* context) const {
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.get(), nullptr, 0);
        context->PSSetShader(pixel.get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        ID3D11Buffer* buffer = constants.get();
        context->VSSetConstantBuffers(0, 1, &buffer);
        context->RSSetState(raster.get());
        context->OMSetBlendState(nullptr, nullptr, std::numeric_limits<UINT>::max());
    }
    void draw(ID3D11DeviceContext* context, const Parameters& parameters) const {
        context->UpdateSubresource(constants.get(), 0, nullptr, &parameters, 0, 0);
        context->Draw(6, 0);
    }
};

Com<ID3D11DepthStencilState> depth_state(ID3D11Device* device,
    D3D11_COMPARISON_FUNC comparison, bool writable) {
    D3D11_DEPTH_STENCIL_DESC descriptor{};
    descriptor.DepthEnable = TRUE;
    descriptor.DepthWriteMask = writable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    descriptor.DepthFunc = comparison;
    descriptor.StencilEnable = FALSE;
    descriptor.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    descriptor.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
    descriptor.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP,
        D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};
    descriptor.BackFace = descriptor.FrontFace;
    Com<ID3D11DepthStencilState> result;
    checked(device->CreateDepthStencilState(&descriptor, result.put()), "Create depth test state");
    return result;
}

std::vector<std::uint8_t> read_texture(Device& device, ID3D11Texture2D* texture, UINT bytes) {
    D3D11_TEXTURE2D_DESC descriptor{};
    texture->GetDesc(&descriptor);
    require(descriptor.SampleDesc.Count == 1 && descriptor.MipLevels == 1
        && descriptor.ArraySize == 1, "Readback requires a single subresource without MSAA");
    descriptor.Usage = D3D11_USAGE_STAGING;
    descriptor.BindFlags = 0;
    descriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    descriptor.MiscFlags = 0;
    Com<ID3D11Texture2D> staging;
    checked(device.device->CreateTexture2D(&descriptor, nullptr, staging.put()),
        "Create actual staging readback texture");
    const auto row_bytes = static_cast<std::size_t>(descriptor.Width) * bytes;
    std::vector<std::uint8_t> result(row_bytes * descriptor.Height);
    device.context->CopyResource(staging.get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    checked(device.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped),
        "Map actual WARP readback");
    struct Unmap {
        ID3D11DeviceContext* context;
        ID3D11Texture2D* texture;
        ~Unmap() { context->Unmap(texture, 0); }
    } unmap{device.context.get(), staging.get()};
    require(mapped.pData != nullptr && mapped.RowPitch >= row_bytes,
        "Readback mapping omitted required row storage");
    const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
    for (std::size_t row = 0; row < descriptor.Height; ++row) {
        std::memcpy(result.data() + row * row_bytes,
            source + row * mapped.RowPitch, row_bytes);
    }
    return result;
}

float projected_depth(const projection::CameraContract& value, double distance) {
    const auto matrix = projection::perspective_matrix(value.perspective);
    require(matrix.has_value(), "P2 did not produce a perspective matrix");
    const auto clip = host::transform(*matrix, {0, 0, distance, 1});
    const auto screen = projection::screen_point(clip, value.perspective, value.viewport);
    require(screen && screen->inside, "P2 rejected a visible depth test point");
    return static_cast<float>(screen->depth);
}

void occlusion(Device& device, const Program& program, bool reverse) {
    device.context->ClearState();
    Host source(reverse);
    auto targets = surface(device.device.get());
    bind(device.context.get(), targets, source.current);
    program.install(device.context.get());
    const auto comparison = reverse ? D3D11_COMPARISON_GREATER_EQUAL : D3D11_COMPARISON_LESS_EQUAL;
    const auto writable = depth_state(device.device.get(), comparison, true);
    const auto readonly = depth_state(device.device.get(), comparison, false);
    device.context->OMSetDepthStencilState(writable.get(), 37);
    constexpr std::array<float, 4> black{0, 0, 0, 1};
    device.context->ClearRenderTargetView(targets.rtv.get(), black.data());
    device.context->ClearDepthStencilView(targets.dsv.get(), D3D11_CLEAR_DEPTH, reverse ? 0.0F : 1.0F, 0);
    const float wall = projected_depth(source.current, 4);
    const float far_depth = projected_depth(source.current, 6);
    const float near_depth = projected_depth(source.current, 2);
    require(reverse ? (near_depth > wall && wall > far_depth)
        : (near_depth < wall && wall < far_depth),
        "P2 depth ordering disagrees with depth comparison");
    program.draw(device.context.get(), {{-1, -1, 0, 1}, {0, 0, 1, 1}, wall, {}});
    device.context->OMSetRenderTargets(0, nullptr, nullptr);
    const auto depth_before = read_texture(device, targets.depth.get(), 4);
    float wall_sample = 0, clear_sample = 0;
    std::memcpy(&wall_sample, depth_before.data() + (32 * 64 + 4) * 4, sizeof(float));
    std::memcpy(&clear_sample, depth_before.data() + (32 * 64 + 60) * 4, sizeof(float));
    require(std::abs(wall_sample - wall) < 1e-6F,
        "Host wall did not write actual WARP depth");
    require(clear_sample == (reverse ? 0.0F : 1.0F), "Clear depth changed outside the host wall");
    bind(device.context.get(), targets, source.current);
    D3D11DepthAttachment attachment(source.session);
    DepthDescription description;
    {
        const State before(device.context.get());
        description = ready(attachment.prepare(source.evidence, source.current, 1,
            device.context.get()), "Prepare actual wall depth");
        before.unchanged(device.context.get(), "Prepare changed existing graphics state");
        require(description.comparison == comparison, "Attachment selected the wrong Z comparison");
        const auto access = attachment.access(description.key, source.evidence, source.current,
            device.context.get());
        static_cast<void>(ready(access, "Access actual wall depth"));
        before.unchanged(device.context.get(), "Access changed existing graphics state");
        device.context->OMSetDepthStencilState(readonly.get(), 37);
        bind(device.context.get(), targets, source.current, 0, access.view);
    }
    static_cast<void>(ready(attachment.access(description.key, source.evidence, source.current,
        device.context.get()), "Accept caller binding of borrowed readonly view"));
    program.draw(device.context.get(), {{-1, -1, 1, 1}, {1, 0, 0, 1}, far_depth, {}});
    program.draw(device.context.get(), {{-0.75F, -0.5F, -0.25F, 0.5F}, {0, 1, 0, 1}, near_depth, {}});
    device.context->OMSetRenderTargets(0, nullptr, nullptr);
    const auto pixels = read_texture(device, targets.color.get(), 4);
    const auto depth_after = read_texture(device, targets.depth.get(), 4);
    require(depth_before == depth_after, "Readonly composition modified shared depth bytes");
    for (std::size_t y = 0; y < 64; ++y) {
        for (std::size_t x = 0; x < 64; ++x) {
            const bool green = x >= 8 && x < 24 && y >= 16 && y < 48;
            const std::array<std::uint8_t, 4> expected = green
                ? std::array<std::uint8_t, 4>{0, 255, 0, 255}
                : x < 32 ? std::array<std::uint8_t, 4>{0, 0, 255, 255}
                         : std::array<std::uint8_t, 4>{255, 0, 0, 255};
            const auto offset = (y * 64 + x) * 4;
            for (std::size_t channel = 0; channel < expected.size(); ++channel) {
                require(pixels[offset + channel] == expected[channel],
                    std::string(reverse ? "Reverse" : "Normal") + " Z pixel oracle at "
                    + std::to_string(x) + "," + std::to_string(y)
                    + " channel " + std::to_string(channel));
            }
        }
    }
    attachment.stop();
    device.context->ClearState();
    std::cout << (reverse ? "reverse" : "normal")
        << "_z: 4096 RGBA pixels matched; 16384 shared depth bytes unchanged\n";
}

void formats_and_samples(Device& device) {
    struct FormatCase { DXGI_FORMAT format; std::uint64_t bytes; bool stencil; };
    constexpr std::array<FormatCase, 4> formats{{
        {DXGI_FORMAT_D16_UNORM, 2, false}, {DXGI_FORMAT_D24_UNORM_S8_UINT, 4, true},
        {DXGI_FORMAT_D32_FLOAT, 4, false}, {DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 8, true}}};
    for (const auto& format : formats) {
        for (const bool typed : {false, true}) {
            device.context->ClearState();
            Host source;
            SurfaceOptions options;
            options.depth = format.format;
            options.typed_depth = typed;
            auto targets = surface(device.device.get(), options);
            bind(device.context.get(), targets, source.current);
            D3D11DepthAttachment attachment(source.session);
            const State before(device.context.get());
            const auto description = ready(attachment.prepare(source.evidence, source.current, 1,
                device.context.get()), "Accept supported actual depth storage");
            require(description.width == 64 && description.height == 64
                && description.samples == 1 && description.quality == 0,
                "Attachment reported incorrect surface dimensions");
            require(description.color_format == DXGI_FORMAT_R8G8B8A8_UNORM
                && description.depth_format == format.format
                && description.source_bytes == 64 * 64 * (4 + format.bytes),
                "Attachment reported incorrect typed format or source storage budget");
            const auto access = attachment.access(description.key, source.evidence, source.current,
                device.context.get());
            D3D11_DEPTH_STENCIL_VIEW_DESC actual{};
            ready(access, "Access each typed depth format")->GetDesc(&actual);
            const UINT flags = D3D11_DSV_READ_ONLY_DEPTH
                | (format.stencil ? static_cast<UINT>(D3D11_DSV_READ_ONLY_STENCIL) : 0U);
            require(actual.Format == format.format && actual.Flags == flags
                && actual.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2D
                && actual.Texture2D.MipSlice == 0, "Readonly DSV descriptor was incorrect");
            Com<ID3D11Resource> resource;
            access.view->GetResource(resource.put());
            require(same_object(resource.get(), targets.depth.get()),
                "Readonly view changed the original depth resource");
            before.unchanged(device.context.get(), "Depth format inspection changed context state");
            const auto readonly_state = depth_state(device.device.get(), description.comparison, false);
            device.context->OMSetDepthStencilState(readonly_state.get(), 0);
            bind(device.context.get(), targets, source.current, 0, access.view);
            static_cast<void>(ready(attachment.access(description.key, source.evidence, source.current,
                device.context.get()), "Accept matching readonly DSV binding"));
            bind(device.context.get(), targets, source.current);
            static_cast<void>(ready(attachment.access(description.key, source.evidence, source.current,
                device.context.get()), "Accept original writable DSV binding again"));
            attachment.stop();
        }
    }
    for (const auto& color : std::array<FormatCase, 3>{{
             {DXGI_FORMAT_B8G8R8A8_UNORM, 4, false},
             {DXGI_FORMAT_R16G16B16A16_FLOAT, 8, false},
             {DXGI_FORMAT_R32G32B32A32_FLOAT, 16, false}}}) {
        device.context->ClearState();
        Host source;
        SurfaceOptions options;
        options.color = color.format;
        auto targets = surface(device.device.get(), options);
        bind(device.context.get(), targets, source.current);
        D3D11DepthAttachment attachment(source.session);
        const auto description = ready(attachment.prepare(source.evidence, source.current, 1,
            device.context.get()), "Accept supported color attachment format");
        require(description.color_format == color.format
            && description.source_bytes == 64 * 64 * (color.bytes + 4),
            "Color format byte accounting was incorrect");
    }
    device.context->ClearState();
    Host source;
    for (const DXGI_FORMAT format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT}) {
        UINT levels = 0;
        checked(device.device->CheckMultisampleQualityLevels(format, 4, &levels),
            "Query actual WARP 4x MSAA support");
        require(levels > 0, "WARP FL11_0 lacked required 4x MSAA support");
    }
    SurfaceOptions multisample;
    multisample.samples = 4;
    auto targets = surface(device.device.get(), multisample);
    bind(device.context.get(), targets, source.current);
    D3D11DepthAttachment attachment(source.session);
    auto description = ready(attachment.prepare(source.evidence, source.current, 1,
        device.context.get()), "Accept actual 4x MSAA quality zero");
    require(description.samples == 4 && description.quality == 0
        && description.source_bytes == 64 * 64 * 8 * 4, "MSAA storage accounting was incorrect");
    D3D11_DEPTH_STENCIL_VIEW_DESC actual{};
    ready(attachment.access(description.key, source.evidence, source.current, device.context.get()),
        "Access actual multisampled depth")->GetDesc(&actual);
    require(actual.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DMS
        && actual.Flags == D3D11_DSV_READ_ONLY_DEPTH, "MSAA readonly view dimension was incorrect");
    source.advance();
    multisample.quality = D3D11_STANDARD_MULTISAMPLE_PATTERN;
    targets = surface(device.device.get(), multisample);
    bind(device.context.get(), targets, source.current);
    expect_status(attachment.prepare(source.evidence, source.current, 2, device.context.get()),
        DepthStatus::unsupported_surface, "Reject valid but unsupported nonzero MSAA quality");
    multisample.quality = 0;
    targets = surface(device.device.get(), multisample);
    bind(device.context.get(), targets, source.current);
    static_cast<void>(ready(attachment.prepare(source.evidence, source.current, 2,
        device.context.get()), "Recover same frame and generation after unsupported MSAA"));
    device.context->ClearState();
}

void budgets_and_layouts(Device& device) {
    device.context->ClearState();
    Host source;
    auto targets = surface(device.device.get());
    bind(device.context.get(), targets, source.current);
    for (const auto budget : {std::uint64_t{0}, graphics::max_depth_source_budget + 1}) {
        bool threw = false;
        try { D3D11DepthAttachment attachment(source.session, budget); }
        catch (const std::invalid_argument&) { threw = true; }
        require(threw, "Invalid source budget did not reject construction");
    }
    D3D11DepthAttachment small(source.session, 32767);
    expect_status(small.prepare(source.evidence, source.current, 1, device.context.get()),
        DepthStatus::budget_exceeded, "Reject one byte below actual selected texture storage");
    D3D11DepthAttachment exact(source.session, 32768);
    static_cast<void>(ready(exact.prepare(source.evidence, source.current, 1,
        device.context.get()), "Accept exact source byte budget"));
    D3D11DepthAttachment maximum(source.session, graphics::max_depth_source_budget);
    static_cast<void>(ready(maximum.prepare(source.evidence, source.current, 1,
        device.context.get()), "Accept maximum permitted budget"));
    for (UINT variant = 0; variant < 3; ++variant) {
        source.advance();
        SurfaceOptions options;
        if (variant == 0) options.mips = 2;
        if (variant == 1) options.slices = 2;
        if (variant == 2) options.color = DXGI_FORMAT_R32_FLOAT;
        targets = surface(device.device.get(), options);
        bind(device.context.get(), targets, source.current);
        D3D11DepthAttachment attachment(source.session);
        const State before(device.context.get());
        expect_status(attachment.prepare(source.evidence, source.current, 1, device.context.get()),
            DepthStatus::unsupported_surface, "Reject actual unsupported layout or color format");
        before.unchanged(device.context.get(), "Rejected layout changed current bindings");
    }
    device.context->ClearState();
}

void contexts_and_bindings(Device& device) {
    device.context->ClearState();
    Host source;
    auto targets = surface(device.device.get());
    auto extra = surface(device.device.get());
    bind(device.context.get(), targets, source.current, 7);
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> colors{};
    colors[0] = extra.rtv.get();
    colors[7] = targets.rtv.get();
    device.context->OMSetRenderTargets(static_cast<UINT>(colors.size()), colors.data(), targets.dsv.get());
    D3D11DepthAttachment attachment(source.session);
    auto description = ready(attachment.prepare(source.evidence, source.current, 1,
        device.context.get(), 7), "Accept selected color slot seven among multiple targets");
    require(description.key.color_slot == 7 && description.source_bytes == 32768,
        "Selected slot or selected source storage accounting was incorrect");
    {
        const State before(device.context.get());
        static_cast<void>(ready(attachment.access(description.key, source.evidence, source.current,
            device.context.get()), "Access color slot seven"));
        before.unchanged(device.context.get(), "Access changed an unselected render target");
    }
    colors[0] = nullptr;
    device.context->OMSetRenderTargets(static_cast<UINT>(colors.size()), colors.data(), targets.dsv.get());
    static_cast<void>(ready(attachment.access(description.key, source.evidence, source.current,
        device.context.get()), "Changing an unselected slot retired the selected attachment"));
    source.advance();
    bind(device.context.get(), targets, source.current);
    expect_status(attachment.prepare(source.evidence, source.current, 2, device.context.get(), 7),
        DepthStatus::missing_target, "Reject an unbound selected color slot");
    expect_status(attachment.prepare(source.evidence, source.current, 2, device.context.get(), 8),
        DepthStatus::invalid_argument, "Reject color slot eight");
    expect_status(attachment.prepare(source.evidence, source.current, 2, nullptr),
        DepthStatus::invalid_argument, "Reject null context");
    Com<ID3D11DeviceContext> deferred;
    checked(device.device->CreateDeferredContext(0, deferred.put()), "Create actual deferred context");
    expect_status(attachment.prepare(source.evidence, source.current, 2, deferred.get()),
        DepthStatus::unsupported_context, "Reject actual deferred context before observing its state");
    Device lower(D3D_FEATURE_LEVEL_10_0);
    expect_status(attachment.prepare(source.evidence, source.current, 2, lower.context.get()),
        DepthStatus::unsupported_context, "Reject actual feature level below eleven");
    device.context->OMSetRenderTargets(0, nullptr, targets.dsv.get());
    expect_status(attachment.prepare(source.evidence, source.current, 2, device.context.get()),
        DepthStatus::missing_target, "Reject depth without color");
    ID3D11RenderTargetView* color = targets.rtv.get();
    device.context->OMSetRenderTargets(1, &color, nullptr);
    expect_status(attachment.prepare(source.evidence, source.current, 2, device.context.get()),
        DepthStatus::missing_target, "Reject color without depth");
    bind(device.context.get(), targets, source.current);
    description = ready(attachment.prepare(source.evidence, source.current, 2,
        device.context.get()), "Recover same capture after failed context and binding attempts");
    Device foreign;
    rejected(attachment.access(description.key, source.evidence, source.current, foreign.context.get()),
        DepthStatus::device_changed, "Reject another actual device before reading its empty bindings");
    rejected(attachment.access(description.key, source.evidence, source.current, device.context.get()),
        DepthStatus::inactive, "Foreign device failure retained an attachment");
    source.advance();
    auto foreign_targets = surface(foreign.device.get());
    bind(foreign.context.get(), foreign_targets, source.current);
    static_cast<void>(ready(attachment.prepare(source.evidence, source.current, 3,
        foreign.context.get()), "Accept a new device only with fresh capture and generation"));
    attachment.stop();

    for (UINT replacement = 0; replacement < 3; ++replacement) {
        device.context->ClearState();
        Host local;
        SurfaceOptions options;
        options.typeless_color = true;
        auto original = surface(device.device.get(), options);
        auto changed = surface(device.device.get());
        bind(device.context.get(), original, local.current);
        D3D11DepthAttachment candidate(local.session);
        const auto original_description = ready(candidate.prepare(local.evidence, local.current, 1,
            device.context.get()), "Prepare target identity fixture");
        Com<ID3D11RenderTargetView> alternate;
        ID3D11RenderTargetView* selected = original.rtv.get();
        ID3D11DepthStencilView* depth = original.dsv.get();
        if (replacement == 0) selected = changed.rtv.get();
        if (replacement == 1) depth = changed.dsv.get();
        if (replacement == 2) {
            D3D11_RENDER_TARGET_VIEW_DESC descriptor{};
            descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            descriptor.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            checked(device.device->CreateRenderTargetView(original.color.get(), &descriptor,
                alternate.put()), "Create different actual view over the same color resource");
            require(!same_object(original.rtv.get(), alternate.get()),
                "Different typed color views unexpectedly shared COM identity");
            selected = alternate.get();
        }
        device.context->OMSetRenderTargets(1, &selected, depth);
        const State before(device.context.get());
        rejected(candidate.access(original_description.key, local.evidence, local.current,
            device.context.get()), DepthStatus::target_mismatch,
            "Reject changed selected color view or depth resource identity");
        before.unchanged(device.context.get(), "Failed identity check changed current bindings");
        rejected(candidate.access(original_description.key, local.evidence, local.current,
            device.context.get()), DepthStatus::inactive, "Target replacement retained old access");
    }
    device.context->ClearState();
}

void viewport_failures(Device& device) {
    for (UINT variant = 0; variant < 9; ++variant) {
        device.context->ClearState();
        Host source;
        auto targets = surface(device.device.get());
        bind(device.context.get(), targets, source.current);
        const auto original = source.current;
        D3D11DepthAttachment attachment(source.session);
        std::array<D3D11_VIEWPORT, 2> values{viewport(source.current.viewport),
            viewport(source.current.viewport)};
        UINT count = 1;
        if (variant == 0) values[0].TopLeftX = 0.5F;
        if (variant == 1) values[0].Width = 63.5F;
        if (variant == 2) values[0].Height = 63;
        if (variant == 3) values[0].MinDepth = 0.125F;
        if (variant == 4) values[0].MaxDepth = 0.875F;
        if (variant == 5) count = 2;
        if (variant == 6) count = 0;
        if (variant == 7) {
            source.current.viewport.x = 1;
            values[0] = viewport(source.current.viewport);
        }
        if (variant == 8) {
            source.current.viewport.y = -1;
            values[0] = viewport(source.current.viewport);
        }
        device.context->RSSetViewports(count, count == 0 ? nullptr : values.data());
        {
            const State before(device.context.get());
            expect_status(attachment.prepare(source.evidence, source.current, 1, device.context.get()),
                DepthStatus::invalid_viewport, "Reject actual viewport mismatch or out-of-resource extent");
            before.unchanged(device.context.get(), "Failed viewport preparation changed state");
        }
        source.current = original;
        bind(device.context.get(), targets, source.current);
        const auto description = ready(attachment.prepare(source.evidence, source.current, 1,
            device.context.get()), "Recover same capture and generation after viewport failure");
        if (variant < 7) {
            device.context->RSSetViewports(count, count == 0 ? nullptr : values.data());
            rejected(attachment.access(description.key, source.evidence, source.current,
                device.context.get()), DepthStatus::invalid_viewport,
                "Retire attachment when its actual viewport changes");
            bind(device.context.get(), targets, source.current);
            rejected(attachment.access(description.key, source.evidence, source.current,
                device.context.get()), DepthStatus::inactive,
                "Restoring only viewport revived an invalidated attachment");
        }
    }
    device.context->ClearState();
}

void keys_and_cameras(Device& device) {
    device.context->ClearState();
    Host source;
    auto targets = surface(device.device.get());
    bind(device.context.get(), targets, source.current);
    struct KeyMutation { const char* name; void (*change)(DepthKey&); };
    const std::array<KeyMutation, 9> keys{{
        {"version", [](DepthKey& key) { ++key.version; }},
        {"generation", [](DepthKey& key) { ++key.generation; }},
        {"epoch", [](DepthKey& key) { ++key.capture.epoch; }},
        {"world", [](DepthKey& key) { ++key.capture.world; }},
        {"camera", [](DepthKey& key) { ++key.capture.camera; }},
        {"frame", [](DepthKey& key) { ++key.capture.frame; }},
        {"capture", [](DepthKey& key) { ++key.capture.capture; }},
        {"camera configuration", [](DepthKey& key) { ++key.camera_configuration_revision; }},
        {"color slot", [](DepthKey& key) { ++key.color_slot; }}}};
    for (const auto& mutation : keys) {
        D3D11DepthAttachment attachment(source.session);
        const auto description = ready(attachment.prepare(source.evidence, source.current, 1,
            device.context.get()), "Prepare exact attachment identity");
        auto wrong = description.key;
        mutation.change(wrong);
        rejected(attachment.access(wrong, source.evidence, source.current, device.context.get()),
            DepthStatus::stale_capture, std::string("Reject mismatched depth key ") + mutation.name);
        rejected(attachment.access(description.key, source.evidence, source.current, device.context.get()),
            DepthStatus::inactive, "Incorrect key did not retire the candidate");
    }
    using CameraMutation = void (*)(projection::CameraContract&);
    const std::array<CameraMutation, 11> cameras{{
        [](projection::CameraContract& value) { ++value.configuration_revision; },
        [](projection::CameraContract& value) { value.perspective.vertical_fov_radians *= 0.9; },
        [](projection::CameraContract& value) { value.position[0] = 0.01; },
        [](projection::CameraContract& value) { value.view[12] = 0.01; },
        [](projection::CameraContract& value) { ++value.viewport.x; },
        [](projection::CameraContract& value) { value.max_position_error *= 2; },
        [](projection::CameraContract& value) { value.perspective.reverse_z = true; },
        [](projection::CameraContract& value) { ++value.perspective.far_distance; },
        [](projection::CameraContract& value) { value.perspective.depth_range = projection::DepthRange::minus_one_to_one; },
        [](projection::CameraContract& value) { value.perspective.handedness = projection::Handedness::right; },
        [](projection::CameraContract& value) { ++value.key.frame; }}};
    for (const auto mutate : cameras) {
        D3D11DepthAttachment attachment(source.session);
        const auto description = ready(attachment.prepare(source.evidence, source.current, 1,
            device.context.get()), "Prepare immutable complete camera contract");
        auto changed = source.current;
        mutate(changed);
        rejected(attachment.access(description.key, source.evidence, changed, device.context.get()),
            DepthStatus::invalid_camera, "Reject any change to complete prepared camera contract");
    }
    for (UINT variant = 0; variant < 4; ++variant) {
        D3D11DepthAttachment attachment(source.session);
        auto invalid = source.current;
        if (variant == 0) invalid.perspective.depth_range = projection::DepthRange::minus_one_to_one;
        if (variant == 1) invalid.viewport.origin = projection::ScreenOrigin::bottom_left;
        if (variant == 2) invalid.perspective.vertical_fov_radians = std::numeric_limits<double>::quiet_NaN();
        if (variant == 3) invalid.configuration_revision = 0;
        expect_status(attachment.prepare(source.evidence, invalid, 1, device.context.get()),
            DepthStatus::invalid_camera, "Reject invalid or unsupported P2 camera convention");
        static_cast<void>(ready(attachment.prepare(source.evidence, source.current, 1,
            device.context.get()), "Invalid camera attempt advanced attachment history"));
    }
    D3D11DepthAttachment attachment(source.session);
    expect_status(attachment.prepare(source.evidence, source.current, 0, device.context.get()),
        DepthStatus::invalid_argument, "Reject zero attachment generation");
    ++source.current.key.frame;
    ++source.current.key.capture;
    auto observed = source.sample();
    observed.projection[0] *= 1.25;
    require(source.session.capture(source.evidence, observed), "Fixture projection capture failed");
    expect_status(attachment.prepare(source.evidence, source.current, 1, device.context.get()),
        DepthStatus::stale_capture, "Reject finite observed projection inconsistent with P2 camera");
    source.advance();
    const auto description = ready(attachment.prepare(source.evidence, source.current, 1,
        device.context.get()), "Recover after rejected observed projection");
    source.advance();
    rejected(attachment.access(description.key, source.evidence, camera(false), device.context.get()),
        DepthStatus::invalid_camera, "Reject absent camera identity during access");
    device.context->ClearState();
}

void history_and_revocation(Device& device) {
    device.context->ClearState();
    Host source;
    auto targets = surface(device.device.get());
    bind(device.context.get(), targets, source.current);
    D3D11DepthAttachment attachment(source.session);
    auto description = ready(attachment.prepare(source.evidence, source.current, 5,
        device.context.get()), "Prepare initial monotonic attachment");
    {
        const State before(device.context.get());
        attachment.stop();
        before.unchanged(device.context.get(), "Stop changed context state");
    }
    expect_status(attachment.prepare(source.evidence, source.current, 6, device.context.get()),
        DepthStatus::stale_capture, "Stop erased same-frame replay history");
    source.advance();
    expect_status(attachment.prepare(source.evidence, source.current, 5, device.context.get()),
        DepthStatus::stale_capture, "Stop erased generation replay history");
    description = ready(attachment.prepare(source.evidence, source.current, 6,
        device.context.get()), "Recover fresh frame and generation after stop");
    source.advance();
    auto invalid = viewport(source.current.viewport);
    invalid.Width = 63;
    device.context->RSSetViewports(1, &invalid);
    expect_status(attachment.prepare(source.evidence, source.current, 7, device.context.get()),
        DepthStatus::invalid_viewport, "Reject invalid replacement preparation");
    rejected(attachment.access(description.key, source.evidence, source.current, device.context.get()),
        DepthStatus::inactive, "Failed prepare retained the previous borrowed view");
    bind(device.context.get(), targets, source.current);
    static_cast<void>(ready(attachment.prepare(source.evidence, source.current, 7,
        device.context.get()), "Failed prepare advanced generation or frame history"));
    source.current.perspective.vertical_fov_radians *= 0.9;
    source.advance();
    expect_status(attachment.prepare(source.evidence, source.current, 8, device.context.get()),
        DepthStatus::stale_capture, "Changed projection reused the same configuration revision");
    source.current.configuration_revision = 2;
    static_cast<void>(ready(attachment.prepare(source.evidence, source.current, 8,
        device.context.get()), "Accept fresh projection configuration after failed attempt"));
    source.current.configuration_revision = 1;
    source.advance();
    expect_status(attachment.prepare(source.evidence, source.current, 9, device.context.get()),
        DepthStatus::stale_capture, "Camera configuration revision moved backwards");
    source.current.configuration_revision = 3;
    static_cast<void>(ready(attachment.prepare(source.evidence, source.current, 9,
        device.context.get()), "Recover after camera configuration rollback"));
    ++source.current.key.camera;
    source.current.configuration_revision = 1;
    source.current.perspective.far_distance = 20;
    source.advance();
    static_cast<void>(ready(attachment.prepare(source.evidence, source.current, 10,
        device.context.get()), "Accept new camera with its own configuration revision"));
    source.restart(19);
    expect_status(attachment.prepare(source.evidence, source.current, 10, device.context.get()),
        DepthStatus::stale_capture, "New host epoch reset attachment generation history");
    description = ready(attachment.prepare(source.evidence, source.current, 11,
        device.context.get()), "Accept new epoch with fresh attachment generation");
    const auto prepared_camera = source.current;
    source.advance();
    rejected(attachment.access(description.key, source.evidence, prepared_camera, device.context.get()),
        DepthStatus::stale_capture, "A newer BuildSession frame left old depth access usable");

    for (UINT variant = 0; variant < 7; ++variant) {
        Host local;
        bind(device.context.get(), targets, local.current);
        D3D11DepthAttachment candidate(local.session);
        const auto accepted = ready(candidate.prepare(local.evidence, local.current, 1,
            device.context.get()), "Prepare BuildSession revocation fixture");
        auto evidence = local.evidence;
        if (variant == 0) local.session.stop();
        if (variant == 1) require(!local.session.refresh(std::nullopt), "Missing build did not revoke");
        if (variant == 2) {
            auto changed = local.profile;
            ++changed.modules[0].image.sha256[0];
            require(!local.session.refresh(changed), "Changed DLL did not revoke BuildSession");
        }
        if (variant == 3) evidence.mode = host::Mode::cooperative;
        if (variant == 4) evidence.lifecycle = host::Lifecycle::loading;
        if (variant == 5) evidence.phase = host::ReadPhase::window_or_present;
        if (variant == 6) ++evidence.world;
        rejected(candidate.access(accepted.key, evidence, local.current, device.context.get()),
            DepthStatus::stale_capture, "Reject revoked build, lifecycle, world or read phase");
        require(!local.session.snapshot(local.evidence, local.current.key),
            "Rejected source evidence did not revoke its underlying BuildSession");
        rejected(candidate.access(accepted.key, local.evidence, local.current, device.context.get()),
            DepthStatus::inactive, "Source revocation retained attachment data");
        local.restart(23);
        bind(device.context.get(), targets, local.current);
        static_cast<void>(ready(candidate.prepare(local.evidence, local.current, 2,
            device.context.get()), "Recover with independently begun BuildSession epoch"));
    }
    device.context->ClearState();
}

void owner_thread(Device& device) {
    device.context->ClearState();
    Host source;
    auto targets = surface(device.device.get());
    bind(device.context.get(), targets, source.current);
    D3D11DepthAttachment attachment(source.session);
    const auto description = ready(attachment.prepare(source.evidence, source.current, 1,
        device.context.get()), "Prepare owner thread fixture");
    std::array<bool, 3> rejected_calls{};
    std::thread worker([&] {
        try {
            static_cast<void>(attachment.prepare(source.evidence, source.current, 2, device.context.get()));
        } catch (const std::logic_error&) { rejected_calls[0] = true; }
        try {
            static_cast<void>(attachment.access(description.key, source.evidence, source.current,
                device.context.get()));
        } catch (const std::logic_error&) { rejected_calls[1] = true; }
        try { attachment.stop(); }
        catch (const std::logic_error&) { rejected_calls[2] = true; }
    });
    worker.join();
    for (const bool value : rejected_calls) require(value, "Attachment accepted a non-owner thread");
    static_cast<void>(ready(attachment.access(description.key, source.evidence, source.current,
        device.context.get()), "Wrong-thread calls changed owner-thread attachment state"));
    device.context->ClearState();
}

struct LifeCounts {
    std::atomic<std::uint32_t> live{0};
    std::atomic<std::uint32_t> destroyed{0};
};

class LifeProbe final : public IUnknown {
public:
    explicit LifeProbe(std::shared_ptr<LifeCounts> counts) : counts_(std::move(counts)) {
        ++counts_->live;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (!IsEqualIID(id, __uuidof(IUnknown))) return E_NOINTERFACE;
        *result = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }
private:
    ~LifeProbe() {
        --counts_->live;
        ++counts_->destroyed;
    }
    std::atomic<ULONG> references_{1};
    std::shared_ptr<LifeCounts> counts_;
};

constexpr GUID probe_id{0x3528f561, 0x5ee9, 0x4ad2, {0xac, 0x0f, 0x1c, 0x83, 0x52, 0x68, 0x93, 0x04}};

template<class T> void watch(T* object, const std::shared_ptr<LifeCounts>& counts) {
    auto* probe = new LifeProbe(counts);
    const HRESULT result = object->SetPrivateDataInterface(probe_id, probe);
    probe->Release();
    checked(result, "Attach actual COM destruction probe");
}

void expect_released(const std::shared_ptr<LifeCounts>& counts, std::uint32_t expected,
    std::string_view reason) {
    require(counts->live.load() == 0 && counts->destroyed.load() == expected,
        std::string(reason) + ": live=" + std::to_string(counts->live.load())
        + ", destroyed=" + std::to_string(counts->destroyed.load()));
}

void com_ownership(Device& device) {
    for (UINT retirement = 0; retirement < 4; ++retirement) {
        device.context->ClearState();
        Host source;
        auto targets = surface(device.device.get());
        const auto counts = std::make_shared<LifeCounts>();
        watch(targets.rtv.get(), counts);
        watch(targets.depth.get(), counts);
        bind(device.context.get(), targets, source.current);
        auto attachment = std::make_unique<D3D11DepthAttachment>(source.session);
        const auto description = ready(attachment->prepare(source.evidence, source.current, 1,
            device.context.get()), "Prepare actual COM ownership fixture");
        const auto access = attachment->access(description.key, source.evidence, source.current,
            device.context.get());
        watch(ready(access, "Access tracked readonly depth view"), counts);
        device.context->ClearState();
        targets = Surface{};
        device.context->Flush();
        require(counts->live.load() == 3 && counts->destroyed.load() == 0,
            "Attachment did not retain its own actual color view and depth resource references");
        if (retirement == 0) attachment->stop();
        if (retirement == 1) attachment.reset();
        if (retirement == 2) {
            source.advance();
            expect_status(attachment->prepare(source.evidence, source.current, 2, nullptr),
                DepthStatus::invalid_argument, "Failed prepare must release previous COM references");
        }
        if (retirement == 3) {
            auto wrong = description.key;
            ++wrong.generation;
            rejected(attachment->access(wrong, source.evidence, source.current, device.context.get()),
                DepthStatus::stale_capture, "Failed access must release previous COM references");
        }
        device.context->Flush();
        expect_released(counts, 3, "Retirement leaked owned actual COM objects");
    }
    {
        device.context->ClearState();
        Host source;
        auto targets = surface(device.device.get());
        const auto counts = std::make_shared<LifeCounts>();
        watch(targets.rtv.get(), counts);
        watch(targets.depth.get(), counts);
        bind(device.context.get(), targets, source.current);
        D3D11DepthAttachment attachment(source.session);
        const auto description = ready(attachment.prepare(source.evidence, source.current, 1,
            device.context.get()), "Prepare independently retained context binding");
        const auto access = attachment.access(description.key, source.evidence, source.current,
            device.context.get());
        watch(ready(access, "Track caller-bound readonly view"), counts);
        const auto readonly_state = depth_state(device.device.get(), description.comparison, false);
        device.context->OMSetDepthStencilState(readonly_state.get(), 0);
        bind(device.context.get(), targets, source.current, 0, access.view);
        targets = Surface{};
        attachment.stop();
        device.context->Flush();
        require(counts->live.load() == 3, "Stop removed consumer-owned context bindings");
        {
            Com<ID3D11DepthStencilView> bound;
            device.context->OMGetRenderTargets(0, nullptr, bound.put());
            require(same_object(bound.get(), access.view),
                "Stop replaced the caller's existing depth binding");
        }
        device.context->ClearState();
        device.context->Flush();
        expect_released(counts, 3, "Consumer unbind left hidden attachment COM ownership");
    }
    {
        device.context->ClearState();
        Host source;
        auto selected = surface(device.device.get());
        auto unselected = surface(device.device.get());
        const auto counts = std::make_shared<LifeCounts>();
        watch(unselected.rtv.get(), counts);
        bind(device.context.get(), selected, source.current);
        std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> colors{};
        colors[0] = selected.rtv.get();
        colors[7] = unselected.rtv.get();
        device.context->OMSetRenderTargets(static_cast<UINT>(colors.size()), colors.data(), selected.dsv.get());
        D3D11DepthAttachment attachment(source.session);
        const auto description = ready(attachment.prepare(source.evidence, source.current, 1,
            device.context.get()), "Observe all current RTV slots with one selected target");
        static_cast<void>(ready(attachment.access(description.key, source.evidence, source.current,
            device.context.get()), "Inspect additional RTV reference ownership on access"));
        bind(device.context.get(), selected, source.current);
        unselected = Surface{};
        device.context->Flush();
        expect_released(counts, 1, "OMGet temporary reference leaked an unselected RTV");
    }
    {
        device.context->ClearState();
        Host source;
        auto targets = surface(device.device.get());
        const auto counts = std::make_shared<LifeCounts>();
        watch(targets.rtv.get(), counts);
        watch(targets.depth.get(), counts);
        bind(device.context.get(), targets, source.current);
        auto invalid = viewport(source.current.viewport);
        invalid.Width = 63;
        device.context->RSSetViewports(1, &invalid);
        D3D11DepthAttachment attachment(source.session);
        expect_status(attachment.prepare(source.evidence, source.current, 1, device.context.get()),
            DepthStatus::invalid_viewport, "Fail after acquiring actual temporary resource references");
        device.context->ClearState();
        targets = Surface{};
        device.context->Flush();
        expect_released(counts, 2, "Failed preparation leaked temporary view or resource references");
    }
    {
        Device isolated;
        Host source;
        auto targets = surface(isolated.device.get());
        const auto counts = std::make_shared<LifeCounts>();
        watch(isolated.device.get(), counts);
        watch(targets.rtv.get(), counts);
        watch(targets.depth.get(), counts);
        bind(isolated.context.get(), targets, source.current);
        D3D11DepthAttachment attachment(source.session);
        const auto description = ready(attachment.prepare(source.evidence, source.current, 1,
            isolated.context.get()), "Prepare independently retained device ownership");
        watch(ready(attachment.access(description.key, source.evidence, source.current,
            isolated.context.get()), "Track readonly view on isolated device"), counts);
        isolated.context->ClearState();
        targets = Surface{};
        isolated.context->Flush();
        isolated.context.reset();
        isolated.device.reset();
        require(counts->live.load() == 4, "Attachment failed to retain its actual device");
        attachment.stop();
        expect_released(counts, 4, "Stop leaked device or child COM ownership after caller release");
    }
    device.context->ClearState();
    device.context->Flush();
    std::cout << "COM ownership: stop, destruction, failure, extra RTV and consumer binding verified\n";
}

} // namespace

int main() {
    try {
        Device device;
        const Program program(device.device.get());
        occlusion(device, program, false);
        occlusion(device, program, true);
        formats_and_samples(device);
        budgets_and_layouts(device);
        contexts_and_bindings(device);
        viewport_failures(device);
        keys_and_cameras(device);
        history_and_revocation(device);
        owner_thread(device);
        com_ownership(device);
        std::cout << "D3D11 WARP FL11_0 depth attachment: " << assertions << " assertions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "D3D11 WARP depth attachment failed: " << error.what() << '\n';
        return 1;
    }
}
