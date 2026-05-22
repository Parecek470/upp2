#include <iostream>
#include <string>
#include <vector>
#include <mpi.h>

struct URLParts {
    std::string scheme;
    std::string domain;
    std::string path;
};

URLParts parseURL(const std::string& url) {
    URLParts parts;

    // Find "://" and extract scheme
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        return parts; // malformed URL

    parts.scheme = url.substr(0, schemeEnd);

    // Start of domain is right after "://"
    size_t domainStart = schemeEnd + 3;

    // Extract domain until first '/' after the authority
    size_t pathStart = url.find('/', domainStart);

    if (pathStart == std::string::npos) {
        // No path found — domain is the rest of the string
        parts.domain = url.substr(domainStart);
        parts.path = "/";
    } else {
        parts.domain = url.substr(domainStart, pathStart - domainStart);
        parts.path = url.substr(pathStart); // includes the leading '/'
    }

    return parts;
}

int countTag(const std::string& html, const std::string& tag) {
    int count = 0;
    size_t pos = 0;

    // Loop through html, finding each occurrence of tag
    while ((pos = html.find(tag, pos)) != std::string::npos) {
        count++;
        pos += tag.length(); // Move past the last found tag
    }

    return count;
}

static bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> extractLinks(const std::string& html, const std::string& baseUrl) {
    std::vector<std::string> links;
    size_t pos = 0;
    
    while ((pos = html.find("<a ", pos)) != std::string::npos) {
        size_t closingTag = html.find(">", pos);
        if (closingTag == std::string::npos) break;
        
        size_t hrefStart = html.find("href=\"", pos);
        if (hrefStart != std::string::npos && hrefStart < closingTag) {
            hrefStart += 6; // Move past 'href="'
            size_t hrefEnd = html.find("\"", hrefStart);
            if (hrefEnd != std::string::npos && hrefEnd < closingTag) {
                std::string href = html.substr(hrefStart, hrefEnd - hrefStart);
                
                // Convert relative to absolute
                std::string absUrl;
                if (href.find("://") != std::string::npos) {
                    // Already absolute
                    absUrl = href;
                } else if (href[0] == '/') {
                    // Root-relative: prepend scheme + domain
                    URLParts base = parseURL(baseUrl);
                    absUrl = base.scheme + "://" + base.domain + href;
                } else {
                    // Skip relative paths or just skip
                    pos += 3;
                    continue;
                }
                
                // Check if it starts with baseUrl
                if (absUrl.find(baseUrl) == 0) {
                    links.push_back(absUrl);
                }
            }
        }
        
        pos += 3;
    }
    
    return links;
}

std::vector<std::string> extractHeadings(const std::string& html) {
    std::vector<std::string> headings;
    size_t pos = 0;
    
    while (pos < html.size()) {
        // Find next heading of any level
        size_t nextPos = std::string::npos;
        int foundLevel = 0;
        
        // Check which heading comes first
        for (int level = 1; level <= 6; ++level) {
            std::string openTag = "<h" + std::to_string(level) + ">";
            size_t found = html.find(openTag, pos);
            if (found != std::string::npos && (nextPos == std::string::npos || found < nextPos)) {
                nextPos = found;
                foundLevel = level;
            }
        }
        
        if (nextPos == std::string::npos) break; // No more headings
        
        // Extract this heading
        std::string openTag = "<h" + std::to_string(foundLevel) + ">";
        std::string closeTag = "</h" + std::to_string(foundLevel) + ">";
        
        size_t textStart = nextPos + openTag.length();
        size_t closePos = html.find(closeTag, textStart);
        
        if (closePos != std::string::npos) {
            std::string text = html.substr(textStart, closePos - textStart);
            std::string prefix(foundLevel, '-');
            headings.push_back(prefix + " " + text);
            pos = closePos + closeTag.length();
        } else {
            break;
        }
    }
    
    return headings;
}


void workerB(int rank, int workerAId) {
    while (true) {
        // 1. Receive URL from Worker A
        char urlBuffer[1024];
        MPI_Status status;
        MPI_Recv(urlBuffer, 1024, MPI_CHAR, workerAId, 0, MPI_COMM_WORLD, &status);
        
        std::string url(urlBuffer);
        if (url == "DONE") break; // Signal to stop
        
        // 2. Download HTML
        std::string html = utils::downloadHTML(url);
        
        // 3. Parse and count
        int imgCount = countTag(html, "<img ");
        int formCount = countTag(html, "<form ");
        std::vector<std::string> links = extractLinks(html, url);
        std::vector<std::string> headings = extractHeadings(html);
        
        // 4. Serialize to string
        std::string result = "URL:" + url + "\n";
        result += "IMAGES:" + std::to_string(imgCount) + "\n";
        result += "LINKS:" + std::to_string(links.size()) + "\n";
        result += "FORMS:" + std::to_string(formCount) + "\n";
        
        for (const auto& link : links) {
            result += "LINK:" + link + "\n";
        }
        
        for (const auto& heading : headings) {
            result += "HEADING:" + heading + "\n";
        }
        
        // 5. Send back to Worker A
        MPI_Send(result.c_str(), result.size() + 1, MPI_CHAR, workerAId, 0, MPI_COMM_WORLD);
    }
}