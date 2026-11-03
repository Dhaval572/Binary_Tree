#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "binary_tree/binary_tree.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using binary_tree::BinaryTree;
using binary_tree::TreeError;

namespace
{
    // Test payload whose (int) constructor throws when armed.
    // Copy and move are defaulted; move is noexcept so vector
    // reallocation during insertion never throws.
    struct ThrowingValue
    {
        static inline bool armed = false;
        int value;

        explicit ThrowingValue(int v) : value(v)
        {
            if (armed)
                throw std::runtime_error("ThrowingValue");
        }

        ThrowingValue(const ThrowingValue &)     = default;
        ThrowingValue(ThrowingValue &&) noexcept = default;
    };
} // namespace

// Compile-time API checks
static_assert(std::is_default_constructible_v<BinaryTree<int>>);

static_assert(!std::is_copy_constructible_v<BinaryTree<int>>);
static_assert(!std::is_copy_assignable_v<BinaryTree<int>>);

static_assert(std::is_move_constructible_v<BinaryTree<int>>);
static_assert(std::is_move_assignable_v<BinaryTree<int>>);

static_assert(std::is_default_constructible_v<BinaryTree<int>::NodeHandle>);

static_assert(
    std::is_default_constructible_v<BinaryTree<int>::ConstNodeHandle>);

static_assert(std::same_as<decltype(std::declval<BinaryTree<int> &>().Value(
                               std::declval<BinaryTree<int>::NodeHandle>())),
                           int *>);

static_assert(
    std::same_as<decltype(std::declval<const BinaryTree<int> &>().Value(
                     std::declval<BinaryTree<int>::NodeHandle>())),
                 const int *>);

static_assert(
    std::same_as<decltype(std::declval<const BinaryTree<int> &>().Value(
                     std::declval<BinaryTree<int>::ConstNodeHandle>())),
                 const int *>);

static_assert(!std::is_convertible_v<BinaryTree<int>::NodeHandle,
                                     BinaryTree<int>::ConstNodeHandle>);

// Tree must be instantiable with types whose move is noexcept
static_assert(std::is_move_constructible_v<BinaryTree<ThrowingValue>>);

TEST_SUITE("BinaryTree")
{
    // Construction
    TEST_CASE("Default construction")
    {
        BinaryTree<int> tree;

        CHECK(tree.Empty());
        CHECK(tree.Size() == 0);

        CHECK_FALSE(tree.Root());
        CHECK_FALSE(tree.IsValid(tree.Root()));
    }

    // Root insertion
    TEST_CASE("EmplaceRoot")
    {
        BinaryTree<int> tree;

        auto result = tree.EmplaceRoot(10);

        CHECK(std::holds_alternative<std::monostate>(result));

        CHECK_FALSE(tree.Empty());
        CHECK(tree.Size() == 1);

        auto root = tree.Root();

        REQUIRE(root);
        CHECK(tree.IsValid(root));

        auto *value = tree.Value(root);

        REQUIRE(value != nullptr);
        CHECK(*value == 10);
    }

    TEST_CASE("Root insertion rejects an existing root")
    {
        BinaryTree<int> tree;

        tree.EmplaceRoot(10);

        auto result = tree.EmplaceRoot(20);

        REQUIRE(std::holds_alternative<TreeError>(result));

        CHECK(std::get<TreeError>(result) == TreeError::RootAlreadyExists);

        auto root = tree.Root();

        REQUIRE(root);

        auto *value = tree.Value(root);

        REQUIRE(value != nullptr);
        CHECK(*value == 10);

        CHECK(tree.Size() == 1);
    }

    // Child insertion
    TEST_CASE("EmplaceLeft and EmplaceRight")
    {
        BinaryTree<int> tree;

        tree.EmplaceRoot(10);

        auto root = tree.Root();

        REQUIRE(root);

        auto left_result  = tree.EmplaceLeft(root, 5);
        auto right_result = tree.EmplaceRight(root, 15);

        CHECK(std::holds_alternative<std::monostate>(left_result));
        CHECK(std::holds_alternative<std::monostate>(right_result));

        auto left  = tree.Left(root);
        auto right = tree.Right(root);

        REQUIRE(left);
        REQUIRE(right);

        REQUIRE(tree.Value(left));
        REQUIRE(tree.Value(right));

        CHECK(*tree.Value(left) == 5);
        CHECK(*tree.Value(right) == 15);

        CHECK(tree.Size() == 3);
    }

    TEST_CASE("Child insertion rejects an empty parent")
    {
        BinaryTree<int> tree;

        BinaryTree<int>::NodeHandle invalid_parent{};

        auto left_result  = tree.EmplaceLeft(invalid_parent, 5);
        auto right_result = tree.EmplaceRight(invalid_parent, 15);

        REQUIRE(std::holds_alternative<TreeError>(left_result));
        REQUIRE(std::holds_alternative<TreeError>(right_result));

        CHECK(std::get<TreeError>(left_result) == TreeError::InvalidParent);

        CHECK(std::get<TreeError>(right_result) == TreeError::InvalidParent);

        CHECK(tree.Empty());
    }

    TEST_CASE("Child insertion rejects duplicate children")
    {
        BinaryTree<int> tree;

        tree.EmplaceRoot(10);

        auto root = tree.Root();

        REQUIRE(root);

        tree.EmplaceLeft(root, 5);
        tree.EmplaceRight(root, 15);

        auto left_result  = tree.EmplaceLeft(root, 20);
        auto right_result = tree.EmplaceRight(root, 30);

        REQUIRE(std::holds_alternative<TreeError>(left_result));
        REQUIRE(std::holds_alternative<TreeError>(right_result));

        CHECK(std::get<TreeError>(left_result) ==
              TreeError::ChildAlreadyExists);

        CHECK(std::get<TreeError>(right_result) ==
              TreeError::ChildAlreadyExists);

        REQUIRE(tree.Value(tree.Left(root)));
        REQUIRE(tree.Value(tree.Right(root)));

        CHECK(*tree.Value(tree.Left(root)) == 5);
        CHECK(*tree.Value(tree.Right(root)) == 15);

        CHECK(tree.Size() == 3);
    }

    // Handle validation
    TEST_CASE("Default constructed handle is invalid")
    {
        BinaryTree<int> tree;

        BinaryTree<int>::NodeHandle handle{};

        CHECK_FALSE(handle);
        CHECK_FALSE(tree.IsValid(handle));

        CHECK(tree.Value(handle) == nullptr);
        CHECK_FALSE(tree.Left(handle));
        CHECK_FALSE(tree.Right(handle));
    }

    TEST_CASE("Handle belongs to its originating tree")
    {
        BinaryTree<int> first_tree;
        BinaryTree<int> second_tree;

        first_tree.EmplaceRoot(10);
        second_tree.EmplaceRoot(20);

        auto first_root  = first_tree.Root();
        auto second_root = second_tree.Root();

        REQUIRE(first_root);
        REQUIRE(second_root);

        CHECK(first_tree.IsValid(first_root));
        CHECK(second_tree.IsValid(second_root));

        CHECK_FALSE(second_tree.IsValid(first_root));
        CHECK_FALSE(first_tree.IsValid(second_root));

        CHECK(second_tree.Value(first_root) == nullptr);
        CHECK(first_tree.Value(second_root) == nullptr);
    }

    TEST_CASE("Cross-tree parent handles are rejected")
    {
        BinaryTree<int> first_tree;
        BinaryTree<int> second_tree;

        first_tree.EmplaceRoot(10);
        second_tree.EmplaceRoot(20);

        auto first_root = first_tree.Root();

        REQUIRE(first_root);

        auto left_result  = second_tree.EmplaceLeft(first_root, 5);
        auto right_result = second_tree.EmplaceRight(first_root, 15);

        REQUIRE(std::holds_alternative<TreeError>(left_result));
        REQUIRE(std::holds_alternative<TreeError>(right_result));

        CHECK(std::get<TreeError>(left_result) == TreeError::InvalidParent);

        CHECK(std::get<TreeError>(right_result) == TreeError::InvalidParent);

        CHECK(second_tree.Size() == 1);
    }

    TEST_CASE("Left and Right reject invalid parent handles")
    {
        BinaryTree<int> tree;

        BinaryTree<int>::NodeHandle invalid_parent{};

        CHECK_FALSE(tree.Left(invalid_parent));
        CHECK_FALSE(tree.Right(invalid_parent));
    }

    TEST_CASE("IsValid distinguishes handles from different trees")
    {
        BinaryTree<int> first_tree;
        BinaryTree<int> second_tree;

        first_tree.EmplaceRoot(10);

        auto root = first_tree.Root();

        REQUIRE(root);

        CHECK(first_tree.IsValid(root));
        CHECK_FALSE(second_tree.IsValid(root));
    }

    // Value access
    TEST_CASE("Mutable handle provides controlled value access")
    {
        BinaryTree<int> tree;

        tree.EmplaceRoot(10);

        auto root = tree.Root();

        REQUIRE(root);

        auto *value = tree.Value(root);

        REQUIRE(value != nullptr);

        CHECK(*value == 10);

        *value = 20;

        CHECK(*tree.Value(root) == 20);
        CHECK(tree.Size() == 1);
    }

    TEST_CASE("Const tree provides read-only value access")
    {
        BinaryTree<int> tree;
        tree.EmplaceRoot(10);

        const auto &const_tree = tree;

        auto root = const_tree.Root();

        REQUIRE(root);
        CHECK(const_tree.IsValid(root));

        auto *value = const_tree.Value(root);

        REQUIRE(value != nullptr);

        static_assert(std::is_same_v<decltype(value), const int *>);

        CHECK(*value == 10);
    }

    TEST_CASE("Const handle provides read-only value access")
    {
        BinaryTree<int> tree;

        tree.EmplaceRoot(10);

        const auto &const_tree = tree;

        auto root = const_tree.Root();

        REQUIRE(root);

        auto *value = const_tree.Value(root);

        REQUIRE(value != nullptr);

        static_assert(std::is_same_v<decltype(value), const int *>);

        CHECK(*value == 10);
    }

    // Registry stability
    TEST_CASE("Handles remain valid after slot vector reallocation")
    {
        BinaryTree<int> tree;
        tree.EmplaceRoot(100);
        auto root = tree.Root();
        REQUIRE(root);

        constexpr std::size_t insertion_count = 256;

        // Chain insertions: each new node becomes parent of the next.
        // Forces slot vector reallocation while root handle stays valid.
        auto parent = root;

        for (std::size_t i = 0; i < insertion_count; ++i)
        {
            auto result = tree.EmplaceRight(parent, static_cast<int>(i));
            REQUIRE(std::holds_alternative<std::monostate>(result));

            auto child = tree.Right(parent);
            REQUIRE(child);
            parent = child;
        }

        CHECK(tree.IsValid(root));
        CHECK(*tree.Value(root) == 100);
    }

    // Storage architecture

    TEST_CASE("Node storage is contiguous")
    {
        BinaryTree<int> tree;

        tree.EmplaceRoot(0);

        auto parent = tree.Root();

        REQUIRE(parent);

        std::vector<BinaryTree<int>::NodeHandle> handles;
        handles.push_back(parent);

        constexpr std::size_t insertion_count = 512;

        for (std::size_t i = 1; i < insertion_count; ++i)
        {
            REQUIRE(std::holds_alternative<std::monostate>(
                tree.EmplaceRight(parent, static_cast<int>(i))));

            auto child = tree.Right(parent);
            REQUIRE(child);
            handles.push_back(child);
            parent = child;
        }

        std::vector<std::uintptr_t> addresses;
        addresses.reserve(handles.size());

        for (const auto &handle : handles)
        {
            const int *value = tree.Value(handle);
            REQUIRE(value != nullptr);
            addresses.push_back(reinterpret_cast<std::uintptr_t>(value));
        }

        const std::uintptr_t stride = addresses[1] - addresses[0];

        REQUIRE(stride > 0);

        for (std::size_t i = 2; i < addresses.size(); ++i)
        {
            REQUIRE(addresses[i] - addresses[i - 1] == stride);
        }
    }

    TEST_CASE("Handles and values correct after large growth")
    {
        BinaryTree<int> tree;

        tree.EmplaceRoot(0);

        auto root = tree.Root();

        REQUIRE(root);

        constexpr int target = 10000;

        std::vector<std::pair<BinaryTree<int>::NodeHandle, int>> nodes;
        nodes.emplace_back(root, 0);

        std::size_t next_parent = 0;
        int created             = 1;

        while (created < target)
        {
            auto parent = nodes[next_parent].first;

            if (std::holds_alternative<std::monostate>(
                    tree.EmplaceLeft(parent, created)))
            {
                auto child = tree.Left(parent);
                REQUIRE(child);
                nodes.emplace_back(child, created);
                ++created;
            }

            if (created >= target)
                break;

            if (std::holds_alternative<std::monostate>(
                    tree.EmplaceRight(parent, created)))
            {
                auto child = tree.Right(parent);
                REQUIRE(child);
                nodes.emplace_back(child, created);
                ++created;
            }

            ++next_parent;
        }

        CHECK(tree.Size() == static_cast<std::size_t>(target));

        for (const auto &[handle, value] : nodes)
        {
            REQUIRE(tree.IsValid(handle));
            REQUIRE(tree.Value(handle) != nullptr);
            CHECK(*tree.Value(handle) == value);
        }

        std::size_t visited = 0;
        tree.InOrder([&](const int &) { ++visited; });
        CHECK(visited == static_cast<std::size_t>(target));
    }

    TEST_CASE("Move-only values survive slot reallocation")
    {
        BinaryTree<std::unique_ptr<int>> tree;

        REQUIRE(std::holds_alternative<std::monostate>(
            tree.EmplaceRoot(std::make_unique<int>(0))));

        auto parent = tree.Root();

        REQUIRE(parent);

        constexpr std::size_t insertion_count = 512;

        for (std::size_t i = 1; i < insertion_count; ++i)
        {
            REQUIRE(std::holds_alternative<std::monostate>(tree.EmplaceRight(
                parent, std::make_unique<int>(static_cast<int>(i)))));

            auto child = tree.Right(parent);
            REQUIRE(child);
            parent = child;
        }

        std::size_t expected = 0;
        auto current         = tree.Root();

        while (current)
        {
            const std::unique_ptr<int> *value = tree.Value(current);
            REQUIRE(value != nullptr);
            REQUIRE(*value != nullptr);
            CHECK(**value == static_cast<int>(expected));
            ++expected;
            current = tree.Right(current);
        }

        CHECK(expected == insertion_count);
    }

    // Traversal
    TEST_CASE("InOrder traversal")
    {
        BinaryTree<int> tree;

        tree.EmplaceRoot(10);

        auto root = tree.Root();

        REQUIRE(root);

        tree.EmplaceLeft(root, 5);
        tree.EmplaceRight(root, 15);

        auto left = tree.Left(root);

        REQUIRE(left);

        tree.EmplaceLeft(left, 2);
        tree.EmplaceRight(left, 7);

        std::vector<int> result;

        tree.InOrder([&](const int &value) { result.push_back(value); });

        CHECK(result == std::vector<int>{2, 5, 7, 10, 15});
    }

    TEST_CASE("PreOrder traversal")
    {
        BinaryTree<int> tree;
        tree.EmplaceRoot(10);

        auto root = tree.Root();
        REQUIRE(root);

        tree.EmplaceLeft(root, 5);
        tree.EmplaceRight(root, 15);

        auto left = tree.Left(root);

        REQUIRE(left);

        tree.EmplaceLeft(left, 2);
        tree.EmplaceRight(left, 7);

        std::vector<int> result;

        tree.PreOrder([&](const int &value) { result.push_back(value); });

        CHECK(result == std::vector<int>{10, 5, 2, 7, 15});
    }

    TEST_CASE("PostOrder traversal")
    {
        BinaryTree<int> tree;

        tree.EmplaceRoot(10);

        auto root = tree.Root();

        REQUIRE(root);

        tree.EmplaceLeft(root, 5);
        tree.EmplaceRight(root, 15);

        auto left = tree.Left(root);

        REQUIRE(left);

        tree.EmplaceLeft(left, 2);
        tree.EmplaceRight(left, 7);

        std::vector<int> result;

        tree.PostOrder([&](const int &value) { result.push_back(value); });

        CHECK(result == std::vector<int>{2, 7, 5, 15, 10});
    }

    TEST_CASE("Traversal of empty tree")
    {
        BinaryTree<int> tree;

        int count = 0;

        tree.InOrder([&](const int &) { ++count; });
        tree.PreOrder([&](const int &) { ++count; });
        tree.PostOrder([&](const int &) { ++count; });

        CHECK(count == 0);
    }

    TEST_CASE("Traversal callbacks receive const values")
    {
        using Callback = decltype([](const int &) {});
        static_assert(std::invocable<Callback &, const int &>);
    }

    // Move semantics
    TEST_CASE("Move construction transfers handle ownership")
    {
        BinaryTree<int> source;

        source.EmplaceRoot(10);

        auto source_root = source.Root();

        REQUIRE(source_root);

        source.EmplaceLeft(source_root, 5);

        BinaryTree<int> destination(std::move(source));

        CHECK(destination.Size() == 2);
        CHECK(destination.IsValid(source_root));

        CHECK_FALSE(source.IsValid(source_root));

        auto root = destination.Root();

        REQUIRE(root);

        CHECK(*destination.Value(root) == 10);

        auto left = destination.Left(root);

        REQUIRE(left);

        CHECK(*destination.Value(left) == 5);

        CHECK(source.Empty());
        CHECK(source.Size() == 0);
        CHECK_FALSE(source.Root());
    }

    TEST_CASE("Move assignment transfers handle ownership")
    {
        BinaryTree<int> source;

        source.EmplaceRoot(10);

        auto source_root = source.Root();

        REQUIRE(source_root);

        source.EmplaceRight(source_root, 15);

        BinaryTree<int> destination;

        destination.EmplaceRoot(100);

        auto old_destination_root = destination.Root();

        REQUIRE(old_destination_root);

        destination = std::move(source);

        CHECK(destination.Size() == 2);

        CHECK(destination.IsValid(source_root));
        CHECK_FALSE(destination.IsValid(old_destination_root));

        CHECK_FALSE(source.IsValid(source_root));

        auto root = destination.Root();

        REQUIRE(root);

        CHECK(*destination.Value(root) == 10);

        auto right = destination.Right(root);

        REQUIRE(right);

        CHECK(*destination.Value(right) == 15);

        CHECK(source.Empty());
        CHECK(source.Size() == 0);
        CHECK_FALSE(source.Root());
    }

    TEST_CASE("Self move assignment preserves tree")
    {
        BinaryTree<int> tree;

        tree.EmplaceRoot(10);
        tree.EmplaceLeft(tree.Root(), 5);
        tree.EmplaceRight(tree.Root(), 15);

        auto root = tree.Root();
        REQUIRE(root);
        auto left = tree.Left(root);
        REQUIRE(left);
        auto right = tree.Right(root);
        REQUIRE(right);

        const auto size_before = tree.Size();

        // Intentional self-move: the implementation guards with
        // `if (this != &other)` and must treat this as a no-op.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
        tree = std::move(tree);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

        CHECK(tree.Size() == size_before);
        CHECK(tree.IsValid(root));
        CHECK(tree.IsValid(left));
        CHECK(tree.IsValid(right));
        CHECK(*tree.Value(root) == 10);
        CHECK(*tree.Value(left) == 5);
        CHECK(*tree.Value(right) == 15);
    }

    // Move-only values
    TEST_CASE("Tree supports move-only value types")
    {
        BinaryTree<std::unique_ptr<int>> tree;

        auto result = tree.EmplaceRoot(std::make_unique<int>(42));
        CHECK(std::holds_alternative<std::monostate>(result));
        auto root = tree.Root();

        REQUIRE(root);

        auto *value = tree.Value(root);

        REQUIRE(value != nullptr);
        REQUIRE(*value != nullptr);

        CHECK(**value == 42);
    }

    // Capacity
    TEST_CASE("Size and Empty reflect inserted nodes")
    {
        BinaryTree<int> tree;

        CHECK(tree.Empty());
        CHECK(tree.Size() == 0);

        tree.EmplaceRoot(10);

        CHECK_FALSE(tree.Empty());
        CHECK(tree.Size() == 1);

        auto root = tree.Root();

        REQUIRE(root);

        tree.EmplaceLeft(root, 5);

        CHECK(tree.Size() == 2);

        tree.EmplaceRight(root, 15);

        CHECK(tree.Size() == 3);
    }

    // ----------------------------------------------------------------
    // Edge cases: exception safety
    //
    // The insertion path is `allocate (may throw) -> commit (no-throw)`.
    // These tests prove that a throwing `T` constructor leaves the tree
    // in a consistent, usable state — no half-linked parent, no inflated
    // Size(), no corrupted root pointer.
    // ----------------------------------------------------------------

    TEST_CASE("EmplaceRoot leaves tree empty if T's constructor throws")
    {
        BinaryTree<ThrowingValue> tree;

        ThrowingValue::armed = true;
        CHECK_THROWS_AS(tree.EmplaceRoot(42), std::runtime_error);
        ThrowingValue::armed = false;

        // Externally observable state is unchanged.
        CHECK(tree.Empty());
        CHECK(tree.Size() == 0);
        CHECK_FALSE(tree.Root());
        CHECK_FALSE(tree.IsValid(tree.Root()));
        CHECK(tree.Value(tree.Root()) == nullptr);

        // And the tree remains fully usable.
        auto result = tree.EmplaceRoot(42);
        CHECK(std::holds_alternative<std::monostate>(result));
        CHECK(tree.Size() == 1);
        auto root = tree.Root();
        REQUIRE(root);
        CHECK(tree.Value(root)->value == 42);
    }

    TEST_CASE("EmplaceLeft leaves parent childless if T's constructor throws")
    {
        BinaryTree<ThrowingValue> tree;

        tree.EmplaceRoot(10);
        auto root = tree.Root();
        REQUIRE(root);

        ThrowingValue::armed = true;
        CHECK_THROWS_AS(tree.EmplaceLeft(root, 20), std::runtime_error);
        ThrowingValue::armed = false;

        // Parent's left slot must still be empty.
        CHECK_FALSE(tree.Left(root));
        CHECK(tree.Size() == 1);
        CHECK(tree.IsValid(root));
        CHECK(tree.Value(root)->value == 10);

        // Right slot still works after the failed left insertion.
        auto right_result = tree.EmplaceRight(root, 30);
        CHECK(std::holds_alternative<std::monostate>(right_result));
        CHECK(tree.Size() == 2);
        auto right = tree.Right(root);
        REQUIRE(right);
        CHECK(tree.Value(right)->value == 30);
    }

    TEST_CASE("EmplaceRight leaves parent childless if T's constructor throws")
    {
        BinaryTree<ThrowingValue> tree;

        tree.EmplaceRoot(10);
        auto root = tree.Root();
        REQUIRE(root);

        ThrowingValue::armed = true;
        CHECK_THROWS_AS(tree.EmplaceRight(root, 20), std::runtime_error);
        ThrowingValue::armed = false;

        CHECK_FALSE(tree.Right(root));
        CHECK(tree.Size() == 1);

        // Left slot still works after the failed right insertion.
        auto left_result = tree.EmplaceLeft(root, 5);
        CHECK(std::holds_alternative<std::monostate>(left_result));
        CHECK(tree.Size() == 2);
    }

    TEST_CASE("Failed emplace preserves all existing handles")
    {
        BinaryTree<ThrowingValue> tree;

        tree.EmplaceRoot(10);
        auto root = tree.Root();
        REQUIRE(root);

        tree.EmplaceLeft(root, 20);
        auto left = tree.Left(root);
        REQUIRE(left);

        tree.EmplaceRight(root, 30);
        auto right = tree.Right(root);
        REQUIRE(right);

        // Attempt a deeper insertion that throws.
        ThrowingValue::armed = true;
        CHECK_THROWS_AS(tree.EmplaceLeft(left, 40), std::runtime_error);
        ThrowingValue::armed = false;

        // All pre-existing handles and values remain valid and correct.
        CHECK(tree.Size() == 3);
        CHECK(tree.IsValid(root));
        CHECK(tree.IsValid(left));
        CHECK(tree.IsValid(right));
        CHECK(tree.Value(root)->value == 10);
        CHECK(tree.Value(left)->value == 20);
        CHECK(tree.Value(right)->value == 30);
        CHECK_FALSE(tree.Left(left));
    }

    TEST_CASE("Repeated throwing insertions leave tree consistent")
    {
        BinaryTree<ThrowingValue> tree;

        ThrowingValue::armed = true;
        CHECK_THROWS_AS(tree.EmplaceRoot(1), std::runtime_error);
        CHECK_THROWS_AS(tree.EmplaceRoot(2), std::runtime_error);
        CHECK_THROWS_AS(tree.EmplaceRoot(3), std::runtime_error);
        ThrowingValue::armed = false;

        CHECK(tree.Empty());
        CHECK(tree.Size() == 0);

        auto result = tree.EmplaceRoot(99);
        CHECK(std::holds_alternative<std::monostate>(result));
        CHECK(tree.Size() == 1);
        CHECK(tree.Value(tree.Root())->value == 99);
    }

    TEST_CASE("Throwing insertion after storage has grown is safe")
    {
        BinaryTree<ThrowingValue> tree;

        tree.EmplaceRoot(0);
        auto parent = tree.Root();
        REQUIRE(parent);

        // Fill several nodes so the slot vector has non-trivial size.
        for (int i = 1; i < 20; ++i)
        {
            REQUIRE(std::holds_alternative<std::monostate>(
                tree.EmplaceRight(parent, i)));
            parent = tree.Right(parent);
            REQUIRE(parent);
        }

        auto root = tree.Root();
        REQUIRE(root);

        // Throw during a subsequent insertion (this exercises the
        // fresh-slot path in AllocateNode with a non-empty vector).
        ThrowingValue::armed = true;
        CHECK_THROWS_AS(tree.EmplaceLeft(root, 999), std::runtime_error);
        ThrowingValue::armed = false;

        CHECK(tree.Size() == 20);
        CHECK_FALSE(tree.Left(root));
        CHECK(tree.Value(root)->value == 0);
    }

    TEST_CASE("Size only increments on successful insertion")
    {
        BinaryTree<int> tree;

        CHECK(tree.Size() == 0);

        CHECK(std::holds_alternative<std::monostate>(tree.EmplaceRoot(10)));
        CHECK(tree.Size() == 1);

        CHECK(std::holds_alternative<TreeError>(tree.EmplaceRoot(20)));
        CHECK(tree.Size() == 1);

        auto root = tree.Root();
        REQUIRE(root);

        CHECK(
            std::holds_alternative<std::monostate>(tree.EmplaceLeft(root, 5)));
        CHECK(tree.Size() == 2);

        CHECK(std::holds_alternative<TreeError>(tree.EmplaceLeft(root, 15)));
        CHECK(tree.Size() == 2);
    }

    // Edge cases: move semantics
    TEST_CASE("Move-construct from an empty tree")
    {
        BinaryTree<int> source;

        BinaryTree<int> dest(std::move(source));

        CHECK(dest.Empty());
        CHECK(dest.Size() == 0);
        CHECK_FALSE(dest.Root());

        CHECK(source.Empty());
        CHECK(source.Size() == 0);
    }

    TEST_CASE("Move-assign an empty tree over a non-empty one")
    {
        BinaryTree<int> source;
        BinaryTree<int> dest;

        dest.EmplaceRoot(10);
        auto old_handle = dest.Root();
        REQUIRE(old_handle);

        dest = std::move(source);

        CHECK(dest.Empty());
        CHECK(dest.Size() == 0);
        CHECK_FALSE(dest.IsValid(old_handle));
        CHECK_FALSE(dest.Root());
    }

    TEST_CASE("Handles remain scoped to their tree after a move")
    {
        BinaryTree<int> a;
        BinaryTree<int> b;

        a.EmplaceRoot(10);
        b.EmplaceRoot(20);

        auto ha = a.Root();
        auto hb = b.Root();
        REQUIRE(ha);
        REQUIRE(hb);

        BinaryTree<int> c(std::move(a));

        CHECK(c.IsValid(ha));       // moved with the data
        CHECK_FALSE(a.IsValid(ha)); // source's tree id rotated
        CHECK_FALSE(c.IsValid(hb)); // wrong tree
        CHECK(b.IsValid(hb));
    }

    TEST_CASE("Moved-from tree is empty and reusable")
    {
        BinaryTree<int> source;
        source.EmplaceRoot(10);
        source.EmplaceLeft(source.Root(), 5);
        auto stale_handle = source.Root();
        REQUIRE(stale_handle);

        BinaryTree<int> dest(std::move(source));

        CHECK(source.Empty());
        CHECK(source.Size() == 0);

        // Reuse the moved-from tree; it must be fully functional.
        auto result = source.EmplaceRoot(100);
        CHECK(std::holds_alternative<std::monostate>(result));
        CHECK(source.Size() == 1);

        auto new_root = source.Root();
        REQUIRE(new_root);
        CHECK(*source.Value(new_root) == 100);

        // A pre-move handle to the source must not become valid
        // on the reused source.
        CHECK_FALSE(source.IsValid(stale_handle));
    }

    TEST_CASE("Handle works for mutation after move")
    {
        BinaryTree<int> source;
        source.EmplaceRoot(10);
        auto handle = source.Root();
        REQUIRE(handle);

        BinaryTree<int> dest(std::move(source));

        // The handle belongs to `dest` now; mutating through it should work.
        auto result = dest.EmplaceLeft(handle, 20);
        CHECK(std::holds_alternative<std::monostate>(result));
        CHECK(dest.Size() == 2);

        auto child = dest.Left(handle);
        REQUIRE(child);
        REQUIRE(dest.Value(child));      // pin the failure here
        CHECK(*dest.Value(child) == 20); // int*, dereference
    }

    // Edge cases: Reserve() contract
    TEST_CASE("Reserve keeps Value pointers stable through insertions")
    {
        BinaryTree<int> tree;
        tree.Reserve(100);

        tree.EmplaceRoot(0);
        auto root = tree.Root();
        REQUIRE(root);

        auto *p_root = tree.Value(root);
        REQUIRE(p_root);
        CHECK(*p_root == 0);

        auto parent = root;
        for (int i = 1; i < 100; ++i)
        {
            REQUIRE(std::holds_alternative<std::monostate>(
                tree.EmplaceRight(parent, i)));
            parent = tree.Right(parent);
            REQUIRE(parent);
        }

        // No reallocation should have occurred: the root pointer is
        // still valid and still points at the same value.
        CHECK(*p_root == 0);
        CHECK(tree.Size() == 100);
    }

    // Edge cases: Value() pointer stability, traversal exceptions
    TEST_CASE("Value returns the same pointer on successive calls")
    {
        BinaryTree<int> tree;
        tree.EmplaceRoot(10);
        auto root = tree.Root();
        REQUIRE(root);

        auto *p1 = tree.Value(root);
        auto *p2 = tree.Value(root);
        auto *p3 = tree.Value(root);

        REQUIRE(p1);
        REQUIRE(p2);
        REQUIRE(p3);

        CHECK(p1 == p2);
        CHECK(p2 == p3);
    }

    TEST_CASE("Traversal propagates exceptions from the callback")
    {
        BinaryTree<int> tree;
        tree.EmplaceRoot(10);
        auto root = tree.Root();
        REQUIRE(root);
        tree.EmplaceLeft(root, 5);

        int calls = 0;
        CHECK_THROWS_AS(tree.InOrder(
                            [&](const int &)
                            {
                                ++calls;
                                throw std::runtime_error("callback boom");
                            }),
                        std::runtime_error);

        // Exactly one call before the exception unwound the recursion.
        CHECK(calls == 1);
        
        // The tree itself is untouched.
        CHECK(tree.Size() == 2);
        CHECK(tree.IsValid(root));
        REQUIRE(tree.Value(root));      // pin the failure here
        CHECK(*tree.Value(root) == 10); // int*, dereference
    }
}
