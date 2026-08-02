#include "ui/ui_manager.hpp"
#include "ui/interface_fonts.hpp"

#include <algorithm>
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

    namespace fs = std::filesystem;
    std::error_code ec;

    // Extracted data does not agree with itself about case, and this path is
    // reached directly rather than through the asset manager's manifest.
    const char* dirs[] = { "misc/fonts", "Misc/Fonts", "fonts", "Fonts" };
    fs::path fontDir;
    for (const char* rel : dirs) {
        const fs::path p = fs::path(dataRoot) / rel;
        if (fs::is_directory(p, ec)) { fontDir = p; break; }
    }
    if (fontDir.empty()) {
        LOG_INFO("No interface fonts under ", dataRoot, "; keeping the built-in");
        return;
    }

    // Built at a size above what the interface mostly asks for. A font string
    // carries its own height and is drawn scaled from its face, and scaling
    // down from a larger atlas reads better than up from a smaller.
    constexpr float kAtlasSize = 18.0f;

    // What the client's own panels are drawn at. They were laid out against
    // ImGui's built-in face, which is 13 pixels tall, and FRIZQT at the atlas
    // size above would push text out of buttons sized for it. Close enough to
    // the old metrics to keep those layouts intact, in the game's typeface,
    // which is the point.
    constexpr float kClientSize = 15.0f;

    ImGuiIO& io = ImGui::GetIO();

    // Case is not agreed on here either, so look for the file rather than
    // assuming the spelling the manifest happens to use.
    auto resolve = [&](const char* name) {
        fs::path file = fontDir / name;
        if (fs::exists(file, ec)) return file;
        for (const auto& entry : fs::directory_iterator(fontDir, ec)) {
            std::string have = entry.path().filename().string();
            std::transform(have.begin(), have.end(), have.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (have == name) return entry.path();
        }
        return fs::path();
    };

    // FRIZQT at the client's size, first, because ImGui draws with whichever
    // face was added first and that is what everything without an opinion gets
    // — this client's own windows included. The same face is added again below
    // at the atlas size for the interface, which asks for it by name.
    const fs::path frizqt = resolve("frizqt__.ttf");
    if (!frizqt.empty()) {
        io.Fonts->AddFontFromFileTTF(frizqt.string().c_str(), kClientSize);
    } else {
        io.Fonts->AddFontDefault();
    }

    // The faces FrameXML's font objects name: body text in FRIZQT, headings in
    // MORPHEUS, damage in SKURRI, condensed numbers in ARIALN.
    const char* faces[] = {
        "frizqt__.ttf", "morpheus.ttf", "skurri.ttf", "arialn.ttf", "friends.ttf"
    };
    int loaded = 0;
    for (const char* name : faces) {
        const fs::path file = resolve(name);
        if (file.empty()) continue;
        if (ImFont* f = io.Fonts->AddFontFromFileTTF(file.string().c_str(), kAtlasSize)) {
            registerInterfaceFace(name, f);
            ++loaded;
        }
    }
    LOG_INFO("Interface fonts loaded: ", loaded, " from ", fontDir.string());
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
