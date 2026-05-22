#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>

struct PageData {
    std::string url;
    int imageCount;
    int linkCount;
    int formCount;
    std::vector<std::string> headings;
};

struct CrawlResults {
    std::string baseUrl;
    std::map<std::string, PageData> pages;
    std::set<std::pair<std::string, std::string>> edges;
};

CrawlResults parseMasterResult(const std::string& msg);
std::string getCurrentTimestamp();
std::string makeDirectoryName(const std::string& baseUrl);
void createDirectory(const std::string& path);
void writeMapFile(const std::string& filepath, const CrawlResults& results);
void writeContentFile(const std::string& filepath, const CrawlResults& results);
void writeLogFile(const std::string& filepath, const std::string& startTime, 
                  const std::string& endTime, const std::string& status);