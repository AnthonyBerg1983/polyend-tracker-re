#include "app/undo.h"

namespace ot {

void UndoRing::push(const Pattern& cur, uint8_t num) {
    if (!slots_ || n_ < 2) return;
    slots_[idx(pos_)] = cur;
    nums_[idx(pos_)] = num;
    if (pos_ < n_ - 1) ++pos_;
    else base_ = (base_ + 1) % n_;
    redo_ = 0;
}

bool UndoRing::undo(Pattern& cur, uint8_t curNum, uint8_t* num) {
    if (!slots_ || pos_ == 0) return false;
    // Keep the current state as the redo point, then step back.
    slots_[idx(pos_)] = cur;
    nums_[idx(pos_)] = curNum;
    --pos_;
    cur = slots_[idx(pos_)];
    if (num) *num = nums_[idx(pos_)];
    ++redo_;   // invariant pos_ + redo_ <= n_ - 1 is preserved
    return true;
}

bool UndoRing::redo(Pattern& cur, uint8_t curNum, uint8_t* num) {
    (void)curNum;
    if (!slots_ || redo_ == 0) return false;
    ++pos_;
    --redo_;
    cur = slots_[idx(pos_)];
    if (num) *num = nums_[idx(pos_)];
    return true;
}

void FileUndo::push(uint8_t num, const Pattern& p) {
    if (!slots_ || n_ == 0) return;
    slots_[top_] = p;
    nums_[top_] = num;
    top_ = (top_ + 1) % n_;
    if (count_ < n_) ++count_;
}

bool FileUndo::pop(uint8_t* num, Pattern* out) {
    if (!slots_ || count_ == 0) return false;
    top_ = (top_ + n_ - 1) % n_;
    --count_;
    if (num) *num = nums_[top_];
    if (out) *out = slots_[top_];
    return true;
}

}  // namespace ot
