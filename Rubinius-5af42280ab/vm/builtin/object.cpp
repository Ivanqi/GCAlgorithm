#include <iostream>
#include <sstream>

#include <cstdarg>

#include "builtin/object.hpp"
#include "builtin/bignum.hpp"
#include "builtin/class.hpp"
#include "builtin/compactlookuptable.hpp"
#include "builtin/fixnum.hpp"
#include "builtin/lookuptable.hpp"
#include "builtin/symbol.hpp"
#include "builtin/string.hpp"
#include "builtin/tuple.hpp"
#include "builtin/array.hpp"
#include "builtin/selector.hpp"
#include "builtin/sendsite.hpp"
#include "builtin/float.hpp"
#include "builtin/staticscope.hpp"
#include "builtin/system.hpp"
#include "builtin/methodtable.hpp"

#include "objectmemory.hpp"
#include "arguments.hpp"
#include "dispatch.hpp"
#include "lookup_data.hpp"
#include "primitives.hpp"

#include "vm/object_utils.hpp"

namespace rubinius {

  Object* Object::change_class_to(STATE, Class* other_klass) {
    this->klass(state, other_klass);

    return this;
  }

  Class* Object::class_object(STATE) const {
    if(reference_p()) {
      Module* mod = klass_;
      while(!mod->nil_p() && !instance_of<Class>(mod)) {
        mod = as<Module>(mod->superclass());
      }

      if(mod->nil_p()) {
        Exception::assertion_error(state, "Object::class_object() failed to find a class");
      }
      return as<Class>(mod);
    }

    return state->globals.special_classes[((uintptr_t)this) & SPECIAL_CLASS_MASK].get();
  }

  void Object::cleanup(STATE) {
    type_info(state)->cleanup(this);
  }

  Object* Object::duplicate(STATE) {
    if(!reference_p()) return this;

    Object* other = state->new_object_typed(
        class_object(state), this->total_size(state), obj_type_);
    return other->copy_object(state, this);
  }

  Object* Object::copy_metaclass(STATE, Object* other) {
    if(MetaClass* mc = try_as<MetaClass>(other->klass())) {
      MethodTable* source_methods = mc->method_table()->duplicate(state);
      LookupTable* source_constants = mc->constants()->duplicate(state);

      metaclass(state)->method_table(state, source_methods);
      metaclass(state)->constants(state, source_constants);
      // TODO inc the global serial here?

      // This allows us to preserve included modules
      metaclass(state)->superclass(state, mc->superclass());
    }

    return this;
  }

  Object* Object::copy_object_prim(STATE, Object* other, CallFrame* call_frame) {
    if(type_id() != other->type_id() ||
        class_object(state) != other->class_object(state)) {
      Exception* exc =
        Exception::make_type_error(state, type_id(), other);
      exc->locations(state, System::vm_backtrace(state, Fixnum::from(0), call_frame));
      state->thread_state()->raise_exception(exc);
      return NULL;
    }

    return copy_object(state, other);
  }

  Object* Object::copy_object(STATE, Object* other) {
    initialize_copy(other, age);

    /* C extensions use Data objects for various purposes. The object
     * usually is made an instance of some extension class. So, we
     * have to check the object type to ensure we don't clobber the
     * data caried in the new instance.
     */
    if(type_id() != DataType) {
      copy_body(state, other);
    }

    // Ensure that the metaclass is not shared
    klass(state, other->class_object(state));

    // HACK: If other is mature, remember it.
    // We could inspect inspect the references we just copied to see
    // if there are any young ones if other is mature, then and only
    // then remember other. The up side to just remembering it like
    // this is that other is rarely mature, and the remember_set is
    // flushed on each collection anyway.
    if(zone == MatureObjectZone) {
      state->om->remember_object(this);
    }

    // Copy ivars.
    if(other->ivars_->reference_p()) {
      // NOTE Don't combine these 2 branches even though they both just call
      // ::copy. There is a special LookupTable::copy that can only be seen
      // when the receiver is of LookupTable* type. Without the explicit cast
      // and call, the wrong one will be called.
      if(LookupTable* lt = try_as<LookupTable>(other->ivars_)) {
        ivars_ = lt->duplicate(state);
        LookupTable* ld = as<LookupTable>(ivars_);

        // We store the object_id in the ivar table, so nuke it.
        ld->remove(state, G(sym_object_id));
        ld->remove(state, state->symbol("frozen"));
        ld->remove(state, state->symbol("capi_handle"));
      } else {
        // Use as<> so that we throw a TypeError if there is something else
        // here.
        CompactLookupTable* clt = as<CompactLookupTable>(other->ivars_);
        ivars_ = clt->duplicate(state);
        CompactLookupTable* ld = as<CompactLookupTable>(ivars_);

        // We store the object_id in the ivar table, so nuke it.
        ld->remove(state, G(sym_object_id));
        ld->remove(state, state->symbol("frozen"));
        ld->remove(state, state->symbol("capi_handle"));
      };
    }

    return this;
  }

  Object* Object::equal(STATE, Object* other) {
    return this == other ? Qtrue : Qfalse;
  }

  Object* Object::freeze(STATE) {
    if(reference_p()) {
      set_ivar(state, state->symbol("frozen"), Qtrue);
    }
    return this;
  }

  Object* Object::frozen_p(STATE) {
    if(reference_p()) {
      if(get_ivar(state, state->symbol("frozen"))->nil_p()) return Qfalse;
      return Qtrue;
    } else {
      return Qfalse;
    }
  }

  Object* Object::get_field(STATE, size_t index) {
    return type_info(state)->get_field(state, this, index);
  }

  Object* Object::get_table_ivar(STATE, Symbol* sym) {
    if(CompactLookupTable* tbl = try_as<CompactLookupTable>(ivars_)) {
      return tbl->fetch(state, sym);
    } else if(LookupTable* tbl = try_as<LookupTable>(ivars_)) {
      return tbl->fetch(state, sym);
    }

    return Qnil;
  }

  Object* Object::get_ivar_prim(STATE, Symbol* sym) {
    if(sym->is_ivar_p(state)->false_p()) {
      return reinterpret_cast<Object*>(kPrimitiveFailed);
    }

    return get_ivar(state, sym);
  }

  Object* Object::get_ivar(STATE, Symbol* sym) {
    /* Implements the external ivars table for objects that don't
       have their own space for ivars. */
    if(!reference_p()) {
      LookupTable* tbl = try_as<LookupTable>(G(external_ivars)->fetch(state, this));

      if(tbl) return tbl->fetch(state, sym);
      return Qnil;
    }

    // We might be trying to access a slot, so try that first.

    TypeInfo* ti = state->om->find_type_info(this);
    if(ti) {
      TypeInfo::Slots::iterator it = ti->slots.find(sym->index());
      if(it != ti->slots.end()) {
        return ti->get_field(state, this, it->second);
      }
    }

    return get_table_ivar(state, sym);
  }

  /*
   * Returns a LookupTable or a CompactLookupTable.  Below a certain number of
   * instance variables a CompactTable is used to save memory.  See
   * Object::get_ivar for how to fetch an item out of get_ivars depending upon
   * storage type.
   */
  Object* Object::get_ivars(STATE) {
    if(!reference_p()) {
      LookupTable* tbl = try_as<LookupTable>(G(external_ivars)->fetch(state, this));

      if(tbl) return tbl;
      return Qnil;
    }

    return ivars_;
  }

  object_type Object::get_type() const {
    if(reference_p()) return type_id();
    if(fixnum_p()) return FixnumType;
    if(symbol_p()) return SymbolType;
    if(nil_p()) return NilType;
    if(true_p()) return TrueType;
    if(false_p()) return FalseType;
    return ObjectType;
  }

  hashval Object::hash(STATE) {
    if(!reference_p()) {
#ifdef _LP64
      uintptr_t key = reinterpret_cast<uintptr_t>(this);
      key = (~key) + (key << 21); // key = (key << 21) - key - 1;
      key = key ^ (key >> 24);
      key = (key + (key << 3)) + (key << 8); // key * 265
      key = key ^ (key >> 14);
      key = (key + (key << 2)) + (key << 4); // key * 21
      key = key ^ (key >> 28);
      key = key + (key << 31);
      return key & FIXNUM_MAX;
#else
      // See http://burtleburtle.net/bob/hash/integer.html
      uint32_t a = (uint32_t)this;
      a = (a+0x7ed55d16) + (a<<12);
      a = (a^0xc761c23c) ^ (a>>19);
      a = (a+0x165667b1) + (a<<5);
      a = (a+0xd3a2646c) ^ (a<<9);
      a = (a+0xfd7046c5) + (a<<3);
      a = (a^0xb55a4f09) ^ (a>>16);
      return a & FIXNUM_MAX;
#endif
    } else {
      if(String* string = try_as<String>(this)) {
        return string->hash_string(state);
      } else if(Bignum* bignum = try_as<Bignum>(this)) {
        return bignum->hash_bignum(state);
      } else if(Float* flt = try_as<Float>(this)) {
        return String::hash_str((unsigned char *)(&(flt->val)), sizeof(double));
      } else {
        return id(state)->to_native();
      }
    }
  }

  Integer* Object::hash_prim(STATE) {
    return Integer::from(state, hash(state));
  }

  Integer* Object::id(STATE) {
    if(reference_p()) {
      Object* id = get_ivar(state, G(sym_object_id));

      /* Lazy allocate object's ids, since most don't need them. */
      if(id->nil_p()) {
        /* All references have an even object_id. last_object_id starts out at 0
         * but we don't want to use 0 as an object_id, so we just add before using */
        id = Fixnum::from(state->om->last_object_id += 2);
        set_ivar(state, G(sym_object_id), id);
      }

      return as<Integer>(id);
    } else {
      /* All non-references have an odd object_id */
      return Fixnum::from(((uintptr_t)this << 1) | 1);
    }
  }

  void Object::infect(STATE, Object* other) {
    if(this->tainted_p(state) == Qtrue) {
      other->taint(state);
    }
  }

  bool Object::kind_of_p(STATE, Object* module) {
    Module* found = NULL;

    if(!reference_p()) {
      found = state->globals.special_classes[((uintptr_t)this) & SPECIAL_CLASS_MASK].get();
    } else {
      found = try_as<Module>(klass_);
    }

    while(found) {
      if(found == module) return true;

      if(IncludedModule* im = try_as<IncludedModule>(found)) {
        if(im->module() == module) return true;
      }

      found = try_as<Module>(found->superclass());
    }

    return false;
  }

  Object* Object::kind_of_prim(STATE, Module* klass) {
    return kind_of_p(state, klass) ? Qtrue : Qfalse;
  }

  Class* Object::metaclass(STATE) {
    if(reference_p()) {
      if(kind_of<MetaClass>(klass_)) {
        return as<MetaClass>(klass_);
      }
      return MetaClass::attach(state, this);
    }

    return class_object(state);
  }

  Object* Object::send(STATE, CallFrame* caller, Symbol* name, Array* ary,
      Object* block, bool allow_private) {
    LookupData lookup(this, this->lookup_begin(state), allow_private);
    Dispatch dis(name);

    Arguments args(ary);
    args.set_block(block);
    args.set_recv(this);

    return dis.send(state, caller, lookup, args);
  }

  Object* Object::send(STATE, CallFrame* caller, Symbol* name, bool allow_private) {
    LookupData lookup(this, this->lookup_begin(state), allow_private);
    Dispatch dis(name);

    Arguments args;
    args.set_block(Qnil);
    args.set_recv(this);

    return dis.send(state, caller, lookup, args);
  }

  Object* Object::send_prim(STATE, Executable* exec, CallFrame* call_frame, Dispatch& msg,
                            Arguments& args) {
    Object* meth = args.shift(state);
    Symbol* sym = try_as<Symbol>(meth);

    if(!sym) {
      sym = as<String>(meth)->to_sym(state);
    }

    Dispatch dis(sym);
    LookupData lookup(this, this->lookup_begin(state), true);

    return dis.send(state, call_frame, lookup, args);
  }

  void Object::set_field(STATE, size_t index, Object* val) {
    type_info(state)->set_field(state, this, index, val);
  }

  Object* Object::set_table_ivar(STATE, Symbol* sym, Object* val) {
    /* Lazy creation of a lookuptable to store instance variables. */
    if(ivars_->nil_p()) {
      CompactLookupTable* tbl = CompactLookupTable::create(state);
      ivars(state, tbl);
      tbl->store(state, sym, val);
      return val;
    }

    if(CompactLookupTable* tbl = try_as<CompactLookupTable>(ivars_)) {
      if(tbl->store(state, sym, val) == Qtrue) {
        return val;
      }

      /* No more room in the CompactLookupTable. */
      ivars(state, tbl->to_lookuptable(state));
    }

    if(LookupTable* tbl = try_as<LookupTable>(ivars_)) {
      tbl->store(state, sym, val);
    }
    /* else.. what? */

    return val;
  }

  Object* Object::set_ivar_prim(STATE, Symbol* sym, Object* val) {
    if(sym->is_ivar_p(state)->false_p()) {
      return reinterpret_cast<Object*>(kPrimitiveFailed);
    }

    return set_ivar(state, sym, val);
  }

  Object* Object::set_ivar(STATE, Symbol* sym, Object* val) {
    LookupTable* tbl;

    /* Implements the external ivars table for objects that don't
       have their own space for ivars. */
    if(!reference_p()) {
      tbl = try_as<LookupTable>(G(external_ivars)->fetch(state, this));

      if(!tbl) {
        tbl = LookupTable::create(state);
        G(external_ivars)->store(state, this, tbl);
      }
      tbl->store(state, sym, val);
      return val;
    }

    /* We might be trying to access a field, so check there first. */
    TypeInfo* ti = state->om->find_type_info(this);
    if(ti) {
      TypeInfo::Slots::iterator it = ti->slots.find(sym->index());
      if(it != ti->slots.end()) {
        ti->set_field(state, this, it->second, val);
        return val;
      }
    }

    return set_table_ivar(state, sym, val);
  }

  Object* Object::del_ivar(STATE, Symbol* sym) {
    LookupTable* tbl;

    /* Implements the external ivars table for objects that don't
       have their own space for ivars. */
    if(!reference_p()) {
      tbl = try_as<LookupTable>(G(external_ivars)->fetch(state, this));

      if(tbl) tbl->remove(state, sym);
      return this;
    }

    /* We might be trying to access a field, so check there first. */
    TypeInfo* ti = state->om->find_type_info(this);
    if(ti) {
      TypeInfo::Slots::iterator it = ti->slots.find(sym->index());
      // Can't remove a slot, so just bail.
      if(it != ti->slots.end()) return this;
    }

    /* No ivars, we're done! */
    if(ivars_->nil_p()) return this;

    if(CompactLookupTable* tbl = try_as<CompactLookupTable>(ivars_)) {
      tbl->remove(state, sym);
    } else if(LookupTable* tbl = try_as<LookupTable>(ivars_)) {
      tbl->remove(state, sym);
    }
    return this;
  }

  String* Object::to_s(STATE, bool address) {
    std::stringstream name;

    name << "#<";
    if(Module* mod = try_as<Module>(this)) {
      if(mod->name()->nil_p()) {
        name << "Class";
      } else {
        name << mod->name()->c_str(state);
      }
      name << "(" << this->class_object(state)->name()->c_str(state) << ")";
    } else {
      if(this->class_object(state)->name()->nil_p()) {
        name << "Object";
      } else {
        name << this->class_object(state)->name()->c_str(state);
      }
    }

    name << ":";
    if(address) {
      name << reinterpret_cast<void*>(this);
    } else {
      name << "0x" << std::hex << this->id(state)->to_native();
    }
    name << ">";

    return String::create(state, name.str().c_str());
  }

  Object* Object::show(STATE) {
    return show(state, 0);
  }

  Object* Object::show(STATE, int level) {
    type_info(state)->show(state, this, level);
    return Qnil;
  }

  Object* Object::show_simple(STATE) {
    return show_simple(state, 0);
  }

  Object* Object::show_simple(STATE, int level) {
    type_info(state)->show_simple(state, this, level);
    return Qnil;
  }

  Object* Object::taint(STATE) {
    if(reference_p()) {
      set_ivar(state, state->symbol("tainted"), Qtrue);
    }
    return this;
  }

  Object* Object::tainted_p(STATE) {
    if(reference_p()) {
      Object* b = get_ivar(state, state->symbol("tainted"));
      if(b->nil_p()) return Qfalse;
      return Qtrue;
    } else {
      return Qfalse;
    }
  }

  TypeInfo* Object::type_info(STATE) const {
    return state->om->type_info[get_type()];
  }

  Object* Object::untaint(STATE) {
    if(reference_p()) {
      del_ivar(state, state->symbol("tainted"));
    }
    return this;
  }

  Object* Object::respond_to(STATE, Symbol* name, Object* priv) {
    LookupData lookup(this, lookup_begin(state));
    lookup.priv = RTEST(priv);

    Dispatch dis(name);

    if(!GlobalCacheResolver::resolve(state, name, dis, lookup)) {
      return Qfalse;
    }

    return Qtrue;
  }

  /**
   *  We use void* as the type for obj to work around C++'s type system
   *  that requires full definitions of classes to be present for it
   *  figure out if you can properly pass an object (the superclass
   *  has to be known).
   *
   *  If we have Object* obj here, then we either have to cast to call
   *  write_barrier (which means we lose the ability to have type specific
   *  write_barrier versions, which we do), or we have to include
   *  every header up front. We opt for the former.
   * 
   * 写入屏障就是把从老年代到新生代的引用记录在记录集的手法
   */
  void Object::write_barrier(STATE, void* obj) {
    state->om->write_barrier(this, reinterpret_cast<Object*>(obj));
  }

}
