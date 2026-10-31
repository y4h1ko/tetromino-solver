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

static const int CELL_UNCOVERED = INT_MAX;

//orientacia tvaru
struct Shape { int dr[4]; int dc[4]; };

//stav dosky a pocet T/Z
struct Config { std::vector<int> board; int countT; int countZ; int actualCost;};

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

//vytvori zoznam všetkych moznych pociatocnych konfiguracii s 1 tvarom
//vytvori objekt dosky, kde updatne kolko Z/T je pouzitych, aka je aktualna cena
//vrati zoznam vsetkych pociatocnych konfiguracii
std::vector<Config> initializeConfigs(const int rows, const int cols, const std::vector<Shape>& shapesZ, const std::vector<Shape>& shapesT) {
    
    int N = rows * cols;                    //vypocet poctu poli na 1D doske
    std::vector<int> emptyBoard(N, 0);      //prazdna doska
    std::vector<Config> queue;              //zoznam vsetkych poc. konfig.

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {

            //vkladanie Z
            for (int i = 0; i < (int)shapesZ.size(); i++) {
                if (canPlace(emptyBoard, rows, cols, r, c, shapesZ[i])) {
                    Config cfg;
                    cfg.board = emptyBoard;
                    cfg.countT = 0;
                    cfg.countZ = 0;
                    cfg.actualCost = 0;

                    place(cfg.board, rows, cols, r, c, shapesZ[i], -1);
                    cfg.countZ = 1;

                    queue.push_back(cfg);
                }
            }

            //vkladanie T
            for (int i = 0; i < (int)shapesT.size(); i++) {
                if (canPlace(emptyBoard, rows, cols, r, c, shapesT[i])) {
                    Config cfg;
                    cfg.board = emptyBoard;
                    cfg.countT = 0;
                    cfg.countZ = 0;
                    cfg.actualCost = 0;

                    place(cfg.board, rows, cols, r, c, shapesT[i], +1);
                    cfg.countT = 1;

                    queue.push_back(cfg);
                }
            }
        }
    }

    return queue;
}

//najde prve nerozhodnuty index na doske a vrati ho
int findFirstUndecided(const std::vector<int>& board) {
    for (int i = 0; i < (int)board.size(); i++)
        if (board[i] == 0) return i;
    return -1;
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

    //polož T
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

//načíta vstup, vytvori všetky počiatočne konfiguracie a spustí riešenie
int main(int argc, char* argv[]) {
    int rows, cols;
    std::vector<int> weights;
    int theoreticalMinimum = 0;

    std::string inputFile;
    int usedThreads;
    bool inputThreads = false;    
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
                std::cerr << "Pocet threads musi byt cele cislo.\n";
                return EXIT_FAILURE;
            }
        }
    }

    //Parameter je nutný
    if (inputFile.empty()) {
        std::cerr << "Chyba: Musis zadat vstupny subor pomocou -f alebo --file\n";
        return EXIT_FAILURE;
    }
    if (inputThreads && (numOfThreadsInput < 1 || numOfThreadsInput > maxThreads)) {
        std::cerr << "Pocet threads musi byt cele cislo v intervale [1,"<< maxThreads << "].\n";
        return EXIT_FAILURE;
    } 
    
    //načitanie a spustenie aalgoritmu
    loadFile(inputFile, rows, cols, weights);
    double start = omp_get_wtime();
    std::vector<Config> queue = initializeConfigs(rows, cols, shapesZ, shapesT);

    std::vector<int> bestState;
    long long recursionCalls = 0;
    int bestT = 0, bestZ = 0;
    int bestCost = INT_MAX;
    bool stop = false;

    // vypocet trivialnej dolnej medze
    std::vector<int> tmp = weights;
    std::sort(tmp.begin(), tmp.end());

    int k = (rows * cols) % 4;
    for (int i = 0; i < k; i++) { theoreticalMinimum += tmp[i]; }


    //paralelna oblast a zadanie počtu vlákien v týme
    #pragma omp parallel num_threads(numOfThreadsInput)
    {
        //jedno vlakno vytvori frontu pociatocnych konfiguracii
        #pragma omp master
        {
            usedThreads = omp_get_num_threads();
            //Prehladaj všetky moznosti z poč. konfig.
            for (int i = 0; i < (int)queue.size(); i++) {

                //soustenie  vlakien na pociatocne konfiguracie
                #pragma omp task firstprivate(i)
                {
                    long long localRecursionCalls = 0;

                    bbDfs(queue[i], rows, cols, weights, shapesZ, shapesT,
                        bestCost, bestState, bestT, bestZ, theoreticalMinimum, stop, localRecursionCalls);

                    //atomicke zapisovanie do total recursionCalls, sčítanie všetkych rekurzivných volaní
                    #pragma omp atomic
                    recursionCalls += localRecursionCalls;
                }
            }
            //ukončenie paralelizmu spoločne
            #pragma omp taskwait
        }
    }

    if (bestCost == INT_MAX) {
        std::cout << "\nNenaslo sa ziadne riesenie.\n";
        return 0;
    }

    //skonči čas a vypiš výsledky
    double end = omp_get_wtime();
    double elapsed = end - start;
    std::cout << std::fixed << std::setprecision(3) << "\nRecurecive calls: " << recursionCalls 
            << ", Total runtime: " << elapsed << "s, " << "Threads wanted: " << numOfThreadsInput << "; Threads used: " << usedThreads << std::endl;
    std::cout << "Best cost = " << bestCost << "  (T=" << bestT << ", Z=" << bestZ << ")\n" << std::endl;
    printSolution(bestState, weights, rows, cols);

    return 0;
}