#include <string>

struct Test{
    std::string type;
    int operations;
    double medianTime;
    int totalRuns;
    int trades;
};

double RunMostlyResting(int runs);
double RunManyInstruments(int runs);
double RunCancelHeavy(int runs);
double RunRandom(int runs);
double RunMostlyCrossing(int runs);
double RunLargeSweep(int runs);
void PrintTests(const std::vector <Test>&);