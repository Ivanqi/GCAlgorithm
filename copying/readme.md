# GC复制算法
## 概述
把某个空间里的活动对象复制到其他空间，把原空间里的所有对象都回收掉

## 代码实现
第一是防止重复拷贝，对象之间的引用可以很复杂，各种交叉；第二是拷贝后，新老堆中的引用关系要缕顺
```
copying() {
  $free = $to_start // free是新堆的起点指针，复制回收刚开始时，free指向起点
  // 复制能从根引用的对象
  for (r : $root) 
   // 复制结束后返回指针，这里返回的指针指向的是 *r 所在的新空间的对象
    *r = copy(r)   // 注意，这里实际是一个update，将对象替换
  
  // 在 GC 复制算法中，在 GC 结束时，原空间的对象会作为垃圾被回收

  swap($from_start, $to_start)
}
```

```
// 这里，传给copy的对象，一定是旧堆的
copy(obj) {
  if obj.tag != COPIED                       // 重要，在老堆的对象中，标志这对象已经拷走
    copy_data($free, obj, obj.size)          // 拷贝到free
    obj.tag = COPIED
    obj.forwarding = $free                   // 重要！obj是老堆中对象，forwarding字段指向了新堆中的同一对象！！
    $free += obj.size
 
  for (child : children(obj.forwarding) 
    child = copy(child)                     // 重要，又是一个update操作，不仅仅是递归处理，也是递归赋值。将老的
 
  return obj.forwarding                     // 最终返回
}
```

分配方法
```
new_obj(size) {
  if ($free + size > $from_start + HEAD_SIZE/2)
    copying()                                              // 超过一半触发
    if ($free + size > $from_start + HEAP_SIZE/2)
      fail()
 
    obj = $free                                            // 分配直接从指针处取内存！
    obj.size = size
    $free += size
    return obj
}
```

## 优点
### 优点的吞吐量
GC 标记 - 清除算法消耗的吞吐量是搜索活动对象（标记阶段）所花费的时间和搜索整体堆（清除阶段）所花费的时间之和

吞吐量高，因为并没有像mark_sweep那种全堆扫描处理，只处理活动对象

### 可实现高速分配
GC 复制算法不使用空闲链表

这是因为分块是一个连续的内存空间。因此，调查这个分块的大小，只要这个分块大小不小于所申请的大小，那么移动 $free 指针就可以进行分配了

### 不会发生碎片化
对象全部紧邻，没有碎片

### 与缓存兼容
在 GC 复制算法中有引用关系的对象会被安排在堆里离彼此较近的位置

所以，有引用关系的对象内存相邻，缓存利用率好

## 缺点
### 堆使用效率低
GC 复制算法把堆二等分，通常只能利用其中的一半来安排对象。也就是说，只有一半堆能被使用

### 不兼容保守式 GC算法
GC 复制算法必须移动对象重写指针

### 递归调用函数
每次进行复制的时候都要调用函数，由此带来的额外负担不容忽视

递归调用时都会消耗栈，所以还有栈溢出的可能

# Cheney的GC复制算法(迭代复制)
## 解决的问题
为了解决递归调用都会消耗，可能还会存在栈溢出的问题
所以，把递归改成了迭代的方式。同时遍历算法也从深度优先算法(DFS)改成了广度优先算法(BFS)

## 代码实现
```
copying() {
  scan = $free = $to_start        // scan是一个逻辑队列的头，free则是这个逻辑队列的尾
  for (r : $root)
    r = copy(r)                  // 从root出发，先走一层。拉开scan与free的差距，理解成逻辑上将root的第一层子节点入队列。具体要看后面copy实现
 
  while (scan != free)
    for (child : children(scan))
      child = copy(child)
    scan += scan.size
 
  swap($from_start, $to_start)
}
 
copy(obj) {
  if !(obj.forwarding belong $to_start)     // 此处可以直接拿forwarding字段来判断是否已经拷贝到新堆空间
    copy_data($free, obj, obj.size)
    obj.forwarding = $free                  // 老对象指向新对象
    $free += obj.size
  return obj.forwarding
}
```

这里的scan 和 $free都指向to内存块。当开始内存复制的时候，首先会把root节点的内存复制过来，这个 $free指针就会增加，scan指针保存不变
开始遍历root的时候，scan的指针就会增加，同时会把root的子节点复制到to内存块中，$free指针也会对应增加。以此类推把旧堆数据进行复制

同时 scan和$free在to内存块组合了一个隐藏的队列，用于实现BFS的逻辑

## 优点
抑制调用函数的额外负担和栈的消耗，拿堆用作队列，省去了用于搜索的内存空间这一点

## 缺点
广度优先的方式会破坏深度优先的缓存利用率高的优点

# 近似深度优先搜索方法
## Cheney的GC复制算法回顾
假设所有对象都是2个字，下图所示是对象间的引用关系
![avatar](/images/copying_1.png)

下图所示是执行该算法时候，各个对象所在的页面

右上角数字是页面编号，假如说页面容量是6个字（只能放3个对象）
![avatar](/images/copying_2.png)

从上图不难看出，A,B,C是相邻的，这就是比较理想的状态。对于其他对象来说，降低了连续读取的可能性，降低了缓存命中率

## 前提
在这个方法中有下面四个变量
- \$page: 将堆分割成一个个页面的数组。$page[i]指向第i个页面的开头
- \$local_scan：将每个页面中搜索用的指针作为元素的数组。$local_scan[i]指向第i个页面中下一个应该搜索的位置
- \$major_scan：指向搜索尚未完成的页面开头的指针
- \$free：指向分块开头的指针

## 工作流程
`先复制A到To空间，然后复制他们的孩子B,C，都被放置到了0页`。如下图示：
![avatar](/images/copying_3.png)
- 因为A已经搜索完毕，所以$local_scan[0]指向B
- \$free指向第一页的开头，也就是说下一次复制对象会被安排在新的页面。在这种情况下，程序会从$major_scan引用的页面和$local_scan开始搜索
- 当对象被复制到新页面时，程序会根据这个页面的$local_scan进行搜索，直到新页面对象被完全占满为止
- 此时因为$major_scan还指向第0页，所以还是从$local_scan[0]开始搜索，也就是说要搜索B

`复制了D（B引用的对象），放到了$page[1]开头`
![avatar](/images/copying_4.png)
- 像这样的页面放在开头时候，程序会使用该页面的\$local_scan进行搜索
- 此时\$local_scan[0]暂停，\$local_scan[1]开始。之后复制了H,I

`这里第一页满了，所以$free指向第二页开头`
![avatar](/images/copying_5.png)
- 因此\$local_scan[1]暂停搜索，程序\$local_scan[0]开始搜索
- 即对B对象再次进行搜索，看有没有其他孩子

`可以看到B的孩子E被复制到了$page[2],同样，对$local_scan[0]再次进行暂停，对E用local_scan[2]进行搜索`
![avatar](/images/copying_6.png)
- 因此复制了J,K

`通过对J,K的搜索页面2满了，$free指向了页面3。再次回到$local_scan[0]进行搜索`
![avatar](/images/copying_7.png)

`搜索完对象C，复制完A到O的所有对象之后状态如下图所示`
![avatar](/images/copying_8.png)
- 这样就搜索完了第0页($major_scan),虽然还没有搜索完子对象，但是孩子没有孩子，所以现在这个状态，和搜索完后是一样的

## 执行结果
该方法是如何安排对象的呢？如下图示：
![avatar](/images/copying_2.png)

`很明显能看出与Cheney的复制算法不同，不管下一个页面在哪里，对象之间都存在引用关系`

该方法，采用了不完整的广度优先，它实际上是用到了暂停的。从一开始我们就根据关系，然后进行暂停，将有关系的对象安排到了一个页面中

# 多空间复制算法
GC复制算法最大的缺点就是只能利用半个堆

但是如果我们把空间分成十份，To空间只占一份那么这个负担就站到了整体的1/10。剩下的8份是空的，在这里执行GC标记清除算法

多空间复制算法，实际上就是把空间分成N份，对其中两份进行GC复制算法，对其中(N-2)份进行GC标记-清除

## multi_space_copying()函数
```
muti_space_copying(){
    $free = $heap[$to_space_index]
    for(r :$roots)
        *r = mark_or_copy(*r)
        
    for(index :0..(N-1))
        if(is_copying_index(index) == FALSE)
            sweep_block(index)
            
    $to_space_index = $from_space_index
    $from_space_index = ($from_space_index +1) % N
}

```
将堆分为N等份，分别是\$heap[0],\$heap[1]...\$heap[N-1]。这里的\$heap[\$to_space_index]表示To空间，每次执行GC时，To空间都会像
\$heap[0],\$heap[1]...\$heap[N-1]，\$heap[0],这样进行替换。Form空间在To空间的右边，也就是\$heap[1]...\$heap[N-1]
- 其中第一个for循环，为活动对象打上标记。能看出来是标记清除算法中的一个阶段
- 其中第一个for循环，当对象在From空间时，mark_or_copy()函数会将其复制到To空间，返回复制完毕的对象。如果obj在除Form空间以外的其他地方mark_or_copy()会给其打上标记，递归标记或复制它的子对象
- 其中第二个for循环，是清除阶段。对除From和To空间外的其他空间，把没有标记的对象连接到空闲链表
- 最后将To和From空间向右以一个位置，GC就结束了

## mark_or_copy()
```
mark_or_copy(obj){
    if(is_pointer_to_from_space(obj) == True)
        return copy(obj)
    else
        if(obj.mark == FALSE)
            obj.mark == TRUE
            for(child :children(obj))
                *child = mark_or_copy(*child)
        return obj

}

```
调查参数obj是否在From空间里。如果在From空间里，那么它就是GC复制算法的对象。这时就通过copy()函数复制obj，返回新空间的地址

如果obj不在From空间里，它就是GC标记-清除算法的对象。这时要设置标志位，对其子对象递归调用mark_or_copy()函数。最后不要忘了返回obj

## copy()
```
copy(obj){
    if(obj.tag != COPIED)
        copy_data($free, obj, obj.size)
        obj.tag = COPIED
        obj.forwarding = $free
        $free += obj.size
        for(child :children(obj.forwarding))
            *child = mark_or_copy(*child)
        return obj.forwarding
}

```

递归调用不是copy()函数，而是调用mark_ or_copy()函数。如果对象*child是复制对象，则通过mark_or_copy() 函数再次调用这个copy()函数

## 执行过程
将内存分为4等份。如下图示
![avatar](/images/copying_9.png)

To空间\$heap[0]空着，其他三个都被占用。这个状态下，GC就会变为如下如示
![avatar](/images/copying_10.png)

我们将\$heap[0]作为To空间，将\$heap[1]作为From空间执行GC复制算法。此外\$heap[2]和\$heap[3]中执行GC标记-清除算法，将分块连接到空闲链表

当mutator申请分块时候，程序会从空闲链表或者\$heap[0]中分割出块给mutator

接下来，To空间和From空间都向后移动一个位置。mutator重新开始
![avatar](/images/copying_11.png)

这次\$heap[1]是To空间，\$heap[2]From空。这种状态下执行就会变为下图所示
![avatar](/images/copying_12.png)

## 优缺点
### 优点
提高内存利用率：没有将内存空间二等分，而是分割了更多空间

### 缺点
GC标记清除，分配耗时，分块碎片化。当GC标记清除算法的空间越小的时候，该问题表现的越不突出。例如将内存分为3份的情况下

# 参考资料
- [垃圾回收算法（3）复制](https://www.cnblogs.com/qqmomery/p/6642867.html)
- [Copying GC (Part two :Multi Space Copying GC) ](https://www.cnblogs.com/Leon-The-Professional/p/9992345.html)