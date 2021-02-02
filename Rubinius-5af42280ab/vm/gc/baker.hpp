#ifndef RBX_VM_GC_BAKER_HPP
#define RBX_VM_GC_BAKER_HPP

#include <iostream>
#include <cstring>

#include "heap.hpp"
#include "gc/gc.hpp"
#include "gc/root.hpp"
#include "object_position.hpp"

#include "builtin/object.hpp"

#include "instruments/stats.hpp"

#include "call_frame_list.hpp"

#include "object_watch.hpp"

namespace rubinius {

  class ObjectMemory;
  class GCData;

  class BakerGC : public GarbageCollector {
  public:

    /* Fields */
    Heap heap_a;
    Heap heap_b;
    Heap *current;
    Heap *next;
    size_t lifetime;
    size_t total_objects;

    /* Inline methods */
    Object* allocate(size_t bytes, bool *collect_now) {
      Object* obj;

#ifdef RBX_GC_STATS
      stats::GCStats::get()->young_bytes_allocated += bytes;
      stats::GCStats::get()->allocate_young.start();
#endif

      /**
       * enough_space_p() 函数负责调查所申请大小(bytes)的分块在不在用于GC复制算法的内存空间(Heap)里
       */
      if(!current->enough_space_p(bytes)) {
#if 0
        if(!next->enough_space_p(bytes)) {
          return NULL;
        } else {
          total_objects++;
          obj = (Object*)next->allocate(bytes);
        }
#endif
        *collect_now = true;

#ifdef RBX_GC_STATS
      stats::GCStats::get()->allocate_young.stop();
#endif

        return NULL;
      } else {
        total_objects++;  // 正在分配的对象总数
        // 如果还有空闲空间，就调用Heap类的成员函数allocate(),执行分配
        obj = (Object*)current->allocate(bytes);
      }

      if(watched_p(obj)) {
        std::cout << "detected " << obj << " during baker allocation.\n";
      }
      
      /**
       * 如果函数成功地从Heap类返回了所分配的内存空间的地址，就初始化这个对象内的ObjectHeader的标记
       * 这次只执行分配，所以要设定InvalidType(无效型)
       * 然后把zone(所属世代)设置成YoundObjectZone(新生代)
       */
      obj->init_header(YoungObjectZone, InvalidType);

#ifdef RBX_GC_STATS
      stats::GCStats::get()->allocate_young.stop();
#endif

      return obj;
    }

  private:
    ObjectArray* promoted_;
    ObjectArray::iterator promoted_insert, promoted_current;

    // Assume ObjectArray is a vector!
    void promoted_push(Object* obj) {
      if(promoted_insert == promoted_current) {
        size_t i = promoted_insert - promoted_->begin(),
               j = promoted_current - promoted_->begin();
        promoted_->push_back(obj);
        promoted_current = promoted_->begin() + j;
        promoted_insert = promoted_->begin() + i;
      } else {
        *promoted_insert++ = obj;
      }
    }

  public:
    /* Prototypes */
    BakerGC(ObjectMemory *om, size_t size);
    virtual ~BakerGC();
    void free_objects();
    virtual Object* saw_object(Object* obj);
    void    copy_unscanned();
    bool    fully_scanned_p();
    void    collect(GCData& data);
    void    clear_marks();
    void    find_lost_souls();

    ObjectPosition validate_object(Object* obj);

  private:
    /* Private for inlining */
    Object*  next_object(Object* obj);
  };
};

#endif
