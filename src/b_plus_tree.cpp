#include "b_plus_tree.h"

#include <algorithm>

// GCC 6.x 需要类外定义 static constexpr 成员，避免链接期 ODR-use 问题
constexpr int BPlusTree::MAX_KEYS;

BPlusTree::Node* BPlusTree::find_leaf(int key) const {
    Node* cur = root_;
    if (cur == nullptr) {
        return nullptr;
    }
    while (!cur->is_leaf) {
        // 内部节点语义：keys[i] 是 children[i+1] 子树的最小 key。
        // 因此第一个满足 keys[i] > key 的下标 i，就是应进入的子树 children[i]；
        // 若 key == keys[i]，循环继续，最终进入 children[i+1]，正好找到该 key。
        std::size_t i = 0;
        while (i < cur->keys.size() && cur->keys[i] <= key) {
            ++i;
        }
        cur = cur->children[i];
    }
    return cur;
}

bool BPlusTree::insert(int key, Rid rid) {
    if (root_ == nullptr) {
        root_ = new Node(true);
    }

    Node* leaf = find_leaf(key);

    // 唯一键检查：叶子内 key 有序，二分定位
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it != leaf->keys.end() && *it == key) {
        return false;
    }

    const std::size_t idx = static_cast<std::size_t>(it - leaf->keys.begin());
    leaf->keys.insert(leaf->keys.begin() + idx, key);
    leaf->values.insert(leaf->values.begin() + idx, rid);
    ++size_;

    // 先插入后分裂：超过容量才触发
    if (static_cast<int>(leaf->keys.size()) > MAX_KEYS) {
        split_leaf(leaf);
    }
    return true;
}

Maybe<Rid> BPlusTree::get(int key) const {
    Node* leaf = find_leaf(key);
    if (leaf == nullptr) {
        return Maybe<Rid>();
    }
    for (std::size_t i = 0; i < leaf->keys.size(); ++i) {
        if (leaf->keys[i] == key) {
            return Maybe<Rid>(leaf->values[i]);
        }
        if (leaf->keys[i] > key) {
            break;  // 有序，可提前终止
        }
    }
    return Maybe<Rid>();
}

bool BPlusTree::erase(int key) {
    Node* leaf = find_leaf(key);
    if (leaf == nullptr) {
        return false;
    }
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it == leaf->keys.end() || *it != key) {
        return false;
    }
    const std::size_t pos = static_cast<std::size_t>(it - leaf->keys.begin());
    leaf->keys.erase(leaf->keys.begin() + pos);
    leaf->values.erase(leaf->values.begin() + pos);
    --size_;

    // 实验版简化：叶子删空则摘除（不合并、不借位、允许节点欠载）
    if (leaf->keys.empty()) {
        remove_leaf_node(leaf);
    }
    return true;
}

BPlusTree::Node* BPlusTree::rightmost_leaf(Node* n) {
    while (!n->is_leaf) {
        n = n->children.back();
    }
    return n;
}

BPlusTree::Node* BPlusTree::prev_leaf(Node* leaf) {
    Node* p = leaf->parent;
    Node* cur = leaf;
    while (p != nullptr) {
        auto it = std::find(p->children.begin(), p->children.end(), cur);
        const std::size_t idx = static_cast<std::size_t>(it - p->children.begin());
        if (idx > 0) {
            // leaf 的直接链表前驱 = 左兄弟子树的"最右"叶子（不是最左！）
            return rightmost_leaf(p->children[idx - 1]);
        }
        cur = p;
        p = p->parent;
    }
    return nullptr;  // leaf 是整棵树最左叶子
}

void BPlusTree::remove_leaf_node(Node* leaf) {
    Node* parent = leaf->parent;
    if (parent == nullptr) {
        // 整棵树只有一个空叶子
        delete leaf;
        root_ = nullptr;
        return;
    }

    // 先维护叶子链表：前驱的 next 指向 leaf 的后继
    Node* prev = prev_leaf(leaf);
    if (prev != nullptr) {
        prev->next = leaf->next;
    }

    auto it = std::find(parent->children.begin(), parent->children.end(), leaf);
    const std::size_t idx = static_cast<std::size_t>(it - parent->children.begin());

    parent->children.erase(it);

    // 摘除 children[idx] 后需删除一个分隔键，保持 children 数 = keys 数 + 1：
    //   idx == 0 时删 keys[0]，idx > 0 时删 keys[idx-1]（推导见实验笔记）
    if (!parent->keys.empty()) {
        const std::size_t key_idx = (idx == 0) ? 0 : idx - 1;
        parent->keys.erase(parent->keys.begin() + key_idx);
    }

    delete leaf;

    // 级联摘除空内部节点（children 已为 0）：
    // 不做借位/合并/上提（实验版简化），允许内部节点欠载（可只剩 1 个孩子），
    // 因此所有叶子仍保持等深，validate_tree 的等深检查恒成立。
    while (parent->children.empty()) {
        Node* victim = parent;
        parent = parent->parent;
        if (parent == nullptr) {
            root_ = nullptr;
            delete victim;
            break;
        }
        auto g_it = std::find(parent->children.begin(), parent->children.end(), victim);
        const std::size_t g_idx = static_cast<std::size_t>(g_it - parent->children.begin());
        parent->children.erase(g_it);
        if (!parent->keys.empty()) {
            const std::size_t g_key_idx = (g_idx == 0) ? 0 : g_idx - 1;
            parent->keys.erase(parent->keys.begin() + g_key_idx);
        }
        delete victim;
    }
}

int BPlusTree::height() const {
    int h = 0;
    Node* cur = root_;
    while (cur != nullptr) {
        ++h;
        cur = cur->children.empty() ? nullptr : cur->children[0];
    }
    return h;
}

bool BPlusTree::validate_leaf_chain() const {
    if (root_ == nullptr) {
        return true;
    }
    // 沿最左路径下到第一个叶子
    Node* cur = root_;
    while (!cur->is_leaf) {
        cur = cur->children[0];
    }
    int prev = -1;  // 本实验 key 从 0 开始
    int visited = 0;
    while (cur != nullptr) {
        for (std::size_t i = 0; i < cur->keys.size(); ++i) {
            if (cur->keys[i] <= prev) {
                return false;  // 违反严格递增（唯一键）或链表有环
            }
            prev = cur->keys[i];
        }
        ++visited;
        cur = cur->next;
    }
    return visited > 0;
}

std::vector<Rid> BPlusTree::range_query(int lower, int upper) const {
    std::vector<Rid> out;
    if (root_ == nullptr || lower > upper) {
        return out;
    }
    // 找到 lower 所在的叶子（可能含小于 lower 的 key），随后沿链表收集
    Node* leaf = find_leaf(lower);
    while (leaf != nullptr) {
        for (std::size_t i = 0; i < leaf->keys.size(); ++i) {
            if (leaf->keys[i] > upper) {
                return out;  // 链表有序，可提前终止
            }
            if (leaf->keys[i] >= lower) {
                out.push_back(leaf->values[i]);
            }
        }
        leaf = leaf->next;
    }
    return out;
}

bool BPlusTree::validate_tree(std::string& err) const {
    if (root_ == nullptr) {
        return true;
    }
    int leaf_depth = -1;
    std::size_t leaf_key_count = 0;
    std::vector<Node*> leaves;
    validate_node(root_, 0, leaf_depth, err, leaf_key_count, leaves);
    if (!err.empty()) {
        return false;
    }

    // 叶子链表：全局严格递增、无环、节点数与递归收集的叶子数一致
    Node* cur = root_;
    while (!cur->is_leaf) {
        cur = cur->children[0];
    }
    int prev = -1;  // 本实验 key 从 0 开始
    std::size_t visited = 0;
    while (cur != nullptr) {
        if (cur->keys.empty()) {
            err = "leaf node is empty";
            return false;
        }
        if (cur->keys.front() <= prev) {
            err = "leaf chain not strictly increasing";
            return false;
        }
        prev = cur->keys.back();
        ++visited;
        cur = cur->next;
    }
    if (visited != leaves.size()) {
        err = "leaf chain count mismatch: chain=" + std::to_string(visited) +
              " tree=" + std::to_string(leaves.size());
        return false;
    }
    if (leaf_key_count != size_) {
        err = "size_ mismatch with leaf key count";
        return false;
    }
    return true;
}

BPlusTree::KeyRange BPlusTree::validate_node(Node* n, int depth, int& leaf_depth,
                                             std::string& err, size_t& leaf_key_count,
                                             std::vector<Node*>& leaves) const {
    // 节点内 key 严格递增
    for (std::size_t i = 1; i < n->keys.size(); ++i) {
        if (n->keys[i] <= n->keys[i - 1]) {
            err = "node keys not strictly increasing";
            return KeyRange{0, 0};
        }
    }

    if (n->is_leaf) {
        if (n->keys.empty()) {
            err = "leaf node is empty";
            return KeyRange{0, 0};
        }
        if (leaf_depth == -1) {
            leaf_depth = depth;
        } else if (depth != leaf_depth) {
            err = "leaf depth mismatch";
            return KeyRange{0, 0};
        }
        if (n->values.size() != n->keys.size()) {
            err = "leaf values/keys count mismatch";
            return KeyRange{0, 0};
        }
        leaves.push_back(n);
        leaf_key_count += n->keys.size();
        return KeyRange{n->keys.front(), n->keys.back()};
    }

    if (n->children.size() != n->keys.size() + 1) {
        err = "internal children/keys count mismatch";
        return KeyRange{0, 0};
    }
    for (Node* c : n->children) {
        if (c->parent != n) {
            err = "parent pointer mismatch";
            return KeyRange{0, 0};
        }
    }

    KeyRange left = validate_node(n->children[0], depth + 1, leaf_depth, err,
                                  leaf_key_count, leaves);
    if (!err.empty()) {
        return KeyRange{0, 0};
    }
    int mn = left.mn;
    int mx = left.mx;
    for (std::size_t i = 0; i < n->keys.size(); ++i) {
        // 分隔键划分有效性（宽松版：允许删除后分隔键滞后）：
        //   keys[i] 必须严格大于左子树最大 key，且不超过右子树最小 key
        if (n->keys[i] <= mx) {
            err = "separator not greater than left subtree max";
            return KeyRange{0, 0};
        }
        KeyRange right = validate_node(n->children[i + 1], depth + 1, leaf_depth,
                                       err, leaf_key_count, leaves);
        if (!err.empty()) {
            return KeyRange{0, 0};
        }
        if (n->keys[i] > right.mn) {
            err = "separator exceeds right subtree min (must be <=)";
            return KeyRange{0, 0};
        }
        mx = right.mx;
    }
    return KeyRange{mn, mx};
}

void BPlusTree::split_leaf(Node* leaf) {
    // 例：MAX_KEYS=4，满节点 keys.size()=5（先插入后分裂）
    // mid = 2 → 左 [0,2)，右 [2,5)，上提 keys[2]
    const std::size_t mid = leaf->keys.size() / 2;

    Node* right = new Node(true);
    right->keys.assign(leaf->keys.begin() + mid, leaf->keys.end());
    right->values.assign(leaf->values.begin() + mid, leaf->values.end());

    leaf->keys.resize(mid);
    leaf->values.resize(mid);

    // 维护叶子链表
    right->next = leaf->next;
    leaf->next = right;

    // 上提右节点最小 key
    promote(leaf, right->keys.front(), right);
}

void BPlusTree::split_internal(Node* node) {
    // 例：keys.size()=5，mid=2
    // 上提 keys[2]；右节点拿 keys[3..4] 与 children[3..5]；左节点留 keys[0..1] 与 children[0..2]
    const std::size_t mid = node->keys.size() / 2;
    const int up_key = node->keys[mid];

    Node* right = new Node(false);
    right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
    right->children.assign(node->children.begin() + mid + 1, node->children.end());

    node->keys.resize(mid);
    node->children.resize(mid + 1);

    for (Node* c : right->children) {
        c->parent = right;
    }

    promote(node, up_key, right);
}

void BPlusTree::promote(Node* child, int up_key, Node* right) {
    Node* parent = child->parent;

    if (parent == nullptr) {
        // 原根分裂：新建根，树高 +1
        Node* new_root = new Node(false);
        new_root->keys.push_back(up_key);
        new_root->children.push_back(child);
        new_root->children.push_back(right);
        child->parent = new_root;
        right->parent = new_root;
        root_ = new_root;
        return;
    }

    // 在父节点中找到 child 的下标，把分隔键插到该位置，right 挂为它的右孩子
    auto it = std::find(parent->children.begin(), parent->children.end(), child);
    const std::size_t idx = static_cast<std::size_t>(it - parent->children.begin());

    parent->keys.insert(parent->keys.begin() + idx, up_key);
    parent->children.insert(parent->children.begin() + idx + 1, right);
    right->parent = parent;

    if (static_cast<int>(parent->keys.size()) > MAX_KEYS) {
        split_internal(parent);
    }
}

void BPlusTree::destroy(Node* cur) {
    if (cur == nullptr) {
        return;
    }
    if (!cur->is_leaf) {
        for (Node* c : cur->children) {
            destroy(c);
        }
    }
    delete cur;
}
