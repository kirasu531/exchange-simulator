# V0.1 Profiling

Profiles taken at 500K operations using `gprof`.

## Main findings

* **CancelHeavy:** `std::set<Order>` insert + lookup/erase ≈ **34%** of self time. Cancellation is strongly affected by tree operations.
* **Resting:** dominated by `AddOrder`; tree insertion and ID tracking are visible costs as the book grows.
* **LargeSweep:** dominated by `AddOrder`; instrument-map lookup and order insertion are also significant.
* **MostlyCrossing:** `AddOrder` remains dominant; instrument lookup and ID tracking are still visible.

## Conclusion

Repeated hotspots are:

* `OrderBook::AddOrder`
* `MatchingEngine::AddOrder`
* `std::set<Order>` insertion/search/erase
* global ID tracking
* instrument-map lookup

First optimization candidates:

1. avoid searching the order tree again during cancellation;
2. investigate a price-level order-book representation;
3. reduce repeated tree/map lookups.

All changes must be benchmarked against the V0.1 baseline.

