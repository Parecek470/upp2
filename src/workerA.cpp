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

// ---------------------------------------------------------------------------
// Local types
// ---------------------------------------------------------------------------
struct PageData {
    std::string url;
    int imageCount = 0, linkCount = 0, formCount = 0;
    std::vector<std::string> headings;
    std::vector<std::string> foundLinks;
};

// ---------------------------------------------------------------------------
// Parse the result message that Worker B sends back
// ---------------------------------------------------------------------------
static PageData parseWorkerBResult(const std::string& msg) {
    PageData data;
    std::istringstream stream(msg);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.rfind("URL:", 0) == 0)
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

// ---------------------------------------------------------------------------
// Serialize all results into a single string for sending back to Master
// ---------------------------------------------------------------------------
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
        for (const auto& heading : data.headings)
            result += "HEADING|" + heading + "\n";
        result += "END_PAGE\n";
    }
    result += "END_PAGES\n";

    result += "BEGIN_GRAPH\n";
    for (const auto& [source, target] : linkGraph)
        result += "EDGE|" + source + "|" + target + "\n";
    result += "END_GRAPH\n";

    return result;
}

// ---------------------------------------------------------------------------
// Safely receive a variable-length MPI_CHAR message from a known source.
// Uses MPI_Probe (blocking) so we never allocate before we know the real size.
// ---------------------------------------------------------------------------
static std::string recvString(int source, int tag) {
    MPI_Status status;
    MPI_Probe(source, tag, MPI_COMM_WORLD, &status);

    int msgSize = 0;
    MPI_Get_count(&status, MPI_CHAR, &msgSize);

    // Guard against MPI_UNDEFINED or zero-length messages
    if (msgSize <= 0) return "";

    std::vector<char> buf(msgSize);
    MPI_Recv(buf.data(), msgSize, MPI_CHAR, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // Null-terminate just in case, then build string
    buf.back() = '\0';
    return std::string(buf.data());
}

// ---------------------------------------------------------------------------
// Main Worker A logic
// ---------------------------------------------------------------------------
void runWorkerA(int rank, int N, int M) {
    // My Worker B children occupy contiguous ranks:
    //   Worker A rank r (1-based): firstB = N+1 + (r-1)*M  ..  firstB+M-1
    const int firstB = N + 1 + (rank - 1) * M;

    // -----------------------------------------------------------------------
    // Step 1: receive the initial URL assigned by Master (blocking on tag 0)
    // -----------------------------------------------------------------------
    std::string baseUrl = recvString(0, 0);
    if (baseUrl.empty()) {
        // Nothing to do — send empty result back
        std::string empty = serializeResults("", {}, {});
        MPI_Send(empty.c_str(), (int)(empty.size() + 1), MPI_CHAR, 0, 0, MPI_COMM_WORLD);
        return;
    }

    // -----------------------------------------------------------------------
    // Crawling data structures
    // -----------------------------------------------------------------------
    std::set<std::string>   visitedUrls;
    std::queue<std::string> workQueue;
    std::map<std::string, PageData> pageResults;
    std::vector<std::pair<std::string, std::string>> linkGraph;

    workQueue.push(baseUrl);

    // Track which B workers are currently idle (sent READY, waiting for a URL)
    // and which are busy (sent a URL, waiting for result).
    std::queue<int> idleWorkers;   // ranks of Worker B that sent READY
    int             activeWorkers = 0; // number of B workers currently processing

    // -----------------------------------------------------------------------
    // Helper: dispatch one URL to an idle Worker B, or send DONE if finished.
    // Returns true if a URL was dispatched, false if DONE was sent.
    // -----------------------------------------------------------------------
    auto tryDispatch = [&](int workerBRank) -> bool {
        // Drain already-visited URLs from the front of the queue
        while (!workQueue.empty() && visitedUrls.count(workQueue.front())) {
            workQueue.pop();
        }

        if (!workQueue.empty()) {
            std::string nextUrl = workQueue.front();
            workQueue.pop();
            visitedUrls.insert(nextUrl);

            MPI_Send(nextUrl.c_str(), (int)(nextUrl.size() + 1), MPI_CHAR,
                     workerBRank, 0, MPI_COMM_WORLD);
            activeWorkers++;
            return true;
        }

        // Queue is empty right now — if other workers are busy, they may add
        // new URLs when they return.  Park this worker in the idle queue.
        if (activeWorkers > 0) {
            idleWorkers.push(workerBRank);
            return false;
        }

        // Truly nothing left: queue empty AND no active workers → send DONE
        MPI_Send("DONE", 5, MPI_CHAR, workerBRank, 0, MPI_COMM_WORLD);
        return false;
    };

    // -----------------------------------------------------------------------
    // Step 2: main event loop
    //
    // We only exit when:
    //   - The work queue is empty
    //   - No Worker B is actively processing
    //   - All idle Workers have been sent DONE
    // -----------------------------------------------------------------------
    int doneCount = 0;  // how many Worker B have been told DONE

    while (doneCount < M) {
        // Poll all our Worker B children for an incoming message (READY or result)
        MPI_Status status;
        bool found = false;

        while (!found) {
            for (int b = firstB; b < firstB + M; b++) {
                int flag = 0;
                MPI_Iprobe(b, 0, MPI_COMM_WORLD, &flag, &status);
                if (flag) { found = true; break; }
            }
        }

        // We know the source and exact count from the successful Iprobe.
        // Use a blocking Probe on that specific source to get a reliable status,
        // then Recv — this avoids any Iprobe/Recv race.
        MPI_Probe(status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);

        int msgSize = 0;
        MPI_Get_count(&status, MPI_CHAR, &msgSize);
        int workerBRank = status.MPI_SOURCE;

        if (msgSize <= 0) continue;   // defensive: skip empty/undefined

        std::vector<char> buf(msgSize);
        MPI_Recv(buf.data(), msgSize, MPI_CHAR, workerBRank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        buf.back() = '\0';
        std::string msg(buf.data());

        if (msg == "READY") {
            // Worker B is idle and wants work
            bool dispatched = tryDispatch(workerBRank);
            if (!dispatched && activeWorkers == 0) {
                // tryDispatch sent DONE directly
                doneCount++;
            }
            // else: parked in idleWorkers, will be dispatched when results arrive
        } else {
            // Worker B sent back a crawl result
            activeWorkers--;

            PageData result = parseWorkerBResult(msg);
            pageResults[result.url] = result;

            for (const auto& link : result.foundLinks) {
                linkGraph.push_back({result.url, link});
                if (!visitedUrls.count(link))
                    workQueue.push(link);
            }

            // Now that we may have new URLs, dispatch to any parked idle workers
            while (!idleWorkers.empty()) {
                int idleB = idleWorkers.front();
                idleWorkers.pop();

                bool dispatched = tryDispatch(idleB);
                if (!dispatched && activeWorkers == 0) {
                    // Queue still empty and nobody else is working → DONE was sent
                    doneCount++;
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Step 3: send aggregated results back to Master (tag 0)
    // -----------------------------------------------------------------------
    std::string finalResults = serializeResults(baseUrl, pageResults, linkGraph);
    MPI_Send(finalResults.c_str(), (int)(finalResults.size() + 1), MPI_CHAR,
             0, 0, MPI_COMM_WORLD);
}