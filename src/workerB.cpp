#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <mpi.h>

#include "parser.h"
#include "workerB.h"
#include "utils.h"

static int calculateParentWorkerA(int myRank, int N, int M) {
    int workerBIndex = myRank - (N + 1);
    int parentWorkerA = 1 + (workerBIndex / M);
    return parentWorkerA;
}

void runWorkerB(int rank, int N, int M) {
    int workerAId = calculateParentWorkerA(rank, N, M);

    while (true) {
        // Tell Worker A we are ready for work
        MPI_Send("READY", 6, MPI_CHAR, workerAId, 0, MPI_COMM_WORLD);

        // -------------------------------------------------------------------
        // Receive URL (or DONE) from Worker A.
        // FIX: use Probe+Get_count so the buffer is exactly the right size,
        //      then explicitly null-terminate before constructing std::string.
        //      The old code used a fixed 1024-byte stack buffer with no null
        //      termination guarantee → stack corruption → segfault.
        // -------------------------------------------------------------------
        MPI_Status status;
        MPI_Probe(workerAId, 0, MPI_COMM_WORLD, &status);

        int msgSize = 0;
        MPI_Get_count(&status, MPI_CHAR, &msgSize);

        if (msgSize <= 0) continue;  // should never happen, but be safe

        std::vector<char> urlBuf(msgSize + 1, '\0');   // +1 for guaranteed '\0'
        MPI_Recv(urlBuf.data(), msgSize, MPI_CHAR, workerAId, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        std::string url(urlBuf.data(), static_cast<size_t>(msgSize) - 1);

        if (url == "DONE") break;

        // Download and parse the page
        std::string html = utils::downloadHTML(url);

        int imgCount  = countTag(html, "<img ");
        int formCount = countTag(html, "<form ");
        int linkCount = countTag(html, "<a ");   // total <a> tags, regardless of domain
        std::vector<std::string> links    = extractLinks(html, url);  // domain-filtered, for graph
        std::vector<std::string> headings = extractHeadings(html);

        // Serialize result
        std::string result;
        result += "URL:"    + url + "\n";
        result += "IMAGES:" + std::to_string(imgCount)   + "\n";
        result += "LINKS:"  + std::to_string(linkCount)  + "\n";  // all links, not just same-domain
        result += "FORMS:"  + std::to_string(formCount)  + "\n";

        for (const auto& link : links)
            result += "LINK:" + link + "\n";

        for (const auto& heading : headings)
            result += "HEADING:" + heading + "\n";

        // -------------------------------------------------------------------
        // FIX: cast size to int explicitly; result.size()+1 is safe here
        //      because result is never large enough to overflow int in practice.
        // -------------------------------------------------------------------
        int sendLen = static_cast<int>(result.size()) + 1;
        MPI_Send(result.c_str(), sendLen, MPI_CHAR, workerAId, 0, MPI_COMM_WORLD);
    }
}