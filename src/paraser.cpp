#include "parser.h"
#include <algorithm>
#include <sstream>


static bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

URLParts parseURL(const std::string& url) {
    URLParts parts;

    // Find "://" and extract scheme
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        return parts;

    parts.scheme = url.substr(0, schemeEnd);
    size_t domainStart = schemeEnd + 3;
    size_t pathStart = url.find('/', domainStart);

    if (pathStart == std::string::npos) {
        parts.domain = url.substr(domainStart);
        parts.path = "/";
    } else {
        parts.domain = url.substr(domainStart, pathStart - domainStart);
        parts.path = url.substr(pathStart); 
    }

    return parts;
}

// Normalize a URL
static std::string normalizePath(const std::string& path) {
    std::vector<std::string> segments;
    
    bool leadingSlash = (!path.empty() && path[0] == '/');

    size_t start = 0;
    while (start < path.size()) {
        size_t slash = path.find('/', start);
        if (slash == std::string::npos) {
            segments.push_back(path.substr(start));
            break;
        }
        if (slash > start)
            segments.push_back(path.substr(start, slash - start));
        else
            segments.push_back(""); 
        start = slash + 1;
    }

    std::vector<std::string> resolved;
    for (const auto& s : segments) {
        if (s == "." || s.empty()) {
            // skip
        } else if (s == "..") {
            if (!resolved.empty()) resolved.pop_back();
        } else {
            resolved.push_back(s);
        }
    }

    std::string result = leadingSlash ? "/" : "";
    for (size_t i = 0; i < resolved.size(); ++i) {
        if (i > 0) result += "/";
        result += resolved[i];
    }


    if (result != "/" && !path.empty() && path.back() == '/')
        result += "/";
    if (result.empty()) result = "/";

    return result;
}

//counts occurrences of a substring in a string
int countTag(const std::string& html, const std::string& tag) {
    int count = 0;
    size_t pos = 0;

    while ((pos = html.find(tag, pos)) != std::string::npos) {
        count++;
        pos += tag.length(); 
    }

    return count;
}

std::vector<std::string> extractLinks(const std::string& html, const std::string& baseUrl) {
    std::vector<std::string> links;
    size_t pos = 0;

    URLParts baseParts = parseURL(baseUrl);
    std::string baseScheme = baseParts.scheme;
    std::string baseDomain = baseParts.domain;          

    while ((pos = html.find("<a ", pos)) != std::string::npos) {
        size_t closingTag = html.find('>', pos);
        if (closingTag == std::string::npos) break;

        size_t hrefPos = html.find("href", pos);
        if (hrefPos == std::string::npos || hrefPos >= closingTag) {
            pos += 3;
            continue;
        }

        size_t eq = html.find('=', hrefPos);
        if (eq == std::string::npos || eq >= closingTag) {
            pos += 3;
            continue;
        }
        size_t quoteOpen = eq + 1;
        if (quoteOpen >= closingTag || html[quoteOpen] != '"') {
            pos += 3;
            continue;
        }
        size_t hrefStart = quoteOpen + 1;
        size_t hrefEnd   = html.find('"', hrefStart);
        if (hrefEnd == std::string::npos || hrefEnd > closingTag) {
            pos += 3;
            continue;
        }

        std::string href = html.substr(hrefStart, hrefEnd - hrefStart);

        // Skip empty, anchor-only, javascript:, mailto: etc.
        if (href.empty() || href[0] == '#' ||
            href.find("javascript:") == 0 || href.find("mailto:") == 0) {
            pos += 3;
            continue;
        }

        // Convert to absolute URL
        std::string absUrl;
        if (href.find("://") != std::string::npos) {
            absUrl = href;
        } else if (!href.empty() && href[0] == '/') {
            absUrl = baseScheme + "://" + baseDomain + href;
        } else {
            std::string basePath = baseParts.path;
            size_t lastSlash = basePath.rfind('/');
            std::string baseDir = (lastSlash != std::string::npos) ? basePath.substr(0, lastSlash + 1) : "/";
            absUrl = baseScheme + "://" + baseDomain + baseDir + href;
        }

        URLParts linkParts = parseURL(absUrl);
        if (linkParts.domain == baseDomain ) {
            std::string cleanUrl = baseScheme + "://" + baseDomain + normalizePath(linkParts.path);
            size_t fragPos = cleanUrl.find('#');
            if (fragPos != std::string::npos)
                cleanUrl = cleanUrl.substr(0, fragPos);
            links.push_back(cleanUrl);
        }

        pos += 3;
    }

    return links;
}


// Strip all HTML tags from a string (e.g. heading text may contain <span>, <br>, etc.)
static std::string stripTags(const std::string& s) {
    std::string out;
    bool inTag = false;
    for (char c : s) {
        if (c == '<')      inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag)   out += c;
    }
    size_t start = out.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = out.find_last_not_of(" \t\r\n");
    return out.substr(start, end - start + 1);
}

std::vector<std::string> extractHeadings(const std::string& html) {
    std::vector<std::string> headings;
    size_t pos = 0;

    while (pos < html.size()) {
        size_t nextPos   = std::string::npos;
        int    foundLevel = 0;

        for (int level = 1; level <= 6; ++level) {
            std::string prefix = "<h" + std::to_string(level);
            size_t found = html.find(prefix, pos);
            while (found != std::string::npos) {
                size_t after = found + prefix.size();
                if (after < html.size() && (html[after] == '>' || html[after] == ' ' || html[after] == '\t' || html[after] == '\n' || html[after] == '\r')) {
                    if (nextPos == std::string::npos || found < nextPos) {
                        nextPos    = found;
                        foundLevel = level;
                    }
                    break;
                }
                found = html.find(prefix, found + 1);
            }
        }

        if (nextPos == std::string::npos) break; // No more headings

        // Find the '>' that closes the opening tag (may have attributes)
        size_t tagClose = html.find('>', nextPos);
        if (tagClose == std::string::npos) break;

        std::string closeTag = "</h" + std::to_string(foundLevel) + ">";
        size_t textStart = tagClose + 1;
        size_t closePos  = html.find(closeTag, textStart);

        if (closePos != std::string::npos) {
            std::string raw  = html.substr(textStart, closePos - textStart);
            std::string text = stripTags(raw);  // remove any inline tags like <span>
            std::string prefix(foundLevel, '-');
            if (!text.empty())
                headings.push_back(prefix + " " + text);
            pos = closePos + closeTag.size();
        } else {
            break;
        }
    }

    return headings;
}