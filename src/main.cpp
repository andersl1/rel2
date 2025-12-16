#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h> 

#include "dsp_reader.h"
#include "alpha_vantage.h"
#include "dsp_library.h" 
#include "analysis_engine.h" 
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <numeric>

// Global state
DspData g_Data;
char g_FilePath[1024] = "C:/Users/ander/OneDrive/Documents/REL2/src/save_files/PEBO20(S1).dsp"; 
std::string g_StatusMessage = "Ready";

// Library State
std::vector<DspFileEntry> g_LibraryFiles;
std::vector<DspFileEntry> g_FilteredFiles;
char g_FileFilter[128] = "";
bool g_LibraryLoaded = false;

// Alpha Vantage & Search State
static char g_AlphaApiKey[128] = "";
static char g_Symbol[32] = "IBM";
static std::string g_AlphaStatus = "Ready";
static std::vector<double> g_StockData;
static std::vector<SearchResult> g_SearchResults;

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

void UpdateFilter() {
    if (strlen(g_FileFilter) == 0) {
        g_FilteredFiles = g_LibraryFiles;
    } else {
        g_FilteredFiles.clear();
        std::string filterUpper = g_FileFilter;
        std::transform(filterUpper.begin(), filterUpper.end(), filterUpper.begin(), ::toupper);
        
        for (const auto& entry : g_LibraryFiles) {
            std::string nameUpper = entry.displayName;
            std::transform(nameUpper.begin(), nameUpper.end(), nameUpper.begin(), ::toupper);
            
            if (nameUpper.find(filterUpper) != std::string::npos) {
                g_FilteredFiles.push_back(entry);
            }
        }
    }
}

// Z-Score Normalization helper for display
std::vector<double> Normalize(const std::vector<double>& input) {
    if (input.empty()) return {};
    double sum = std::accumulate(input.begin(), input.end(), 0.0);
    double mean = sum / input.size();
    double sq_sum = 0.0;
    for (double v : input) sq_sum += (v - mean) * (v - mean);
    double stdev = std::sqrt(sq_sum / input.size());
    
    std::vector<double> out;
    out.reserve(input.size());
    if (stdev == 0) {
        for (double v : input) out.push_back(v - mean); // Center at 0
    } else {
        for (double v : input) out.push_back((v - mean) / stdev);
    }
    return out;
}

int main(int, char**)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "REL2 - DSP Plotter", NULL, NULL);
    if (window == NULL)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Initial Library Scan
    std::string root = DspLibrary::FindRoot();
    if (!root.empty()) {
        g_LibraryFiles = DspLibrary::Scan(root);
        UpdateFilter();
        g_LibraryLoaded = true;
        if (g_LibraryFiles.size() > 0) {
            g_StatusMessage = "Found " + std::to_string(g_LibraryFiles.size()) + " files.";
        }
    }

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // UI Panel
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_FirstUseEver);
            ImGui::Begin("REL2 Dashboard");
            
            if (ImGui::BeginTabBar("Tabs")) {
                // Tab 1: DSP Viewer
                if (ImGui::BeginTabItem("DSP Viewer")) {
                    
                    // Left Column: Library Browser
                    ImGui::BeginGroup();
                    ImGui::Text("Library (%zu files)", g_LibraryFiles.size());
                    if (ImGui::InputText("Filter", g_FileFilter, sizeof(g_FileFilter))) {
                        UpdateFilter();
                    }
                    
                    if (ImGui::BeginListBox("##files", ImVec2(300, -1))) { 
                        for (const auto& entry : g_FilteredFiles) {
                            bool isSelected = (g_FilePath == entry.fullPath);
                            if (ImGui::Selectable(entry.displayName.c_str(), isSelected)) {
                                strncpy(g_FilePath, entry.fullPath.c_str(), sizeof(g_FilePath) - 1);
                                try {
                                    g_Data = DspReader::Load(g_FilePath);
                                    g_StatusMessage = "Loaded: " + g_Data.GetName() + ", N=" + std::to_string(g_Data.n);
                                } catch (const std::exception& e) {
                                    g_StatusMessage = "Error: " + std::string(e.what());
                                }
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndListBox();
                    }
                    ImGui::EndGroup();

                    ImGui::SameLine();

                    // Right Column: Plot
                    ImGui::BeginGroup();
                        ImGui::InputText("Current File", g_FilePath, sizeof(g_FilePath));
                        ImGui::SameLine();
                        if (ImGui::Button("Reload")) {
                            try {
                                g_Data = DspReader::Load(g_FilePath);
                                g_StatusMessage = "Loaded: " + g_Data.GetName() + ", N=" + std::to_string(g_Data.n);
                            } catch (const std::exception& e) {
                                g_StatusMessage = "Error: " + std::string(e.what());
                            }
                        }
                        
                        ImGui::Text("Status: %s", g_StatusMessage.c_str());

                        if (!g_Data.values.empty()) {
                            ImGui::Separator();
                            ImGui::Text("Total Investment: %.2f", g_Data.total_investment);
                            ImGui::Text("Points: %zu", g_Data.values.size());
                            
                            ImVec2 plotSize = ImGui::GetContentRegionAvail();
                            plotSize.y -= 20; 
                            if (plotSize.y < 200) plotSize.y = 200;

                            if (ImPlot::BeginPlot("Signal", plotSize)) {
                                ImPlot::SetupAxes("Index", "Value");
                                ImPlot::PlotLine("Data", g_Data.values.data(), static_cast<int>(g_Data.values.size()));
                                ImPlot::EndPlot();
                            }
                        }
                    ImGui::EndGroup();

                    ImGui::EndTabItem();
                }

                // Tab 2: Alpha Vantage & Search
                if (ImGui::BeginTabItem("Alpha Vantage")) {
                    ImGui::Text("Enter your Alpha Vantage API Key below.");
                    ImGui::InputText("API Key", g_AlphaApiKey, sizeof(g_AlphaApiKey), ImGuiInputTextFlags_Password);
                    ImGui::InputText("Symbol", g_Symbol, sizeof(g_Symbol));
                    
                    if (ImGui::Button("Fetch Data")) {
                        if (strlen(g_AlphaApiKey) == 0) {
                            g_AlphaStatus = "Error: API Key Required";
                        } else {
                            // 1. Ensure Cache
                            auto& engine = AnalysisEngine::GetInstance();
                            if (!engine.IsLoaded()) {
                                g_AlphaStatus = "Caching Library...";
                                std::string root = DspLibrary::FindRoot();
                                size_t count = engine.LoadLibrary(root);
                                std::cout << "Cached " << count << " stocks." << std::endl;
                            }

                            // 2. Fetch
                            g_AlphaStatus = "Fetching Stock Data...";
                            try {
                                g_StockData = AlphaVantage::FetchDaily(g_Symbol, g_AlphaApiKey);
                                
                                // 3. Search
                                if (g_StockData.size() >= 300) {
                                    g_AlphaStatus = "Running OpenMP Search...";
                                    g_SearchResults = engine.Search(g_StockData, 10);
                                    g_AlphaStatus = "Found Top 10 Matches.";
                                } else {
                                    g_AlphaStatus = "Data too short for search (<300).";
                                    g_SearchResults.clear();
                                }
                                
                            } catch (const std::exception& e) {
                                g_AlphaStatus = "Error: " + std::string(e.what());
                            }
                        }
                    }
                    ImGui::SameLine();
                    ImGui::Text("Status: %s", g_AlphaStatus.c_str());
                    
                    // Search Results Plot
                    if (!g_StockData.empty()) {
                        ImVec2 plotSize = ImGui::GetContentRegionAvail();
                        plotSize.y -= 20;
                        if (ImPlot::BeginPlot("Search Results (Z-Scored)", plotSize)) {
                            ImPlot::SetupAxes("Index", "Norm Value");
                            
                            // 1. Plot Matches (Background)
                            ImPlot::SetNextLineStyle(ImVec4(0.5f, 0.5f, 0.5f, 0.3f)); // Dim gray
                            for (size_t i = 0; i < g_SearchResults.size(); ++i) {
                                const auto& res = g_SearchResults[i];
                                if (res.stockPtr) {
                                    // Extract region 300 + 100
                                    int start = res.offset;
                                    int len = 400; 
                                    if (start + len > res.stockPtr->data.size()) len = res.stockPtr->data.size() - start;
                                    
                                    if (len > 0) {
                                        std::vector<double> segment(res.stockPtr->data.begin() + start, 
                                                                    res.stockPtr->data.begin() + start + len);
                                        std::vector<double> norm = Normalize(segment);
                                        
                                        std::string label = res.symbol + " (P:" + std::to_string(res.pearson).substr(0,4) + ")";
                                        ImPlot::PlotLine(label.c_str(), norm.data(), static_cast<int>(norm.size()));
                                    }
                                }
                            }

                            // 2. Plot Query (Foreground)
                            int queryLen = std::min((int)g_StockData.size(), 300);
                            std::vector<double> querySeg(g_StockData.begin(), g_StockData.begin() + queryLen);
                            std::vector<double> normQuery = Normalize(querySeg);
                            
                            ImPlot::SetNextLineStyle(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), 2.0f); // Bright Cyan
                            ImPlot::PlotLine("Query (300)", normQuery.data(), static_cast<int>(normQuery.size()));

                            // 3. Prediction Zone vertical line
                            ImPlot::PlotInfLines("Prediction", (double*)&queryLen, 1);

                            ImPlot::EndPlot();
                        }
                    }

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
