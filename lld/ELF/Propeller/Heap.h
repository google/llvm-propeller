#ifndef LLD_ELF_HEAP_H
#define LLD_ELF_HEAP_H
#include "llvm/ADT/DenseMap.h"
#include <algorithm>
#include <assert.h>
#include <functional>
#include <memory>
#include <string.h>
#include <vector>

template <class K, class V, class CmpK, class CmpV> class HeapNode {
public:
  K key;
  V value;
  HeapNode<K, V, CmpK, CmpV> *parent;
  std::vector<HeapNode<K, V, CmpK, CmpV> *> children;

  HeapNode(K k, V v)
      : key(k), value(std::move(v)), parent(nullptr), children(2, nullptr) {}

  HeapNode<K, V, CmpK, CmpV> *leftChild() { return children.at(0); }

  HeapNode<K, V, CmpK, CmpV> *rightChild() { return children.at(1); }

  bool isLeftChild() {
    if (parent)
      return parent->leftChild() == this;
    return false;
  }

  bool isRightChild() {
    if (parent)
      return parent->rightChild() == this;
    return false;
  }

  void adoptLeftChild(HeapNode<K, V, CmpK, CmpV> *c) {
    children.at(0) = c;
    if (c)
      c->parent = this;
  }

  void adoptRightChild(HeapNode<K, V, CmpK, CmpV> *c) {
    children.at(1) = c;
    if (c)
      c->parent = this;
  }

  void
  adoptChildren(const std::vector<HeapNode<K, V, CmpK, CmpV> *> &_children) {
    adoptLeftChild(_children.at(0));
    adoptRightChild(_children.at(1));
  }

  std::string toString(unsigned level) {
    std::string str = std::string(level, ' ');
    str += "NODE: " + std::to_string(key) + " -> " + std::to_string(value);
    for (auto *c : children) {
      if (c) {
        str += "\n";
        str += c->toString(level + 1);
      }
    }
    return str;
  }
};

template <class K, class V, class CmpK, class CmpV> struct CompareHeapNode {
  CmpK KeyComparator;
  CmpV ValueComparator;
  bool operator()(const HeapNode<K, V, CmpK, CmpV> &n1,
                  const HeapNode<K, V, CmpK, CmpV> &n2) const {
    if (ValueComparator(n1.value, n2.value))
      return true;
    if (ValueComparator(n2.value, n1.value))
      return false;
    return KeyComparator(n1.key, n2.key);
  }
};

template <class K, class V, class CmpK = std::less<K>,
          class CmpV = std::less<V>>
class Heap {
private:
  CompareHeapNode<K, V, CmpK, CmpV> HeapNodeComparator;

  llvm::DenseMap<K, std::unique_ptr<HeapNode<K, V, CmpK, CmpV>>> nodes;
  HeapNode<K, V, CmpK, CmpV> *root = nullptr;
  unsigned Size = 0;

  void assignRoot(HeapNode<K, V, CmpK, CmpV> *node) {
    root = node;
    if (root)
      root->parent = nullptr;
  }

  HeapNode<K, V, CmpK, CmpV> *getNodeWithHandle(unsigned handle) {
    return getNodeWithHandleHelper(root, handle);
  }

  HeapNode<K, V, CmpK, CmpV> *
  getNodeWithHandleHelper(HeapNode<K, V, CmpK, CmpV> *node, unsigned handle) {
    if (handle == 1)
      return node;
    auto *p = getNodeWithHandleHelper(node, handle >> 1);
    return (handle & 1) ? p->rightChild() : p->leftChild();
  }

  void insert(HeapNode<K, V, CmpK, CmpV> *node) {
    if (!root) {
      assignRoot(node);
    } else {
      unsigned handle = Size + 1;
      auto *p = getNodeWithHandleHelper(root, handle >> 1);
      if (handle & 1)
        p->adoptRightChild(node);
      else
        p->adoptLeftChild(node);
      heapifyUp(node);
    }
    Size++;
  }

  V remove(HeapNode<K, V, CmpK, CmpV> *node) {
    auto *last = getNodeWithHandleHelper(root, Size);
    assert(last->leftChild() == nullptr && last->rightChild() == nullptr);

    if (last->parent) {
      if (last->isLeftChild())
        last->parent->adoptLeftChild(nullptr);
      else
        last->parent->adoptRightChild(nullptr);
    }

    if (node != last) {
      if (node->parent) {
        if (node->isLeftChild())
          node->parent->adoptLeftChild(last);
        else
          node->parent->adoptRightChild(last);
      } else {
        assert(node == root);
        assignRoot(last);
      }
      last->adoptChildren(node->children);
      heapifyUp(last);
      heapifyDown(last);
    } else if (node->parent == nullptr) {
      assert(node == root);
      assignRoot(nullptr);
    }
    Size--;
    auto nodeElement = std::move(node->value);
    nodes.erase(node->key);
    return nodeElement;
  }

  void heapifyUp(HeapNode<K, V, CmpK, CmpV> *node) {
    if (node->parent && HeapNodeComparator(*node->parent, *node)) {
      swapWithParent(node);
      heapifyUp(node);
    }
  }

  void heapifyDown(HeapNode<K, V, CmpK, CmpV> *node) {
    if (node->children.empty())
      return;
    auto maxChild = std::max_element(
        node->children.begin(), node->children.end(),
        [this](const HeapNode<K, V, CmpK, CmpV> *c1,
               const HeapNode<K, V, CmpK, CmpV> *c2) {
          return c1 == nullptr
                     ? true
                     : (c2 == nullptr ? false : HeapNodeComparator(*c1, *c2));
        });
    if (*maxChild != nullptr && HeapNodeComparator(*node, **maxChild)) {
      swapWithParent(*maxChild);
      heapifyDown(node);
    }
  }

  void swapWithParent(HeapNode<K, V, CmpK, CmpV> *node) {
    auto *par = node->parent;
    assert(par);
    auto *gpar = node->parent->parent;
    if (!gpar) {
      assert(this->root == par);
      this->assignRoot(node);
    } else {
      if (par->isLeftChild())
        gpar->adoptLeftChild(node);
      else
        gpar->adoptRightChild(node);
    }
    auto *par_old_left = par->leftChild();
    auto *par_old_right = par->rightChild();

    par->adoptChildren(node->children);
    if (par_old_left == node)
      node->adoptChildren(
          std::vector<HeapNode<K, V, CmpK, CmpV> *>({par, par_old_right}));
    else
      node->adoptChildren(
          std::vector<HeapNode<K, V, CmpK, CmpV> *>({par_old_left, par}));
  }

public:
  void insert(K key, V value) {
    auto cur = nodes.find(key);
    if (cur != nodes.end()) {
      cur->second->value = std::move(value);
      heapifyUp(cur->second.get());
      heapifyDown(cur->second.get());
    } else {
      auto *node = new HeapNode<K, V, CmpK, CmpV>(key, std::move(value));
      insert(node);
      nodes.try_emplace(key, node);
    }
  }

  void erase(K k) {
    auto cur = nodes.find(k);
    if (cur != nodes.end())
      remove(cur->second.get());
  }

  HeapNode<K, V, CmpK, CmpV> *get(K k) {
    auto it = nodes.find(k);
    return it == nodes.end() ? nullptr : it->second.get();
  }

  void pop() {
    assert(!empty());
    remove(root);
  }

  V top() { return std::move(root->value); }

  bool empty() { return Size == 0; }

  unsigned size() { return Size; };

  std::string toString() {
    std::string str;
    str += "HEAP with ";
    str += std::to_string(Size);
    str += " nodes\n";
    if (root)
      str += root->toString(0);
    return str;
  }
};

#endif
