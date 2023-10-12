# 什么是增量式垃圾回收
一种通过逐渐推进垃圾回收来控制mutator最大暂停时间的方法

有时候GC时间太长会导致mutator迟迟不能进行。如下图示
![avatar](/images/incremental_1.png)

这样的GC称为停止型GC（Stop the world GC）

为此出现了增量式垃圾回收。增量(incremental)式垃圾回收是将GC和mutator一点点交替运行的手法，如下图示
![avatar](/images/incremental_2.png)

# 三色标记算法
描述增量式垃圾回收算法使用Edsger W.Dijkstra等人提出的三色标记算法(Tri-color marking)。将GC中的对象按照各自的情况分为三种，使用三种颜色代替
- 白色：还未搜索过的对象
- 灰色：正在搜索的对象
- 黑色：搜索完成的对象

以下使用GC标记-清除算法为示例
- GC开始运行前的所有对象都是白色，GC一旦开始运行，所有能从根到达的对象都会被标记，然后送到栈里。这样的对像是灰色。灰色的对象会从栈中取出，其子对象也会被涂成灰色，当所有子对象全部被涂成灰色，这时该对象就会成为黑色
- `GC结束的时候，活动对象全部是黑色，垃圾为白色`
  
## GC 标记清除算法的分割
`GC标记清除算法那增量式运行的三个阶段`
- 根查找阶段：将根直接指向对象标记成灰色
- 标记阶段：将子对象涂成灰色，结束时候所有对象都是黑色
- 清除阶段：查找清除白色对象连接到空闲链表。将黑色对象变为白色对象

下面是增量式垃圾回收的incremental_gc()函数
```
incremental_gc(){
    case $gc_phase                  // 检查变量，判断应该进入那个阶段
    when GC_ROOT_SCAN               // 进入查找阶段
        root_scan_phase()
    
    when GC_MARK                    // 进入标记阶段
        incremental_mark_phase()
    else
        incremental_sweep_phase()   // 清除阶段
}
```
- 当进入根查找阶段，我们直接把根引用的对象打上记号，放入栈中。GC开始时运行一次
- 根查找结束后，incremental_gc()会告一段落，mutator会再次开始运行
- 下来再次执行incremental_gc()，函数进入标记阶段。在标记阶段incremental_mark_phase()函数会从栈中取出对象和搜索对象。操作一定次数后，mutator会再次开始运行。直到栈标记为空
- 之后就是清除阶段。incremental_sweep_phase()函数不是一次性清除整个堆，而是每次只清除一定个数，然后中断GC，再次运行mutator

## 根查找阶段
根查找阶段非常简单。作为根查找实体的 root_scan_phase() 函数，如代码清单所示
```
root_scan_phase(){
    for(r : $roots)
        mark(*r)
    $gc_phase = GC_MARK
}
```

对能直接从根找到的对象调用 mark() 函数。mark() 函数的伪代码如下所示
```
mark(obj){
    if(obj.mark == FALSE)
        obj.mark = TRUE
        push(obj, $mark_stack) 
}
```
- 如果参数 obj 还没有被标记，那么就将其标记后堆到标记栈。这个函数正是把 obj 由白色涂成灰色的函数
- 当我们把所有直接从根引用的对象涂成了灰色时，根查找阶段就结束了，mutator会继 续执行

此外，这时 \$gc_phase 变成了 GC_MARK。也就是说，下一次 GC 时会进入标记阶段。

## 标记阶段
```
incremental_mark_phase() {
    for (i :1...MARK_MAX)
        if (is_empty($mark_stack) == FALSE) // 从栈中取出对象，将其子对象涂成灰色
            obj = pop($mark_stack)
            for (child :children(obj))
                mark(*child)                // 递归涂子节点
        else
            // 再次对根直接引用的对象进行标记。因为第一次标记根本没有进行完，而且之后也可能发生变化
            for (r :$roots)                 
                mark(*r)

            while (is_empty($mark_stack) == FALSE)
                obj = pop($mark_stack)
                for (child :children(obj))
                    mark(*child)
            
            $gc_phase = GC_SWEEP            // 为清除阶段做准备
            $sweeping = $heap_start
            return
            
}
```
- 可以看到首先从栈中取出对象，将其子对象涂成灰色
- `但是这一系列操作只执行了MARK_MAX次。我们知道增量式的垃圾回收不是一次性处理完了。所以这个MARK_MAX就显得格外重要了` 

之后在标记即将结束前，对根对象指向的对象再次标记。原因如下图示
![avatar](/images/incremental_3.png)

我们可以看到由于增量式垃圾回收它是一步一步走的，并不是说一次就把GC做完，所以它在GC的过程中指针时会变化的

如果变化如上图，我们又不对其重新标记，那得到的结果就是，C对象被删掉了。很严重的一个后果啊

为了防止这样，我们又一次的使用了写入屏障

## 写入屏障
看一下Edsger W. Dijkstra 等人提出的写入屏障
```
write_barrier(obj, field, newobj){
    if(newobj.mark == FALSE)
        newobj.mark = TRUE
        push(newobj, $mark_stack)
    
    *field = newobj
}
```
如果新引用的对象newobj没有被标记过，就将其标记后堆到标记栈里
![avatar](/images/incremental_4.png)

即使在 mutator 更新指针后的图中c，也没有产生从黑色对象指向白色对象的引用。这样一来我们就成功地防止了标记遗漏

## 清除阶段
当标记栈为空时，GC就会进入清除阶段。代码清单如下
```
incremental_sweep_phase(){
    swept_count = 0
    while(swept_count < SWEEP_MAX)
        if($sweeping < $heap_end)
            if($sweeping.mark ==TRUE)
                $sweeping.mark = FALSE
            else
                $sweeping.next = $free_list
                $free_list = $sweeping
                $free_size += $sweeping.size
            $sweeping += $sweeping.size
            swept_count++
        else
        $gc_phase = GC_ROOT_SCAN
        return 
        
}
```
该函数所进行的操作就是把没被标记的对象连接到空闲链表，取消已标记的 对象的标志位

为了只对一定数量的对象进行回收，事先准备swept_count用来记录数量

swept_count >= SWEEP_MAX 时，就暂停清除阶段，再次执行 mutator。当把堆全部清除完毕时，就将 \$gc_phase 设为 GC_ROOT_SCAN，结束 GC

## 分配
```
newobj(size){
    if($free_size < HEAP_SIZE * GC_THRESHOLD) // 如果分块的总量 $free_size 少于一定的量HEAP_SIZE就执行GC
        incremental_gc()
    
    chunk = pickup_chunk(size, $free_list)  // 搜索空闲链表返回大小时size的块
    if(chunk != NULL)
        chunk.size = size
        $free_size -= size
        if($gc_phase == GC_SWEEP && $sweeping <= chunk) // 判断GC是否在清除阶段和chunk是不是在已清除完毕的空间
            chunk.mark = TRUE  // 没有在清除完毕的空间，我们要设置标志位
        return chunk
    else
        allocation_fail()
}
```
- 判断$free_size 是不是小于HEAP_SIZE * GC_THRESHOLD，如果是就执行GC
- 在空闲链表查找大小为size的分块，并返回
- 对分块进行标记，对\$free_size进行后移操作
- 判断 GC状态，和chunk状态
- 如果chunk在清除完毕的空间的空间里什么都不做，如果不在则进行标记

![avatar](/images/incremental_5.png)

## 优点和缺点
缩短最大暂停时间
- 增量式垃圾回收通过交替运行GC和mutator来减少停止时间，减少二者的相互影响。从而保证GC不会长时间妨碍mutator
- 增量式垃圾回收不是重视吞吐量，而是重视如何缩短最大暂停时间

降低了吞吐量
- 写入屏障会增加额外负担。但是这是必要的牺牲
- 高吞吐量和缩短最大暂停时间，二者不可兼得。根据需要选择最合适的最好

# Steele 的算法
1975，Guy.Steele

这个算法中使用的写入屏障条件更严格，它能减少GC中错误标记的对象

## mark()函数
```
mark(obj){
    if(obj.mark == FALSE)
        push(obj, $mark_stack)
}
```
再把对象放入标记栈的时候还没有标记，在这个算法中从标记栈取出时才为它设置标记为

这里的灰色对象时“标记栈里的没有设置标记位置的对象”，黑色是设置了标识位的对象

## 写入屏障
```
write_barrier(obj, fieldm newobj){
    if($gc_phase == GC_MARK && obj.mark == TRUE && newobj.mark == FALSE)
        obj.mark = FALSE
        push(obj, $mark_stack)
        
    *field = newobj
}
```
- 判断条件，条件成立时，将obj.mark设置为FALSE
- 将obj 放入标记栈
- 如果标记过程中发出引用的对象时黑色，且新的引用对象为灰色或者白色，那么我们就把发出引用的对象涂成灰色

![avatar](/images/incremental_6.png)

如上图示：写入屏障在a到b中发挥了作用。对象A被涂成了灰色，其结果就是c中不存在从黑色对象指向的白色对象，也就不会出现把活动对象标记遗漏的状况了

A对象为灰色的时候，我们会再次对A对象进行搜索和标记

# 汤浅的算法
汤浅太一，1990 Snapshot GC

这种算法是以GC开始时对象间的引用关系(snapshot)为基础来执行GC的

因此，根据汤浅算法，在GC开始时回收垃圾，保留GC开始时的活动对象和GC执行过程中被分配的对象

## 标记阶段
```
incremental_mark_phase(){
    for(i :1..MARK_MAX)
        if(is_empty($mark_stack) == FALSE)
            obj = pop($mark_stack)
            for(child: children(obj))
                mark(*child)
        else
            $gc_phase = GC_SWEEP
            $sweeping = $heap_start
            return
}
```
- 在汤浅算法中，清除阶段没有必要再去搜索根了，因为该算法以GC开始时对象间的引用关系为基础执行GC
- 在标记阶段中，新的从根引用的对象在GC开始时应该会被别的对象锁引用
- 因此搜索GC开始时就存在的指针，就会发现这个对象已经被标记完毕了。所以没有必要从新的根重新标记它

## 从黑色对象指向白色对象的指针
之前我们提到过，使用写入屏障来防止产生从黑色对象指向白色对象的指针。但是汤浅算法中我们允许黑色对象指向白色对象。这样还能回收成功的原因是因为GC一开始就保留活动对象的这项原则

遵循这项原则，就没有必要在新生成指针时标记引用的目标子对象。即使生成了从黑色对象指向白色对象的指针，只要保留了GC开始时的指针，作为引用目标的白色对象早晚都会被标记

其实指针被删除时的情况应该引起我们的注意。指向对象的指针删除，就可能无法保留GC开始时的活动对象了。因此在汤浅的写入屏障中，再删除指向对象的指针时要进行特殊处理

## 写入屏障
```
write_barrier(obj, field, newobj){
    oldobj = *field
    if(gc_phase == GC_MARK && oldobj.mark == FALSE)
        oldobj.mark = TRUE
        push(oldobj， $mark_stack)
    *field = newobj
}
```
当GC进入到标记阶段且oldobj是白色对象，就将其涂成灰色
![avatar](/images/incremental_7.png)

图b转移到图c的过程中写入屏障发挥了作用，他把c涂成了灰色，这样就防止c的标记遗漏

图b中，黑色对象指向了白色对象。但是B指向C并没有被删除

在汤浅的写入屏障中这时候不会进行特殊的处理。只有当B指向C的指针被删除的时候，C才会变为灰色

## 分配
```
newobj(size){
    if($free_size < HEAP_SIZE * GC_THRESHOLD)
        incremental_gc()
    
    chunk = pickup_chunk(size, $free_list)
    if(chunk ！= NULL)
        chunk.size = size
        $free_size -= size
        if($gc_phase == GC_MARK)
            chunk.mark = TRUE
        else if($gc_phase == GC_SWEEP && $sweeping <= chunk)
            chunk.mark = TRUE
        return chunk
        else
            allocation_fail()
            
}
```
在标记阶段进行分配时会无条件设置obj的标志位

也就是说，会把obj涂成黑色。汤浅算法的写入屏障比较简单，所以保留了很多对象，无意间也保留了很多垃圾对象

# 比较各个写入屏障
![avatar](/images/incremental_8.png)