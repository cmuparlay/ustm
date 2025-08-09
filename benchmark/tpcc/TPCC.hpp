#ifndef TPCC_HPP
#define TPCC_HPP

/*
 * This is a test for transactions.
 */

#include "glue.hpp"
//#include "utils.hpp"
#include "TPCCMacro.hpp"
#include "loaders.hpp"
#include "helpers.hpp"

#include <iostream>
#include <cstdlib>
#include <atomic>
#include <algorithm>
#include <vector>
#include <string>
#include <thread>
#include <unordered_set>

#include <typeinfo>

namespace tpcc {

  class TPCC {
    enum OpType {
      NewOrder=0,
      Payment,
      Delivery,
      OrderStatus,
      StockLevel,
      NOPS
    };

    padded<::std::atomic<size_t>> *g_district_ids;
    padded<size_t> *backoff_shifts;

    TPCC_TABLE_LIST(TPCC_TABLE_DECLARE)

    public:
    typedef ::std::pair<bool, ssize_t> txn_result;

    std::vector<int> op_type_percent;

    TPCC(
         int p0, // NewOrder
         int p1, // Payment
         int p2, // Delivery
         int p3, // OrderStatus
         int p4) // StockLevel
    {
      op_type_percent.push_back(p0);
      op_type_percent.push_back(p0+p1);
      op_type_percent.push_back(p0+p1+p2);
      op_type_percent.push_back(p0+p1+p2+p3);
      op_type_percent.push_back(p0+p1+p2+p3+p4);
      assert(op_type_percent.size()==OpType::NOPS);
      assert(op_type_percent[OpType::NOPS-1] == 100);
    }


    template <typename F>
    void do_tx(int tid, const F& f) { stm::atomic_region(f); }

    virtual void init(GlobalTestConfig* gtc){
      if(gtc->verbose){
        TPCC_TABLE_LIST(TPCC_TABLE_PRINT_SIZE)
          }

      // As how Silo does, we set NumWarehouses to thread number.
      ::tpcc::NumWarehouses = gtc->num_warehouses;

      g_district_ids = new padded<::std::atomic<size_t>>[NumWarehouses*NumDistrictsPerWarehouse]();
      for (size_t i = 0; i < NumWarehouses * NumDistrictsPerWarehouse; i++) 
        g_district_ids[i].ui.store(3001);

      backoff_shifts = new padded<size_t> [gtc->task_num]();
      for (int i = 0; i < gtc->task_num; i++) 
        backoff_shifts[i].ui = 0;

      TPCC_TABLE_LIST(TPCC_TABLE_INIT)

        doPrefill(gtc);

#ifdef TPCC_STATS
      reset_tpcc_stats();
#endif
    }

    void table_sizes() { TPCC_TABLE_LIST(TPCC_TABLE_SIZES) }

    virtual void parInit(GlobalTestConfig* gtc, LocalTestConfig* ltc){
    }

    void doPrefill(GlobalTestConfig* gtc) {
      // Wentao:
      // Different from Silo, we only parallelize loaders of the
      // same kind, to ensure that the maximum thread number is
      // always <= the thread number set in _esys.
      // Due to this reason, we have to call loaders_run_clear()
      // every time we are done with one kind.

      ::std::vector<tpcc_loader *> loaders;
      loaders.reserve(NumWarehouses);
      auto loaders_run_clear = [=, &loaders]() mutable {
        assert(loaders.size() <= NumWarehouses);
        for(auto l:loaders)
          l->start();
        for(auto l:loaders)
          l->join();
        tpcc_loader::reset_thread_info();
        for (size_t i = 0; i < loaders.size(); i++)
          delete loaders[i];
        loaders.clear();
      };
      constexpr bool enable_parallel_loading = true;

      loaders.push_back(new tpcc_warehouse_loader(9324, gtc, tbl_warehouse));
      loaders_run_clear();

      loaders.push_back(new tpcc_item_loader(235443, gtc, tbl_item));
      loaders_run_clear();

      if (enable_parallel_loading) {
        fast_random r(89785943);
        for (uint i = 1; i <= NumWarehouses; i++)
          loaders.push_back(new tpcc_stock_loader(r.next(), gtc, tbl_stock, tbl_stock_data, i));
      } else {
        loaders.push_back(new tpcc_stock_loader(89785943, gtc, tbl_stock, tbl_stock_data, -1));
      }
      loaders_run_clear();

      loaders.push_back(new tpcc_district_loader(129856349, gtc, tbl_district));
      loaders_run_clear();

      if (enable_parallel_loading) {
        fast_random r(923587856425);
        for (uint i = 1; i <= NumWarehouses; i++)
          loaders.push_back(new tpcc_customer_loader(r.next(), gtc, tbl_customer, tbl_customer_name_idx, tbl_history, i));
      } else {
        loaders.push_back(new tpcc_customer_loader(923587856425, gtc, tbl_customer, tbl_customer_name_idx, tbl_history, -1));
      }
      loaders_run_clear();

      if (enable_parallel_loading) {
        fast_random r(2343352);
        for (uint i = 1; i <= NumWarehouses; i++)
          loaders.push_back(new tpcc_order_loader(r.next(), gtc, tbl_oorder, tbl_oorder_c_id_idx, tbl_new_order, tbl_order_line, i));
      } else {
        loaders.push_back(new tpcc_order_loader(2343352, gtc, tbl_oorder, tbl_oorder_c_id_idx, tbl_new_order, tbl_order_line, -1));
      }
      std::cout << "here" << std::endl;
      loaders_run_clear();

      // one sync is enough
      //txn_manager._esys->sync();

      if(gtc->verbose){
        printf("Prefilled\n");
      }
    }
  
    int execute(GlobalTestConfig* gtc, LocalTestConfig* ltc){
      auto time_up = gtc->finish;

      int ops = 0;
      uint64_t seed = ltc->seed;

      fast_random r(seed); 

      int tid = ltc->tid;

      auto now = ::std::chrono::high_resolution_clock::now();

      while(::std::chrono::duration_cast<::std::chrono::microseconds>(time_up - now).count()>0){
        operation(r, tid);

        ops++;
        if (ops % 512 == 0){
          now = ::std::chrono::high_resolution_clock::now();
        }
      }
      return ops;
    }

    void operation(fast_random& r, int tid){
      while (true) {
        int op = abs(static_cast<long>(r.next()%100));
        int retry = 0;
        // try txn and see if it's committed
        // if not, roll back `r` state, backoff a little, and
        // retry the same txn
        const unsigned long old_seed = r.get_seed();

        txn_result ret;
        if(op<this->op_type_percent[OpType::NewOrder]){
          ret = txn_new_order(r, tid);
        }
        else if(op<this->op_type_percent[OpType::Payment]){
          ret = txn_payment(r, tid);
        }
        else if(op<this->op_type_percent[OpType::Delivery]){
          ret = txn_delivery(r, tid);
        }
        else if(op<this->op_type_percent[OpType::OrderStatus]){
          ret = txn_order_status(r, tid);
        }
        else{ // op<=this->op_type_percent[OpType::StockLevel]
          ret = txn_stock_level(r, tid);
        }
        if (LIKELY(ret.first)){
          //     backoff_shifts[tid].ui >>= 1;
          break; // succeeded, return
        } else {
          if (retry>10000) {
            errexit("Retry too many times!");
            break;
          } else {
            //         if(backoff_shifts[tid].ui < 63)
            //             backoff_shifts[tid].ui++;
            //         uint64_t spins = 1UL << backoff_shifts[tid].ui;
            //         spins *= 100;
            //         while (spins) {
            //             __asm volatile("pause" : :);
            //             spins--;
            //         }
            //         r.set_seed(old_seed);
            retry++;
            // retry
          }
        }
      } // while(true)
    }

    txn_result txn_new_order(fast_random& r, int tid){
      // id index start from 1; inherited from Silo
      uint warehouse_id_start = (tid % NumWarehouses) + 1;
      uint warehouse_id_end = (tid % NumWarehouses) + 2;
      const uint warehouse_id = PickWarehouseId(r, warehouse_id_start, warehouse_id_end);
      const uint districtID = RandomNumber(r, 1, 10);
      const uint customerID = GetCustomerId(r);
      const uint numItems = RandomNumber(r, 5, 15);
      uint itemIDs[15], supplierWarehouseIDs[15], orderQuantities[15];
      ::std::unordered_set<uint> items;
      for (uint i = 0; i < numItems; i++) {
        itemIDs[i] = GetItemId(r);
        // FIXME(Wentao): somehow Medley doesn't work when there are
        // duplicated itemIDs. Work around for now.
        //do {
        //    itemIDs[i] = GetItemId(r);
        //} while(items.count(itemIDs[i]) > 0);
        items.insert(itemIDs[i]);

        if(LIKELY(g_disable_xpartition_txn ||
                  NumWarehouses == 1 ||
                  RandomNumber(r, 1, 100) > g_new_order_remote_item_pct)) {
          supplierWarehouseIDs[i] = warehouse_id;
        } else {
          do {
            supplierWarehouseIDs[i] = RandomNumber(r, 1, NumWarehouses);
          } while (supplierWarehouseIDs[i] == warehouse_id);
        }
        orderQuantities[i] = RandomNumber(r, 1, 10);
      }

      // XXX(stephentu): implement rollback
      //
      // worst case txn profile:
      //   1 customer get
      //   1 warehouse get
      //   1 district get
      //   1 new_order insert
      //   1 district put
      //   1 oorder insert
      //   1 oorder_cid_idx insert
      //   15 times:
      //      1 item get
      //      1 stock get
      //      1 stock put
      //      1 order_line insert
      //
      // output from txn counters:
      //   max_absent_range_set_size : 0
      //   max_absent_set_size : 0
      //   max_node_scan_size : 0
      //   max_read_set_size : 15
      //   max_write_set_size : 15
      //   num_txn_contexts : 9
        
      ssize_t ret = 0;
      uint64_t retry = 0;
      while(true) {
        //try {
        do_tx(tid, [&] {
          ret = 0;

          const customer::key k_c(warehouse_id, districtID, customerID);
          auto v_c = tbl_customer[warehouse_id-1]->get(k_c, tid);
          assert(v_c.has_value());
          SanityCheckCustomer(&k_c, &(*v_c));

          const warehouse::key k_w(warehouse_id);
          auto v_w = tbl_warehouse[warehouse_id-1]->get(k_w, tid);
          assert(v_w.has_value());
          SanityCheckWarehouse(&k_w, &(*v_w));

          const district::key k_d(warehouse_id, districtID);
          auto v_d = tbl_district[warehouse_id-1]->get(k_d, tid);

          assert(v_d.has_value());
          SanityCheckDistrict(&k_d, &(*v_d));

          const uint64_t my_next_o_id = g_new_order_fast_id_gen ?
            FastNewOrderIdGen(warehouse_id, districtID) : v_d->d_next_o_id;

          const new_order::key k_no(warehouse_id, districtID, my_next_o_id);
          const new_order::value v_no;
          const size_t new_order_sz = sizeof(v_no);
          tbl_new_order[warehouse_id-1]->insert(k_no, v_no, tid);
          ret += new_order_sz;

          if (!g_new_order_fast_id_gen) {
            district::value v_d_new(*v_d);
            // abort();
            v_d_new.d_next_o_id++;
            tbl_district[warehouse_id-1]->put(k_d, v_d_new, tid);
          }

          const oorder::key k_oo(warehouse_id, districtID, k_no.no_o_id);
          oorder::value v_oo;
          v_oo.o_c_id = int32_t(customerID);
          v_oo.o_carrier_id = 0; // seems to be ignored
          v_oo.o_ol_cnt = int8_t(numItems);
          v_oo.o_all_local = true;
          v_oo.o_entry_d = GetCurrentTimeMillis();

          const size_t oorder_sz = sizeof(v_oo);
          tbl_oorder[warehouse_id-1]->insert(k_oo, v_oo, tid);
          ret += oorder_sz;

          const oorder_c_id_idx::key k_oo_idx(warehouse_id, districtID, customerID, k_no.no_o_id);
          const oorder_c_id_idx::value v_oo_idx(0);

          tbl_oorder_c_id_idx[warehouse_id-1]->insert(k_oo_idx, v_oo_idx, tid);

          for (uint ol_number = 1; ol_number <= numItems; ol_number++) {
            const uint ol_supply_w_id = supplierWarehouseIDs[ol_number - 1];
            const uint ol_i_id = itemIDs[ol_number - 1];
            const uint ol_quantity = orderQuantities[ol_number - 1];

            const item::key k_i(ol_i_id);
            auto v_i = tbl_item[0]->get(k_i, tid);
            SanityCheckItem(&k_i, &(*v_i));

            const stock::key k_s(ol_supply_w_id, ol_i_id);
            auto update_stock = [&] (const std::optional<stock::value>& v_s_o) {
              stock::value v_s = *v_s_o;
              if (v_s.s_quantity - ol_quantity >= 10)
                v_s.s_quantity -= ol_quantity;
              else
                v_s.s_quantity += -int32_t(ol_quantity) + 91;
              v_s.s_ytd += ol_quantity;
              v_s.s_remote_cnt += (ol_supply_w_id == warehouse_id) ? 0 : 1;
              return v_s;
            };

            tbl_stock[ol_supply_w_id-1]->upsert_f(k_s, update_stock, tid);

            //auto v_s = tbl_stock[ol_supply_w_id-1]->get(k_s, tid);
            //SanityCheckStock(&k_s, &(*v_s));
            // stock::value v_s_new(*v_s);
            // //stock::value v_s_new; // toss
            // if (v_s_new.s_quantity - ol_quantity >= 10)
            //     v_s_new.s_quantity -= ol_quantity;
            // else
            //     v_s_new.s_quantity += -int32_t(ol_quantity) + 91;
            // v_s_new.s_ytd += ol_quantity;
            // v_s_new.s_remote_cnt += (ol_supply_w_id == warehouse_id) ? 0 : 1;

            // tbl_stock[ol_supply_w_id-1]->put(k_s, v_s_new, tid);

            const order_line::key k_ol(warehouse_id, districtID, k_no.no_o_id, ol_number);
            order_line::value v_ol;
            v_ol.ol_i_id = int32_t(ol_i_id);
            v_ol.ol_delivery_d = 0; // not delivered yet
            v_ol.ol_amount = float(ol_quantity) * v_i->i_price;
            v_ol.ol_supply_w_id = int32_t(ol_supply_w_id);
            v_ol.ol_quantity = int8_t(ol_quantity);

            const size_t order_line_sz = sizeof(v_ol);
            tbl_order_line[warehouse_id-1]->insert(k_ol, v_ol, tid);
            ret += order_line_sz;
          }
          return true;
        });
        // tx_end(tid);

        return txn_result(true, ret);
        // } catch (const pds::TransactionAborted& e) {
        //     // txn aborted
        //     #ifdef TPCC_STATS
        //         tpcc_aborts[tid*TPCC_PADDING]++;
        //     #endif
        //     retry++;
        //     if(retry > 10000) {
        //         std::cerr << "too many retries" << std::endl;
        //         abort();
        //     }
        // }
      }
        
      return txn_result(false, 0);
    }

    // WARNING: This version of txn_payment only select customers by ID!
    txn_result txn_payment(fast_random& r, int tid){
      // id index start from 1; inherited from Silo
      uint warehouse_id_start = (tid % NumWarehouses) + 1;
      uint warehouse_id_end = (tid % NumWarehouses) + 2;
      const uint warehouse_id = PickWarehouseId(r, warehouse_id_start, warehouse_id_end);
      const uint districtID = RandomNumber(r, 1, NumDistrictsPerWarehouse);
      uint customerDistrictID, customerWarehouseID;
      if(LIKELY(g_disable_xpartition_txn ||
                NumWarehouses == 1 ||
                RandomNumber(r, 1, 100) <= 85)) {
        customerDistrictID = districtID;
        customerWarehouseID = warehouse_id;
      } else {
        customerDistrictID = RandomNumber(r, 1, NumDistrictsPerWarehouse);
        do {
          customerWarehouseID = RandomNumber(r, 1, NumWarehouses);
        } while (customerWarehouseID == warehouse_id);
      }

      const float paymentAmount = (float) (RandomNumber(r, 100, 500000) / 100.0);
      const uint32_t ts = GetCurrentTimeMillis();
      assert(!g_disable_xpartition_txn || customerWarehouseID == warehouse_id);

      // output from txn counters:
      //   max_absent_range_set_size : 0
      //   max_absent_set_size : 0
      //   max_node_scan_size : 10
      //   max_read_set_size : 71
      //   max_write_set_size : 1
      //   num_txn_contexts : 5

      ssize_t ret = 0;
      uint64_t retry = 0;
      while(true) {
        //try {
        // tx_begin(tid);
        do_tx(tid, [&] {
          ret = 0;
          const warehouse::key k_w(warehouse_id);
          std::string w_name;
          auto update_warehouse = [&] (auto x) {
            warehouse::value v_w = *x;
            w_name = v_w.w_name.str();
            v_w.w_ytd += paymentAmount;
            return v_w;
          };
          tbl_warehouse[warehouse_id-1]->upsert_f(k_w, update_warehouse, tid);
                    
          //auto v_w = tbl_warehouse[warehouse_id-1]->get(k_w, tid);
          // assert(v_w.has_value());
          // SanityCheckWarehouse(&k_w, &(*v_w));

          //warehouse::value v_w_new(*v_w);
          //warehouse::value v_w_new; 
          //v_w_new.w_ytd += paymentAmount;
          //tbl_warehouse[warehouse_id-1]->put(k_w, v_w_new, tid);

          const district::key k_d(warehouse_id, districtID);
          std::string d_name;
          auto update_district = [&] (auto x) {
            district::value v_d = *x;
            d_name = v_d.d_name.str();
            v_d.d_ytd += paymentAmount;
            return v_d;
          };

          auto v_d = tbl_district[warehouse_id-1]->upsert_f(k_d, update_district, tid);
                    
          //auto v_d = tbl_district[warehouse_id-1]->get(k_d, tid);
          //assert(v_d.has_value());
          //SanityCheckDistrict(&k_d, &(*v_d));

          //district::value v_d_new(*v_d);
          //v_d_new.d_ytd += paymentAmount;
          //tbl_district[warehouse_id-1]->put(k_d, v_d_new, tid);

          customer::key k_c;
          if (RandomNumber(r, 1, 100) <= 60) {
            // cust by name
            uint8_t lastname_buf[CustomerLastNameMaxSize + 1];
            static_assert(sizeof(lastname_buf) == 16, "xx");
            memset(lastname_buf, 0, sizeof(lastname_buf));
            GetNonUniformCustomerLastNameRun(r, lastname_buf);

            customer_name_idx::key k_c_idx;
            k_c_idx.c_w_id = customerWarehouseID;
            k_c_idx.c_d_id = customerDistrictID;
            k_c_idx.c_last.assign((const char *) lastname_buf, 16);

            auto v_c_idx = tbl_customer_name_idx[customerWarehouseID-1]->get(k_c_idx, tid);
            assert(v_c_idx.has_value() && v_c_idx->c_id_idx->size() > 0);
            assert(v_c_idx->c_id_idx->size() < NMaxCustomerIdxScanElems); // we should detect this
            int index = v_c_idx->c_id_idx->size() / 2;
            if (v_c_idx->c_id_idx->size() % 2 == 0)
              index--;

            k_c.c_w_id = customerWarehouseID;
            k_c.c_d_id = customerDistrictID;
            k_c.c_id = (*(v_c_idx->c_id_idx))[index].first.c_id;
          } else {
            // cust by ID
            const uint customerID = GetCustomerId(r);
            k_c.c_w_id = customerWarehouseID;
            k_c.c_d_id = customerDistrictID;
            k_c.c_id = customerID;
          }
          auto v_c = tbl_customer[customerWarehouseID-1]->get(k_c, tid);
          assert(v_c.has_value());
          SanityCheckCustomer(&k_c, &(*v_c));
          customer::value v_c_new(*v_c);

          v_c_new.c_balance -= paymentAmount;
          v_c_new.c_ytd_payment += paymentAmount;
          v_c_new.c_payment_cnt++;
          if (strncmp(v_c->c_credit.data(), "BC", 2) == 0) {
            char buf[501];
            int n = snprintf(buf, sizeof(buf), "%d %d %d %d %d %f | %s",
                             k_c.c_id,
                             k_c.c_d_id,
                             k_c.c_w_id,
                             districtID,
                             warehouse_id,
                             paymentAmount,
                             v_c->c_data.c_str());
            v_c_new.c_data.resize_junk(
                                       std::min(static_cast<size_t>(n), v_c_new.c_data.max_size()));
            memcpy((void *) v_c_new.c_data.data(), &buf[0], v_c_new.c_data.size());
          }

          tbl_customer[customerWarehouseID-1]->put(k_c, v_c_new, tid);

          const history::key k_h(k_c.c_d_id, k_c.c_w_id, k_c.c_id, districtID, warehouse_id, ts);
          history::value v_h;
          v_h.h_amount = paymentAmount;
          v_h.h_data.assign(w_name + "    " + d_name);

          const size_t history_sz = sizeof(v_h);
          tbl_history[warehouse_id-1]->insert(k_h, v_h, tid);
          ret += history_sz;
          return true;
        });

        // tx_end(tid);

        return txn_result(true, ret);
        // } catch (const pds::TransactionAborted& e) {
        //     // txn aborted
        //     // std::cerr << "transaction aborted 2" << std::endl;
        //     #ifdef TPCC_STATS
        //         tpcc_aborts[tid*TPCC_PADDING]++;
        //     #endif
        //     retry++;
        //     if(retry > 10000) {
        //         std::cerr << "too many retries" << std::endl;
        //         abort();
        //     }
        // }
      }
        
      return txn_result(false, 0);
    }

    txn_result txn_delivery(fast_random& r, int tid){
      // id index start from 1; inherited from Silo
      uint warehouse_id_start = (tid % NumWarehouses) + 1;
      uint warehouse_id_end = (tid % NumWarehouses) + 2;
      const uint warehouse_id = PickWarehouseId(r, warehouse_id_start, warehouse_id_end);
      const uint o_carrier_id = RandomNumber(r, 1, NumDistrictsPerWarehouse);
      const uint32_t ts = GetCurrentTimeMillis();

      errexit("txn_delivery not implemented!");
      return txn_result(true, 1);
    }

    txn_result txn_order_status(fast_random& r, int tid){
      // id index start from 1; inherited from Silo
      uint warehouse_id_start = (tid % NumWarehouses) + 1;
      uint warehouse_id_end = (tid % NumWarehouses) + 2;
      const uint warehouse_id = PickWarehouseId(r, warehouse_id_start, warehouse_id_end);
      const uint districtID = RandomNumber(r, 1, NumDistrictsPerWarehouse);

      errexit("txn_order_status not implemented!");
      return txn_result(true, 1);
    }
    txn_result txn_stock_level(fast_random& r, int tid){
      // id index start from 1; inherited from Silo
      uint warehouse_id_start = (tid % NumWarehouses) + 1;
      uint warehouse_id_end = (tid % NumWarehouses) + 2;
      errexit("txn_stock_level not implemented!");
      return txn_result(true, 1);
    }

    void cleanup(GlobalTestConfig* gtc){
      //TPCC_TABLE_LIST(TPCC_TABLE_SIZES)
      //tbl_order_line[0]->stats();
      TPCC_TABLE_LIST(TPCC_TABLE_CLEANUP)
        // tbl_order_line[0]->stats();
#ifdef TPCC_STATS
        std::cout << "puts=" << num_tpcc_puts()*1.0/gtc->total_operations << ", inserts=" << num_tpcc_inserts()*1.0/gtc->total_operations << ", gets=" << num_tpcc_gets()*1.0/gtc->total_operations << ", aborts=" << num_tpcc_aborts()*1.0/gtc->total_operations << std::endl;
#endif
    }

    inline std::atomic<size_t> &
    NewOrderIdHolder(unsigned warehouse, unsigned district)
    {
      assert(warehouse >= 1 && warehouse <= NumWarehouses);
      assert(district >= 1 && district <= NumDistrictsPerWarehouse);
      const unsigned idx =
        (warehouse - 1) * NumDistrictsPerWarehouse + (district - 1);
      return g_district_ids[idx].ui;
    }

    inline size_t
    FastNewOrderIdGen(unsigned warehouse, unsigned district)
    {
      return NewOrderIdHolder(warehouse, district).fetch_add(1, std::memory_order_acq_rel);
    }

  };
}; // namespace tpcc
#endif
