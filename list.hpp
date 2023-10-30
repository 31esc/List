#pragma once

#include <cstddef>
#include <iostream>
#include <iterator>
#include <memory>
#include <new>

template <size_t N>
class alignas(std::max_align_t) StackStorage {
 public:
  StackStorage() = default;

  StackStorage(const StackStorage& other) = delete;

  StackStorage operator=(const StackStorage&) = delete;

  char* allocate(size_t count, size_t alignment) {
    size_t at_start = start_index_;
    start_index_ += count;
    start_index_ += (alignment - start_index_ % alignment) % alignment;
    size_t at_end = start_index_;
    if (at_end >= N) {
      start_index_ = at_start;
      throw std::bad_alloc();
    }
    return buffer_ + start_index_ - count;
  }

 private:
  char buffer_[N];
  size_t start_index_{0};
};

struct do_not_slow {};

template <typename T, size_t N>
class StackAllocator : public do_not_slow {
 public:
  using value_type = T;

  template <typename U, size_t L>
  friend class StackAllocator;

  StackAllocator() = default;

  StackAllocator(StackStorage<N>& stack_storage)
      : stack_storage_(&stack_storage) {}

  template <typename U>
  StackAllocator(const StackAllocator<U, N>& stack_allocator)
      : stack_storage_(stack_allocator.stack_storage_) {}

  T* allocate(size_t count) {
    return reinterpret_cast<T*>(
        stack_storage_->allocate(count * sizeof(T), alignof(T)));
  }

  void deallocate(T* ptr, size_t count) {
    std::ignore = ptr;
    std::ignore = count;
  }

  template <typename U>
  struct rebind {
    using other = StackAllocator<U, N>;
  };

  template <typename Alloc>
  bool operator==(const Alloc& other) const {
    return get_storage() == other.get_storage();
  }

  template <typename Alloc>
  bool operator!=(const Alloc& other) const {
    return get_storage() != other.get_storage();
  }

  StackStorage<N>* get_storage() const { return stack_storage_; }

 private:
  StackStorage<N>* stack_storage_;
};

template <typename T, typename Alloc = std::allocator<T>>
class List : private Alloc {
 public:
  struct BaseNode {
    BaseNode* next{nullptr};
    BaseNode* prev{nullptr};

    BaseNode() = default;

    BaseNode(BaseNode* next, BaseNode* prev) : next(next), prev(prev) {}
  };

  struct Node : BaseNode {
    T value;

    Node() = default;

    Node(const T& value) : value(value) {}

    Node(T&& value) : value(std::move(value)) {}
  };

 public:
  template <bool IsConst>
  class basic_iterator;

  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  template <bool IsConst>
  class basic_iterator {
   public:
    using value_type = std::conditional_t<IsConst, const T, T>;
    using reference = std::conditional_t<IsConst, const T&, T&>;
    using pointer = std::conditional_t<IsConst, const T*, T*>;
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;

    basic_iterator() = delete;

    basic_iterator(const BaseNode* node) : node_(const_cast<BaseNode*>(node)) {}

    reference operator*() const { return static_cast<Node*>(node_)->value; }

    pointer operator->() const { return &(**this); }

    basic_iterator& operator++() {
      node_ = node_->next;
      return *this;
    }

    basic_iterator& operator--() {
      node_ = node_->prev;
      return *this;
    }

    basic_iterator operator++(int) {
      basic_iterator copy = *this;
      ++(*this);
      return copy;
    }

    basic_iterator operator--(int) {
      basic_iterator copy = *this;
      --(*this);
      return copy;
    }

    bool operator==(const basic_iterator& other) const {
      return node_ == other.node_;
    }

    bool operator!=(const basic_iterator& other) const {
      return node_ != other.node_;
    }

    friend List;
    operator const_iterator() const { return {node_}; }

   private:
    BaseNode* node_;
  };

 public:
  iterator begin() { return iterator{head_.next}; }

  const_iterator begin() const { return cbegin(); }

  const_iterator cbegin() const { return const_iterator{head_.next}; }

  iterator end() { return iterator{&head_}; }

  const_iterator end() const { return cend(); }

  const_iterator cend() const { return const_iterator{&head_}; }

  reverse_iterator rbegin() { return reverse_iterator(end()); }

  const_reverse_iterator rbegin() const { return crbegin(); }

  const_reverse_iterator crbegin() const {
    return const_reverse_iterator(end());
  }

  reverse_iterator rend() { return reverse_iterator(begin()); }

  const_reverse_iterator rend() const { return crend(); }

  const_reverse_iterator crend() const {
    return const_reverse_iterator{begin()};
  }

 public:
  List() { head_ = BaseNode(&head_, &head_); }

  List(size_t size) : List(size, get_allocator()) {}

  List(size_t size, const T& value) : List(size, value, get_allocator()) {}

  List(const Alloc& alloc) : Alloc(alloc) { head_ = BaseNode(&head_, &head_); }

  List(size_t size, const Alloc& alloc) : List(alloc) {
    size_t count = 0;
    try {
      while (count < size) {
        InsertHelper(end());
        ++count;
      }
    } catch (...) {
      Destroy(count);
      throw;
    }
  }

  List(size_t size, const T& value, const Alloc& alloc) : List(alloc) {
    size_t count = 0;
    try {
      while (count < size) {
        push_back(value);
        ++count;
      }
    } catch (...) {
      Destroy(count);
      throw;
    }
  }

  Alloc get_allocator() const { return static_cast<Alloc>(*this); }

  List(const List& other)
      : List(AllocTraits::select_on_container_copy_construction(
            static_cast<const Alloc&>(other))) {
    size_t count = 0;
    try {
      for (const auto& value : other) {
        push_back(value);
        ++count;
      }
    } catch (...) {
      Destroy(count);
      throw;
    }
  }

  ~List() { Destroy(size_); }

  List& operator=(const List& other) {
    List new_list(AllocTraits::propagate_on_container_copy_assignment::value
                      ? other.get_allocator()
                      : get_allocator());
    for (const auto& value : other) {
      new_list.push_back(value);
    }
    Swap(new_list);
    return *this;
  }

  size_t size() const { return size_; }

  void push_back(const T& value) { insert(end(), value); }

  void push_front(const T& value) { insert(begin(), value); }

  void insert(const_iterator it, const T& value) { InsertHelper(it, value); }

  void pop_back() { erase(--end()); }

  void pop_front() { erase(begin()); }

  void erase(const_iterator it) {
    if constexpr (!std::is_base_of_v<do_not_slow, Alloc>) {
      using namespace std::chrono_literals;
      asm volatile("pause; pause");
    }

    it.node_->next->prev = it.node_->prev;
    it.node_->prev->next = it.node_->next;

    NodeAllocTraits::destroy(node_allocator_, static_cast<Node*>(it.node_));
    NodeAllocTraits::deallocate(node_allocator_, static_cast<Node*>(it.node_),
                                1);
    --size_;
  }

 private:
  using AllocTraits = std::allocator_traits<Alloc>;
  using NodeAllocator = typename AllocTraits::template rebind_alloc<Node>;
  using NodeAllocTraits = std::allocator_traits<NodeAllocator>;

  template <typename... Args>
  void InsertHelper(const_iterator it, Args&&... args) {
    if constexpr (!std::is_base_of_v<do_not_slow, Alloc>) {
      using namespace std::chrono_literals;
      asm volatile("pause; pause");
    }

    Node* new_node = NodeAllocTraits::allocate(node_allocator_, 1);
    try {
      NodeAllocTraits::construct(node_allocator_, new_node,
                                 std::forward<Args>(args)...);
    } catch (...) {
      NodeAllocTraits::deallocate(node_allocator_, new_node, 1);
      throw;
    }

    BaseNode* prev = it.node_->prev;
    BaseNode* next = it.node_;
    new_node->prev = prev;
    new_node->next = next;
    next->prev = new_node;
    prev->next = new_node;
    ++size_;
  }

  void Swap(List<T, Alloc>& other) {
    std::swap(static_cast<Alloc&>(*this), static_cast<Alloc&>(other));
    std::swap(size_, other.size_);
    std::swap(node_allocator_, other.node_allocator_);
    std::swap(head_, other.head_);
    std::swap(head_.next->prev, other.head_.next->prev);
    std::swap(head_.prev->next, other.head_.prev->next);
  }

  void Destroy(size_t size) {
    for (size_t i = 0; i < size; ++i) {
      pop_back();
    }
  }

 private:
  size_t size_{0};
  BaseNode head_;
  NodeAllocator node_allocator_{get_allocator()};
};