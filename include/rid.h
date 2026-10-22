#pragma once

// 记录地址：索引键映射到记录的存储位置（page 内第 slot_no 个槽位）
struct Rid {
    int page_no;
    int slot_no;

    Rid() : page_no(0), slot_no(0) {}
    Rid(int p, int s) : page_no(p), slot_no(s) {}
};

inline bool operator==(const Rid& a, const Rid& b) {
    return a.page_no == b.page_no && a.slot_no == b.slot_no;
}

inline bool operator!=(const Rid& a, const Rid& b) {
    return !(a == b);
}
