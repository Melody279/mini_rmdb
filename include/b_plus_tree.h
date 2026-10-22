#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "maybe.h"
#include "rid.h"

// 内存版 B+ 树
//
// Day 1：insert / get / 节点分裂
// Day 2：range_query / erase / validate_tree（全量结构校验）
//
// 设计约束（与计划文档一致）：
//   - Key 为 int，Value 为 Rid
//   - 每个节点最多 MAX_KEYS 个 key（实验用小度数，4 个即分裂，便于测试覆盖）
//   - 叶子节点按 key 有序，并通过 next 指针串成链表
//   - 唯一键语义：插入已存在的 key 返回 false；删除不存在的 key 返回 false
//   - 实验版删除简化：不维护节点下界（允许欠载）、不借位不合并，
//     仅处理"叶子删空则摘除 + 内部节点只剩一个孩子则上提压缩"；
//     删除最小 key 后父节点分隔键允许滞后（仍满足划分有效性，见 validate_tree）
//   - 不做磁盘 Page / Buffer Pool / 并发 / 恢复
class BPlusTree {
public:
    static constexpr int MAX_KEYS = 4;

    BPlusTree() : root_(nullptr), size_(0) {}
    ~BPlusTree() { destroy(root_); }

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    // 唯一键插入；成功返回 true，key 已存在返回 false
    bool insert(int key, Rid rid);

    // 点查询；key 不存在时返回空 Maybe
    Maybe<Rid> get(int key) const;

    // 删除；key 不存在时返回 false
    bool erase(int key);

    // 范围查询：[lower, upper] 闭区间，按 key 升序返回所有 Rid
    std::vector<Rid> range_query(int lower, int upper) const;

    size_t size() const { return size_; }

    // 树高（只有根节点时为 1，空树为 0）
    int height() const;

    // Day1 轻量检查：叶子链表上的 key 严格递增、无环
    // 用于在测试中快速发现分裂 / 上提逻辑错误
    bool validate_leaf_chain() const;

    // Day2 全量结构校验，失败时把原因写入 err：
    //   1. 内部节点 children 数 = keys 数 + 1
    //   2. 每个节点 key 严格递增，叶子 values 数与 keys 数一致
    //   3. 父指针一致
    //   4. 所有叶子深度相同
    //   5. 分隔键划分有效：keys[i] > 左子树最大 key 且 keys[i] <= 右子树最小 key
    //      （允许删除后分隔键滞后，仍满足划分有效性）
    //   6. 叶子链表按 key 全局严格递增、无环、节点数与叶子数一致
    //   7. size_ 与叶子 key 总数一致
    bool validate_tree(std::string& err) const;

private:
    struct Node {
        explicit Node(bool leaf) : is_leaf(leaf) {}

        bool is_leaf;
        std::vector<int> keys;

        // 叶子节点使用
        std::vector<Rid> values;
        Node* next = nullptr;

        // 内部节点使用（children 数量 = keys 数量 + 1）
        std::vector<Node*> children;

        Node* parent = nullptr;
    };

    struct KeyRange {
        int mn;
        int mx;
    };

    Node* root_;
    size_t size_;

    // 从根开始，沿内部节点下探到 key 所在的叶子节点
    Node* find_leaf(int key) const;

    // 叶子节点已满（keys.size() > MAX_KEYS）时分裂：
    // 右半 [mid, end) 移入新节点，keys[mid] 上提到父节点
    void split_leaf(Node* leaf);

    // 内部节点已满时分裂：
    // keys[mid] 作为分隔符上提（不保留在左右任何一侧），
    // 右侧 keys[mid+1, end) 与其 children 移入新节点
    void split_internal(Node* node);

    // 把分隔键 up_key 上提到 child 的父节点，并把 right 挂为右兄弟；
    // 若 child 是根，则新建根节点
    void promote(Node* child, int up_key, Node* right);

    // 叶子被删空后从父链摘除，并维护叶子链表（前驱 next 接到后继）；
    // 若内部节点因此变空，则级联摘除空节点（不做借位/合并/上提，允许欠载）
    void remove_leaf_node(Node* leaf);

    // 返回 subtree 中最右的叶子（用于找链表前驱）
    static Node* rightmost_leaf(Node* n);

    // 返回 leaf 在叶子链表中的直接前驱（左兄弟子树的最右叶子）；
    // leaf 是最左叶子时返回 nullptr
    static Node* prev_leaf(Node* leaf);

    // validate_tree 的递归辅助：返回子树 key 范围 [mn, mx]
    KeyRange validate_node(Node* n, int depth, int& leaf_depth, std::string& err,
                           size_t& leaf_key_count, std::vector<Node*>& leaves) const;

    void destroy(Node* cur);
};
