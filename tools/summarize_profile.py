import sys


def friendly_name(function):
    if "_Rb_tree<Order" in function:
        if "equal_range" in function:
            return "std::set<Order> lookup/erase"

        if "_M_insert_unique" in function:
            return "std::set<Order> insertion"

    if "_Rb_tree<int" in function:
        if "_M_insert_unique" in function:
            return "std::set<int> insertion"

    if "_Rb_tree<std::__cxx11::basic_string" in function and "OrderBook" in function:
        if "_M_get_insert" in function:
            return "std::map<std::string, OrderBook> lookup/insertion"

    if "basic_string" in function and "_M_assign" in function:
        return "std::string assignment"

    if "std::vector<Order" in function and "_M_default_append" in function:
        return "None"

    if "_Hashtable<std::__cxx11::basic_string" in function and "OrderBook" in function and "_M_rehash" in function:
        return "std::unordered_map<std::string, OrderBook> rehash"

    if "_Hashtable<int, int" in function and "_M_rehash" in function:
        return "std::unordered_set<int> rehash"

    if "_Map_base<std::__cxx11::basic_string" in function and "OrderBook" in function and "operator[]" in function:
        return "std::unordered_map<std::string, OrderBook> lookup/operator[]"

    if "_Hashtable<int, std::pair<int const, std::__cxx11::basic_string" in function and "_M_rehash" in function:
        return "std::unordered_map<int, std::string> rehash"

    if "_Hashtable<int, std::pair<int const, Order>" in function and "_M_rehash" in function:
        return "std::unordered_map<int, Order> rehash"

    if "_Map_base<int" in function and "basic_string" in function and "operator[]" in function:
        return "std::unordered_map<int, std::string> lookup/operator[]"

    if "_Rb_tree<int" in function and "queue<Order" in function and "_M_get_insert_hint_unique_pos" in function:
        return "Price-level map insertion"

    return function


def should_ignore(function):
    ignored = [
        "frame_dummy",
        "_init",
        "getMedian(",
        "ResetRng(",
        "ResetTime(",
        "getExecTime(",
        "mersenne_twister_engine",
        "Trade",
        "None"
    ]

    for name in ignored:
        if name in function:
            return True

    if function.startswith("Run"):
        return True

    return False


def parse_profile():
    entries = []

    for line in sys.stdin:
        parts = line.split()

        if len(parts) < 4:
            continue

        try:
            percentage = float(parts[0])
            self_time = float(parts[2])
        except ValueError:
            continue

        try:
            calls = int(parts[3])
            function = " ".join(parts[6:])
        except ValueError:
            calls = None
            function = " ".join(parts[3:])

        if should_ignore(function) or function == "":
            continue

        friendly = friendly_name(function)

        if friendly is "None":
            continue

        entries.append(
            (
                percentage,
                self_time,
                calls,
                friendly
            )
        )

    return entries


def print_summary(entries, workload, top_n):
    entries.sort(key=lambda entry: entry[0], reverse=True)

    entries = entries[:top_n]

    print(f"Workload: {workload}")
    print()
    print(f"| {'% Time':>8} | {'Self (s)':>8} | {'Calls':>12} | {'Function':<60} |")
    print(f"|{'-' * 10}|{'-' * 10}|{'-' * 14}|{'-' * 62}|")

    for percentage, self_time, calls, function in entries:
        calls_text = "-" if calls is None else f"{calls:,}"

        print(
            f"| {percentage:>7.2f}% "
            f"| {self_time:>8.2f} "
            f"| {calls_text:>12} "
            f"| {function:<60} |"
        )


if len(sys.argv) not in [2, 3, 4]:
    print(
        "Usage: python3 summarize_profile.py "
        "<profile-file> [workload] [top-N]"
    )
    sys.exit(1)

if len(sys.argv) >= 2:
    workload = sys.argv[1]
else:
    workload = "Unspecified"

if len(sys.argv) == 3:
    top_n = int(sys.argv[2])
else:
    top_n = 5


entries = parse_profile()

print_summary(entries, workload, top_n)