#include "b_plus_tree.h"

#include <algorithm>
#include <limits>

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
    if (static_cast<int>(leaf->keys.size()) > max_keys_) {
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

    // 完整下界维护：借位 / 合并 / 根压缩（替换 Day2 的"删空才摘除"简化版）
    rebalance(leaf);
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

// 删除后维护节点下界（借位 → 合并 → 根压缩），自底向上逐层修复。
//
// 分隔键语义：内部节点 keys[i] = children[i+1] 子树的最小 key（删除后允许滞后）。
// 借位/合并的旋转公式就是按这个语义推导的，见 README 设计决策。
void BPlusTree::rebalance(Node* n) {
    while (n != nullptr) {
        Node* parent = n->parent;
        if (parent == nullptr) {
            // ---- 根节点：允许欠载，只处理两类特殊情况 ----
            if (n->is_leaf) {
                if (n->keys.empty()) {  // 整棵树删空
                    delete n;
                    root_ = nullptr;
                }
                return;
            }
            if (n->children.size() == 1) {  // 根压缩：唯一孩子顶上，树高减一
                Node* child = n->children[0];
                child->parent = nullptr;
                root_ = child;
                delete n;
            }
            return;  // 根内部节点有 >= 2 个孩子：合法，无需处理
        }

        auto it = std::find(parent->children.begin(), parent->children.end(), n);
        const std::size_t idx = static_cast<std::size_t>(it - parent->children.begin());
        Node* left = (idx > 0) ? parent->children[idx - 1] : nullptr;
        Node* right = (idx + 1 < parent->children.size()) ? parent->children[idx + 1]
                                                           : nullptr;

        if (n->is_leaf) {
            if (static_cast<int>(n->keys.size()) >= min_keys_) {
                return;  // 达标
            }
            // 空叶子借位一次仍欠载，直接合并；非空时优先借位
            if (!n->keys.empty() && left != nullptr && static_cast<int>(left->keys.size()) > min_keys_) {
                // 借左：左兄弟最大 key 移入本叶最前，父分隔键更新为新的"本叶最小 key"
                const int lmax = left->keys.back();
                const Rid lval = left->values.back();
                left->keys.pop_back();
                left->values.pop_back();
                n->keys.insert(n->keys.begin(), lmax);
                n->values.insert(n->values.begin(), lval);
                parent->keys[idx - 1] = lmax;
                return;
            }
            if (!n->keys.empty() && right != nullptr && static_cast<int>(right->keys.size()) > min_keys_) {
                // 借右：右兄弟最小 key 移入本叶末尾，父分隔键更新为右兄弟的新最小
                const int rmin = right->keys.front();
                const Rid rval = right->values.front();
                right->keys.erase(right->keys.begin());
                right->values.erase(right->values.begin());
                n->keys.push_back(rmin);
                n->values.push_back(rval);
                parent->keys[idx] = right->keys.front();
                return;
            }
            if (left != nullptr) {
                // 并入左兄弟（含空叶）：内容搬入，维护链表，父节点少一个孩子
                left->keys.insert(left->keys.end(), n->keys.begin(), n->keys.end());
                left->values.insert(left->values.end(), n->values.begin(), n->values.end());
                left->next = n->next;
                parent->children.erase(parent->children.begin() + static_cast<long>(idx));
                parent->keys.erase(parent->keys.begin() + static_cast<long>(idx - 1));
                delete n;
                n = parent;  // 父节点可能欠载，继续上溯
                continue;
            }
            // 无左兄弟（本叶是最左叶子）→ 并入右兄弟
            right->keys.insert(right->keys.begin(), n->keys.begin(), n->keys.end());
            right->values.insert(right->values.begin(), n->values.begin(), n->values.end());
            if (Node* prev = prev_leaf(n)) {  // 本叶前驱的 next 改为指向 right
                prev->next = right;
            }
            parent->children.erase(parent->children.begin() + static_cast<long>(idx));
            parent->keys.erase(parent->keys.begin() + static_cast<long>(idx));
            delete n;
            n = parent;
            continue;
        }

        // ---- 内部节点（下界 min_children_ 个孩子）----
        if (static_cast<int>(n->children.size()) >= min_children_) {
            return;
        }
        if (left != nullptr && static_cast<int>(left->children.size()) > min_children_) {
            // 借左旋转：左兄弟最大键域子树 S 移入本节点最前；
            // 旧分隔键 x 下落为本节点新 keys[0]；分隔键更新为 S 的最小 key（= 左兄弟旧最大 key）
            Node* S = left->children.back();
            const int x = parent->keys[idx - 1];
            parent->keys[idx - 1] = left->keys.back();
            left->keys.pop_back();
            left->children.pop_back();
            S->parent = n;
            n->children.insert(n->children.begin(), S);
            n->keys.insert(n->keys.begin(), x);
            return;
        }
        if (right != nullptr && static_cast<int>(right->children.size()) > min_children_) {
            // 借右旋转：右兄弟最小键域子树 S 移入本节点末尾；
            // 旧分隔键 x 下落为本节点新 keys[尾]；分隔键更新为右兄弟的新最小 key
            Node* S = right->children.front();
            const int x = parent->keys[idx];
            const int y = right->keys.front();
            right->children.erase(right->children.begin());
            right->keys.erase(right->keys.begin());
            parent->keys[idx] = y;
            S->parent = n;
            n->children.push_back(S);
            n->keys.push_back(x);
            return;
        }
        if (left != nullptr) {
            // 并入左兄弟：父分隔键下拉，本节点所有子树整体搬入
            const int x = parent->keys[idx - 1];
            left->keys.push_back(x);
            left->children.push_back(n->children[0]);
            for (std::size_t i = 0; i < n->keys.size(); ++i) {
                left->keys.push_back(n->keys[i]);
                left->children.push_back(n->children[i + 1]);
            }
            for (Node* c : n->children) {
                c->parent = left;
            }
            parent->children.erase(parent->children.begin() + static_cast<long>(idx));
            parent->keys.erase(parent->keys.begin() + static_cast<long>(idx - 1));
            delete n;
            n = parent;
            continue;
        }
        // 无左兄弟 → 把右兄弟并入本节点（链表只连叶子，内部节点顺序无关）
        {
            const int x = parent->keys[idx];
            n->keys.push_back(x);
            n->children.push_back(right->children[0]);
            for (std::size_t i = 0; i < right->keys.size(); ++i) {
                n->keys.push_back(right->keys[i]);
                n->children.push_back(right->children[i + 1]);
            }
            for (Node* c : right->children) {
                c->parent = n;
            }
            parent->children.erase(parent->children.begin() + static_cast<long>(idx + 1));
            parent->keys.erase(parent->keys.begin() + static_cast<long>(idx));
            delete right;
            n = parent;
            continue;
        }
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
    int prev = std::numeric_limits<int>::lowest();  // 哨兵：兼容负数 key（勿用 -1 假设 key>=0）
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
    int prev = std::numeric_limits<int>::lowest();  // 兼容负数 key
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
        // 非根叶子必须满足下界（根叶子允许欠载）
        if (n->parent != nullptr &&
            static_cast<int>(n->keys.size()) < min_keys_) {
            err = "leaf node underfull";
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
    // 非根内部节点必须满足下界（根内部允许欠载，但不得为 0 个孩子）
    if (n->parent != nullptr &&
        static_cast<int>(n->children.size()) < min_children_) {
        err = "internal node underfull";
        return KeyRange{0, 0};
    }
    if (n->children.empty()) {
        err = "internal node has no children";
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

    if (static_cast<int>(parent->keys.size()) > max_keys_) {
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
