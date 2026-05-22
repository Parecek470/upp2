#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <set>
#include <map>
#include <sstream>
#include <cctype>
#include <mpi.h>

#include "workerA.h"
#include "parser.h"

struct PageData {
    std::string url;
    int imageCount = 0, linkCount = 0, formCount = 0;
    std::vector<std::string> headings;
    std::vector<std::string> foundLinks;
};

PageData parseWorkerBResult(const std::string& msg) {
    PageData data;
    std::istringstream stream(msg);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.rfind("URL:", 0) == 0)  // Fixed: colon not pipe
            data.url = line.substr(4);
        else if (line.rfind("IMAGES:", 0) == 0)
            data.imageCount = std::stoi(line.substr(7));
        else if (line.rfind("LINKS:", 0) == 0)
            data.linkCount = std::stoi(line.substr(6));
        else if (line.rfind("FORMS:", 0) == 0)
            data.formCount = std::stoi(line.substr(6));
        else if (line.rfind("HEADING:", 0) == 0)
            data.headings.push_back(line.substr(8));
        else if (line.rfind("LINK:", 0) == 0)
            data.foundLinks.push_back(line.substr(5));
    }
    return data;
}

std::string serializeResults(const std::string& baseUrl,
                              const std::map<std::string, PageData>& pageResults,
                              const std::vector<std::pair<std::string, std::string>>& linkGraph) {
    std::string result;

    result += "BASE_URL|" + baseUrl + "\n";

    result += "BEGIN_PAGES\n";
    for (const auto& [url, data] : pageResults) {
        result += "PAGE|" + url + "\n";
        result += "IMAGES|" + std::to_string(data.imageCount) + "\n";
        result += "LINKS|" + std::to_string(data.linkCount) + "\n";
        result += "FORMS|" + std::to_string(data.formCount) + "\n";
        for (const auto& heading : data.headings) {
            result += "HEADING|" + heading + "\n";
        }
        result += "END_PAGE\n";
    }
    result += "END_PAGES\n";

    result += "BEGIN_GRAPH\n";
    for (const auto& [source, target] : linkGraph) {
        result += "EDGE|" + source + "|" + target + "\n";
    }
    result += "END_GRAPH\n";

    return result;
}

void runWorkerA(int rank, int N, int M) {  // Fixed: runWorkerA not workerA
    // Data structures
    std::set<std::string> visitedUrls;
    std::queue<std::string> workQueue;
    std::map<std::string, PageData> pageResults;
    std::vector<std::pair<std::string, std::string>> linkGraph;
    std::string baseUrl;

    // 1. Receive initial URL from Master
    MPI_Status status;
    MPI_Probe(0, 0, MPI_COMM_WORLD, &status);
    int urlSize;
    MPI_Get_count(&status, MPI_CHAR, &urlSize);
    
    char* urlBuffer = new char[urlSize];
    MPI_Recv(urlBuffer, urlSize, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    
    baseUrl = std::string(urlBuffer);
    workQueue.push(baseUrl);
    delete[] urlBuffer;

    int activeWorkers = 0;

    // 2. Main loop
    while (!workQueue.empty() || activeWorkers > 0) {
        // Probe for message size
        MPI_Probe(MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
        int msgSize;
        MPI_Get_count(&status, MPI_CHAR, &msgSize);
        
        char* buffer = new char[msgSize];
        MPI_Recv(buffer, msgSize, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        int workerBRank = status.MPI_SOURCE;

        std::string msg(buffer);
        delete[] buffer;

        if (msg == "READY") {
            std::string nextUrl;
            bool foundWork = false;

            // Find next unvisited URL
            while (!workQueue.empty()) {
                nextUrl = workQueue.front();
                workQueue.pop();

                if (!visitedUrls.count(nextUrl)) {
                    visitedUrls.insert(nextUrl);
                    foundWork = true;
                    break;
                }
            }

            if (foundWork) {
                MPI_Send(nextUrl.c_str(), nextUrl.size() + 1, MPI_CHAR, workerBRank, 0, MPI_COMM_WORLD);
                activeWorkers++;
            } else {
                std::string done = "DONE";
                MPI_Send(done.c_str(), done.size() + 1, MPI_CHAR, workerBRank, 0, MPI_COMM_WORLD);
            }
        } else {
            // Worker B sent results
            activeWorkers--;

            PageData result = parseWorkerBResult(msg);

            // Store result
            pageResults[result.url] = result;

            // Build graph edges
            for (const auto& link : result.foundLinks) {
                linkGraph.push_back({result.url, link});

                // Add new URLs to queue if not yet visited
                if (!visitedUrls.count(link)) {
                    workQueue.push(link);
                }
            }
        }
    }

    // 3. Send aggregated results back to Master
    std::string finalResults = serializeResults(baseUrl, pageResults, linkGraph);
    MPI_Send(finalResults.c_str(), finalResults.size() + 1, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
}