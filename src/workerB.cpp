#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <mpi.h>

#include "parser.h"
#include "workerB.h"
#include "utils.h"

int calculateParentWorkerA(int myRank, int N, int M) {
    int workerBIndex = myRank - (N + 1);  
    int parentWorkerA = 1 + (workerBIndex / M); 
    return parentWorkerA;
}

void runWorkerB(int rank, int N, int M) {
    // Calculate which Worker A is my parent
    int workerAId = calculateParentWorkerA(rank, N, M);
    
    while (true) {
        // Request work from Worker A
        MPI_Send("READY", 6, MPI_CHAR, workerAId, 0, MPI_COMM_WORLD);
        
        // Receive URL from Worker A
        char urlBuffer[1024];
        MPI_Status status;
        MPI_Recv(urlBuffer, 1024, MPI_CHAR, workerAId, 0, MPI_COMM_WORLD, &status);
        
        std::string url(urlBuffer);
        if (url == "DONE") break; // Signal to stop
        
        // Download HTML
        std::string html = utils::downloadHTML(url);
        
        // Parse and count
        int imgCount = countTag(html, "<img ");
        int formCount = countTag(html, "<form ");
        std::vector<std::string> links = extractLinks(html, url);
        std::vector<std::string> headings = extractHeadings(html);
        
        // Serialize to string
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
        
        // Send back to Worker A
        MPI_Send(result.c_str(), result.size() + 1, MPI_CHAR, workerAId, 0, MPI_COMM_WORLD);
    }
}