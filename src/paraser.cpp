#include "parser.h"
#include <algorithm>

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

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

std::vector<std::string> extractLinks(const std::string& html, const std::string& baseUrl) {
    std::vector<std::string> links;
    size_t pos = 0;

    // Parse the base URL once to get scheme + domain for relative-URL resolution
    // and for domain-matching against discovered links.
    URLParts baseParts = parseURL(baseUrl);
    // Canonical base prefix used for domain-matching: scheme://domain
    // We compare against both http and https variants of the same domain so a
    // page downloaded via https but submitted as http still matches.
    std::string baseScheme = baseParts.scheme;          // "http" or "https"
    std::string baseDomain = baseParts.domain;          // e.g. "upp-test-2.martinubl.cz"

    while ((pos = html.find("<a ", pos)) != std::string::npos) {
        size_t closingTag = html.find('>', pos);
        if (closingTag == std::string::npos) break;

        // Find "href" attribute within this tag
        size_t hrefPos = html.find("href", pos);
        if (hrefPos == std::string::npos || hrefPos >= closingTag) {
            pos += 3;
            continue;
        }

        // Step over "href" and optional whitespace, then '=', then opening quote
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
            // Root-relative: use same scheme and domain as base
            absUrl = baseScheme + "://" + baseDomain + href;
        } else {
            // Relative path: resolve against base URL's directory
            // e.g. base="http://example.com/foo/bar", href="baz.html" -> "http://example.com/foo/baz.html"
            std::string basePath = baseParts.path;
            size_t lastSlash = basePath.rfind('/');
            std::string baseDir = (lastSlash != std::string::npos) ? basePath.substr(0, lastSlash + 1) : "/";
            absUrl = baseScheme + "://" + baseDomain + baseDir + href;
        }

        // Accept links that belong to the same domain regardless of scheme.
        // Normalize to baseScheme so all URLs in the graph are consistent
        // (avoids scheme mismatch in visitedUrls, toRelativePath, and downloadHTML).
        URLParts linkParts = parseURL(absUrl);
        if (linkParts.domain == baseDomain) {
            // Use baseScheme, not linkParts.scheme, so http/https mismatches don't fragment the graph
            std::string cleanUrl = baseScheme + "://" + baseDomain + linkParts.path;
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
    // Trim leading/trailing whitespace
    size_t start = out.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = out.find_last_not_of(" \t\r\n");
    return out.substr(start, end - start + 1);
}

std::vector<std::string> extractHeadings(const std::string& html) {
    std::vector<std::string> headings;
    size_t pos = 0;

    while (pos < html.size()) {
        // Find whichever heading level appears next
        size_t nextPos   = std::string::npos;
        int    foundLevel = 0;

        for (int level = 1; level <= 6; ++level) {
            // Match <h1> or <h1 ...> — i.e. '<h' + digit followed by '>' or ' '
            std::string prefix = "<h" + std::to_string(level);
            size_t found = html.find(prefix, pos);
            while (found != std::string::npos) {
                // Make sure the character right after 'h<digit>' is '>' or whitespace
                size_t after = found + prefix.size();
                if (after < html.size() && (html[after] == '>' || html[after] == ' ' || html[after] == '\t' || html[after] == '\n' || html[after] == '\r')) {
                    if (nextPos == std::string::npos || found < nextPos) {
                        nextPos    = found;
                        foundLevel = level;
                    }
                    break;
                }
                // Could be e.g. <h10> — keep searching
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