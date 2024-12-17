/* Hash Tables Implementation.
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "fmacros.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>

#include "dict.h"
#include "zmalloc.h"
#include "redisassert.h"

/* Using dictEnableResize() / dictDisableResize() we make possible to disable
 * resizing and rehashing of the hash table as needed. This is very important
 * for Redis, as we use copy-on-write and don't want to move too much memory
 * around when there is a child performing saving operations.
 * 使用 dictEnableResize() / dictDisableResize() ，我们可以根据需要禁用哈希表的大小调整和重新散列、
 * 这对redis非常重要，因为我们使用写时复制而且不希望移动太多内存当有子节点执行保存操作时
 *
 * Note that even when dict_can_resize is set to 0, not all resizes are
 * prevented: a hash table is still allowed to grow if the ratio between
 * the number of elements and the buckets > dict_force_resize_ratio.
 * 注意即使当 dict_can_resize 设置为0，并不是所有的调整大小操作被禁止：
 * 如果元素数量与桶之间的比率大于 dict_force_resize_ratio ，哈希表仍然允许增长
 * dict_force_resize_ratio =5，代表元素数量必须是桶的数量五倍及以上
 * */
static dictResizeEnable dict_can_resize = DICT_RESIZE_ENABLE;
static unsigned int dict_force_resize_ratio = 5;

/* -------------------------- private prototypes ---------------------------- */

static int _dictExpandIfNeeded(dict *ht);
static unsigned long _dictNextPower(unsigned long size);
static long _dictKeyIndex(dict *ht, const void *key, uint64_t hash, dictEntry **existing);
static int _dictInit(dict *ht, dictType *type, void *privDataPtr);

/* -------------------------- hash functions -------------------------------- */

static uint8_t dict_hash_function_seed[16];

void dictSetHashFunctionSeed(uint8_t *seed) {
    memcpy(dict_hash_function_seed,seed,sizeof(dict_hash_function_seed));
}

uint8_t *dictGetHashFunctionSeed(void) {
    return dict_hash_function_seed;
}

/* The default hashing function uses SipHash implementation
 * in siphash.c. */

uint64_t siphash(const uint8_t *in, const size_t inlen, const uint8_t *k);
uint64_t siphash_nocase(const uint8_t *in, const size_t inlen, const uint8_t *k);

uint64_t dictGenHashFunction(const void *key, int len) {
    return siphash(key,len,dict_hash_function_seed);
}

uint64_t dictGenCaseHashFunction(const unsigned char *buf, int len) {
    return siphash_nocase(buf,len,dict_hash_function_seed);
}

/* ----------------------------- API implementation ------------------------- */

/* Reset a hash table already initialized with ht_init().
 * 重置已经用 ht_init() 初始化的哈希表
 * NOTE: This function should only be called by ht_destroy().
 * 请注意，这个方法应该只被 ht_destory() 调用
 * */
static void _dictReset(dictht *ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

/* Create a new hash table
 * 创建哈希表
 * */
dict *dictCreate(dictType *type,
        void *privDataPtr)
{
    //分配初始内存
    dict *d = zmalloc(sizeof(*d));

    //初始化哈希表
    _dictInit(d,type,privDataPtr);
    return d;
}

/* Initialize the hash table
 * 初始化哈希表
 * */
int _dictInit(dict *d, dictType *type,
        void *privDataPtr)
{
    //哈希表 1 重置
    _dictReset(&d->ht[0]);
    //哈希表 2 重置
    _dictReset(&d->ht[1]);
    d->type = type;
    d->privdata = privDataPtr;
    d->rehashidx = -1;
    d->pauserehash = 0;
    return DICT_OK;
}

/* Resize the table to the minimal size that contains all the elements,
 * but with the invariant of a USED/BUCKETS ratio near to <= 1
 * 将包含所有元素的哈希表调整到最小，但是使用 USED/BUCKETS 比率接近 <=1 的不变量
 * */
int dictResize(dict *d)
{
    unsigned long minimal;
    //如果当前哈希表不允许调整大小 或 正在处于哈希过程中，直接返回失败
    if (dict_can_resize != DICT_RESIZE_ENABLE || dictIsRehashing(d)) return DICT_ERR;
    //取出表0已使用的元素，代表最小值
    minimal = d->ht[0].used;
    //若小于系统最小值 4 则还是按照4来
    if (minimal < DICT_HT_INITIAL_SIZE)
        minimal = DICT_HT_INITIAL_SIZE;
    return dictExpand(d, minimal);
}

/* Expand or create the hash table,
 * when malloc_failed is non-NULL, it'll avoid panic if malloc fails (in which case it'll be set to 1).
 * Returns DICT_OK if expand was performed, and DICT_ERR if skipped.
 * 扩容或创建哈希表，
 * 当malloc_failed为非null时，它将避免malloc失败时的恐慌（在这些情况下，它将被设置为1）
 *
 * */
int _dictExpand(dict *d, unsigned long size, int* malloc_failed)
{
    //如果已尝试扩容失败，重置失败标识 继续扩容
    if (malloc_failed) *malloc_failed = 0;

    /* the size is invalid if it is smaller than the number of
     * elements already inside the hash table
     * 如果该大小 小于哈希表中已存在的元素数量，则该大小无效
     * */
    //不能在重哈希过程中 或 扩容后的数量不能小于等于已使用的数量
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    //扩容后的哈希表
    dictht n; /* the new hash table */
    //扩容后的数量，数量为 当前数量的下一个2的幂次方
    unsigned long realsize = _dictNextPower(size);

    /* Detect overflows
     * 检查扩容后是否溢出
     * */
    if (realsize < size || realsize * sizeof(dictEntry*) < realsize)
        return DICT_ERR;

    /* Rehashing to the same table size is not useful.
     * 如果扩容后的数量等于表1的数量 返回失败，无需扩容
     * */
    if (realsize == d->ht[0].size) return DICT_ERR;

    /* Allocate the new hash table and initialize all pointers to NULL
     * 给新表分配内存 且 初始化所有指针指向null
     * */
    //给扩容后的哈希表初始化属性
    n.size = realsize;
    n.sizemask = realsize-1;
    if (malloc_failed) {
        n.table = ztrycalloc(realsize*sizeof(dictEntry*));
        *malloc_failed = n.table == NULL;
        if (*malloc_failed)
            return DICT_ERR;
    } else
        n.table = zcalloc(realsize*sizeof(dictEntry*));

    n.used = 0;

    /* Is this the first initialization? If so it's not really a rehashing
     * we just set the first hash table so that it can accept keys.
     * 这是第一次初始化吗？如果是这样，那就不是重新散列了，我们只是设置了第一个哈希表，这样他就可以接受键了
     * */
    if (d->ht[0].table == NULL) {
        d->ht[0] = n;
        return DICT_OK;
    }

    /* Prepare a second hash table for incremental rehashing
     * 为增量哈希准备第二个哈希表
     * */
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

/* return DICT_ERR if expand was not performed
 * 如果扩容执行失败返回 DICT_ERR
 * */
int dictExpand(dict *d, unsigned long size) {
    return _dictExpand(d, size, NULL);
}

/* return DICT_ERR if expand failed due to memory allocation failure */
int dictTryExpand(dict *d, unsigned long size) {
    int malloc_failed;
    _dictExpand(d, size, &malloc_failed);
    return malloc_failed? DICT_ERR : DICT_OK;
}

/* Performs N steps of incremental rehashing. Returns 1 if there are still
 * keys to move from the old to the new hash table, otherwise 0 is returned.
 * 执行 N 步增量哈希。如果仍然存在键需要从旧表移动到新表则返回1，否则返回0
 *
 * Note that a rehashing step consists in moving a bucket (that may have more
 * than one key as we use chaining) from the old to the new hash table, however
 * since part of the hash table may be composed of empty spaces, it is not
 * guaranteed that this function will rehash even a single bucket, since it
 * will visit at max N*10 empty buckets in total, otherwise the amount of
 * work it does would be unbound and the function may block for a long time.
 * 请注意 一个重哈希步骤包含移动一个桶(正如我们使用的链式包含不止一个kry) 从旧表到新表，
 * 但是由于哈希表的其他部分可能包含空白空间，它不保证这个方法重新哈希甚至是单个桶，因为它总共会访问最大 N*10 个空桶，
 * 否则它所做的工作将被解除绑定，而且这个方法可能会阻塞很长时间
 * */
int dictRehash(dict *d, int n) {
    //空桶的最大访问数量
    int empty_visits = n*10; /* Max number of empty buckets to visit. */
    //初始化 两个哈希表的当前大小
    unsigned long s0 = d->ht[0].size;
    unsigned long s1 = d->ht[1].size;
    //判断哈希是否被禁止 返回0
    if (dict_can_resize == DICT_RESIZE_FORBID || !dictIsRehashing(d)) return 0;
    //如果避免哈希，但是 当前比率 小于 dict_force_resize_ratio 即不允许调整大小 则返回0
    if (dict_can_resize == DICT_RESIZE_AVOID &&
        ((s1 > s0 && s1 / s0 < dict_force_resize_ratio) ||
         (s1 < s0 && s0 / s1 < dict_force_resize_ratio)))
    {
        return 0;
    }

    //循环 n 直到为 0，且 哈希表 0 已使用的节点不等于0
    while(n-- && d->ht[0].used != 0) {
        dictEntry *de, *nextde;

        /* Note that rehashidx can't overflow as we are sure there are more
         * elements because ht[0].used != 0
         * 请注意，rehashidx 不能溢出，因为我们确信有更多的元素，因为 哈希表0 使用的元素不等于 0
         * */
        assert(d->ht[0].size > (unsigned long)d->rehashidx);
        //若哈希表0 所对应的 rehashidx 索引位置的值为 null
        //说明访问到空桶了，哈希索引值++， 最大空桶访问量empty_visits--，直到等于0，说明 rehash结束返回1
        while(d->ht[0].table[d->rehashidx] == NULL) {
            d->rehashidx++;
            if (--empty_visits == 0) return 1;
        }
        //取出当前哈希索引所对应的值
        de = d->ht[0].table[d->rehashidx];
        /* Move all the keys in this bucket from the old to the new hash HT
         * 将这个桶中的所有键从旧的哈希表中移动到新的哈希表中
         * */
        //因为 de 是dictEntry，是链表，所以需要将整个链表中的元素都重哈希到 表1 中
        while(de) {
            //初始化重哈希的值
            uint64_t h;
            //保留下一个链表要处理的值，因为当前要处理 de
            nextde = de->next;
            /* Get the index in the new hash table
             * 使用表1的 sizemask 重新计算索引位置
             * */
            h = dictHashKey(d, de->key) & d->ht[1].sizemask;
            //将表1中 h 对应字典值插入到 待插入值 de 的后面
            de->next = d->ht[1].table[h];
            //将de 赋值给新位置
            d->ht[1].table[h] = de;
            //表0可用元素 --
            d->ht[0].used--;
            //表1可用元素++
            d->ht[1].used++;
            //将de设置为链表对应的下一个值
            de = nextde;
        }
        //上述操作遍历完后说明 表0的元素都重哈希结束 将 rehashidx 索引位置的值设置为0
        d->ht[0].table[d->rehashidx] = NULL;
        //rehashidx 索引位置+1
        d->rehashidx++;
    }

    /* Check if we already rehashed the whole table...
     * 检查是否已经将整个哈希表都重哈希
     * */
    //若表0 已使用的节点数量为 0 说明都重哈希过了
    if (d->ht[0].used == 0) {
        //释放表0的内存
        zfree(d->ht[0].table);
        //将表1 替换 表0
        d->ht[0] = d->ht[1];
        //重置表1
        _dictReset(&d->ht[1]);
        //重置rehashidx为-1
        d->rehashidx = -1;
        return 0;
    }

    /* More to rehash...
     * 还有剩余的值需要哈希
     * */
    return 1;
}

long long timeInMilliseconds(void) {
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

/* Rehash in ms+"delta" milliseconds. The value of "delta" is larger 
 * than 0, and is smaller than 1 in most cases. The exact upper bound 
 * depends on the running time of dictRehash(d,100).*/
int dictRehashMilliseconds(dict *d, int ms) {
    if (d->pauserehash > 0) return 0;

    long long start = timeInMilliseconds();
    int rehashes = 0;

    while(dictRehash(d,100)) {
        rehashes += 100;
        if (timeInMilliseconds()-start > ms) break;
    }
    return rehashes;
}

/* This function performs just a step of rehashing, and only if hashing has
 * not been paused for our hash table. When we have iterators in the
 * middle of a rehashing we can't mess with the two hash tables otherwise
 * some element can be missed or duplicated.
 * 这个函数只执行重新散列的一个步骤，并且只有在散列没有暂停的情况下才执行。
 * 当我们在重新散列过程中使用迭代器时，我们不能混淆两个哈希表，否则可能会丢失或重复某些元素
 *
 * This function is called by common lookup or update operations in the
 * dictionary so that the hash table automatically migrates from H1 to H2
 * while it is actively used.
 * 该函数由字典中的常见查找或更新操作调用，以便哈希表在活跃使用时自动从 H1迁移到 H2.
 * */
static void _dictRehashStep(dict *d) {
    //当 pauserehash 为0 ，表示在哈希中
    if (d->pauserehash == 0) dictRehash(d,1);
}

/* Add an element to the target hash table
 * 添加元素到目标哈希表中
 * */
int dictAdd(dict *d, void *key, void *val)
{
    //哈希表新增key
    dictEntry *entry = dictAddRaw(d,key,NULL);

    //若新增失败返回失败
    if (!entry) return DICT_ERR;
    //设置对应的值
    dictSetVal(d, entry, val);
    return DICT_OK;
}

/* Low level add or find:
 * This function adds the entry but instead of setting a value returns the
 * dictEntry structure to the user, that will make sure to fill the value
 * field as they wish.
 * 低级别添加或查询：
 * 此函数添加条目，但不设置值，而是向用户返回 dictEntry 结构，这将确保按用户的意愿填充字段
 *
 * This function is also directly exposed to the user API to be called
 * mainly in order to store non-pointers inside the hash value, example:
 * 这个函数也直接暴露给用户API，主要是为了在哈希值中存储非指针，例如：
 *
 * entry = dictAddRaw(dict,mykey,NULL);
 * if (entry != NULL) dictSetSignedIntegerVal(entry,1000);
 *
 * Return values:
 *
 * If key already exists NULL is returned, and "*existing" is populated
 * with the existing entry if existing is not NULL.
 * 如果key已经存在直接返回null， 如果 existing 非null， *existing 则填充为已有的节点
 *
 * If key was added, the hash entry is returned to be manipulated by the caller.
 * 如果添加了 key，则返回哈希项以供调用者操作
 */
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing)
{
    long index;
    dictEntry *entry;
    dictht *ht;

    //判断哈希表是否在重新哈希中
    if (dictIsRehashing(d)) _dictRehashStep(d);

    /* Get the index of the new element, or -1 if
     * the element already exists.
     * 获取新元素的索引位置，如果元素已经存在则为-1
     * */
    if ((index = _dictKeyIndex(d, key, dictHashKey(d,key), existing)) == -1)
        return NULL;

    /* Allocate the memory and store the new entry.
     * Insert the element in top, with the assumption that in a database
     * system it is more likely that recently added entries are accessed
     * more frequently.
     * 分配内存且存储新节点
     * 将新元素插入到顶部，假设在数据库系统中，最近添加的条目更有可能被频繁的访问
     * */
    //判断是否处于渐进式哈希，是则返回表1否则返回表0
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    //分配新节点的内存
    entry = zmalloc(sizeof(*entry));
    //新节点的下一个指针指向 当前哈希表数组对应位置
    entry->next = ht->table[index];
    //将新节点替换原索引
    ht->table[index] = entry;
    //已使用节点++
    ht->used++;

    /* Set the hash entry fields.
     * 设置哈希字段
     * */
    dictSetKey(d, entry, key);
    return entry;
}

/* Add or Overwrite:
 * Add an element, discarding the old value if the key already exists.
 * Return 1 if the key was added from scratch, 0 if there was already an
 * element with such key and dictReplace() just performed a value update
 * operation.
 * 添加或覆盖：
 * 添加元素，如果key已经存在则丢弃原来的值。
 * 如果key是从头开始加的返回1，如果当前key已经存在一个元素且该方法执行了更新操作则返回0
 * */
int dictReplace(dict *d, void *key, void *val)
{
    dictEntry *entry, *existing, auxentry;

    /* Try to add the element. If the key
     * does not exists dictAdd will succeed.
     * 尝试执行添加元素操作。
     * 如果key不存在将插入成功
     * */
    entry = dictAddRaw(d,key,&existing);
    //插入成功返回1
    if (entry) {
        dictSetVal(d, entry, val);
        return 1;
    }

    /* Set the new value and free the old one. Note that it is important
     * to do that in this order, as the value may just be exactly the same
     * as the previous one. In this context, think to reference counting,
     * you want to increment (set), and then decrement (free), and not the
     * reverse.
     * 设置新值且释放旧值。请注意 按此顺序执行是非常重要的，因为该值可能与前一个值完全相同。
     * 在该上下文中，考虑到引用计数，你希望先增加然后建设，而不是相反
     * */
    //若插入失败表明已有值
    auxentry = *existing;
    //设置新值
    dictSetVal(d, existing, val);
    //释放旧值
    dictFreeVal(d, &auxentry);
    return 0;
}

/* Add or Find:
 * dictAddOrFind() is simply a version of dictAddRaw() that always
 * returns the hash entry of the specified key, even if the key already
 * exists and can't be added (in that case the entry of the already
 * existing key is returned.)
 * dictAddOrFind() 只是 dictAddRaw() 的一个版本，它总是返回特定key的哈希值，即使这个key已经存在且无法被添加（在这种情况下返回已经存在的值）
 *
 * See dictAddRaw() for more information. */
dictEntry *dictAddOrFind(dict *d, void *key) {
    //初始化参数
    //entry:插入成功的节点
    //existing:已存在的节点
    dictEntry *entry, *existing;
    //执行插入，如果已有节点则会将值填充到existing
    entry = dictAddRaw(d,key,&existing);
    //插入成功返回插入值，若已存在返回已存在的值
    return entry ? entry : existing;
}

/* Search and remove an element. This is an helper function for
 * dictDelete() and dictUnlink(), please check the top comment
 * of those functions.
 * 搜索并移除元素。他是dictDelete() and dictUnlink()的辅助函数，请检查这些函数的头部注释
 *
 * d:哈希表
 * key:待删除的key
 * nofree:是否释放内存  0-释放 1-不释放
 * */
static dictEntry *dictGenericDelete(dict *d, const void *key, int nofree) {
    //初始化变量
    //h: key对应的哈希值
    //idx: key对应的索引位置
    uint64_t h, idx;
    //*he: idx对应的哈希链表节点
    //*prevHe: 遍历*he时对应的前节点
    dictEntry *he, *prevHe;
    //哈希表d对应的表0和表1
    int table;

    //如果哈希表1和表2的已使用元素都为0说明哈希表为空，直接返回null
    if (d->ht[0].used == 0 && d->ht[1].used == 0) return NULL;

    //若哈希表处于重哈希过程中，执行重哈希方法
    if (dictIsRehashing(d)) _dictRehashStep(d);

    //计算新key对应的哈希值
    h = dictHashKey(d, key);

    //遍历表0和表1检查是否需要删除元素
    for (table = 0; table <= 1; table++) {
        //计算对应的索引位置
        //例如：h为5，哈希表的大小初始化为4，sizemask则为size-1
        //故有 h&sizemask = 2，所以该键值对就是存放在索引位置为2的地方
        idx = h & d->ht[table].sizemask;
        //找到对应位置哈希链表节点
        he = d->ht[table].table[idx];

        //前置节点
        prevHe = NULL;
        //若存在哈希链表节点
        while(he) {
            //若key相同，执行删除动作
            if (key==he->key || dictCompareKeys(d, key, he->key)) {
                /* Unlink the element from the list
                 * 从链表中移除该元素
                 * */
                //判断是否存在前置节点
                if (prevHe)
                    //将前置节点的下一个节点指针指向当前节点的下一个节点
                    prevHe->next = he->next;
                else
                    //没有前置节点 直接将当前节点的下一个节点覆盖到表中
                    d->ht[table].table[idx] = he->next;
                //是否要释放内存 key和val
                if (!nofree) {
                    //哈希表释放
                    dictFreeKey(d, he);
                    dictFreeVal(d, he);
                    //空间释放
                    zfree(he);
                }
                //删除成功已使用元素
                d->ht[table].used--;
                return he;
            }
            //赋值前置节点 prevHe的值
            prevHe = he;
            //继续往下遍历
            he = he->next;
        }
        //如果哈希表不在重哈希过程中，说明元素都在一个表中，直接返回
        if (!dictIsRehashing(d)) break;
    }
    //若没有发现相同key的元素直接返回null
    return NULL; /* not found */
}

/* Remove an element, returning DICT_OK on success or DICT_ERR if the
 * element was not found.
 * 移除元素，成功返回 DICT_OK ，若元素不存在返回 DICT_ERR
 * */
int dictDelete(dict *ht, const void *key) {
    //删除节点并且释放内存
    return dictGenericDelete(ht,key,0) ? DICT_OK : DICT_ERR;
}

/* Remove an element from the table, but without actually releasing
 * the key, value and dictionary entry. The dictionary entry is returned
 * if the element was found (and unlinked from the table), and the user
 * should later call `dictFreeUnlinkedEntry()` with it in order to release it.
 * Otherwise if the key is not found, NULL is returned.
 * 从表中移除元素，但不真正的释放该key、值和字典节点。
 * 如果元素被找到返回字典节点，且用户应该后续调用dictFreeUnlinkedEntry按顺序释放它。
 * 否则如果key没有被找到将返回null
 *
 * This function is useful when we want to remove something from the hash
 * table but want to use its value before actually deleting the entry.
 * Without this function the pattern would require two lookups:
 * 当我们想移除哈希表中的一些元素但是在真正删除节点前使用它的值时是非常有用的。
 * 如果没有这个函数，模式将需要两次查找
 *
 *  entry = dictFind(...);
 *  // Do something with entry
 *  dictDelete(dictionary,entry);
 *
 * Thanks to this function it is possible to avoid this, and use
 * instead:
 *
 * entry = dictUnlink(dictionary,entry);
 * // Do something with entry
 * dictFreeUnlinkedEntry(entry); // <- This does not need to lookup again.
 */
dictEntry *dictUnlink(dict *ht, const void *key) {
    //删除节点并且不释放内存
    return dictGenericDelete(ht,key,1);
}

/* You need to call this function to really free the entry after a call
 * to dictUnlink(). It's safe to call this function with 'he' = NULL. */
void dictFreeUnlinkedEntry(dict *d, dictEntry *he) {
    if (he == NULL) return;
    dictFreeKey(d, he);
    dictFreeVal(d, he);
    zfree(he);
}

/* Destroy an entire dictionary
 * 销毁整个字典
 * *d: 哈希表结构
 * *ht: 哈希表
 * callback:？
 * */
int _dictClear(dict *d, dictht *ht, void(callback)(void *)) {
    unsigned long i;

    /* Free all the elements
     * 释放所有元素
     * */
    //只要存在元素则遍历
    for (i = 0; i < ht->size && ht->used > 0; i++) {
        //当前遍历的哈希链表节点以及下一个节点
        dictEntry *he, *nextHe;

        if (callback && (i & 65535) == 0) callback(d->privdata);
        //如果哈希表为null 继续循环
        if ((he = ht->table[i]) == NULL) continue;
        //若存在当前哈希链表节点
        while(he) {
            //保留当前节点的下一个节点
            nextHe = he->next;
            //释放当前节点的key
            dictFreeKey(d, he);
            //释放当前节点的value
            dictFreeVal(d, he);
            //释放当前节点
            zfree(he);
            //可用节点减1
            ht->used--;
            //继续处理下一个节点
            he = nextHe;
        }
    }
    /* Free the table and the allocated cache structure
     * 释放哈希表和已分配的缓存结构
     * */
    zfree(ht->table);
    /* Re-initialize the table
     * 重新初始化哈希表
     * */
    _dictReset(ht);
    return DICT_OK; /* never fails */
}

/* Clear & Release the hash table
 * 清空或释放哈希表
 * */
void dictRelease(dict *d)
{
    //清空表0
    _dictClear(d,&d->ht[0],NULL);
    //清空表1
    _dictClear(d,&d->ht[1],NULL);
    //释放内存
    zfree(d);
}

//根据key查找元素
dictEntry *dictFind(dict *d, const void *key)
{
    dictEntry *he;
    uint64_t h, idx, table;

    //如果哈希表为空直接返回null
    if (dictSize(d) == 0) return NULL; /* dict is empty */
    //如果哈希表处于重哈希过程中执行渐进式哈希
    if (dictIsRehashing(d)) _dictRehashStep(d);
    //计算该key对应的哈希值
    h = dictHashKey(d, key);
    //遍历两个哈希表
    for (table = 0; table <= 1; table++) {
        //计算当前表中 哈希对应的索引位置
        idx = h & d->ht[table].sizemask;
        //取出对应索引位置的哈希链表节点
        he = d->ht[table].table[idx];
        //遍历链表节点
        while(he) {
            //若存在哈希值相同的key则返回
            if (key==he->key || dictCompareKeys(d, key, he->key))
                return he;
            //继续往下遍历
            he = he->next;
        }
        //若遍历过程中发现重哈希结束，直接返回null  说明元素都在另一个表中
        if (!dictIsRehashing(d)) return NULL;
    }
    return NULL;
}

void *dictFetchValue(dict *d, const void *key) {
    dictEntry *he;

    he = dictFind(d,key);
    return he ? dictGetVal(he) : NULL;
}

/* A fingerprint is a 64 bit number that represents the state of the dictionary
 * at a given time, it's just a few dict properties xored together.
 * When an unsafe iterator is initialized, we get the dict fingerprint, and check
 * the fingerprint again when the iterator is released.
 * If the two fingerprints are different it means that the user of the iterator
 * performed forbidden operations against the dictionary while iterating. */
unsigned long long dictFingerprint(dict *d) {
    unsigned long long integers[6], hash = 0;
    int j;

    integers[0] = (long) d->ht[0].table;
    integers[1] = d->ht[0].size;
    integers[2] = d->ht[0].used;
    integers[3] = (long) d->ht[1].table;
    integers[4] = d->ht[1].size;
    integers[5] = d->ht[1].used;

    /* We hash N integers by summing every successive integer with the integer
     * hashing of the previous sum. Basically:
     *
     * Result = hash(hash(hash(int1)+int2)+int3) ...
     *
     * This way the same set of integers in a different order will (likely) hash
     * to a different number. */
    for (j = 0; j < 6; j++) {
        hash += integers[j];
        /* For the hashing step we use Tomas Wang's 64 bit integer hash. */
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}

dictIterator *dictGetIterator(dict *d)
{
    dictIterator *iter = zmalloc(sizeof(*iter));

    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;
    return iter;
}

dictIterator *dictGetSafeIterator(dict *d) {
    dictIterator *i = dictGetIterator(d);

    i->safe = 1;
    return i;
}

dictEntry *dictNext(dictIterator *iter)
{
    while (1) {
        if (iter->entry == NULL) {
            dictht *ht = &iter->d->ht[iter->table];
            if (iter->index == -1 && iter->table == 0) {
                if (iter->safe)
                    dictPauseRehashing(iter->d);
                else
                    iter->fingerprint = dictFingerprint(iter->d);
            }
            iter->index++;
            if (iter->index >= (long) ht->size) {
                if (dictIsRehashing(iter->d) && iter->table == 0) {
                    iter->table++;
                    iter->index = 0;
                    ht = &iter->d->ht[1];
                } else {
                    break;
                }
            }
            iter->entry = ht->table[iter->index];
        } else {
            iter->entry = iter->nextEntry;
        }
        if (iter->entry) {
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }
    return NULL;
}

void dictReleaseIterator(dictIterator *iter)
{
    if (!(iter->index == -1 && iter->table == 0)) {
        if (iter->safe)
            dictResumeRehashing(iter->d);
        else
            assert(iter->fingerprint == dictFingerprint(iter->d));
    }
    zfree(iter);
}

/* Return a random entry from the hash table. Useful to
 * implement randomized algorithms */
dictEntry *dictGetRandomKey(dict *d)
{
    dictEntry *he, *orighe;
    unsigned long h;
    int listlen, listele;

    if (dictSize(d) == 0) return NULL;
    if (dictIsRehashing(d)) _dictRehashStep(d);
    if (dictIsRehashing(d)) {
        do {
            /* We are sure there are no elements in indexes from 0
             * to rehashidx-1 */
            h = d->rehashidx + (randomULong() % (dictSlots(d) - d->rehashidx));
            he = (h >= d->ht[0].size) ? d->ht[1].table[h - d->ht[0].size] :
                                      d->ht[0].table[h];
        } while(he == NULL);
    } else {
        do {
            h = randomULong() & d->ht[0].sizemask;
            he = d->ht[0].table[h];
        } while(he == NULL);
    }

    /* Now we found a non empty bucket, but it is a linked
     * list and we need to get a random element from the list.
     * The only sane way to do so is counting the elements and
     * select a random index. */
    listlen = 0;
    orighe = he;
    while(he) {
        he = he->next;
        listlen++;
    }
    listele = random() % listlen;
    he = orighe;
    while(listele--) he = he->next;
    return he;
}

/* This function samples the dictionary to return a few keys from random
 * locations.
 *
 * It does not guarantee to return all the keys specified in 'count', nor
 * it does guarantee to return non-duplicated elements, however it will make
 * some effort to do both things.
 *
 * Returned pointers to hash table entries are stored into 'des' that
 * points to an array of dictEntry pointers. The array must have room for
 * at least 'count' elements, that is the argument we pass to the function
 * to tell how many random elements we need.
 *
 * The function returns the number of items stored into 'des', that may
 * be less than 'count' if the hash table has less than 'count' elements
 * inside, or if not enough elements were found in a reasonable amount of
 * steps.
 *
 * Note that this function is not suitable when you need a good distribution
 * of the returned items, but only when you need to "sample" a given number
 * of continuous elements to run some kind of algorithm or to produce
 * statistics. However the function is much faster than dictGetRandomKey()
 * at producing N elements. */
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count) {
    unsigned long j; /* internal hash table id, 0 or 1. */
    unsigned long tables; /* 1 or 2 tables? */
    unsigned long stored = 0, maxsizemask;
    unsigned long maxsteps;

    if (dictSize(d) < count) count = dictSize(d);
    maxsteps = count*10;

    /* Try to do a rehashing work proportional to 'count'. */
    for (j = 0; j < count; j++) {
        if (dictIsRehashing(d))
            _dictRehashStep(d);
        else
            break;
    }

    tables = dictIsRehashing(d) ? 2 : 1;
    maxsizemask = d->ht[0].sizemask;
    if (tables > 1 && maxsizemask < d->ht[1].sizemask)
        maxsizemask = d->ht[1].sizemask;

    /* Pick a random point inside the larger table. */
    unsigned long i = randomULong() & maxsizemask;
    unsigned long emptylen = 0; /* Continuous empty entries so far. */
    while(stored < count && maxsteps--) {
        for (j = 0; j < tables; j++) {
            /* Invariant of the dict.c rehashing: up to the indexes already
             * visited in ht[0] during the rehashing, there are no populated
             * buckets, so we can skip ht[0] for indexes between 0 and idx-1. */
            if (tables == 2 && j == 0 && i < (unsigned long) d->rehashidx) {
                /* Moreover, if we are currently out of range in the second
                 * table, there will be no elements in both tables up to
                 * the current rehashing index, so we jump if possible.
                 * (this happens when going from big to small table). */
                if (i >= d->ht[1].size)
                    i = d->rehashidx;
                else
                    continue;
            }
            if (i >= d->ht[j].size) continue; /* Out of range for this table. */
            dictEntry *he = d->ht[j].table[i];

            /* Count contiguous empty buckets, and jump to other
             * locations if they reach 'count' (with a minimum of 5). */
            if (he == NULL) {
                emptylen++;
                if (emptylen >= 5 && emptylen > count) {
                    i = randomULong() & maxsizemask;
                    emptylen = 0;
                }
            } else {
                emptylen = 0;
                while (he) {
                    /* Collect all the elements of the buckets found non
                     * empty while iterating. */
                    *des = he;
                    des++;
                    he = he->next;
                    stored++;
                    if (stored == count) return stored;
                }
            }
        }
        i = (i+1) & maxsizemask;
    }
    return stored;
}

/* This is like dictGetRandomKey() from the POV of the API, but will do more
 * work to ensure a better distribution of the returned element.
 *
 * This function improves the distribution because the dictGetRandomKey()
 * problem is that it selects a random bucket, then it selects a random
 * element from the chain in the bucket. However elements being in different
 * chain lengths will have different probabilities of being reported. With
 * this function instead what we do is to consider a "linear" range of the table
 * that may be constituted of N buckets with chains of different lengths
 * appearing one after the other. Then we report a random element in the range.
 * In this way we smooth away the problem of different chain lengths. */
#define GETFAIR_NUM_ENTRIES 15
dictEntry *dictGetFairRandomKey(dict *d) {
    dictEntry *entries[GETFAIR_NUM_ENTRIES];
    unsigned int count = dictGetSomeKeys(d,entries,GETFAIR_NUM_ENTRIES);
    /* Note that dictGetSomeKeys() may return zero elements in an unlucky
     * run() even if there are actually elements inside the hash table. So
     * when we get zero, we call the true dictGetRandomKey() that will always
     * yield the element if the hash table has at least one. */
    if (count == 0) return dictGetRandomKey(d);
    unsigned int idx = rand() % count;
    return entries[idx];
}

/* Function to reverse bits. Algorithm from:
 * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel
 * 二进制反转  即 0000 0111  ->  1110 0000
 * */
static unsigned long rev(unsigned long v) {
    unsigned long s = CHAR_BIT * sizeof(v); // bit size; must be power of 2
    unsigned long mask = ~0UL;
    while ((s >>= 1) > 0) {
        mask ^= (mask << s);
        v = ((v >> s) & mask) | ((v << s) & ~mask);
    }
    return v;
}

/* dictScan() is used to iterate over the elements of a dictionary.
 *
 * dictScan 用于遍历字典中的元素
 *
 * Iterating works the following way:
 * 迭代的工作方式如下
 *
 * 1) Initially you call the function using a cursor (v) value of 0.
 * 1) 最初 你使用游标 0 来调用函数
 * 2) The function performs one step of the iteration, and returns the
 *    new cursor value you must use in the next call.
 * 2) 该函数执行迭代的一个步骤，返回你必须在下一个调用使用的新游标
 * 3) When the returned cursor is 0, the iteration is complete.
 * 3) 当返回的游标为0，迭代就已经完成
 *
 * The function guarantees all elements present in the
 * dictionary get returned between the start and end of the iteration.
 * However it is possible some elements get returned multiple times.
 * 该函数保证在迭代开始和结束之前返回字典中存在的所有元素。
 * 但是有可能一些元素返回多次
 *
 * For every element returned, the callback argument 'fn' is
 * called with 'privdata' as first argument and the dictionary entry
 * 'de' as second argument.
 * 对于每个返回的元素，回调参数 fn 被调用，private 作为第一个参数，字典条目 de 作为第二个参数
 *
 * HOW IT WORKS.
 * 他是如何工作的
 *
 * The iteration algorithm was designed by Pieter Noordhuis.
 * The main idea is to increment a cursor starting from the higher order
 * bits. That is, instead of incrementing the cursor normally, the bits
 * of the cursor are reversed, then the cursor is incremented, and finally
 * the bits are reversed again.
 * 这个迭代算法是由 Pieter Noordhuis 设计的。
 * 主要思想是将游标从二进制高位递增。也就是说，不是正常的增加游标，而是对光标的位进行反转，然后对光标进行递增，最后再次对位进行反转。
 *
 * This strategy is needed because the hash table may be resized between
 * iteration calls.
 * 这个策略是必须的，因为哈希表可能会在迭代调用之间调整大小
 *
 * dict.c hash tables are always power of two in size, and they
 * use chaining, so the position of an element in a given table is given
 * by computing the bitwise AND between Hash(key) and SIZE-1
 * (where SIZE-1 is always the mask that is equivalent to taking the rest
 *  of the division between the Hash of the key and SIZE).
 * 哈希表的大小总是2的幂次，并且他们使用链，因此给定表中元素的位置是通过计算 哈希key和 size-1 之间的按位与来给出的。
 * （其中 size-1 总是掩码，相当于取键的哈希值和size之间的剩余部分）
 *
 * For example if the current hash table size is 16, the mask is
 * (in binary) 1111. The position of a key in the hash table will always be
 * the last four bits of the hash output, and so forth.
 * 例如当前哈希表大小是16，二进制为1111。
 * 键在哈希表中的位置始终是哈希输出的最后四位，以此类推
 *
 * WHAT HAPPENS IF THE TABLE CHANGES IN SIZE?
 * 如果表的大小改变会发生什么？
 *
 * If the hash table grows, elements can go anywhere in one multiple of
 * the old bucket: for example let's say we already iterated with
 * a 4 bit cursor 1100 (the mask is 1111 because hash table size = 16).
 * 如果哈希表增长，元素可以在旧桶的一个倍数内移动到任何地方。
 * 举例来说：假设我们已经迭代了一个 4 位游标 1100（掩码是1111，因为哈希表大小为16）
 *
 * If the hash table will be resized to 64 elements, then the new mask will
 * be 111111. The new buckets you obtain by substituting in ??1100
 * with either 0 or 1 can be targeted only by keys we already visited
 * when scanning the bucket 1100 in the smaller hash table.
 * 如果哈希表大小调整到64个元素，新掩码将为 111111。
 * 通过将 ??1100 替换为0或1而获得的新桶只能通过我们在较小的哈希表中扫描 1100 时已经访问过的键来定位
 *
 * By iterating the higher bits first, because of the inverted counter, the
 * cursor does not need to restart if the table size gets bigger. It will
 * continue iterating using cursors without '1100' at the end, and also
 * without any other combination of the final 4 bits already explored.
 * 通过首先迭代较高的位，由于反向计数器，如果表大小变大，游标不需要重新启动。
 * 它将继续使用游标进行迭代，最后没有 1100 ，也没有最后4位的任何其他组合。
 *
 * Similarly when the table size shrinks over time, for example going from
 * 16 to 8, if a combination of the lower three bits (the mask for size 8
 * is 111) were already completely explored, it would not be visited again
 * because we are sure we tried, for example, both 0111 and 1111 (all the
 * variations of the higher bit) so we don't need to test it again.
 * 同样，当表大小随着时间的推移而缩小时，例如从16到8，如果已经完全探索了较低三位的组合（大小为8的掩码是111），则不会再次访问它，因为我们确信已经尝试过，
 * 例如 0111和1111 （较高位的所有变体），因此我们不会再尝试它
 *
 * WAIT... YOU HAVE *TWO* TABLES DURING REHASHING!
 * 等等，你有两个哈希表在重哈希
 *
 * Yes, this is true, but we always iterate the smaller table first, then
 * we test all the expansions of the current cursor into the larger
 * table. For example if the current cursor is 101 and we also have a
 * larger table of size 16, we also test (0)101 and (1)101 inside the larger
 * table. This reduces the problem back to having only one table, where
 * the larger one, if it exists, is just an expansion of the smaller one.
 * 是的，这是真的，但是我们总是先迭代较小的表，然后我们测试当前游标在较大表中的所有扩展。
 * 例如当前游标是 101 而且我们有大表的大小为16，我们在大表中验证 0101 和 1101.
 * 这将问题减少到只有一个表，其中较大的表（如果存在的话）只是较小表的扩展
 *
 *
 * LIMITATIONS
 * 限制
 *
 * This iterator is completely stateless, and this is a huge advantage,
 * including no additional memory used.
 * 这个迭代器是完全无状态的，这是一个巨大的优势，包括不使用额外的内存
 *
 * The disadvantages resulting from this design are:
 * 这种设计的缺点是
 *
 * 1) It is possible we return elements more than once. However this is usually
 *    easy to deal with in the application level.
 * 1) 我们可能多次返回相同元素。但是它通常在程序级别易于处理
 * 2) The iterator must return multiple elements per call, as it needs to always
 *    return all the keys chained in a given bucket, and all the expansions, so
 *    we are sure we don't miss keys moving during rehashing.
 * 2) 迭代器每次调用必须返回多个元素，因为它总是需要返回给定桶中链接的所有键，以及所有的展开，所以我们确保在重新散列期间不会错过键的移动
 * 3) The reverse cursor is somewhat hard to understand at first, but this
 *    comment is supposed to help.
 * 3) 反向光标一开始有点难以理解，但是这条注释应该会有所帮助
 */
unsigned long dictScan(dict *d,
                       unsigned long v, //对应的游标 从0开始
                       dictScanFunction *fn,
                       dictScanBucketFunction* bucketfn,
                       void *privdata)
{
    //初始化 表0地址和表1地址
    dictht *t0, *t1;
    //初始化当前哈希节点和下一个哈希节点
    const dictEntry *de, *next;
    //初始化 表0和表1对应的掩码
    unsigned long m0, m1;

    //如果哈希表数量为0值返回
    if (dictSize(d) == 0) return 0;

    /* This is needed in case the scan callback tries to do dictFind or alike.
     * 这在扫描回调试图执行 dictFind 或类型操作时是需要的。
     * */
    //暂停重哈希
    dictPauseRehashing(d);

    //判断哈希表是在重哈希过程中
    //若不处于重哈希过程中直接根据 v 找到需要迭代的 Bucket索引，针对该bucket中链表中的所有节点，调用用户提供的fn函数
    if (!dictIsRehashing(d)) {
        //t0 指向表
        t0 = &(d->ht[0]);
        //表0对应的掩码
        m0 = t0->sizemask;

        /* Emit entries at cursor
         * 在游标处发出项
         * */
        //执行对应bucketfn方法
        if (bucketfn) bucketfn(privdata, &t0->table[v & m0]);
        //取游标对应索引位置的链表节点
        de = t0->table[v & m0];
        //遍历链表
        while (de) {
            //取下一个哈希节点
            next = de->next;
            //执行对应fn方法
            fn(privdata, de); //将这个链表里的数据全部入队，准备返回客户端
            //指向下一个哈希节点
            de = next;
        }

        /* Set unmasked bits so incrementing the reversed cursor
         * operates on the masked bits
         * 设置非掩码位，以增加反向游标对掩码位的操作
         * */
        //用于保留 v 的低n位数，其余位全置位1
        v |= ~m0;

        /* Increment the reverse cursor
         * 增加反向光标
         * */
        //主要作用就是游标高位+1
        //游标倒置，将v的二进制位进行翻转，所以，v的低n位数成了高n位数，并且进行翻转
        v = rev(v);
        //游标加1，低位加1
        v++;
        //游标继续倒置，导致高位加1
        v = rev(v);

    } else {
        //表0的位置
        t0 = &d->ht[0];
        //表1的位置
        t1 = &d->ht[1];

        /* Make sure t0 is the smaller and t1 is the bigger table
         * 保证 t0 的数量小于 t1 的数量
         * */
        //保证t0数量最小，为后续遍历做准备，若t0大于t1，会导致部分数据漏掉
        if (t0->size > t1->size) {
            t0 = &d->ht[1];
            t1 = &d->ht[0];
        }

        //表0 对应的掩码
        m0 = t0->sizemask;
        //表1 对应的掩码
        m1 = t1->sizemask;

        /* Emit entries at cursor */
        if (bucketfn) bucketfn(privdata, &t0->table[v & m0]);
        //取哈希对应索引位置的链表节点
        de = t0->table[v & m0];
        //遍历链表 同上
        while (de) {
            //取下一个哈希节点
            next = de->next;
            //执行对应fn方法
            fn(privdata, de);
            //指向下一个哈希节点
            de = next;
        }

        /* Iterate over indices in larger table that are the expansion
         * of the index pointed to by the cursor in the smaller table
         * 迭代较大表中的索引，这些索引是游标在较小表中所指向的索引的扩展
         * */
        //同遍历表0
        do {
            /* Emit entries at cursor */
            if (bucketfn) bucketfn(privdata, &t1->table[v & m1]);
            de = t1->table[v & m1];
            while (de) {
                next = de->next;
                fn(privdata, de);
                de = next;
            }

            /* Increment the reverse cursor not covered by the smaller mask.*/
            v |= ~m1;
            v = rev(v);
            v++;
            v = rev(v);

            /* Continue while bits covered by mask difference is non-zero
             * 当掩码差所覆盖的位不为零时继续执行
             * */
        } while (v & (m0 ^ m1)); //表示直到 v 的低m1-m0位 到 低m1位（指 v 的高位都没有1） 之间全为0为止
    }

    //释放重哈希标志
    dictResumeRehashing(d);

    return v;
}

/* ------------------------- private functions ------------------------------ */

/* Because we may need to allocate huge memory chunk at once when dict
 * expands, we will check this allocation is allowed or not if the dict
 * type has expandAllowed member function.
 * 因为当哈希扩容时，我们需要同时分配大量内存，我们需要检查分配是否允许，如果字典类型有 expandAllowed 成员函数
 * */
static int dictTypeExpandAllowed(dict *d) {
    if (d->type->expandAllowed == NULL) return 1;
    return d->type->expandAllowed(
                    _dictNextPower(d->ht[0].used + 1) * sizeof(dictEntry*),
                    (double)d->ht[0].used / d->ht[0].size);
}

/* Expand the hash table if needed
 * 如果需要的话扩容哈希表
 * */
static int _dictExpandIfNeeded(dict *d)
{
    /* Incremental rehashing already in progress. Return.
     * 增量散列已经在进行中，直接返回
     * */
    if (dictIsRehashing(d)) return DICT_OK;

    /* If the hash table is empty expand it to the initial size.
     * 如果哈希表为空，扩容到初始大小
     * */
    if (d->ht[0].size == 0) return dictExpand(d, DICT_HT_INITIAL_SIZE);

    /* If we reached the 1:1 ratio, and we are allowed to resize the hash
     * table (global setting) or we should avoid it but the ratio between
     * elements/buckets is over the "safe" threshold, we resize doubling
     * the number of buckets.
     * 如果我们达到1比1的比例，且允许重新设置哈希表的大小，
     * 或这我们应该禁止重新调整 但是 数量和桶的比例在安全的阈值，我们将大小设置为 两倍的桶的大小
     * */
    //当前哈希类型是否允许扩容
    if (!dictTypeExpandAllowed(d))
        return DICT_OK;
    //两张情况允许扩展
    //1. 允许重新调整且表0的已使用数量和大小相同
    //2. 不运行重新调整且大于安全阈值
    if ((dict_can_resize == DICT_RESIZE_ENABLE &&
         d->ht[0].used >= d->ht[0].size) ||
        (dict_can_resize != DICT_RESIZE_FORBID &&
         d->ht[0].used / d->ht[0].size > dict_force_resize_ratio))
    {
        //执行扩容操作
        return dictExpand(d, d->ht[0].used + 1);
    }
    return DICT_OK;
}

/* Our hash table capability is a power of two
 * 我们哈希表的数量都是2的幂数
 * */
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITIAL_SIZE;

    //如果超长则新增后返回
    if (size >= LONG_MAX) return LONG_MAX + 1LU;
    //数量一直累乘2
    while(1) {
        if (i >= size)
            return i;
        i *= 2;
    }
}

/* Returns the index of a free slot that can be populated with
 * a hash entry for the given 'key'.
 * If the key already exists, -1 is returned
 * and the optional output parameter may be filled.
 * 返回空闲槽位的索引，它可以用给定 key 的散列条目填充
 * 如果 key 已经存在，直接返回-1， 且 可选的输出参数可能被填充。
 *
 * Note that if we are in the process of rehashing the hash table, the
 * index is always returned in the context of the second (new) hash table.
 * 请注意 如果处于正在重新哈希的过程中时，这个索引总是在新哈希表的上下文中返回
 * */
static long _dictKeyIndex(dict *d, const void *key, uint64_t hash, dictEntry **existing)
{
    unsigned long idx, table;
    dictEntry *he;
    if (existing) *existing = NULL;

    /* Expand the hash table if needed
     * 如果需要的话扩容哈希表
     * */
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        //扩容失败返回-1
        return -1;
    //从表0和表1中根据哈希值找到对应的值
    for (table = 0; table <= 1; table++) {
        //计算哈希位置
        idx = hash & d->ht[table].sizemask;
        /* Search if this slot does not already contain the given key
         * 检查这个槽位是否已经存在给定的key
         * */
        //找到当前表中指定哈希位置的节点
        he = d->ht[table].table[idx];
        //遍历哈希表数组，如果 对应键key 相同 或值相同则返回-1，不存在则返回后计算的哈希值
        while(he) {
            if (key==he->key || dictCompareKeys(d, key, he->key)) {
                if (existing) *existing = he;
                return -1;
            }
            he = he->next;
        }
        if (!dictIsRehashing(d)) break;
    }
    return idx;
}

void dictEmpty(dict *d, void(callback)(void*)) {
    _dictClear(d,&d->ht[0],callback);
    _dictClear(d,&d->ht[1],callback);
    d->rehashidx = -1;
    d->pauserehash = 0;
}

void dictSetResizeEnabled(dictResizeEnable enable) {
    dict_can_resize = enable;
}

uint64_t dictGetHash(dict *d, const void *key) {
    return dictHashKey(d, key);
}

/* Finds the dictEntry reference by using pointer and pre-calculated hash.
 * oldkey is a dead pointer and should not be accessed.
 * the hash value should be provided using dictGetHash.
 * no string / key comparison is performed.
 * return value is the reference to the dictEntry if found, or NULL if not found. */
dictEntry **dictFindEntryRefByPtrAndHash(dict *d, const void *oldptr, uint64_t hash) {
    dictEntry *he, **heref;
    unsigned long idx, table;

    if (dictSize(d) == 0) return NULL; /* dict is empty */
    for (table = 0; table <= 1; table++) {
        idx = hash & d->ht[table].sizemask;
        heref = &d->ht[table].table[idx];
        he = *heref;
        while(he) {
            if (oldptr==he->key)
                return heref;
            heref = &he->next;
            he = *heref;
        }
        if (!dictIsRehashing(d)) return NULL;
    }
    return NULL;
}

/* ------------------------------- Debugging ---------------------------------*/

#define DICT_STATS_VECTLEN 50
size_t _dictGetStatsHt(char *buf, size_t bufsize, dictht *ht, int tableid) {
    unsigned long i, slots = 0, chainlen, maxchainlen = 0;
    unsigned long totchainlen = 0;
    unsigned long clvector[DICT_STATS_VECTLEN];
    size_t l = 0;

    if (ht->used == 0) {
        return snprintf(buf,bufsize,
            "Hash table %d stats (%s):\n"
            "No stats available for empty dictionaries\n",
            tableid, (tableid == 0) ? "main hash table" : "rehashing target");
    }

    /* Compute stats. */
    for (i = 0; i < DICT_STATS_VECTLEN; i++) clvector[i] = 0;
    for (i = 0; i < ht->size; i++) {
        dictEntry *he;

        if (ht->table[i] == NULL) {
            clvector[0]++;
            continue;
        }
        slots++;
        /* For each hash entry on this slot... */
        chainlen = 0;
        he = ht->table[i];
        while(he) {
            chainlen++;
            he = he->next;
        }
        clvector[(chainlen < DICT_STATS_VECTLEN) ? chainlen : (DICT_STATS_VECTLEN-1)]++;
        if (chainlen > maxchainlen) maxchainlen = chainlen;
        totchainlen += chainlen;
    }

    /* Generate human readable stats. */
    l += snprintf(buf+l,bufsize-l,
        "Hash table %d stats (%s):\n"
        " table size: %lu\n"
        " number of elements: %lu\n"
        " different slots: %lu\n"
        " max chain length: %lu\n"
        " avg chain length (counted): %.02f\n"
        " avg chain length (computed): %.02f\n"
        " Chain length distribution:\n",
        tableid, (tableid == 0) ? "main hash table" : "rehashing target",
        ht->size, ht->used, slots, maxchainlen,
        (float)totchainlen/slots, (float)ht->used/slots);

    for (i = 0; i < DICT_STATS_VECTLEN-1; i++) {
        if (clvector[i] == 0) continue;
        if (l >= bufsize) break;
        l += snprintf(buf+l,bufsize-l,
            "   %s%ld: %ld (%.02f%%)\n",
            (i == DICT_STATS_VECTLEN-1)?">= ":"",
            i, clvector[i], ((float)clvector[i]/ht->size)*100);
    }

    /* Unlike snprintf(), return the number of characters actually written. */
    if (bufsize) buf[bufsize-1] = '\0';
    return strlen(buf);
}

void dictGetStats(char *buf, size_t bufsize, dict *d) {
    size_t l;
    char *orig_buf = buf;
    size_t orig_bufsize = bufsize;

    l = _dictGetStatsHt(buf,bufsize,&d->ht[0],0);
    buf += l;
    bufsize -= l;
    if (dictIsRehashing(d) && bufsize > 0) {
        _dictGetStatsHt(buf,bufsize,&d->ht[1],1);
    }
    /* Make sure there is a NULL term at the end. */
    if (orig_bufsize) orig_buf[orig_bufsize-1] = '\0';
}

/* ------------------------------- Benchmark ---------------------------------*/

#ifdef REDIS_TEST

uint64_t hashCallback(const void *key) {
    return dictGenHashFunction((unsigned char*)key, strlen((char*)key));
}

int compareCallback(void *privdata, const void *key1, const void *key2) {
    int l1,l2;
    DICT_NOTUSED(privdata);

    l1 = strlen((char*)key1);
    l2 = strlen((char*)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

void freeCallback(void *privdata, void *val) {
    DICT_NOTUSED(privdata);

    zfree(val);
}

char *stringFromLongLong(long long value) {
    char buf[32];
    int len;
    char *s;

    len = sprintf(buf,"%lld",value);
    s = zmalloc(len+1);
    memcpy(s, buf, len);
    s[len] = '\0';
    return s;
}

dictType BenchmarkDictType = {
    hashCallback,
    NULL,
    NULL,
    compareCallback,
    freeCallback,
    NULL,
    NULL
};

#define start_benchmark() start = timeInMilliseconds()
#define end_benchmark(msg) do { \
    elapsed = timeInMilliseconds()-start; \
    printf(msg ": %ld items in %lld ms\n", count, elapsed); \
} while(0)

/* ./redis-server test dict [<count> | --accurate] */
int dictTest(int argc, char **argv, int accurate) {
    long j;
    long long start, elapsed;
    dict *dict = dictCreate(&BenchmarkDictType,NULL);
    long count = 0;

    if (argc == 4) {
        if (accurate) {
            count = 5000000;
        } else {
            count = strtol(argv[3],NULL,10);
        }
    } else {
        count = 5000;
    }

    start_benchmark();
    for (j = 0; j < count; j++) {
        int retval = dictAdd(dict,stringFromLongLong(j),(void*)j);
        assert(retval == DICT_OK);
    }
    end_benchmark("Inserting");
    assert((long)dictSize(dict) == count);

    /* Wait for rehashing. */
    while (dictIsRehashing(dict)) {
        dictRehashMilliseconds(dict,100);
    }

    start_benchmark();
    for (j = 0; j < count; j++) {
        char *key = stringFromLongLong(j);
        dictEntry *de = dictFind(dict,key);
        assert(de != NULL);
        zfree(key);
    }
    end_benchmark("Linear access of existing elements");

    start_benchmark();
    for (j = 0; j < count; j++) {
        char *key = stringFromLongLong(j);
        dictEntry *de = dictFind(dict,key);
        assert(de != NULL);
        zfree(key);
    }
    end_benchmark("Linear access of existing elements (2nd round)");

    start_benchmark();
    for (j = 0; j < count; j++) {
        char *key = stringFromLongLong(rand() % count);
        dictEntry *de = dictFind(dict,key);
        assert(de != NULL);
        zfree(key);
    }
    end_benchmark("Random access of existing elements");

    start_benchmark();
    for (j = 0; j < count; j++) {
        dictEntry *de = dictGetRandomKey(dict);
        assert(de != NULL);
    }
    end_benchmark("Accessing random keys");

    start_benchmark();
    for (j = 0; j < count; j++) {
        char *key = stringFromLongLong(rand() % count);
        key[0] = 'X';
        dictEntry *de = dictFind(dict,key);
        assert(de == NULL);
        zfree(key);
    }
    end_benchmark("Accessing missing");

    start_benchmark();
    for (j = 0; j < count; j++) {
        char *key = stringFromLongLong(j);
        int retval = dictDelete(dict,key);
        assert(retval == DICT_OK);
        key[0] += 17; /* Change first number to letter. */
        retval = dictAdd(dict,key,(void*)j);
        assert(retval == DICT_OK);
    }
    end_benchmark("Removing and adding");
    dictRelease(dict);
    return 0;
}
#endif
