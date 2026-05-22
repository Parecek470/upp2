#include <iostream>
#include <string>
#include <set>
#include <queue>
#include <map>
#include <vector>
#include <mpi.h>

void workerA(int rank, int N, int M) {
    // Calculate my Worker B ranks: which M workers belong to me?
    int myWorkerBStart = N + 1 + (rank - 1) * M;
    int myWorkerBEnd = myWorkerBStart + M;
    
    // Data structures
    std::set<std::string> visitedUrls;
    std::queue<std::string> workQueue;
    std::map<std::string, PageData> pageResults;
    std::vector<std::pair<std::string, std::string>> linkGraph;
    
    // 1. Receive initial URLs from Master
    char urlsBuffer[4096];
    MPI_Recv(urlsBuffer, 4096, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    // Parse and add to workQueue
    
    int activeWorkers = 0;
    
    // 2. Main loop
    while (!workQueue.empty() || activeWorkers > 0) {
        MPI_Status status;
        char buffer[4096];
        MPI_Recv(buffer, 4096, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
        int workerBRank = status.MPI_SOURCE;
        
        std::string msg(buffer);
        
        if (msg == "READY") {
            // Worker B is requesting work
            if (!workQueue.empty()) {
                std::string nextUrl = workQueue.front();
                workQueue.pop();
                
                // Skip if already visited
                if (visitedUrls.count(nextUrl)) continue;
                visitedUrls.insert(nextUrl);
                
                MPI_Send(nextUrl.c_str(), nextUrl.size() + 1, MPI_CHAR, workerBRank, 0, MPI_COMM_WORLD);
                activeWorkers++;
            } else {
                // No work left
                std::string done = "DONE";
                MPI_Send(done.c_str(), done.size() + 1, MPI_CHAR, workerBRank, 0, MPI_COMM_WORLD);
            }
        } else {
            // Worker B sent results
            activeWorkers--;
            // Parse results, extract links, add to workQueue
            // Build graph edges
        }
    }
    
    // 3. Send aggregated results back to Master
}