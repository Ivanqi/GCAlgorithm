/* The GC superclass methods, used by both GCs. */
#include "object_utils.hpp"
#include "gc/gc.hpp"

#include "objectmemory.hpp"

#include "gc/object_mark.hpp"

#include "builtin/class.hpp"
#include "builtin/tuple.hpp"
#include "builtin/module.hpp"
#include "builtin/symbol.hpp"
#include "builtin/compiledmethod.hpp"
#include "call_frame.hpp"
#include "builtin/variable_scope.hpp"
#include "builtin/staticscope.hpp"
#include "capi/handle.hpp"

namespace rubinius {

  GCData::GCData(STATE)
    : roots_(state->globals.roots)
    , call_frames_(state->shared.call_frame_locations())
    , variable_buffers_(*state->variable_buffers())
    , handles_(state->shared.global_handles())
    , cached_handles_(state->shared.cached_handles())
    , global_cache_(state->shared.global_cache)
  {}

  GarbageCollector::GarbageCollector(ObjectMemory *om)
                   :object_memory(om), weak_refs(NULL) { }

  /* Understands how to read the inside of an object and find all references
   * located within. It copies the objects pointed to, but does not follow into
   * those further (ie, not recursive)
   * 
   * 负责"复制指定对象的子对象"
   * */
  void GarbageCollector::scan_object(Object* obj) {
    Object* slot;

    if(watched_p(obj)) {
      std::cout << "detected " << obj << " during scan_object.\n";
    }

    /**
     * 复制所有种类的对象共同的子对象
     * 
     * klass() 和 ivars()分别是和成员函数klass_和ivars_相对应的getter
     * 这些成员函数是由ObjectHeader类定义的。因为所有对象类都继承了ObjectHeader类
     * 所以它们是所有对象共同的成员变量，并且这些成员变量里存有指向对象的指针
     * 
     * 首先以指向此对象的指针为参数调用saw_object()。如果被指定为参数的对象是新生代对象，就将其复制到To空间，返回目标空间地址
     * 如果是老年代对象，就不进行复制，直接返回参数值
     */
    if(obj->klass() && obj->klass()->reference_p()) {
      slot = saw_object(obj->klass());
      if(slot) object_memory->set_class(obj, slot);
    }

    if(obj->ivars() && obj->ivars()->reference_p()) {
      slot = saw_object(obj->ivars());
      if(slot) obj->ivars(object_memory->state, slot);  // 复制对象固有的子对象
    }

    // If this object's refs are weak, then add it to the weak_refs
    // vector and don't look at it otherwise.
    if(obj->refs_are_weak_p()) {
      if(!weak_refs) {
        weak_refs = new ObjectArray(0);
      }

      weak_refs->push_back(obj);
      return;
    }

    /**
     * 取出TypeInfo类的实例。所有对象都有继承了TypeInfo类的XX::Info类
     * 举个例子，Array就存在Array::Info类，这里就是取出实例
     */
    TypeInfo* ti = object_memory->type_info[obj->type_id()];
    assert(ti);

    /**
     * 生成ObjectMark类的实例
     * 大家请记住一点: 这里取的参数是this(GC类的实例)
     */
    ObjectMark mark(this);
    ti->mark(obj, mark);
  }

  void GarbageCollector::delete_object(Object* obj) {
    if(obj->requires_cleanup_p()) {
      object_memory->find_type_info(obj)->cleanup(obj);
    }

    if(obj->remembered_p()) {
      object_memory->unremember_object(obj);
    }
  }

  void GarbageCollector::saw_variable_scope(CallFrame* call_frame,
      StackVariables* scope)
  {
    scope->self_ = mark_object(scope->self());
    scope->block_ = mark_object(scope->block());
    scope->module_ = (Module*)mark_object(scope->module());

    int locals = call_frame->cm->backend_method_->number_of_locals;
    for(int i = 0; i < locals; i++) {
      Object* local = scope->get_local(i);
      if(local->reference_p()) {
        scope->set_local(i, mark_object(local));
      }
    }

    VariableScope* parent = scope->parent();
    if(parent) {
      scope->parent_ = (VariableScope*)mark_object(parent);
    }

    VariableScope* heap = scope->on_heap();
    if(heap) {
      scope->on_heap_ = (VariableScope*)mark_object(heap);
    }
  }

  void GarbageCollector::walk_call_frame(CallFrame* top_call_frame) {
    CallFrame* call_frame = top_call_frame;
    while(call_frame) {
      if(call_frame->custom_static_scope_p() &&
          call_frame->static_scope_ &&
          call_frame->static_scope_->reference_p()) {
        call_frame->static_scope_ =
          (StaticScope*)mark_object(call_frame->static_scope_);
      }

      if(call_frame->cm && call_frame->cm->reference_p()) {
        call_frame->cm = (CompiledMethod*)mark_object(call_frame->cm);
      }

      if(call_frame->cm && call_frame->stk) {
        native_int stack_size = call_frame->cm->stack_size()->to_native();
        for(native_int i = 0; i < stack_size; i++) {
          Object* obj = call_frame->stk[i];
          if(obj && obj->reference_p()) {
            call_frame->stk[i] = mark_object(obj);
          }
        }
      }

      if(call_frame->multiple_scopes_p() &&
          call_frame->top_scope_) {
        call_frame->top_scope_ = (VariableScope*)mark_object(call_frame->top_scope_);
      }

      if(call_frame->msg) {
        call_frame->msg->module = (Module*)mark_object(call_frame->msg->module);
        call_frame->msg->method = (Executable*)mark_object(call_frame->msg->method);
      }

      saw_variable_scope(call_frame, call_frame->scope);

      call_frame = static_cast<CallFrame*>(call_frame->previous);
    }
  }

  void GarbageCollector::visit_variable_scope(CallFrame* call_frame,
      StackVariables* scope, ObjectVisitor& visit)
  {

    scope->self_ = visit.call(scope->self());
    scope->block_ = visit.call(scope->block());
    scope->module_ = (Module*)visit.call(scope->module());

    int locals = call_frame->cm->backend_method_->number_of_locals;

    for(int i = 0; i < locals; i++) {
      Object* local = scope->get_local(i);
      if(local->reference_p()) {
        scope->set_local(i, visit.call(local));
      }
    }

    VariableScope* parent = scope->parent();
    if(parent && parent->reference_p()) {
      scope->parent_ = ((VariableScope*)visit.call(parent));
    }

    VariableScope* on_heap = scope->on_heap();
    if(on_heap) {
      scope->on_heap_ = ((VariableScope*)visit.call(on_heap));
    }
  }

  void GarbageCollector::visit_call_frame(CallFrame* top_call_frame, ObjectVisitor& visit) {
    CallFrame* call_frame = top_call_frame;
    while(call_frame) {
      if(call_frame->custom_static_scope_p() &&
          call_frame->static_scope_ &&
          call_frame->static_scope_->reference_p()) {
        call_frame->static_scope_ =
          (StaticScope*)visit.call(call_frame->static_scope_);
      }

      if(call_frame->cm && call_frame->cm->reference_p()) {
        call_frame->cm = (CompiledMethod*)visit.call(call_frame->cm);
      }

      if(call_frame->cm && call_frame->stk) {
        native_int stack_size = call_frame->cm->stack_size()->to_native();
        for(native_int i = 0; i < stack_size; i++) {
          Object* obj = call_frame->stk[i];
          if(obj && obj->reference_p()) {
            call_frame->stk[i] = visit.call(obj);
          }
        }
      }

      if(call_frame->multiple_scopes_p() &&
          call_frame->top_scope_) {
        call_frame->top_scope_ = (VariableScope*)visit.call(call_frame->top_scope_);
      }

      visit_variable_scope(call_frame, call_frame->scope, visit);

      call_frame = static_cast<CallFrame*>(call_frame->previous);
    }
  }

  void GarbageCollector::visit_roots(Roots& roots, ObjectVisitor& visit) {
    Root* root = static_cast<Root*>(roots.head());
    while(root) {
      Object* tmp = root->get();
      if(tmp->reference_p()) {
        visit.call(tmp);
      }

      root = static_cast<Root*>(root->next());
    }
  }

  void GarbageCollector::visit_call_frames_list(CallFrameLocationList& call_frames,
      ObjectVisitor& visit) {

    // Walk all the call frames
    for(CallFrameLocationList::const_iterator i = call_frames.begin();
        i != call_frames.end();
        i++) {
      CallFrame** loc = *i;
      visit_call_frame(*loc, visit);
    }
  }

  class UnmarkVisitor : public ObjectVisitor {
    std::vector<Object*> stack_;
    ObjectMemory* object_memory_;

  public:

    UnmarkVisitor(ObjectMemory* om)
      : object_memory_(om)
    {}

    Object* call(Object* obj) {
      if(watched_p(obj)) {
        std::cout << "detected " << obj << " during unmarking.\n";
      }

      if(obj->reference_p() && obj->marked_p()) {
        obj->clear_mark();
        stack_.push_back(obj);
      }

      return obj;
    }

    /* Understands how to read the inside of an object and find all references
     * located within. It copies the objects pointed to, but does not follow into
     * those further (ie, not recursive) */
    void visit_object(Object* obj) {
      if(obj->klass() && obj->klass()->reference_p()) {
        call(obj->klass());
      }

      if(obj->ivars() && obj->ivars()->reference_p()) {
        call(obj->ivars());
      }

      TypeInfo* ti = object_memory_->type_info[obj->type_id()];
      assert(ti);

      ti->visit(obj, *this);
    }

    void drain_stack() {
      while(!stack_.empty()) {
        Object* obj = stack_.back();
        stack_.pop_back();

        if(watched_p(obj)) {
          std::cout << "detected " << obj << " in unmarking stack.\n";
        }

        visit_object(obj);
      }
    }
  };

  void GarbageCollector::unmark_all(GCData& data) {
    UnmarkVisitor visit(object_memory);

    visit_roots(data.roots(), visit);
    visit_call_frames_list(data.call_frames(), visit);

    for(capi::Handles::Iterator i(*data.handles()); i.more(); i.advance()) {
      visit.call(i->object());
    }

    for(capi::Handles::Iterator i(*data.cached_handles()); i.more(); i.advance()) {
      visit.call(i->object());
    }

    visit.drain_stack();
  }

  void GarbageCollector::clean_weakrefs(bool check_forwards) {
    if(!weak_refs) return;

    for(ObjectArray::iterator i = weak_refs->begin();
        i != weak_refs->end();
        i++) {
      // ATM, only a Tuple can be marked weak.
      Tuple* tup = as<Tuple>(*i);
      for(size_t ti = 0; ti < tup->num_fields(); ti++) {
        Object* obj = tup->at(object_memory->state, ti);

        if(!obj->reference_p()) continue;

        if(check_forwards) {
          if(obj->young_object_p()) {
            if(!obj->forwarded_p()) {
              tup->field[ti] = Qnil;
            } else {
              tup->field[ti] = obj->forward();
              tup->write_barrier(object_memory->state, obj->forward());
            }
          }
        } else if(!obj->marked_p()) {
          tup->field[ti] = Qnil;
        }
      }
    }

    delete weak_refs;
    weak_refs = NULL;
  }
}
