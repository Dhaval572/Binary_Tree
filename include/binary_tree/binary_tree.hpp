/**
 * @file binary_tree.hpp
 * @brief Generational-index binary tree with contiguous slot storage.
 *
 * Header-only C++20 library. Nodes live inline in one contiguous slot
 * array (no per-node heap allocation); node references are
 * index+generation handles validated on every use.
 *
 * @par Safety model
 * - Handles never dangle: slots are addressed by index, not pointer.
 * - Stale handles are detected, not dereferenced: every slot carries a
 *   generation counter; reuse bumps it, old handles fail validation.
 * - Cross-tree misuse is rejected: each handle embeds its tree id.
 * - Every access bounds-checks; every failure returns an error value.
 * - No exceptions from library logic, no silent failures, no copying.
 *
 * @see binary_tree::BinaryTree
 */

#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace binary_tree
{
    /**
     * @brief Error codes returned by tree operations.
     *
     * Every mutating operation reports failure through this enum
     * instead of throwing or asserting, so callers cannot silently
     * ignore a rejected request.
     */
    enum class TreeError
    {
        RootAlreadyExists, ///< EmplaceRoot() called on a tree that already has
                           ///< a root.
        InvalidParent, ///< Parent handle is stale, default-constructed, or from
                       ///< another tree.
        ChildAlreadyExists ///< Requested child slot on the parent is already
                           ///< occupied.
    };

    /**
     * @brief Result of an operation that produces a value of type @p T.
     *
     * Holds either the value (on success) or a ::TreeError (on failure).
     */
    template <typename T> using TreeResult = std::variant<T, TreeError>;

    /**
     * @brief Result of a mutation that produces no value.
     *
     * Holds `std::monostate` on success or a ::TreeError on failure.
     * Check with `std::holds_alternative<std::monostate>(status)`.
     */
    using TreeStatus = std::variant<std::monostate, TreeError>;

    namespace detail
    {
        using index_type = std::uint32_t;

        inline constexpr index_type InvalidIndex =
            std::numeric_limits<index_type>::max();

        template <typename T> struct Node
        {
            T data;

            index_type left{InvalidIndex};
            index_type right{InvalidIndex};

            template <typename... Args>
                requires std::constructible_from<T, Args...>
            explicit Node(Args &&...args) : data(std::forward<Args>(args)...)
            {
            }
        };

    } // namespace detail

    /**
     * @brief Binary tree with generational handles over contiguous storage.
     *
     * Nodes are stored inline in a single growable slot array — no
     * per-node allocation, cache-friendly traversal. Node references are
     * lightweight handles (index + generation + tree id) instead of raw
     * pointers, which fixes the classic pointer-based safety problems:
     *
     * @par Safety problems solved
     * | Problem | Mechanism |
     * |---|---|
     * | Dangling node pointer after storage growth | Handles address slots by
     * index; growth moves storage, handles stay valid | | Use of a handle to a
     * freed/recycled node | Per-slot generation counter; reuse bumps it, stale
     * handle fails IsValid() | | Handle used on the wrong tree | Handle embeds
     * tree id; mismatch rejected | | Out-of-bounds slot access | Bounds check
     * on every resolve | | Silently ignored failed insertion | Every mutation
     * returns ::TreeStatus with a ::TreeError | | Accidental deep copy / double
     * free | Copy construction and copy assignment deleted; move only | |
     * Dangling pointer from Value() | Contract documented: pointers die on
     * growth; handles never do; see Reserve() |
     *
     * @par Requirements on T
     * - Move- or copy-insertable (slot growth relocates nodes).
     * - Move-only types (e.g. `std::unique_ptr<int>`) work.
     * - No default-constructible requirement.
     *
     * @par Thread safety
     * - Concurrent const operations (traversal, Value(), IsValid()) on the
     *   same tree are safe: they touch no mutable state.
     * - Any mutation requires external synchronization with all other
     *   access to that tree.
     *
     * @par Exception safety
     * - Operations do not throw library errors; only `T` construction or
     *   container allocation may throw (`std::bad_alloc`).
     * - Move construction and move assignment are `noexcept`.
     *
     * @par Performance (measured)
     * Handles are validated on every use; this is the library's core
     * trade-off. Measured on an Intel Core i3 (Fedora 44, Clang 22,
     * Google Benchmark, 10 repetitions, median):
     *
     * | Operation                       | Cost          |
     * |---------------------------------|---------------|
     * | `Value()` (validated deref)     | ~4 ns         |
     * | `IsValid()` (full check)        | ~4.5 ns       |
     * | Full walk (2 checks per node)   | ~5 ns/node    |
     * | Insertion (≤4096 nodes)         | ~16 ns/node   |
     * | Insertion (>4096 nodes, L2)     | ~30 ns/node   |
     *
     * - Insertion throughput drops ~2x above ~4096 nodes on this CPU
     *   (256 KB L2 boundary for `m_Slots + m_Generations`). Call
     *   Reserve() up front to hide the realloc cost on top of it.
     * - For repeated reads without intervening insertion, cache
     *   `Value()` pointers after Reserve() — re-calling Value() per
     *   iteration re-validates the handle each time.
     *
     * @par Usage rules (what to avoid)
     * - Check every handle from Root(), Left(), and Right() before use:
     *   a null handle means empty tree, missing child, or invalid parent,
     *   and later calls with it fail silently.
     * - Check every Emplace* result; ignoring the status variant hides
     *   failed insertions.
     * - Re-fetch Value() pointers after any insertion, or Reserve() first
     *   to keep them stable.
     * - Never mutate while other threads read; never persist handles
     *   across processes or runs.
     *
     * Full rules with rationale: @ref pitfalls on the main page.
     *
     * @tparam T Element type stored in nodes.
     */
    template <typename T> class BinaryTree
    {
      public:
        template <bool b_IsConst> class BasicNodeHandle;

      private:
        using node_type  = detail::Node<T>;
        using index_type = detail::index_type;

        static constexpr index_type InvalidIndex = detail::InvalidIndex;

        // Contiguous node storage: nodes live inline in m_Slots.
        // m_Generations runs parallel to m_Slots (1:1) so traversal
        // lines carry pure node data.
        std::vector<std::optional<node_type>> m_Slots{};
        std::vector<std::uint64_t> m_Generations{};
        std::vector<index_type> m_FreeSlots{};

        index_type m_Root{InvalidIndex};
        std::size_t m_Size{};

        std::uint64_t m_TreeId{NextTreeId()};

        inline static std::atomic<std::uint64_t> s_NextTreeId{1};

        [[nodiscard]]
        static std::uint64_t NextTreeId() noexcept
        {
            return s_NextTreeId.fetch_add(1, std::memory_order_relaxed);
        }

        // Node registry
        template <typename... Args>
            requires std::constructible_from<T, Args...>
        index_type AllocateNode(Args &&...args)
        {
            if (!m_FreeSlots.empty())
            {
                const index_type index = m_FreeSlots.back();
                m_FreeSlots.pop_back();
                m_Slots[index].emplace(std::forward<Args>(args)...);

                return index;
            }

            const index_type index = static_cast<index_type>(m_Slots.size());

            m_Slots.emplace_back();
            m_Generations.push_back(1);

            m_Slots.back().emplace(std::forward<Args>(args)...);

            return index;
        }

        // Prepared for future node deletion and slot reuse.
        void ReleaseSlot(index_type index)
        {
            m_Slots[index].reset();

            // Never allow generation wraparound.
            // A retired slot will not be reused.
            if (m_Generations[index] ==
                std::numeric_limits<std::uint64_t>::max())
            {
                return;
            }

            ++m_Generations[index];
            m_FreeSlots.push_back(index);
            ++m_Size;
        }

        [[nodiscard]]
        node_type *Resolve(index_type index) noexcept
        {
            if (index >= m_Slots.size())
                return nullptr;

            auto &slot = m_Slots[index];

            if (!slot)
                return nullptr;

            return &*slot;
        }

        [[nodiscard]]
        const node_type *Resolve(index_type index) const noexcept
        {
            if (index >= m_Slots.size())
                return nullptr;

            const auto &slot = m_Slots[index];

            if (!slot)
                return nullptr;

            return &*slot;
        }

        template <bool b_IsConst>
        [[nodiscard]]
        bool
        IsValidHandle(const BasicNodeHandle<b_IsConst> &handle) const noexcept
        {
            if (!handle)
                return false;

            if (handle.m_TreeId != m_TreeId)
                return false;

            if (handle.m_Index >= m_Slots.size())
                return false;

            return m_Slots[handle.m_Index].has_value() &&
                   m_Generations[handle.m_Index] == handle.m_Generation;
        }

        // Traversal
        template <typename Func>
        void InOrderImpl(index_type index, Func &func) const
        {
            const node_type *node = Resolve(index);

            if (!node)
                return;

            InOrderImpl(node->left, func);
            std::invoke(func, std::as_const(node->data));
            InOrderImpl(node->right, func);
        }

        template <typename Func>
        void PreOrderImpl(index_type index, Func &func) const
        {
            const node_type *node = Resolve(index);

            if (!node)
                return;

            std::invoke(func, std::as_const(node->data));
            PreOrderImpl(node->left, func);
            PreOrderImpl(node->right, func);
        }

        template <typename Func>
        void PostOrderImpl(index_type index, Func &func) const
        {
            const node_type *node = Resolve(index);

            if (!node)
                return;

            PostOrderImpl(node->left, func);
            PostOrderImpl(node->right, func);
            std::invoke(func, std::as_const(node->data));
        }

      public:
        /** @brief Type stored in each node. */
        using value_type = T;

        /** @brief Unsigned type used for sizes and capacities. */
        using size_type = std::size_t;

        /**
         * @brief Lightweight validated reference to a tree node.
         *
         * Stores slot index, slot generation, and owning tree id. Never
         * dangles: copying, storage growth, and tree moves preserve it.
         * Validate with BinaryTree::IsValid() (or tree lookups, which
         * validate internally and return a null handle on failure).
         *
         * @warning Handles are process-local: tree ids come from an
         * in-process counter. Never persist, serialize, or send handles
         * across processes or program runs — re-issue them instead.
         *
         * @tparam b_IsConst `true` for read-only handles, `false` for
         *         mutable handles.
         */
        template <bool b_IsConst> class BasicNodeHandle
        {
            friend class BinaryTree;

          private:
            index_type m_Index{InvalidIndex};
            std::uint64_t m_Generation{};
            std::uint64_t m_TreeId{};

            explicit BasicNodeHandle(index_type index, std::uint64_t generation,
                                     std::uint64_t tree_id) noexcept
                : m_Index(index), m_Generation(generation), m_TreeId(tree_id)
            {
            }

          public:
            /** @brief Constructs a null (invalid) handle. */
            BasicNodeHandle() noexcept = default;

            /**
             * @brief Checks whether the handle is non-null.
             *
             * Contextual boolean conversion, e.g. `if (handle)`.
             * Only proves the handle was constructed from a real node —
             * it does not re-validate against a tree. Use
             * BinaryTree::IsValid() for full validation.
             *
             * @return `true` if the handle references some node.
             */
            [[nodiscard]]
            explicit operator bool() const noexcept
            {
                return m_Index != InvalidIndex && m_TreeId != 0;
            }
        };

        /** @brief Mutable handle: allows value writes through Value(). */
        using NodeHandle = BasicNodeHandle<false>;

        /** @brief Read-only handle: Value() yields `const T*`. */
        using ConstNodeHandle = BasicNodeHandle<true>;

      private:
        /**
         * @brief Shared body for the three Value() overloads.
         *
         * `Self` carries the const-ness of the calling tree; the
         * trailing return type is `T*` for a non-const tree and
         * `const T*` for a const tree, so all three public overloads
         * route through this one body with no `const_cast`.
         */
        template <typename Handle, typename Self>
        [[nodiscard]]
        static auto ValueImpl(Self *self, Handle handle) noexcept
            -> decltype(&self->Resolve(handle.m_Index)->data)
        {
            if (!self->IsValid(handle))
                return nullptr;

            return &self->Resolve(handle.m_Index)->data;
        }

        /**
         * @brief Shared body for the six Left()/Right() overloads.
         *
         * `Self` carries the const-ness of the calling tree; the return
         * type is NodeHandle for a non-const tree and ConstNodeHandle
         * for a const tree. `child` selects the left or right member.
         */
        template <typename Handle, typename Self>
        [[nodiscard]]
        static auto ChildImpl(Self *self, Handle parent,
                              index_type node_type::*child) noexcept
            -> BasicNodeHandle<std::is_const_v<Self>>
        {
            using handle_type = BasicNodeHandle<std::is_const_v<Self>>;

            if (!self->IsValid(parent))
                return {};

            const index_type index = self->Resolve(parent.m_Index)->*child;

            if (index == InvalidIndex)
                return {};

            return handle_type{index, self->m_Generations[index],
                               self->m_TreeId};
        }

        /**
         * @brief Shared body for EmplaceLeft()/EmplaceRight().
         *
         * Validates the parent, checks the chosen child slot is empty,
         * allocates the node, then links it. The re-Resolve after
         * AllocateNode is required because AllocateNode may reallocate
         * m_Slots. Allocate-then-commit ordering keeps the tree
         * unchanged if AllocateNode throws.
         */
        template <typename... Args>
            requires std::constructible_from<T, Args...>
        TreeStatus EmplaceChild(NodeHandle parent, index_type node_type::*child,
                                Args &&...args)
        {
            if (!IsValid(parent))
                return TreeError::InvalidParent;

            if (Resolve(parent.m_Index)->*child != InvalidIndex)
                return TreeError::ChildAlreadyExists;

            const index_type idx = AllocateNode(std::forward<Args>(args)...);

            // Resolve again because AllocateNode may reallocate m_Slots.
            Resolve(parent.m_Index)->*child = idx;
            ++m_Size;

            return std::monostate{};
        }

      public:
        /**
         * @brief Fully validates a handle against this tree.
         *
         * Checks, in order: handle non-null, tree id matches this tree,
         * index in range, slot occupied, generation matches. Catches
         * handles to other trees, default handles, and stale handles
         * whose slot was recycled.
         *
         * @param handle Handle to validate.
         * @return `true` if the handle safely refers to a live node.
         */
        [[nodiscard]]
        bool IsValid(NodeHandle handle) const noexcept
        {
            return IsValidHandle(handle);
        }

        /**
         * @brief Fully validates a const handle against this tree.
         *
         * @param handle Handle to validate.
         * @return `true` if the handle safely refers to a live node.
         */
        [[nodiscard]]
        bool IsValid(ConstNodeHandle handle) const noexcept
        {
            return IsValidHandle(handle);
        }

        /// @name Construction and assignment
        /// @{

        /** @brief Constructs an empty tree (no nodes). */
        BinaryTree() noexcept = default;

        /**
         * @brief Copying is disabled.
         *
         * Trees are move-only so ownership is always unambiguous —
         * eliminates double-free and copy-while-iterating class of bugs.
         * Duplicate content by rebuilding or by sharing via reference.
         */
        BinaryTree(const BinaryTree &) = delete;

        /** @brief Copy assignment is disabled. See copy constructor. */
        BinaryTree &operator=(const BinaryTree &) = delete;

        /**
         * @brief Move-constructs a tree in `noexcept` time.
         *
         * @param other Tree to steal from; becomes empty.
         *
         * @par Handle behavior
         * Handles previously issued by @p other remain valid and now
         * resolve inside this tree (tree id travels with the data).
         * Handles to this tree's previous content (if any was replaced)
         * are not valid afterwards.
         */
        BinaryTree(BinaryTree &&other) noexcept
            : m_Slots(std::move(other.m_Slots)),
              m_Generations(std::move(other.m_Generations)),
              m_FreeSlots(std::move(other.m_FreeSlots)),
              m_Root(std::exchange(other.m_Root, InvalidIndex)),
              m_Size(std::exchange(other.m_Size, 0)),
              m_TreeId(std::exchange(other.m_TreeId, NextTreeId()))
        {
        }

        /**
         * @brief Move-assigns a tree in `noexcept` time.
         *
         * @param other Tree to steal from; becomes empty.
         * @return Reference to `*this`.
         *
         * Self-move is a no-op. Same handle behavior as the move
         * constructor.
         */
        BinaryTree &operator=(BinaryTree &&other) noexcept
        {
            if (this != &other)
            {
                m_Slots       = std::move(other.m_Slots);
                m_Generations = std::move(other.m_Generations);
                m_FreeSlots   = std::move(other.m_FreeSlots);

                m_Root = std::exchange(other.m_Root, InvalidIndex);

                m_Size = std::exchange(other.m_Size, 0);

                m_TreeId = std::exchange(other.m_TreeId, NextTreeId());
            }

            return *this;
        }

        /// @}

        /// @name Insertion
        /// @{

        /**
         * @brief Creates the root node of an empty tree.
         *
         * @param args Arguments forwarded to the constructor of @p T.
         * @return `std::monostate` on success,
         *         TreeError::RootAlreadyExists if a root exists.
         * @exception std::bad_alloc Allocation failed or @p T's
         *            constructor threw; the tree is unchanged.
         * @warning Never ignore the result — a dropped status hides
         * RootAlreadyExists and the tree simply keeps no root.
         */
        template <typename... Args>
            requires std::constructible_from<T, Args...>
        TreeStatus EmplaceRoot(Args &&...args)
        {
            if (m_Root != InvalidIndex)
                return TreeError::RootAlreadyExists;

            // Assign m_Root and bump m_Size only after AllocateNode
            // returns: if it throws, both stay unchanged and the tree
            // remains consistent.
            const index_type idx = AllocateNode(std::forward<Args>(args)...);
            m_Root               = idx;
            ++m_Size;
            return std::monostate{};
        }

        /**
         * @brief Emplaces a node as the left child of @p parent.
         *
         * @param parent Valid handle of a node in @b this tree whose left
         *               slot is empty.
         * @param args   Arguments forwarded to the constructor of @p T.
         * @return `std::monostate` on success, otherwise
         *         TreeError::InvalidParent (stale/foreign/null handle) or
         *         TreeError::ChildAlreadyExists (left slot occupied).
         * @exception std::bad_alloc Allocation failed or @p T's
         *            constructor threw; the tree is unchanged.
         * @warning Never ignore the result — a dropped status hides
         * failed insertions and your logic continues on a missing node.
         *
         * Storage may grow here; all existing handles stay valid,
         * pointers from Value() are invalidated (see Value()).
         */
        template <typename... Args>
            requires std::constructible_from<T, Args...>
        TreeStatus EmplaceLeft(NodeHandle parent, Args &&...args)
        {
            return EmplaceChild(parent, &node_type::left,
                                std::forward<Args>(args)...);
        }

        /**
         * @brief Emplaces a node as the right child of @p parent.
         *
         * @param parent Valid handle of a node in @b this tree whose right
         *               slot is empty.
         * @param args   Arguments forwarded to the constructor of @p T.
         * @return `std::monostate` on success, otherwise
         *         TreeError::InvalidParent or TreeError::ChildAlreadyExists.
         * @exception std::bad_alloc Allocation failed or @p T's
         *            constructor threw; the tree is unchanged.
         * @warning Never ignore the result — a dropped status hides
         * failed insertions and your logic continues on a missing node.
         *
         * Storage may grow here; all existing handles stay valid,
         * pointers from Value() are invalidated (see Value()).
         */
        template <typename... Args>
            requires std::constructible_from<T, Args...>
        TreeStatus EmplaceRight(NodeHandle parent, Args &&...args)
        {
            return EmplaceChild(parent, &node_type::right,
                                std::forward<Args>(args)...);
        }

        /// @}

        /// @name Node traversal handles
        /// @{

        /**
         * @brief Handle to the root node.
         * @return Valid handle, or a null handle if the tree is empty.
         * @warning Check before use: a null handle means the tree is
         * empty. Using it unchecked makes later calls fail silently.
         */
        [[nodiscard]]
        NodeHandle Root() noexcept
        {
            if (m_Root == InvalidIndex)
                return {};

            return NodeHandle{m_Root, m_Generations[m_Root], m_TreeId};
        }

        /**
         * @brief Handle to the root node (const tree).
         * @return Valid handle, or a null handle if the tree is empty.
         * @warning Check before use: a null handle means the tree is
         * empty. Using it unchecked makes later calls fail silently.
         */
        [[nodiscard]]
        ConstNodeHandle Root() const noexcept
        {
            if (m_Root == InvalidIndex)
                return {};

            return ConstNodeHandle{m_Root, m_Generations[m_Root], m_TreeId};
        }

        /**
         * @brief Handle to the left child of @p parent.
         *
         * @param parent Parent node handle.
         * @return Child handle, or a null handle if @p parent is invalid
         *        or has no left child. Validation happens internally —
         *        a stale or foreign parent can never cause undefined
         *        behavior here.
         * @warning Check before use: null means invalid @p parent or
         * missing child. An unchecked handle makes later calls fail
         * silently instead of crashing.
         */
        [[nodiscard]]
        NodeHandle Left(NodeHandle parent) noexcept
        {
            return ChildImpl(this, parent, &node_type::left);
        }

        /**
         * @brief Handle to the left child of @p parent (const handles).
         *
         * @param parent Parent node handle.
         * @return Child handle, or a null handle if unavailable.
         * @warning Check before use: null means invalid @p parent or
         * missing child. An unchecked handle makes later calls fail
         * silently instead of crashing.
         */
        [[nodiscard]]
        ConstNodeHandle Left(ConstNodeHandle parent) const noexcept
        {
            return ChildImpl(this, parent, &node_type::left);
        }

        /**
         * @brief Handle to the left child of @p parent (const tree).
         *
         * @param parent Parent node handle.
         * @return Child handle, or a null handle if unavailable.
         * @warning Check before use: null means invalid @p parent or
         * missing child. An unchecked handle makes later calls fail
         * silently instead of crashing.
         */
        [[nodiscard]]
        ConstNodeHandle Left(NodeHandle parent) const noexcept
        {
            return ChildImpl(this, parent, &node_type::left);
        }

        /**
         * @brief Handle to the right child of @p parent.
         *
         * @param parent Parent node handle.
         * @return Child handle, or a null handle if @p parent is invalid
         *        or has no right child.
         * @warning Check before use: null means invalid @p parent or
         * missing child. An unchecked handle makes later calls fail
         * silently instead of crashing.
         */
        [[nodiscard]]
        NodeHandle Right(NodeHandle parent) noexcept
        {
            return ChildImpl(this, parent, &node_type::right);
        }

        /**
         * @brief Handle to the right child of @p parent (const handles).
         *
         * @param parent Parent node handle.
         * @return Child handle, or a null handle if unavailable.
         * @warning Check before use: null means invalid @p parent or
         * missing child. An unchecked handle makes later calls fail
         * silently instead of crashing.
         */
        [[nodiscard]]
        ConstNodeHandle Right(ConstNodeHandle parent) const noexcept
        {
            return ChildImpl(this, parent, &node_type::right);
        }

        /**
         * @brief Handle to the right child of @p parent (const tree).
         *
         * @param parent Parent node handle.
         * @return Child handle, or a null handle if unavailable.
         * @warning Check before use: null means invalid @p parent or
         * missing child. An unchecked handle makes later calls fail
         * silently instead of crashing.
         */
        [[nodiscard]]
        ConstNodeHandle Right(NodeHandle parent) const noexcept
        {
            return ChildImpl(this, parent, &node_type::right);
        }

        /// @}

        /// @name Value access
        /// @{

        /**
         * @brief Pointer to the value of the referenced node.
         *
         * @param handle Node handle (validated internally).
         * @return Pointer to the stored @p T, or `nullptr` if the handle
         *         is stale, foreign, or null. A null return is always
         *         safe — invalid handles never cause undefined behavior.
         *
         * @warning Lifetime contract: the returned pointer is invalidated
         * by any later successful insertion that grows storage (exactly
         * `std::vector` reallocation rules). Handles are never invalidated
         * by insertion — re-call Value() after mutations, or call
         * Reserve() up front to keep pointers stable.
         */
        [[nodiscard]]
        T *Value(NodeHandle handle) noexcept
        {
            return ValueImpl(this, handle);
        }

        /**
         * @brief Pointer to the value of the referenced node (const tree).
         *
         * @param handle Node handle.
         * @return Pointer to the value, or `nullptr` if invalid.
         * @warning Same pointer-invalidation contract as the non-const
         *          overload.
         */
        [[nodiscard]]
        const T *Value(ConstNodeHandle handle) const noexcept
        {
            return ValueImpl(this, handle);
        }

        /**
         * @brief Pointer to the value of the referenced node (const tree,
         *        mutable handle).
         *
         * @param handle Node handle.
         * @return `const` pointer to the value, or `nullptr` if invalid.
         * @warning Same pointer-invalidation contract as the non-const
         *          overload.
         */
        [[nodiscard]]
        const T *Value(NodeHandle handle) const noexcept
        {
            return ValueImpl(this, handle);
        }

        /// @}

        /// @name Traversal
        /// @{

        /**
         * @brief Visits every node left-root-right (sorted order for
         *        search trees).
         *
         * @param func Callable invoked as `func(const T&)` per node.
         *        Receives values by const reference; collect them or
         *        fold them into state.
         *
         * Empty tree: no calls. Recursive implementation — extremely
         * deep degenerate chains consume one stack frame per level.
         */
        template <typename Func>
            requires std::invocable<Func &, const T &>
        void InOrder(Func &&func) const
        {
            InOrderImpl(m_Root, func);
        }

        /**
         * @brief Visits every node root-left-right (prefix order).
         *
         * @param func Callable invoked as `func(const T&)` per node.
         */
        template <typename Func>
            requires std::invocable<Func &, const T &>
        void PreOrder(Func &&func) const
        {
            PreOrderImpl(m_Root, func);
        }

        /**
         * @brief Visits every node left-right-root (postfix order).
         *
         * @param func Callable invoked as `func(const T&)` per node.
         *         Natural order for destruction-time sweeps and
         *         post-order serialization.
         */
        template <typename Func>
            requires std::invocable<Func &, const T &>
        void PostOrder(Func &&func) const
        {
            PostOrderImpl(m_Root, func);
        }

        /// @}

        /// @name Capacity
        /// @{

        /**
         * @brief Pre-allocates storage for @p count nodes.
         *
         * After Reserve(n) with `n >= final node count`, no insertion
         * reallocates — Value() pointers obtained inside that window
         * stay valid for the whole window.
         *
         * @par Performance
         * - Prevents reallocation across the whole insertion window,
         *   so `Value()` pointers stay valid.
         * - Above ~4096 nodes the pool's working set exceeds L2 on
         *   this class of CPU and insertion throughput drops; Reserve()
         *   doesn't remove that cliff but does remove the realloc
         *   copies on top of it.
         *
         * @param count Desired capacity in nodes.
         * @note Never shrinks: capacity may exceed @c Size() for the
         * tree's lifetime. Pointers fetched before Reserve() may die if
         * Reserve() itself reallocates — re-fetch once after it.
         */
        void Reserve(size_type count)
        {
            m_Slots.reserve(count);
            m_Generations.reserve(count);
            m_FreeSlots.reserve(count);
        }

        /**
         * @brief Number of nodes currently stored.
         * @return Node count; `0` for an empty tree.
         */
        [[nodiscard]]
        size_type Size() const noexcept
        {
            return m_Size;
        }

        /**
         * @brief Whether the tree has no nodes.
         * @return `true` if Size() == 0.
         */
        [[nodiscard]]
        bool Empty() const noexcept
        {
            return m_Size == 0;
        }

        /// @}
    };

} // namespace binary_tree
