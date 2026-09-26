#include "matching_engine.hpp"

struct Test{
    std::string type;
    int operations;
    double time;
    int totalRuns;
    int trades;
};

bool SendAll(int fd, const void *data, size_t size);
bool RecvLine(int fd, std::string &pending, std::string &line, void *data, size_t size);

void PrintTests(const std::vector <Test> &tests);

void ResetRng();
void ResetTime();
double getExecTime();

std::string AddOrderToStr(const Order &order);
std::string CancelToStr(const int &orderId);

void RoundTrip(const std::string &command, void *data, size_t size, const std::string verdict);
void GetTrades(void *data, size_t size);

double RunMostlyResting();
double RunManyInstruments();
double RunCancelHeavy();
double RunRandom();
double RunMostlyCrossing();
double RunLargeSweep();

double RunTest(int testId);