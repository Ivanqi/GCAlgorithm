#ifndef RBX_VM_HEAP

#include "builtin/object.hpp"

namespace rubinius {
  typedef void *address;
  class Object;
  class VM;

  class Heap {
    /* Fields */

  public:
    address start;    // start用于GC复制算法的内容空间的初始地址
    address current;  // current是分块的初始地址
    address last;     // 结束地址
    address scan;     // scan指的是下一个要查找的内存空间的地址

    size_t size;

    /* Inline methods */
    /**
     * 将Heap类的成员变量current设定给addr，把current偏移size大小(分配的大小)，之后只要返回addr就行了
     */
    address allocate(size_t size) {
      address addr;
      addr = current;
      current = (address)((uintptr_t)current + size);

      return addr;
    }

    void put_back(size_t size) {
      current = (address)((uintptr_t)current - size);
    }

    bool contains_p(address addr) {
      if(addr < start) return false;
      if(addr >= last) return false;
      return true;
    }

    bool enough_space_p(size_t size) {
      if((uintptr_t)current + size > (uintptr_t)last) return false;
      return true;
    }

    bool fully_scanned_p() {
      return scan == current;
    }

    Object* next_unscanned(VM* state) {
      Object* obj;
      if(fully_scanned_p()) return NULL;

      obj = (Object*)scan;
      scan = (address)((uintptr_t)scan + obj->size_in_bytes(state));
      return obj;
    }

    Object* first_object() {
      return (Object*)start;
    }

    // Set the scan pointer to +addr+
    void set_scan(address addr) {
      scan = addr;
    }

    /* Prototypes */
    Heap(size_t size);
    ~Heap();
    void reset();
    size_t remaining();
    size_t used();
    Object* copy_object(VM* state, Object*);
  };

}

#endif
