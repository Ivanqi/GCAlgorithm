# GC引用计数算法
引用计数法中引入了一个概念，那就是“计数器”。计数器表示的是对象的人气指数，也就是有多少程序引用了这个对象（被引用数）

# GC引用计数算法优点
- 可即刻回收垃圾
- 最大暂停时间短
- 没有必要沿指针查找

# GC引用计数算法缺点
- 计数器值的增减处理繁重
- 计数器需要占用很多位
- 循环引用无法回收

# 延迟引用计数法（Deferred Reference Counting）
## 概述
延迟引用计数法（Deferred Reference Counting）是一种改进的引用计数法，在对象的引用计数器为0时，不立即回收对象，而是将其添加到一个特殊的数据结构中，通常称为Zero Count Table（ZCT），来延迟回收的时机

在延迟引用计数法中，当一个对象的引用计数减少到0时，不立即回收该对象，而是将其添加到ZCT中

ZCT是一个记录了引用计数为0的对象的表格。然后，定期或在适当的时机，系统会遍历ZCT并回收其中的对象

## 优点
在延迟引用计数法中，程序延迟了根引用的计数，将垃圾一并回收

通过延迟，减轻了因根引用频繁发生变化而导致的计数器增减所带来的额外负担

## 缺点
为了延迟计数器值的增减，垃圾不能马上得到回收，这样一来垃圾就会压迫堆，也就失去了引用计数法的一大优点 — 可即刻回收垃圾

# Sticky引用计数法
## 概述
Sticky引用计数法是一种优化引用计数算法的方法，用于处理计数器占用位数较多的情况  

研究表明，很多对象一生成立即就会死了，也就是说大多数对象的计数是在0，1之间(`1位引用计数法`)，达到32的本身很少

另一方面，如果真有达到32个引用的对象，那么很大程度上这个对象在执行的程序中占有重要的位置，甚至可以不需要回收

## Sticky引用计数法的实现
使用GC标记-清除算法进行管理

一开始就把所有对象的计数器值设置为0

在标记阶段，不标记对象，而是对计数器进行增量操作

在清除阶段，为了对计数器进行增量操作，算法对活动对象进行不止一次的搜索。回收计数器值仍为0的对象

# 部分标记-清除算法（partial mark sweep）
## 概述
找出一个可能是循环引用垃圾（注意，不是找循环引用，是找循环引用垃圾）环中的一个对象，将其放置入一个特殊的集合。对这个集合进行mark-sweep，判断出是否真的循环引用了

## 部分标记-清除算法的实现
首先把对象分为4种
- black：确定的活动对象
- white:确定的非活动对象
- hatch：可能是循环引用的对象
- gray：用于判断循环引用的一个中间

算法的切入点在于减引用计数
```
def_ref_cnt(obj) {
  obj.ref_cnt--
  if obj.ref_cnt == 0           // 引用为0，绝不可能是循环引用垃圾
    delete(obj)                 // delete函数上面有，减子对象的引用计数并回收
  else if obj.color != HATCH    // 可见，疑似循环引用垃圾的必要条件，是被减引用后，计数未达0
    obj.color = HATCH
    enqueue(obj, $hatch_queue)  // 这里仅仅将可疑的对象本身入队列
}
```

对应的，new
```
new_obj() {
  obj = pickup_chunk(size)
  if obj != NULL 
    obj.color = BLACK
    obj.ref_cnt = 1
    return obj
  else if !is_empty($hatch_queue)
    scan_hatch_queue()      // 当无内存可用时，开始检测循环引用队列并释放之
    return new_obj(size)
  else
    fail()
}
```

下面，便是如何判断循环引用垃圾的核心逻辑
```
scan_hatch_queue() {
  obj = dequeue($hatch_queue)
  if obj.color == HATCH        // 思考，什么时候不为hatch？
    paint_gray(obj)
    scan_gray(obj)
    collect_white(obj)
  else if !is_empty($hatch_queue)
    scan_hatch_queue()
}
```

继续看下一个关键函数。它的核心思想在于，如果当前这个obj是个循环垃圾，那么它的引用计数不为0的原因，是因为被垃圾循环引用着
同理，如果从它自己的子节点开始尝试着循环减引用计数，如果能减到自己为0，那么可以说明自己是循环引用的垃圾
```
paint_gray(obj) {
  // 递归函数
  if obj.color == BLACK | HATCH         // 为什么可能为BLACK？因为起始对象虽然是hatch，但它的引用的子对象可能是black
    obj.color = GRAY                    // 标识，防止在循环引用的情况下无尽递归
    for child : children(obj)
      child.ref_cnt--                  // 注意！关键点！hatch的obj本身没有减，而是从子节点开始减！这个减是个试探减，最终如果不是循环引用垃圾，还要恢复！
      paint_gray(child)
}
```

经过上述处理，已经将可疑的hatch对象的子对象全部递归了一遍
以上是核心逻辑，下面则是最终判断，要为hatch定性：到底是不是循环引用垃圾？
```
scan_gray(obj) {
  if obj.color == GRAY
    if obj.ref_cnt > 0
      paint_black(obj)            // 平反，因为如果真是循环引用垃圾，转一轮下来应该被引用的子对象回头来减过引用计数了
    else
      obj.color = WHITE           // 定罪，因为本身paint_gray时，并未减自身的计数，这里为0了，只可能是被引用的对象轮回回来减了，
      for child : children(obj)   // 既然本身已经确定是循环垃圾了，那么之前的尝试减有效，可以遍历子节点找出引用环了。
        scan_gray(obj)
}
```

最后，看一下“平反”的过程，很容易理解，在paint_gray中试减掉的引用计数要恢复回来
```
paint_black(obj) {
  obj.color = BLACK
  for child : children(obj)
    child.ref_cnt++              // 注意，这里也是当前对象没有加，从引用的子对象开始加。因为当证明当前非垃圾的情况下，当前对象当初也没有减
    if child.color != BLACK
      paint_black(child)         // 递归恢复
}
```

最后的最后，递归清理垃圾
```
collect_white(obj) {
  if obj.color == WHITE
    obj.color = BLACK  //防止循环不结束，并非真的非垃圾
    for child : children(obj)
      collect_whilte(child)
    reclaim(obj)             // 回收
}
```

上面这个算法虽然很精妙，但是毕竟遍历了3次对象：mark_gray, scan_gray, collect_whilte，最大暂停时间有影响

# 参考资料
- [GC算法之引用计数](https://zhuanlan.zhihu.com/p/27939756)
- [垃圾回收机制中，引用计数法是如何维护所有对象引用的？](https://www.zhihu.com/question/21539353/answer/18596488)
- [垃圾回收算法（2）引用计数](https://www.cnblogs.com/qqmomery/p/6629524.html)