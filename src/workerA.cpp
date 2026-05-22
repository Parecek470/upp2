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


struct WorkerAPageData {
    std::string url;
    int imageCount = 0, linkCount = 0, formCount = 0;
    std::vector<std::string> headings;
    std::vector<std::string> foundLinks;
};

static WorkerAPageData parseWorkerBResult(const std::string& msg) {
    WorkerAPageData data{};
    std::istringstream stream(msg);
    std::string line;


    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty()) continue;

        if (line.rfind("URL:", 0) == 0) {
            data.url = line.substr(4);
        } else if (line.rfind("IMAGES:", 0) == 0) {
            try { data.imageCount = std::stoi(line.substr(7)); } catch (...) {}
        } else if (line.rfind("LINKS:", 0) == 0) {
            std::string val = line.substr(6);
            try { data.linkCount = std::stoi(val); } catch (...) {}
        } else if (line.rfind("FORMS:", 0) == 0) {
            try { data.formCount = std::stoi(line.substr(6)); } catch (...) {}
        } else if (line.rfind("LINK:", 0) == 0) {
            std::string val = line.substr(5);
            data.foundLinks.push_back(val);
        } else if (line.rfind("HEADING:", 0) == 0) {
            std::string val = line.substr(8);
            data.headings.push_back(val);
        }
    }
    return data;
}

static std::string serializeResults(
    const std::string& baseUrl,
    const std::map<std::string, WorkerAPageData>& pageResults,
    const std::vector<std::pair<std::string, std::string>>& linkGraph)
{
    std::string result;
    result += "BASE_URL|" + baseUrl + "\n";
    result += "BEGIN_PAGES\n";
    for (const auto& [url, data] : pageResults) {
        result += "PAGE|"   + url + "\n";
        result += "IMAGES|" + std::to_string(data.imageCount) + "\n";
        result += "LINKS|"  + std::to_string(data.linkCount)  + "\n";
        result += "FORMS|"  + std::to_string(data.formCount)  + "\n";
        for (const auto& h : data.headings)
            result += "HEADING|" + h + "\n";
        result += "END_PAGE\n";
    }
    result += "END_PAGES\n";
    result += "BEGIN_GRAPH\n";
    for (const auto& [s, t] : linkGraph)
        result += "EDGE|" + s + "|" + t + "\n";
    result += "END_GRAPH\n";
    return result;
}

// Safe blocking recv — Probe first, then Recv with exact size
static std::string safeRecv(int source, int tag, int myRank) {
    MPI_Status st;
    MPI_Probe(source, tag, MPI_COMM_WORLD, &st);

    int n = 0;
    MPI_Get_count(&st, MPI_CHAR, &n);
    if (n <= 0) {
        // Still must consume the message, recv with 1 byte
        char dummy = 0;
        MPI_Recv(&dummy, 0, MPI_CHAR, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        return "";
    }

    // Use a heap buffer via vector, guaranteed null at back
    std::vector<char> buf(static_cast<size_t>(n) + 1, '\0');
    MPI_Recv(buf.data(), n, MPI_CHAR, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    buf[n] = '\0';  // belt-and-suspenders null termination

    size_t len = (n > 0 && buf[n - 1] == '\0') ? static_cast<size_t>(n) - 1 
                                             : static_cast<size_t>(n);
    std::string s(buf.data(), len);
    // Trim any trailing null bytes just in case
    while (!s.empty() && s.back() == '\0')
        s.pop_back();

    return s;
}

// Outer loop: wait for WORK/SHUTDOWN from master on tag 1, then run one crawl job.
void runWorkerA(int rank, int N, int M) {
    while (true) {
        // Block until master sends a control message on tag 1
        MPI_Status ctrlStatus;
        MPI_Probe(0, 1, MPI_COMM_WORLD, &ctrlStatus);
        int ctrlLen = 0;
        MPI_Get_count(&ctrlStatus, MPI_CHAR, &ctrlLen);
        std::vector<char> ctrlBuf(ctrlLen + 1, '\0');
        MPI_Recv(ctrlBuf.data(), ctrlLen, MPI_CHAR, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        std::string ctrl(ctrlBuf.data());

        if (ctrl == "SHUTDOWN") break;
        // ctrl == "WORK" — fall through to run the job
        runWorkerA_job(rank, N, M);
    }
}

void runWorkerA_job(int rank, int N, int M) {
    const int firstB = N + 1 + (rank - 1) * M;
    std::string baseUrl = safeRecv(0, 0, rank);

    if (baseUrl.empty()) {
        std::string empty = serializeResults("", {}, {});
        MPI_Send(empty.c_str(), static_cast<int>(empty.size()) + 1, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
        return;
    }

    std::set<std::string>    visitedUrls;
    std::queue<std::string>  workQueue;
    std::map<std::string, WorkerAPageData> pageResults;
    std::vector<std::pair<std::string, std::string>> linkGraph;
    std::queue<int>          idleWorkers;
    int                      activeWorkers = 0;
    int                      doneCount = 0;

    workQueue.push(baseUrl);

    // Dispatch one URL to workerBRank, or park/DONE it
    auto tryDispatch = [&](int bRank) -> bool {
        while (!workQueue.empty() && visitedUrls.count(workQueue.front()))
            workQueue.pop();

        if (!workQueue.empty()) {
            std::string url = workQueue.front(); workQueue.pop();
            visitedUrls.insert(url);
            MPI_Send(url.c_str(), static_cast<int>(url.size()) + 1, MPI_CHAR, bRank, 0, MPI_COMM_WORLD);
            activeWorkers++;
            return true;
        }
        if (activeWorkers > 0) {
            idleWorkers.push(bRank);
            return false;
        }
        MPI_Send("DONE", 5, MPI_CHAR, bRank, 0, MPI_COMM_WORLD);
        return false;
    };

    // Main event loop
    while (doneCount < M) {
        // Spin-probe all B children for any incoming message
        MPI_Status status;
        bool found = false;
        while (!found) {
            for (int b = firstB; b < firstB + M; b++) {
                int flag = 0;
                MPI_Iprobe(b, 0, MPI_COMM_WORLD, &flag, &status);
                if (flag) { found = true; break; }
            }
        }

        // Confirmed message from status.MPI_SOURCE — do a blocking Probe to refresh count
        int src = status.MPI_SOURCE;

        MPI_Probe(src, 0, MPI_COMM_WORLD, &status);
        int msgSize = 0;
        MPI_Get_count(&status, MPI_CHAR, &msgSize);

        if (msgSize <= 0) {
            char dummy = 0;
            MPI_Recv(&dummy, 0, MPI_CHAR, src, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            continue;
        }

        std::vector<char> buf(static_cast<size_t>(msgSize) + 1, '\0');
        MPI_Recv(buf.data(), msgSize, MPI_CHAR, src, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        buf[msgSize] = '\0';

        size_t actualLen = (msgSize > 0 && buf[msgSize - 1] == '\0') 
                ? static_cast<size_t>(msgSize) - 1 
                : static_cast<size_t>(msgSize);
        std::string msg(buf.data(), actualLen);

        if (msg == "READY") {
            bool dispatched = tryDispatch(src);
            if (!dispatched && activeWorkers == 0)
                doneCount++;
        } else {
            activeWorkers--;

            WorkerAPageData result = parseWorkerBResult(msg);

            if (!result.url.empty()) {
                std::string url = result.url;                        // save before move
                pageResults[url] = std::move(result);
                for (const auto& link : pageResults[url].foundLinks) {
                    linkGraph.push_back({url, link});
                    if (!visitedUrls.count(link))
                        workQueue.push(link);
                }
            }
            // Dispatch to any parked idle workers now that queue may have grown
            while (!idleWorkers.empty()) {
                int idleB = idleWorkers.front(); idleWorkers.pop();
                bool dispatched = tryDispatch(idleB);
                if (!dispatched && activeWorkers == 0)
                    doneCount++;
            }
        }
    }

    std::string finalResults = serializeResults(baseUrl, pageResults, linkGraph);
    MPI_Send(finalResults.c_str(), static_cast<int>(finalResults.size()) + 1, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
}