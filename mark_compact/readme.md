# 什么是GC 标记-压缩算法
GC标记-压缩算法是由标记阶段和压缩阶段构成

标记阶段和标记清除的标记阶段完全一样。之后我们要通过搜索数次堆来进行压缩

它首先通过标记-清除算法标记出所有的存活对象和垃圾对象，然后将存活对象压缩到内存的一端，以连续的方式存放，将垃圾对象所占用的内存空间释放出来，从而解决了内存空间碎片化的问题

标记-压缩算法的实现步骤如下
- 从根节点出发，标记出所有的存活对象
- 将存活对象移动到内存的一端，使其连续存放
- 更新对象引用，使其指向新的位置
- 释放垃圾对象所占用的内存空间

# Lisp2 算法
## 算法对象构成
![avatar](/images/mark_compact_1.png)
Lisp2 算法在对象头中为forwarding指针留出空间，forwarding指针表示对象的目标地点。（设定forwarding时，还不存在移动完毕的对象）

## 概要
### 初始阶段
![avatar](/images/mark_compact_2.png)

### 标记结束阶段
![avatar](/images/mark_compact_3.png)

### 压缩阶段结束
![avatar](/images/mark_compact_4.png)

## 算法步骤
### 压缩阶段代码
```
compaction_phase(){
    set_forwarding_ptr() // 设置forwarding指针
    adjust_ptr()        // 更新指针
    move_obj()          // 移动对象
}
```

### 步骤一：设定forwarding指针
首先程序会搜索整个堆，给活动的对象设定forwarding指针。初始状态下forwarding是NULL
```
set_fowarding_ptr(){
    scan = new_address = $heap_start
    while(scan < $heap_end)
        if(scan.mark = TRUE)
            scan.forwarding = new_address
            new_address += scan.size
        scan += scan.size
}
```
- scan 用来搜索堆中的指针，new_address指向目标地点的指针
- 一旦scan找到活动对象，forwarding指针就要被更新。按着new_address对象的长度移动

步骤结果如图所示
![avatar](/images/mark_compact_5.png)

### 步骤二：更新指针
```
adjust_ptr(){
    for(r :$roots)                      // 更新根对象的指针
        *r = (*r).forwarding
    
    scan = $heap_start
    while(scan < $heap_end)
        if (scan.mark == TRUE)
            for(child :children(scan)) // 通过scan 更新其他对象指针
                *child = (*child).forwarding
        scan += scan.size
}
```
- 首先更新根的指针
- 然后重写所有活动对象的指针（对堆进行第二次的搜索）

步骤结果如图所示
![avatar](/images/mark_compact_6.png)

### 步骤三：移动对象
搜索整个堆（第三次搜索），再将对象移动到forwarding指针的引用处
```
move_obj(){
   scan = $free = $heap_start
   while(scan < $heap_end)
    if(scan.mark == TRUE)                         // 判断是否是活动对象
        new_address = scan.forwarding             // 获取对象要移动的地点
        copy_data(new_address, scan, scan.size)   // 复制对象（移动对象）
        new_address.forwarding = NULL             // 将forwarding改为NULL
        new_address.mark = FALSE                  // mark改为FALSE
        $free += new_address.size                 // 指针后移
        scan += scan.size                         // 指针后移
}
```
- 算法不会对对象本身的顺序进行改变，只会把对象集中在堆的一端
- 算法没有去删除对象，知识吧对象的mark设置为FALSE
- 之后把forwarding改为NULL，标志位改为FALSE，将$free移动obj.size个长度

步骤结果如图所示
![avatar](/images/mark_compact_7.png)

## 优缺点
优点：可有效利用堆

使用整个堆在进行垃圾回收，没啥说的。任何的算法都是有得有失，用时间换空间。或者用空间换时间。重要的是它在这里
适不适用

缺点：压缩花费计算成本

Lisp2 算法中，对堆进行了3次搜索。在搜索时间与堆大小成正相关的状态下，三次搜索花费的时间是很恐怖。也就是说，它的吞吐量要低于其他算法。时间成本至少是标记清除的三倍（当然不包含mutator）

# Two-Finger算法
## 概述
对堆执行两次搜索

## 前提
Two-Finger 算法，必须将所有对象整理成大小一致

它没有在对象的头中设立forwarding指针，而是在对象的域中设立forwarding指针即可

## 概要
Two-Finger算法由一下两个步骤构造
- 移动对象
- 更新指针

在Lisp2算法中，是将对象移动到堆的一端。在Two-Finger中，操作对象向左滑动，通过执行压缩算法来填补空闲空间。此时为了让更好的填补空间，所以对象大小必须一样
![avatar](/images/mark_compact_8.png)

移动前的对象都会被保留（图的白色对象）

因为在Two-Finger算法中，我们要利用放置非活动对象的空间来作为活动对象的目标空间，这是为了让移动前的对象不会在GC过程中被覆盖掉

这样一来，我们就能把forwarding指针设定在这个移动前 的对象的域中，没有必要多准备出 1 个字了。

## 步骤一：移动对象
\$free和live两个指针，从两端向正中间搜索堆

\$free是用于寻找非活动的指针，live是寻找活动对象(原空间)
![avatar](/images/mark_compact_9.png)

两个指针发现空间和原空间的对象时会移动对象
![avatar](/images/mark_compact_10.png)

途中虚线其实表示forwarding

之后使用move_obj函数对对象进行移动其伪代码如下
```
move_obj(){
    $free = $heap_start
    live = $heap_end - OBJ_SIZE
    while (TRUE)
        while ($free.mark == TRUE)          // 从前往后寻找非活动对象
            $free += OBJ_SIZE
        while (live.mark == FLASE)          // 重后往前 寻找活动对象
            live -= OBJ_SIZE
        if ($free < live)                   // 判断交换条件
            copy_data($free, live, OBJ_SIZE)
            live.forwarding = $free
            live.mark = FALSE
        else
            break
}
```
- 先从前往后，使用\$free寻找非活动对象
- 在从后往前，使用live寻找活动对象
- 找到之后，判断两者位置。如果非活动对象在活动对象之前，就执行复制操作。否则就退出循环

## 步骤二：更新指针
接下来寻找指向移动前的对象的指针，把它更新，使其指向移动后的对象。更新指针操作的是adjust_ptr()函数
![avatar](/images/mark_compact_11.png)

当对象移动结束时，\$free 指针指向分块的开头，这时位于 \$free 指针右边的不是非活动对象就是活动对象

\$free右边地址的指针引用的是移动前的对象
```
adjust_ptr(){
    for (r :$roots)
        if (*r >= $free)
            *r = (*r).forwarding
    
    scan = $heap_start
    while (scan < $free)
        scan.mark = FALSE
        for (child :children(scan))
            if (*child >= $free)
                *child = (*child).forwarding
            scab += OBJ_SIZE
            
}
```
- 先查询根直接引用的对象。当这些指针的对象在\$free右边的时候,就意味这个对象已经被移动到了某处。在这种情况下必须将指针的引用目标更新到移动后的对象
- 所有活动对象都在\$heap_start 和 \$free之间，我们需要取遍历这一部分堆

## 优缺点
优点：Two-Finger 算法能把 forwarding 指针设置在移动前的对象的域里，所以不需要额外的内存 空间以用于 forwarding 指针。`只需要2次搜索堆`

缺点：Two-Finger 算法则不考虑对象间的引用关系，一律对其进行压缩，结果就导致对象的顺序在压缩前后产生了巨大的变化。因此，我们无法更好的使缓存。 `对象大小必须一样`

# 表格算法
## 概述
表格算法是B. K. Haddon和W. M. Waite于1967年发明的算法，也只需要遍历堆2次

考虑到许多活动对象在堆中都是连续的，构成一个对象群，我们可以将连续的活动对象作为一个整体来移动

相比于Lisp2算法要为每个对象记录一个forwarding指针，表格算法只需要记录下每个活动对象群第一个对象的移动距离，就能推算出对象群中每个对象的新地址。同时，相比于Two-Finger算法的填空方式，表格算法在压缩过程中能保持对象的相对顺序不变

表格算法中需要记录的是`对象群的最低地址`和`左边空闲空间的大小`（也就是移动距离）

所有对象群都需要记录这两项信息，这些信息就叫做`间隙表格`

表格指的是这两项信息构成的数组，间隙指的是对象群左边空闲空间的大小，就是活动对象间的空隙

间隙才是有用的信息，而对象群最低地址是为了能够找到间隙，相当于一个索引，你可以将它们看成一个键值对

明白这两项信息的作用对于理解算法大有帮助,间隙表格示例如下
![avatar](/images/mark_compact_12.png)

间隙表格会被记录在空闲空间中，也不需要占据额外的空间。为了能够利用非活动对象记录间隙表格，表格算法在分配对象时有一个最低大小限制

比如记录一个间隙需要2个字，则分配对象大小不能小于2个字
```
非活动对象占据的空间就是空闲空间
```

## 算法步骤
### 移动对象群并构建间隙表格
假设移动前堆的状态如下
![avatar](/images/mark_compact_13.png)
```
move_obj() {
    // scan用来遍历堆
    // $free指向空闲空间开头
    scan = $free = $heap_start 
    size = 0 //记录对象群左边空闲空间大小
    while (scan < $heap_end) // 遍历堆
        while(scan.mark == FALSE) // 非活动对象
            size += scan.size // 将非活动对象大小加到空间空间大小中
            scan += scan.size // 下一个对象
        live = scan // 对象群的第一个对象，也就是对象群最低地址

    while(scan.mark == TRUE) // 活动对象
        scan += scan.size // 下一个对象

    // 移动对象并构建间隙表格
    slide_objs_and_make_bt(scan, $free, live, size) 
    $free += (scan - live) // 跳过活动对象群
}
```

构造间隙表格非常复杂，简要过程如下所示，括号中的数字表示对象首地址
![avatar](/images/mark_compact_14.gif)

首先我们移动对象群BC，将最低地址，也就是B的首地址100和BC左边空闲空间大小100写入scan指向的位置，因为此时scan一定指向着一个非活动对象

接下来继续执行move_obj函数，找到对象群FG

但此时不能直接移动FG，因为这样会覆盖掉450号地址记录的间隙表格

我们需要首先将450号地址记录的间隙表格移动到scan指向的地址，也就是H占据的800号地址，然后移动对象群FG，最后将F的首地址和size记录到G腾出的空间，也就是700号地址的位置

经过这样的"回避"操作之后，间隙表格就不是按对象群最低地址有序的了，这会给下一步重写指针带来一定的麻烦

因为前面我们说过真正有用的信息是对象群左侧空闲空间大小，也就是移动距离，但这个信息是通过对象群最低地址搜索获得的

如果间隙表格有序，那么可以就可以利用二分查找，否则除了排序就只能线性搜索了

### 重写指针
这一步主要是利用间隙表格记录的信息更新移动后对象的新地址，伪代码如下
```
// 重写指针
adjust_ptr() { 
    for(r : $roots)             // 遍历根直接引用对象
        *r = new_address(*r)    // 重写对象地址
    scan = $heap_start          // 从堆头开始
    while(scan < $free)         // 遍历活动对象
        scan.mark = FALSE       // 取消活动对象标记，为下次GC做准备
        for(child : children(scan))      // 遍历子对象
            *child = new_address(*child) // 重写对象地址
        scan += scan.size //下一个对象
}

// 通过间隙表格计算对象新地址
new_address(obj) {
    best_entry = new_bt_entry(0, 0) // 记录间隙表格的变量
    // 找到obj所在对象群对应的间隙表格
    for(entry : break_table)        // 遍历间隙表格
        // 找到最低地址小于等于obj的间隙表格中地址最大的那个
        if(entry.address <= obj && $best_entry.address < entry.address) 
            best_entry = entry      // 记录找到的间隙表格
    return obj - best_entry.size    // 原地址-移动距离=新地址
}

```
因为间隙表格的最低地址记录的是对象群中第一个对象的地址，所以对象群中每个对象的地址一定是大于等于该对象群对应的间隙表格所记录的最低地址的
- 比如示例中BC对象群对应的间隙表格(100,100)，B的地址是100，C的地址是250，都是不小于100的，FG对象群同理

找到间隙表格之后，用对象地址减去移动距离就是对象的新地址
- 比如对象B的原地址是100，移动距离是100，新地址就是100-100=0，与B同属一个对象群的C的原地址是250，移动距离也是100，新地址就是250-100=150

FG同理

### 优缺点
优点: 表格算法的优点是空间利用率提高了，因为它有效利用了非活动对象来记录压缩需要的信息，甚至都不需要forwarding指针

缺点: 表格算法的缺点是压缩耗时，代价来源于维护和搜索间隙表格，而且在不排序的情况下只能线性搜索

# 参考资料
- [垃圾回收算法（4）标记整理](https://www.cnblogs.com/qqmomery/p/6654254.html)
- [Mark Compact GC (Part one: Lisp2)](https://www.cnblogs.com/Leon-The-Professional/p/9994389.html)
- [GC标记压缩算法](https://blog.csdn.net/puss0/article/details/126357914)