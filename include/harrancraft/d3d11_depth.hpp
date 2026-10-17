#pragma once

#include <harrancraft/camera_projection.hpp>
#include <harrancraft/host_build.hpp>

#include <d3d11.h>

#include <cstdint>
#include <optional>
#include <thread>

namespace harrancraft::graphics {

inline constexpr std::uint32_t depth_contract_version = 1;
inline constexpr std::uint64_t default_depth_source_budget = 256ULL * 1024 * 1024;
inline constexpr std::uint64_t max_depth_source_budget = 1024ULL * 1024 * 1024;

enum class DepthStatus {
    ready, inactive, invalid_argument, invalid_camera, stale_capture,
    unsupported_context, missing_target, unsupported_surface, target_mismatch,
    invalid_viewport, budget_exceeded, device_changed, device_lost, api_failure
};

struct DepthKey {
    std::uint32_t version = depth_contract_version;
    std::uint64_t generation = 0;
    host::CaptureKey capture;
    std::uint64_t camera_configuration_revision = 0;
    std::uint32_t color_slot = 0;
    bool operator==(const DepthKey&) const = default;
};

struct DepthDescription {
    DepthKey key;
    std::uint32_t width = 0, height = 0, samples = 0, quality = 0;
    DXGI_FORMAT color_format = DXGI_FORMAT_UNKNOWN, depth_format = DXGI_FORMAT_UNKNOWN;
    // Selected color plus depth texel storage, excluding driver overhead and extra bindings.
    std::uint64_t source_bytes = 0;
    D3D11_COMPARISON_FUNC comparison = D3D11_COMPARISON_NEVER;
};

struct DepthResult {
    DepthStatus status = DepthStatus::inactive;
    HRESULT error = S_OK;
    std::optional<DepthDescription> description;
};

struct DepthAccess {
    DepthResult result;
    // Borrowed until an owner-thread operation retires/replaces the attachment or it is destroyed.
    ID3D11DepthStencilView* view = nullptr;
};

// The caller supplies the real scene-pass/camera association and owns the immediate
// context thread. This object observes bindings; it never binds, draws, clears or maps.
// Construct, use and destroy it on that thread; the BuildSession must outlive it.
class D3D11DepthAttachment {
public:
    explicit D3D11DepthAttachment(host::BuildSession& source,
        std::uint64_t source_budget = default_depth_source_budget);
    ~D3D11DepthAttachment();
    D3D11DepthAttachment(const D3D11DepthAttachment&) = delete;
    D3D11DepthAttachment& operator=(const D3D11DepthAttachment&) = delete;
    D3D11DepthAttachment(D3D11DepthAttachment&&) = delete;
    D3D11DepthAttachment& operator=(D3D11DepthAttachment&&) = delete;

    DepthResult prepare(const host::Evidence& evidence,
        const projection::CameraContract& camera, std::uint64_t generation,
        ID3D11DeviceContext* context, std::uint32_t color_slot = 0);

    DepthAccess access(const DepthKey& key, const host::Evidence& evidence,
        const projection::CameraContract& camera, ID3D11DeviceContext* context);

    // Retires this object's references only. Consumers must release their own bindings
    // and references before resizing, replacing or destroying host resources.
    void stop();

private:
    void check_owner() const;
    void release() noexcept;
    bool matches_camera(const host::Evidence&, const projection::CameraContract&);
    bool follows(const projection::CameraContract&) const;

    const std::thread::id owner_;
    host::BuildSession& source_;
    const std::uint64_t source_budget_;
    std::optional<projection::CameraContract> previous_, camera_;
    std::optional<DepthDescription> description_;
    std::uint64_t last_generation_ = 0;
    ID3D11Device* device_ = nullptr;
    ID3D11RenderTargetView* color_ = nullptr;
    ID3D11DepthStencilView* depth_ = nullptr;
};

} // namespace harrancraft::graphics
