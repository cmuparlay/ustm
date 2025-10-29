#include "benchmark/glue/other_glue.h"
#include <iostream>
int main() {
  stm::atomic<int> a;
  tx_field<int> b; 
  std::cout << "atomic<int> size: " << sizeof(a) << std::endl;
  std::cout << "tx_field<int> size: " << sizeof(b) << std::endl;
  std::cout << "atomic<int> address: " << &a << std::endl;
  std::cout << "atomic<int> data address: " << &(a.data) << std::endl;
  std::cout << "tx_field<int> address: " << &b << std::endl;
  std::cout << "tx_field<int> data address: " << &(b.data) << std::endl;
  return 0;
}
