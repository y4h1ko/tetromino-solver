# NI-PDP: Parallel Tetromino Solver

Semester project for NI-PDP at FIT CTU (2025/2026), comparing sequential and parallel C++ algorithms for a board-covering optimization problem.

## Goal

Place non-overlapping T- and Z-shaped tetrominoes on a weighted rectangular board to **minimize the sum of weights of uncovered cells**. Pieces must stay inside the board, and the numbers of T and Z pieces may differ by at most one.

The solvers use depth-first search with branch-and-bound pruning. The project compares runtime, speedup, and parallel efficiency across these implementations.

## Main parts

| File or folder | Purpose |
| --- | --- |
| `Algoritmy/part1seq.cpp` | Sequential baseline. |
| `Algoritmy/part2parTask.cpp` | OpenMP task parallelism over recursive search branches. |
| `Algoritmy/part3parData.cpp` | OpenMP parallel processing of initial board configurations. |
| `Algoritmy/part3parDataWithBFS.cpp` | Variant that generates initial configurations using breadth-first search (BFS). |
| `Algoritmy/part4parMPI.cpp` | Hybrid MPI + OpenMP solver that distributes batches of configurations between processes. |
| `Algoritmy/mapa/` | Sample weighted boards. |
| `final-report/` | LaTeX report, benchmark tables, and the final PDF. |

## Usage

Build with a C++ compiler and OpenMP support; the hybrid version also requires MPI. Even the sequential version uses OpenMP for timing.

Example with GCC, from the repository root:

```sh
g++ -O3 -fopenmp Algoritmy/part1seq.cpp -o part1seq
./part1seq -f Algoritmy/mapa/mapa3_5.txt
```

Input files contain the row and column counts followed by the cell weights. Output includes the best cost, piece counts, board layout, runtime, and recursive-call count.

Parallel options: `-t` sets threads; `-d` sets the task depth limit in part 2; `-l` sets the BFS depth in the BFS and MPI versions; `-c` sets the MPI batch size.
