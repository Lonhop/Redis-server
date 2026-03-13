#include "data_structures/heap.h"
#include <algorithm>

namespace redis::data_structures {
    Heap::Heap(Heap&& other) noexcept : heap_(std::move(other.heap_)) {
        for (size_t i = 0; i < heap_.size(); ++i) {
            if (heap_[i].ref) {
                *heap_[i].ref = i;
            }
        }
    }
    Heap& Heap::operator=(Heap&& other) noexcept {
        if (this != &other) {
            heap_ = std::move(other.heap_);
            for (size_t i = 0; i < heap_.size(); ++i) {
                if (heap_[i].ref) {
                    *heap_[i].ref = i;
                }
            }
        }
        return *this;
    }
    void Heap::validate_pos(size_t pos) const {
        if (pos >= heap_.size()) {
            throw std::out_of_range("Heap position out of range");
        }
    }
    bool Heap::contains(size_t pos) const noexcept {
        if (pos >= heap_.size()) {
            return false;
        }
        if (!heap_[pos].ref) {
            return false;
        }
        return *heap_[pos].ref == pos;
    }
    const HeapItem& Heap::at(size_t pos) const {
        validate_pos(pos);
        return heap_[pos];
    }
    HeapItem& Heap::at(size_t pos) {
        validate_pos(pos);
        return heap_[pos];
    }
    const HeapItem& Heap::top() const {
        if (heap_.empty()) {
            throw std::runtime_error("Heap is empty");
        }
        return heap_[0];
    }
    HeapItem& Heap::top() {
        if (heap_.empty()) {
            throw std::runtime_error("Heap is empty");
        }
        return heap_[0];
    }
    void Heap::swap_items(size_t i, size_t j) {
        if (i == j) return;
        std::swap(heap_[i], heap_[j]);
        if (heap_[i].ref) {
            *heap_[i].ref = i;
        }
        if (heap_[j].ref) {
            *heap_[j].ref = j;
        }
    }
    void Heap::up(size_t pos) {
        while (pos > 0) {
            size_t p = parent(pos);
            if (!(heap_[pos] < heap_[p])) {
                break;
            }
            swap_items(pos, p);
            pos = p;
        }
    }
    void Heap::down(size_t pos) {
        size_t n = heap_.size();
        while (true) {
            size_t smallest = pos;
            size_t l = left(pos);
            size_t r = right(pos);
            if (l < n && heap_[l] < heap_[smallest]) {
                smallest = l;
            }
            if (r < n && heap_[r] < heap_[smallest]) {
                smallest = r;
            }
            if (smallest == pos) {
                break;
            }
            swap_items(pos, smallest);
            pos = smallest;
        }
    }
    void Heap::push(uint64_t val, size_t* ref) {
        size_t pos = heap_.size();
        heap_.emplace_back(val, ref);
        if (ref) {
            *ref = pos;
        }
        up(pos);
    }
    void Heap::update(size_t pos, uint64_t new_val) {
        validate_pos(pos);
        uint64_t old_val = heap_[pos].val;
        if (old_val == new_val) {
            return;
        }
        heap_[pos].val = new_val;
        if (new_val < old_val) {
            up(pos);
        }
        else {
            down(pos);
        }
    }
    void Heap::erase(size_t pos) {
        validate_pos(pos);
        size_t last = heap_.size() - 1;
        if (heap_[pos].ref) {
            *heap_[pos].ref = SIZE_MAX;
        }
        if (pos != last) {
            swap_items(pos, last);
        }
        heap_.pop_back();
        if (pos < heap_.size()) {
            if (pos > 0 && heap_[pos] < heap_[parent(pos)]) {
                up(pos);
            } else {
                down(pos);
            }
        }
    }
}