#include <vk_engine.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <windows.h>
#include <shobjidl.h> 
#include <map>
#include <string>
#include <sstream>
#include <filesystem>

//send to both the console and the text file
class TeeBuffer : public std::streambuf {
    std::streambuf* sbuf1;
    std::streambuf* sbuf2;
public:
    TeeBuffer(std::streambuf* sb1, std::streambuf* sb2) :
        sbuf1(sb1), sbuf2(sb2) {}
protected:
    virtual int overflow(int c) {
        if (c == EOF) return !EOF;
        else {
            int const r1 = sbuf1->sputc(c);
            int const r2 = sbuf2->sputc(c);
            return r1 == EOF || r2 == EOF ? EOF : c;
        }
    }
};

// Struct for performance metrics
struct PerformanceMetrics {
    long long init_swapchain;
    long long init_commands;
    long long init_synch_structures;
    long long init_descriptors;
    long long init_pipelines;
    long long total_time;
};

// custom output buffer to capture metrics
class MetricBuffer : public std::streambuf {
private:
    std::streambuf* originalBuf;
    PerformanceMetrics& metrics;
    std::string buffer;

protected:
    virtual int overflow(int c) {
        if (c == EOF) return !EOF;

        buffer += static_cast<char>(c);

        if (c == '\n') {
            std::string line = buffer;

            // Parse metrics from the line
            if (line.find("init_swapchain()") != std::string::npos) {
                metrics.init_swapchain = extractTime(line);
            }
            else if (line.find("init_commands()") != std::string::npos) {
                metrics.init_commands = extractTime(line);
            }
            else if (line.find("init_synch_structures()") != std::string::npos) {
                metrics.init_synch_structures = extractTime(line);
            }
            else if (line.find("init_descriptors()") != std::string::npos) {
                metrics.init_descriptors = extractTime(line);
            }
            else if (line.find("init_pipelines()") != std::string::npos) {
                metrics.init_pipelines = extractTime(line);
            }
            else if (line.find("Total time taken") != std::string::npos) {
                metrics.total_time = extractTotalTime(line);
            }

            // forward to original buffer
            originalBuf->sputn(buffer.c_str(), buffer.size());
            buffer.clear();
        }

        return c;
    }

private:
    long long extractTime(const std::string& line) {
        size_t pos = line.find("took");
        if (pos != std::string::npos) {
            std::string timeStr = line.substr(pos + 5);
            std::stringstream ss(timeStr);
            long long value;
            std::string unit;
            ss >> value >> unit;

            // Convert milliseconds to microseconds
            if (unit.find("milli") != std::string::npos) {
                value *= 1000;  // convert to microseconds
            }
            // microseconds values stay 

            return value;
        }
        return 0;
    }

    long long extractTotalTime(const std::string& line) {
        size_t pos = line.find(":");
        if (pos != std::string::npos) {
            std::string timeStr = line.substr(pos + 2);
            std::stringstream ss(timeStr);
            long long value;
            std::string unit;
            ss >> value >> unit;

            // Convert milliseconds to microseconds
            if (unit.find("milli") != std::string::npos) {
                value *= 1000;  // convert to microseconds
            }
            return value;
        }
        return 0;
    }

public:
    MetricBuffer(std::streambuf* buf, PerformanceMetrics& m)
        : originalBuf(buf), metrics(m) {}
};

// selecting a folder
std::string SelectFolder() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        return "";
    }
    std::string selectedPath;
    IFileDialog* pfd = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
            pfd->SetOptions(dwOptions | FOS_PICKFOLDERS); // Set to select folders
        }
        hr = pfd->Show(NULL);
        if (SUCCEEDED(hr)) {
            IShellItem* psi;
            hr = pfd->GetResult(&psi);
            if (SUCCEEDED(hr)) {
                PWSTR pszFilePath;
                hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr)) {
                    selectedPath = std::string(pszFilePath, pszFilePath + wcslen(pszFilePath));
                    CoTaskMemFree(pszFilePath);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
    CoUninitialize();
    return selectedPath;
}

// save baseline metrics to a file
void saveBaselineMetrics(const PerformanceMetrics& metrics, const std::string& filepath) {
    std::ofstream file(filepath);
    file << "BASELINE_METRICS\n";  // Header to identify file type
    file << "init_swapchain=" << metrics.init_swapchain << "\n";
    file << "init_commands=" << metrics.init_commands << "\n";
    file << "init_synch_structures=" << metrics.init_synch_structures << "\n";
    file << "init_descriptors=" << metrics.init_descriptors << "\n";
    file << "init_pipelines=" << metrics.init_pipelines << "\n";
    file << "total_time=" << metrics.total_time << "\n";
}

// read baseline metrics from file
PerformanceMetrics readBaselineMetrics(const std::string& filepath) {
    PerformanceMetrics metrics = { 0 };
    std::ifstream file(filepath);
    std::string line;

    // Check for header
    std::getline(file, line);
    if (line != "BASELINE_METRICS") {
        std::cout << "Warning: Baseline file format not recognized\n";
        return metrics;
    }

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        long long value;

        std::getline(iss, key, '=');
        iss >> value;

        if (key == "init_swapchain") metrics.init_swapchain = value;
        else if (key == "init_commands") metrics.init_commands = value;
        else if (key == "init_synch_structures") metrics.init_synch_structures = value;
        else if (key == "init_descriptors") metrics.init_descriptors = value;
        else if (key == "init_pipelines") metrics.init_pipelines = value;
        else if (key == "total_time") metrics.total_time = value;
    }
    return metrics;
}

// function to select baseline file
std::string selectBaselineFile() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) return "";

    std::string selectedPath;
    IFileDialog* pfd = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));

    if (SUCCEEDED(hr)) {
        // set file type filter for .txt files
        COMDLG_FILTERSPEC fileTypes[] = {
            { L"Baseline Files", L"*.txt" },
            { L"All Files", L"*.*" }
        };
        pfd->SetFileTypes(2, fileTypes);

        // Show the dialog
        hr = pfd->Show(NULL);
        if (SUCCEEDED(hr)) {
            IShellItem* psi;
            hr = pfd->GetResult(&psi);
            if (SUCCEEDED(hr)) {
                PWSTR pszFilePath;
                hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr)) {
                    selectedPath = std::string(pszFilePath, pszFilePath + wcslen(pszFilePath));
                    CoTaskMemFree(pszFilePath);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
    CoUninitialize();
    return selectedPath;
}

// function to compare and print performance differences
void comparePerformance(const PerformanceMetrics& current, const PerformanceMetrics& baseline) {
    std::cout << "\n=== Performance Comparison vs Baseline (in microseconds) ===\n";

    auto calculateDiff = [](long long current, long long baseline) -> float {
        return baseline != 0 ? ((float)current - baseline) / baseline * 100.0f : 0.0f;
        };

    auto formatDiff = [](long long current, long long baseline, float diff) -> std::string {
        std::stringstream ss;
        ss << current << "µs vs " << baseline << "µs (";
        if (diff > 0)
            ss << "+" << diff << "% slower)";
        else
            ss << diff << "% faster)";
        return ss.str();
        };

    std::cout << "init_swapchain: " << formatDiff(current.init_swapchain, baseline.init_swapchain,
        calculateDiff(current.init_swapchain, baseline.init_swapchain)) << "\n";
    std::cout << "init_commands: " << formatDiff(current.init_commands, baseline.init_commands,
        calculateDiff(current.init_commands, baseline.init_commands)) << "\n";
    std::cout << "init_synch_structures: " << formatDiff(current.init_synch_structures, baseline.init_synch_structures,
        calculateDiff(current.init_synch_structures, baseline.init_synch_structures)) << "\n";
    std::cout << "init_descriptors: " << formatDiff(current.init_descriptors, baseline.init_descriptors,
        calculateDiff(current.init_descriptors, baseline.init_descriptors)) << "\n";
    std::cout << "init_pipelines: " << formatDiff(current.init_pipelines, baseline.init_pipelines,
        calculateDiff(current.init_pipelines, baseline.init_pipelines)) << "\n";
    std::cout << "Total time: " << formatDiff(current.total_time, baseline.total_time,
        calculateDiff(current.total_time, baseline.total_time)) << "\n";
}

int main(int argc, char* argv[]) {
    char choice;
    std::cout << "Is this the baseline PC? (y/n): ";
    std::cin >> choice;

    // output directory
    std::cout << "Select directory for current run output:\n";
    std::string outputDirectory = SelectFolder();
    if (outputDirectory.empty()) {
        std::cout << "No directory selected. Exiting.\n";
        return 1;
    }

    std::string currentLogPath = outputDirectory + "\\performance_log.txt";
    std::string baselineLogPath;

    if (choice == 'y') {
        // save in selected baseline directory
        baselineLogPath = outputDirectory + "\\baseline_performance.txt";
    }
    else {
        // selecting the baseline file if it not the baseline pc
        std::cout << "Please select the baseline performance file (baseline_performance.txt):\n";
        baselineLogPath = selectBaselineFile();
        if (baselineLogPath.empty()) {
            std::cout << "No baseline file selected. Will only save current performance data.\n";
        }
    }

    // initialize metrics structure
    PerformanceMetrics currentMetrics = { 0 };

    // open the current runs output file
    std::ofstream outFile(currentLogPath);

    // setup metric capture buffer
    MetricBuffer metricBuf(std::cout.rdbuf(), currentMetrics);
    TeeBuffer teeBuf(&metricBuf, outFile.rdbuf());

    // save cout buffer and redirect it to tee buffer
    std::streambuf* coutbuf = std::cout.rdbuf();
    std::cout.rdbuf(&teeBuf);

    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::cout << "\n=== Run at " << std::ctime(&time) << "===\n";

    // Run the engine and measure performance
    auto start = std::chrono::high_resolution_clock::now();
    VulkanEngine engine;
    engine.init();
    engine.run();
    auto end = std::chrono::high_resolution_clock::now();
    engine.cleanup();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Total time taken for init and run: " << duration << " milliseconds.\n";

    // handle the baseline file
    if (choice == 'y') {
        // Save baseline metrics 
        saveBaselineMetrics(currentMetrics, baselineLogPath);
        std::cout << "\nBaseline metrics saved to: " << baselineLogPath << "\n";
        std::cout << "Copy this file to compare performance on other PCs.\n";
    }
    else if (!baselineLogPath.empty()) {
        // Compare with baseline 
        auto baselineMetrics = readBaselineMetrics(baselineLogPath);
        comparePerformance(currentMetrics, baselineMetrics);
    }

    // testore cout buffer
    std::cout.rdbuf(coutbuf);
    return 0;
}