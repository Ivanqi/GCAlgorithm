#include <cstdlib>
#include <iostream>

#include "gc/baker.hpp"
#include "objectmemory.hpp"
#include "object_utils.hpp"

#include "builtin/tuple.hpp"

#include "instruments/stats.hpp"

#include "call_frame.hpp"

#include "gc/gc.hpp"

#include "capi/handle.hpp"

namespace rubinius {
  /**
   * 在GC复制算法中，需要两个内存空间，这两个内存空间就是heap_a和heap_b
   * 这里将执行分配的内存空间(From空间)的地址分配给current，在GC的时候把作为对象目标空间的内存空间(To空间)的地址分配给next
   */
  BakerGC::BakerGC(ObjectMemory *om, size_t bytes) :
    GarbageCollector(om),
    heap_a(bytes),
    heap_b(bytes),
    total_objects(0),
    promoted_(0)
  {
    current = &heap_a;
    next = &heap_b;
  }

  BakerGC::~BakerGC() { }

  Object* BakerGC::saw_object(Object* obj) {
    Object* copy;

    if(watched_p(obj)) {
      std::cout << "detected " << obj << " during baker collection\n";
    }

    /**
     * forwarded_p()负责返回是否设定了forwarding指针
     * 如果条件为真, forwarded_p()就会返回obj的forwarding指针
     */
    if(!obj->reference_p()) return obj;

    if(obj->zone != YoungObjectZone) return obj;

    if(obj->forwarded_p()) return obj->forward();

    // This object is already in the next space, we don't want to
    // copy it again!
    // TODO test this!
    /**
     * next值得是To空间，如果obj为To空间内的对象，就不进行复制
     * 这样一来obj就会被原样返回
     */
    if(next->contains_p(obj)) return obj;

    /**
     * obj->age负责计算obj的年龄，这里的年龄指的是obj当过多少次GC复制算法(新生代GC)的对象
     * 当age(年龄)为1时，就表示这个对象过去只当过一次GC复制算法的对象
     * 
     * lifetime是视为新生代的年龄的阀值。一旦age超过lifetime,这个对象就到了老年代的年龄，必须将其移动(晋升)到老年代空间
     */
    if(unlikely(obj->age++ >= lifetime)) {
      copy = object_memory->promote_object(obj);  // 负责对象移动(晋升)到老年空间

      promoted_push(copy);  // 将其指针记录在晋升链表中
    } else if(likely(next->enough_space_p(obj->size_in_bytes(object_memory->state)))) { // 检查To空间里还有没有可以复制的空闲空间
      /**
       * size_in_bytes() 负责返回对象参数的大小
       * enough_space_p() 负责调查还有没有指定大小的空闲空间
       */
      copy = next->copy_object(object_memory->state, obj);  // copy_object() 执行对象复制
      total_objects++;
    } else {  // 如果To空间里已经没有空闲空间了，那就只好让对象晋升了
      copy = object_memory->promote_object(obj);
      promoted_push(copy);
    }

    if(watched_p(copy)) {
      std::cout << "detected " << copy << " during baker collection (2)\n";
    }

    /**
     * obj就已经复制到To空间或老年代空间了
     * 在这里将目标空间的地址作为forwarding指针，设定给原空间的对象
     */
    obj->set_forward(copy);
    return copy;
  }

  /**
   * 复制搜素To空间里那些未搜索的对象
   */
  void BakerGC::copy_unscanned() {
    // next_unscanned()函数负责返回指向下一个未搜索对象的指针。如果没有未搜索的对象，那么函数就会返回NULL
    Object* iobj = next->next_unscanned(object_memory->state);

    while(iobj) {
      assert(iobj->zone == YoungObjectZone);
      if(!iobj->forwarded_p()) scan_object(iobj);
      iobj = next->next_unscanned(object_memory->state);
    }
  }

  bool BakerGC::fully_scanned_p() {
    return next->fully_scanned_p();
  }

  /* Perform garbage collection on the young objects. */
  void BakerGC::collect(GCData& data) {
#ifdef RBX_GC_STATS
    stats::GCStats::get()->bytes_copied.start();
    stats::GCStats::get()->objects_copied.start();
    stats::GCStats::get()->objects_promoted.start();
    stats::GCStats::get()->collect_young.start();
#endif
    // (1) 搜索从记录集引用的对象
    Object* tmp;
    // 负责把指向现在的记录集的指针存入局部变量current_rs
    ObjectArray *current_rs = object_memory->remember_set;

    /**
     * 把ObjectArray类的实例设为new,把这个实例作为新的记录集存入object_memory->remember中
     * ObjectArray是以Object元素的vector类(动态数据)的别名
     */
    object_memory->remember_set = new ObjectArray(0); // 已晋升对象的动态数组
    total_objects = 0;

    // Tracks all objects that we promoted during this run, so
    // we can scan them at the end.
    promoted_ = new ObjectArray(0);

    promoted_current = promoted_insert = promoted_->begin();

    /**
     * 按顺序取出记录集里的对象的指针
     */
    for(ObjectArray::iterator oi = current_rs->begin();
        oi != current_rs->end();
        ++oi) {
      tmp = *oi;
      // unremember_object throws a NULL in to remove an object
      // so we don't have to compact the set in unremember
      if(tmp) {
        assert(tmp->zone == MatureObjectZone);
        assert(!tmp->forwarded_p());

        /**
         * Remove the Remember bit, since we're clearing the set.
         * clear_remember()将对象的标记内的Remember位设为0
         * */
        tmp->clear_remember();
        /**
         * scan_object()函数搜索指定的对象，把对象内的子对象复制到To空间
         */
        scan_object(tmp);
      }
    }

    delete current_rs;
    
    // (2). 复制从根引用的对象
    /**
     * 内置类、模块、符号
     * data.roots为双向链表，元素内存有指向内置类的指针，实际负责取出这些指针的是函数get()
     * reference_p()负责检查指针是否为内嵌对象
     * young_object_p() 负责检查指针所指的对象是否在新生代空间
     **/
    for(Roots::Iterator i(data.roots()); i.more(); i.advance()) {
      tmp = i->get();
      if(tmp->reference_p() && tmp->young_object_p()) {
        /**
         * 复制对象的操作是由成员函数saw_object()执行的
         * saw_object()成功复制完对象后会返回目标空间的地址
         * 如果对象已经被复制了，那么saw_object()就会返回forwarding指针(指向目标空间的指针)
         * 在调用完saw_object()后，成员函数set()把返回的目标空间地址设定为根。也就是说，在这里执行了根的重写操作
         */
        i->set(saw_object(tmp));
      }
    }

    for(capi::Handles::Iterator i(*data.handles()); i.more(); i.advance()) {
      if(!i->weak_p() && i->object()->young_object_p()) {
        i->set_object(saw_object(i->object()));
      }

      assert(i->object()->type_id() != InvalidType);
    }

    for(capi::Handles::Iterator i(*data.cached_handles()); i.more(); i.advance()) {
      if(!i->weak_p() && i->object()->young_object_p()) {
        i->set_object(saw_object(i->object()));
      }

      assert(i->object()->type_id() != InvalidType);
    }

    for(VariableRootBuffers::Iterator i(data.variable_buffers());
        i.more(); i.advance()) {
      Object*** buffer = i->buffer();
      for(int idx = 0; idx < i->size(); idx++) {
        Object** var = buffer[idx];
        Object* tmp = *var;

        if(tmp->reference_p() && tmp->young_object_p()) {
          *var = saw_object(tmp);
        }
      }
    }

    // Walk all the call frames
    for(CallFrameLocationList::iterator i = data.call_frames().begin();
        i != data.call_frames().end();
        i++) {
      CallFrame** loc = *i;
      walk_call_frame(*loc);
    }

    /* Ok, now handle all promoted objects. This is setup a little weird
     * so I should explain.
     *
     * We want to scan each promoted object. But this scanning will likely
     * cause more objects to be promoted. Adding to an ObjectArray that your
     * iterating over blows up the iterators, so instead we rotate the
     * current promoted set out as we iterator over it, and stick an
     * empty ObjectArray in.
     *
     * This way, when there are no more objects that are promoted, the last
     * ObjectArray will be empty.
     * */
    // (3) 搜索复制完毕的对象

    /**
     * promoted_current: 指示搜索位置
     * promoted_insert: 指示保存着指向下一个晋升对象的指针的场所
     */
    promoted_current = promoted_insert = promoted_->begin();

    /**
     * 这个while循环的延续条件是"有未搜索的已晋升对象" 或 "To空间里有未搜索的对象"。也就是说，只要搜索完所有已经复制的对象，这个循环就停止了
     * 
     * 为什么处理得这么复杂？原因就是内存的使用效率
     *  如果在搜索对象的过程中其子对象发生了晋升，倒是可以把对象的指针追加到动态数组的末尾，不过这样一类，就变成了晋升和搜索的死循环，可能会造成巨大的动态数据
     *  因此这里采用了另一种方法，就是重新利用那些已经搜索完毕的数组元素
     */
    while(promoted_->size() > 0 || !fully_scanned_p()) {
      if(promoted_->size() > 0) {
        /**
         * for 循环执行的操作是搜索所有已经从From空间晋升的对象
         */
        for(;promoted_current != promoted_->end();
            ++promoted_current) {
          tmp = *promoted_current;
          assert(tmp->zone == MatureObjectZone);
          scan_object(tmp); // 用来执行搜索
          if(watched_p(tmp)) {
            std::cout << "detected " << tmp << " during scan of promoted objects.\n";
          }
        }

        /**
         * 通过promoted_insert的位置重写调整动态数组的大小
         * 然后promoted_current = promoted_insert指向位置重合，为下一项处理做准备
         */
        promoted_->resize(promoted_insert - promoted_->begin());
        promoted_current = promoted_insert = promoted_->begin();

      }

      /* As we're handling promoted objects, also handle unscanned objects.
       * Scanning these unscanned objects (via the scan pointer) will
       * cause more promotions. */
      /**
       * 搜索To空间里未搜索的对象
       * 在搜索To空间的对象的过程中，可能有对象已经晋升了。这种情况下while循环是不会结束的，要从头来过
       * 像这样，程序会把所有的活动对象复制到To空间或老年代空间
       * 此外，因为调用copy_unscanned()函数的时候promoted_insert和promoted_current指着同一个位置，所以已经晋升的对象会被追加到promoted_末尾
       * */
      copy_unscanned();
    }

    assert(promoted_->size() == 0);

    delete promoted_;
    promoted_ = NULL;

    assert(fully_scanned_p());

    /* Another than is going to be found is found now, so we go back and
     * look at everything in current and call delete_object() on anything
     * thats not been forwarded. */

    // (4) 垃圾对象的后处理
    find_lost_souls();

    /* Check any weakrefs and replace dead objects with nil*/
    clean_weakrefs(true);

    /* Swap the 2 halves */
    Heap *x = next;
    next = current;
    current = x;
    next->reset();

#ifdef RBX_GC_STATS
    stats::GCStats::get()->collect_young.stop();
    stats::GCStats::get()->objects_copied.stop();
    stats::GCStats::get()->objects_promoted.stop();
    stats::GCStats::get()->bytes_copied.stop();
#endif
  }

  inline Object * BakerGC::next_object(Object * obj) {
    return reinterpret_cast<Object*>(reinterpret_cast<uintptr_t>(obj) +
      obj->size_in_bytes(object_memory->state));
  }

  void BakerGC::clear_marks() {
    Object* obj = current->first_object();
    while(obj < current->current) {
      obj->clear_mark();
      obj = next_object(obj);
    }

    obj = next->first_object();
    while(obj < next->current) {
      obj->clear_mark();
      obj = next_object(obj);
    }
  }

  void BakerGC::free_objects() {
    Object* obj = current->first_object();
    while(obj < current->current) {
      delete_object(obj);
      obj = next_object(obj);
    }

    assert(next->current < next->last);
    obj = next->first_object();
    while(obj < next->current) {
      delete_object(obj);
      obj = next_object(obj);
    }
  }

  /**
   * find_lost_souls()函数被用于执行垃圾对象的后处理，那么后处理又是什么？
   *  根据对象的种类不同，有些对象持有GC对象范围之外的内存空间
   *  简单来说，这些空间就是用malloc()等分配的VM Heap范围外的内存空间
   *  因为这些内存空间并没有分配到From空间，所以自然不能通过GC复制算法将其重新利用
   *  当对象成了垃圾时，如果不明确释放内存的话，就会产生内存泄漏
   *  
   *  执行释放操作的函数正是find_lost_souls()。也就是说，在这里我们将位于GC对象范围外的那些不能再次利用的内存空间称为“失落的灵魂”
   */
  void BakerGC::find_lost_souls() {
    Object* obj = current->first_object();
    while(obj < current->current) {
      /**
       * 调用obj的forwarded_p()。因为没有forwarding指针，说明这个对象没有被复制，所以计算机将其视为垃圾对象
       */
      if(!obj->forwarded_p()) {
        /**
         * 虽然delete_object()中调用的是每个对象的成员函数cleanup()，但实现了这个函数的只有Regexp类和Bitnum类
         * 除此之外的类的对象都不会出现"失落的灵魂"
         */
        delete_object(obj);

#ifdef RBX_GC_STATS
        stats::GCStats::get()->lifetimes[obj->age]++;
#endif
      }
      obj = next_object(obj);
    }
  }

  ObjectPosition BakerGC::validate_object(Object* obj) {
    if(current->contains_p(obj)) {
      return cValid;
    } else if(next->contains_p(obj)) {
      return cInWrongYoungHalf;
    } else {
      return cUnknown;
    }
  }
}
