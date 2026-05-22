/**
 * Kostra druhe semestralni prace z predmetu KIV/UPP
 * Soubory a hlavicku upravujte dle sveho uvazeni a nutnosti
 */
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <set>
#include <mpi.h>

#include "utils.h"
#include "server.h"
#include "master.h"
#include "workerA.h"
#include "workerB.h"

int g_N = 0;
int g_M = 0;

void process(const std::vector<std::string>& URLs, std::string& vystup) {
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if(rank != 0) {
        return;
    }

    std::string startTime = getCurrentTimestamp();
    std::ostringstream outputHtml;
    outputHtml << "<h2>Crawling Results</h2>";

    // --- Wake up all workers for this request (tag 1 = control channel) ---
    for (int w = 1; w < 1 + g_N + g_N * g_M; w++)
        MPI_Send("WORK", 5, MPI_CHAR, w, 1, MPI_COMM_WORLD);

    // Distribute URLs to Worker A nodes (round-robin)
    for (size_t i = 0; i < URLs.size(); i++) {
        int workerARank = 1 + (i % g_N);
        std::string url = URLs[i];
        while (!url.empty() && url.back() == '\r')
            url.pop_back();
        if (url.empty()) continue;
        MPI_Send(url.c_str(), (int)(url.size() + 1), MPI_CHAR, workerARank, 0, MPI_COMM_WORLD);

        outputHtml << "<p>Assigned URL: <strong>" << url << "</strong> to Worker A #" << workerARank << "</p>";
    }
    
    std::set<int> assignedWorkers;
    for (size_t i = 0; i < URLs.size(); i++) {
        assignedWorkers.insert(1 + (i % g_N));
    }

    for (size_t i = 0; i < assignedWorkers.size(); i++) {
        MPI_Status status;
        MPI_Probe(MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status); 

        int msgSize = 0;
        MPI_Get_count(&status, MPI_CHAR, &msgSize);
        if (msgSize <= 0) { i--; continue; }  

        std::vector<char> resultBuffer(msgSize + 1, '\0');
        MPI_Recv(resultBuffer.data(), msgSize, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        std::string resultMsg(resultBuffer.data());

        // Parse results
        CrawlResults results = parseMasterResult(resultMsg);

        // Create timestamped directory
        createDirectory("results");  
        std::string dirName = "results/" + makeDirectoryName(results.baseUrl);
        createDirectory(dirName);

        // Write output files
        std::string endTime = getCurrentTimestamp();
        writeMapFile(dirName + "/map.txt", results);
        writeContentFile(dirName + "/content.txt", results);
        writeLogFile(dirName + "/log.txt", startTime, endTime, "OK");

        outputHtml << "<p>&#10003; Completed crawling: <strong>" << results.baseUrl << "</strong></p>";
        outputHtml << "<p>Results saved to: <code>" << dirName << "</code></p>";
        outputHtml << "<ul>";
        outputHtml << "<li>Pages found: " << results.pages.size() << "</li>";
        outputHtml << "<li>Links found: " << results.edges.size() << "</li>";
        outputHtml << "</ul>";
    }

    outputHtml << "<p><strong>All crawling tasks completed!</strong></p>";
    vystup = outputHtml.str();
}

void parseArgs(int argc, char** argv, int& N, int& M) {
    N = -1;
    M = -1;
    
    for (int i = 1; i < argc - 1; i++) {
        if (std::string(argv[i]) == "-n") {
            N = std::stoi(argv[i + 1]);
        } else if (std::string(argv[i]) == "-m") {
            M = std::stoi(argv[i + 1]);
        }
    }
    
    if (N == -1 || M == -1) {
        std::cerr << "Usage: ./upp2 -n <N> -m <M>\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
}



int main(int argc, char** argv) {
	// initialize MPI
	MPI_Init(&argc, &argv);
	
	int rank, size;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	
	parseArgs(argc, argv, g_N, g_M);

	int expectedSize = 1 + g_N + g_N * g_M;
    if (size != expectedSize) {
        if (rank == 0) {
            std::cerr << "Error: Expected " << expectedSize << " processes, but got " << size << "\n";
            std::cerr << "Run with: mpirun -np " << expectedSize << " ./upp2 -n " << g_N << " -m " << g_M << "\n";
        }
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

	if(rank == 0) {
		// inicializace serveru
		CServer svr;
		if (!svr.Init("./data", "0.0.0.0", 8001)) {
			std::cerr << "Nelze inicializovat server!" << std::endl;
			MPI_Abort(MPI_COMM_WORLD, 1);
		}

		// registrace callbacku pro zpracovani odeslanych URL
		svr.RegisterFormCallback(process);

		svr.Run();

		// shutdown all workers on server stop (tag 1 = control channel)
		for (int w = 1; w < 1 + g_N + g_N * g_M; w++)
			MPI_Send("SHUTDOWN", 9, MPI_CHAR, w, 1, MPI_COMM_WORLD);

	} else if (rank >= 1 && rank <= g_N) {
		runWorkerA(rank, g_N, g_M);
	} else {
		runWorkerB(rank, g_N, g_M);
	}

	MPI_Finalize();
	return 0;
}