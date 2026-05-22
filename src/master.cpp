#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <ctime>
#include <iomanip>
#include <mpi.h>

#include "master.h"


CrawlResults parseMasterResult(const std::string& msg) {
    CrawlResults results;
    std::istringstream stream(msg);
    std::string line;
    
    PageData currentPage;
    bool inPages = false;
    bool inGraph = false;
    
    while (std::getline(stream, line)) {
        if (line.rfind("BASE_URL|", 0) == 0) {
            results.baseUrl = line.substr(9);
        } else if (line == "BEGIN_PAGES") {
            inPages = true;
            inGraph = false;
        } else if (line == "END_PAGES") {
            inPages = false;
        } else if (line == "BEGIN_GRAPH") {
            inGraph = true;
            inPages = false;
        } else if (line == "END_GRAPH") {
            inGraph = false;
        } else if (inPages) {
            if (line.rfind("PAGE|", 0) == 0) {
                // Save previous page if exists
                if (!currentPage.url.empty()) {
                    results.pages[currentPage.url] = currentPage;
                }
                // Start new page
                currentPage = PageData();
                currentPage.url = line.substr(5);
            } else if (line.rfind("IMAGES|", 0) == 0) {
                currentPage.imageCount = std::stoi(line.substr(7));
            } else if (line.rfind("LINKS|", 0) == 0) {
                currentPage.linkCount = std::stoi(line.substr(6));
            } else if (line.rfind("FORMS|", 0) == 0) {
                currentPage.formCount = std::stoi(line.substr(6));
            } else if (line.rfind("HEADING|", 0) == 0) {
                currentPage.headings.push_back(line.substr(8));
            } else if (line == "END_PAGE") {
                // Save current page
                if (!currentPage.url.empty()) {
                    results.pages[currentPage.url] = currentPage;
                    currentPage = PageData();
                }
            }
        } else if (inGraph) {
            if (line.rfind("EDGE|", 0) == 0) {
                // Format: EDGE|source|target
                size_t firstPipe = 5;
                size_t secondPipe = line.find('|', firstPipe);
                if (secondPipe != std::string::npos) {
                    std::string source = line.substr(firstPipe, secondPipe - firstPipe);
                    std::string target = line.substr(secondPipe + 1);
                    results.edges.push_back({source, target});
                }
            }
        }
    }
    
    return results;
}

std::string getCurrentTimestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string makeDirectoryName(const std::string& baseUrl) {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y_%m_%d_%H_%M_");
    
    // Make safe name from URL
    std::string safe = baseUrl;
    for (char& c : safe) {
        if (!std::isalnum(c) && c != '-') {
            c = '_';
        }
    }
    oss << safe;
    
    return oss.str();
}

void createDirectory(const std::string& path) {
    mkdir(path.c_str(), 0755);
}

void writeMapFile(const std::string& filepath, const CrawlResults& results) {
    std::ofstream file(filepath);
    
    // Section 1: List all unique page URLs (nodes)
    for (const auto& [url, data] : results.pages) {
        file << "\"" << url << "\"\n";
    }
    
    // Section 2: List all edges
    for (const auto& [source, target] : results.edges) {
        file << "\"" << source << "\" \"" << target << "\"\n";
    }
    
    file.close();
}

void writeContentFile(const std::string& filepath, const CrawlResults& results) {
    std::ofstream file(filepath);
    
    for (const auto& [url, data] : results.pages) {
        file << "\"" << url << "\"\n";
        file << "IMAGES " << data.imageCount << "\n";
        file << "LINKS " << data.linkCount << "\n";
        file << "FORMS " << data.formCount << "\n";
        
        for (const auto& heading : data.headings) {
            file << heading << "\n";  // Already has dashes
        }
        file << "\n";  // Blank line between pages
    }
    
    file.close();
}

void writeLogFile(const std::string& filepath, const std::string& startTime, 
                  const std::string& endTime, const std::string& status) {
    std::ofstream file(filepath);
    file << startTime << "\n";
    file << endTime << "\n";
    file << status << "\n";
    file.close();
}