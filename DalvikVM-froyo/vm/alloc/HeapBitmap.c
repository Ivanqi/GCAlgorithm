/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Dalvik.h"
#include "HeapBitmap.h"
#include "clz.h"
#include <limits.h>     // for ULONG_MAX
#include <sys/mman.h>   // for madvise(), mmap()
#include <cutils/ashmem.h>

#define HB_ASHMEM_NAME "dalvik-heap-bitmap"

#define ALIGN_UP_TO_PAGE_SIZE(p) \
    (((size_t)(p) + (SYSTEM_PAGE_SIZE - 1)) & ~(SYSTEM_PAGE_SIZE - 1))

#define LIKELY(exp)     (__builtin_expect((exp) != 0, true))
#define UNLIKELY(exp)   (__builtin_expect((exp) != 0, false))

/*
 * Initialize a HeapBitmap so that it points to a bitmap large
 * enough to cover a heap at <base> of <maxSize> bytes, where
 * objects are guaranteed to be HB_OBJECT_ALIGNMENT-aligned.
 */
bool
dvmHeapBitmapInit(HeapBitmap *hb, const void *base, size_t maxSize,
        const char *name)
{
    void *bits;
    size_t bitsLen;
    size_t allocLen;
    int fd;
    char nameBuf[ASHMEM_NAME_LEN] = HB_ASHMEM_NAME;

    assert(hb != NULL);
    
    /**
     * 根据maxSize(最大VM Heap大小)计算bitsLen(位图大小)
     * 宏HB_OFFSET_TO_INDEX()会由VM Heap的开头地址作为对象的object的偏移来计算位图索引
     * 也就是说，只要在这里计算位图的 "最大索引 x 元素的大小"，就算出了位图整体的大小
     */
    bitsLen = HB_OFFSET_TO_INDEX(maxSize) * sizeof(*hb->bits);
    // 用4K字节将计算出来的bitLen进一对齐
    allocLen = ALIGN_UP_TO_PAGE_SIZE(bitsLen);   // required by ashmem

    if (name != NULL) {
        snprintf(nameBuf, sizeof(nameBuf), HB_ASHMEM_NAME "/%s", name);
    }
    /**
     * ashmen_create_region()函数是用Android的ashmen定义的函数
     * 在ashmen_create_region() 函数中将获取指向 /dev/ashmem的文件描述符、进行初始设定
     */
    fd = ashmem_create_region(nameBuf, allocLen);
    if (fd < 0) {
        LOGE("Could not create %zu-byte ashmem region \"%s\" to cover "
                "%zu-byte heap (%d)\n",
                allocLen, nameBuf, maxSize, fd);
        return false;
    }

    /**
     * 用mmap()函数分配位图，在内存保护里加入PROT_READ(可读取)和PROT_WRITE(可写入)
     * 把ashmen_create_region()函数的结果加入参数的文件描述法，即通过ashmem来分配内存空间
     * 将已映射到内存空间设为MAP_PRIVATE(私人的)，也就是说，即使在内存空间执行写入操作，也不会反映到其他进程上
     */
    bits = mmap(NULL, bitsLen, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);
    if (bits == MAP_FAILED) {
        LOGE("Could not mmap %d-byte ashmem region \"%s\"\n",
                bitsLen, nameBuf);
        return false;
    }

    memset(hb, 0, sizeof(*hb));
    /**
     * 初始化结构体HeapBitmap
     * 这里将base(与位图对应的空间) - 1d的值放在max里面。意味着，如果max是一个低于base的值，就意味着"还没有在位图表格上设置任何位"
     * 因此也将其作为标志使用
     */
    hb->bits = bits;
    hb->bitsLen = bitsLen;
    hb->base = (uintptr_t)base;
    hb->max = hb->base - 1;

    return true;
}

/*
 * Initialize <hb> so that it covers the same extent as <templateBitmap>.
 */
bool
dvmHeapBitmapInitFromTemplate(HeapBitmap *hb, const HeapBitmap *templateBitmap,
        const char *name)
{
    return dvmHeapBitmapInit(hb,
            (void *)templateBitmap->base, HB_MAX_OFFSET(templateBitmap), name);
}

/*
 * Initialize the bitmaps in <out> so that they cover the same extent as
 * the corresponding bitmaps in <templates>.
 */
bool
dvmHeapBitmapInitListFromTemplates(HeapBitmap out[], HeapBitmap templates[],
    size_t numBitmaps, const char *name)
{
    size_t i;
    char fullName[PATH_MAX];

    fullName[sizeof(fullName)-1] = '\0';
    for (i = 0; i < numBitmaps; i++) {
        bool ok;

        /* If two ashmem regions have the same name, only one gets
         * the name when looking at the maps.
         */
        snprintf(fullName, sizeof(fullName)-1, "%s/%zd", name, i);
        
        ok = dvmHeapBitmapInitFromTemplate(&out[i], &templates[i], fullName);
        if (!ok) {
            dvmHeapBitmapDeleteList(out, i);
            return false;
        }
    }
    return true;
}

/*
 * Clean up any resources associated with the bitmap.
 */
void
dvmHeapBitmapDelete(HeapBitmap *hb)
{
    assert(hb != NULL);

    if (hb->bits != NULL) {
        // Re-calculate the size we passed to mmap().
        size_t allocLen = ALIGN_UP_TO_PAGE_SIZE(hb->bitsLen);
        munmap((char *)hb->bits, allocLen);
    }
    memset(hb, 0, sizeof(*hb));
}

/*
 * Clean up any resources associated with the bitmaps.
 */
void
dvmHeapBitmapDeleteList(HeapBitmap hbs[], size_t numBitmaps)
{
    size_t i;

    for (i = 0; i < numBitmaps; i++) {
        dvmHeapBitmapDelete(&hbs[i]);
    }
}

/*
 * Fill the bitmap with zeroes.  Returns the bitmap's memory to
 * the system as a side-effect.
 */
void
dvmHeapBitmapZero(HeapBitmap *hb)
{
    assert(hb != NULL);

    if (hb->bits != NULL) {
        /* This returns the memory to the system.
         * Successive page faults will return zeroed memory.
         */
        madvise(hb->bits, hb->bitsLen, MADV_DONTNEED);
        hb->max = hb->base - 1;
    }
}

/*
 * Walk through the bitmaps in increasing address order, and find the
 * object pointers that correspond to places where the bitmaps differ.
 * Call <callback> zero or more times with lists of these object pointers.
 *
 * The <finger> argument to the callback indicates the next-highest
 * address that hasn't been visited yet; setting bits for objects whose
 * addresses are less than <finger> are not guaranteed to be seen by
 * the current XorWalk.  <finger> will be set to ULONG_MAX when the
 * end of the bitmap is reached.
 * 
 * dvmHeapBitmapXorWalk 的作用
 *  1). 寻找位图内的标记位
 *  2). 把对应标记位的对象存入缓冲区
 *  3). 重复1和2直到缓冲区被填满
 *  4). 当缓冲区满时，调用回调行数(scanBitmapCallback)
 *  5). 对位图内进行全方位的搜索，搜索完毕即结束
 */
bool
dvmHeapBitmapXorWalk(const HeapBitmap *hb1, const HeapBitmap *hb2,
        bool (*callback)(size_t numPtrs, void **ptrs,
                         const void *finger, void *arg),
        void *callbackArg)
{
    // 定义指针缓冲区
    static const size_t kPointerBufSize = 128;
    void *pointerBuf[kPointerBufSize];  // 缓冲区的实体
    void **pb = pointerBuf;             // pointerBuf 开头元素的指针
    size_t index;
    size_t i;

/**
 * 这个宏基本上只调用回调函数
 * 计算把把存在缓冲区里的对象指针的个数和缓冲区自身交给回调函数，此外还要把下一个要标记的对象指针(finger_)也交给回调函数
 */
#define FLUSH_POINTERBUF(finger_) \
    do { \
        if (!callback(pb - pointerBuf, (void **)pointerBuf, \
                (void *)(finger_), callbackArg)) \
        { \
            LOGW("dvmHeapBitmapXorWalk: callback failed\n"); \
            return false; \
        } \
        pb = pointerBuf; \
    } while (false)

/**
 * hb_是位图、bits_是位图内的元素(像素0010010)这样的位串
 * 
 * static const unsigned long kHighBit = (unsigned long)1 << (HB_BITS_PER_WORD - 1);
 *  把32位中的开头位(1000..00)的正数(位串)设定给kHight
 * 
 * const uintptr_t ptrBase = HB_INDEX_TO_OFFSET(i) + hb_->base; 
 *  把对应的bits_开头位的对象指针设定给ptrBase
 * 
 * 之后执行while语句，直到bits_等于0为止
 *  宏 CLZ() 中调用 ARM 汇编语言的CLZ 命令
 *      CLZ命令会从位串的开头开始数0连续了多少次，并返回搜集到的结果
 *      这样就可以求出到标记为止的右偏移数，然后将其存入rshift
 *  bits_ &= ~(kHighBit >> rshift);
 *      消除rshift查找到的位标记
 *  *pb++ = (void *)(ptrBase + rshift * HB_OBJECT_ALIGNMENT);
 *      求出对应标记位的对象指针，将其存入缓冲区
 *      这项操作要一直进行到bits_内的标记全部消除(也就是变成0)为止
 * 
 * FLUSH_POINTERBUF(ptrBase + HB_BITS_PER_WORD * HB_OBJECT_ALIGNMENT);
 *  把下一个要检查标记的对象指针传递给参数
 * 
 * index = HB_OFFSET_TO_INDEX(hb_->max - hb_->base);
 *  重新计算index
 *  这时因为回调函数可能害hb_hb_->max增加
 *  对于增加的部分，也必须调用宏DECODE_BITS()
 */
#define DECODE_BITS(hb_, bits_, update_index_) \
    do { \
        if (UNLIKELY(bits_ != 0)) { \
            static const unsigned long kHighBit = \
                    (unsigned long)1 << (HB_BITS_PER_WORD - 1); \
            const uintptr_t ptrBase = HB_INDEX_TO_OFFSET(i) + hb_->base; \
/*TODO: hold onto ptrBase so we can shrink max later if possible */ \
/*TODO: see if this is likely or unlikely */ \
            while (bits_ != 0) { \
                const int rshift = CLZ(bits_); \
                bits_ &= ~(kHighBit >> rshift); \
                *pb++ = (void *)(ptrBase + rshift * HB_OBJECT_ALIGNMENT); \
            } \
            /* Make sure that there are always enough slots available */ \
            /* for an entire word of 1s. */ \
            if (kPointerBufSize - (pb - pointerBuf) < HB_BITS_PER_WORD) { \
                FLUSH_POINTERBUF(ptrBase + \
                        HB_BITS_PER_WORD * HB_OBJECT_ALIGNMENT); \
                if (update_index_) { \
                    /* The callback may have caused hb_->max to grow. */ \
                    index = HB_OFFSET_TO_INDEX(hb_->max - hb_->base); \
                } \
            } \
        } \
    } while (false)

    assert(hb1 != NULL);
    assert(hb1->bits != NULL);
    assert(hb2 != NULL);
    assert(hb2->bits != NULL);
    assert(callback != NULL);

    if (hb1->base != hb2->base) {
        LOGW("dvmHeapBitmapXorWalk: bitmaps cover different heaps "
                "(0x%08x != 0x%08x)\n",
                (uintptr_t)hb1->base, (uintptr_t)hb2->base);
        return false;
    }
    if (hb1->bitsLen != hb2->bitsLen) {
        LOGW("dvmHeapBitmapXorWalk: size of bitmaps differ (%zd != %zd)\n",
                hb1->bitsLen, hb2->bitsLen);
        return false;
    }
    if (hb1->max < hb1->base && hb2->max < hb2->base) {
        /* Easy case; both are obviously empty.
         */
        return true;
    }

    /* First, walk along the section of the bitmaps that may be the same.
     */
    if (hb1->max >= hb1->base && hb2->max >= hb2->base) {
        unsigned long int *p1, *p2;
        uintptr_t offset;

        offset = ((hb1->max < hb2->max) ? hb1->max : hb2->max) - hb1->base;
//TODO: keep track of which (and whether) one is longer for later
        // 求位图开头的index
        index = HB_OFFSET_TO_INDEX(offset);

        p1 = hb1->bits;
        p2 = hb2->bits;
        for (i = 0; i <= index; i++) {
//TODO: unroll this. pile up a few in locals?
            /**
             * 比较两个位图内的元素，把指针移动到下一个元素
             * 比较元素的时候使用的是 "^"(XOR)运算符
             * XOR有个特点，那就是"如果两个位是同值则返回0，如果不为同值则返回1"
             */
            unsigned long int diff = *p1++ ^ *p2++;
            DECODE_BITS(hb1, diff, false);
//BUG: if the callback was called, either max could have changed.
        }
        /* The next index to look at.
         */
        index++;
    } else {
        /* One of the bitmaps is empty.
         */
        index = 0;  // 表示位图的开头。将其初始值设定为0，直到第287行，index的值一直为0
    }

    /* If one bitmap's max is larger, walk through the rest of the
     * set bits.
     */
const HeapBitmap *longHb;
unsigned long int *p;
//TODO: may be the same size, in which case this is wasted work
    /**
     * 比较hb1跟hb2的max值，将max值较大的位图设为longHb
     * 不过hb2是虚拟的位图，其max被设置得比h1小，因此将h1设定为longHb
     */
    longHb = (hb1->max > hb2->max) ? hb1 : hb2;
    i = index;
    // 用于获取位图的最大索引
    index = HB_OFFSET_TO_INDEX(longHb->max - longHb->base);
    p = longHb->bits + i;
    // 按照从索引0到最大索引的顺序循环，将位图的元素按顺序交给宏DECODE_BITS()
    for (/* i = i */; i <= index; i++) {
//TODO: unroll this
        unsigned long bits = *p++;
        DECODE_BITS(longHb, bits, true);
    }

    if (pb > pointerBuf) {
        /* Set the finger to the end of the heap (rather than longHb->max)
         * so that the callback doesn't expect to be called again
         * if it happens to change the current max.
         */
        FLUSH_POINTERBUF(longHb->base + HB_MAX_OFFSET(longHb));
    }

    return true;

#undef FLUSH_POINTERBUF
#undef DECODE_BITS
}

/*
 * Fills outIndexList with indices so that for all i:
 *
 *   hb[outIndexList[i]].base < hb[outIndexList[i+1]].base
 */
static void
createSortedBitmapIndexList(const HeapBitmap hbs[], size_t numBitmaps,
        size_t outIndexList[])
{
    int i, j;

    /* numBitmaps is usually 2 or 3, so use a simple sort */
    for (i = 0; i < (int) numBitmaps; i++) {
        outIndexList[i] = i;
        for (j = 0; j < i; j++) {
            if (hbs[j].base > hbs[i].base) {
                int tmp = outIndexList[i];
                outIndexList[i] = outIndexList[j];
                outIndexList[j] = tmp;
            }
        }
    }
}

/*
 * Similar to dvmHeapBitmapXorWalk(), but compare multiple bitmaps.
 * Regardless of the order of the arrays, the bitmaps will be visited
 * in address order, so that finger will increase monotonically.
 */
bool
dvmHeapBitmapXorWalkLists(const HeapBitmap hbs1[], const HeapBitmap hbs2[],
        size_t numBitmaps,
        bool (*callback)(size_t numPtrs, void **ptrs,
                         const void *finger, void *arg),
        void *callbackArg)
{
    size_t indexList[numBitmaps];
    size_t i;

    /* Sort the bitmaps by address.  Assume that the two lists contain
     * congruent bitmaps.
     */
    createSortedBitmapIndexList(hbs1, numBitmaps, indexList);

    /* Walk each pair of bitmaps, lowest address first.
     */
    for (i = 0; i < numBitmaps; i++) {
        bool ok;

        ok = dvmHeapBitmapXorWalk(&hbs1[indexList[i]], &hbs2[indexList[i]],
                callback, callbackArg);
        if (!ok) {
            return false;
        }
    }

    return true;
}

/*
 * Similar to dvmHeapBitmapXorWalk(), but visit the set bits
 * in a single bitmap.
 */
bool
dvmHeapBitmapWalk(const HeapBitmap *hb,
        bool (*callback)(size_t numPtrs, void **ptrs,
                         const void *finger, void *arg),
        void *callbackArg)
{
    /* Create an empty bitmap with the same extent as <hb>.
     * Don't actually allocate any memory.
     */
    HeapBitmap emptyHb = *hb;
    emptyHb.max = emptyHb.base - 1; // empty
    emptyHb.bits = (void *)1;       // non-NULL but intentionally bad

    return dvmHeapBitmapXorWalk(hb, &emptyHb, callback, callbackArg);
}

/*
 * Similar to dvmHeapBitmapXorWalkList(), but visit the set bits
 * in a single list of bitmaps.  Regardless of the order of the array,
 * the bitmaps will be visited in address order, so that finger will
 * increase monotonically.
 */
bool dvmHeapBitmapWalkList(const HeapBitmap hbs[], size_t numBitmaps,
        bool (*callback)(size_t numPtrs, void **ptrs,
                         const void *finger, void *arg),
        void *callbackArg)
{
    size_t indexList[numBitmaps];
    size_t i;

    /* Sort the bitmaps by address.
     */
    createSortedBitmapIndexList(hbs, numBitmaps, indexList);

    /* Walk each bitmap, lowest address first.
     */
    for (i = 0; i < numBitmaps; i++) {
        bool ok;

        ok = dvmHeapBitmapWalk(&hbs[indexList[i]], callback, callbackArg);
        if (!ok) {
            return false;
        }
    }

    return true;
}
