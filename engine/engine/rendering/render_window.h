#pragma once
#include <engine/engine_export.h>

#include <graphics/render_pass.h>
#include <ospp/display_mode.h>
#include <ospp/event.h>
#include <ospp/init.h>
#include <ospp/window.h>

#include <memory>

namespace unravel
{

/**
 * @brief Struct representing a render window.
 */
struct render_window
{
public:
    using graphics_surface_t = std::shared_ptr<gfx::frame_buffer>;

    /**
     * @brief How a window presents what is drawn into its surface.
     */
    enum class surface_kind : std::uint8_t
    {
        /// The window owns a swap chain frame buffer of its own.
        swap_chain,
        /// The window is the one bgfx was initialized on: its surface stands for bgfx's
        /// backbuffer, which the renderer resets when the window size changes.
        backbuffer
    };

    /**
     * @brief Constructs a render window with the specified OS window.
     * @param win The OS window to associate with this render window.
     * @param kind How the window presents. A backbuffer window touches no bgfx object, so it can
     *        be constructed before bgfx is initialized and destroyed after it shut down.
     */
    render_window(os::window&& win, surface_kind kind = surface_kind::swap_chain);

    /**
     * @brief Destructor for the render window.
     */
    ~render_window();

    /**
     * @brief Prepares the rendering surface for the window's current size. A backbuffer window
     *        only updates the size its surface reports; resetting the backbuffer to it is the
     *        renderer's job.
     */
    void prepare_surface();

    /**
     * @brief Destroys the rendering surface.
     */
    void destroy_surface(int frames);

    /**
     * @brief Per-surface swap chain flags (BGFX_SWAP_CHAIN_*) every window surface is
     *        created with, the window bgfx is initialized on included. bgfx no longer
     *        carries these over from the main window to other swap chains.
     */
    static auto get_swap_chain_flags() -> uint32_t;

    /**
     * @brief Resizes the render window to the specified width and height.
     * @param w The new width of the window.
     * @param h The new height of the window.
     */
    void resize(uint32_t w, uint32_t h);

    /**
     * @brief Gets the associated OS window.
     * @return Reference to the OS window.
     */
    auto get_window() -> os::window&;

    /**
     * @brief Gets the rendering surface.
     * @return Reference to the rendering surface.
     */
    auto get_surface() -> graphics_surface_t&;

    /**
     * @brief Begins the present render pass.
     * @return Reference to the present render pass.
     */
    auto begin_present_pass() -> gfx::render_pass&;

    /**
     * @brief Gets the present render pass.
     * @return Reference to the present render pass.
     */
    auto get_present_pass() -> gfx::render_pass&;

private:
    /// The associated OS window.
    os::window window_;
    /// How the window presents.
    surface_kind kind_{surface_kind::swap_chain};
    /// The render pass for presenting.
    std::unique_ptr<gfx::render_pass> pass_;
    /// The rendering surface for this window.
    mutable graphics_surface_t surface_;
};

} // namespace unravel
