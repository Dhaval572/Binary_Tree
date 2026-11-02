#include <benchmark/benchmark.h>

#include <binary_tree/binary_tree.hpp>

#include <cstdint>
#include <queue>
#include <vector>

namespace
{
    using Tree = binary_tree::BinaryTree<int>;

    // Level-order fill: complete tree of `count` nodes.
    void FillBalanced(Tree &tree, std::int64_t count)
    {
        if (count <= 0)
            return;

        tree.EmplaceRoot(0);

        auto root = tree.Root();

        if (!root)
            return;

        std::queue<Tree::NodeHandle> parents;
        parents.push(root);

        std::int64_t assigned = 1;

        while (!parents.empty() && assigned < count)
        {
            auto parent = parents.front();
            parents.pop();

            if (assigned < count)
            {
                tree.EmplaceLeft(parent, static_cast<int>(assigned));

                auto child = tree.Left(parent);

                if (child)
                {
                    parents.push(child);
                }

                ++assigned;
            }

            if (assigned < count)
            {
                tree.EmplaceRight(parent, static_cast<int>(assigned));

                auto child = tree.Right(parent);

                if (child)
                {
                    parents.push(child);
                }

                ++assigned;
            }
        }
    }

    Tree MakeBalanced(std::int64_t count)
    {
        Tree tree;

        FillBalanced(tree, count);

        return tree;
    }

    std::vector<Tree::ConstNodeHandle> CollectHandles(const Tree &tree)
    {
        std::vector<Tree::ConstNodeHandle> handles;

        auto root = tree.Root();

        if (!root)
            return handles;

        std::queue<Tree::ConstNodeHandle> pending;
        pending.push(root);
        handles.push_back(root);

        while (!pending.empty())
        {
            auto current = pending.front();
            pending.pop();

            if (auto left = tree.Left(current))
            {
                handles.push_back(left);
                pending.push(left);
            }

            if (auto right = tree.Right(current))
            {
                handles.push_back(right);
                pending.push(right);
            }
        }

        return handles;
    }

} // namespace

// Insertion
static void BM_ChainInsert(benchmark::State &state)
{
    const std::int64_t count = state.range(0);

    for (auto _ : state)
    {
        Tree tree;
        Tree::NodeHandle parent;

        for (std::int64_t i = 0; i < count; ++i)
        {
            if (!parent)
            {
                tree.EmplaceRoot(static_cast<int>(i));
                parent = tree.Root();
                continue;
            }

            tree.EmplaceRight(parent, static_cast<int>(i));
            parent = tree.Right(parent);
        }

        benchmark::DoNotOptimize(tree.Size());
    }

    state.SetItemsProcessed(state.iterations() * count);
}

BENCHMARK(BM_ChainInsert)->RangeMultiplier(4)->Range(128, 8192);

static void BM_BuildBalanced(benchmark::State &state)
{
    const std::int64_t count = state.range(0);

    for (auto _ : state)
    {
        Tree tree = MakeBalanced(count);

        benchmark::DoNotOptimize(tree.Size());
    }

    state.SetItemsProcessed(state.iterations() * count);
}

BENCHMARK(BM_BuildBalanced)->RangeMultiplier(4)->Range(128, 8192);

static void BM_BuildBalancedReserve(benchmark::State &state)
{
    const std::int64_t count = state.range(0);

    for (auto _ : state)
    {
        Tree tree;
        tree.Reserve(static_cast<std::size_t>(count));

        FillBalanced(tree, count);

        benchmark::DoNotOptimize(tree.Size());
    }

    state.SetItemsProcessed(state.iterations() * count);
}

BENCHMARK(BM_BuildBalancedReserve)->RangeMultiplier(4)->Range(128, 8192);

static void BM_DeepChainWalk(benchmark::State &state)
{
    const std::int64_t count = state.range(0);

    Tree tree;
    tree.EmplaceRoot(0);

    auto parent = tree.Root();

    for (std::int64_t i = 1; i < count; ++i)
    {
        tree.EmplaceRight(parent, static_cast<int>(i));
        parent = tree.Right(parent);
    }

    for (auto _ : state)
    {
        std::int64_t sum = 0;

        auto current = tree.Root();

        for (std::int64_t i = 0; i < count; ++i)
        {
            const int *value = tree.Value(current);

            if (!value)
                break;

            sum += *value;
            current = tree.Right(current);
        }

        benchmark::DoNotOptimize(sum);
    }

    state.SetItemsProcessed(state.iterations() * count);
}

BENCHMARK(BM_DeepChainWalk)->RangeMultiplier(4)->Range(1024, 65536);

// Traversal
static void BM_InOrder(benchmark::State &state)
{
    const std::int64_t count = state.range(0);

    Tree tree = MakeBalanced(count);

    for (auto _ : state)
    {
        std::int64_t sum = 0;

        tree.InOrder([&sum](const int &value) { sum += value; });

        benchmark::DoNotOptimize(sum);
    }

    state.SetItemsProcessed(state.iterations() * count);
}

BENCHMARK(BM_InOrder)->RangeMultiplier(4)->Range(128, 8192);

static void BM_PreOrder(benchmark::State &state)
{
    const std::int64_t count = state.range(0);

    Tree tree = MakeBalanced(count);

    for (auto _ : state)
    {
        std::int64_t sum = 0;

        tree.PreOrder([&sum](const int &value) { sum += value; });

        benchmark::DoNotOptimize(sum);
    }

    state.SetItemsProcessed(state.iterations() * count);
}

BENCHMARK(BM_PreOrder)->RangeMultiplier(4)->Range(128, 8192);

static void BM_PostOrder(benchmark::State &state)
{
    const std::int64_t count = state.range(0);

    Tree tree = MakeBalanced(count);

    for (auto _ : state)
    {
        std::int64_t sum = 0;

        tree.PostOrder([&sum](const int &value) { sum += value; });

        benchmark::DoNotOptimize(sum);
    }

    state.SetItemsProcessed(state.iterations() * count);
}

BENCHMARK(BM_PostOrder)->RangeMultiplier(4)->Range(128, 8192);

// Handle operations
static void BM_IsValid(benchmark::State &state)
{
    const std::int64_t count = state.range(0);

    Tree tree = MakeBalanced(count);

    const auto handles = CollectHandles(tree);

    for (auto _ : state)
    {
        std::size_t valid = 0;

        for (const auto &handle : handles)
        {
            if (tree.IsValid(handle))
            {
                ++valid;
            }
        }

        benchmark::DoNotOptimize(valid);
    }

    state.SetItemsProcessed(state.iterations() * count);
}

BENCHMARK(BM_IsValid)->RangeMultiplier(4)->Range(128, 8192);

static void BM_ValueAccess(benchmark::State &state)
{
    const std::int64_t count = state.range(0);

    Tree tree = MakeBalanced(count);

    const auto handles = CollectHandles(tree);

    for (auto _ : state)
    {
        int sum = 0;

        for (const auto &handle : handles)
        {
            if (const int *value = tree.Value(handle))
            {
                sum += *value;
            }
        }

        benchmark::DoNotOptimize(sum);
    }

    state.SetItemsProcessed(state.iterations() * count);
}

BENCHMARK(BM_ValueAccess)->RangeMultiplier(4)->Range(128, 8192);

static void BM_ChildLookup(benchmark::State &state)
{
    const std::int64_t count = state.range(0);

    Tree tree = MakeBalanced(count);

    const auto handles = CollectHandles(tree);

    for (auto _ : state)
    {
        std::size_t found = 0;

        for (const auto &handle : handles)
        {
            if (tree.Left(handle))
            {
                ++found;
            }

            if (tree.Right(handle))
            {
                ++found;
            }
        }

        benchmark::DoNotOptimize(found);
    }

    state.SetItemsProcessed(state.iterations() * count);
}

BENCHMARK(BM_ChildLookup)->RangeMultiplier(4)->Range(128, 8192);
