#include "ui/ui_manager.hpp"

#include <filesystem>
#include <chrono>
#include "core/window.hpp"
#include "core/application.hpp"
#include "core/logger.hpp"
#include "auth/auth_handler.hpp"
#include "game/game_handler.hpp"
#include "rendering/vk_context.hpp"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>

namespace wowee {
namespace ui {

UIManager::UIManager() {
    // Create screen instances
    authScreen = std::make_unique<AuthScreen>();
    realmScreen = std::make_unique<RealmScreen>();
    characterCreateScreen = std::make_unique<CharacterCreateScreen>();
    characterScreen = std::make_unique<CharacterScreen>();
    gameScreen = std::make_unique<GameScreen>();
}

UIManager::~UIManager() = default;

bool UIManager::initialize(core::Window* win) {
    window = win;
    LOG_INFO("Initializing UI manager");

    auto* vkCtx = window->getVkContext();
    if (!vkCtx) {
        LOG_ERROR("No Vulkan context available for ImGui initialization");
        return false;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Setup ImGui style
    ImGui::StyleColorsDark();

    // Customize style for better WoW feel
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    // WoW-inspired colors
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.94f);
    // ImGui uses PopupBg for hover tooltips. Keep their text and item details
    // fully legible over the 3D scene.
    colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.06f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.25f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.40f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.30f, 0.50f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.20f, 0.35f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.40f, 0.55f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.30f, 0.50f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.25f, 0.45f, 1.00f);

    // Initialize ImGui for SDL2 + Vulkan
    ImGui_ImplSDL2_InitForVulkan(window->getSDLWindow());

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_1;
    initInfo.Instance = vkCtx->getInstance();
    initInfo.PhysicalDevice = vkCtx->getPhysicalDevice();
    initInfo.Device = vkCtx->getDevice();
    initInfo.QueueFamily = vkCtx->getGraphicsQueueFamily();
    initInfo.Queue = vkCtx->getGraphicsQueue();
    initInfo.DescriptorPool = vkCtx->getImGuiDescriptorPool();
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = vkCtx->getSwapchainImageCount();
    // The UI renders in the overlay pass, which is single-sampled on purpose:
    // ImGui draws axis-aligned rects and pre-antialiased glyphs, so MSAA buys
    // almost nothing there and costs fill rate at the sample count the scene uses.
    initInfo.PipelineInfoMain.RenderPass = vkCtx->getOverlayRenderPass();
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS)
            LOG_ERROR("ImGui Vulkan error: ", static_cast<int>(err));
    };

    ImGui_ImplVulkan_Init(&initInfo);

    imguiInitialized = true;

    LOG_INFO("UI manager initialized successfully (Vulkan)");
    return true;
}

void UIManager::loadInterfaceFont(const std::string& dataRoot) {
    if (!imguiInitialized || dataRoot.empty()) return;

    // Extracted data does not agree with itself about case, and this path is
    // reached directly rather than through the asset manager's manifest.
    namespace fs = std::filesystem;
    const char* candidates[] = {
        "misc/fonts/frizqt__.ttf", "Misc/Fonts/FRIZQT__.TTF",
        "fonts/frizqt__.ttf",      "Fonts/FRIZQT__.TTF",
    };
    std::error_code ec;
    fs::path found;
    for (const char* rel : candidates) {
        const fs::path p = fs::path(dataRoot) / rel;
        if (fs::exists(p, ec)) { found = p; break; }
    }
    if (found.empty()) {
        LOG_INFO("Interface font not found under ", dataRoot, "; keeping the built-in");
        return;
    }

    // Built at a size above what the interface mostly asks for. A font string
    // carries its own height and is drawn scaled from this one face, and
    // scaling down from a larger atlas reads better than up from a smaller.
    constexpr float kAtlasSize = 18.0f;
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->AddFontFromFileTTF(found.string().c_str(), kAtlasSize) == nullptr) {
        LOG_WARNING("Interface font at ", found.string(), " could not be read");
        return;
    }
    LOG_INFO("Interface font loaded: ", found.string());
}

void UIManager::shutdown() {
    if (imguiInitialized) {
        auto* vkCtx = window ? window->getVkContext() : nullptr;
        if (vkCtx) {
            vkDeviceWaitIdle(vkCtx->getDevice());
        }
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized = false;
    }
    LOG_INFO("UI manager shutdown");
}

void UIManager::update([[maybe_unused]] float deltaTime) {
    if (!imguiInitialized) return;

    // Start ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void UIManager::render(core::AppState appState, auth::AuthHandler* authHandler, game::GameHandler* gameHandler) {
    if (!imguiInitialized) return;

    // Two ~150-200ms spikes land here every launch, before login. Decoding the
    // auth background off the main thread did not move them, so report which
    // application state was being drawn when one happens — that narrows it to a
    // screen before anyone goes looking inside one.
    const auto uiRenderStart = std::chrono::steady_clock::now();
    struct StateReport {
        std::chrono::steady_clock::time_point start;
        core::AppState state;
        ~StateReport() {
            const float ms = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            if (ms > 50.0f) {
                LOG_WARNING("SLOW UI screen render: ", ms, "ms in appState=",
                            static_cast<int>(state));
            }
        }
    } stateReport{uiRenderStart, appState};

    // Render appropriate screen based on application state
    switch (appState) {
        case core::AppState::AUTHENTICATION:
            if (authHandler) {
                authScreen->render(*authHandler);
            }
            break;

        case core::AppState::REALM_SELECTION:
            authScreen->stopLoginMusic();
            if (authHandler) {
                realmScreen->render(*authHandler);
            }
            break;

        case core::AppState::CHARACTER_CREATION:
            authScreen->stopLoginMusic();
            if (gameHandler) {
                characterCreateScreen->render(*gameHandler);
            }
            break;

        case core::AppState::CHARACTER_SELECTION:
            authScreen->stopLoginMusic();
            if (gameHandler) {
                characterScreen->render(*gameHandler);
            }
            break;

        case core::AppState::IN_GAME:
            authScreen->stopLoginMusic();
            if (gameHandler) {
                gameScreen->render(*gameHandler);
            }
            break;

        case core::AppState::DISCONNECTED:
            authScreen->stopLoginMusic();
            ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 200,
                                           ImGui::GetIO().DisplaySize.y * 0.5f - 75),
                                    ImGuiCond_Always);
            ImGui::Begin("Disconnected", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
            ImGui::TextWrapped("You have been disconnected from the server.");
            ImGui::Spacing();
            if (ImGui::Button("Return to Login", ImVec2(-1, 0))) {
                // Will be handled by application
            }
            ImGui::End();
            break;
    }

    // Finalize ImGui draw data (actual rendering happens in the command buffer)
    ImGui::Render();
}

void UIManager::processEvent(const SDL_Event& event) {
    if (imguiInitialized) {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }
}

} // namespace ui
} // namespace wowee
