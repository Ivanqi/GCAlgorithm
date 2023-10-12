# RC Immix
Rifat Shariyar等，Reference Counting Immix，2013

RC Immix算法将引用计数的一大缺点（吞吐量低）改善到了实用的级别。本算法改善了引用计数算法的“合并型引用计数法”和Immix组合起来使用

# 合并型引用计数
Yossi Levanoni, Erez Petrank，2001

在吞吐量方面引用计数不如搜索型GC。其原因是引用计数器频繁增减

在引用计数中，每当对象之间的关系发生变化，对象的计数器就会发生变化

如果计数器频繁发生增减，那么写入屏障执行的频率就会增大，处理就会变得繁重

![avatar](/images/rc_immix_1.png)

在这里注意下，如果我们对一个对象的计数器执行加后在执行减。因为两者会互相抵消，最终计数器并没有变化。由此可知可知，比起一直保持计数器的数值正确，不如计数器的增量和减量相互抵消这样更方便管理，节省资源

于是出现了一种新的方法，`就是把注意力只放在初始和最后的状态上，该期间内不对计数器进行修改`。这就是`合并型引用计数法(Coalesced Reference Counting)`。在该方法中，即使指针发生改动，计数器也不会增减。指针改动时的信息会被注册到`更改缓冲区`

如果对象间的引用关系发生变化，就会导致计数器值是错误的。如果期间ZCT满了就要去查找ZCT并更正计数器的值

在合并型引用计数法中，要将指针发生改动的对象和其所有子对象注册到更改缓冲区中。这项操作是通过写入屏障来执行的。不过因为这个时候我们不更新计数器，所以计数器的值会保持错误

![avatar](/images/rc_immix_2.png)

我们将指针改动了的X和指针改动前被X引用的A注册到缓冲区。因为没有更新计数器，所以A和B的计数器在这个时候是不正确的

等到缓冲区满了，就要运行GC了。合并型引用计数法中的GC指的是查找更改缓冲区。并正确设置计数器的过程

通过查找更改缓冲区，如何重新正确设定计数器的值，用下图来说明
![avatar](/images/rc_immix_3.png)

首先，X将其指针从A变更到B。此时我们把X和其子对象A注册到缓冲区

然后假设X的元引用对象发送了B->A->B这样的变化。因为我们已经吧X注册到更改缓冲区了，所以没有必要进行重新注册

接下来，假设d阶段更改缓冲区满了，则是后就启动GC了。首先查找更改缓冲区，我们可以得到以下信息
- X在 某个阶段引用的是A
- X现在引用的是B
- 对A的计数器进行减量
- 对B的计数器进行加量

## 伪代码
```
# 合并型引用计数法的写入屏障
write_barrier_coalesced_RC(obj, field, dst){
    if(!obj.dirty)
        register(obj)
    obj.field = dst
    
}
```
写入屏障负责检查要改动的指针的对象obj的标识dirty是否注册完毕。如果没有注册，就将其注册到更改缓冲区($mod_buf)

执行注册的方法是register函数
```
# register
register(obj){
    if($mod_buf.size <= $mod_buf.used_sized)
        garbage_collect()
        
    entry.obj = obj
    foreach(child_ptr :children(obj))
        if(*child_ptr != nil)
            push(entry.children, *child_ptr)

    push($mod_buf, entry)
    obj.dirty = true
    
}
```
- 首先，当\$mod_buf满的时候，我们就要执行GC
- 接下来，准备obj的信息。以将其注册到更改缓冲区。这个entry是指向某对象所有子对象的指针集合。我们将这个信息作为\$mod_buf的一个元素进行注册
- 后设置的dirty标识已经将其注册过了

```
#garbage_collect
garbage_collect(){
    foreach(entry: $mod_buf)
        obj = entry.obj
        foreach(child:obj)
            inc_ref_cnt(child)
        foreach(child : entry.children)
            dec_ref_cnt(child)
        obj.dirty = false
    clear($mod_buf)
}
```
- 合并型引用计数法是将某一时期最初的状态和最后的状态进行比较，合理调整计数器的算法
- 在garbage_collect()中，先查找$mod_buf，对于已经注册的对象进行如下处理
  - 对obj现在的子对象的计数器进行增量
  - 对obj以前的子对象的计数器进行减量
- 通过以上操作，根据对象最初的最后的状态对计数器进行合理的调整。此外先进行增量是为了确保AB是同一对象时也能后顺利运行

## 优点和缺点
优点：增加了吞吐量

缺点：增加了mutator的暂停时间

# 合并型引用计数法和Immix的融合
Immix 中不是以对象为单位，而是以线为单位进行内存管理的，因此不使用空闲链表

如果线内一个活动对象都没有了，就回收整个线。只要线内还有一个活动对象，这个线就无法作为分块回收

RC Immix中，对象不仅有计数器，线也有计数器，这样就可以获悉线内是否存在活动对象

不过线的计数器和对象有所不同。对象的计数器表示的是指向这个对象的指针数。而线的计数器则是表示这个线里存活的对象数量。如果变成了0就将整个线回收
![avatar](/images/rc_immix_4.png)

对象生成和废弃的频率要低于对象间引用关系变化的频率，这样一来 更新计数器所产生的额外负担就小了

下面来说一下RC Immix中是如何以线为单位进行内存管理的
```
dec_ref_cnt(obj){
    obj.ref_cnt --
    if(obj.ref_cnt ==0)
        reclaim_obj(obj)
        line = get_line(obj)
        line.ref_cnt--
        if(line.ref_cnt == 0 )
            reclaim_line(line)     
}
```
当对象的计数器为0的时候，对线的计数器进行减量。当线的计数器为0 的时候，我们就可以回收整条线

此方法虽然把合并型引用计数和Immix组合到了一起，但是不能执行压缩

在压缩中要进行复制对象的操作。要实现这些操作，不仅要复制对象，还要将引用此对象的指针全部改写。因此压缩所需的信息这里是不能提供的

于是RC Immix通过限定对象来时实现压缩。就是下面要说的新对象

## 新对象
在RC Immix中，把没有经历过GC的对象称为新对象。即就是在上一次GC之后生成的对象
- 更改缓冲区里记录的是从上一次GC开始到现在为止指针改动过的对象
- 所有指向新对象的指针都是上一次GC之后生成的。也就是说，所有引用新对象的对象都被注册到了更改缓冲区
- 因此可以通过查找更改缓冲区，只对新对象进行复制操作
- 利用这条性质，RC Immix中以新对象为对象进行压缩，这种方法称为被动的碎片整理(Reactive Defragmentation)

## 被动的碎片整理
RC Immix,在更改缓冲区满了的时候会查找更改缓冲区，这时如果发现了新的对象，就把他复制到别的空间去

我们准备一个空的块来当做目标空间。在复制过程中目标空间满了的情况下，就采用一个空的块。我们不对旧对象执行被动的碎片整理

RC Immix中的 garbage_collect()函数代码清单如下
```
garbage_collect(){
    dst_block = get_empty_block()
    foreach(entry :$mod_but)
        obj = entry.obj
        foreach(child_ptr :children(obj))
            inc_ref_cnt(*child_ptr)
            if(!(*child_ptr).old)
                reactive_defrag(child_ptr, dst_block)
        foreach(child : entry.children)
            dec_ref_cnt(child)
        obj.dirty = false        
}

```
这里的garbage_collect()函数和合并型引用计数法中的garbage_collect()函数很像。基本流程都是查找更改缓冲区，根据情况增量或者减量操作

不同的是这里对新对象调用了reactive_defrag()函数，这就是被懂得碎片整理。那么我们来看看 reactive_defrag()函数
```
reactive_defrag(ptr, dst_block){
    obj = *ptr
    if(obj.copied)
        *ptr = obj.forwarding
    else
        if(obj.size>dst_block.free_size)
            dst_block = get_empty_block()
        
        new_obj = dst_block.free_top
        copy_data(obj, new_obj, obj.size)
        obj.forwarding = new_obj
        *ptr = new_obj
        obj.copied = true
        new_obj.old = true
        dst_block.free_top += obj.size
        dst_block.free_size += obj.size
        line = get_line(obj)
        line.ref_cnt++
        
}
```
- 复制对象并设定forwarding指针。在RC Immix中还需要留意线计数器。将对象复制到线时，也要对线的计数器进行增量
- 通过被动的碎片整理，就可以以引用计数法为基础，来执行压缩
- 此外，因为我们以引用计数法为基础，所以不能解决循环引用的问题
  
为了解决问题，可以使用积极地碎片整理(Proactive Defragmentation)

## 积极的碎片整理
被动的碎片整理的两处缺陷
- 无法对旧对象进行压缩
- 无法回收有循环引用的垃圾

为了解决这些问题，RC Immix中进行了被动的碎片处理之外，还进行了另一项操作。也就是`积极的碎片整理`
- 首先决定要复制到那个块，然后把能够通过指针从根查找到的对象全都复制过去
- 通过积极的碎片整理，对就对象进行压缩和回收循环垃圾都成为了可能
- 他还有以个优点就是可以重置计数器。如果某个对象发生计数器溢出，通过执行积极的碎片整理，就会从根重新查找所有指针，也就能重新设定计数器的值

## 优点和缺点
### 优点
吞吐量得到改善，据说平均提高了12个点。甚至会超过搜索型GC

吞吐量改善的原因是因为合并型引用计数法没有通过写入屏障来执行计数器的增减操作。即使对象之间的引用关系频繁发生变化，吞吐量也不会下降太多

吞吐量改善的原因是撤出了空闲链表。通过以线为单位来管理分块，只要在线内移动指针就可以进行分配。此外省去了把分块重新连接到空闲链表上的操作

### 缺点
会增加暂停时间，不过可以通过调整缓冲区的大小缩短暂停时间

只要线内还有一个非垃圾对象，就无法将其回收。也就是说线内只要有一个活动对象就会浪费一条线