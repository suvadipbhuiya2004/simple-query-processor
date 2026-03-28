#include "execution/seq_scan_executor.h"

#include <stdexcept>

// Sequential scan executor implementation.

SeqScanExecutor::SeqScanExecutor(Table* t) : table(t), index(0) {
    if (table == nullptr) {
        throw std::runtime_error("SeqScanExecutor received a null table");
    }
}

void SeqScanExecutor::open() {
    index = 0;
}

bool SeqScanExecutor::next(Row& row) {
    if (index >= table->size()) {
        return false;
    }
    row = (*table)[index++];
    return true;
}

void SeqScanExecutor::close() {}
