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

// Outer loop: wait for WORK/SHUTDOWN from master on tag 1, then run one crawl job.
void runWorkerB(int rank, int N, int M) {
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
        runWorkerB_job(rank, N, M);
    }
}

void runWorkerB_job(int rank, int N, int M) {
    int workerAId = calculateParentWorkerA(rank, N, M);

    while (true) {
        // Tell Worker A it can work
        MPI_Send("READY", 6, MPI_CHAR, workerAId, 0, MPI_COMM_WORLD);
        MPI_Status status;
        MPI_Probe(workerAId, 0, MPI_COMM_WORLD, &status);

        int msgSize = 0;
        MPI_Get_count(&status, MPI_CHAR, &msgSize);

        std::vector<char> urlBuf(msgSize + 1, '\0'); 
        MPI_Recv(urlBuf.data(), msgSize, MPI_CHAR, workerAId, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        std::string url(urlBuf.data(), static_cast<size_t>(msgSize) - 1);

        if (url == "DONE") break;

        // Download and parse the page
        std::string html = utils::downloadHTML(url);

        int imgCount  = countTag(html, "<img ");
        int formCount = countTag(html, "<form ");
        int linkCount = countTag(html, "<a ");   
        std::vector<std::string> links    = extractLinks(html, url);  
        std::vector<std::string> headings = extractHeadings(html);

        // Serialize result
        std::string result;
        result += "URL:" + url + "\n";
        result += "IMAGES:" + std::to_string(imgCount)   + "\n";
        result += "LINKS:"  + std::to_string(linkCount)  + "\n";
        result += "FORMS:"  + std::to_string(formCount)  + "\n";

        for (const auto& link : links)
            result += "LINK:" + link + "\n";

        for (const auto& heading : headings)
            result += "HEADING:" + heading + "\n";

        int sendLen = static_cast<int>(result.size()) + 1;
        MPI_Send(result.c_str(), sendLen, MPI_CHAR, workerAId, 0, MPI_COMM_WORLD);
    }
}