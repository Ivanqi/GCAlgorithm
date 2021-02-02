#ifndef RBX_OBJECTMEMORY_H
#define RBX_OBJECTMEMORY_H

#include "gc/marksweep.hpp"
#include "gc/baker.hpp"
#include "gc/immix.hpp"

#include "prelude.hpp"
#include "type_info.hpp"

#include "object_position.hpp"

#include "call_frame_list.hpp"

namespace rubinius {

  class Object;

  /* ObjectMemory is the primary API that the rest of the VM uses to interact
   * with actions such as allocating objects, storing data in objects, and
   * perform garbage collection.
   *
   * It is current split between 2 generations, the BakerGC, which handles
   * the young objects, and the MarkSweepGC, which handles the mature.
   *
   * Basic tasks:
   *
   * Allocate an object of a given class and number of fields.
   *   If the object is large, it's put to start in the mature space,
   *   otherwise in the young space.
   *
   * Detection of memory condition requiring collection of both generations
   *   independently.
   *
   */

  class CallFrame;
  class GCData;
  class Configuration;

  class ObjectMemory {
  public:

    bool collect_young_now;
    bool collect_mature_now;

    STATE;
    ObjectArray *remember_set;
    BakerGC young;
    MarkSweepGC mark_sweep_;

    ImmixGC immix_;

    size_t last_object_id;
    TypeInfo* type_info[(int)LastObjectType];

    /* Config variables */
    size_t large_object_threshold;

    ObjectMemory(STATE, Configuration& config);
    ~ObjectMemory();

    void remember_object(Object* target);
    void unremember_object(Object* target);

    void store_object(Object* target, size_t index, Object* val);
    void set_class(Object* target, Object* obj);

    Object* new_object_typed(Class* cls, size_t bytes, object_type type);
    Object* new_object_typed_mature(Class* cls, size_t bytes, object_type type);
    Object* new_object_typed_enduring(Class* cls, size_t bytes, object_type type);

    template <class T>
      T* new_object_bytes(Class* cls, size_t& bytes) {
        bytes = ObjectHeader::align(sizeof(T) + bytes);
        T* obj = reinterpret_cast<T*>(new_object_typed(cls, bytes, T::type));

        return obj;
      }

    template <class T>
      T* new_object_bytes_mature(Class* cls, size_t& bytes) {
        bytes = ObjectHeader::align(sizeof(T) + bytes);
        T* obj = reinterpret_cast<T*>(new_object_typed_mature(cls, bytes, T::type));

        return obj;
      }

    template <class T>
      T* new_object_variable(Class* cls, size_t fields, size_t& bytes) {
        bytes = sizeof(T) + (fields * sizeof(Object*));
        return reinterpret_cast<T*>(new_object_typed(cls, bytes, T::type));
      }

    template <class T>
      T* new_object_enduring(Class* cls) {
        return reinterpret_cast<T*>(
            new_object_typed_enduring(cls, sizeof(T), T::type));
      }

    TypeInfo* find_type_info(Object* obj);
    void set_young_lifetime(size_t age);
    void collect_young(GCData& data);
    void collect_mature(GCData& data);
    Object* promote_object(Object* obj);
    bool valid_object_p(Object* obj);
    void debug_marksweep(bool val);
    void add_type_info(TypeInfo* ti);

    void prune_handles(capi::Handles* handles, bool check_forwards);

    ObjectPosition validate_object(Object* obj);

    /**
     * target: 发出引用的对象
     * val: 引用的目标对象 
     * 
     * 只要下面的情况有一个符合，target就不会被记录到记录集中
     * 也就是说，这些if语句是一项检查处理，用于弹开那些不能写入屏障对象的对象
     */
    void write_barrier(Object* target, Object* val) {
      // 发出引用的对象是否已经记录在记录集里了？
      if(target->remembered_p()) return;
      // 引用的目标对象是否为指针(是否为内嵌对象)
      if(!REFERENCE_P(val)) return;
      // 发出引用的对象是否为新生代对象?
      if(target->zone == YoungObjectZone) return;
      // 发出引用的对象是否为新生代对象
      if(val->zone != YoungObjectZone) return;

      // 调用remember_object()，来将指针记录到记录集里
      remember_object(target);
    }

    // This only has one use! Don't use it!
    Object* allocate_object_raw(size_t bytes);

  private:
    Object* allocate_object(size_t bytes);
    Object* allocate_object_mature(size_t bytes);
  };

#define FREE(obj) free(obj)
#define ALLOC_N(type, size) ((type*)calloc((size), sizeof(type)))
#define ALLOC(t) (t*)XMALLOC(sizeof(t))
#define REALLOC_N(v,t,n) (v)=(t*)realloc((void*)(v), sizeof(t)*n)

#define ALLOCA_N(type, size) ((type*)alloca(sizeof(type) * (size)))
#define ALLOCA(type) ((type*)alloca(sizeof(type)))

};


extern "C" {
  void* XMALLOC(size_t bytes);
  void  XFREE(void* ptr);
  void* XREALLOC(void* ptr, size_t bytes);
  void* XCALLOC(size_t items, size_t bytes);
}

#endif
