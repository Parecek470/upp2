#pragma once
#include <string>
#include <vector>


struct URLParts {
    std::string scheme;
    std::string domain;
    std::string path;
};

URLParts parseURL(const std::string& url);
int countTag(const std::string& html, const std::string& tag);
std::vector<std::string> extractLinks(const std::string& html, const std::string& baseUrl);
std::vector<std::string> extractHeadings(const std::string& html);