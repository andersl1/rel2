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
#include <chrono>

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
bool g_TestingMode = false;
bool g_UseFred = false; // Default Off // False = Last 300 (Live), True = First 300 (Testing)
std::vector<double> g_StockData;
std::vector<SearchResult> g_SearchResults;
std::vector<double> g_PredictionData;

struct FuturePoint { double z; double weight; };
std::vector<FuturePoint> g_FuturePoints;
std::vector<double> g_MedianData;

std::string g_AlphaStatus = "Idle";
float g_Zoom = 1.0f;
ImVec2 g_Pan = ImVec2(0, 0);

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
        for (double v : input) out.push_back(v - mean); 
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
                    
                    ImGui::Checkbox("Testing Mode (Use First 300 pts)", &g_TestingMode);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("If checked, uses FIRST 300 points. Default (Unchecked) is LAST 300 points.");
                    
                    ImGui::SameLine();
                    ImGui::Checkbox("Include FRED Data", &g_UseFred);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Include 100,000+ Economic Series from FRED in the search (Slower).");

                    // Persistent Query Segment for plotting (updated on fetch)
                    static std::vector<double> s_DisplayQuery;
                    
                    if (ImGui::Button("Fetch Data")) {
                        if (strlen(g_AlphaApiKey) == 0) {
                            g_AlphaStatus = "Error: API Key Required";
                        } else {
                            auto start_time = std::chrono::high_resolution_clock::now();

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
                                    
                                    // EXTRACT QUERY PATTERN BASED ON MODE
                                    std::vector<double> searchPattern;
                                    if (g_TestingMode) {
                                        // First 300 for searching
                                        searchPattern.assign(g_StockData.begin(), g_StockData.begin() + 300);
                                        // First 400 for Display (if available, to show "actual" future)
                                        int dispLen = std::min((int)g_StockData.size(), 400);
                                        s_DisplayQuery.assign(g_StockData.begin(), g_StockData.begin() + dispLen);
                                    } else {
                                        // Last 300 for searching AND display
                                        searchPattern.assign(g_StockData.end() - 300, g_StockData.end());
                                        s_DisplayQuery = searchPattern;
                                    }
                                    
                                    // 4. Search and Calculate Prediction
                                    g_SearchResults = engine.Search(searchPattern, g_UseFred, 35);
                                    g_AlphaStatus = "Found Top 10 Matches.";

                                    g_PredictionData.clear();
                                    g_FuturePoints.clear();
                                    g_MedianData.clear();
                                    
                                    if (!g_SearchResults.empty()) {
                                        std::vector<double> sum_returns(100, 0.0);
                                        int count = 0;
                                        std::vector<std::vector<double>> allSegments; // For Median

                                        for (const auto& res : g_SearchResults) {
                                            if (!res.stockPtr) continue;

                                            // Reconstruct the data at the correct scale
                                            std::vector<double> scaledData;
                                            if (res.scale == 1) {
                                                scaledData = res.stockPtr->data;
                                            } else {
                                                scaledData = res.stockPtr->data;
                                                for (int s = 1; s < res.scale; s *= 2) {
                                                    scaledData = AnalysisEngine::Downsample(scaledData);
                                                }
                                            }
                                            
                                            // Segment Match stats
                                            double seg_sum = 0, seg_sq_sum = 0;
                                            int match_end_idx = res.offset + 300 - 1;
                                            
                                            // Calculate stats for the match segment (for Z-score normalization)
                                            // And accumulate for Median Calculation
                                            if (res.offset + 300 <= (int)scaledData.size()) {
                                                for(int k=0; k<300; ++k) {
                                                    double val = scaledData[res.offset + k];
                                                    seg_sum += val;
                                                }
                                                double seg_mean = seg_sum / 300.0;
                                                // Re-iterate for stdev
                                                for(int k=0; k<300; ++k) {
                                                    double v = scaledData[res.offset + k];
                                                    seg_sq_sum += (v - seg_mean)*(v - seg_mean);
                                                }
                                                double seg_stdev = std::sqrt(seg_sq_sum / 300.0);
                                                if (seg_stdev == 0) seg_stdev = 1.0;

                                                // --- 1. Store Normalized Full Segment for Median ---
                                                int len = 400; 
                                                if (res.offset + len > (int)scaledData.size()) len = (int)scaledData.size() - res.offset;
                                                
                                                std::vector<double> norm_full;
                                                for(int k=0; k<len; ++k) {
                                                    double v = scaledData[res.offset + k];
                                                    norm_full.push_back((v - seg_mean) / seg_stdev);
                                                }
                                                allSegments.push_back(norm_full);

                                                // --- 2. Future Point Z-Score Calculation (Robust to Negative Data) ---
                                                if (res.offset + 399 < (int)scaledData.size()) {
                                                    double future_val = scaledData[res.offset + 399];
                                                    double z = (future_val - seg_mean) / seg_stdev;
                                                    g_FuturePoints.push_back({z, res.pearson});
                                                }
                                            }

                                            // For Prediction Line (average returns)
                                            if (match_end_idx + 100 < (int)scaledData.size()) {
                                                double base_val = scaledData[match_end_idx];
                                                if (base_val == 0) base_val = 0.0001;

                                                for (int k = 0; k < 100; ++k) {
                                                    double future_val = scaledData[match_end_idx + 1 + k];
                                                    double ret = (future_val - base_val) / base_val;
                                                    sum_returns[k] += ret;
                                                }
                                                count++;
                                            }
                                        }
                                        
                                        // Calculate Median Line
                                        if (!allSegments.empty()) {
                                            for (int t = 0; t < 400; ++t) {
                                                std::vector<double> vals;
                                                for (const auto& seg : allSegments) {
                                                    if (t < (int)seg.size()) vals.push_back(seg[t]);
                                                }
                                                if (!vals.empty()) {
                                                    std::sort(vals.begin(), vals.end());
                                                    double med = vals[vals.size()/2];
                                                    if (vals.size() % 2 == 0) {
                                                        med = (vals[vals.size()/2 - 1] + vals[vals.size()/2]) * 0.5;
                                                    }
                                                    g_MedianData.push_back(med);
                                                }
                                            }
                                        }

                                        if (count > 0 && !searchPattern.empty()) {
                                            double current_price = searchPattern.back(); 
                                            for (int k = 0; k < 100; ++k) {
                                                double avg_ret = sum_returns[k] / count;
                                                g_PredictionData.push_back(current_price * (1.0 + avg_ret));
                                            }
                                        }
                                    }

                                } else {
                                    g_AlphaStatus = "Data too short for search (<300).";
                                    g_SearchResults.clear();
                                    g_PredictionData.clear();
                                    s_DisplayQuery.clear();
                                }
                                
                            } catch (const std::exception& e) {
                                g_AlphaStatus = "Error: " + std::string(e.what());
                            }

                            auto end_time = std::chrono::high_resolution_clock::now();
                            auto ms_int = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                            std::cout << "Fetch+Search Total Time: " << ms_int.count() << "ms" << std::endl;
                            g_AlphaStatus += " (" + std::to_string(ms_int.count()) + "ms)";
                        }
                    }
                    ImGui::SameLine();
                    ImGui::Text("Status: %s", g_AlphaStatus.c_str());
                    
                    // Search Results Plot Area
                    // ... (UI Code Layout) ...
                    
                    if (!s_DisplayQuery.empty()) {
                        static int s_HoveredIdx = -1;
                        if (ImGui::BeginTable("PlotLayout", 2, ImGuiTableFlags_Resizable)) {
                            ImGui::TableSetupColumn("Main Plot", ImGuiTableColumnFlags_WidthStretch, 0.8f);
                            ImGui::TableSetupColumn("EV Dist", ImGuiTableColumnFlags_WidthStretch, 0.2f);
                            
                            ImGui::TableNextRow();
                            
                            // --- COLUMN 0: Main Plot ---
                            ImGui::TableSetColumnIndex(0);
                            ImVec2 plotSize = ImGui::GetContentRegionAvail();
                            plotSize.y -= 5; 
                            if (ImPlot::BeginPlot("Search Results (Z-Scored)", plotSize)) {
                                ImPlot::SetupAxes("Index", "Norm Value");
                                
                                int new_hovered = -1;

                                // 1. Plot Matches (Background)
                                for (size_t i = 0; i < g_SearchResults.size(); ++i) {
                                    const auto& res = g_SearchResults[i];
                                    if (res.stockPtr) {
                                        // Reconstruct Scaled Data
                                        std::vector<double> scaledData;
                                        if (res.scale == 1) {
                                            scaledData = res.stockPtr->data;
                                        } else {
                                            scaledData = res.stockPtr->data;
                                            for (int s = 1; s < res.scale; s *= 2) {
                                                scaledData = AnalysisEngine::Downsample(scaledData);
                                            }
                                        }

                                        int start = res.offset;
                                        int len = 400; // 300 match + 100 future
                                        if (start + len > (int)scaledData.size()) len = (int)scaledData.size() - start;
                                        
                                        if (len > 0) {
                                            std::vector<double> segment(scaledData.begin() + start, 
                                                                        scaledData.begin() + start + len);
                                            std::vector<double> norm = Normalize(segment);
                                            std::string label = res.symbol + " (D:" + std::to_string(res.distance).substr(0,4) + ")";
                                            
                                            // Determine Style based on Legend Hover
                                            float alpha = 0.5f; 
                                            float line_width = 1.0f;
                                            if (s_HoveredIdx != -1) {
                                                if ((int)i == s_HoveredIdx) {
                                                    alpha = 1.0f;
                                                    line_width = 2.0f;
                                                } else {
                                                    alpha = 0.05f; // Dim others significantly
                                                }
                                            }

                                            ImPlot::SetNextLineStyle(ImVec4(0.5f, 0.5f, 0.5f, alpha), line_width);
                                            ImPlot::PlotLine(label.c_str(), norm.data(), static_cast<int>(norm.size()));
                                            
                                            if (ImPlot::IsLegendEntryHovered(label.c_str())) {
                                                new_hovered = (int)i;
                                            }
                                        }
                                    }
                                }
                                s_HoveredIdx = new_hovered;
                                
                                // Plot Median Bundle
                                if (!g_MedianData.empty()) {
                                    ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 2.0f); // Thick White
                                    ImPlot::PlotLine("Median Bundle", g_MedianData.data(), static_cast<int>(g_MedianData.size()));
                                }

                                // Match Query Stats for Normalization
                                int patternLen = std::min((int)s_DisplayQuery.size(), 300);
                                double sum = 0.0;
                                for(int i=0; i<patternLen; ++i) sum += s_DisplayQuery[i];
                                double mean = sum / patternLen;
                                double sq_sum = 0.0;
                                for(int i=0; i<patternLen; ++i) sq_sum += (s_DisplayQuery[i] - mean) * (s_DisplayQuery[i] - mean);
                                double stdev = std::sqrt(sq_sum / patternLen);
                                if (stdev == 0) stdev = 1.0; 

                                // 2. Plot Query (Foreground)
                                std::vector<double> normQuery;
                                for (double v : s_DisplayQuery) normQuery.push_back((v - mean) / stdev);
                                
                                ImPlot::SetNextLineStyle(ImVec4(0.1f, 1.0f, 1.0f, 1.0f), 1.5f); // Bright Cyan
                                ImPlot::PlotLine("Query", normQuery.data(), static_cast<int>(normQuery.size()));

                                // 3. Prediction
                                if (!g_PredictionData.empty()) {
                                    std::vector<double> normPred;
                                    for (double v : g_PredictionData) normPred.push_back((v - mean) / stdev);
                                    ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), 1.5f); // Gold
                                    ImPlot::PlotLine("Prediction (Avg)", normPred.data(), static_cast<int>(normPred.size()), 1.0, 300.0);
                                }
                                
                                // 4. Prediction Zone Line
                                double cutoff = 300.0;
                                ImPlot::PlotInfLines("Prediction", &cutoff, 1);

                                ImPlot::EndPlot();
                            }

                            // --- COLUMN 1: EV Distribution ---
                            ImGui::TableSetColumnIndex(1);
                            if (ImPlot::BeginPlot("EV Dist", ImVec2(-1, plotSize.y))) {
                                ImPlot::SetupAxes("Density", "Z-Score");
                                
                                if (!g_FuturePoints.empty()) {
                                    // 1. Calculate Query Stats (Same as Main Plot)
                                    int patternLen = std::min((int)s_DisplayQuery.size(), 300);
                                    double sum = 0.0;
                                    for(int i=0; i<patternLen; ++i) sum += s_DisplayQuery[i];
                                    double mean = sum / patternLen;
                                    double sq_sum = 0.0;
                                    for(int i=0; i<patternLen; ++i) sq_sum += (s_DisplayQuery[i] - mean) * (s_DisplayQuery[i] - mean);
                                    double stdev = std::sqrt(sq_sum / patternLen);
                                    if (stdev == 0) stdev = 1.0;
                                    
                                    double query_last = s_DisplayQuery[patternLen - 1];

                                    // 2. Generate KDE
                                    std::vector<double> y_vals;
                                    std::vector<double> density;
                                    double sigma = 0.3; 
                                    
                                    // Iterate Y (Z-Score space)
                                    for (double y = -5.0; y <= 5.0; y += 0.1) {
                                        double d = 0;
                                        for (const auto& p : g_FuturePoints) {
                                            // p.z is already the Projectable Z-Score
                                            double diff = y - p.z;
                                            d += p.weight * std::exp(-(diff*diff)/(2*sigma*sigma));
                                        }
                                        y_vals.push_back(y);
                                        density.push_back(d);
                                    }
                                    
                                    ImPlot::PlotLine("EV Density", density.data(), y_vals.data(), density.size());
                                    
                                    // Calculate Weighted Average Z (EV)
                                    double total_weight = 0.0;
                                    double weighted_sum_z = 0.0;
                                    for (const auto& p : g_FuturePoints) {
                                        weighted_sum_z += p.z * p.weight;
                                        total_weight += p.weight;
                                    }
                                    double avg_z = (total_weight > 0) ? (weighted_sum_z / total_weight) : 0.0;

                                    // Calculate EV % Return (Using Query Volatility)
                                    // Projected Price = avg_z * stdev + mean
                                    double predicted_price = avg_z * stdev + mean;
                                    double pct = 0.0;
                                    if (std::abs(query_last) > 1e-9) {
                                        pct = (predicted_price - query_last) / query_last * 100.0;
                                    }

                                    // Breakeven Z (Where Price = query_last)
                                    double break_z = (query_last - mean) / stdev;

                                    // Plot Breakeven Line (Gray Dotted)
                                    double line_xs[2] = {0, 100}; 
                                    double be_ys[2] = {break_z, break_z};
                                    ImPlot::SetNextLineStyle(ImVec4(0.7f, 0.7f, 0.7f, 0.5f));
                                    ImPlot::PlotLine("Breakeven", line_xs, be_ys, 2);

                                    // Plot EV Line (Green/Red)
                                    double ev_ys[2] = {avg_z, avg_z};
                                    // Green if predicted price > current price (pct > 0)
                                    ImVec4 color = (pct >= 0) ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1);
                                    ImPlot::SetNextLineStyle(color, 1.5f);
                                    ImPlot::PlotLine("EV", line_xs, ev_ys, 2);

                                    // Annotation
                                    char label[32];
                                    sprintf(label, "EV: %+.1f%%", pct);
                                    ImPlot::Annotation(50, avg_z, color, ImVec2(0, -10), false, "%s", label);

                                    // Actual Outcome Line (Testing Mode)
                                    if (g_TestingMode && s_DisplayQuery.size() > 399) {
                                        double actual_val = s_DisplayQuery[399];
                                        double actual_z = (actual_val - mean) / stdev;
                                        
                                        double actual_ys[2] = {actual_z, actual_z};
                                        ImPlot::SetNextLineStyle(ImVec4(0,1,1,1));
                                        ImPlot::PlotLine("Actual", line_xs, actual_ys, 2);
                                    }
                                }
                                ImPlot::EndPlot();
                            }
                            
                            ImGui::EndTable();
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
