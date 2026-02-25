#pragma once

#include <atomic>
#include <vector>
#include <queue>
#include <string>
#include <iostream>

#include "uepoch.h"

#define FIXED_AGG_COUNT 6

namespace SIMPLE_AGG_FUNNEL
{
    template <typename T>
    class alignas(1024) AggFunnelCounter
    {
    private:
        struct alignas(32) MappingListNode
        {
            MappingListNode *prev = nullptr;
            T child_from = 0;
            T child_to = 0;
            T root_from = -1;
        };

        struct alignas(256) Node
        {
            alignas(128) std::atomic<T> count = 0;
            alignas(128) std::atomic<T> sent = 0;
            std::atomic<MappingListNode *> mapping_list = new MappingListNode();
            ~Node() { delete mapping_list.load(); }
        };

        alignas(1024) std::atomic<T> counter;
        Node child[FIXED_AGG_COUNT];
        int PADDING[32] = {};

    public:
        AggFunnelCounter(T start) : counter(start) {}
        AggFunnelCounter(T start, unsigned tid) : AggFunnelCounter(start)  {}
        AggFunnelCounter() : AggFunnelCounter(0) {}

        T update(Node *child, T child_from, T child_to)
        {
            T root_from = counter.fetch_add(child_to - child_from);
            MappingListNode *new_mapping = new MappingListNode();
            //auto *new_mapping = uepoch::New<MappingListNode>();

            MappingListNode *existing_mapping = child->mapping_list.load();
            new_mapping->prev = existing_mapping;
            new_mapping->child_from = child_from;
            new_mapping->child_to = child_to;
            new_mapping->root_from = root_from;
            child->mapping_list.store(new_mapping, std::memory_order_release);
            child->sent.store(child_to, std::memory_order_release);

            //uepoch::Retire(existing_mapping);
            uepoch::delay([=] {delete(existing_mapping);});
            return root_from;
        }

        T get_my_root(Node *child, T my_child_from)
        {
            MappingListNode *mapping = child->mapping_list.load();
            while (mapping->child_from > my_child_from)
            {
                mapping = mapping->prev;
            }

            T root_from = mapping->root_from;
            T child_from = mapping->child_from;
            return root_from + my_child_from - child_from;
        }

        T fetch_add(T diff, unsigned tid = 0)
        {
            int nd_idx = uepoch::thread_id() % FIXED_AGG_COUNT;
            if (nd_idx < 0) {
                return counter.fetch_add(diff);
            }
            return uepoch::protect([&] {
                Node *child = &this->child[nd_idx];
                T child_from = child->count.fetch_add(diff);
                T next_from = child->sent.load();
                while (next_from < child_from) {
                    next_from = child->sent.load();
                }
                if (child_from == next_from) {
                    // I should do the work
                    T child_to = child->count.load();
                    return update(child, child_from, child_to);
                } else {
                    // Mine is already done
                    return get_my_root(child, child_from);
                }
            });
        }

        T load() const
        {
            return counter.load();
        }

        void store(T value, std::memory_order order = std::memory_order_seq_cst)
        {
            counter.store(value, order);
        }

        bool compare_exchange(T &expected, T desired)
        {
            return counter.compare_exchange_strong(expected, desired);
        }
    };
}
