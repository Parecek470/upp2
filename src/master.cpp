#include <iostream>
#include <string>
#include <set>
#include <map>
#include <vector>
#include <mpi.h>


void writeMapFile(const std::string& filepath,
                  const std::map<std::string, PageData>& pageResults,
                  const std::vector<std::pair<std::string, std::string>>& linkGraph) {
    std::ofstream file(filepath);
    
    // Section 1: List all unique page URLs (nodes)
    for (const auto& [url, data] : pageResults) {
        file << "\"" << url << "\"\n";
    }
    
    // Section 2: List all edges
    for (const auto& [source, target] : linkGraph) {
        file << "\"" << source << "\" \"" << target << "\"\n";
    }
    
    file.close();
}

void writeContentFile(const std::string& filepath,
                      const std::map<std::string, PageData>& pageResults) {
    std::ofstream file(filepath);
    
    for (const auto& [url, data] : pageResults) {
        file << "\"" << url << "\"\n";
        file << "IMAGES " << data.imageCount << "\n";
        file << "LINKS " << data.linkCount << "\n";
        file << "FORMS " << data.formCount << "\n";
        
        for (const auto& heading : data.headings) {
            file << heading << "\n";  // Already has dashes
        }
        file << "\n";  // Blank line between pages
    }
    
    file.close();
}


void runMaster(int N, int M) {
    // Start HTTP server (already in skeleton)
    
    // When form submitted with URLs:
    std::vector<std::string> urls = parseFormInput();
    
    // Distribute URLs to Worker A nodes
    for (int i = 0; i < urls.size(); i++) {
        int workerARank = 1 + (i % N);  // Round-robin distribution
        MPI_Send(urls[i].c_str(), ..., workerARank, ...);
    }
    
    // Receive results from each Worker A
    for (int i = 0; i < urls.size(); i++) {
        char resultBuffer[LARGE_SIZE];
        MPI_Recv(resultBuffer, ..., MPI_ANY_SOURCE, ...);
        
        // Parse results
        // Create timestamped directory
        // Write map.txt, content.txt, log.txt
    }
    
    // Send response to user
}