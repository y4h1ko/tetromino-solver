#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <ctime>
#include <climits>
#include <cstdlib>
#include <iomanip>
#include <omp.h>
#include <mpi.h>

static const int CELL_UNCOVERED = INT_MAX;
static const int WORK_HEADER_TAG = 1;
static const int WORK_BOARD_TAG = 2;
static const int RESULT_HEADER_TAG = 3;
static const int RESULT_CALLS_TAG = 4;
static const int RESULT_BOARD_TAG = 5;
static const int STOP_TAG = 6;

//orientacia tvaru
struct Shape { int dr[4]; int dc[4]; };

//stav dosky a pocet T/Z
struct Config { std::vector<int> board; int countT; int countZ; int actualCost;};

//vysledok jedneho prehladavaneho podstromu
struct Result {
    int bestCost;
    std::vector<int> bestState;
    int bestT;
    int bestZ;
    long long recursionCalls;
    int usedThreads;
    bool foundSolution;
};

//oreintacia tvaru Z a T
const std::vector<Shape> shapesZ = {  {{0,0,1,1}, {0,1,1,2}},  {{0,1,1,2}, {1,0,1,0}},  {{0,0,1,1}, {1,2,0,1}},  {{0,1,1,2}, {0,0,1,1}}  };
const std::vector<Shape> shapesT = {  {{0,0,0,1}, {0,1,2,1}},  {{0,1,1,2}, {1,0,1,1}},  {{0,1,1,1}, {1,0,1,2}},  {{0,1,1,2}, {0,0,1,0}}  };

//cita subor a ulozi data do rows, cols a weights
void loadFile(const std::string& filename, int& rows, int& cols, std::vector<int>& weights) {
    std::ifstream file(filename);

    //file validacia
    if (!file.is_open()) {
        std::cout << "Nepodarilo sa otvorit subor.\n";
        exit(EXIT_FAILURE);
    }

    file >> rows >> cols;

    int total = rows * cols;
    weights.resize(total);

    for (int i = 0; i < total; i++) {
        file >> weights[i];
    }

    file.close();
}

//validacia schopnosti umiestnit dany tvary v kazdej orientacii na dosku
//spočita index v doske a ak sa dá položit, vrati true, inak false
bool canPlace(const std::vector<int>& board, const int rows, const int cols, const int r, const int c, const Shape& sh) {
    for (int k = 0; k < 4; k++) {
        int rr = r + sh.dr[k];
        int cc = c + sh.dc[k];

        if (rr < 0 || rr >= rows || cc < 0 || cc >= cols) return false;

        int cellIdx = rr * cols + cc;
        if (board[cellIdx] != 0) return false;
    }
    return true;
}

//zápis tvaru/nepokrytia na dosku.
void place(std::vector<int>& board, int rows, int cols, int r, int c, const Shape& sh, int mark) {
    for (int k = 0; k < 4; k++) {
        int rr = r + sh.dr[k];
        int cc = c + sh.dc[k];
        int cellIdx = rr * cols + cc;
        board[cellIdx] = mark;
    }
}

//najde prve nerozhodnuty index na doske a vrati ho
int findFirstUndecided(const std::vector<int>& board) {
    for (int i = 0; i < (int)board.size(); i++)
        if (board[i] == 0) return i;
    return -1;
}

//vytvori zoznam pociatocnych konfiguracii pomocou BFS do zadanej hlbky
//vytvori objekt dosky, kde updatuje kolko Z/T je pouzitych a aka je aktualna cena
//vrati frontier konfiguracii po prejdeni bfsDepth urovni
std::vector<Config> initializeConfigs(const int rows, const int cols, const std::vector<Shape>& shapesZ, const std::vector<Shape>& shapesT, 
                                const std::vector<int>& weights, const int bfsDepth) {
    
    int N = rows * cols;                    //vypocet poctu poli na 1D doske
    std::vector<Config> frontier;

    //pociatocna konfiguracia
    Config root;
    root.board.assign(N, 0);
    root.countT = 0;
    root.countZ = 0;
    root.actualCost = 0;
    frontier.push_back(root);

    for (int level = 0; level < bfsDepth; level++) {
        std::vector<Config> nextFrontier;

        for (const Config& cfg : frontier) {
            int cellIndex = findFirstUndecided(cfg.board);

            //ak uz nie je co rozhodovat,chod dalej
            if (cellIndex == -1) {
                nextFrontier.push_back(cfg);
                continue;
            }

            int r = cellIndex / cols;
            int c = cellIndex % cols;

            //vkladanie Z
            for (int i = 0; i < (int)shapesZ.size(); i++) {
                for (int k = 0; k < 4; k++) {
                    int ar = r - shapesZ[i].dr[k];
                    int ac = c - shapesZ[i].dc[k];

                    if (canPlace(cfg.board, rows, cols, ar, ac, shapesZ[i])) {
                        Config child;
                        child = cfg;

                        int idZ = child.countZ + 1;
                        place(child.board, rows, cols, ar, ac, shapesZ[i], -idZ);
                        child.countZ++;

                        nextFrontier.push_back(child);
                    }
                }
            }

           //vkladanie T
            for (int i = 0; i < (int)shapesT.size(); i++) {
                for (int k = 0; k < 4; k++) {
                    int ar = r - shapesT[i].dr[k];
                    int ac = c - shapesT[i].dc[k];

                    if (canPlace(cfg.board, rows, cols, ar, ac, shapesT[i])) {
                        Config child;
                        child = cfg;

                        int idT = child.countT + 1;
                        place(child.board, rows, cols, ar, ac, shapesT[i], +idT);
                        child.countT++;

                        nextFrontier.push_back(child);
                    }
                }
            }

            //necha pole nepokryte
            {
                Config child;
                child = cfg;

                child.board[cellIndex] = CELL_UNCOVERED;
                child.actualCost += weights[cellIndex];

                nextFrontier.push_back(child);
            }
        }

        frontier.swap(nextFrontier);

        if (frontier.empty()) {
            break;
        }
    }

    return frontier;
}

//vypocet dolnej medze = nerozhodnute + neobsadene polia
//sucet poctu poli, ktore nejde obsadit ()
int actualLowerBound(const std::vector<int>& board, const std::vector<int>& weights) {
    int undecidedCount = 0;
    for (int v : board) if (v == 0) undecidedCount++;

    //ak je doska delitelna 4, vrati 0
    int minUncoveredCells = undecidedCount % 4;
    if (minUncoveredCells == 0) return 0;

    std::vector<int> undecidedWeights;
    undecidedWeights.reserve(undecidedCount);
    for (int i = 0; i < (int)board.size(); i++)
        if (board[i] == 0) undecidedWeights.push_back(weights[i]);

    //zorad prve (max) 3 nerozhodnute polia a spocitaj ich vahy
    std::nth_element(undecidedWeights.begin(), undecidedWeights.begin() + minUncoveredCells - 1, undecidedWeights.end());
    int sum = 0;
    for (int i = 0; i < minUncoveredCells; i++) sum += undecidedWeights[i];

    return sum;
}

//BB-DFS prehladavanie stavoveho priestoru pomocou rekurzie
//dostane cfg (vyuzite polia, aktualnu cenu, pocet T/Z) a updatne ju o dalsi krok (pokrytie/nerozhodnutie)
//Preruší sa ak: - aktualna cena je najnižšia teoretická hodnota;    - už je vetva neperspektívna (dolná medza >= bestCost)
//      - už nie je možné napraviť balance Z a T tak aby |Z - T| <= 1 pomocou maxPiecesLeft
//      - už sa neda polozit dalši tvar

void bbDfs(const Config& cfg, const int rows, const int cols, const std::vector<int>& weights, const std::vector<Shape>& shapesZ,
            const std::vector<Shape>& shapesT, int& bestCost, std::vector<int>& bestState, int& bestT, int& bestZ, 
            const int theoreticalMinimum, bool& stop, long long& localRecursionCalls) {


    localRecursionCalls++;
    bool localStop;
    int localBestCost;

    //nuntnosť pracovať s so stop a bestCost po jednom
    #pragma omp atomic read
    localStop = stop;
    if (localStop) return;

    #pragma omp atomic read
    localBestCost = bestCost;

    //najdi prvy nerozhodnuty index
    int cellIndex = findFirstUndecided(cfg.board);

    //kontrola, ci je doska plna
    if (cellIndex == -1) {
        if (std::abs(cfg.countT - cfg.countZ) <= 1) {

            if (cfg.actualCost < localBestCost) {
                //kritická oblasť zapisovania viaero vecí pri nájdení lepšieho riešenia
                #pragma omp critical (newBestCost)
                {
                    //znovu kontrola ci naozaj je bestCost najaktuálnejšia hodnota - deje sa velmi málo
                    //a zapísasnie noveho najlepšieho riešenia
                    if (cfg.actualCost < bestCost) {
                            bestCost = cfg.actualCost;
                            bestState = cfg.board;
                            bestT = cfg.countT;
                            bestZ = cfg.countZ;

                        //ak sme dosiahli trivialnu dolnu medzu, koncime
                        //atomicky, aby mohol len jeden povedať, že je koniec v správny moment
                        if (bestCost == theoreticalMinimum) {
                            #pragma omp atomic write
                            stop = true;
                        }   
                    }
                }
            }
        }
        return;
    }

    //odrezanie vetvy, ktoru nedokažem už nijak zlepšiť nez bestCost
    int minPossibleTotalCost = cfg.actualCost + actualLowerBound(cfg.board, weights);
    if (minPossibleTotalCost >= localBestCost) return;

    //koľko ešte dokaže položit tvarov a kontrola balancu |Z - T| <= maxPiecesLeft
    int undecidedCount = 0;
    for (int v : cfg.board) if (v == 0) undecidedCount++;
    int maxPiecesLeft = undecidedCount / 4;
    if (std::abs(cfg.countT - cfg.countZ) > maxPiecesLeft + 1) return;

    int r = cellIndex / cols;
    int c = cellIndex % cols;

    //poloz T
    for (int i = 0; i < (int)shapesT.size(); i++) {
        for (int k = 0; k < 4; k++) {
            int ar = r - shapesT[i].dr[k];
            int ac = c - shapesT[i].dc[k];

            if (canPlace(cfg.board, rows, cols, ar, ac, shapesT[i])) {
                Config child = cfg;
                int idT = child.countT + 1;
                place(child.board, rows, cols, ar, ac, shapesT[i], +idT);
                child.countT++;
                bbDfs(child, rows, cols, weights, shapesZ, shapesT, bestCost, bestState, bestT, bestZ, theoreticalMinimum, stop, localRecursionCalls);
            }
        }
    }

    //poloz Z
    for (int i = 0; i < (int)shapesZ.size(); i++) {
        for (int k = 0; k < 4; k++) {
            int ar = r - shapesZ[i].dr[k];
            int ac = c - shapesZ[i].dc[k];

            if (canPlace(cfg.board, rows, cols, ar, ac, shapesZ[i])) {
                Config child = cfg;
                int idZ = child.countZ + 1;
                place(child.board, rows, cols, ar, ac, shapesZ[i], -idZ);
                child.countZ++;
                bbDfs(child, rows, cols, weights, shapesZ, shapesT, bestCost, bestState, bestT, bestZ, theoreticalMinimum, stop, localRecursionCalls);
            }
        }
    }

    //nepoloz nič
    {
        Config child = cfg;
        child.board[cellIndex] = CELL_UNCOVERED;
        child.actualCost += weights[cellIndex];
        bbDfs(child, rows, cols, weights, shapesZ, shapesT, bestCost, bestState, bestT, bestZ, theoreticalMinimum, stop, localRecursionCalls);
    }
}

//Vypiš do logu riešenie (polička s tvarmi)
void printSolution(const std::vector<int>& board, const std::vector<int>& weights, const int rows, const int cols) {
    const int cellW = (rows * cols > 75) ? 4 : 3;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = r * cols + c;
            int v = board[idx];

            std::string cell;
            if (v == CELL_UNCOVERED) cell = std::to_string(weights[idx]);
            else if (v > 0) cell = "T" + std::to_string(v);
            else cell = "Z" + std::to_string(-v);

            std::cout << std::setw(cellW) << cell;
        }
        std::cout << "\n";
    }
}

//spracuje front konfiguracii pomocou OpenMP a vrati najlepsie riesenie z nej
Result solveQueueOpenMP(const std::vector<Config>& queue, const int rows, const int cols, const std::vector<int>& weights,
                        const std::vector<Shape>& shapesZ, const std::vector<Shape>& shapesT,
                        const int theoreticalMinimum, const int numOfThreadsInput, const int initialBestCost = INT_MAX) {

    Result result;
    result.bestCost = initialBestCost;
    result.bestT = 0;
    result.bestZ = 0;
    result.recursionCalls = 0;
    result.usedThreads = 1;
    result.foundSolution = false;

    bool stop = false;

    //paralelna oblast a zadanie počtu vlákien v týme
    #pragma omp parallel num_threads(numOfThreadsInput)
    {
        //jedno vlakno vytvori frontu pociatocnych konfiguracii
        #pragma omp master
        {
            result.usedThreads = omp_get_num_threads();
            //Prehladaj všetky moznosti z poč. konfig.
            for (int i = 0; i < (int)queue.size(); i++) {

                //soustenie  vlakien na pociatocne konfiguracie
                #pragma omp task firstprivate(i)
                {
                    long long localRecursionCalls = 0;

                    bbDfs(queue[i], rows, cols, weights, shapesZ, shapesT,
                        result.bestCost, result.bestState, result.bestT, result.bestZ, theoreticalMinimum, stop, localRecursionCalls);

                    //atomicke zapisovanie do total recursionCalls, sčítanie všetkych rekurzivných volaní
                    #pragma omp atomic
                    result.recursionCalls += localRecursionCalls;
                }
            }
            //ukončenie paralelizmu spoločne
            #pragma omp taskwait
        }
    }

    result.foundSolution = !result.bestState.empty();
    return result;
}

//rozoslanie konfigurácii ako prácu
void sendConfigBatch(const int dest, const std::vector<Config>& batch, const int currentBestCost) {
    int batchSize = (int)batch.size();
    int workInfo[2] = { batchSize, currentBestCost };
    MPI_Send(workInfo, 2, MPI_INT, dest, WORK_HEADER_TAG, MPI_COMM_WORLD);

    for (const Config& cfg : batch) {
        int header[3] = {cfg.countT, cfg.countZ, cfg.actualCost};
        MPI_Send(header, 3, MPI_INT, dest, WORK_HEADER_TAG, MPI_COMM_WORLD);
        MPI_Send((void*)cfg.board.data(), (int)cfg.board.size(), MPI_INT, dest, WORK_BOARD_TAG, MPI_COMM_WORLD);
    }
}

//prijatie viac configuracii
std::vector<Config> recvConfigBatch(const int rows, const int cols, MPI_Status& status, int& receivedBestCost) {
    int workInfo[2] = {0, INT_MAX};
    MPI_Recv(workInfo, 2, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    int batchSize = workInfo[0];
    receivedBestCost = workInfo[1];

    std::vector<Config> batch;

    if (status.MPI_TAG == STOP_TAG) {
        return batch;
    }

    batch.reserve(batchSize);

    for (int i = 0; i < batchSize; i++) {
        int header[3];
        MPI_Recv(header, 3, MPI_INT, 0, WORK_HEADER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        Config cfg;
        cfg.countT = header[0];
        cfg.countZ = header[1];
        cfg.actualCost = header[2];
        cfg.board.resize(rows * cols);

        MPI_Recv(cfg.board.data(), rows * cols, MPI_INT, 0, WORK_BOARD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        batch.push_back(std::move(cfg));
    }

    return batch;
}

//poslanie vysledku od slaves
void sendResult(const int dest, const Result& result) {
    int header[5] = {
        result.foundSolution ? 1 : 0,
        result.bestCost,
        result.bestT,
        result.bestZ,
        result.usedThreads
    };
    MPI_Send(header, 5, MPI_INT, dest, RESULT_HEADER_TAG, MPI_COMM_WORLD);
    MPI_Send((void*)&result.recursionCalls, 1, MPI_LONG_LONG, dest, RESULT_CALLS_TAG, MPI_COMM_WORLD);

    if (result.foundSolution && !result.bestState.empty()) {
        MPI_Send(result.bestState.data(), (int)result.bestState.size(), MPI_INT, dest, RESULT_BOARD_TAG, MPI_COMM_WORLD);
    }
}

//prijatie vysledku oddd slaves
Result recvResult(const int rows, const int cols, int& source) {
    MPI_Status status;
    int header[5];
    MPI_Recv(header, 5, MPI_INT, MPI_ANY_SOURCE, RESULT_HEADER_TAG, MPI_COMM_WORLD, &status);
    source = status.MPI_SOURCE;

    Result result;
    result.foundSolution = (header[0] != 0);
    result.bestCost = header[1];
    result.bestT = header[2];
    result.bestZ = header[3];
    result.usedThreads = header[4];
    MPI_Recv(&result.recursionCalls, 1, MPI_LONG_LONG, source, RESULT_CALLS_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (result.foundSolution) {
        result.bestState.resize(rows * cols);
        MPI_Recv(result.bestState.data(), rows * cols, MPI_INT, source, RESULT_BOARD_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    return result;
}

//sprava o ukonceni prace
void sendStop(const int dest) {
    int workInfo[2] = {0, INT_MAX};
    MPI_Send(workInfo, 2, MPI_INT, dest, STOP_TAG, MPI_COMM_WORLD);
}

//načíta vstup, vytvori všetky počiatočne konfiguracie a spustí riešenie
int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);

    int rank = 0;
    int numProcs = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &numProcs);

    int rows, cols;
    std::vector<int> weights;
    int theoreticalMinimum = 0; 
    int bfsDepth = 4;       //defaultna hlbka BFS
    int chunkSize = 4;      //defaultne sa posiela 4 configy
    bool inputChunk = false;

    std::string inputFile;
    int usedThreads = 1;
    bool inputThreads = false;
    bool inputLevel = false;
    int maxThreads = omp_get_max_threads();     //defaultne nastaviť použitie maxc threadov
    int numOfThreadsInput = maxThreads;

    //Vyriešenie vstupu
    //načítanie parametru s názvom súboru so zadaním
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if ((arg == "-f" || arg == "--file") && i + 1 < argc) {
            inputFile = argv[i + 1];
            i++;
        }

        //ak je zadaný počet threadov, nastaviť to
        if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
            try {
                numOfThreadsInput = std::stoi(argv[++i]);
                inputThreads = true;
            } catch (...) {
                if (rank == 0) std::cerr << "Pocet threads musi byt cele cislo.\n";
                MPI_Finalize();
                return EXIT_FAILURE;
            }
        }

        //ak je zadana hlbka BFS
        if ((arg == "-l" || arg == "--level") && i + 1 < argc) {
            try {
                bfsDepth = std::stoi(argv[++i]);
                inputLevel = true;
            } catch (...) {
                if (rank == 0) std::cerr << "Chyba: parameter pre BFS hlbku musi byt cele cislo.\n";
                MPI_Finalize();
                return EXIT_FAILURE;
            }
        }
 
        if ((arg == "-c" || arg == "--chunk") && i + 1 < argc) {
            try {
                chunkSize = std::stoi(argv[++i]);
                inputChunk = true;
            } catch (...) {
                if (rank == 0) std::cerr << "Chyba: parameter pre velkost chunku musi byt cele cislo.\n";
                MPI_Finalize();
                return EXIT_FAILURE;
            }
        }
    }

    //Parameter je nutný
    if (inputFile.empty()) {
        if (rank == 0) std::cerr << "Chyba: Musis zadat vstupny subor pomocou -f alebo --file\n";
        MPI_Finalize();
        return EXIT_FAILURE;
    }
    if (inputThreads && (numOfThreadsInput < 1 || numOfThreadsInput > maxThreads)) {
        if (rank == 0) std::cerr << "Pocet threads musi byt cele cislo v intervale [1,"<< maxThreads << "].\n";
        MPI_Finalize();
        return EXIT_FAILURE;
    }
    if (inputLevel && bfsDepth < 0) {
        if (rank == 0) std::cerr << "Chyba: BFS hlbka musi byt nezaporne cele cislo.\n";
        MPI_Finalize();
        return EXIT_FAILURE;
    }
    if (inputChunk && chunkSize < 1) {
        if (rank == 0) std::cerr << "Chyba: chunk size musi byt kladne cele cislo.\n";
        MPI_Finalize();
        return EXIT_FAILURE;
    }   
    
    //načitanie a spustenie aalgoritmu
    loadFile(inputFile, rows, cols, weights);

    MPI_Barrier(MPI_COMM_WORLD);
    double start = MPI_Wtime();

    // vypocet trivialnej dolnej medze
    std::vector<int> tmp = weights;
    std::sort(tmp.begin(), tmp.end());

    int k = (rows * cols) % 4;
    for (int i = 0; i < k; i++) { theoreticalMinimum += tmp[i]; }

    Result finalResult;
    finalResult.bestCost = INT_MAX;
    finalResult.bestT = 0;
    finalResult.bestZ = 0;
    finalResult.recursionCalls = 0;
    finalResult.usedThreads = 1;
    finalResult.foundSolution = false;

    if (numProcs == 1) {
        std::vector<Config> queue = initializeConfigs(rows, cols, shapesZ, shapesT, weights, bfsDepth);
        finalResult = solveQueueOpenMP(queue, rows, cols, weights, shapesZ, shapesT, theoreticalMinimum, numOfThreadsInput);
        usedThreads = finalResult.usedThreads;

        if (!finalResult.foundSolution) {
            std::cout << "\nNenaslo sa ziadne riesenie.\n";
            MPI_Finalize();
            return 0;
        }
    } else if (rank == 0) {
        std::vector<Config> queue = initializeConfigs(rows, cols, shapesZ, shapesT, weights, bfsDepth);
        int nextConfig = 0;
        int activeWorkers = 0;

        for (int worker = 1; worker < numProcs; worker++) {
            if (nextConfig < (int)queue.size()) {
                int endConfig = std::min(nextConfig + chunkSize, (int)queue.size());
                std::vector<Config> batch(queue.begin() + nextConfig, queue.begin() + endConfig);
                sendConfigBatch(worker, batch, finalResult.bestCost);
                nextConfig = endConfig;
                activeWorkers++;
            } else {
                sendStop(worker);
            }
        }

        while (activeWorkers > 0) {
            int source = 0;
            Result workerResult = recvResult(rows, cols, source);
            finalResult.recursionCalls += workerResult.recursionCalls;
            usedThreads = workerResult.usedThreads;

            if (workerResult.foundSolution) {
                if (!finalResult.foundSolution || workerResult.bestCost < finalResult.bestCost) {
                    finalResult.bestCost = workerResult.bestCost;
                    finalResult.bestState = workerResult.bestState;
                    finalResult.bestT = workerResult.bestT;
                    finalResult.bestZ = workerResult.bestZ;
                    finalResult.usedThreads = workerResult.usedThreads;
                    finalResult.foundSolution = true;
                }
            }

            if (nextConfig < (int)queue.size() && (!finalResult.foundSolution || finalResult.bestCost > theoreticalMinimum)) {
                int endConfig = std::min(nextConfig + chunkSize, (int)queue.size());
                std::vector<Config> batch(queue.begin() + nextConfig, queue.begin() + endConfig);
                sendConfigBatch(source, batch, finalResult.bestCost);
                nextConfig = endConfig;
            } else {
                sendStop(source);
                activeWorkers--;
            }
        }

        if (!finalResult.foundSolution) {
            std::cout << "\nNenaslo sa ziadne riesenie.\n";
            MPI_Finalize();
            return 0;
        }
    } else {
        while (true) {
            MPI_Status status;
            int receivedBestCost = INT_MAX;
            std::vector<Config> localQueue = recvConfigBatch(rows, cols, status, receivedBestCost);

            if (status.MPI_TAG == STOP_TAG) {
                break;
            }

            Result localResult = solveQueueOpenMP(
                localQueue, rows, cols, weights, shapesZ, shapesT,
                theoreticalMinimum, numOfThreadsInput, receivedBestCost
            );

            sendResult(0, localResult);
        }
    }

    //skonči čas a vypiš výsledky
    MPI_Barrier(MPI_COMM_WORLD);
    double end = MPI_Wtime();
    double elapsed = end - start;

    if (rank == 0) {
        std::cout << std::fixed << std::setprecision(3) << "\nRecurecive calls: " << finalResult.recursionCalls 
                << ", Total runtime: " << elapsed << "s, " << "Processes used: " << numProcs << ", Threads wanted: " << numOfThreadsInput << "; Threads used: " << usedThreads << std::endl;
        std::cout << "Best cost = " << finalResult.bestCost << "  (T=" << finalResult.bestT << ", Z=" << finalResult.bestZ << ")\n" << std::endl;
        printSolution(finalResult.bestState, weights, rows, cols);
    }

    MPI_Finalize();
    return 0;
}
