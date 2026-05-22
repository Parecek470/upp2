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

// DEBUG macro — prints to stderr with rank prefix, flushes immediately
#define DBG(rank, msg) \
    do { std::cerr << "[WorkerA rank=" << (rank) << "] " << msg << std::endl; } while(0)

struct PageData {
    std::string url;
    int imageCount = 0, linkCount = 0, formCount = 0;
    std::vector<std::string> headings;
    std::vector<std::string> foundLinks;
};

static PageData parseWorkerBResult(const std::string& msg) {
    PageData data{};
    std::cerr << "[parse] fresh PageData: &data=" << (void*)&data 
          << " &foundLinks=" << (void*)&data.foundLinks
          << " foundLinks.size()=" << data.foundLinks.size() << std::endl;
std::istringstream stream(msg);
std::cerr << "[parse] after istringstream constructed: foundLinks.size()=" << data.foundLinks.size() << std::endl;
    std::string line;

    std::cerr << "[parse] msg bytes: ";
    for (size_t i = 0; i < std::min(msg.size(), size_t(100)); i++)
        std::cerr << std::hex << (int)(unsigned char)msg[i] << " ";
    std::cerr << std::dec << std::endl;

    while (std::getline(stream, line)) {
        // Strip \r if present (HTTP line endings)
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty()) continue;

        if (line.rfind("URL:", 0) == 0) {
            data.url = line.substr(4);
            std::cerr << "[parse] URL='" << data.url << "'" << std::endl;
        } else if (line.rfind("IMAGES:", 0) == 0) {
            try { data.imageCount = std::stoi(line.substr(7)); } catch (...) {}
        } else if (line.rfind("LINKS:", 0) == 0) {
            std::string val = line.substr(6);
            std::cerr << "[parse] LINKS val='" << val << "' len=" << val.size()
                    << " &data.foundLinks=" << (void*)&data.foundLinks
                    << " foundLinks.size()=" << data.foundLinks.size() << std::endl;
            try { data.linkCount = std::stoi(val); } catch (...) { std::cerr << "[parse] LINKS stoi threw" << std::endl; }
            std::cerr << "[parse] after LINKS parse, foundLinks.size()=" << data.foundLinks.size() << std::endl;
        } else if (line.rfind("FORMS:", 0) == 0) {
            try { data.formCount = std::stoi(line.substr(6)); } catch (...) {}
        } else if (line.rfind("LINK:", 0) == 0) {
            std::string val = line.substr(5);
            std::cerr << "[parse] LINK val='" << val << "' len=" << val.size()
                      << " foundLinks.size()=" << data.foundLinks.size() << std::endl;
            data.foundLinks.push_back(val);
            std::cerr << "[parse] push_back OK, foundLinks.size()=" << data.foundLinks.size() << std::endl;
        } else if (line.rfind("HEADING:", 0) == 0) {
            std::string val = line.substr(8);
            std::cerr << "[parse] HEADING val='" << val << "'" << std::endl;
            data.headings.push_back(val);
        }
    }
    std::cerr << "[parse] done: url='" << data.url << "' links=" << data.foundLinks.size() << std::endl;
    return data;
}

static std::string serializeResults(
    const std::string& baseUrl,
    const std::map<std::string, PageData>& pageResults,
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
    DBG(myRank, "safeRecv: probing src=" << source << " tag=" << tag);
    MPI_Status st;
    MPI_Probe(source, tag, MPI_COMM_WORLD, &st);

    int n = 0;
    MPI_Get_count(&st, MPI_CHAR, &n);
    DBG(myRank, "safeRecv: MPI_Get_count=" << n << " from src=" << source);

    if (n <= 0) {
        DBG(myRank, "safeRecv: WARNING n<=0, returning empty");
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

    DBG(myRank, "safeRecv: got msg of len=" << s.size() << " content='" << s.substr(0, 40) << (s.size()>40?"...":"") << "'");
    return s;
}

void runWorkerA(int rank, int N, int M) {
    const int firstB = N + 1 + (rank - 1) * M;
    DBG(rank, "started. firstB=" << firstB << " M=" << M);

    // Step 1: receive initial URL from Master
    DBG(rank, "waiting for URL from master (rank 0)");
    std::string baseUrl = safeRecv(0, 0, rank);
    DBG(rank, "received baseUrl='" << baseUrl << "'");

    if (baseUrl.empty()) {
        DBG(rank, "empty baseUrl, sending empty result to master");
        std::string empty = serializeResults("", {}, {});
        MPI_Send(empty.c_str(), static_cast<int>(empty.size()) + 1, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
        return;
    }

    std::set<std::string>    visitedUrls;
    std::queue<std::string>  workQueue;
    std::map<std::string, PageData> pageResults;
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
            DBG(rank, "dispatching '" << url << "' to B rank=" << bRank);
            MPI_Send(url.c_str(), static_cast<int>(url.size()) + 1, MPI_CHAR, bRank, 0, MPI_COMM_WORLD);
            activeWorkers++;
            return true;
        }
        if (activeWorkers > 0) {
            DBG(rank, "queue empty but " << activeWorkers << " active, parking B rank=" << bRank);
            idleWorkers.push(bRank);
            return false;
        }
        DBG(rank, "all done, sending DONE to B rank=" << bRank);
        MPI_Send("DONE", 5, MPI_CHAR, bRank, 0, MPI_COMM_WORLD);
        return false;
    };

    // Main event loop
    while (doneCount < M) {
        DBG(rank, "loop: doneCount=" << doneCount << "/" << M
            << " activeWorkers=" << activeWorkers
            << " queueSize=" << workQueue.size()
            << " idleWorkers=" << idleWorkers.size());

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
        DBG(rank, "incoming message from B rank=" << src);

        MPI_Probe(src, 0, MPI_COMM_WORLD, &status);
        int msgSize = 0;
        MPI_Get_count(&status, MPI_CHAR, &msgSize);
        DBG(rank, "msgSize=" << msgSize << " from B rank=" << src);

        if (msgSize <= 0) {
            DBG(rank, "WARNING: msgSize<=0, consuming and skipping");
            char dummy = 0;
            MPI_Recv(&dummy, 0, MPI_CHAR, src, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            continue;
        }

        // Safe recv with exact allocation
        std::vector<char> buf(static_cast<size_t>(msgSize) + 1, '\0');
        MPI_Recv(buf.data(), msgSize, MPI_CHAR, src, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        buf[msgSize] = '\0';

        size_t actualLen = (msgSize > 0 && buf[msgSize - 1] == '\0') 
                ? static_cast<size_t>(msgSize) - 1 
                : static_cast<size_t>(msgSize);
        std::string msg(buf.data(), actualLen);
        // No need for the while-strip loop anymore — actualLen already excludes the null
        DBG(rank, "msg from B rank=" << src << ": '" << msg.substr(0, 60) << (msg.size()>60?"...":"") << "'");

        if (msg == "READY") {
            DBG(rank, "B rank=" << src << " is READY");
            bool dispatched = tryDispatch(src);
            if (!dispatched && activeWorkers == 0)
                doneCount++;
        } else {
            DBG(rank, "B rank=" << src << " sent result, parsing...");
            activeWorkers--;
            std::cerr << "[workerA] before parse: msg.size()=" << msg.size()
          << " msg.capacity()=" << msg.capacity()
          << " msg.data()=" << (void*)msg.data() << std::endl;

            PageData result = parseWorkerBResult(msg);
            DBG(rank, "parsed result for url='" << result.url << "' links=" << result.foundLinks.size());

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

    DBG(rank, "all B workers done, serializing and sending to master");
    std::string finalResults = serializeResults(baseUrl, pageResults, linkGraph);
    DBG(rank, "final result size=" << finalResults.size() << " bytes");
    MPI_Send(finalResults.c_str(), static_cast<int>(finalResults.size()) + 1, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
    DBG(rank, "done, exiting runWorkerA");
}